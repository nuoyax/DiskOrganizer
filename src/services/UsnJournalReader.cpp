#include "services/UsnJournalReader.h"

#include <QHash>
#include <QString>

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
    if (m_volume == INVALID_HANDLE_VALUE) return false;
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

bool UsnJournalReader::enumerateAll(const std::function<bool(const Record&)>& onRecord) {
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
        if (!got) break; // ERROR_HANDLE_EOF 或其他错误 = 枚举完成
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
    const std::function<bool(const Record&)>& onRecord, quint64 minFileSize) {
    if (m_volume == INVALID_HANDLE_VALUE || !onRecord) return false;

    MftLayout layout;
    if (!getMftLayout(m_volume, layout)) return false;

    // 大文件场景加速（WizTree 直读 $MFT 同款路线），三个关键点：
    // 1) 小于阈值的文件在解析阶段直接丢弃，不进索引、不拼路径；
    // 2) 紧凑 vector 节点 + UTF-16 名字竞技场（arena），解析期零 QString 分配；
    // 3) 只为候选大文件的祖先目录按需拼路径并缓存（不再全卷目录预拼）。
    struct Node {
        quint64 parent = 0;   // 父 FRN（低 6 字节）
        quint64 frn = 0;
        quint64 size = 0;
        qint64 mtimeMs = 0;
        quint32 nameOff = 0;  // arena 内 UTF-16 字节偏移
        quint32 nameLen = 0;  // 字符数
        bool isDir = false;
    };
    std::vector<Node> nodes;
    nodes.reserve(1 << 16);
    QHash<quint64, quint32> indexOf; // FRN -> nodes 下标
    indexOf.reserve(1 << 18);
    QByteArray arena;                // 保留节点的 UTF-16 名字（无对齐要求）
    arena.reserve(1 << 20);

    // 分块顺序读 MFT。MFT 可能很大（百万文件≈1GB），按 8MB 块读。
    // 记录不会跨块边界：块大小取记录大小的整数倍，一次定点读整块。
    const DWORDLONG recSize = layout.bytesPerRecord;
    NTFS_VOLUME_DATA_BUFFER vd = {};
    DWORD ret = 0;
    DWORDLONG totalRecords = 1ULL << 30;
    if (DeviceIoControl(m_volume, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0,
                        &vd, sizeof(vd), &ret, nullptr)
        && vd.MftValidDataLength.QuadPart > 0)
        totalRecords = vd.MftValidDataLength.QuadPart / recSize;

    const DWORD kChunkRecords = 2048;
    std::vector<BYTE> chunk(size_t(recSize * kChunkRecords));

    for (DWORDLONG base = 0; base < totalRecords; base += kChunkRecords) {
        const DWORDLONG records = qMin<DWORDLONG>(kChunkRecords, totalRecords - base);
        OVERLAPPED ov = {};
        const DWORDLONG fileOff = layout.mftStartOffset + base * recSize;
        ov.Offset = DWORD(fileOff & 0xFFFFFFFF);
        ov.OffsetHigh = DWORD(fileOff >> 32);
        DWORD got = 0;
        if (!ReadFile(m_volume, chunk.data(), DWORD(records * recSize), &got, &ov)
            && GetLastError() == ERROR_IO_PENDING
            && !GetOverlappedResult(m_volume, &ov, &got, TRUE))
            break; // 读失败 → 已解析部分仍可用
        if (got < recSize) break;

        for (DWORDLONG i = 0; i < got / recSize; ++i) {
            BYTE* rec = chunk.data() + size_t(i * recSize);
            auto* h = reinterpret_cast<MftRecordHeader*>(rec);
            if (h->magic != 0x454C4946) continue;
            if (!applyFixup(rec, DWORD(recSize))) continue;
            if (!(h->flags & 0x01)) continue; // 未使用记录跳过

            Node n;
            n.frn = base + i;
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
                            n.nameOff = quint32(arena.size());
                            n.nameLen = nameLen;
                            arena.append(reinterpret_cast<const char*>(namePtr),
                                         size_t(nameLen) * 2);
                        }
                    }
                    break; // 取第一个 $FILE_NAME（还有一个 win32/正名即可）
                }
                off += attr->length;
            }
            if (skipRecord) continue;
            if (n.nameLen == 0) continue;
            indexOf.insert(n.frn, quint32(nodes.size()));
            nodes.push_back(n);
        }
    }

    if (nodes.empty()) return false;

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
        for (int i = depth - 1; i >= 0; --i) {
            const Node& nd = nodes[*indexOf.constFind(chain[i])];
            path += QLatin1Char('/');
            path += QString::fromWCharArray(names + nd.nameOff / 2, nd.nameLen);
            m_frnPathCache.insert(chain[i], path);
        }
        return path;
    };

    for (quint32 idx = 0; idx < nodes.size(); ++idx) {
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
        if (!onRecord(r)) break;
    }
    return true;
}

} // namespace DiskOrganizer
