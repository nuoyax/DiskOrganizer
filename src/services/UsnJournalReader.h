#pragma once
#include <QObject>
#include <QString>
#include <QHash>
#include <functional>
#include <windows.h>

namespace DiskOrganizer {

// NTFS USN Journal（变更日志）读取器。
// 用途：不遍历目录树，直接从 NTFS 变更日志取全量文件记录，实现秒级全盘枚举。
// 需要目标卷为 NTFS 且进程具备读卷句柄的权限（通常需管理员）。
class UsnJournalReader {
public:
    UsnJournalReader() = default;
    ~UsnJournalReader() { close(); }
    UsnJournalReader(const UsnJournalReader&) = delete;
    UsnJournalReader& operator=(const UsnJournalReader&) = delete;

    // 打开卷(如 "C:")并校验 USN Journal 存在。失败返回 false（非 NTFS / 无权限）
    bool open(wchar_t driveLetter);
    void close();

    // 单条 USN 记录解析结果
    struct Record {
        QString path;      // 绝对路径（不含反斜杠结尾）
        quint64 fileReferenceNumber = 0;
        quint64 parentReferenceNumber = 0;
        quint64 size = 0;          // 文件大小（目录为 0）
        quint64 lastModifiedMs = 0; // epoch ms
        bool isDirectory = false;
    };

    // 枚举卷上全部文件记录。onRecord 返回 false 提前中止。
    // 返回是否成功完整枚举（失败可能因权限/非 NTFS/中途错误）
    bool enumerateAll(const std::function<bool(const Record&)>& onRecord);

    static bool isNtfs(wchar_t driveLetter);

private:
    // 由 FRN(文件引用号)向上回溯拼出完整路径，带缓存
    QString pathFromFrn(quint64 frn);

    HANDLE m_volume = INVALID_HANDLE_VALUE;
    wchar_t m_drive = 0;
    QHash<quint64, QString> m_frnPathCache; // FRN -> 完整路径缓存
};

} // namespace DiskOrganizer
