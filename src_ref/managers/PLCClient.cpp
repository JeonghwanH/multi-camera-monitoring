// Author: SeungJae Lee
// PLCClient: communicates with the PLC controller over UDP using Mitsubishi MC protocol frames.

#include "PLCClient.h"

#include "utils/DebugConfig.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDataStream>
#include <QHostAddress>
#include <QMutexLocker>
#include <QtEndian>
#include <QUdpSocket>
#include <QTimer>
#include <QEventLoop>
#include <QElapsedTimer>

#include <cmath>
#include <limits>

namespace
{
constexpr quint8 kSubHeader = 0x50;
constexpr quint8 kNetworkNumber = 0x00;
constexpr quint8 kPlcNumber = 0xFF;
constexpr quint16 kIoNumber = 0x03FF;
constexpr quint8 kStationNumber = 0x00;
constexpr quint16 kMonitoringTimer = 0x000A;
constexpr quint8 kDeviceCodeD = 0xA8;
constexpr quint16 kReadCommand = 0x0401;
constexpr quint16 kWriteCommand = 0x1401;
constexpr quint16 kSubCommandWord = 0x0000;
constexpr int kDataStartIndex = 11;

constexpr int kAddrMove = 200;            // D200
constexpr int kAddrDistance = 202;        // D202
constexpr int kAddrMotorMove = 204;       // D204
constexpr int kAddrDistanceLimit = 210;   // D210
constexpr int kAddrMoveSpeed = 212;       // D212
constexpr int kAddrServoBusy = 300;       // D300
constexpr int kAddrServoStatus = 310;     // D310
}

PLCClient::PLCClient(QObject *parent)
    : QObject(parent)
{
}

PLCClient::~PLCClient()
{
    QMutexLocker locker(&m_mutex);
    if (m_socket)
    {
        m_socket->close();
        delete m_socket;
        m_socket = nullptr;
    }
}

void PLCClient::connectToController(const ConnectionInfo &info)
{
    QString validationError;
    if (!validateConnectionInfo(info, &validationError))
    {
        emit connectionStateChanged(info.id, State::Error, validationError);
        emit connectionStateChanged(info.id, State::Disconnected, QString());
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (!m_socket)
            m_socket = new QUdpSocket(this);

        m_socket->close();
        if (!m_socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::DefaultForPlatform))
        {
            const QString bindError = tr("Failed to prepare PLC socket: %1").arg(m_socket->errorString());
            locker.unlock();
            emit connectionStateChanged(info.id, State::Error, bindError);
            emit connectionStateChanged(info.id, State::Disconnected, QString());
            return;
        }

        m_active = info;
        m_hostAddress = QHostAddress(info.ipAddress);
        m_state = State::Connecting;
    }

    emitState(State::Connecting);

    const ConnectionInfo currentInfo = info;
    // Perform handshake on the next event loop tick so we do not block the calling thread.
    QTimer::singleShot(0, this, [this, currentInfo]() {
        QString handshakeError;
        const bool ok = testConnection(&handshakeError);

        QMutexLocker locker(&m_mutex);
        if (m_active.id != currentInfo.id)
            return; // superseded by another request

        if (!ok)
        {
            locker.unlock();
            emitState(State::Error, handshakeError.isEmpty() ? tr("Failed to reach PLC controller.") : handshakeError);

            locker.relock();
            if (m_socket)
                m_socket->close();
            m_state = State::Disconnected;
            locker.unlock();
            emitState(State::Disconnected);
            return;
        }

        m_state = State::Connected;
        locker.unlock();
        emitState(State::Connected);
    });
}

void PLCClient::disconnectFromController(const QString &id)
{
    {
        QMutexLocker locker(&m_mutex);
        if (!id.isEmpty() && !m_active.id.isEmpty() && id != m_active.id)
        {
            locker.unlock();
            emit connectionStateChanged(id, State::Disconnected, QString());
            return;
        }

        if (!m_socket)
        {
            m_state = State::Disconnected;
            locker.unlock();
            emitState(State::Disconnected);
            return;
        }

        m_state = State::Disconnecting;
    }

    emitState(State::Disconnecting);

    {
        QMutexLocker locker(&m_mutex);
        if (m_socket)
            m_socket->close();
        m_state = State::Disconnected;
    }

    emitState(State::Disconnected);
}

