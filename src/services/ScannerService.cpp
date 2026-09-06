#include "services/ScannerService.h"
#include "services/UsnJournalReader.h"
#include "services/Logger.h"
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

namespace {
// 快速聚合目录总大小（文件数多时用 QDirIterator 顺序遍历，不做任何分配）。
// 仅用于剪枝判断：小目录（总大小低于阈值）可整体跳过。
// cancelled 可选：逐文件检查，取消后立即返回 -1。
qint64 dirTotalSize(const QString& dir, const std::function<bool()>& cancelled = {}) {
    qint64 total = 0;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    int n = 0;
    while (it.hasNext()) {
        if (cancelled && (++n & 1023) == 0 && cancelled()) return -1;
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}
} // namespace

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
    const std::function<bool(qint64, const QString&)>& onProgress,
    qint64 minFileSizeBytes, qint64 minDirTotalBytes,
    std::function<bool()> cancelledFn) {
    // 快速路径：整卷扫描 + NTFS + 有权限 → 直接枚举 USN/MFT（秒级），
    // 失败则回退到并行目录树遍历。
    // cancelled：上层取消令牌（取消后不再回退遍历，直接返回空）。
    if (rootPaths.size() == 1) {
        const QString root = rootPaths.first();
        LOG << "scanBlocking root=" << root
              << " minFile=" << minFileSizeBytes << " minDir=" << minDirTotalBytes;
        // 整卷（"C:"/"C:/"）或卷内目录（"C:/dir"）都走 MFT 快速路径：
        // MFT 全量枚举后按路径前缀过滤，目录级扫描同样秒级。
        if (root.length() >= 2 && root[1] == QLatin1Char(':') && root[0].isLetter()) {
            DiskOrganizer::UsnJournalReader usn;
            if (usn.open(root[0].toLatin1())) {
                LOG << "USN volume opened, trying MFT direct read";
                QList<FileInfo> out;
                out.reserve(200000);
                qint64 count = 0;
                // 优先带元数据版本（size 来自 $DATA，name/mtime 来自 $FILE_NAME），
                // 大小阈值在 MFT 解析阶段剪枝（小文件不建节点/拼路径）。
                // 失败再退到纯 USN 枚举，最后才回退目录遍历。
                bool ok = usn.enumerateAllWithMeta([&](const DiskOrganizer::UsnJournalReader::Record& r) {
                    FileInfo info;
                    // 归一为 Windows 原生分隔符，展示/删除一致（C:/dir/file -> C:\dir\file）
                    info.absolutePath = QDir::fromNativeSeparators(r.path);
                    info.name = info.absolutePath.section(QLatin1Char('/'), -1);
                    info.isDir = r.isDirectory;
                    info.isSymlink = false;
                    info.size = r.size;
                    info.lastModified = static_cast<qint64>(r.lastModifiedMs);
                    info.extension = info.name.contains(QLatin1Char('.'))
                        ? QLatin1Char('.') + info.name.section(QLatin1Char('.'), -1).toLower()
                        : QString();
                    out.append(info);
                    if (onProgress && (++count % 4096 == 0)
                        && !onProgress(count, r.path))
                        return false;
                    return true;
                }, quint64(minFileSizeBytes), cancelledFn, root);
                LOG << "enumerateAllWithMeta ok=" << ok << " records=" << out.size();
                if (ok) return out; // 成功即返回（含 0 条 = 阈值下无匹配文件，属正常结果）
                // enumerateAllWithMeta 失败 → 退到纯 USN 枚举（无 size）
                if (cancelledFn && cancelledFn()) return {};
                ok = usn.enumerateAll([&](const DiskOrganizer::UsnJournalReader::Record& r) {
                    if (cancelledFn && cancelledFn()) return false;
                    FileInfo info;
                    info.absolutePath = QDir::fromNativeSeparators(r.path);
                    info.name = info.absolutePath.section(QLatin1Char('/'), -1);
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
                }, cancelledFn);
                LOG << "enumerateAll(plain) ok=" << ok << " records=" << out.size();
                if (ok) return out;
                // 失败（权限/日志缺失/取消）→ 回退遍历；已取消则直接返回
                if (cancelledFn && cancelledFn()) return {};
            }
        }
    }

    // 并行扫描：先把各根目录展开成一级子目录分片，再 blockingMapped 多线程遍历。
    LOG << "scanBlocking FALLBACK to directory walk, roots=" << rootPaths.join(',');
    // 磁盘根目录的一级子目录往往分布在不同目录树分支，并行度好。
    // 大文件扫描加速：先快速聚合每个一级子目录的总大小，
    // 低于 minDirTotalBytes 的整个目录（含零碎小文件）直接剪掉，不再遍历。
    // 预聚合本身可能很慢（串行遍历零碎目录树），放并行池里做 + 响应取消。
    QStringList shards;
    for (const QString& root : rootPaths) {
        if (!QFileInfo::exists(root)) continue;
        const QStringList subDirs = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (subDirs.isEmpty()) {
            shards << root;
        } else if (minDirTotalBytes > 0 && subDirs.size() > 0) {
            // 并行预聚合每个一级子目录总大小（可取消）
            QAtomicInteger<bool> pruneCancelled{false};
            const auto sizes = QtConcurrent::blockingMapped(subDirs,
                [&pruneCancelled, &cancelledFn, &root](const QString& sub) -> qint64 {
                    if (pruneCancelled.loadRelaxed()) return -1;
                    if (cancelledFn && cancelledFn()) { pruneCancelled.storeRelaxed(true); return -1; }
                    return dirTotalSize(root + QLatin1Char('/') + sub, cancelledFn);
                });
            if (cancelledFn && cancelledFn()) return {};
            for (int i = 0; i < subDirs.size(); ++i) {
                if (sizes[i] >= 0 && sizes[i] < minDirTotalBytes)
                    continue; // 小目录整体跳过
                shards << root + QLatin1Char('/') + subDirs[i];
            }
            shards << root;   // 根层散文件
        } else {
            for (const QString& sub : subDirs)
                shards << root + QLatin1Char('/') + sub;
            shards << root;
        }
    }

    QAtomicInteger<qint64> scannedCount{0};
    QAtomicInteger<bool> cancelled{false};

    const qint64 minFile = minFileSizeBytes;
    auto results = QtConcurrent::blockingMapped(shards,
        [this, &cancelled, &scannedCount, &onProgress, minFile, &cancelledFn](const QString& shard) -> QList<FileInfo> {
            QList<FileInfo> local;
            local.reserve(1024);
            QDirIterator it(shard, QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            int sinceReport = 0;
            int cancelCheck = 0;
            while (it.hasNext()) {
                if (cancelled.loadRelaxed()) break;
                // onProgress 未设置时也要响应取消（每 512 项检查一次）
                if (++cancelCheck >= 512) {
                    cancelCheck = 0;
                    if (cancelledFn && cancelledFn()) { cancelled.storeRelaxed(true); break; }
                }
                it.next();
                const QFileInfo fi = it.fileInfo();
                if (!fi.exists()) continue;
                if (minFile > 0 && fi.size() < minFile) continue; // 小文件跳过
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
    LOG << "dir walk done, files=" << out.size();
    return out;
}

void ScannerService::cancel() {
    g_cancelRequested = true;
}
