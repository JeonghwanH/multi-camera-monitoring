#include "RtspCapture.h"
#include <QDebug>
#include <QThread>

namespace MCM {

RtspCapture::RtspCapture(int slotId, QObject* parent)
    : CaptureThread(slotId, parent)
{
}

RtspCapture::~RtspCapture() {
    stopCapture();
    closeStream();
}

void RtspCapture::setRtspUrl(const QString& url) {
    m_rtspUrl = url;
    m_source = url;
    m_sourceType = SourceType::Rtsp;
}

bool RtspCapture::openStream() {
    closeStream();
    
    if (m_rtspUrl.isEmpty()) {
        emit errorOccurred("RTSP URL is empty");
        return false;
    }
    
    // Allocate format context
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        emit errorOccurred("Failed to allocate format context");
        return false;
    }
    
    // Set options for RTSP
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);  // Use TCP for reliability
    av_dict_set(&options, "stimeout", QString::number(READ_TIMEOUT_US).toUtf8().constData(), 0);
    av_dict_set(&options, "analyzeduration", "1000000", 0);  // 1 second
    av_dict_set(&options, "probesize", "1000000", 0);
    
    // Open input
    int ret = avformat_open_input(&m_formatContext, m_rtspUrl.toUtf8().constData(), 
                                   nullptr, &options);
    av_dict_free(&options);
    
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errBuf, sizeof(errBuf));
        emit errorOccurred(QString("Failed to open RTSP stream: %1").arg(errBuf));
        closeStream();
        return false;
    }
    
    // Find stream info
    ret = avformat_find_stream_info(m_formatContext, nullptr);
    if (ret < 0) {
        emit errorOccurred("Failed to find stream info");
        closeStream();
        return false;
    }
    
    // Find video stream
    m_videoStreamIndex = -1;
    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        if (m_formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = static_cast<int>(i);
            break;
        }
    }
    
    if (m_videoStreamIndex < 0) {
        emit errorOccurred("No video stream found");
        closeStream();
        return false;
    }
    
    // Get codec parameters
    AVCodecParameters* codecPar = m_formatContext->streams[m_videoStreamIndex]->codecpar;
    
    // Find decoder
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        emit errorOccurred("Failed to find decoder");
        closeStream();
        return false;
    }
    
    // Allocate codec context
    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext) {
        emit errorOccurred("Failed to allocate codec context");
        closeStream();
        return false;
    }
    
    // Copy parameters to context
    ret = avcodec_parameters_to_context(m_codecContext, codecPar);
    if (ret < 0) {
        emit errorOccurred("Failed to copy codec parameters");
        closeStream();
        return false;
    }
    
    // Open codec
    ret = avcodec_open2(m_codecContext, codec, nullptr);
    if (ret < 0) {
        emit errorOccurred("Failed to open codec");
        closeStream();
        return false;
    }
    
    // Allocate frames
    m_frame = av_frame_alloc();
    m_frameRgb = av_frame_alloc();
    if (!m_frame || !m_frameRgb) {
        emit errorOccurred("Failed to allocate frames");
        closeStream();
        return false;
    }
    
    // Allocate RGB buffer
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, 
                                            m_codecContext->width,
                                            m_codecContext->height, 1);
    m_rgbBuffer = static_cast<uint8_t*>(av_malloc(numBytes));
    if (!m_rgbBuffer) {
        emit errorOccurred("Failed to allocate RGB buffer");
        closeStream();
        return false;
    }
    
    // Setup RGB frame
    av_image_fill_arrays(m_frameRgb->data, m_frameRgb->linesize, m_rgbBuffer,
                         AV_PIX_FMT_RGB24, m_codecContext->width,
                         m_codecContext->height, 1);
    
    // Create scaler context
    m_swsContext = sws_getContext(
        m_codecContext->width, m_codecContext->height, m_codecContext->pix_fmt,
        m_codecContext->width, m_codecContext->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    
    if (!m_swsContext) {
        emit errorOccurred("Failed to create scaler context");
        closeStream();
        return false;
    }
    
    qDebug() << "RtspCapture: Opened stream" << m_rtspUrl 
             << "for slot" << m_slotId
             << "- Resolution:" << m_codecContext->width << "x" << m_codecContext->height;
    
    return true;
}

void RtspCapture::closeStream() {
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    
    if (m_rgbBuffer) {
        av_free(m_rgbBuffer);
        m_rgbBuffer = nullptr;
    }
    
    if (m_frameRgb) {
        av_frame_free(&m_frameRgb);
        m_frameRgb = nullptr;
    }
    
    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }
    
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }
    
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }
    
    m_videoStreamIndex = -1;
}

void RtspCapture::run() {
    m_running = true;
    m_connected = false;
    
    qDebug() << "RtspCapture: Starting capture for slot" << m_slotId 
             << "URL:" << m_rtspUrl;
    
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        emit errorOccurred("Failed to allocate packet");
        return;
    }
    
    while (m_running) {
        // Try to connect if not connected
        if (!m_connected) {
            if (openStream()) {
                m_connected = true;
                emit connectionEstablished();
            } else {
                // Wait before retry (check m_running frequently)
                for (int i = 0; i < 20 && m_running; ++i) {  // 20 * 100ms = 2 seconds
                    QThread::msleep(100);
                }
                continue;
            }
        }
        
        // Read frame
        int ret = av_read_frame(m_formatContext, packet);
        
        if (ret < 0) {
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                QThread::msleep(10);
                continue;
            }
            
            // Lost connection
            if (m_connected) {
                m_connected = false;
                emit connectionLost();
                closeStream();
            }
            
            QThread::msleep(100);
            continue;
        }
        
        // Check if this is our video stream
        if (packet->stream_index == m_videoStreamIndex) {
            QImage qframe = decodePacket(packet);
            if (!qframe.isNull()) {
                processFrame(qframe);
            }
        }
        
        av_packet_unref(packet);
    }
    
    av_packet_free(&packet);
    closeStream();
    m_connected = false;
    
    qDebug() << "RtspCapture: Stopped capture for slot" << m_slotId;
}

QImage RtspCapture::decodePacket(AVPacket* packet) {
    if (!m_codecContext || !m_frame) {
        return QImage();
    }
    
    // Send packet to decoder
    int ret = avcodec_send_packet(m_codecContext, packet);
    if (ret < 0) {
        return QImage();
    }
    
    // Receive frame from decoder
    ret = avcodec_receive_frame(m_codecContext, m_frame);
    if (ret < 0) {
        return QImage();
    }
    
    return frameToQImage(m_frame);
}

QImage RtspCapture::frameToQImage(AVFrame* frame) {
    if (!frame || !m_swsContext || !m_frameRgb) {
        return QImage();
    }
    
    // Convert to RGB
    sws_scale(m_swsContext, frame->data, frame->linesize, 0, 
              m_codecContext->height, m_frameRgb->data, m_frameRgb->linesize);
    
    // Create QImage
    QImage image(m_frameRgb->data[0], m_codecContext->width, m_codecContext->height,
                 m_frameRgb->linesize[0], QImage::Format_RGB888);
    
    return image.copy();  // Return a deep copy
}

} // namespace MCM

