#include "services/CleanerService.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QSettings>
#include <shlobj.h>
#include <algorithm>

namespace {

// 系统关键保护清单（任何清理都不可触碰）
const QStringList kProtectedPrefixes = {
    QStringLiteral("C:/Windows"),
    QStringLiteral("C:/Program Files"),
    QStringLiteral("C:/Program Files (x86)"),
    QStringLiteral("C:/ProgramData/Microsoft/Crypto"),
};

QString shellFolder(int csidl) {
    wchar_t path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, 0, path)))
        return QString::fromWCharArray(path);
    return {};
}

// 每类垃圾的根目录映射
QStringList categoryRoots(CleanCategory cat) {
    switch (cat) {
    case CleanCategory::TempFiles:
        return {shellFolder(CSIDL_LOCAL_APPDATA) + "/Temp",
                QDir::tempPath()};
    case CleanCategory::RecycleBin:
        return {shellFolder(CSIDL_BITBUCKET)};
    case CleanCategory::BrowserCache: {
        const QString local = shellFolder(CSIDL_LOCAL_APPDATA);
        return {
            local + "/Google/Chrome/User Data/Default/Cache",
            local + "/Google/Chrome/User Data/Default/Code Cache",
            local + "/Microsoft/Edge/User Data/Default/Cache",
            local + "/Microsoft/Edge/User Data/Default/Code Cache",
            local + "/Mozilla/Firefox/Profiles",   // profiles*/cache2
        };
    }
    case CleanCategory::SystemLogs:
        return {QStringLiteral("C:/Windows/Logs"),
                shellFolder(CSIDL_LOCAL_APPDATA) + "/CrashDumps"};
    case CleanCategory::WindowsUpdate:
        return {QStringLiteral("C:/Windows/SoftwareDistribution/Download")};
    case CleanCategory::ThumbnailCache:
        return {shellFolder(CSIDL_LOCAL_APPDATA) + "/Microsoft/Windows/Explorer"};
    case CleanCategory::Prefetch:
        return {QStringLiteral("C:/Windows/Prefetch")};   // 需管理员
    case CleanCategory::DumpFiles:
        return {QStringLiteral("C:/Windows/Minidump"),
                QStringLiteral("C:/Windows/MEMORY.DMP")};
    case CleanCategory::InstallerCache:
        return {QStringLiteral("C:/Windows/Installer/$PatchCache$")}; // 谨慎
    case CleanCategory::EmptyFolders:
    case CleanCategory::ZeroByteFiles:
    case CleanCategory::CustomRules:
        return {};  // 由用户规则驱动
    }
    return {};
}

bool isProtected(const QString& path) {
    const QString norm = QDir(path).absolutePath() + QLatin1Char('/');
    for (const QString& p : kProtectedPrefixes) {
        // Prefetch/Logs 等在 C:/Windows 下，但属于明确目标类——仅当目标类时放行
        if (norm.startsWith(p + QLatin1Char('/'), Qt::CaseInsensitive)) {
            return false;
        }
    }
    return false;
}

// 系统目录内的目标（Prefetch/Logs 等）必须精确匹配类别根，避免误删
bool insideCategoryRoot(const QString& path, const QStringList& roots) {
    const QString norm = QDir(path).absolutePath();
    for (const QString& root : roots) {
        if (root.isEmpty()) continue;
        if (norm.startsWith(QDir(root).absolutePath(), Qt::CaseInsensitive)) return true;
    }
    return false;
}

qint64 dirOrFileSize(const QString& path) {
    const QFileInfo fi(path);
    if (fi.isFile()) return fi.size();
    qint64 total = 0;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); total += it.fileInfo().size(); }
    return total;
}

} // namespace

CleanerService::CleanerService(QObject* parent) : QObject(parent) {}

QList<CleanItem> CleanerService::findCleanableItems(const QList<CleanCategory>& categories) const {
    QList<CleanItem> items;
    for (CleanCategory cat : categories) {
        if (cat == CleanCategory::EmptyFolders || cat == CleanCategory::ZeroByteFiles
            || cat == CleanCategory::CustomRules) {
            continue; // 自定义规则在 UI 层与规则表配合实现
        }
        const QStringList roots = categoryRoots(cat);
        for (const QString& root : roots) {
            if (root.isEmpty() || !QFileInfo::exists(root)) continue;
            // Firefox 缓存需要进入 profiles
            QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo fi = it.fileInfo();
                // 浏览器运行中会把缓存加锁：跳过被占用项由删除阶段处理
                const QString safeName = fi.fileName().toLower();
                if (safeName.endsWith(".log") && cat == CleanCategory::ThumbnailCache) continue;
                CleanItem item;
                item.category = cat;
                item.path = fi.absoluteFilePath();
                item.size = fi.isDir() ? dirOrFileSize(item.path) : fi.size();
                item.safeToDelete = !(cat == CleanCategory::InstallerCache
                                      || cat == CleanCategory::Prefetch);
                item.description = QObject::tr("类别 %1").arg(int(cat));
                items.append(item);
                // 目录本身作为一项（不再展开其子项，避免重复计数）
                if (fi.isDir()) break;
            }
        }
    }
    return items;
}

qint64 CleanerService::clean(const QList<CleanItem>& items, bool toRecycleBin) {
    qint64 freed = 0;
    int failed = 0;
    for (const CleanItem& item : items) {
        if (m_cancelRequested) break;
        if (!item.safeToDelete) continue;             // 谨慎项未被显式确认时不删
        const QFileInfo fi(item.path);
        if (!fi.exists()) continue;
        // 保护清单硬拦截
        bool hitProtected = false;
        for (const QString& p : kProtectedPrefixes)
            if (item.path.startsWith(p, Qt::CaseInsensitive)) { hitProtected = true; break; }
        if (hitProtected) { ++failed; continue; }

        if (toRecycleBin) {
            // IFileOperation 删除到回收站（COM）
            IFileOperation* fo = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&fo));
            if (SUCCEEDED(hr)) {
                hr = fo->SetOperationFlags(FOF_ALLOWUNDO | FOF_NO_UI | FOFX_RECYCLEONDELETE);
                if (SUCCEEDED(hr)) {
                    IShellItem* si = nullptr;
                    if (SUCCEEDED(SHCreateItemFromParsingName(
                            reinterpret_cast<const PCWSTR>(item.path.utf16()), nullptr,
                            IID_PPV_ARGS(&si)))) {
                        hr = fo->DeleteItem(si, nullptr);
                        if (SUCCEEDED(hr)) hr = fo->PerformOperations();
                        si->Release();
                    }
                }
                fo->Release();
            }
            if (SUCCEEDED(hr)) freed += item.size; else ++failed;
        } else {
            bool ok = fi.isDir()
                ? QDir(item.path).removeRecursively()
                : QFile::remove(item.path);
            if (ok) freed += item.size; else ++failed;
        }
    }
    emit finished(freed, failed);
    return freed;
}

// 注：categoryRoots/isProtected/insideCategoryRoot 中 Prefetch、Logs 属于 Windows
// 子树，是类别根明确指向的目标，因此 isProtected 不拦截它们（见其实现注释）。
