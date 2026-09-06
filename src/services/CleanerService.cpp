#include "services/CleanerService.h"
#include "services/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QSettings>
#include <QtConcurrent/QtConcurrent>
#include <atomic>
#include <shlobj.h>
#include <shellapi.h>
#include <vector>

namespace {

// 扫描排除用：勿把 Program Files 整树当垃圾扫进来即可
const QStringList kScanExcludePrefixes = {
    QStringLiteral("C:/Program Files"),
    QStringLiteral("C:/Program Files (x86)"),
    QStringLiteral("C:/ProgramData/Microsoft/Crypto"),
};

QString shellFolder(int csidl) {
    wchar_t path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, 0, path)))
        return QDir::fromNativeSeparators(QString::fromWCharArray(path));
    return {};
}

QStringList categoryRoots(CleanCategory cat) {
    switch (cat) {
    case CleanCategory::TempFiles:
        return {shellFolder(CSIDL_LOCAL_APPDATA) + QStringLiteral("/Temp"),
                QDir::fromNativeSeparators(QDir::tempPath())};
    case CleanCategory::RecycleBin:
        return {}; // 特殊处理 SHQueryRecycleBin
    case CleanCategory::BrowserCache: {
        const QString local = shellFolder(CSIDL_LOCAL_APPDATA);
        return {
            local + QStringLiteral("/Google/Chrome/User Data/Default/Cache"),
            local + QStringLiteral("/Google/Chrome/User Data/Default/Code Cache"),
            local + QStringLiteral("/Microsoft/Edge/User Data/Default/Cache"),
            local + QStringLiteral("/Microsoft/Edge/User Data/Default/Code Cache"),
            local + QStringLiteral("/Mozilla/Firefox/Profiles"),
        };
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

// 允许删除的 Windows 子树（垃圾清理白名单）
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

qint64 dirOrFileSize(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists()) return 0;
    if (fi.isFile()) return fi.size();
    qint64 total = 0;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

// 单个顶层子项的大小统计：串行遍历该子树；多个顶层子项之间由调用方并行
void appendItem(QList<CleanItem>& items, CleanCategory cat, const QString& path,
                qint64 size, bool cautious, const QString& desc) {
    if (path.isEmpty() || size <= 0) return;
    const QString norm = QDir::fromNativeSeparators(path);
    for (const QString& ex : kScanExcludePrefixes) {
        if (norm.startsWith(ex, Qt::CaseInsensitive)) return;
    }
    CleanItem item;
    item.category = cat;
    item.path = norm;
    item.size = size;
    item.safeToDelete = !cautious;
    item.description = desc;
    items.append(item);
}

} // namespace

CleanerService::CleanerService(QObject* parent) : QObject(parent) {}

QList<CleanItem> CleanerService::findCleanableItems(const QList<CleanCategory>& categories) const {
    QList<CleanItem> items;
    for (CleanCategory cat : categories) {
        if (cat == CleanCategory::EmptyFolders || cat == CleanCategory::ZeroByteFiles
            || cat == CleanCategory::CustomRules)
            continue;

        // 回收站：用系统 API 汇总，不枚举 $Recycle.Bin
        if (cat == CleanCategory::RecycleBin) {
            SHQUERYRBINFO info = {};
            info.cbSize = sizeof(info);
            if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &info)) && info.i64Size > 0) {
                appendItem(items, cat, QStringLiteral("RecycleBin://"),
                           qint64(info.i64Size), false,
                           QObject::tr("回收站 %1 项").arg(qint64(info.i64NumItems)));
            }
            continue;
        }

        const bool cautious = (cat == CleanCategory::InstallerCache
                               || cat == CleanCategory::Prefetch);
        const QStringList roots = categoryRoots(cat);
        for (const QString& rootRaw : roots) {
            const QString root = QDir::fromNativeSeparators(rootRaw);
            if (root.isEmpty()) continue;
            const QFileInfo rootFi(root);
            if (!rootFi.exists()) continue;

            if (rootFi.isFile()) {
                appendItem(items, cat, root, rootFi.size(), cautious,
                           QObject::tr("系统文件"));
                continue;
            }

            // 顶层子项各作为一条（目录按总大小），避免旧逻辑「遇目录就 break」扫不全
            const QFileInfoList children = QDir(root).entryInfoList(
                QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
            if (children.isEmpty()) {
                // 空目录也报一下占用 0，便于用户知道扫过了；跳过 size<=0
                continue;
            }
            // 目录大小统计并行化：各顶层子目录独立遍历，互不依赖
            QVector<QFuture<qint64>> sizes;
            sizes.reserve(children.size());
            for (const QFileInfo& fi : children) {
                if (fi.isDir()) {
                    sizes.append(QtConcurrent::run([p = fi.absoluteFilePath()]() -> qint64 {
                        return dirOrFileSize(p);
                    }));
                }
            }
            int dirIdx = 0;
            for (const QFileInfo& fi : children) {
                qint64 sz = 0;
                if (fi.isDir()) {
                    sz = sizes[dirIdx++].result();
                } else {
                    sz = fi.size();
                }
                appendItem(items, cat, fi.absoluteFilePath(), sz, cautious,
                           fi.isDir() ? QObject::tr("目录") : QObject::tr("文件"));
            }
        }
    }
    LOG << "findCleanableItems categories=" << categories.size()
          << " items=" << items.size();
    return items;
}

