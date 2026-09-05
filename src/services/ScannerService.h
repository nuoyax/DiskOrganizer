#pragma once
#include <QObject>
#include <QStringList>
#include "models/FileInfo.h"

// 目录扫描服务：递归遍历目录树，统计大小，支持取消与进度
class ScannerService : public QObject {
    Q_OBJECT
public:
    explicit ScannerService(QObject* parent = nullptr);

    void startScan(const QStringList& rootPaths);
    void cancel();

signals:
    void progress(int percent, const QString& currentPath);
    void fileScanned(const FileInfo& info);
    void finished(qint64 totalFiles, qint64 totalBytes, qint64 elapsedMs);

private:
    bool m_cancelRequested = false;
};
