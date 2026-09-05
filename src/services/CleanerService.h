#pragma once
#include <QObject>
#include <QStringList>
#include <QtGlobal>

// 可清理项类型
enum class CleanCategory {
    TempFiles,        // %TEMP%、%TMP%
    RecycleBin,       // 回收站
    BrowserCache,     // 各浏览器缓存
    SystemLogs,       // 日志 / .log
    WindowsUpdate,    // Windows 更新残留 (SoftwareDistribution\Download)
    ThumbnailCache,   // 缩略图缓存 thumbcache
    Prefetch,         // Prefetch
    DumpFiles,        // minidump / memory.dmp
    InstallerCache,   // Windows Installer 残留（谨慎）
    EmptyFolders,     // 空目录
    ZeroByteFiles,    // 0 字节文件
    CustomRules,      // 用户自定义规则（通配符）
};

struct CleanItem {
    CleanCategory category;
    QString path;
    qint64 size = 0;
    bool safeToDelete = true;  // 影响系统运行与否
    QString description;
};

// 清理服务：扫描可清理项 → 删除（可回收站/永久）
class CleanerService : public QObject {
    Q_OBJECT
public:
    explicit CleanerService(QObject* parent = nullptr);

    QList<CleanItem> findCleanableItems(const QList<CleanCategory>& categories) const;
    // 删除；toRecycleBin=false 时永久删除；返回成功释放的字节数
    qint64 clean(const QList<CleanItem>& items, bool toRecycleBin);

signals:
    void progress(int percent, const QString& currentPath);
    void finished(qint64 freedBytes, int failedCount);

private:
    bool m_cancelRequested = false;
};
