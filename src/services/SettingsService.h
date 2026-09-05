#pragma once
#include <QObject>
#include <QSettings>
#include <QStringList>

namespace DiskOrganizer {

// 设置持久化（QSettings/注册表或 ini）
struct AppSettings {
    QStringList excludePaths;        // 扫描排除目录
    qint64 bigFileThreshold = 100LL * 1024 * 1024;
    bool cleanToRecycleBin = true;   // 清理默认进回收站
    bool scanFollowSymlinks = false;
    QString language = "zh_CN";
};

class SettingsService : public QObject {
    Q_OBJECT
public:
    explicit SettingsService(QObject* parent = nullptr);
    AppSettings load() const;
    void save(const AppSettings& s);

private:
    QSettings m_store;
};

} // namespace DiskOrganizer
