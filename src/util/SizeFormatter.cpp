#include "util/SizeFormatter.h"
#include <QtMath>

namespace DiskOrganizer {

QString formatSize(qint64 bytes) {
    if (bytes < 0) return QObject::tr("未知");
    static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int unit = 0;
    double value = (double)bytes;
    while (value >= 1024.0 && unit < 5) { value /= 1024.0; ++unit; }
    int precision = value < 10.0 && unit > 0 ? 2 : 1;
    return QString::number(value, 'f', precision) + " " + units[unit];
}

QString formatDuration(qint64 ms) {
    qint64 s = ms / 1000;
    qint64 h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    if (h > 0) return QString("%1h %2m %3s").arg(h).arg(m).arg(sec);
    if (m > 0) return QString("%1m %2s").arg(m).arg(sec);
    return QString("%1s").arg(sec);
}

} // namespace DiskOrganizer
