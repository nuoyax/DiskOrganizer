#include "services/Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMutex>
#include <QThread>
#include <QFile>
#include <QDir>

namespace DiskOrganizer {

Logger& Logger::instance() {
    static Logger g;
    return g;
}

void Logger::write(const QString& line) {
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    // exe 同目录/DiskOrganizer.log（便携式，随 exe 走，便于直接查看）
    static const QString path = [] {
        const QString base = QCoreApplication::applicationDirPath();
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
