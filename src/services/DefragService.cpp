#include "services/DefragService.h"
#include <QProcess>

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
    return r;
}

DefragResult DefragService::analyze(const QString& drive) { return runDefrag({drive, "/A", "/V"}); }
DefragResult DefragService::defrag(const QString& drive) { return runDefrag({drive, "/D", "/V"}); }
DefragResult DefragService::optimize(const QString& drive) { return runDefrag({drive, "/O", "/V"}); }

} // namespace DiskOrganizer
