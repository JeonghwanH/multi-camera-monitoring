// Author: SeungJae Lee
// Logger interface: exposes static helpers to install file-backed Qt logging.

#pragma once

#include <QObject>
#include <QFile>
#include <QMutex>
#include <QString>
#include <QTextStream>

class Logger : public QObject
{
    Q_OBJECT

public:
    static void initialize(const QString &appName);
    static QString logFilePath();

private:
    explicit Logger(const QString &filePath, QObject *parent = nullptr);
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

    QString m_logPath;
    QFile m_logFile;
    QTextStream m_stream;

    static Logger *s_instance;
    static QtMessageHandler s_previousHandler;
    static QMutex s_mutex; // guards log stream access across threads
};
