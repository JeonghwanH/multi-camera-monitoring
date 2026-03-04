// Author: SeungJae Lee
// PLCClient interface: wraps Mitsubishi MC protocol UDP commands for PLC motion control.

#pragma once

#include <QObject>
#include <QMutex>
#include <QString>
#include <QVector>
#include <QByteArray>
#include <QHostAddress>

class QUdpSocket;

class PLCClient : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Disconnected,
        Connecting,
        Disconnecting,
        Connected,
        Error
    };

    struct ConnectionInfo
    {
        QString id;
        QString ipAddress;
        quint16 port = 0;
    };

    struct PlcStatus
    {
        bool servoBusy = false;
        bool waiting = false;
        int mode = 0;
    };

    explicit PLCClient(QObject *parent = nullptr);
    ~PLCClient() override;

    void connectToController(const ConnectionInfo &info);
    void disconnectFromController(const QString &id = QString());
    bool isConnected() const;
    ConnectionInfo activeConnection() const;

    bool testConnection(QString *errorMessage = nullptr);
    bool checkStatus(PlcStatus *status = nullptr, QString *errorMessage = nullptr);
    bool configureMove(double moveLimitMm, double moveSpeedMmPerMin, QString *errorMessage = nullptr);
    bool startOperation(double moveLimitMm = 10.0, double moveSpeedMmPerMin = 100.0, QString *errorMessage = nullptr);
    bool stopOperation(QString *errorMessage = nullptr);
    bool sendDeviation(double deviationMm, QString *errorMessage = nullptr);

signals:
    void connectionStateChanged(const QString &id, PLCClient::State state, const QString &message = QString());

private:
    void emitState(PLCClient::State state, const QString &message = QString());

    bool ensureSocket(QString *errorMessage = nullptr);
    bool validateConnectionInfo(const ConnectionInfo &info, QString *errorMessage = nullptr) const;
    bool sendReadCommand(int address, int count, QByteArray *outPayload = nullptr, QString *errorMessage = nullptr);
    bool sendWriteCommand(int address, const QVector<quint16> &values, QString *errorMessage = nullptr);
    QByteArray buildFrame(const QByteArray &payload) const;
    static QByteArray addressToBytes(int address);
    static QVector<quint16> toMcDwordParts(double value, double scale);

    QUdpSocket *m_socket = nullptr;
    QHostAddress m_hostAddress;
    ConnectionInfo m_active;
    PLCClient::State m_state = PLCClient::State::Disconnected;
    mutable QMutex m_mutex;
    int m_socketTimeoutMs = 3000;
};
