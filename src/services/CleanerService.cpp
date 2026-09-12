#include "services/CleanerService.h"
#include "services/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSettings>
#include <QtConcurrent/QtConcurrent>
#include <atomic>
#include <shlobj.h>
#include <shellapi.h>
#include <vector>

namespace {

const QStringList kScanExcludePrefixes = {
    QStringLiteral("C:/Program Files"),
    QStringLiteral("C:/Program Files (x86)"),
    QStringLiteral("C:/ProgramData/Microsoft/Crypto"),
};

QString shellFolder(int csidl) {
    wchar_t path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_FLAG_CREATE | csidl, nullptr, 0, path))
        || SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, 0, path)))
        return QDir::fromNativeSeparators(QString::fromWCharArray(path));
    return {};
}

bool isAllowedWindowsJunk(const QString& norm) {
    static const QStringList allow = {
        QStringLiteral("C:/Windows/Logs"),
        QStringLiteral("C:/Windows/Temp"),
        QStringLiteral("C:/Windows/Prefetch"),
        QStringLiteral("C:/Windows/SoftwareDistribution/Download"),
        QStringLiteral("C:/Windows/Minidump"),
        QStringLiteral("C:/Windows/MEMORY.DMP"),
        QStringLiteral("C:/Windows/Installer/$PatchCache$"),
    };
    for (const QString& p : allow) {
        if (norm.compare(p, Qt::CaseInsensitive) == 0) return true;
        if (norm.startsWith(p + QLatin1Char('/'), Qt::CaseInsensitive)) return true;
    }
    return false;
}

bool isHardBlocked(const QString& norm) {
    if (isAllowedWindowsJunk(norm)) return false;
    static const QStringList block = {
        QStringLiteral("C:/Windows"),
        QStringLiteral("C:/ProgramData/Microsoft/Crypto"),
        QStringLiteral("C:/System Volume Information"),
    };
    for (const QString& p : block) {
        if (norm.startsWith(p, Qt::CaseInsensitive)
            && (norm.size() == p.size() || norm.at(p.size()) == QLatin1Char('/')))
            return true;
    }
    return false;
}

void appendItem(QList<CleanItem>& items, CleanCategory cat, const QString& path,
                qint64 size, bool cautious, const QString& desc) {
    if (path.isEmpty() || size <= 0) return;
    const QString norm = QDir::fromNativeSeparators(path);
    for (const QString& ex : kScanExcludePrefixes) {
        if (norm.startsWith(ex, Qt::CaseInsensitive)) return;
    }
    if (isHardBlocked(norm)) return;
    CleanItem item;
    item.category = cat;
    item.path = norm;
    item.size = size;
    item.safeToDelete = !cautious;
    item.description = desc;
    items.append(item);
}

// 单次遍历：按「根下第一层子项」汇总大小（避免每个子目录再递归一遍）
QList<QPair<QString, qint64>> sizeTopLevelOnce(const QString& rootRaw) {
    QList<QPair<QString, qint64>> out;
    const QString root = QDir::fromNativeSeparators(rootRaw);
    if (root.isEmpty()) return out;
    const QFileInfo rootFi(root);
    if (!rootFi.exists()) return out;
    if (rootFi.isFile()) {
        out.append({root, rootFi.size()});
        return out;
    }

    QHash<QString, qint64> byTop; // 绝对路径 → 累计字节
    // 先登记顶层，保证空目录/空文件也能出现（大小 0 稍后过滤）
    const QFileInfoList top = QDir(root).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    byTop.reserve(top.size());
    for (const QFileInfo& fi : top)
        byTop.insert(QDir::fromNativeSeparators(fi.absoluteFilePath()), 0);

    // 顶层文件直接记 size；深层文件归到所属顶层目录
    QDirIterator it(root,
                    QDir::Files | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    // 限制极端大目录：超过 N 个文件后停止继续累加（已有抽样足够展示）
    constexpr int kMaxFiles = 200000;
    int nFiles = 0;
    const QString rootPrefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    while (it.hasNext() && nFiles < kMaxFiles) {
        it.next();
        ++nFiles;
        const QFileInfo fi = it.fileInfo();
        const qint64 sz = fi.size();
        if (sz <= 0) continue;
        const QString abs = QDir::fromNativeSeparators(fi.absoluteFilePath());
        // 相对 root 的第一段
        if (!abs.startsWith(rootPrefix, Qt::CaseInsensitive)
            && abs.compare(root, Qt::CaseInsensitive) != 0)
            continue;
        QString rel = abs.mid(rootPrefix.size());
        const int slash = rel.indexOf(QLatin1Char('/'));
        const QString topName = slash < 0 ? rel : rel.left(slash);
        if (topName.isEmpty()) continue;
        const QString topPath = rootPrefix + topName;
        byTop[topPath] += sz;
    }

    out.reserve(byTop.size());
    for (auto it2 = byTop.constBegin(); it2 != byTop.constEnd(); ++it2) {
        if (it2.value() > 0)
            out.append({it2.key(), it2.value()});
    }
    std::sort(out.begin(), out.end(),
              [](const QPair<QString, qint64>& a, const QPair<QString, qint64>& b) {
                  return a.second > b.second;
              });
    // 每类根只保留头部大项，避免 UI 被上万碎文件拖垮
    if (out.size() > 80)
        out.resize(80);
    return out;
}

qint64 dirOrFileSizeFast(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists()) return 0;
    if (fi.isFile()) return fi.size();
    qint64 total = 0;
    int n = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext() && n < 200000) {
        it.next();
        ++n;
        total += it.fileInfo().size();
    }
    return total;
}

