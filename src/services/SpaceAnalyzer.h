#pragma once
#include <QObject>
#include "models/FileInfo.h"

namespace DiskOrganizer {

// 空间分析：按目录聚合大小、按扩展名聚合
struct TypeStat {
    QString extension;
    qint64 totalBytes = 0;
    int count = 0;
};

class SpaceAnalyzer : public QObject {
    Q_OBJECT
public:
    explicit SpaceAnalyzer(QObject* parent = nullptr);

    // 目录大小排名（含子目录聚合）
    QList<QPair<QString, qint64>> directorySizes(const QList<FileInfo>& files) const;
    // 扩展名分布
    QList<TypeStat> typeDistribution(const QList<FileInfo>& files) const;
};

} // namespace DiskOrganizer
