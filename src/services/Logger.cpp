#include "services/Logger.h"

#include <QDateTime>
#include <QMutex>
#include <QThread>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

namespace DiskOrganizer {

Logger& Logger::instance() {
    static Logger g;
    return g;
}

void Logger::write(const QString& line) {
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    // %LOCALAPPDATA%/DiskOrganizer/DiskOrganizer.log（UAC 提权后 TEMP 可能指向别的用户）
    static const QString path = [] {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(base);
        return base + QStringLiteral("/DiskOrganizer.log");
    }();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    f.write(QString(QStringLiteral("[%1] [%2] %3\n"))
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")))
                .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
                .arg(line)
                .toUtf8());
}

} // namespace DiskOrganizer
