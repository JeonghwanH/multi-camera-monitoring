#include "QtRtspCapture.h"
#include "widgets/OptimizedVideoWidget.h"
#include <QDebug>

namespace MCM {

QtRtspCapture::QtRtspCapture(int slotId, QObject* parent)
    : QObject(parent)
    , m_slotId(slotId)
{
    // Create media player
    m_player = new QMediaPlayer(this);
    
    // Create a video sink for frame access (needed for recording)
    m_frameSink = new QVideoSink(this);
    m_player->setVideoSink(m_frameSink);
    
    // Connect player signals
    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, &QtRtspCapture::onPlaybackStateChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, &QtRtspCapture::onMediaStatusChanged);
    connect(m_player, &QMediaPlayer::errorOccurred,
            this, &QtRtspCapture::onErrorOccurred);
    
    // Connect frame sink for recording access
    connect(m_frameSink, &QVideoSink::videoFrameChanged,
            this, &QtRtspCapture::onVideoFrameChanged);
    
    // Reconnect timer
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &QtRtspCapture::attemptReconnect);
}

QtRtspCapture::~QtRtspCapture() {
    stop();
}

void QtRtspCapture::setRtspUrl(const QString& url) {
    m_rtspUrl = url;
    
    // Validate URL format
    QUrl qurl(url);
    if (!qurl.isValid() || qurl.scheme().toLower() != "rtsp") {
        qWarning() << "QtRtspCapture: Invalid RTSP URL:" << url;
    }
    
    m_player->setSource(qurl);
    
    qDebug() << "QtRtspCapture: Set URL" << url << "for slot" << m_slotId;
}

void QtRtspCapture::setVideoOutput(QObject* videoOutput) {
    m_player->setVideoOutput(videoOutput);
}

void QtRtspCapture::setVideoSink(QVideoSink* sink) {
    // Connect external sink in addition to our frame sink
    if (sink && sink != m_frameSink) {
        connect(m_frameSink, &QVideoSink::videoFrameChanged,
                sink, &QVideoSink::setVideoFrame);
    }
}

void QtRtspCapture::start() {
    if (m_rtspUrl.isEmpty()) {
        qWarning() << "QtRtspCapture: No URL set, cannot start";
        emit errorOccurred("No RTSP URL set");
        return;
    }
    
    m_shouldPlay = true;
    m_reconnectAttempts = 0;
    
    qDebug() << "QtRtspCapture: Starting playback for slot" << m_slotId 
             << "URL:" << m_rtspUrl;
    
    m_player->play();
}

void QtRtspCapture::stop() {
    m_shouldPlay = false;
    m_reconnectTimer->stop();
    
    if (m_player->playbackState() != QMediaPlayer::StoppedState) {
        qDebug() << "QtRtspCapture: Stopping playback for slot" << m_slotId;
        m_player->stop();
    }
    
    m_connected = false;
}

bool QtRtspCapture::isActive() const {
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

void QtRtspCapture::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {
    qDebug() << "QtRtspCapture: Playback state changed to" << state 
             << "for slot" << m_slotId;
    
    if (state == QMediaPlayer::PlayingState && !m_connected) {
        m_connected = true;
        m_reconnectAttempts = 0;
        emit connectionEstablished();
    } else if (state == QMediaPlayer::StoppedState && m_connected) {
        m_connected = false;
        emit connectionLost();
        
        // Attempt reconnection if we should be playing
        if (m_shouldPlay && m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
            m_reconnectTimer->start(RECONNECT_DELAY_MS);
        }
    }
}

void QtRtspCapture::onMediaStatusChanged(QMediaPlayer::MediaStatus status) {
    qDebug() << "QtRtspCapture: Media status changed to" << status 
             << "for slot" << m_slotId;
    
    switch (status) {
        case QMediaPlayer::LoadedMedia:
            // Stream loaded, ready to play
            if (m_shouldPlay && m_player->playbackState() != QMediaPlayer::PlayingState) {
                m_player->play();
            }
            break;
            
        case QMediaPlayer::BufferedMedia:
            // Buffering complete, connection stable
            if (!m_connected) {
                m_connected = true;
                m_reconnectAttempts = 0;
                emit connectionEstablished();
            }
            break;
            
        case QMediaPlayer::EndOfMedia:
        case QMediaPlayer::InvalidMedia:
            // Stream ended or invalid
            if (m_connected) {
                m_connected = false;
                emit connectionLost();
            }
            
            // Attempt reconnection
            if (m_shouldPlay && m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
                m_reconnectTimer->start(RECONNECT_DELAY_MS);
            }
            break;
            
        case QMediaPlayer::StalledMedia:
            // Network stall - wait for recovery
            qDebug() << "QtRtspCapture: Stream stalled for slot" << m_slotId;
            break;
            
        default:
            break;
    }
}

void QtRtspCapture::onErrorOccurred(QMediaPlayer::Error error, const QString& errorString) {
    qWarning() << "QtRtspCapture: Error" << error << "-" << errorString 
               << "for slot" << m_slotId;
    
    if (m_connected) {
        m_connected = false;
        emit connectionLost();
    }
    
    emit errorOccurred(errorString);
    
    // Attempt reconnection on error
    if (m_shouldPlay && m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        m_reconnectTimer->start(RECONNECT_DELAY_MS);
    }
}

void QtRtspCapture::attemptReconnect() {
    if (!m_shouldPlay) {
        return;
    }
    
    m_reconnectAttempts++;
    qDebug() << "QtRtspCapture: Reconnect attempt" << m_reconnectAttempts 
             << "/" << MAX_RECONNECT_ATTEMPTS << "for slot" << m_slotId;
    
    // Reset source and play
    m_player->setSource(QUrl(m_rtspUrl));
    m_player->play();
}

void QtRtspCapture::onVideoFrameChanged(const QVideoFrame& frame) {
    // Emit frame for recording (if needed)
    if (frame.isValid()) {
        emit frameReady(frame);
    }
}

} // namespace MCM

