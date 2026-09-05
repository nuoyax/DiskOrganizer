#pragma once
#include <QObject>
#include <QStringList>
#include <atomic>
#include "models/FileInfo.h"

// 目录扫描服务：递归遍历目录树，统计大小，支持取消与进度
class ScannerService : public QObject {
    Q_OBJECT
public:
    explicit ScannerService(QObject* parent = nullptr);

    void startScan(const QStringList& rootPaths);
    void cancel();
    // 同步扫描（调用方自行放入后台线程）；返回所有条目（文件+目录）
    // onProgress 可选：定期回调 (已扫描文件数, 当前路径)，返回 false 中止
    // minFileSizeBytes > 0：小于该大小的文件直接跳过（大文件扫描提速）
    // minDirTotalBytes > 0：目录总大小低于阈值的整个目录跳过（含零碎小文件）
    QList<FileInfo> scanBlocking(const QStringList& rootPaths,
        const std::function<bool(qint64, const QString&)>& onProgress = {},
        qint64 minFileSizeBytes = 0, qint64 minDirTotalBytes = 0,
        std::function<bool()> cancelledFn = {});

signals:
    void progress(int percent, const QString& currentPath);
    void fileScanned(const FileInfo& info);
    void finished(qint64 totalFiles, qint64 totalBytes, qint64 elapsedMs);

private:
    std::atomic<bool> m_cancelRequested{false};
};
