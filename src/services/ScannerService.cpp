#include "services/ScannerService.h"
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
    QList<FileInfo> out;
    qint64 scanned = 0;
    for (const QString& root : rootPaths) {
        if (m_cancelRequested) break;
        if (!QFileInfo::exists(root)) continue;
        QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
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
            out.append(info);
            if (onProgress && (++scanned % 512 == 0)) {
                if (!onProgress(scanned, info.absolutePath)) {
                    m_cancelRequested = true;
                    break;
                }
            }
        }
    }
    return out;
}

void ScannerService::cancel() {
    g_cancelRequested = true;
}
