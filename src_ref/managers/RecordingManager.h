// Author: SeungJae Lee
// RecordingManager interface: handles media session lifecycle, metadata persistence, and snapshot management.

#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QElapsedTimer>
#include <QFile>

#include <memory>

class QCamera;
class QImageCapture;
class QMediaCaptureSession;
class QMediaRecorder;

class RecordingManager : public QObject
{
    Q_OBJECT

public:
    struct RecordingMetadata
    {
        QString cameraId;
        QString slotId;
        QString filePath;
        QString metadataPath;
        QString analysisPath;
        QString passLevel;
        QDateTime startedAt;
        QDateTime finishedAt;
        qint64 durationMs = 0;
    };

    explicit RecordingManager(QObject *parent = nullptr);
    ~RecordingManager() override;

    void setOutputDirectory(const QString &path);
    QString outputDirectory() const;

    bool startRecording(const QString &cameraId,
                       QCamera *camera,
                       QMediaCaptureSession *sharedSession,
                       const QString &slotId,
                       const QString &passLevel = QString());
    void stopRecording(const QString &cameraId);
    bool captureSnapshot(const QString &cameraId, QCamera *camera, const QString &slotId);
    bool deleteRecording(const QString &filePath);

    QVector<RecordingMetadata> recordings() const;
    void appendAnalysisResult(const QString &cameraId, const QJsonObject &result);

signals:
    void recordingStarted(const QString &cameraId);
    void recordingStopped(const QString &cameraId);
    void recordingAdded(const RecordingManager::RecordingMetadata &metadata);
    void recordingRemoved(const QString &filePath);
    void snapshotCaptured(const QString &cameraId, const QString &filePath);

private:
    struct ActiveRecording
    {
        std::unique_ptr<QMediaCaptureSession> ownedSession;
        QMediaCaptureSession *session = nullptr;
        std::unique_ptr<QMediaRecorder> recorder;
        std::unique_ptr<QImageCapture> imageCapture;
        RecordingMetadata metadata;
        bool usesSharedSession = false;
        std::unique_ptr<QFile> analysisFile;
        QElapsedTimer analysisTimer;
    };

    struct PendingSnapshot
    {
        std::unique_ptr<QMediaCaptureSession> session;
        std::unique_ptr<QImageCapture> capture;
        QString cameraId;
        QString filePath;
    };

    QString createOutputDirectoryIfNeeded() const;
    QString ensureSessionDirectory(const QString &slotId);
    QString buildVideoFilePath(const QString &cameraId, const QString &slotId, const QString &sessionPath) const;
    QString buildSnapshotFilePath(const QString &cameraId, const QString &slotId, const QString &sessionPath) const;
    QString metadataFilePathFor(const QString &videoFilePath) const;
    QString analysisFilePathFor(const QString &videoFilePath) const;
    QString makeSessionDirectoryName(const QString &slotId) const;
    QString sanitizeForFolder(const QString &value) const;
    void resetSessionDirectoryIfIdle();
    void tryRemoveEmptyDirectory(const QString &dirPath) const;
    void writeMetadata(const RecordingMetadata &metadata) const;
    void finalizeRecording(const QString &cameraId);
    void loadExistingRecordings();

    // State for output directory management and active recording sessions.
    QString m_outputDirectory;
    QString m_currentSessionDirectory;
    QHash<QString, std::shared_ptr<ActiveRecording>> m_activeRecordings;
    QVector<RecordingMetadata> m_recordings;
    QHash<QImageCapture *, PendingSnapshot *> m_pendingSnapshots;
};
