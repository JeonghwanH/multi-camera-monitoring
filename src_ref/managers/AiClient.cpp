// Author: SeungJae Lee
// AiClient: manages HTTP interactions with the AI analysis service and tracks analysis settings.

#include "AiClient.h"

#include "AiClientPayloadBuilder.h"
#include "utils/DebugConfig.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaEnum>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <QtMath>

namespace
{
// REST endpoints exposed by the AI backend.
constexpr auto kStartupPath = "/api/model/startup";
constexpr auto kShutdownPath = "/api/model/shutdown";
constexpr auto kUploadPath = "/api/queue/upload-image";
constexpr auto kAnalyzePath = "/api/inference/analyze-data";
}

AiClient::AiClient(QObject *parent)
    : QObject(parent)
{
    setNetworkAccessManager(nullptr);
}

AiClient::~AiClient()
{
    if (m_networkAccessManager)
    {
        disconnect(m_networkAccessManager, nullptr, this, nullptr);
        if (m_ownsNetworkAccessManager)
            delete m_networkAccessManager;
    }
    m_networkAccessManager = nullptr;
    m_ownsNetworkAccessManager = false;
}

void AiClient::setNetworkAccessManager(QNetworkAccessManager *manager)
{
    if (manager && manager == m_networkAccessManager)
        return;

    QNetworkAccessManager *oldManager = m_networkAccessManager;
    const bool oldOwned = m_ownsNetworkAccessManager;

    if (!manager)
    {
        manager = new QNetworkAccessManager(this);
        m_ownsNetworkAccessManager = true;
    }
    else
    {
        m_ownsNetworkAccessManager = false;
    }

    m_networkAccessManager = manager;

    if (m_networkAccessManager)
    {
        connect(m_networkAccessManager, &QNetworkAccessManager::finished,
                this, &AiClient::onReplyFinished);
    }

    if (oldManager)
    {
        disconnect(oldManager, nullptr, this, nullptr);
        if (oldOwned)
        {
            delete oldManager;
        }
    }
}

void AiClient::configureNoProxy(bool enabled)
{
    if (enabled)
    {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    }
    else
    {
        QNetworkProxy::setApplicationProxy(QNetworkProxy());
        QNetworkProxyFactory::setUseSystemConfiguration(true);
    }
}

void AiClient::setBaseUrl(const QString &baseUrl)
{
    const QString trimmed = baseUrl.trimmed();
    m_baseUrl = trimmed.isEmpty() ? QStringLiteral("http://127.0.0.1:8000") : trimmed;
}

void AiClient::setBaseUrl(const QUrl &baseUrl)
{
    if (baseUrl.isEmpty())
    {
        m_baseUrl = QStringLiteral("http://127.0.0.1:8000");
        return;
    }
    m_baseUrl = baseUrl.toString();
}

void AiClient::setStreamKey(const QString &key)
{
    m_streamKey = key.trimmed();
}

void AiClient::setSettings(const Settings &settings)
{
    if (m_settings.enableAnalysis == settings.enableAnalysis &&
        m_settings.modelName == settings.modelName &&
        qFuzzyCompare(m_settings.confidenceThreshold, settings.confidenceThreshold) &&
        m_settings.passNumber == settings.passNumber &&
        m_settings.passDirection == settings.passDirection &&
        m_settings.passLevel == settings.passLevel &&
        qFuzzyCompare(m_settings.torchLengthMm, settings.torchLengthMm) &&
        qFuzzyCompare(m_settings.detectionDotSizePx, settings.detectionDotSizePx))
    {
        return;
    }

    m_settings = settings;
    if (DebugConfig::isDebugLoggingEnabled())
    {
        qInfo().nospace()
            << "[AiClient] Settings applied enableAnalysis=" << (m_settings.enableAnalysis ? "true" : "false")
            << " model=" << m_settings.modelName
            << " confidence=" << QString::number(m_settings.confidenceThreshold, 'f', 2)
            << " passNumber=" << m_settings.passNumber
            << " passDirection=" << m_settings.passDirection
            << " passLevel=" << m_settings.passLevel
            << " torchLengthMm=" << QString::number(m_settings.torchLengthMm, 'f', 1)
            << " dotSizePx=" << QString::number(m_settings.detectionDotSizePx, 'f', 1);
    }
    // Notify listeners (UI/Pages) so they can refresh toggles and labels.
    emit settingsChanged(m_settings);
}

