#ifndef VIDEORECORDER_H
#define VIDEORECORDER_H

#include <QObject>
#include <QImage>
#include <QString>
#include <QDateTime>
#include <QMutex>
#include <opencv2/opencv.hpp>
#include <atomic>

namespace MCM {

/**
 * @brief Chunk-based video recorder
 * 
 * Records video in chunks to manage memory efficiently.
 * Each chunk is a complete, playable video file.
 * 
 * Output path format: {outputDir}/slot_{slotId}/{chunkNum}_{startDateTime}.mp4
 */
class VideoRecorder : public QObject {
    Q_OBJECT

public:
    explicit VideoRecorder(int slotId, QObject* parent = nullptr);
    ~VideoRecorder();

    /**
     * @brief Start recording
     * @param outputDirectory Base directory for recordings
     * @param fps Frames per second
     * @param codec FourCC codec string (e.g., "mp4v", "avc1")
     * @param chunkDurationSeconds Duration of each chunk
     * @return true if recording started successfully
     */
    bool startRecording(const QString& outputDirectory, int fps, 
                       const QString& codec, int chunkDurationSeconds);

    /**
     * @brief Stop recording and finalize current chunk
     */
    void stopRecording();

    /**
     * @brief Write a frame to the recording
     * @param frame The frame to write
     * 
     * Automatically handles chunk rotation when needed
     */
    void writeFrame(const QImage& frame);

    /**
     * @brief Check if currently recording
     */
    bool isRecording() const { return m_recording; }

    /**
     * @brief Get the slot ID this recorder belongs to
     */
    int slotId() const { return m_slotId; }

    /**
     * @brief Get current chunk number
     */
    int currentChunkNumber() const { return m_chunkNumber; }

    /**
     * @brief Get total frames written in current session
     */
    int totalFramesWritten() const { return m_totalFramesWritten; }

signals:
    /**
     * @brief Emitted when a new chunk is started
     */
    void chunkStarted(int chunkNumber, const QString& filename);

    /**
     * @brief Emitted when a chunk is completed
     */
    void chunkCompleted(int chunkNumber, const QString& filename);

    /**
     * @brief Emitted when an error occurs
     */
    void errorOccurred(const QString& message);

private:
    /**
     * @brief Rotate to a new chunk file
     */
    void rotateChunk();

    /**
     * @brief Generate filename for current chunk
     */
    QString generateFilename() const;

    /**
     * @brief Convert QImage to cv::Mat
     */
    cv::Mat qImageToMat(const QImage& image);

    /**
     * @brief Ensure output directory exists
     */
    bool ensureDirectoryExists(const QString& path);

    int m_slotId;
    std::atomic<bool> m_recording{false};
    
    QString m_outputDirectory;
    int m_fps;
    QString m_codec;
    int m_chunkDurationSeconds;
    int m_maxFramesPerChunk;
    
    cv::VideoWriter m_writer;
    int m_chunkNumber;
    int m_framesInCurrentChunk;
    int m_totalFramesWritten;
    QDateTime m_chunkStartTime;
    QString m_currentFilename;
    
    int m_frameWidth;
    int m_frameHeight;
    bool m_sizeInitialized;
    
    QMutex m_mutex;
};

} // namespace MCM

#endif // VIDEORECORDER_H

