// Author: SeungJae Lee
// RecordingManager: coordinates video recording sessions, snapshots, and analysis metadata persistence.

#include "RecordingManager.h"

#include "utils/DebugConfig.h"

#include <QCamera>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageCapture>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace
{
constexpr int kMaxSessionFolderNameLength = 120;
}

RecordingManager::RecordingManager(QObject *parent)
    : QObject(parent)
{
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Weldbeing";
    m_outputDirectory = defaultDir;
    m_currentSessionDirectory.clear();
    loadExistingRecordings();
}

RecordingManager::~RecordingManager()
{
    qDeleteAll(m_pendingSnapshots);
    m_pendingSnapshots.clear();
}

void RecordingManager::setOutputDirectory(const QString &path)
{
    m_outputDirectory = path;
    loadExistingRecordings();
}

QString RecordingManager::outputDirectory() const
{
    return m_outputDirectory;
}

bool RecordingManager::startRecording(const QString &cameraId,
                                      QCamera *camera,
                                      QMediaCaptureSession *sharedSession,
                                      const QString &slotId,
                                      const QString &passLevel)
{
    if (!camera || m_activeRecordings.contains(cameraId))
        return false;

    const QString sessionDir = ensureSessionDirectory(slotId);
    if (sessionDir.isEmpty())
        return false;

    auto active = std::make_shared<ActiveRecording>();
    active->recorder = std::make_unique<QMediaRecorder>();
    active->imageCapture = std::make_unique<QImageCapture>();

    if (sharedSession)
    {
        active->session = sharedSession;
        active->usesSharedSession = true;
        if (active->session->camera() != camera)
            active->session->setCamera(camera);
    }
    else
    {
        active->ownedSession = std::make_unique<QMediaCaptureSession>();
        active->session = active->ownedSession.get();
        active->session->setCamera(camera);
    }

    if (!active->session)
        return false;

    active->session->setRecorder(active->recorder.get());
    active->session->setImageCapture(active->imageCapture.get());

    QMediaFormat format;
    format.setFileFormat(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    active->recorder->setMediaFormat(format);

    const QString filePath = buildVideoFilePath(cameraId, slotId, sessionDir);
    if (filePath.isEmpty())
        return false;
    active->recorder->setOutputLocation(QUrl::fromLocalFile(filePath));

    camera->start();

    active->metadata.cameraId = cameraId;
    active->metadata.slotId = slotId;
    active->metadata.filePath = filePath;
    active->metadata.analysisPath = analysisFilePathFor(filePath);
    QString normalizedPassLevel = passLevel.trimmed();
    if (normalizedPassLevel.compare(QStringLiteral("Second"), Qt::CaseInsensitive) == 0)
        normalizedPassLevel = QStringLiteral("Second");
    else if (normalizedPassLevel.compare(QStringLiteral("Root"), Qt::CaseInsensitive) == 0)
        normalizedPassLevel = QStringLiteral("Root");
    if (normalizedPassLevel.isEmpty())
        normalizedPassLevel = QStringLiteral("Root");
    active->metadata.passLevel = normalizedPassLevel;
    active->metadata.startedAt = QDateTime::currentDateTimeUtc();

    active->analysisFile = std::make_unique<QFile>(active->metadata.analysisPath);
    if (active->analysisFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        active->analysisTimer.start();
    }
    else
    {
        qWarning() << "Failed to open analysis file" << active->metadata.analysisPath << active->analysisFile->errorString();
        active->analysisFile.reset();
    }

    auto recorderPtr = active->recorder.get();
    connect(recorderPtr, &QMediaRecorder::recorderStateChanged, this, [this, cameraId](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState)
            finalizeRecording(cameraId);
    });

    active->recorder->record();

    m_activeRecordings.insert(cameraId, active);
    if (DebugConfig::isDebugLoggingEnabled())
    {
        qInfo().nospace() << "[RecordingManager] Recording started camera=" << cameraId
                          << " slot=" << (slotId.isEmpty() ? QStringLiteral("-") : slotId)
                          << " path=" << filePath
                          << " sharedSession=" << (active->usesSharedSession ? "true" : "false");
    }
    emit recordingStarted(cameraId);
    return true;
}

void RecordingManager::stopRecording(const QString &cameraId)
{
    auto active = m_activeRecordings.value(cameraId);
    if (!active || !active->recorder)
        return;

    if (DebugConfig::isDebugLoggingEnabled())
    {
        qInfo().nospace() << "[RecordingManager] Recording stop requested camera=" << cameraId
                          << " file=" << active->metadata.filePath;
    }
    active->recorder->stop();
}

