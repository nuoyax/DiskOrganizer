#pragma once
#include <QString>
#include <QList>
#include "models/FileInfo.h"

// 一个磁盘/分区条目
struct DiskItem {
    QString driveLetter;      // "C:"
    QString volumeLabel;
    qint64 totalBytes = 0;
    qint64 freeBytes = 0;
    QString fileSystem;       // NTFS / FAT32 / exFAT
    bool isSsd = false;

    qint64 usedBytes() const { return totalBytes - freeBytes; }
    double usedRatio() const { return totalBytes > 0 ? (double)usedBytes() / totalBytes : 0.0; }
};

using DiskItemList = QList<DiskItem>;
