#include "services/ReportService.h"
#include <QFile>
#include <QTextStream>
#include "util/SizeFormatter.h"

namespace DiskOrganizer {

bool ReportService::exportScanReport(const QString& filePath, const QString& title, const QList<FileInfo>& files) {
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << title << "\n";
    for (const auto& fi : files)
        ts << fi.absolutePath << "," << fi.size << "," << fi.extension << "\n";
    return true;
}

bool ReportService::exportDuplicateReport(const QString& filePath, const QList<DuplicateGroup>& groups) {
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    for (const auto& g : groups) {
        ts << "== group, wasted " << SizeFormatter::formatSize(g.wastedBytes) << " ==\n";
        for (const auto& fi : g.files) ts << fi.absolutePath << "\n";
    }
    return true;
}

} // namespace DiskOrganizer
