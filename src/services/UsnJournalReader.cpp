#include "services/UsnJournalReader.h"
#include "services/Logger.h"

#include <QHash>
#include <QString>

#include <future>
#include <memory>
#include <vector>

namespace DiskOrganizer {

bool UsnJournalReader::isNtfs(wchar_t driveLetter) {
    const QString root = QString(QChar(driveLetter)) + QStringLiteral(":\\");
    wchar_t fsName[MAX_PATH + 1] = {};
    return GetVolumeInformationW(reinterpret_cast<const wchar_t*>(root.utf16()),
                                 nullptr, 0, nullptr, nullptr, nullptr,
                                 fsName, MAX_PATH)
        && wcscmp(fsName, L"NTFS") == 0;
}

bool UsnJournalReader::open(wchar_t driveLetter) {
    close();
    if (!isNtfs(driveLetter)) return false;
    const QString volumePath = QStringLiteral("\\\\.\\%1:").arg(QChar(driveLetter));
    m_volume = CreateFileW(reinterpret_cast<const wchar_t*>(volumePath.utf16()),
                           GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (m_volume == INVALID_HANDLE_VALUE) {
        LOG << "open volume FAILED err=" << GetLastError();
        return false;
    }
    m_drive = driveLetter;
    m_frnPathCache.clear();
    return true;
}

void UsnJournalReader::close() {
    if (m_volume != INVALID_HANDLE_VALUE) {
        CloseHandle(m_volume);
        m_volume = INVALID_HANDLE_VALUE;
    }
    m_drive = 0;
}

namespace {

#pragma pack(push, 1)
struct UsnRecordV2Header {
    DWORD recordLength;
    WORD majorVersion;
    WORD minorVersion;
    DWORDLONG fileReferenceNumber;
    DWORDLONG parentFileReferenceNumber;
    USN usn;
    LARGE_INTEGER timeStamp;
    DWORD reason;
    DWORD sourceInfo;
    DWORD securityId;
    DWORD fileAttributes;
    WORD fileNameLength;   // 字节（UTF-16）
    WORD fileNameOffset;   // 相对记录起始的偏移
};
#pragma pack(pop)

} // namespace

bool UsnJournalReader::enumerateAll(
    const std::function<bool(const Record&)>& onRecord,
    const std::function<bool()>& isCancelled) {
    if (m_volume == INVALID_HANDLE_VALUE || !onRecord) return false;

    // FSCTL_ENUM_USN_DATA：一次 MFT 全量枚举（Everything 同款路线），
    // 每条记录自带 FRN / 父 FRN / 属性 / 文件名。
    MFT_ENUM_DATA_V0 med = {};
    med.StartFileReferenceNumber = 0;
    med.LowUsn = 0;
    med.HighUsn = 0xFFFFFFFFFFFFFFFFULL;

    constexpr DWORD kBufSize = 1 << 20; // 1MB
    std::vector<BYTE> buf(kBufSize);

    // 第一遍：收集 (FRN -> Record)，第二遍再拼路径
    struct Node {
        quint64 parent;
        QString name;
        bool isDir;
    };
    QHash<quint64, Node> nodes;
    nodes.reserve(200000);

    DWORDLONG startFrn = 0;
    while (true) {
        if (isCancelled && isCancelled()) return false;
        med.StartFileReferenceNumber = startFrn;
        DWORD bytesReturned = 0;
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;
        const BOOL ok = DeviceIoControl(m_volume, FSCTL_ENUM_USN_DATA,
                                        &med, sizeof(med),
                                        buf.data(), kBufSize,
                                        &bytesReturned, &ov);
        bool got = ok != FALSE;
        if (!got && GetLastError() == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, 120000);
            got = wait == WAIT_OBJECT_0
                && GetOverlappedResult(m_volume, &ov, &bytesReturned, FALSE);
        }
        CloseHandle(ov.hEvent);
        if (!got) {
            LOG << "USN enum ended err=" << GetLastError() << " nodes=" << nodes.size();
            break; // ERROR_HANDLE_EOF 或其他错误 = 枚举完成
        }
        if (bytesReturned <= sizeof(DWORDLONG)) break;

        DWORDLONG nextFrn = 0;
        memcpy(&nextFrn, buf.data(), sizeof(DWORDLONG));

        DWORD offset = sizeof(DWORDLONG);
        while (offset + sizeof(UsnRecordV2Header) <= bytesReturned) {
            UsnRecordV2Header rec{};
            memcpy(&rec, buf.data() + offset, sizeof(rec));
            if (rec.recordLength == 0 || rec.majorVersion != 2) break;
            if (rec.fileNameLength > 0) {
                Node n;
                n.parent = rec.parentFileReferenceNumber;
                n.name = QString::fromWCharArray(
                    reinterpret_cast<const wchar_t*>(buf.data() + offset + rec.fileNameOffset),
                    rec.fileNameLength / 2);
                n.isDir = (rec.fileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                nodes.insert(rec.fileReferenceNumber, std::move(n));
            }
            offset += rec.recordLength;
        }
        if (nextFrn == startFrn) break; // 无进展，防死循环
        startFrn = nextFrn;
    }

    if (nodes.isEmpty()) return false;

    // 拼路径：缓存父链，避免 O(n*depth) 重复
    auto pathOf = [&](quint64 frn) -> QString {
        QVector<quint64> chain;
        QString path;
        quint64 cur = frn;
        while (true) {
            auto it = m_frnPathCache.find(cur);
            if (it != m_frnPathCache.end()) { path = *it; break; }
            auto nit = nodes.find(cur);
            if (nit == nodes.end()) { path = QString(); break; } // 不在本卷 MFT 中
            chain.append(cur);
            cur = nit->parent;
            if (cur == frn) { path = QString(); break; } // 环保护
        }
        if (path.isEmpty() && !chain.isEmpty()) {
            // chain 尾端是顶层（parent 不在 nodes），逐级下拼
            path = QString(QChar(m_drive)) + QStringLiteral(":");
            for (int i = chain.size() - 1; i >= 0; --i) {
                auto nit = nodes.find(chain[i]);
                path += QLatin1Char('/') + nit->name;
                m_frnPathCache.insert(chain[i], path);
            }
        }
        return path;
    };

    // 目录先注册路径（保证父目录路径可查），再统一回调
    for (auto it = nodes.begin(); it != nodes.end(); ++it)
        if (it->isDir) pathOf(it.key());

    for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it) {
        Record r;
        r.fileReferenceNumber = it.key();
        r.parentReferenceNumber = it->parent;
        r.isDirectory = it->isDir;
        r.path = pathOf(it.key());
        r.size = 0;
        r.lastModifiedMs = 0;
        if (r.path.isEmpty()) continue;
        if (!onRecord(r)) break;
    }
    return true;
}

QString UsnJournalReader::pathFromFrn(quint64 frn) {
    auto it = m_frnPathCache.find(frn);
    return it == m_frnPathCache.end() ? QString() : *it;
}

// ===== 原始 $MFT 解析（enumerateAllWithMeta）=====

namespace {

// 读 MFT 起始位置与记录大小
struct MftLayout {
    DWORDLONG mftStartOffset = 0; // 字节
    DWORD bytesPerRecord = 1024;
    DWORD bytesPerCluster = 4096;
};

bool getMftLayout(HANDLE volume, MftLayout& layout) {
    NTFS_VOLUME_DATA_BUFFER vd = {};
    DWORD ret = 0;
    if (!DeviceIoControl(volume, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0,
                         &vd, sizeof(vd), &ret, nullptr))
        return false;
    layout.mftStartOffset = vd.MftStartLcn.QuadPart * vd.BytesPerCluster;
    layout.bytesPerRecord = vd.BytesPerFileRecordSegment;
    layout.bytesPerCluster = vd.BytesPerCluster;
    return layout.bytesPerRecord > 0;
}

#pragma pack(push, 1)
struct MftRecordHeader {
    DWORD magic;           // "FILE"
    WORD updateSeqOffset;
    WORD updateSeqSize;    // 含自身，*2 字节
    DWORDLONG logSeqNumber;
    WORD sequenceNumber;
    WORD flags;            // 0x01 in use, 0x02 directory
    WORD attributeOffset;
    DWORD bytesUsed;
    DWORD bytesAllocated;
    DWORDLONG baseRecord;
    WORD nextAttributeId;
};

struct AttributeHeader {
    DWORD type;            // 0x30 = $FILE_NAME
    DWORD length;
    BYTE  nonResident;
    BYTE  nameLength;
    WORD  nameOffset;
    WORD  flags;
    WORD  attributeId;
};

struct FileNameAttribute { // resident value 部分
    DWORDLONG parentDirectory; // 低 6 字节 FRN
    LARGE_INTEGER creationTime;
    LARGE_INTEGER modificationTime;
    LARGE_INTEGER mftModificationTime;
    LARGE_INTEGER accessTime;
    DWORDLONG allocatedSize;
    DWORDLONG realSize;
    DWORD flags;
    DWORD eaReparse;
    BYTE  nameLength;      // 字符数
    BYTE  nameNamespace;
};
#pragma pack(pop)

// FILETIME(100ns since 1601) -> epoch ms
inline qint64 fileTimeToEpochMs(qint64 ft) {
    return ft ? (ft / 10000LL) - 11644473600000LL : 0;
}

// 路径子树判定：按 '/' 分段比较（大小写不敏感）。
// prefix="C:/a" 匹配 "C:/a/x" 与 "C:/a" 自身，不误匹配 "C:/ab"。
inline bool pathIsUnder(const QString& path, const QString& prefix) {
    if (path.startsWith(prefix, Qt::CaseInsensitive)) {
        if (path.size() == prefix.size()) return true;           // 目录自身
        if (path.at(prefix.size()) == QLatin1Char('/')) return true; // 子级
    }
    return false;
}

// 校验并应用 fixup，返回记录是否有效
bool applyFixup(BYTE* rec, DWORD recSize) {
    auto* h = reinterpret_cast<MftRecordHeader*>(rec);
    if (h->magic != 0x454C4946 /*FILE*/) return false;
    const WORD usaOffset = h->updateSeqOffset;
    const WORD usaCount = h->updateSeqSize;
    if (usaOffset < sizeof(MftRecordHeader) || usaCount == 0) return false;
    const DWORD endSector = recSize - 2;
    for (WORD i = 1; i < usaCount; ++i) {
        const DWORD sectorEnd = i * 512 - 2;
        if (sectorEnd + 2 > recSize) return false;
        const WORD check = *reinterpret_cast<WORD*>(rec + usaOffset + i * 2);
        WORD& at = *reinterpret_cast<WORD*>(rec + sectorEnd);
        if (at != check) { at = check; } // 多数场景是值匹配；不一致按 fixup 恢复
        Q_UNUSED(check);
    }
    Q_UNUSED(endSector);
    return true;
}

} // namespace

bool UsnJournalReader::enumerateAllWithMeta(
    const std::function<bool(const Record&)>& onRecord, quint64 minFileSize,
    const std::function<bool()>& isCancelled, const QString& pathPrefix) {
    if (m_volume == INVALID_HANDLE_VALUE || !onRecord) return false;

    MftLayout layout;
    if (!getMftLayout(m_volume, layout)) {
        LOG << "getMftLayout FAILED err=" << GetLastError();
        return false;
    }
    LOG << "MFT layout: start=" << (quint64)layout.mftStartOffset
          << " recSize=" << layout.bytesPerRecord
          << " prefix=" << pathPrefix;

    // 流水线读取 + 多线程解析（WizTree/Everything 同款思路）：
    // 记录之间完全独立，按块切分后并行解析；读下一块与解析当前块重叠，
    // I/O 与多核都不空转。每块独立 arena，合并时统一加偏移。
    const DWORDLONG recSize = layout.bytesPerRecord;
    NTFS_VOLUME_DATA_BUFFER vd = {};
    DWORD ret = 0;
    DWORDLONG totalRecords = 1ULL << 30;
    if (DeviceIoControl(m_volume, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0,
                        &vd, sizeof(vd), &ret, nullptr)
        && vd.MftValidDataLength.QuadPart > 0)
        totalRecords = vd.MftValidDataLength.QuadPart / recSize;

    // $MFT 常被碎片化：线性直读在碎片处会读到无关卷数据（表现为海量
    // badMagic、漏文件）。$MFT 文件在部分系统上即使有备份特权也被拒打开
    // （err=5），改用经典方案：$MFT 自身的文件记录（record 0）永远位于
    // MFT 起始处（第一片内，线性可读），从它的 $DATA 属性解析运行列表
    // （runlist）即可得到完整碎片分布 LCN 映射。
    struct MftExtent { quint64 vcnOff; quint64 lcn; quint64 clusters; };
    std::vector<MftExtent> extents;
    {
        // 读 MFT 起始 1MB（数千条记录，必含 record 0）
        const DWORD headBytes = 1 << 20;
        std::vector<BYTE> head(headBytes, 0);
        OVERLAPPED ov = {};
        ov.Offset = DWORD(layout.mftStartOffset & 0xFFFFFFFF);
        ov.OffsetHigh = DWORD(layout.mftStartOffset >> 32);
        DWORD got = 0;
        if (ReadFile(m_volume, head.data(), headBytes, &got, &ov)
            || (GetLastError() == ERROR_IO_PENDING
                && GetOverlappedResult(m_volume, &ov, &got, TRUE))) {
            auto* rec0 = head.data();
            auto* h0 = reinterpret_cast<MftRecordHeader*>(rec0);
            if (h0->magic == 0x454C4946 && applyFixup(rec0, DWORD(recSize))) {
                // 遍历属性找 $DATA (0x80)，非驻留 → 解析 runlist
                DWORD off = h0->attributeOffset;
                while (off + sizeof(AttributeHeader) <= got) {
                    auto* attr = reinterpret_cast<AttributeHeader*>(rec0 + off);
                    if (attr->type == 0xFFFFFFFF || attr->length < sizeof(AttributeHeader)) break;
                    if (attr->type == 0x80 && attr->nonResident
                        && off + attr->length <= got) {
                        // 非驻留属性头 0x40 字节后是 runlist
                        // （头里 runlistOffset 在 0x20 处，WORD；无映射对时用之）
                        const WORD runlistOff = *reinterpret_cast<WORD*>(rec0 + off + 0x20);
                        const BYTE* run = rec0 + off + runlistOff;
                        const BYTE* runEnd = rec0 + off + attr->length;
                        quint64 vcn = 0, lcn = 0;
                        while (run + 1 <= runEnd && *run != 0) {
                            const BYTE szLen = *run & 0x0F;
                            const BYTE offLen = *run >> 4;
                            ++run;
                            if (szLen == 0 || szLen > 8 || offLen > 8
                                || run + szLen > runEnd) break;
                            quint64 clusters = 0;
                            for (BYTE i = 0; i < szLen; ++i)
                                clusters |= quint64(run[i]) << (8 * i);
                            run += szLen;
                            // 有符号扩展的偏移量
                            qint64 delta = 0;
                            if (offLen > 0) {
                                if (run + offLen > runEnd) break;
                                for (BYTE i = 0; i < offLen; ++i)
                                    delta |= qint64(run[i]) << (8 * i);
                                const BYTE signBits = 8 - offLen;
                                if (signBits < 8 && (delta >> (8 * offLen - 1)))
                                    delta |= qint64(-1) << (8 * offLen); // 符号扩展
                                run += offLen;
                            }
                            lcn += delta;
                            if (clusters > 0)
                                extents.push_back({ vcn, quint64(lcn), clusters });
                            vcn += clusters;
                        }
                        break; // $DATA 已处理
                    }
                    off += attr->length;
                }
                LOG << "MFT runlist extents=" << extents.size();
            } else {
                LOG << "MFT record0 invalid (magic/fixup)";
            }
        } else {
            LOG << "MFT head read FAILED err=" << GetLastError();
        }
    }
    if (!extents.empty()) {
        // 碎片分布已知：总记录数以实际数据范围为准（不超过有效数据长度）
        quint64 totalBytes = 0;
        for (const auto& e : extents) totalBytes += e.clusters * layout.bytesPerCluster;
        const DWORDLONG byExtents = totalBytes / recSize;
        totalRecords = qMin(totalRecords, byExtents);
        LOG << "MFT extents=" << extents.size()
              << " totalRecords=" << (quint64)totalRecords;
    }

    struct Node {
        quint64 parent = 0;   // 父 FRN（低 6 字节）
        quint64 frn = 0;
        quint64 size = 0;
        qint64 mtimeMs = 0;
        quint32 nameOff = 0;  // arena 内 UTF-16 字节偏移
        quint32 nameLen = 0;  // 字符数
        bool isDir = false;
    };
    struct Parsed {
        std::vector<Node> nodes;
        QByteArray arena;     // 本块的 UTF-16 名字
        quint64 smallSkipped = 0; // 本块被阈值剪枝的文件数（诊断用）
        quint64 badMagic = 0, fixupFail = 0, unused = 0, noName = 0;
    };

    const DWORD kChunkRecords = 4096; // 4MB/块（1KB 记录）
    const int kMaxInflight = 4;       // 预取块数上限（读领先解析的深度）

    auto parseChunk = [recSize, minFileSize](const std::vector<BYTE>& buf,
                                             quint64 frnBase, DWORD records) -> Parsed {
        Parsed p;
        p.nodes.reserve(records / 2);
        p.arena.reserve(1 << 18);
        quint64 smallSkipped = 0;
        quint64 badMagic = 0, fixupFail = 0, unused = 0, noName = 0;
        for (DWORD i = 0; i < records; ++i) {
            BYTE* rec = const_cast<BYTE*>(buf.data()) + size_t(i) * size_t(recSize);
            auto* h = reinterpret_cast<MftRecordHeader*>(rec);
            if (h->magic != 0x454C4946) { ++badMagic; continue; }
            if (!applyFixup(rec, DWORD(recSize))) { ++fixupFail; continue; }
            if (!(h->flags & 0x01)) { ++unused; continue; } // 未使用记录跳过

            Node n;
            n.frn = frnBase + i;
            n.isDir = (h->flags & 0x02) != 0;
            bool skipRecord = false;

            // 遍历 resident 属性找 $FILE_NAME (0x30)
            DWORD off = h->attributeOffset;
            while (off + sizeof(AttributeHeader) <= recSize) {
                auto* attr = reinterpret_cast<AttributeHeader*>(rec + off);
                if (attr->type == 0xFFFFFFFF || attr->length < sizeof(AttributeHeader)) break;
                if (attr->type == 0x30 && !attr->nonResident
                    && off + attr->length <= recSize
                    && attr->length >= sizeof(AttributeHeader) + sizeof(FileNameAttribute)) {
                    // resident 属性 value 起始 = 属性头 0x18 字节
                    auto* fn = reinterpret_cast<FileNameAttribute*>(rec + off + 0x18);
                    if (fn->nameNamespace != 2 /*POSIX 保留*/) {
                        // 提前剪枝：小文件不建 Node，直接跳过该记录
                        // （目录除外——父链回溯需要全部目录）
                        if (!n.isDir && minFileSize > 0
                            && fn->realSize < minFileSize) {
                            ++smallSkipped;
                            skipRecord = true;
                            break;
                        }
                        n.parent = fn->parentDirectory & 0x0000FFFFFFFFFFFFULL;
                        n.size = fn->realSize;
                        n.mtimeMs = fileTimeToEpochMs(fn->modificationTime.QuadPart);
                        const WORD nameLen = fn->nameLength;
                        const BYTE* namePtr = rec + off + 0x18 + sizeof(FileNameAttribute);
                        if (nameLen > 0 && off + 0x18 + sizeof(FileNameAttribute)
                                + size_t(nameLen) * 2 <= recSize) {
                            n.nameOff = quint32(p.arena.size());
                            n.nameLen = nameLen;
                            p.arena.append(reinterpret_cast<const char*>(namePtr),
                                           size_t(nameLen) * 2);
                        }
                    }
                    break; // 取第一个 $FILE_NAME（还有一个 win32/正名即可）
                }
                off += attr->length;
            }
            if (skipRecord) continue;
            if (n.nameLen == 0) { ++noName; continue; }
            p.nodes.push_back(n);
        }
        p.smallSkipped = smallSkipped;
        p.badMagic = badMagic; p.fixupFail = fixupFail;
        p.unused = unused; p.noName = noName;
        return p;
    };

    // 读取器：碎片已知时按 extent LCN 直读，且单次读取不越过碎片边界
    // （跨边界的线性读会再次读到无关数据）；否则线性读。
    // 返回实际读到的记录数；0 = 读完/失败（err 置原因）。
    auto readChunk = [&](std::vector<BYTE>& buf, quint64 base, DWORD& err) -> DWORD {
        err = 0;
        const DWORDLONG want = qMin<DWORDLONG>(kChunkRecords, totalRecords - base);
        // extent 感知：want 条记录可能跨多片，逐片拷贝
        if (!extents.empty()) {
            DWORD done = 0;
            quint64 rec = base;
            while (done < want) {
                const quint64 vcn = rec * recSize / layout.bytesPerCluster;
                const MftExtent* cur = nullptr;
                for (const auto& e : extents)
                    if (vcn >= e.vcnOff && vcn < e.vcnOff + e.clusters) { cur = &e; break; }
                if (!cur) break; // 超出已知碎片 = 读完
                // 本片内还剩多少条记录（按字节粒度对齐 extent 末端）
                const quint64 vcnEnd = cur->vcnOff + cur->clusters;
                const DWORDLONG extentEndRec = vcnEnd * layout.bytesPerCluster / recSize;
                const DWORDLONG piece = qMin<DWORDLONG>(want - done, extentEndRec - rec);
                if (piece <= 0) break;
                const DWORDLONG fileOff =
                    (cur->lcn + (vcn - cur->vcnOff)) * layout.bytesPerCluster
                    + (rec * recSize) % layout.bytesPerCluster;
                OVERLAPPED ov = {};
                ov.Offset = DWORD(fileOff & 0xFFFFFFFF);
                ov.OffsetHigh = DWORD(fileOff >> 32);
                DWORD got = 0;
                if (!ReadFile(m_volume, buf.data() + size_t(done) * size_t(recSize),
                              DWORD(piece * recSize), &got, &ov)) {
                    err = GetLastError();
                    if (err == ERROR_IO_PENDING
                        && GetOverlappedResult(m_volume, &ov, &got, TRUE))
                        err = 0;
                    if (err != 0) {
                        LOG << "MFT readChunk FAILED base=" << base
                              << " rec=" << rec << " err=" << err;
                        return 0;
                    }
                }
                done += DWORD(got / recSize);
                rec += got / recSize;
                if (got % recSize != 0) break; // 尾部不足一条 = 到末尾
            }
            if (done == 0) { err = DWORD(-1); return 0; }
            return done;
        }
        // 线性缺省（无碎片表）
        OVERLAPPED ov = {};
        const DWORDLONG fileOff = layout.mftStartOffset + base * recSize;
        ov.Offset = DWORD(fileOff & 0xFFFFFFFF);
        ov.OffsetHigh = DWORD(fileOff >> 32);
        DWORD got = 0;
        if (!ReadFile(m_volume, buf.data(), DWORD(want * recSize), &got, &ov)) {
            err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                if (!GetOverlappedResult(m_volume, &ov, &got, TRUE))
                    err = GetLastError();
                else
                    err = 0; // 异步读完成
            }
            if (err != 0) {
                LOG << "MFT readChunk FAILED base=" << base
                      << " off=" << (quint64)fileOff
                      << " want=" << (quint64)(want * recSize) << " err=" << err;
                return 0;
            }
        }
        if (got == 0) { err = DWORD(-1); return 0; } // 0 字节 = 异常（应读到 EOF 前的数据）
        return got / DWORD(recSize);
    };

