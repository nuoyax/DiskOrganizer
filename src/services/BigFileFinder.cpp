#include "services/BigFileFinder.h"
#include <QDateTime>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <atomic>

BigFileFinder::BigFileFinder(QObject* parent) : QObject(parent) {}

QList<FileInfo> BigFileFinder::find(const QList<FileInfo>& allFiles, const BigFileFilter& filter) const {
    const qint64 cutoff = filter.olderThanDays > 0
        ? QDateTime::currentMSecsSinceEpoch() - filter.olderThanDays * 86400LL * 1000
        : 0;

    // 分块并行过滤：数据量大（百万级文件）时充分利用多核
    const int nThreads = qMax(1, QThread::idealThreadCount());
    const qsizetype chunkSize = qMax<qsizetype>(4096, allFiles.size() / (nThreads * 4) + 1);

    QList<QList<FileInfo>> chunks;
    for (qsizetype i = 0; i < allFiles.size(); i += chunkSize)
        chunks.append(allFiles.mid(i, chunkSize));

    QAtomicInteger<qint64> keptCount{0};
    auto results = QtConcurrent::blockingMapped(chunks,
        [filter, cutoff, &keptCount](const QList<FileInfo>& chunk) -> QList<FileInfo> {
            QList<FileInfo> local;
            local.reserve(chunk.size() / 8);
            for (const auto& f : chunk) {
                if (f.isDir) continue;
                if (f.size < filter.minSizeBytes) continue;
                if (cutoff && f.lastModified > cutoff) continue;          // 太新，排除
                if (!filter.extensionFilter.isEmpty() && !filter.extensionFilter.contains(f.extension)) continue;
                local.append(f);
            }
            keptCount.fetchAndAddRelaxed(local.size());
            return local;
        });

    // 归并 + 截断到 topN 后排序（避免对全部命中排序）
    QList<FileInfo> out;
    for (const auto& r : results) out += r;
    if (out.size() > filter.topN) {
        // 局部选择：nth_element 拿到 topN 再排序，O(n) 级
        std::nth_element(out.begin(), out.begin() + filter.topN, out.end(),
                         [](const FileInfo& a, const FileInfo& b) { return a.size > b.size; });
        out.resize(filter.topN);
    }
    std::sort(out.begin(), out.end(), [](const FileInfo& a, const FileInfo& b) { return a.size > b.size; });
    return out;
}
