#include "VideoRecorder.h"
#include <QDir>
#include <QDebug>

namespace MCM {

VideoRecorder::VideoRecorder(int slotId, QObject* parent)
    : QObject(parent)
    , m_slotId(slotId)
    , m_fps(30)
    , m_chunkDurationSeconds(300)
    , m_maxFramesPerChunk(9000)  // 30fps * 300s
    , m_chunkNumber(0)
    , m_framesInCurrentChunk(0)
    , m_totalFramesWritten(0)
    , m_frameWidth(0)
    , m_frameHeight(0)
    , m_sizeInitialized(false)
{
}

VideoRecorder::~VideoRecorder() {
    stopRecording();
}

bool VideoRecorder::startRecording(const QString& outputDirectory, int fps,
                                   const QString& codec, int chunkDurationSeconds) {
    QMutexLocker locker(&m_mutex);
    
    if (m_recording) {
        return true;  // Already recording
    }
    
    m_outputDirectory = outputDirectory;
    m_fps = fps;
    m_codec = codec;
    m_chunkDurationSeconds = chunkDurationSeconds;
    m_maxFramesPerChunk = fps * chunkDurationSeconds;
    
    m_chunkNumber = 0;
    m_framesInCurrentChunk = 0;
    m_totalFramesWritten = 0;
    m_sizeInitialized = false;
    
    // Create output directory
    QString slotDir = QString("%1/slot_%2").arg(m_outputDirectory).arg(m_slotId);
    if (!ensureDirectoryExists(slotDir)) {
        emit errorOccurred(QString("Failed to create output directory: %1").arg(slotDir));
        return false;
    }
    
    m_recording = true;
    qDebug() << "VideoRecorder started for slot" << m_slotId;
    return true;
}

void VideoRecorder::stopRecording() {
    QMutexLocker locker(&m_mutex);
    
    if (!m_recording) {
        return;
    }
    
    m_recording = false;
    
    if (m_writer.isOpened()) {
        m_writer.release();
        if (!m_currentFilename.isEmpty()) {
            emit chunkCompleted(m_chunkNumber, m_currentFilename);
        }
    }
    
    qDebug() << "VideoRecorder stopped for slot" << m_slotId 
             << "- Total frames:" << m_totalFramesWritten;
}

void VideoRecorder::writeFrame(const QImage& frame) {
    QMutexLocker locker(&m_mutex);
    
    if (!m_recording || frame.isNull()) {
        return;
    }
    
    // Validate frame dimensions
    if (frame.width() <= 0 || frame.height() <= 0) {
        return;
    }
    
    // Initialize frame size on first valid frame
    if (!m_sizeInitialized) {
        m_frameWidth = frame.width();
        m_frameHeight = frame.height();
        m_sizeInitialized = true;
        qDebug() << "VideoRecorder: Initialized with frame size" 
                 << m_frameWidth << "x" << m_frameHeight;
    }
    
    // Check if frame size changed - reinitialize writer with new size
    if (frame.width() != m_frameWidth || frame.height() != m_frameHeight) {
        qDebug() << "VideoRecorder: Frame size changed from"
                 << m_frameWidth << "x" << m_frameHeight
                 << "to" << frame.width() << "x" << frame.height();
        
        // Update to new frame size
        m_frameWidth = frame.width();
        m_frameHeight = frame.height();
        
        // Force new chunk with new dimensions
        if (m_writer.isOpened()) {
            m_writer.release();
            if (!m_currentFilename.isEmpty()) {
                emit chunkCompleted(m_chunkNumber, m_currentFilename);
            }
        }
    }
    
    // Start first chunk or rotate if needed
    if (!m_writer.isOpened() || m_framesInCurrentChunk >= m_maxFramesPerChunk) {
        rotateChunk();
    }
    
    if (!m_writer.isOpened()) {
        return;  // Failed to open writer
    }
    
    // Convert and write frame
    cv::Mat mat = qImageToMat(frame);
    if (!mat.empty()) {
        m_writer.write(mat);
        m_framesInCurrentChunk++;
        m_totalFramesWritten++;
    }
}

void VideoRecorder::rotateChunk() {
    // Close current chunk if open
    if (m_writer.isOpened()) {
        m_writer.release();
        if (!m_currentFilename.isEmpty()) {
            emit chunkCompleted(m_chunkNumber, m_currentFilename);
        }
    }
    
    // Start new chunk
    m_chunkNumber++;
    m_framesInCurrentChunk = 0;
    m_chunkStartTime = QDateTime::currentDateTime();
    
    m_currentFilename = generateFilename();
    
    // Determine codec - use platform-appropriate codec
    int fourcc;
#ifdef __APPLE__
    // On macOS, use H.264 via VideoToolbox or mp4v
    if (m_codec == "h264" || m_codec == "avc1") {
        fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');
    } else {
        // mp4v works reliably on macOS
        fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    }
#else
    if (m_codec == "h264" || m_codec == "avc1") {
        fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');
    } else if (m_codec == "xvid") {
        fourcc = cv::VideoWriter::fourcc('X', 'V', 'I', 'D');
    } else {
        fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    }
#endif
    
    qDebug() << "VideoRecorder: Opening" << m_currentFilename 
             << "Size:" << m_frameWidth << "x" << m_frameHeight
             << "FPS:" << m_fps;
    
    m_writer.open(
        m_currentFilename.toStdString(),
        fourcc,
        m_fps,
        cv::Size(m_frameWidth, m_frameHeight),
        true  // isColor = true
    );
    
    if (!m_writer.isOpened()) {
        qWarning() << "VideoRecorder: Failed to open writer with codec" << m_codec;
        // Try fallback codec
        fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
        m_writer.open(
            m_currentFilename.toStdString(),
            fourcc,
            m_fps,
            cv::Size(m_frameWidth, m_frameHeight),
            true
        );
    }
    
    if (!m_writer.isOpened()) {
        emit errorOccurred(QString("Failed to open video writer: %1").arg(m_currentFilename));
        return;
    }
    
    emit chunkStarted(m_chunkNumber, m_currentFilename);
    qDebug() << "Started chunk" << m_chunkNumber << ":" << m_currentFilename;
}

QString VideoRecorder::generateFilename() const {
    QString slotDir = QString("%1/slot_%2").arg(m_outputDirectory).arg(m_slotId);
    
    QString filename = QString("%1/%2_%3.mp4")
        .arg(slotDir)
        .arg(m_chunkNumber, 3, 10, QChar('0'))  // Zero-padded: 001, 002, ...
        .arg(m_chunkStartTime.toString("yyyyMMdd_HHmmss"));
    
    return filename;
}

cv::Mat VideoRecorder::qImageToMat(const QImage& image) {
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return cv::Mat();
    }
    
    QImage converted = image;
    
    // Convert to RGB888 if needed
    if (image.format() != QImage::Format_RGB888) {
        converted = image.convertToFormat(QImage::Format_RGB888);
    }
    
    if (converted.isNull()) {
        return cv::Mat();
    }
    
    // Create Mat from QImage data
    cv::Mat mat(converted.height(), converted.width(), CV_8UC3,
                const_cast<uchar*>(converted.bits()),
                static_cast<size_t>(converted.bytesPerLine()));
    
    if (mat.empty()) {
        return cv::Mat();
    }
    
    // OpenCV uses BGR, Qt uses RGB
    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);
    
    return bgr.clone();  // Return a deep copy
}

bool VideoRecorder::ensureDirectoryExists(const QString& path) {
    QDir dir(path);
    if (!dir.exists()) {
        return dir.mkpath(".");
    }
    return true;
}

} // namespace MCM