    std::vector<Node> nodes;
    nodes.reserve(1 << 18);
    std::vector<std::future<Parsed>> inflight; // 有序：下标即块序号
    std::vector<std::unique_ptr<std::vector<BYTE>>> bufs;
    quint64 nextBase = 0;
    int nextToParse = 0; // 待合并的块序

    // 预取 + 提交解析，直到读失败/读完/取消
    quint64 lastGoodBase = 0; // 最后一块成功读到的位置（读失败时用于诊断）
    while (nextBase < totalRecords) {
        if (isCancelled && isCancelled()) return false;
        // 限制在途块数，避免内存无限增长
        if (int(inflight.size()) - nextToParse >= kMaxInflight) {
            inflight[nextToParse].wait();
            ++nextToParse;
        }
        auto buf = std::make_unique<std::vector<BYTE>>(size_t(recSize * kChunkRecords));
        DWORD readErr = 0;
        const DWORD records = readChunk(*buf, nextBase, readErr);
        if (records == 0) {
            LOG << "MFT read stopped at base=" << nextBase
                  << " of " << (quint64)totalRecords << " records err=" << readErr;
            break;
        }
        lastGoodBase = nextBase + records;
        auto* raw = buf.release();
        bufs.emplace_back(raw);
        inflight.push_back(std::async(std::launch::async, parseChunk,
                                      std::cref(*raw), nextBase, records));
        nextBase += records;
    }
    LOG << "MFT read complete: records=" << lastGoodBase
          << " of " << (quint64)totalRecords;

