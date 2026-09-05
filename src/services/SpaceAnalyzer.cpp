#include "services/SpaceAnalyzer.h"
#include <QHash>
#include <algorithm>

namespace DiskOrganizer {

SpaceAnalyzer::SpaceAnalyzer(QObject* parent) : QObject(parent) {}

QList<QPair<QString, qint64>> SpaceAnalyzer::directorySizes(const QList<FileInfo>& files) const {
    QHash<QString, qint64> sizes;
    for (const auto& f : files) {
        // 把文件大小累计到所有祖先目录
        QString dir = QFileInfo(f.absolutePath).absolutePath();
        while (dir.length() > 3) { // 停在 "X:/" 之前
            sizes[dir] += f.size;
            dir = QFileInfo(dir + "/.").absolutePath(); // 上一级
        }
    }
    QList<QPair<QString, qint64>> out;
    for (auto it = sizes.constBegin(); it != sizes.constEnd(); ++it)
        out.append({it.key(), it.value()});
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.second > b.second; });
    return out;
}

QList<SpaceAnalyzer::TypeStat> SpaceAnalyzer::typeDistribution(const QList<FileInfo>& files) const {
    QHash<QString, TypeStat> stats;
    for (const auto& f : files) {
        if (f.isDir) continue;
        auto& s = stats[f.extension];
        s.extension = f.extension;
        s.totalBytes += f.size;
        ++s.count;
    }
    QList<TypeStat> out;
    for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) out.append(it.value());
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.totalBytes > b.totalBytes; });
    return out;
}

} // namespace DiskOrganizer