// 单项删除（不含回收站整站清空），供并行调度调用
static bool deleteOne(const CleanItem& item, bool toRecycleBin) {
    const QString norm = QDir::fromNativeSeparators(item.path);
    const QString native = QDir::toNativeSeparators(norm);
    const QFileInfo fi(native);
    if (!fi.exists()) return false;
    if (isHardBlocked(norm)) return true; // 受保护项跳过，视为已处理

    if (toRecycleBin) {
        // moveToTrash 同卷 rename，最快；失败再回退 IFileOperation
        if (QFile::moveToTrash(native)) return true;
        IFileOperation* fo = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&fo));
        if (SUCCEEDED(hr)) {
            hr = fo->SetOperationFlags(FOF_ALLOWUNDO | FOF_NO_UI | FOFX_RECYCLEONDELETE);
            if (SUCCEEDED(hr)) {
                IShellItem* si = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(
                        reinterpret_cast<LPCWSTR>(native.utf16()), nullptr,
                        IID_PPV_ARGS(&si)))) {
                    hr = fo->DeleteItem(si, nullptr);
                    if (SUCCEEDED(hr)) hr = fo->PerformOperations();
                    si->Release();
                }
            }
            fo->Release();
        }
        return SUCCEEDED(hr);
    }
    return fi.isDir() ? QDir(native).removeRecursively() : QFile::remove(native);
}

qint64 CleanerService::clean(const QList<CleanItem>& items, bool toRecycleBin) {
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comOk = (comHr == S_OK || comHr == S_FALSE);

    qint64 freed = 0;
    int failed = 0;
    int skipped = 0;

    // 回收站整站清空单独处理（系统级 API，一次性）；其余项并行删除
    QList<CleanItem> normalItems;
    normalItems.reserve(items.size());
    for (const CleanItem& it : items) {
        if (it.path.startsWith(QStringLiteral("RecycleBin://"), Qt::CaseInsensitive)) {
            const HRESULT hr = SHEmptyRecycleBinW(
                nullptr, nullptr,
                SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
            if (SUCCEEDED(hr)) freed += it.size; else ++failed;
        } else {
            normalItems.append(it);
        }
    }

    if (!normalItems.isEmpty()) {
        // 先做存在性/保护检查，实际不删的项不进并行队列
        QList<CleanItem> deletable;
        deletable.reserve(normalItems.size());
        for (const CleanItem& it : normalItems) {
            const QString norm = QDir::fromNativeSeparators(it.path);
            if (!QFileInfo(QDir::toNativeSeparators(norm)).exists()) { ++failed; continue; }
            if (isHardBlocked(norm)) { ++skipped; continue; }
            deletable.append(it);
        }

        // 并行删除：QtConcurrent::map 在全局线程池跑满多核。
        // 各 worker 结果写入互斥的槽位（按索引），避免竞争，最后单线程汇总。
        const int total = deletable.size();
        std::vector<bool> okFlags(size_t(total), false);
        QAtomicInt doneCount{0};
        QFutureWatcher<void> watcher;
        QEventLoop loop;
        QObject::connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::map(deletable,
                [this, &deletable, &okFlags, &doneCount, total, toRecycleBin](const CleanItem& it) {
            if (m_cancelRequested) return;
            const int done = doneCount.fetchAndAddRelaxed(1) + 1;
            emit progress(done * 100 / qMax(1, total), it.path);
            // map 不带索引，用指针地址反查槽位；QList 连续存储可行
            const ptrdiff_t slot = &it - deletable.constData();
            okFlags[size_t(slot)] = deleteOne(it, toRecycleBin);
        }));
        loop.exec();

        for (int i = 0; i < total; ++i) {
            if (okFlags[size_t(i)]) freed += deletable.at(i).size;
            else ++failed;
        }
    }

    if (comOk) CoUninitialize();
    LOG << "clean done freed=" << freed << " failed=" << failed
          << " skippedProtected=" << skipped << " requested=" << items.size();
    emit finished(freed, failed);
    return freed;
}
