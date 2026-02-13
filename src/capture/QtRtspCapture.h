#ifndef QTRTSPCAPTURE_H
#define QTRTSPCAPTURE_H

#include <QObject>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QUrl>
#include <QTimer>
#include "core/Config.h"

namespace MCM {

class OptimizedVideoWidget;

/**
 * @brief Qt Multimedia-based RTSP stream capture
 * 
 * Replaces FFmpeg-based RtspCapture with Qt's native media player.
 * 
 * Benefits:
 * - Hardware-accelerated decoding (GPU)
 * - Direct GPU pipeline (no CPU frame copying)
 * - Lower latency
 * - Simpler code (no manual FFmpeg management)
 * 
 * Usage:
 *   QtRtspCapture* capture = new QtRtspCapture(slotId);
 *   capture->setVideoOutput(optimizedWidget->videoItem());
 *   capture->setRtspUrl("rtsp://192.168.1.100:554/stream");
 *   capture->start();
 */
class QtRtspCapture : public QObject {
    Q_OBJECT

public:
    explicit QtRtspCapture(int slotId, QObject* parent = nullptr);
    ~QtRtspCapture() override;

    /**
     * @brief Set the RTSP URL to capture from
     * @param url RTSP URL (e.g., rtsp://192.168.1.100:554/stream)
     */
    void setRtspUrl(const QString& url);

    /**
     * @brief Get the current RTSP URL
     */
    QString rtspUrl() const { return m_rtspUrl; }

    /**
     * @brief Set video output (QGraphicsVideoItem or QVideoWidget)
     * @param videoOutput The video output object
     */
    void setVideoOutput(QObject* videoOutput);

    /**
     * @brief Set video sink for frame access (optional)
     * @param sink Video sink to receive frames
     */
    void setVideoSink(QVideoSink* sink);

    /**
     * @brief Start playback/capture
     */
    void start();

    /**
     * @brief Stop playback/capture
     */
    void stop();

    /**
     * @brief Check if player is active (playing)
     */
    bool isActive() const;

    /**
     * @brief Check if connected to stream
     */
    bool isConnected() const { return m_connected; }

    /**
     * @brief Get the slot ID
     */
    int slotId() const { return m_slotId; }

    /**
     * @brief Get the media player (for advanced use)
     */
    QMediaPlayer* mediaPlayer() const { return m_player; }

signals:
    /**
     * @brief Emitted when connection is established
     */
    void connectionEstablished();

    /**
     * @brief Emitted when connection is lost
     */
    void connectionLost();

    /**
     * @brief Emitted when an error occurs
     */
    void errorOccurred(const QString& message);

    /**
     * @brief Emitted when a new frame is available (optional, for recording)
     */
    void frameReady(const QVideoFrame& frame);

private slots:
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onErrorOccurred(QMediaPlayer::Error error, const QString& errorString);
    void onVideoFrameChanged(const QVideoFrame& frame);
    void attemptReconnect();

private:
    int m_slotId;
    QString m_rtspUrl;
    bool m_connected{false};
    bool m_shouldPlay{false};  // Track if user wants playback

    QMediaPlayer* m_player{nullptr};
    QVideoSink* m_frameSink{nullptr};  // For frame access (recording)
    
    // Reconnection
    QTimer* m_reconnectTimer{nullptr};
    int m_reconnectAttempts{0};
    static constexpr int MAX_RECONNECT_ATTEMPTS = 5;
    static constexpr int RECONNECT_DELAY_MS = 3000;
};

} // namespace MCM

#endif // QTRTSPCAPTURE_H

