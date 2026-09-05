#include "services/ScannerService.h"
#include "services/UsnJournalReader.h"
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFileInfo>
#include <QtConcurrent>
#include <atomic>

namespace {
std::atomic<bool> g_cancelRequested{false};
std::atomic<int> g_activeScans{0};
}

ScannerService::ScannerService(QObject* parent) : QObject(parent) {}

void ScannerService::startScan(const QStringList& rootPaths) {
    g_cancelRequested = false;
    ++g_activeScans;
    const qint64 startTime = QDateTime::currentMSecsSinceEpoch();

    (void)QtConcurrent::run([this, rootPaths, startTime]() {
        // 按一级子目录分片并行遍历
        QStringList shards;
        for (const QString& root : rootPaths) {
            const QFileInfo rfi(root);
            if (!rfi.exists()) continue;
            const QStringList subDirs = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            if (subDirs.isEmpty()) { shards << root; continue; }
            for (const QString& sub : subDirs)
                shards << root + QLatin1Char('/') + sub;
            shards << root; // 根层散文件
        }

        QAtomicInteger<int> doneCount{0};
        const int totalShards = qMax(1, shards.size());

        (void)QtConcurrent::blockingMapped(shards, [this, &doneCount, &totalShards](const QString& shard) {
            if (g_cancelRequested) return 0;
            // flat 迭代避免递归栈溢出；不跟随符号链接（防环）
            QDirIterator it(shard, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            int checked = 0;
            while (it.hasNext()) {
                it.next();
                if (++checked % 256 == 0 && g_cancelRequested) return 0;
                const QFileInfo fi = it.fileInfo();
                if (!fi.exists()) continue;
                FileInfo info;
                info.absolutePath = fi.absoluteFilePath();
                info.name = fi.fileName();
                info.size = fi.isDir() ? 0 : fi.size();
                info.lastModified = fi.lastModified().toMSecsSinceEpoch();
                info.isDir = fi.isDir();
                info.isSymlink = fi.isSymLink();
                info.extension = fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix().toLower();
                emit fileScanned(info);
            }
            const int done = doneCount.fetchAndAddRelaxed(1) + 1;
            emit progress(done * 100 / totalShards, shard);
            return 0;
        });

        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
        emit finished(doneCount.loadRelaxed(), 0, elapsed);
        --g_activeScans;
    });
}

QList<FileInfo> ScannerService::scanBlocking(const QStringList& rootPaths,
    const std::function<bool(qint64, const QString&)>& onProgress) {
    // 快速路径：整卷扫描 + NTFS + 有权限 → 直接枚举 USN/MFT（秒级），
    // 失败则回退到并行目录树遍历。
    if (rootPaths.size() == 1) {
        const QString root = rootPaths.first();
        if ((root.length() == 2 || root.length() == 3) && root[1] == QLatin1Char(':')) {
            DiskOrganizer::UsnJournalReader usn;
            if (usn.open(root[0].toLatin1())) {
                QList<FileInfo> out;
                out.reserve(200000);
                qint64 count = 0;
                // 优先带元数据版本（size/mtime 来自 MFT $FILE_NAME），
                // 失败再退到纯 USN 枚举，最后才回退目录遍历。
                bool ok = usn.enumerateAllWithMeta([&](const DiskOrganizer::UsnJournalReader::Record& r) {
                    FileInfo info;
                    info.absolutePath = r.path;
                    info.name = r.path.section(QLatin1Char('/'), -1);
                    info.isDir = r.isDirectory;
                    info.isSymlink = false;
                    info.size = r.size;
                    info.extension = info.name.contains(QLatin1Char('.'))
                        ? QLatin1Char('.') + info.name.section(QLatin1Char('.'), -1).toLower()
                        : QString();
                    out.append(info);
                    if (onProgress && (++count % 4096 == 0)
                        && !onProgress(count, r.path))
                        return false;
                    return true;
                });
                if (ok && !out.isEmpty()) return out;
                // enumerateAllWithMeta 失败 → 退到纯 USN 枚举（无 size）
                ok = usn.enumerateAll([&](const DiskOrganizer::UsnJournalReader::Record& r) {
                    FileInfo info;
                    info.absolutePath = r.path;
                    info.name = r.path.section(QLatin1Char('/'), -1);
                    info.isDir = r.isDirectory;
                    info.isSymlink = false;
                    info.size = r.size;
                    info.extension = info.name.contains(QLatin1Char('.'))
                        ? QLatin1Char('.') + info.name.section(QLatin1Char('.'), -1).toLower()
                        : QString();
                    out.append(info);
                    if (onProgress && (++count % 4096 == 0)
                        && !onProgress(count, r.path))
                        return false;
                    return true;
                });
                if (ok && !out.isEmpty()) return out;
                // 失败（权限/日志缺失）→ 回退遍历
            }
        }
    }

    // 并行扫描：先把各根目录展开成一级子目录分片，再 blockingMapped 多线程遍历。
    // 磁盘根目录的一级子目录往往分布在不同目录树分支，并行度好。
    QStringList shards;
    for (const QString& root : rootPaths) {
        if (!QFileInfo::exists(root)) continue;
        const QStringList subDirs = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (subDirs.isEmpty()) {
            shards << root;
        } else {
            for (const QString& sub : subDirs)
                shards << root + QLatin1Char('/') + sub;
            shards << root;   // 根层散文件
        }
    }

    QAtomicInteger<qint64> scannedCount{0};
    QAtomicInteger<bool> cancelled{false};

    auto results = QtConcurrent::blockingMapped(shards,
        [this, &cancelled, &scannedCount, &onProgress](const QString& shard) -> QList<FileInfo> {
            QList<FileInfo> local;
            local.reserve(1024);
            QDirIterator it(shard, QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            int sinceReport = 0;
            while (it.hasNext()) {
                if (cancelled.loadRelaxed()) break;
                it.next();
                const QFileInfo fi = it.fileInfo();
                if (!fi.exists()) continue;
                FileInfo info;
                info.absolutePath = fi.absoluteFilePath();
                info.name = fi.fileName();
                info.size = fi.size();
                info.lastModified = fi.lastModified().toMSecsSinceEpoch();
                info.isDir = false;
                info.isSymlink = fi.isSymLink();
                info.extension = fi.suffix().isEmpty()
                    ? QString() : QLatin1Char('.') + fi.suffix().toLower();
                local.append(info);
                // 每线程每 256 个上报一次，线程安全累加
                if (onProgress && ++sinceReport >= 256) {
                    sinceReport = 0;
                    const qint64 total = scannedCount.fetchAndAddRelaxed(256) + 256;
                    if (!onProgress(total, info.absolutePath)) {
                        cancelled.storeRelaxed(true);
                        break;
                    }
                }
            }
            if (onProgress && sinceReport > 0)
                scannedCount.fetchAndAddRelaxed(sinceReport);
            return local;
        });

    QList<FileInfo> out;
    for (const auto& r : results) out += r;
    return out;
}

void ScannerService::cancel() {
    g_cancelRequested = true;
}
