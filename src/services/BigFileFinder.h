#pragma once
#include <QObject>
#include "models/FileInfo.h"

// 大文件 / 旧文件查找
struct BigFileFilter {
    qint64 minSizeBytes = 100LL * 1024 * 1024;  // 默认 >=100MB
    qint64 olderThanDays = 0;                    // 0=不过滤
    QString extensionFilter;                     // 空=全部
    int topN = 100;
};

class BigFileFinder : public QObject {
    Q_OBJECT
public:
    explicit BigFileFinder(QObject* parent = nullptr);
    QList<FileInfo> find(const QList<FileInfo>& allFiles, const BigFileFilter& filter) const;
};