bool PLCClient::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_state == State::Connected;
}

PLCClient::ConnectionInfo PLCClient::activeConnection() const
{
    QMutexLocker locker(&m_mutex);
    return m_active;
}

bool PLCClient::testConnection(QString *errorMessage)
{
    // Simple read to verify connectivity and handshake success.
    QByteArray response;
    if (!sendReadCommand(kAddrServoBusy, 1, &response, errorMessage))
        return false;
    return true;
}

bool PLCClient::checkStatus(PlcStatus *status, QString *errorMessage)
{
    QByteArray busyPayload;
    if (!sendReadCommand(kAddrServoBusy, 2, &busyPayload, errorMessage))
        return false;

    if (busyPayload.size() < 4)
    {
        if (errorMessage)
            *errorMessage = tr("PLC status response is invalid.");
        return false;
    }

    const quint16 servoBusy = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(busyPayload.constData()));
    const quint16 waiting = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(busyPayload.constData() + 2));

    QByteArray statusPayload;
    if (!sendReadCommand(kAddrServoStatus, 1, &statusPayload, errorMessage))
        return false;

    if (statusPayload.size() < 2)
    {
        if (errorMessage)
            *errorMessage = tr("PLC mode response is invalid.");
        return false;
    }

    const quint16 mode = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(statusPayload.constData()));

    if (status)
    {
        status->servoBusy = servoBusy != 0;
        status->waiting = waiting != 0;
        status->mode = mode;
    }

    const bool ready = (servoBusy == 0) && (waiting == 1) && (mode == 1);
    if (!ready)
    {
        if (errorMessage)
        {
            *errorMessage = tr("PLC is not ready (busy=%1, waiting=%2, mode=%3).").arg(servoBusy).arg(waiting).arg(mode);
        }
        return false;
    }

    return true;
}

bool PLCClient::configureMove(double moveLimitMm, double moveSpeedMmPerMin, QString *errorMessage)
{
    if (!ensureSocket(errorMessage))
        return false;

    const QVector<quint16> limitWords = toMcDwordParts(moveLimitMm, 10000.0);
    if (!sendWriteCommand(kAddrDistanceLimit, limitWords, errorMessage))
        return false;

    const QVector<quint16> speedWords = toMcDwordParts(moveSpeedMmPerMin, 100.0);
    if (!sendWriteCommand(kAddrMoveSpeed, speedWords, errorMessage))
        return false;

    return true;
}

bool PLCClient::startOperation(double moveLimitMm, double moveSpeedMmPerMin, QString *errorMessage)
{
    if (!checkStatus(nullptr, errorMessage))
        return false;

    if (!configureMove(moveLimitMm, moveSpeedMmPerMin, errorMessage))
        return false;

    return sendWriteCommand(kAddrMove, {1}, errorMessage);
}

bool PLCClient::stopOperation(QString *errorMessage)
{
    return sendWriteCommand(kAddrMove, {0}, errorMessage);
}

bool PLCClient::sendDeviation(double deviationMm, QString *errorMessage)
{
    if (!sendWriteCommand(kAddrMotorMove, {1}, errorMessage))
        return false;

    const QVector<quint16> distanceWords = toMcDwordParts(-deviationMm, 10000.0);
    return sendWriteCommand(kAddrDistance, distanceWords, errorMessage);
}

void PLCClient::emitState(PLCClient::State state, const QString &message)
{
    QMutexLocker locker(&m_mutex);
    const QString id = m_active.id;
    locker.unlock();
    emit connectionStateChanged(id, state, message);
}

