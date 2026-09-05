#pragma once
#include <QString>
#include <qint64>

namespace DiskOrganizer {

// 人类可读大小：1024 进制，保留 1 位小数（<10 时 2 位）
QString formatSize(qint64 bytes);

// 时长格式化 ms → "1h 23m 45s"
QString formatDuration(qint64 ms);

} // namespace DiskOrganizer
