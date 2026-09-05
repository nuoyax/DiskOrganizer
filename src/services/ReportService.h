#pragma once
#include <QString>
#include "services/DuplicateFinder.h"

namespace DiskOrganizer {

// 报告导出：TXT / CSV / HTML
class ReportService {
public:
    static bool exportScanReport(const QString& filePath, const QString& title, const QList<FileInfo>& files);
    static bool exportDuplicateReport(const QString& filePath, const QList<DuplicateGroup>& groups);
};

} // namespace DiskOrganizer
