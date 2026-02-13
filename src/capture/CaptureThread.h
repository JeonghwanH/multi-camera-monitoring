#ifndef CAPTURETHREAD_H
#define CAPTURETHREAD_H

#include <QThread>
#include <QImage>
#include <QString>
#include <atomic>
#include "core/Config.h"

namespace MCM {

class FrameBuffer;
class VideoRecorder;

/**
 * @brief Base class for capture threads
 * 
 * Provides common functionality for capturing frames from various sources.
 * Derived classes implement the actual capture logic for specific source types.
 */
class CaptureThread : public QThread {
    Q_OBJECT

public:
    explicit CaptureThread(int slotId, QObject* parent = nullptr);
    ~CaptureThread() override;

    /**
     * @brief Set the source to capture from
     * @param type Source type (Wired, Rtsp, etc.)
     * @param source Device index or URL
     */
    virtual void setSource(SourceType type, const QString& source);

    /**
     * @brief Get the current source type
     */
    SourceType sourceType() const { return m_sourceType; }

    /**
     * @brief Get the current source string
     */
    QString source() const { return m_source; }

    /**
     * @brief Get the slot ID
     */
    int slotId() const { return m_slotId; }

    /**
     * @brief Set the frame buffer to use
     */
    void setFrameBuffer(FrameBuffer* buffer);

    /**
     * @brief Set the video recorder to use
     */
    void setVideoRecorder(VideoRecorder* recorder);

    /**
     * @brief Stop the capture thread gracefully
     */
    void stopCapture();

    /**
     * @brief Check if capture is running
     */
    bool isCapturing() const { return m_running; }

    /**
     * @brief Check if connected to source
     */
    bool isConnected() const { return m_connected; }

signals:
    /**
     * @brief Emitted when a new frame is ready
     */
    void frameReady(const QImage& frame);

    /**
     * @brief Emitted when connection to source is established
     */
    void connectionEstablished();

    /**
     * @brief Emitted when connection to source is lost
     */
    void connectionLost();

    /**
     * @brief Emitted when connection is restored after being lost
     */
    void connectionRestored();

    /**
     * @brief Emitted when an error occurs
     */
    void errorOccurred(const QString& message);

protected:
    void run() override = 0;

    /**
     * @brief Process a captured frame (buffer, record, emit signal)
     */
    void processFrame(const QImage& frame);

    int m_slotId;
    SourceType m_sourceType;
    QString m_source;
    
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    
    FrameBuffer* m_buffer{nullptr};
    VideoRecorder* m_recorder{nullptr};
};

} // namespace MCM

#endif // CAPTURETHREAD_H

