#pragma once
#include <QString>
#include <QtGlobal>

// 单个文件/目录的元信息
struct FileInfo {
    QString absolutePath;
    QString name;
    qint64 size = 0;          // 字节
    qint64 lastModified = 0;  // epoch ms
    bool isDir = false;
    bool isSymlink = false;
    QString extension;        // 小写、含点
    QString hash;             // 去重时按需填充
};