bool PLCClient::ensureSocket(QString *errorMessage)
{
    QMutexLocker locker(&m_mutex);
    if (!m_socket)
        m_socket = new QUdpSocket(this);

    if (m_socket->state() == QAbstractSocket::BoundState)
        return true;

    if (!m_socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::DefaultForPlatform))
    {
        if (errorMessage)
            *errorMessage = tr("Failed to prepare PLC socket: %1").arg(m_socket->errorString());
        return false;
    }

    return true;
}

bool PLCClient::validateConnectionInfo(const ConnectionInfo &info, QString *errorMessage) const
{
    QHostAddress address;
    if (!address.setAddress(info.ipAddress))
    {
        if (errorMessage)
            *errorMessage = tr("Invalid PLC IP address.");
        return false;
    }

    if (info.port == 0)
    {
        if (errorMessage)
            *errorMessage = tr("Invalid PLC port.");
        return false;
    }

    return true;
}

bool PLCClient::sendReadCommand(int address, int count, QByteArray *outPayload, QString *errorMessage)
{
    if (count <= 0)
    {
        if (outPayload)
            outPayload->clear();
        return true;
    }

    if (!ensureSocket(errorMessage))
        return false;

    QMutexLocker locker(&m_mutex);
    if (!m_socket)
    {
        if (errorMessage)
            *errorMessage = tr("PLC socket is not initialized.");
        return false;
    }

    if (m_active.ipAddress.isEmpty() || m_active.port == 0)
    {
        if (errorMessage)
            *errorMessage = tr("PLC connection is not initialized.");
        return false;
    }

    QUdpSocket *socket = m_socket;
    const QHostAddress host = m_hostAddress;
    const quint16 port = m_active.port;

    if (DebugConfig::isDebugLoggingEnabled())
    {
        qInfo().nospace() << "[PLCClient] sendReadCommand addr=" << address
                          << " count=" << count
                          << " host=" << host.toString() << ":" << port;
    }

    QElapsedTimer elapsed;
    elapsed.start();

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(kReadCommand);
    stream << quint16(kSubCommandWord);
    const QByteArray addressBytes = addressToBytes(address);
    stream.writeRawData(addressBytes.constData(), addressBytes.size());
    stream << quint8(kDeviceCodeD);
    stream << quint16(count);

    const QByteArray frame = buildFrame(payload);
    const qint64 written = socket->writeDatagram(frame, host, port);
    if (written != frame.size())
    {
        if (DebugConfig::isDebugLoggingEnabled())
            qInfo() << "[PLCClient] Failed to send read command" << socket->errorString();
        if (errorMessage)
            *errorMessage = tr("Failed to send PLC command: %1").arg(socket->errorString());
        return false;
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QMetaObject::Connection readyConn = QObject::connect(socket, &QUdpSocket::readyRead, &loop, &QEventLoop::quit);

    timeout.start(m_socketTimeoutMs);

    locker.unlock();
    loop.exec(QEventLoop::AllEvents);
    locker.relock();

    QObject::disconnect(readyConn);

    if (!timeout.isActive())
    {
        if (DebugConfig::isDebugLoggingEnabled())
            qInfo() << "[PLCClient] Read command timed out";
        if (errorMessage)
            *errorMessage = tr("PLC response timed out.");
        return false;
    }

    timeout.stop();

    QByteArray datagram;
    QHostAddress sender;
    quint16 senderPort = 0;
    bool payloadCaptured = false;

    while (socket->hasPendingDatagrams())
    {
        datagram.resize(int(socket->pendingDatagramSize()));
        socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        if (sender == host && senderPort == port)
        {
            payloadCaptured = true;
            break;
        }
    }

    if (!payloadCaptured)
    {
        if (DebugConfig::isDebugLoggingEnabled())
            qInfo() << "[PLCClient] No datagram captured for read response";
        if (errorMessage)
            *errorMessage = tr("PLC sent no valid response.");
        return false;
    }

    if (datagram.size() < kDataStartIndex + count * 2)
    {
        if (DebugConfig::isDebugLoggingEnabled())
            qInfo() << "[PLCClient] Response shorter than expected" << datagram.size();
        if (errorMessage)
            *errorMessage = tr("PLC response was shorter than expected.");
        return false;
    }

    const quint16 endCode = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(datagram.constData() + 9));
    if (endCode != 0)
    {
        if (DebugConfig::isDebugLoggingEnabled())
            qInfo() << "[PLCClient] PLC reported error code" << endCode;
        if (errorMessage)
            *errorMessage = tr("PLC reported error: %1").arg(endCode);
        return false;
    }

    const QByteArray payloadData = datagram.mid(kDataStartIndex, count * 2);
    if (outPayload)
        *outPayload = payloadData;

    if (DebugConfig::isDebugLoggingEnabled())
    {
        qInfo().nospace() << "[PLCClient] Read command completed addr=" << address
                          << " count=" << count
                          << " durationMs=" << elapsed.elapsed()
                          << " payloadBytes=" << payloadData.size();
    }

    return true;
}