bool RecordingManager::captureSnapshot(const QString &cameraId, QCamera *camera, const QString &slotId)
{
    if (!camera)
        return false;

    QString sessionDir = m_currentSessionDirectory;
    if (sessionDir.isEmpty())
    {
        sessionDir = createOutputDirectoryIfNeeded();
        if (sessionDir.isEmpty())
            return false;
    }

    if (!QDir(sessionDir).exists() && !QDir().mkpath(sessionDir))
        return false;

    const QString filePath = buildSnapshotFilePath(cameraId, slotId, sessionDir);
    if (filePath.isEmpty())
        return false;

    auto active = m_activeRecordings.value(cameraId);
    if (active)
    {
        if (!active->session)
            return false;

        if (!active->imageCapture)
        {
            active->imageCapture = std::make_unique<QImageCapture>();
            if (active->session)
                active->session->setImageCapture(active->imageCapture.get());
        }

        auto imageCapture = active->imageCapture.get();
        QObject::connect(imageCapture, &QImageCapture::imageSaved, this, [this, cameraId](int, const QString &path) {
            emit snapshotCaptured(cameraId, path);
        }, Qt::SingleShotConnection);
        imageCapture->captureToFile(filePath);
        return true;
    }

    auto pending = new PendingSnapshot();
    pending->session = std::make_unique<QMediaCaptureSession>();
    pending->capture = std::make_unique<QImageCapture>();
    pending->cameraId = cameraId;
    pending->filePath = filePath;

    pending->session->setCamera(camera);
    pending->session->setImageCapture(pending->capture.get());

    auto capturePtr = pending->capture.get();

    QObject::connect(capturePtr, &QImageCapture::imageSaved, this, [this, capturePtr](int, const QString &path) {
        if (!m_pendingSnapshots.contains(capturePtr))
            return;
        auto pending = m_pendingSnapshots.take(capturePtr);
        emit snapshotCaptured(pending->cameraId, path);
        delete pending;
    });

    QObject::connect(capturePtr, &QImageCapture::errorOccurred, this, [this, capturePtr](int, QImageCapture::Error, const QString &) {
        if (!m_pendingSnapshots.contains(capturePtr))
            return;
        auto pending = m_pendingSnapshots.take(capturePtr);
        delete pending;
    });

    m_pendingSnapshots.insert(capturePtr, pending);
    camera->start();
    capturePtr->captureToFile(filePath);
    return true;
}

bool RecordingManager::deleteRecording(const QString &filePath)
{
    auto it = std::find_if(m_recordings.begin(), m_recordings.end(), [&filePath](const RecordingMetadata &meta) {
        return meta.filePath == filePath;
    });

    if (it == m_recordings.end())
        return false;

    const QString sessionDirPath = QFileInfo(it->filePath).absolutePath();

    QFile::remove(it->filePath);
    if (!it->metadataPath.isEmpty())
        QFile::remove(it->metadataPath);
    if (!it->analysisPath.isEmpty())
        QFile::remove(it->analysisPath);

    m_recordings.erase(it);
    emit recordingRemoved(filePath);

    tryRemoveEmptyDirectory(sessionDirPath);
    return true;
}

QVector<RecordingManager::RecordingMetadata> RecordingManager::recordings() const
{
    return m_recordings;
}

void RecordingManager::appendAnalysisResult(const QString &cameraId, const QJsonObject &result)
{
    auto active = m_activeRecordings.value(cameraId);
    if (!active || !active->analysisFile || !active->analysisFile->isOpen())
        return;

    QJsonObject payload = result;
    if (!payload.contains(QStringLiteral("timestampMs")))
    {
        const qint64 elapsed = active->analysisTimer.isValid() ? active->analysisTimer.elapsed() : 0;
        payload.insert(QStringLiteral("timestampMs"), static_cast<double>(elapsed));
    }

    const QByteArray line = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    active->analysisFile->write(line);
    active->analysisFile->write("\n");
}

QString RecordingManager::createOutputDirectoryIfNeeded() const
{
    if (m_outputDirectory.isEmpty())
        return QString();

    QDir dir(m_outputDirectory);
    if (!dir.exists())
        dir.mkpath(".");

    return dir.absolutePath();
}

