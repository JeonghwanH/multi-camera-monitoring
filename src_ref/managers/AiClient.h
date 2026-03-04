// Author: SeungJae Lee
// AiClient interface: exposes analysis settings and HTTP control hooks for the AI backend.

#pragma once

#include <QObject>
#include <QImage>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QVariant>

#include <QHash>
#include <QPointer>

#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QUrl>

class QNetworkReply;

/**
 * AiClient: 분석 서버와의 통신 및 분석 설정 관리.
 *
 * 참고: WeldingVisionSolution/src/AiClient.{h,cpp}
 */
class AiClient : public QObject
{
    Q_OBJECT

public:
    struct Settings
    {
        bool enableAnalysis = false;
        QString modelName;
        double confidenceThreshold = 0.5;
        int passNumber = 1;
        QString passDirection = QStringLiteral("UP");
        QString passLevel = QStringLiteral("Root");
        double torchLengthMm = 26.0;
        double detectionDotSizePx = 8.0;
    };

    enum class RequestTag {
        Startup,
        Shutdown,
        UploadImage,
        Analyze
    };

    explicit AiClient(QObject *parent = nullptr);
    ~AiClient() override;

    void setNetworkAccessManager(QNetworkAccessManager *manager);
    QNetworkAccessManager *networkAccessManager() const { return m_networkAccessManager; }

    // 전역 Proxy 제어 (NoProxy 강제)
    static void configureNoProxy(bool enabled);

    void setBaseUrl(const QString &baseUrl);
    void setBaseUrl(const QUrl &baseUrl);
    QString baseUrl() const { return m_baseUrl; }

    void setStreamKey(const QString &key);
    QString streamKey() const { return m_streamKey; }

    void setDataFilePath(const QString &path) { m_dataFilePath = path; }
    QString dataFilePath() const { return m_dataFilePath; }

    void setRequestTimeoutMs(int ms) { m_requestTimeoutMs = ms; }
    int requestTimeoutMs() const { return m_requestTimeoutMs; }

    bool loadConfig(const QString &jsonPath);
    QJsonObject currentConfig() const { return m_config; }

    void setSettings(const Settings &settings);
    Settings settings() const { return m_settings; }

public slots:
    void modelStartup(const QStringList &weldAnalysisType,
                      const QString &refType = QString(),
                      const QVariant &refScale = QVariant());
    void modelShutdown();
    void uploadImage(const QString &filePath);
    void uploadImage(const QImage &image,
                     const QString &filename = QStringLiteral("frame.jpg"),
                     int quality = 95);
    void analyzeData();
    void sendFrame(const QByteArray &jpegBytes);

signals:
    void settingsChanged(const AiClient::Settings &settings);

    void infoMessage(const QString &text);
    void errorMessage(const QString &text);

    void modelStartupFinished(bool ok, const QString &message, const QJsonObject &modelInfo);
    void modelShutdownFinished(bool ok, const QString &message);
    void imageUploaded(bool ok, const QString &uploadId, const QString &message);
    void analysisFinished(bool ok, const QJsonObject &results, const QString &message);

private:
    struct Pending
    {
        RequestTag tag;
        QString extra;
    };

    void uploadImageBytes(const QByteArray &jpegBytes,
                          const QString &filename = QStringLiteral("frame.jpg"));

    QNetworkRequest makeJsonRequest(const QString &path) const;
    void attachAbortTimer(QNetworkReply *reply);
    static bool allowInfoMessage(const QString &text);
    void emitFilteredInfo(const QString &text);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    Settings m_settings;

    QJsonObject m_config;
    QString m_baseUrl = QStringLiteral("http://127.0.0.1:8000");
    QString m_streamKey;
    QString m_dataFilePath;
    int m_requestTimeoutMs = 15000;

    QNetworkAccessManager *m_networkAccessManager = nullptr;
    bool m_ownsNetworkAccessManager = false;
    QHash<QNetworkReply *, Pending> m_pending; // tracks outstanding requests to disambiguate responses
    QByteArray m_pendingFrame;
    bool m_sendingFrame = false;
};