QNetworkRequest AiClient::makeJsonRequest(const QString &path) const
{
    QUrl url(QStringLiteral("%1%2").arg(m_baseUrl, path));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    return request;
}

void AiClient::attachAbortTimer(QNetworkReply *reply)
{
    if (!reply || m_requestTimeoutMs <= 0)
        return;

    QPointer<QNetworkReply> guard(reply);
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->setInterval(m_requestTimeoutMs);
    // Abort long-running requests to keep UI responsive; ownership tied to reply lifetime.
    connect(timer, &QTimer::timeout, reply, [guard]() {
        if (guard)
            guard->abort();
    });
    timer->start();
}

bool AiClient::allowInfoMessage(const QString &text)
{
    const QString trimmed = text.trimmed();
    return trimmed.startsWith(QStringLiteral("Model startup")) ||
           trimmed.startsWith(QStringLiteral("Model shutdown")) ||
           trimmed.startsWith(QStringLiteral("Startup response")) ||
           trimmed.startsWith(QStringLiteral("Shutdown response")) ||
           trimmed.startsWith(QStringLiteral("Analysis response")) ||
           trimmed.startsWith(QStringLiteral("Config loaded")) ||
           trimmed.startsWith(QStringLiteral("Response:"));
}

void AiClient::emitFilteredInfo(const QString &text)
{
    if (allowInfoMessage(text))
        emit infoMessage(text);
}

void AiClient::modelStartup(const QStringList &weldAnalysisType,
                            const QString &refType,
                            const QVariant &refScale)
{
    if (m_streamKey.isEmpty())
    {
        const QString msg = QStringLiteral("streamKey is empty");
        emit modelStartupFinished(false, msg, QJsonObject{});
        emit errorMessage(msg);
        return;
    }

    const QJsonObject body = AiClientPayloadBuilder::buildModelStartupPayload(
        m_streamKey,
        m_config,
        weldAnalysisType,
        refType,
        refScale);

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkRequest request = makeJsonRequest(QString::fromUtf8(kStartupPath));

    QStringList weldTypesLog;
    const QJsonArray typeArray = body.value(QStringLiteral("weldAnalysisType")).toArray();
    for (const QJsonValue &value : typeArray)
        weldTypesLog << value.toString();
    const QString refTypeLog = body.value(QStringLiteral("refType")).toString();
    const double refScaleLog = body.value(QStringLiteral("refScale")).toDouble();

    qInfo().nospace()
        << "[AiClient] Model startup request url=" << request.url().toString()
        << " baseUrl=" << m_baseUrl
        << " streamKey=" << m_streamKey
        << " weldTypes=" << weldTypesLog.join(QLatin1Char(','))
        << " refType=" << refTypeLog
        << " refScale=" << QString::number(refScaleLog, 'f', 1);

    emitFilteredInfo(QStringLiteral("REQUEST POST %1 %2")
                         .arg(request.url().toString(), QString::fromUtf8(payload)));

    Q_ASSERT(m_networkAccessManager);
    QNetworkReply *reply = m_networkAccessManager->post(request, payload);
    m_pending.insert(reply, Pending{RequestTag::Startup, {}});
    attachAbortTimer(reply);
    emitFilteredInfo(QStringLiteral("Model startup requested"));
}