QString RecordingManager::ensureSessionDirectory(const QString &slotId)
{
    if (!m_currentSessionDirectory.isEmpty())
    {
        if (QDir(m_currentSessionDirectory).exists())
            return m_currentSessionDirectory;
        m_currentSessionDirectory.clear();
    }

    const QString rootPath = createOutputDirectoryIfNeeded();
    if (rootPath.isEmpty())
        return QString();

    const QString baseName = makeSessionDirectoryName(slotId);
    if (baseName.isEmpty())
        return QString();

    QDir rootDir(rootPath);
    QString candidateName = baseName;
    QString absolutePath = rootDir.filePath(candidateName);

    int attempt = 1;
    while (QDir(absolutePath).exists())
    {
        ++attempt;
        const QString suffix = QString::number(attempt);
        QString trimmedBase = baseName;
        const int availableChars = kMaxSessionFolderNameLength - suffix.size() - 1;
        if (availableChars > 0 && trimmedBase.size() > availableChars)
            trimmedBase.truncate(availableChars);
        else if (availableChars <= 0)
            trimmedBase = QString();

        if (trimmedBase.isEmpty())
            candidateName = suffix;
        else
            candidateName = QStringLiteral("%1_%2").arg(trimmedBase, suffix);

        absolutePath = rootDir.filePath(candidateName);
    }

    if (!rootDir.mkpath(candidateName))
        return QString();

    m_currentSessionDirectory = QDir(absolutePath).absolutePath();
    return m_currentSessionDirectory;
}

QString RecordingManager::buildVideoFilePath(const QString &cameraId, const QString &slotId, const QString &sessionPath) const
{
    if (sessionPath.isEmpty())
        return QString();

    const QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_hhmmss");
    return QDir(sessionPath).filePath(QString("%1_%2_%3.mp4").arg(cameraId, slotId, timestamp));
}

QString RecordingManager::buildSnapshotFilePath(const QString &cameraId, const QString &slotId, const QString &sessionPath) const
{
    if (sessionPath.isEmpty())
        return QString();

    const QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_hhmmsszzz");
    return QDir(sessionPath).filePath(QString("%1_%2_%3.jpg").arg(cameraId, slotId, timestamp));
}

QString RecordingManager::metadataFilePathFor(const QString &videoFilePath) const
{
    QFileInfo info(videoFilePath);
    return info.path() + '/' + info.completeBaseName() + ".json";
}

QString RecordingManager::analysisFilePathFor(const QString &videoFilePath) const
{
    QFileInfo info(videoFilePath);
    return info.path() + '/' + info.completeBaseName() + ".analysis.json";
}

QString RecordingManager::makeSessionDirectoryName(const QString &slotId) const
{
    const QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_hhmmss");
    QString sanitizedSlot = sanitizeForFolder(slotId);

    QString name = sanitizedSlot.isEmpty()
        ? timestamp
        : QStringLiteral("%1_%2").arg(timestamp, sanitizedSlot);

    if (name.size() > kMaxSessionFolderNameLength)
        name.truncate(kMaxSessionFolderNameLength);

    return name;
}

QString RecordingManager::sanitizeForFolder(const QString &value) const
{
    QString normalized = value;
    normalized.remove(QChar::Null); // defensive; unlikely

    QString result;
    result.reserve(normalized.size());
    for (const QChar &ch : normalized)
    {
        if (ch.isLetterOrNumber() || ch == QChar::fromLatin1('-') || ch == QChar::fromLatin1('_'))
            result.append(ch);
    }

    if (result.size() > kMaxSessionFolderNameLength)
        result.truncate(kMaxSessionFolderNameLength);

    return result;
}

void RecordingManager::tryRemoveEmptyDirectory(const QString &dirPath) const
{
    if (dirPath.isEmpty())
        return;

    const QString cleanedRoot = QDir::cleanPath(m_outputDirectory);
    const QString cleanedTarget = QDir::cleanPath(dirPath);
    if (cleanedTarget.isEmpty() || cleanedTarget == cleanedRoot)
        return;

    QDir dir(cleanedTarget);
    if (!dir.exists())
        return;

    const QStringList entries = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty())
        QDir().rmdir(dir.absolutePath());
}

