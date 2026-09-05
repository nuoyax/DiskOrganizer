#pragma once
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include "models/FileInfo.h"
#include "models/DiskItem.h"

namespace DiskOrganizer {

inline FileInfo toFileInfo(const QFileInfo& fi) {
    FileInfo info;
    info.absolutePath = fi.absoluteFilePath();
    info.name = fi.fileName();
    info.size = fi.isDir() ? 0 : fi.size();
    info.lastModified = fi.lastModified().toMSecsSinceEpoch();
    info.isDir = fi.isDir();
    info.isSymlink = fi.isSymLink();
    info.extension = fi.suffix().toLower().prepend('.');
    if (fi.suffix().isEmpty()) info.extension.clear();
    return info;
}

inline DiskItemList enumerateDisks() {
    DiskItemList list;
    for (const QStorageInfo& si : QStorageInfo::mountedVolumes()) {
        if (!si.isValid() || !si.isReady()) continue;
        DiskItem item;
        item.driveLetter = si.rootPath();
        item.volumeLabel = si.displayName();
        item.totalBytes = si.bytesTotal();
        item.freeBytes = si.bytesFree();
        item.fileSystem = QString::fromUtf8(si.fileSystemType());
        list.append(item);
    }
    return list;
}

} // namespace DiskOrganizer