void AiClient::modelShutdown()
{
    if (m_streamKey.isEmpty())
    {
        const QString msg = QStringLiteral("streamKey is empty");
        emit modelShutdownFinished(false, msg);
        emit errorMessage(msg);
        return;
    }

    QNetworkRequest request = makeJsonRequest(QString::fromUtf8(kShutdownPath));
    QJsonObject body;
    body.insert(QStringLiteral("streamKey"), m_streamKey);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    qInfo().nospace()
        << "[AiClient] Model shutdown request url=" << request.url().toString()
        << " baseUrl=" << m_baseUrl
        << " streamKey=" << m_streamKey;
    emitFilteredInfo(QStringLiteral("REQUEST POST %1 %2")
                         .arg(request.url().toString(), QString::fromUtf8(payload)));

    Q_ASSERT(m_networkAccessManager);
    QNetworkReply *reply = m_networkAccessManager->post(request, payload);
    m_pending.insert(reply, Pending{RequestTag::Shutdown, {}});
    attachAbortTimer(reply);
    emitFilteredInfo(QStringLiteral("Model shutdown requested"));
}

void AiClient::uploadImage(const QString &filePath)
{
    if (m_streamKey.isEmpty())
    {
        const QString msg = QStringLiteral("streamKey is empty");
        emit errorMessage(msg);
        emit imageUploaded(false, QString(), msg);
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        const QString msg = QStringLiteral("Failed to open file for upload: %1").arg(filePath);
        emit errorMessage(msg);
        emit imageUploaded(false, QString(), msg);
        return;
    }

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart keyPart;
    keyPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                      QVariant(QStringLiteral("form-data; name=\"streamKey\"")));
    keyPart.setBody(m_streamKey.toUtf8());
    multiPart->append(keyPart);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                                    .arg(QFileInfo(file).fileName())));
    filePart.setBody(file.readAll());
    multiPart->append(filePart);

    QNetworkRequest request(QStringLiteral("%1%2").arg(m_baseUrl, kUploadPath));
    Q_ASSERT(m_networkAccessManager);
    QNetworkReply *reply = m_networkAccessManager->post(request, multiPart);
    multiPart->setParent(reply);
    m_pending.insert(reply, Pending{RequestTag::UploadImage, filePath});
    attachAbortTimer(reply);
}

void AiClient::uploadImage(const QImage &image,
                           const QString &filename,
                           int quality)
{
    if (image.isNull())
    {
        emit errorMessage(QStringLiteral("uploadImage called with null QImage"));
        return;
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", quality);
    uploadImageBytes(bytes, filename);
}

void AiClient::analyzeData()
{
    if (m_streamKey.isEmpty())
    {
        const QString msg = QStringLiteral("streamKey is empty");
        emit analysisFinished(false, QJsonObject{}, msg);
        emit errorMessage(msg);
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("streamKey"), m_streamKey);

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkRequest request = makeJsonRequest(QString::fromUtf8(kAnalyzePath));

    emitFilteredInfo(QStringLiteral("REQUEST POST %1 %2")
                         .arg(request.url().toString(), QString::fromUtf8(payload)));

    Q_ASSERT(m_networkAccessManager);
    QNetworkReply *reply = m_networkAccessManager->post(request, payload);
    m_pending.insert(reply, Pending{RequestTag::Analyze, {}});
    attachAbortTimer(reply);
}

void AiClient::sendFrame(const QByteArray &jpegBytes)
{
    if (jpegBytes.isEmpty())
        return;

    if (m_sendingFrame)
    {
        // Queue latest frame when an upload is in-flight; older frames are dropped to avoid backlog.
        if (DebugConfig::isDebugLoggingEnabled())
        {
            if (m_pendingFrame.isEmpty())
            {
                qInfo().nospace() << "[AiClient] Upload in progress. Queuing latest frame (" << jpegBytes.size()
                                  << " bytes)";
            }
            else
            {
                qInfo().nospace() << "[AiClient] Replacing queued frame (" << jpegBytes.size()
                                  << " bytes) while upload is pending";
            }
        }
        m_pendingFrame = jpegBytes;
        return;
    }

    if (DebugConfig::isDebugLoggingEnabled())
        qInfo().nospace() << "[AiClient] Uploading frame immediately (" << jpegBytes.size() << " bytes)";

    uploadImageBytes(jpegBytes);
}

void AiClient::uploadImageBytes(const QByteArray &jpegBytes,
                                const QString &filename)
{
    if (m_streamKey.isEmpty())
    {
        const QString msg = QStringLiteral("streamKey is empty");
        emit errorMessage(msg);
        return;
    }

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart keyPart;
    keyPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                      QVariant(QStringLiteral("form-data; name=\"streamKey\"")));
    keyPart.setBody(m_streamKey.toUtf8());
    multiPart->append(keyPart);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(filename)));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/jpeg"));
    filePart.setBody(jpegBytes);
    multiPart->append(filePart);

    QNetworkRequest request(QStringLiteral("%1%2").arg(m_baseUrl, kUploadPath));
    Q_ASSERT(m_networkAccessManager);
    QNetworkReply *reply = m_networkAccessManager->post(request, multiPart);
    multiPart->setParent(reply);

    m_pending.insert(reply, Pending{RequestTag::UploadImage, filename});
    attachAbortTimer(reply);
    m_sendingFrame = true;

    if (DebugConfig::isDebugLoggingEnabled())
    {
        qInfo().nospace() << "[AiClient] Frame upload dispatched url=" << request.url().toString()
                          << " bytes=" << jpegBytes.size()
                          << " filename=" << filename;
    }
}

