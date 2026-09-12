#include "services/DefragService.h"
#include <QProcess>
#include <QRegularExpression>

namespace DiskOrganizer {

bool DefragService::isSsd(const QString& drive) {
    // 通过 PowerShell Get-PhysicalDevice 判断介质类型；失败保守返回 false
    QProcess p;
    p.start("powershell", {"-NoProfile", "-Command",
        QString("(Get-Partition -DriveLetter '%1').DiskNumber | ForEach-Object { "
                "(Get-PhysicalDisk -DeviceNumber $_).MediaType }").arg(drive.left(1))});
    p.waitForFinished(10000);
    return QString::fromLocal8Bit(p.readAllStandardOutput())
        .contains("SSD", Qt::CaseInsensitive);
}

static DefragResult runDefrag(const QStringList& args) {
    DefragResult r;
    QProcess p;
    p.start("defrag", args);
    r.success = p.waitForFinished(-1);
    r.output = QString::fromLocal8Bit(p.readAllStandardOutput());
    r.output += QString::fromLocal8Bit(p.readAllStandardError());
    // 解析碎片百分比：匹配常见 “xx% fragmented” / “碎片: xx%” / 纯百分比行
    QRegularExpression re(QStringLiteral("(\\d{1,3})\\s*%"),
                          QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(r.output);
    int best = 0;
    while (it.hasNext()) {
        const int v = it.next().captured(1).toInt();
        if (v >= 0 && v <= 100) best = v; // 取最后一个合理值
    }
    r.fragmentedPercent = best;
    return r;
}

DefragResult DefragService::analyze(const QString& drive) { return runDefrag({drive, "/A", "/V"}); }
DefragResult DefragService::defrag(const QString& drive) { return runDefrag({drive, "/D", "/V"}); }
DefragResult DefragService::optimize(const QString& drive) { return runDefrag({drive, "/O", "/V"}); }

} // namespace DiskOrganizer