QStringList categoryRoots(CleanCategory cat) {
    switch (cat) {
    case CleanCategory::TempFiles:
        return {shellFolder(CSIDL_LOCAL_APPDATA) + QStringLiteral("/Temp"),
                QDir::fromNativeSeparators(QDir::tempPath()),
                QStringLiteral("C:/Windows/Temp")};
    case CleanCategory::RecycleBin:
        return {};
    case CleanCategory::BrowserCache: {
        const QString local = shellFolder(CSIDL_LOCAL_APPDATA);
        QStringList roots = {
            local + QStringLiteral("/Google/Chrome/User Data/Default/Cache"),
            local + QStringLiteral("/Google/Chrome/User Data/Default/Code Cache"),
            local + QStringLiteral("/Google/Chrome/User Data/Default/GPUCache"),
            local + QStringLiteral("/Microsoft/Edge/User Data/Default/Cache"),
            local + QStringLiteral("/Microsoft/Edge/User Data/Default/Code Cache"),
            local + QStringLiteral("/Microsoft/Edge/User Data/Default/GPUCache"),
        };
        // Firefox：只扫各 profile 下的 cache 目录，禁止整树 Profiles
        const QString ffRoot = local + QStringLiteral("/Mozilla/Firefox/Profiles");
        if (QDir(ffRoot).exists()) {
            const QFileInfoList profiles = QDir(ffRoot).entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& p : profiles) {
                const QString base = QDir::fromNativeSeparators(p.absoluteFilePath());
                roots << base + QStringLiteral("/cache2");
                roots << base + QStringLiteral("/startupCache");
                roots << base + QStringLiteral("/offlineCache");
            }
        }
        return roots;
    }
    case CleanCategory::SystemLogs:
        return {QStringLiteral("C:/Windows/Logs"),
                shellFolder(CSIDL_LOCAL_APPDATA) + QStringLiteral("/CrashDumps")};
    case CleanCategory::WindowsUpdate:
        return {QStringLiteral("C:/Windows/SoftwareDistribution/Download")};
    case CleanCategory::ThumbnailCache:
        return {shellFolder(CSIDL_LOCAL_APPDATA) + QStringLiteral("/Microsoft/Windows/Explorer")};
    case CleanCategory::Prefetch:
        return {QStringLiteral("C:/Windows/Prefetch")};
    case CleanCategory::DumpFiles:
        return {QStringLiteral("C:/Windows/Minidump"),
                QStringLiteral("C:/Windows/MEMORY.DMP")};
    case CleanCategory::InstallerCache:
        return {QStringLiteral("C:/Windows/Installer/$PatchCache$")};
    case CleanCategory::EmptyFolders:
    case CleanCategory::ZeroByteFiles:
    case CleanCategory::CustomRules:
        return {};
    }
    return {};
}

