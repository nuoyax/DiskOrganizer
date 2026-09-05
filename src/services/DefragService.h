#pragma once
#include <QString>
#include "models/FileInfo.h"

// 碎片整理：调用 Windows defrag.exe / Defrag API，SSD 跳过并提示 TRIM
namespace DiskOrganizer {

struct DefragResult {
    bool success = false;
    QString output;        // defrag 命令输出
    int fragmentedPercent = 0;
};

class DefragService {
public:
    static bool isSsd(const QString& drive);       // SSD 不需要碎片整理
    static DefragResult analyze(const QString& drive);   // /A 分析
    static DefragResult defrag(const QString& drive);    // /D 整理（需管理员）
    static DefragResult optimize(const QString& drive);  // /O 针对 SSD 的优化
};

} // namespace DiskOrganizer