    // 按序合并：FRN→下标索引 + 名字 arena 偏移平移
    QHash<quint64, quint32> indexOf; // FRN -> nodes 下标
    indexOf.reserve(1 << 18);
    QByteArray arena;
    arena.reserve(1 << 22);
    quint64 totalSmallSkipped = 0, totalBadMagic = 0, totalFixupFail = 0;
    quint64 totalUnused = 0, totalNoName = 0;
    (void)nextToParse;
    // 合并阶段也响应取消：丢弃剩余块，直接中止
    for (int c = 0; c < int(inflight.size()); ++c) {
        if (isCancelled && isCancelled()) return false;
        Parsed p = inflight[c].get();
        totalSmallSkipped += p.smallSkipped;
        totalBadMagic += p.badMagic; totalFixupFail += p.fixupFail;
        totalUnused += p.unused; totalNoName += p.noName;
        const quint32 arenaBase = quint32(arena.size());
        if (arenaBase)
            for (auto& n : p.nodes) n.nameOff += arenaBase;
        arena.append(p.arena);
        const quint32 idxBase = quint32(nodes.size());
        for (auto& n : p.nodes) {
            indexOf.insert(n.frn, idxBase + quint32(nodes.size()));
            nodes.push_back(std::move(n));
        }
        bufs[c].reset(); // 释放该块原始缓冲
    }

