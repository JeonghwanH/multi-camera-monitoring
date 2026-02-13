#include "QtVideoRecorder.h"
#include <QDir>
#include <QDebug>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFileInfo>

namespace MCM {

QtVideoRecorder::QtVideoRecorder(int slotId, QObject* parent)
    : QObject(parent)
    , m_slotId(slotId)
{
    qDebug() << "QtVideoRecorder: Creating double-buffered recorder for slot" << slotId;
    
    // Create two media recorders for seamless chunk rotation
    m_recorderA = new QMediaRecorder(this);
    m_recorderB = new QMediaRecorder(this);
    m_activeRecorder = nullptr;  // Will be set during first startRecording
    
    // Connect signals for both recorders
    connect(m_recorderA, &QMediaRecorder::recorderStateChanged,
            this, &QtVideoRecorder::onRecorderStateChanged);
    connect(m_recorderA, &QMediaRecorder::errorOccurred,
            this, &QtVideoRecorder::onRecorderErrorOccurred);
    connect(m_recorderA, &QMediaRecorder::durationChanged,
            this, &QtVideoRecorder::onDurationChanged);
    
    connect(m_recorderB, &QMediaRecorder::recorderStateChanged,
            this, &QtVideoRecorder::onRecorderStateChanged);
    connect(m_recorderB, &QMediaRecorder::errorOccurred,
            this, &QtVideoRecorder::onRecorderErrorOccurred);
    connect(m_recorderB, &QMediaRecorder::durationChanged,
            this, &QtVideoRecorder::onDurationChanged);
    
    // Configure media format for both recorders
    configureRecorder(m_recorderA);
    configureRecorder(m_recorderB);
    
    // Chunk rotation timer
    m_chunkTimer = new QTimer(this);
    m_chunkTimer->setSingleShot(true);
    connect(m_chunkTimer, &QTimer::timeout,
            this, &QtVideoRecorder::onChunkTimerTimeout);
}

QtVideoRecorder::~QtVideoRecorder() {
    stopRecording();
}

void QtVideoRecorder::configureMediaFormat() {
    // Configure both recorders with same settings
    configureRecorder(m_recorderA);
    configureRecorder(m_recorderB);
}

void QtVideoRecorder::configureRecorder(QMediaRecorder* recorder) {
    if (!recorder) return;
    
    QMediaFormat format;
    
    // Use MP4 container
    format.setFileFormat(QMediaFormat::MPEG4);
    
    // Use H.264 codec - hardware accelerated on most platforms
    // VideoToolbox on macOS, NVENC on NVIDIA, QSV on Intel
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    
    // Explicitly disable audio - set to Unspecified to avoid microphone access
    format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
    
    recorder->setMediaFormat(format);
    
    // Set quality - NormalQuality is more compatible across codecs
    recorder->setQuality(QMediaRecorder::NormalQuality);
    
    // Use average bit rate encoding - more widely supported than constant quality
    recorder->setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
    recorder->setVideoBitRate(4000000);  // 4 Mbps - good quality for 720p/1080p
    recorder->setVideoFrameRate(30);
}

QMediaRecorder* QtVideoRecorder::getStandbyRecorder() const {
    // Return the recorder that is NOT currently active
    if (m_activeRecorder == m_recorderA) {
        return m_recorderB;
    }
    return m_recorderA;  // Default to A if no active recorder
}

void QtVideoRecorder::setSession(QMediaCaptureSession* session) {
    if (m_recording) {
        qWarning() << "QtVideoRecorder: Cannot change session while recording";
        return;
    }
    
    m_session = session;
    
    if (m_session) {
        // Don't attach any recorder yet - we'll attach during rotateChunk
        // This allows seamless double-buffer swapping
        qDebug() << "QtVideoRecorder: Session set for slot" << m_slotId 
                 << "(recorder will be attached on first chunk)";
    }
}

bool QtVideoRecorder::startRecording(const QString& outputDirectory, int chunkDurationSeconds) {
    qDebug() << "=== QtVideoRecorder::startRecording ===" << "slot" << m_slotId;
    qDebug() << "  Output dir (input):" << outputDirectory;
    qDebug() << "  Chunk duration:" << chunkDurationSeconds << "seconds";
    
    if (m_recording) {
        qDebug() << "  Already recording";
        return true;
    }
    
    if (!m_session) {
        qWarning() << "  ERROR: No capture session set";
        emit errorOccurred("No capture session set");
        return false;
    }
    
    // Convert to absolute path if relative
    QString absOutputDir = outputDirectory;
    QFileInfo dirInfo(outputDirectory);
    if (dirInfo.isRelative()) {
        // Use project root (parent of build directory) as base for relative paths
        // On macOS, app is in build/multi-camera-monitor.app/Contents/MacOS/
        // So we go up 4 levels to get to project root
        QDir appDir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
        // macOS app bundle: go up from MacOS -> Contents -> .app -> build -> project root
        appDir.cdUp();  // Contents
        appDir.cdUp();  // .app
        appDir.cdUp();  // build
        appDir.cdUp();  // project root
#else
        // On other platforms, just go up one level from build
        appDir.cdUp();
#endif
        absOutputDir = appDir.absolutePath() + "/" + outputDirectory;
    }
    
    m_outputDirectory = absOutputDir;
    m_chunkDurationSeconds = chunkDurationSeconds;
    m_chunkNumber = 0;
    
    qDebug() << "  Output dir (absolute):" << m_outputDirectory;
    
    // Create output directory
    QString slotDir = QString("%1/slot_%2").arg(m_outputDirectory).arg(m_slotId);
    if (!ensureDirectoryExists(slotDir)) {
        emit errorOccurred(QString("Failed to create output directory: %1").arg(slotDir));
        return false;
    }
    
    qDebug() << "  Slot directory created:" << slotDir;
    
    m_recording = true;
    
    // Start first chunk
    rotateChunk();
    
    emit recordingStateChanged(true);
    qDebug() << "QtVideoRecorder: Recording started for slot" << m_slotId;
    return true;
}

void QtVideoRecorder::stopRecording() {
    qDebug() << "=== QtVideoRecorder::stopRecording ===" << "slot" << m_slotId;
    
    if (!m_recording) {
        return;
    }
    
    m_recording = false;
    m_chunkTimer->stop();
    
    // Stop both recorders (one might be finishing up from last rotation)
    if (m_recorderA && m_recorderA->recorderState() == QMediaRecorder::RecordingState) {
        m_recorderA->stop();
    }
    if (m_recorderB && m_recorderB->recorderState() == QMediaRecorder::RecordingState) {
        m_recorderB->stop();
    }
    
    if (!m_currentFilename.isEmpty()) {
        emit chunkCompleted(m_chunkNumber, m_currentFilename);
    }
    
    m_activeRecorder = nullptr;
    
    emit recordingStateChanged(false);
    qDebug() << "QtVideoRecorder: Recording stopped for slot" << m_slotId;
}

void QtVideoRecorder::rotateChunk() {
    qDebug() << "QtVideoRecorder::rotateChunk (double-buffer) slot" << m_slotId;
    
    if (!m_recording) {
        return;  // Don't start new chunk if we've stopped
    }
    
    if (!m_session) {
        qWarning() << "QtVideoRecorder: No session for rotation";
        return;
    }
    
    // Save reference to old recorder (may be null on first chunk)
    QMediaRecorder* oldRecorder = m_activeRecorder;
    QString oldFilename = m_currentFilename;
    int oldChunkNumber = m_chunkNumber;
    
    // Setup new chunk metadata
    m_chunkNumber++;
    m_chunkStartTime = QDateTime::currentDateTime();
    m_currentFilename = generateFilename();
    
    // Get the standby recorder (the one NOT currently recording)
    QMediaRecorder* newRecorder = getStandbyRecorder();
    
    qDebug() << "  Double-buffer swap: old=" << (void*)oldRecorder 
             << "new=" << (void*)newRecorder;
    qDebug() << "  Starting chunk" << m_chunkNumber << ":" << m_currentFilename;
    
    // Configure the new recorder
    configureRecorder(newRecorder);
    newRecorder->setOutputLocation(QUrl::fromLocalFile(m_currentFilename));
    
    // KEY STEP: Attach new recorder to session FIRST
    // This immediately starts sending frames to the new recorder
    m_session->setRecorder(newRecorder);
    m_activeRecorder = newRecorder;
    
    // Start the new recorder (it's already receiving frames from session)
    newRecorder->record();
    
    // NOW stop the old recorder (it's already detached from session)
    // No frames are lost because new recorder is already capturing
    if (oldRecorder && oldRecorder->recorderState() == QMediaRecorder::RecordingState) {
        oldRecorder->stop();
        
        // Emit completion for previous chunk
        if (!oldFilename.isEmpty()) {
            emit chunkCompleted(oldChunkNumber, oldFilename);
        }
    }
    
    // Start timer for next chunk rotation
    m_chunkTimer->start(m_chunkDurationSeconds * 1000);
    
    emit chunkStarted(m_chunkNumber, m_currentFilename);
}

void QtVideoRecorder::startNextChunk() {
    // This method is now only called internally if needed
    // Main rotation logic is in rotateChunk() with double-buffering
    rotateChunk();
}

void QtVideoRecorder::onChunkTimerTimeout() {
    if (m_recording) {
        qDebug() << "QtVideoRecorder: Chunk timer expired, rotating...";
        rotateChunk();
    }
}

void QtVideoRecorder::onRecorderStateChanged(QMediaRecorder::RecorderState state) {
    QString stateStr;
    switch (state) {
        case QMediaRecorder::StoppedState: stateStr = "Stopped"; break;
        case QMediaRecorder::RecordingState: stateStr = "Recording"; break;
        case QMediaRecorder::PausedState: stateStr = "Paused"; break;
    }
    qDebug() << "QtVideoRecorder: State changed to" << stateStr << "for slot" << m_slotId;
}

void QtVideoRecorder::onRecorderErrorOccurred(QMediaRecorder::Error error, const QString& errorString) {
    QString errorType;
    switch (error) {
        case QMediaRecorder::NoError: errorType = "NoError"; break;
        case QMediaRecorder::ResourceError: errorType = "ResourceError"; break;
        case QMediaRecorder::FormatError: errorType = "FormatError"; break;
        case QMediaRecorder::OutOfSpaceError: errorType = "OutOfSpaceError"; break;
        case QMediaRecorder::LocationNotWritable: errorType = "LocationNotWritable"; break;
    }
    
    qWarning() << "QtVideoRecorder: Error" << errorType << "-" << errorString 
               << "for slot" << m_slotId;
    
    emit errorOccurred(errorString);
}

void QtVideoRecorder::onDurationChanged(qint64 duration) {
    // Log duration periodically (every 10 seconds)
    static qint64 lastLog = 0;
    if (duration - lastLog >= 10000) {
        qDebug() << "QtVideoRecorder slot" << m_slotId 
                 << "recording duration:" << duration / 1000 << "seconds";
        lastLog = duration;
    }
}

QString QtVideoRecorder::generateFilename() const {
    QString slotDir = QString("%1/slot_%2").arg(m_outputDirectory).arg(m_slotId);
    
    QString filename = QString("%1/%2_%3.mp4")
        .arg(slotDir)
        .arg(m_chunkNumber, 3, 10, QChar('0'))  // Zero-padded: 001, 002, ...
        .arg(m_chunkStartTime.toString("yyyyMMdd_HHmmss"));
    
    return filename;
}

bool QtVideoRecorder::ensureDirectoryExists(const QString& path) {
    QDir dir(path);
    if (!dir.exists()) {
        return dir.mkpath(".");
    }
    return true;
}

} // namespace MCM