void AiClient::onReplyFinished(QNetworkReply *reply)
{
    if (!reply)
        return;

    const auto pendingIt = m_pending.find(reply);
    if (pendingIt == m_pending.end())
    {
        if (DebugConfig::isDebugLoggingEnabled())
            qInfo() << "[AiClient] Received reply with no pending entry" << reply;
        reply->deleteLater();
        return;
    }

    const Pending pending = pendingIt.value();
    m_pending.erase(pendingIt);

    const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const QVariant reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
    const auto networkError = reply->error();
    const QString errorString = reply->errorString();
    const QMetaEnum errorEnum = QMetaEnum::fromType<QNetworkReply::NetworkError>();
    const char *errorKey = errorEnum.valueToKey(networkError);
    const QString networkErrorLabel = errorKey ? QString::fromLatin1(errorKey) : errorString;
    const bool ok = (networkError == QNetworkReply::NoError) &&
                    statusCode.toInt() >= 200 && statusCode.toInt() < 300;

    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError{};
    const QJsonDocument json = QJsonDocument::fromJson(raw, &parseError);
    const QJsonObject obj = json.isObject() ? json.object() : QJsonObject{};
    const QString message = obj.value(QStringLiteral("message")).toString(
        ok ? QStringLiteral("OK")
           : QStringLiteral("%1 (%2)").arg(networkErrorLabel,
                                           reason.toString()));
    const QString status = obj.value(QStringLiteral("status")).toString();

    if (DebugConfig::isDebugLoggingEnabled())
    {
        const QString tagLabel = [pending]() {
            switch (pending.tag)
            {
            case RequestTag::Startup:
                return QStringLiteral("Startup");
            case RequestTag::Shutdown:
                return QStringLiteral("Shutdown");
            case RequestTag::UploadImage:
                return QStringLiteral("UploadImage");
            case RequestTag::Analyze:
                return QStringLiteral("Analyze");
            }
            return QStringLiteral("Unknown");
        }();
        const QString httpReason = reason.toString();
        qInfo().nospace()
            << "[AiClient] Reply finished tag=" << tagLabel
            << " status=" << statusCode.toInt()
            << " networkError=" << networkErrorLabel
            << " httpReason=" << (httpReason.isEmpty() ? QStringLiteral("-") : httpReason)
            << " payloadSize=" << raw.size();
        if (parseError.error != QJsonParseError::NoError)
        {
            qInfo().nospace()
                << "[AiClient] Response parse error offset=" << parseError.offset
                << " error=" << parseError.errorString();
        }
        else if (!raw.isEmpty() && raw.size() <= 1024)
        {
            qInfo().nospace() << "[AiClient] Response payload=" << QString::fromUtf8(raw);
        }
    }

    // Dispatch response handlers based on the original request tag.
    switch (pending.tag)
    {
    case RequestTag::Startup: {
        const QJsonObject modelInfo = obj.value(QStringLiteral("modelInfo")).toObject();
        emit modelStartupFinished(ok, message, modelInfo);
        const QString statusOut = status.isEmpty() ? (ok ? QStringLiteral("success") : QStringLiteral("error")) : status;
        emitFilteredInfo(QStringLiteral("Startup response: {\"status\": \"%1\"}").arg(statusOut));
        if (!ok)
            emit errorMessage(message);
        break;
    }
    case RequestTag::Shutdown: {
        emit modelShutdownFinished(ok, message);
        const QString statusOut = status.isEmpty() ? (ok ? QStringLiteral("success") : QStringLiteral("error")) : status;
        emitFilteredInfo(QStringLiteral("Shutdown response: {\"status\": \"%1\"}").arg(statusOut));
        if (!ok)
            emit errorMessage(message);
        break;
    }
    case RequestTag::UploadImage: {
        const QString uploadId = obj.value(QStringLiteral("uploadId")).toString();
        emit imageUploaded(ok, uploadId, message);

        if (DebugConfig::isDebugLoggingEnabled())
        {
            const QString statusOut = status.isEmpty() ? (ok ? QStringLiteral("success") : QStringLiteral("error")) : status;
            emitFilteredInfo(QStringLiteral("Response: {\"status\": \"%1\", \"uploadId\": \"%2\"}")
                                 .arg(statusOut, uploadId));
        }

        if (!ok)
            emit errorMessage(message);

        m_sendingFrame = false;
        if (!m_pendingFrame.isEmpty())
        {
            const QByteArray next = m_pendingFrame;
            m_pendingFrame.clear();
            uploadImageBytes(next);
        }
        break;
    }
    case RequestTag::Analyze: {
        const QJsonObject results = obj.value(QStringLiteral("results")).toObject();
        emit analysisFinished(ok, results, message);

        QString payload;
        if (!results.isEmpty())
            payload = QString::fromUtf8(QJsonDocument(results).toJson(QJsonDocument::Compact));
        else
            payload = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        if (DebugConfig::isDebugLoggingEnabled())
            emitFilteredInfo(QStringLiteral("Analysis response: %1").arg(payload));

        if (!ok)
            emit errorMessage(message);
        break;
    }
    }
}

