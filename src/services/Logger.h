#pragma once
#include <QString>
#include <QStringList>
#include <type_traits>

// 轻量文件日志：%LOCALAPPDATA%/DiskOrganizer/DiskOrganizer.log（追加，时间戳+线程id）。
// 用法：LOG << "msg" << value;（链式 <<，任意线程可用，临时对象析构时整行落盘）
namespace DiskOrganizer {

class Logger {
public:
    static Logger& instance();
    void write(const QString& line);
};

// 临时流对象：析构时整行落盘
class LogLine {
public:
    explicit LogLine(const char* file, int line) {
        m_msg = QStringLiteral("%1:%2").arg(file).arg(line);
    }
    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;
    ~LogLine() { Logger::instance().write(m_msg); }
    LogLine& operator<<(const QString& v) { m_msg += QLatin1Char(' ') + v; return *this; }
    LogLine& operator<<(const char* v) { return *this << QString::fromUtf8(v); }
    LogLine& operator<<(const QStringList& v) { return *this << v.join(QLatin1Char(',')); }
    LogLine& operator<<(bool v) { return *this << (v ? "true" : "false"); }
    template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
    LogLine& operator<<(T v) { return *this << QString::number(qint64(v)); }
private:
    QString m_msg;
};

} // namespace DiskOrganizer

#define LOG DiskOrganizer::LogLine(__FILE__, __LINE__)