    if (nodes.empty()) {
        LOG << "MFT parse produced 0 nodes -> fail";
        return false;
    }
    // 目录级前缀：统一为 "C:/dir" 形式（'/' 分隔，无尾斜杠），大小写不敏感匹配
    QString prefix = pathPrefix;
    if (!prefix.isEmpty()) {
        prefix.replace(QLatin1Char('\\'), QLatin1Char('/'));
        while (prefix.endsWith(QLatin1Char('/'))) prefix.chop(1);
        // "C:" → "C:"（根自身）；前缀匹配按段进行（在输出循环里逐段比较）
    }
    LOG << "MFT parsed nodes=" << nodes.size()
          << " smallSkipped=" << totalSmallSkipped
          << " badMagic=" << totalBadMagic
          << " fixupFail=" << totalFixupFail
          << " unused=" << totalUnused
          << " noName=" << totalNoName
          << " cancelled=" << (isCancelled && isCancelled());

    // 路径拼装：只为候选文件触发，沿父链向上找已缓存祖先，再逐级下拼。
    // 目录节点不再全量预拼（百万级目录的 QString 拼接是此前的最大热点）。
    const wchar_t* names = reinterpret_cast<const wchar_t*>(arena.constData());
    auto pathOf = [&](quint64 frn) -> QString {
        quint64 chain[256];
        int depth = 0;
        QString base; // 最近的已缓存祖先路径（可能为空 = 到卷根）
        quint64 cur = frn;
        while (true) {
            auto cit = m_frnPathCache.constFind(cur);
            if (cit != m_frnPathCache.constEnd()) { base = *cit; break; }
            auto iit = indexOf.constFind(cur);
            if (iit == indexOf.constEnd()) break; // 不在本卷 MFT 中（如卷根 5）
            if (depth < int(sizeof(chain) / sizeof(chain[0]))) chain[depth++] = cur;
            const Node& nd = nodes[*iit];
            if (nd.parent == cur) break; // 环保护
            cur = nd.parent;
        }
        QString path = base;
        if (path.isEmpty()) {
            // 链顶到卷根：补盘符前缀（如 "C:"），否则路径缺盘符
            path = QString(QChar(m_drive)) + QStringLiteral(":");
        }
        for (int i = depth - 1; i >= 0; --i) {
            const Node& nd = nodes[*indexOf.constFind(chain[i])];
            path += QLatin1Char('/');
            path += QString::fromWCharArray(names + nd.nameOff / 2, nd.nameLen);
            m_frnPathCache.insert(chain[i], path);
        }
        return path;
    };

    for (quint32 idx = 0; idx < nodes.size(); ++idx) {
        // 每 8192 条响应一次取消（回溯循环本身无法被打断，靠这里及时退出）
        if ((idx & 8191) == 0 && isCancelled && isCancelled()) return false;
        const Node& nd = nodes[idx];
        if (nd.isDir) { pathOf(nd.frn); continue; } // 目录只注册路径
        Record r;
        r.fileReferenceNumber = nd.frn;
        r.parentReferenceNumber = nd.parent;
        r.isDirectory = false;
        r.path = pathOf(nd.frn);
        r.size = nd.size;
        r.lastModifiedMs = nd.mtimeMs;
        if (r.path.isEmpty()) continue;
        // 目录级扫描：只回报指定子树内的文件（按 '/' 分段前缀比较，避免 "C:/a" 误匹配 "C:/ab"）
        if (!prefix.isEmpty()) {
            if (!pathIsUnder(r.path, prefix)) continue;
        }
        if (!onRecord(r)) break;
    }
    return true;
}

} // namespace DiskOrganizer
