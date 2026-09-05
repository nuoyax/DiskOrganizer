#pragma once
#include <QCryptographicHash>
#include <QFile>
#include <QString>
#include <QByteArray>

namespace DiskOrganizer {

// 文件内容哈希：默认 SHA-1（性能与碰撞率的折中）；smallHead>0 时仅读前 N 字节做粗筛
inline QByteArray fileHash(const QString& path, int smallHead = 0) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash algo(QCryptographicHash::Sha1);
    if (smallHead > 0) {
        algo.addData(f.read(smallHead));
    } else {
        char buf[64 * 1024];
        qint64 n;
        while ((n = f.read(buf, sizeof(buf))) > 0) algo.addData(buf, (int)n);
    }
    return algo.result();
}

} // namespace DiskOrganizer