bool PLCClient::sendWriteCommand(int address, const QVector<quint16> &values, QString *errorMessage)
{
    if (values.isEmpty())
        return true;

    if (!ensureSocket(errorMessage))
        return false;

    QMutexLocker locker(&m_mutex);
    if (!m_socket)
    {
        if (errorMessage)
            *errorMessage = tr("PLC socket is not initialized.");
        return false;
    }

    if (m_active.ipAddress.isEmpty() || m_active.port == 0)
    {
        if (errorMessage)
            *errorMessage = tr("PLC connection is not initialized.");
        return false;
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(kWriteCommand);
    stream << quint16(kSubCommandWord);
    const QByteArray addressBytes = addressToBytes(address);
    stream.writeRawData(addressBytes.constData(), addressBytes.size());
    stream << quint8(kDeviceCodeD);
    stream << quint16(values.size());
    for (quint16 value : values)
        stream << value;

    const QByteArray frame = buildFrame(payload);
    const qint64 written = m_socket->writeDatagram(frame, m_hostAddress, m_active.port);
    if (written != frame.size())
    {
        if (errorMessage)
            *errorMessage = tr("Failed to send PLC command: %1").arg(m_socket->errorString());
        return false;
    }

    return true;
}

QByteArray PLCClient::buildFrame(const QByteArray &payload) const
{
    QByteArray frame;
    frame.resize(11 + payload.size());

    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint8(kSubHeader);
    stream << quint8(0x00);
    stream << quint8(kNetworkNumber);
    stream << quint8(kPlcNumber);
    stream << quint16(kIoNumber);
    stream << quint8(kStationNumber);
    stream << quint16(quint16(payload.size() + 2));
    stream << quint16(kMonitoringTimer);
    stream.writeRawData(payload.constData(), payload.size());

    return frame;
}

QByteArray PLCClient::addressToBytes(int address)
{
    QByteArray out;
    out.resize(3);
    out[0] = static_cast<char>(address & 0xFF);
    out[1] = static_cast<char>((address >> 8) & 0xFF);
    out[2] = static_cast<char>((address >> 16) & 0xFF);
    return out;
}

QVector<quint16> PLCClient::toMcDwordParts(double value, double scale)
{
    const double scaledValue = std::round(value * scale);
    qint64 asInteger = static_cast<qint64>(scaledValue);
    if (asInteger > std::numeric_limits<qint32>::max())
        asInteger = std::numeric_limits<qint32>::max();
    else if (asInteger < std::numeric_limits<qint32>::min())
        asInteger = std::numeric_limits<qint32>::min();

    const quint32 unsignedValue = static_cast<quint32>(static_cast<qint32>(asInteger));

    QVector<quint16> parts;
    parts.reserve(2);
    parts.append(static_cast<quint16>(unsignedValue & 0xFFFF));
    parts.append(static_cast<quint16>((unsignedValue >> 16) & 0xFFFF));
    return parts;
}
