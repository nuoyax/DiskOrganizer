#pragma once
#include <QObject>
#include "models/FileInfo.h"

// 重复文件查找：先按 (size, 部分哈希) 粗筛，再全量哈希确认
struct DuplicateGroup {
    QList<FileInfo> files;   // 同一内容的多份拷贝
    qint64 wastedBytes = 0;  // (count-1) * size
};

class DuplicateFinder : public QObject {
    Q_OBJECT
public:
    explicit DuplicateFinder(QObject* parent = nullptr);
    void find(const QList<FileInfo>& allFiles, bool useContentHash = true);
    void cancel();

signals:
    void groupFound(const DuplicateGroup& group);
    void progress(int percent);
    void finished(int groupCount, qint64 totalWastedBytes);

private:
    bool m_cancelRequested = false;
};
