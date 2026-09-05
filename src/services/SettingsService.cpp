#include "services/SettingsService.h"

namespace DiskOrganizer {

SettingsService::SettingsService(QObject* parent)
    : QObject(parent), m_store(QSettings::IniFormat, QSettings::UserScope, "DiskOrganizer", "DiskOrganizer") {}

AppSettings SettingsService::load() const {
    AppSettings s;
    s.excludePaths = m_store.value("scan/excludePaths").toStringList();
    s.bigFileThreshold = m_store.value("scan/bigFileThreshold", s.bigFileThreshold).toLongLong();
    s.cleanToRecycleBin = m_store.value("clean/toRecycleBin", true).toBool();
    s.scanFollowSymlinks = m_store.value("scan/followSymlinks", false).toBool();
    s.language = m_store.value("general/language", "zh_CN").toString();
    return s;
}

void SettingsService::save(const AppSettings& s) {
    m_store.setValue("scan/excludePaths", s.excludePaths);
    m_store.setValue("scan/bigFileThreshold", s.bigFileThreshold);
    m_store.setValue("clean/toRecycleBin", s.cleanToRecycleBin);
    m_store.setValue("scan/followSymlinks", s.scanFollowSymlinks);
    m_store.setValue("general/language", s.language);
    m_store.sync();
}

} // namespace DiskOrganizer