bool AiClient::loadConfig(const QString &jsonPath)
{
    // Load persisted analysis configuration and update base URL / stream key overrides.
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        emit errorMessage(QStringLiteral("Config open failed: %1").arg(jsonPath));
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        emit errorMessage(QStringLiteral("Config parse error: %1").arg(parseError.errorString()));
        return false;
    }

    m_config = doc.object();

    const auto applyServer = [this](const QJsonObject &server) {
        const QString url = server.value(QStringLiteral("url")).toString().trimmed();
        if (!url.isEmpty())
            setBaseUrl(url);
        const QString key = server.value(QStringLiteral("streamKey")).toString().trimmed();
        if (!key.isEmpty())
            setStreamKey(key);
    };

    const QJsonObject analysisServer = m_config.value(QStringLiteral("analysisServer")).toObject();
    if (!analysisServer.isEmpty())
    {
        applyServer(analysisServer);
    }
    else
    {
        const QString baseUrl = m_config.value(QStringLiteral("baseUrl")).toString().trimmed();
        if (!baseUrl.isEmpty())
            setBaseUrl(baseUrl);
        const QString streamKey = m_config.value(QStringLiteral("streamKey")).toString().trimmed();
        if (!streamKey.isEmpty())
            setStreamKey(streamKey);
    }

    emitFilteredInfo(QStringLiteral("Config loaded: baseUrl=%1, streamKey=%2")
                         .arg(m_baseUrl, m_streamKey));
    if (DebugConfig::isDebugLoggingEnabled())
    {
        qInfo().nospace() << "[AiClient] Config loaded baseUrl=" << m_baseUrl
                          << " streamKey=" << m_streamKey;
    }
    return true;
}
