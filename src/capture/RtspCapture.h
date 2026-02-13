#ifndef RTSPCAPTURE_H
#define RTSPCAPTURE_H

#include "CaptureThread.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace MCM {

/**
 * @brief Capture thread for RTSP streams
 * 
 * Uses FFmpeg for robust RTSP stream handling.
 * Supports automatic reconnection on stream loss.
 */
class RtspCapture : public CaptureThread {
    Q_OBJECT

public:
    explicit RtspCapture(int slotId, QObject* parent = nullptr);
    ~RtspCapture() override;

    /**
     * @brief Set the RTSP URL to capture from
     */
    void setRtspUrl(const QString& url);

    /**
     * @brief Get the current RTSP URL
     */
    QString rtspUrl() const { return m_rtspUrl; }

protected:
    void run() override;

private:
    /**
     * @brief Initialize FFmpeg and open stream
     * @return true if successful
     */
    bool openStream();

    /**
     * @brief Close the stream and cleanup FFmpeg resources
     */
    void closeStream();

    /**
     * @brief Decode a packet and convert to QImage
     * @return The decoded frame or null QImage on failure
     */
    QImage decodePacket(AVPacket* packet);

    /**
     * @brief Convert AVFrame to QImage
     */
    QImage frameToQImage(AVFrame* frame);

    QString m_rtspUrl;
    
    // FFmpeg resources
    AVFormatContext* m_formatContext{nullptr};
    AVCodecContext* m_codecContext{nullptr};
    SwsContext* m_swsContext{nullptr};
    AVFrame* m_frame{nullptr};
    AVFrame* m_frameRgb{nullptr};
    uint8_t* m_rgbBuffer{nullptr};
    int m_videoStreamIndex{-1};
    
    // Reconnection settings
    static constexpr int RECONNECT_DELAY_MS = 5000;
    static constexpr int READ_TIMEOUT_US = 5000000;  // 5 seconds
};

} // namespace MCM

#endif // RTSPCAPTURE_H

