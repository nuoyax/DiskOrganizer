#include "services/BigFileFinder.h"
#include <QDateTime>
#include <algorithm>

BigFileFinder::BigFileFinder(QObject* parent) : QObject(parent) {}

QList<FileInfo> BigFileFinder::find(const QList<FileInfo>& allFiles, const BigFileFilter& filter) const {
    QList<FileInfo> out;
    const qint64 cutoff = filter.olderThanDays > 0
        ? QDateTime::currentMSecsSinceEpoch() - filter.olderThanDays * 86400LL * 1000
        : 0;
    for (const auto& f : allFiles) {
        if (f.isDir) continue;
        if (f.size < filter.minSizeBytes) continue;
        if (cutoff && f.lastModified > cutoff) continue;          // 太新，排除
        if (!filter.extensionFilter.isEmpty() && f.extension != filter.extensionFilter) continue;
        out.append(f);
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.size > b.size; });
    if (out.size() > filter.topN) out.resize(filter.topN);
    return out;
}