QList<CleanItem> scanCategory(CleanCategory cat,
                              const QStringList& customRoots,
                              const QStringList& userExclude) {
    QList<CleanItem> items;

    if (cat == CleanCategory::EmptyFolders) {
        // 仅 Temp，禁止扫整棵 Documents（此前是主要卡顿源）
        const QStringList roots = {
            shellFolder(CSIDL_LOCAL_APPDATA) + QStringLiteral("/Temp"),
            QDir::fromNativeSeparators(QDir::tempPath()),
            QStringLiteral("C:/Windows/Temp"),
        };
        for (const QString& root : roots) {
            if (root.isEmpty() || !QDir(root).exists()) continue;
            // 只扫到浅层，避免海量目录
            QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            int count = 0;
            int visited = 0;
            while (it.hasNext() && count < 200 && visited < 5000) {
                const QString path = it.next();
                ++visited;
                const QString norm = QDir::fromNativeSeparators(path);
                // 相对 root 深度限制 ≤ 3
                QString rel = norm.mid(root.size());
                if (rel.startsWith(QLatin1Char('/'))) rel = rel.mid(1);
                if (rel.count(QLatin1Char('/')) > 2) continue;
                bool excluded = false;
                for (const QString& ex : userExclude) {
                    if (norm.startsWith(QDir::fromNativeSeparators(ex), Qt::CaseInsensitive)) {
                        excluded = true;
                        break;
                    }
                }
                if (excluded || isHardBlocked(norm)) continue;
                // 用 iterator 判空，比 entryInfoList 轻
                QDirIterator childIt(path, QDir::AllEntries | QDir::NoDotAndDotDot
                                             | QDir::Hidden | QDir::System);
                if (!childIt.hasNext()) {
                    CleanItem item;
                    item.category = cat;
                    item.path = norm;
                    item.size = 4096;
                    item.safeToDelete = true;
                    item.description = QObject::tr("空文件夹");
                    items.append(item);
                    ++count;
                }
            }
        }
        return items;
    }

    if (cat == CleanCategory::ZeroByteFiles) {
        // 仅 Temp 浅扫，禁止整棵 Downloads
        const QStringList roots = {
            shellFolder(CSIDL_LOCAL_APPDATA) + QStringLiteral("/Temp"),
            QDir::fromNativeSeparators(QDir::tempPath()),
        };
        for (const QString& root : roots) {
            if (root.isEmpty() || !QDir(root).exists()) continue;
            QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            int count = 0;
            int visited = 0;
            while (it.hasNext() && count < 500 && visited < 20000) {
                it.next();
                ++visited;
                const QFileInfo fi = it.fileInfo();
                if (fi.size() != 0) continue;
                const QString norm = QDir::fromNativeSeparators(fi.absoluteFilePath());
                if (isHardBlocked(norm)) continue;
                CleanItem item;
                item.category = cat;
                item.path = norm;
                item.size = 1; // 占位，便于列表展示；实际占用可忽略
                item.safeToDelete = true;
                item.description = QObject::tr("零字节文件");
                items.append(item);
                ++count;
            }
        }
        return items;
    }

    if (cat == CleanCategory::CustomRules) {
        for (const QString& raw : customRoots) {
            const QString path = QDir::fromNativeSeparators(raw);
            if (path.isEmpty() || !QFileInfo::exists(path)) continue;
            if (isHardBlocked(path)) continue;
            const qint64 sz = dirOrFileSizeFast(path);
            appendItem(items, cat, path, sz, true, QObject::tr("自定义路径"));
        }
        return items;
    }

    if (cat == CleanCategory::RecycleBin) {
        SHQUERYRBINFO info = {};
        info.cbSize = sizeof(info);
        if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &info)) && info.i64Size > 0) {
            appendItem(items, cat, QStringLiteral("RecycleBin://"),
                       qint64(info.i64Size), false,
                       QObject::tr("回收站 %1 项").arg(qint64(info.i64NumItems)));
        }
        return items;
    }

    const bool cautious = (cat == CleanCategory::InstallerCache
                           || cat == CleanCategory::Prefetch);
    for (const QString& rootRaw : categoryRoots(cat)) {
        const auto tops = sizeTopLevelOnce(rootRaw);
        for (const auto& [path, sz] : tops) {
            appendItem(items, cat, path, sz, cautious,
                       QFileInfo(path).isDir() ? QObject::tr("目录")
                                               : QObject::tr("文件"));
        }
    }
    return items;
}

} // namespace

CleanerService::CleanerService(QObject* parent) : QObject(parent) {}

