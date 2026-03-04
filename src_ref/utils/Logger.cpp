// Author: SeungJae Lee
// Logger: installs a global Qt message handler that mirrors logs to a rolling file.

#include "Logger.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMutexLocker>
#include <QStandardPaths>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

Logger *Logger::s_instance = nullptr;
QtMessageHandler Logger::s_previousHandler = nullptr;
QMutex Logger::s_mutex;

void Logger::initialize(const QString &appName)
{
    if (s_instance)
        return;

    // Try platform-specific writable locations; fall back to Documents/home if unavailable.
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty())
        baseDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (baseDir.isEmpty())
        baseDir = QDir::homePath();

    QDir dir(baseDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_hhmmss"));
    const QString fileName = QStringLiteral("%1_%2.log").arg(appName).arg(timestamp);
    const QString filePath = dir.filePath(fileName);

    s_instance = new Logger(filePath);
    s_previousHandler = qInstallMessageHandler(Logger::messageHandler);
    qInfo() << "Logging initialized. File:" << filePath;
}

QString Logger::logFilePath()
{
    QMutexLocker locker(&s_mutex);
    return s_instance ? s_instance->m_logPath : QString();
}

Logger::Logger(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_logPath(filePath)
    , m_logFile(filePath)
    , m_stream(&m_logFile)
{
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        qWarning() << "Failed to open log file for writing:" << filePath;
    else
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        m_stream.setEncoding(QStringConverter::Utf8);
#else
        m_stream.setCodec("UTF-8");
#endif
    }
}

void Logger::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    const char *typeName = "INFO";
    switch (type)
    {
    case QtDebugMsg:
        typeName = "DEBUG";
        break;
    case QtInfoMsg:
        typeName = "INFO";
        break;
    case QtWarningMsg:
        typeName = "WARN";
        break;
    case QtCriticalMsg:
        typeName = "CRITICAL";
        break;
    case QtFatalMsg:
        typeName = "FATAL";
        break;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const QString contextInfo = QStringLiteral("%1:%2").arg(QString::fromUtf8(context.file ? context.file : ""))
                                                       .arg(context.line);
    const QString formatted = QStringLiteral("[%1] [%2] %3 -- %4")
                                  .arg(timestamp, QString::fromLatin1(typeName), contextInfo, msg);

    {
        QMutexLocker locker(&s_mutex);
        if (s_instance && s_instance->m_logFile.isOpen())
        {
            s_instance->m_stream << formatted << '\n';
            s_instance->m_stream.flush();
        }
    }

    if (s_previousHandler)
        s_previousHandler(type, context, msg);

    if (type == QtFatalMsg)
        abort();
}
