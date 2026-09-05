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

} // namespace DiskOrganizer