QList<CleanItem> CleanerService::findCleanableItems(const QList<CleanCategory>& categories) const {
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "DiskOrganizer", "DiskOrganizer");
    const QStringList customRoots = settings.value("clean/customPaths").toStringList();
    const QStringList userExclude = settings.value("scan/excludePaths").toStringList();

    // 各类别互不依赖：并行扫描，显著缩短墙钟时间
    QList<CleanCategory> cats = categories;
    auto parts = QtConcurrent::blockingMapped(cats, [&](CleanCategory cat) {
        return scanCategory(cat, customRoots, userExclude);
    });

    QList<CleanItem> items;
    for (auto& part : parts)
        items += part;

    LOG << "findCleanableItems categories=" << categories.size()
        << " items=" << items.size();
    return items;
}

// 批量 Shell 删除（一次 SHFileOperation 多路径），远快于逐项 IFileOperation / moveToTrash
static bool shellDeleteBatch(const QStringList& paths, bool toRecycleBin) {
    if (paths.isEmpty()) return true;

    QString from;
    from.reserve(paths.size() * 64);
    for (const QString& p : paths) {
        from += QDir::toNativeSeparators(p);
        from += QChar(u'\0');
    }
    from += QChar(u'\0');

    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = reinterpret_cast<PCZZWSTR>(from.utf16());
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (toRecycleBin)
        op.fFlags |= FOF_ALLOWUNDO;

    const int rc = SHFileOperationW(&op);
    return rc == 0 && !op.fAnyOperationsAborted;
}

static bool deleteOneFallback(const CleanItem& item, bool toRecycleBin) {
    const QString native = QDir::toNativeSeparators(QDir::fromNativeSeparators(item.path));
    const QFileInfo fi(native);
    if (!fi.exists()) return false;
    if (toRecycleBin) {
        if (QFile::moveToTrash(native)) return true;
        return shellDeleteBatch({native}, true);
    }
    return fi.isDir() ? QDir(native).removeRecursively() : QFile::remove(native);
}

qint64 CleanerService::clean(const QList<CleanItem>& items, bool toRecycleBin) {
    qint64 freed = 0;
    int failed = 0;
    int skipped = 0;

    QList<CleanItem> deletable;
    deletable.reserve(items.size());
    for (const CleanItem& it : items) {
        if (it.path.startsWith(QStringLiteral("RecycleBin://"), Qt::CaseInsensitive)) {
            const HRESULT hr = SHEmptyRecycleBinW(
                nullptr, nullptr,
                SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
            if (SUCCEEDED(hr)) freed += it.size; else ++failed;
            continue;
        }
        const QString norm = QDir::fromNativeSeparators(it.path);
        if (!QFileInfo(QDir::toNativeSeparators(norm)).exists()) { ++failed; continue; }
        if (isHardBlocked(norm)) { ++skipped; continue; }
        deletable.append(it);
    }

    // SHFileOperation 路径缓冲约 32K；按路径数分批，避免逐项 COM
    constexpr int kBatchSize = 48;
    const int total = deletable.size();
    int done = 0;
    for (int start = 0; start < total && !m_cancelRequested; start += kBatchSize) {
        const int end = qMin(start + kBatchSize, total);
        QStringList batchPaths;
        batchPaths.reserve(end - start);
        for (int i = start; i < end; ++i)
            batchPaths.append(deletable.at(i).path);

        emit progress(done * 100 / qMax(1, total), batchPaths.constFirst());

        if (shellDeleteBatch(batchPaths, toRecycleBin)) {
            for (int i = start; i < end; ++i)
                freed += deletable.at(i).size;
            done = end;
            continue;
        }

        // 整批失败时逐项回退，避免一次失败丢整批
        for (int i = start; i < end && !m_cancelRequested; ++i) {
            const CleanItem& it = deletable.at(i);
            emit progress((i + 1) * 100 / qMax(1, total), it.path);
            if (deleteOneFallback(it, toRecycleBin))
                freed += it.size;
            else
                ++failed;
        }
        done = end;
    }

    if (total > 0)
        emit progress(100, QString());

    LOG << "clean done freed=" << freed << " failed=" << failed
        << " skippedProtected=" << skipped << " requested=" << items.size()
        << " recycle=" << toRecycleBin;
    emit finished(freed, failed);
    return freed;
}