void RecordingManager::writeMetadata(const RecordingMetadata &metadata) const
{
    QFile file(metadata.metadataPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    QJsonObject json;
    json.insert("cameraId", metadata.cameraId);
    json.insert("slotId", metadata.slotId);
    json.insert("filePath", metadata.filePath);
    if (!metadata.analysisPath.isEmpty())
        json.insert("analysisPath", metadata.analysisPath);
    if (!metadata.passLevel.trimmed().isEmpty())
        json.insert("passLevel", metadata.passLevel.trimmed());
    json.insert("startedAt", metadata.startedAt.toString(Qt::ISODateWithMs));
    json.insert("finishedAt", metadata.finishedAt.toString(Qt::ISODateWithMs));
    json.insert("durationMs", static_cast<double>(metadata.durationMs));

    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
}

void RecordingManager::finalizeRecording(const QString &cameraId)
{
    auto active = m_activeRecordings.take(cameraId);
    if (!active)
        return;

    if (active->recorder)
        active->metadata.durationMs = active->recorder->duration();

    if (active->session && active->usesSharedSession)
    {
        active->session->setRecorder(nullptr);
        active->session->setImageCapture(nullptr);
    }
    if (active->analysisFile && active->analysisFile->isOpen())
        active->analysisFile->close();
    active->metadata.finishedAt = QDateTime::currentDateTimeUtc();
    active->metadata.metadataPath = metadataFilePathFor(active->metadata.filePath);

    writeMetadata(active->metadata);

    m_recordings.append(active->metadata);

    emit recordingStopped(cameraId);
    emit recordingAdded(active->metadata);

    resetSessionDirectoryIfIdle();
}

void RecordingManager::resetSessionDirectoryIfIdle()
{
    if (m_currentSessionDirectory.isEmpty())
        return;

    if (!m_activeRecordings.isEmpty())
        return;

    tryRemoveEmptyDirectory(m_currentSessionDirectory);

    m_currentSessionDirectory.clear();
}

void RecordingManager::loadExistingRecordings()
{
    m_recordings.clear();
    m_currentSessionDirectory.clear();

    if (m_outputDirectory.isEmpty())
        return;

    QDir rootDir(m_outputDirectory);
    if (!rootDir.exists())
        return;

    QVector<QFileInfo> metadataFiles;
    QVector<QDir> pendingDirs;
    pendingDirs.append(rootDir);

    while (!pendingDirs.isEmpty())
    {
        const QDir current = pendingDirs.takeLast();

        const QFileInfoList subDirs = current.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable | QDir::NoSymLinks);
        for (const QFileInfo &subDirInfo : subDirs)
            pendingDirs.append(QDir(subDirInfo.absoluteFilePath()));

        const QStringList filters{QStringLiteral("*.json")};
        const QFileInfoList jsonFiles = current.entryInfoList(filters, QDir::Files | QDir::Readable | QDir::NoSymLinks | QDir::NoDotAndDotDot);
        for (const QFileInfo &info : jsonFiles)
        {
            if (info.fileName().endsWith(QStringLiteral(".analysis.json"), Qt::CaseInsensitive))
                continue;
            metadataFiles.append(info);
        }
    }

    QVector<RecordingMetadata> loaded;
    loaded.reserve(metadataFiles.size());

    for (const QFileInfo &info : metadataFiles)
    {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            continue;

        const QJsonObject obj = doc.object();

        RecordingMetadata metadata;
        metadata.metadataPath = info.absoluteFilePath();
        metadata.cameraId = obj.value(QStringLiteral("cameraId")).toString();
        metadata.slotId = obj.value(QStringLiteral("slotId")).toString();
        metadata.filePath = obj.value(QStringLiteral("filePath")).toString();

        if (metadata.filePath.isEmpty())
            metadata.filePath = info.dir().absoluteFilePath(info.completeBaseName() + QStringLiteral(".mp4"));
        else if (!QFileInfo(metadata.filePath).isAbsolute())
            metadata.filePath = info.dir().absoluteFilePath(metadata.filePath);

        metadata.analysisPath = obj.value(QStringLiteral("analysisPath")).toString();
        if (metadata.analysisPath.isEmpty())
            metadata.analysisPath = analysisFilePathFor(metadata.filePath);
        else if (!QFileInfo(metadata.analysisPath).isAbsolute())
            metadata.analysisPath = info.dir().absoluteFilePath(metadata.analysisPath);

        QString passLevel = obj.value(QStringLiteral("passLevel")).toString().trimmed();
        if (passLevel.compare(QStringLiteral("Second"), Qt::CaseInsensitive) == 0)
            passLevel = QStringLiteral("Second");
        else if (passLevel.compare(QStringLiteral("Root"), Qt::CaseInsensitive) == 0)
            passLevel = QStringLiteral("Root");
        metadata.passLevel = passLevel;

        const QString startedAtStr = obj.value(QStringLiteral("startedAt")).toString();
        metadata.startedAt = QDateTime::fromString(startedAtStr, Qt::ISODateWithMs);
        if (!metadata.startedAt.isValid())
            metadata.startedAt = QDateTime::fromString(startedAtStr, Qt::ISODate);

        const QString finishedAtStr = obj.value(QStringLiteral("finishedAt")).toString();
        metadata.finishedAt = QDateTime::fromString(finishedAtStr, Qt::ISODateWithMs);
        if (!metadata.finishedAt.isValid())
            metadata.finishedAt = QDateTime::fromString(finishedAtStr, Qt::ISODate);

        metadata.durationMs = static_cast<qint64>(obj.value(QStringLiteral("durationMs")).toDouble());

        if (!QFile::exists(metadata.filePath))
            continue;

        loaded.append(metadata);
    }

    std::sort(loaded.begin(), loaded.end(), [](const RecordingMetadata &a, const RecordingMetadata &b) {
        return a.startedAt < b.startedAt;
    });

    m_recordings = loaded;
}
