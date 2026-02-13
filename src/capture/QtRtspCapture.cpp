#include "QtRtspCapture.h"
#include "widgets/OptimizedVideoWidget.h"
#include <QDebug>

namespace MCM {

QtRtspCapture::QtRtspCapture(int slotId, QObject* parent)
    : QObject(parent)
    , m_slotId(slotId)
{
    qDebug() << "=== QtRtspCapture::Constructor ===" << "slot" << slotId;
    
    // Create media player
    m_player = new QMediaPlayer(this);
    qDebug() << "  QMediaPlayer created:" << m_player;
    
    // Note: Don't set videoSink here - it conflicts with setVideoOutput()
    // Video output will be set via setVideoOutput() when starting stream
    
    // Connect player signals
    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, &QtRtspCapture::onPlaybackStateChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, &QtRtspCapture::onMediaStatusChanged);
    connect(m_player, &QMediaPlayer::errorOccurred,
            this, &QtRtspCapture::onErrorOccurred);
    
    // Track position/duration changes for debugging
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        static qint64 lastLog = 0;
        if (pos - lastLog > 5000) {  // Log every 5 seconds
            qDebug() << "QtRtspCapture slot" << m_slotId << "position:" << pos << "ms";
            lastLog = pos;
        }
    });
    
    // Track video availability
    connect(m_player, &QMediaPlayer::hasVideoChanged, this, [this](bool hasVideo) {
        qDebug() << "*** QtRtspCapture::hasVideoChanged ***" << "slot" << m_slotId 
                 << "hasVideo:" << hasVideo;
    });
    
    // Track audio availability  
    connect(m_player, &QMediaPlayer::hasAudioChanged, this, [this](bool hasAudio) {
        qDebug() << "*** QtRtspCapture::hasAudioChanged ***" << "slot" << m_slotId 
                 << "hasAudio:" << hasAudio;
    });
    
    // Reconnect timer
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &QtRtspCapture::attemptReconnect);
    
    qDebug() << "  QtRtspCapture initialized";
}

QtRtspCapture::~QtRtspCapture() {
    stop();
}

void QtRtspCapture::setRtspUrl(const QString& url) {
    qDebug() << "=== QtRtspCapture::setRtspUrl ===" << "slot" << m_slotId;
    qDebug() << "  URL:" << url;
    m_rtspUrl = url;
    
    // Validate URL format
    QUrl qurl(url);
    if (!qurl.isValid() || qurl.scheme().toLower() != "rtsp") {
        qWarning() << "  WARNING: Invalid RTSP URL format";
    }
    
    m_player->setSource(qurl);
    qDebug() << "  Source set on player";
}

void QtRtspCapture::setVideoOutput(QObject* videoOutput) {
    qDebug() << "=== QtRtspCapture::setVideoOutput ===" << "slot" << m_slotId;
    qDebug() << "  VideoOutput:" << videoOutput;
    if (videoOutput) {
        qDebug() << "  VideoOutput class:" << videoOutput->metaObject()->className();
    }
    qDebug() << "  Player:" << m_player;
    qDebug() << "  Player current videoOutput before:" << m_player->videoOutput();
    m_player->setVideoOutput(videoOutput);
    qDebug() << "  Player videoOutput after:" << m_player->videoOutput();
}

void QtRtspCapture::setVideoSink(QVideoSink* sink) {
    // Connect external sink in addition to our frame sink
    if (sink && sink != m_frameSink) {
        connect(m_frameSink, &QVideoSink::videoFrameChanged,
                sink, &QVideoSink::setVideoFrame);
    }
}

void QtRtspCapture::start() {
    qDebug() << "=== QtRtspCapture::start ===" << "slot" << m_slotId;
    qDebug() << "  URL:" << m_rtspUrl;
    qDebug() << "  Player:" << m_player;
    qDebug() << "  Player source:" << m_player->source();
    qDebug() << "  Player mediaStatus:" << m_player->mediaStatus();
    qDebug() << "  Player playbackState:" << m_player->playbackState();
    qDebug() << "  Player hasVideo:" << m_player->hasVideo();
    qDebug() << "  Player hasAudio:" << m_player->hasAudio();
    qDebug() << "  Player videoOutput:" << m_player->videoOutput();
    
    if (m_rtspUrl.isEmpty()) {
        qWarning() << "  ERROR: No URL set, cannot start";
        emit errorOccurred("No RTSP URL set");
        return;
    }
    
    m_shouldPlay = true;
    m_reconnectAttempts = 0;
    
    qDebug() << "  Calling m_player->play()...";
    m_player->play();
    qDebug() << "  play() called";
    qDebug() << "  After play - playbackState:" << m_player->playbackState();
    qDebug() << "  After play - mediaStatus:" << m_player->mediaStatus();
}

void QtRtspCapture::stop() {
    qDebug() << "=== QtRtspCapture::stop ===" << "slot" << m_slotId;
    m_shouldPlay = false;
    m_reconnectTimer->stop();
    
    if (m_player->playbackState() != QMediaPlayer::StoppedState) {
        qDebug() << "  Stopping playback...";
        m_player->stop();
    }
    
    // Clear video output to ensure clean state for source switching
    qDebug() << "  Clearing video output...";
    m_player->setVideoOutput(nullptr);
    
    m_connected = false;
    qDebug() << "  Stop complete";
}

bool QtRtspCapture::isActive() const {
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

void QtRtspCapture::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {
    QString stateStr;
    switch (state) {
        case QMediaPlayer::StoppedState: stateStr = "Stopped"; break;
        case QMediaPlayer::PlayingState: stateStr = "Playing"; break;
        case QMediaPlayer::PausedState: stateStr = "Paused"; break;
    }
    qDebug() << "*** QtRtspCapture::onPlaybackStateChanged ***" << "slot" << m_slotId
             << "state:" << stateStr << "m_connected:" << m_connected;
    
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
    QString statusStr;
    switch (status) {
        case QMediaPlayer::NoMedia: statusStr = "NoMedia"; break;
        case QMediaPlayer::LoadingMedia: statusStr = "LoadingMedia"; break;
        case QMediaPlayer::LoadedMedia: statusStr = "LoadedMedia"; break;
        case QMediaPlayer::StalledMedia: statusStr = "StalledMedia"; break;
        case QMediaPlayer::BufferingMedia: statusStr = "BufferingMedia"; break;
        case QMediaPlayer::BufferedMedia: statusStr = "BufferedMedia"; break;
        case QMediaPlayer::EndOfMedia: statusStr = "EndOfMedia"; break;
        case QMediaPlayer::InvalidMedia: statusStr = "InvalidMedia"; break;
    }
    qDebug() << "*** QtRtspCapture::onMediaStatusChanged ***" << "slot" << m_slotId
             << "status:" << statusStr;
    qDebug() << "  hasVideo:" << m_player->hasVideo() << "hasAudio:" << m_player->hasAudio();
    qDebug() << "  playbackState:" << m_player->playbackState();
    qDebug() << "  videoOutput:" << m_player->videoOutput();
    
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
    QString errorType;
    switch (error) {
        case QMediaPlayer::NoError: errorType = "NoError"; break;
        case QMediaPlayer::ResourceError: errorType = "ResourceError"; break;
        case QMediaPlayer::FormatError: errorType = "FormatError"; break;
        case QMediaPlayer::NetworkError: errorType = "NetworkError"; break;
        case QMediaPlayer::AccessDeniedError: errorType = "AccessDeniedError"; break;
    }
    qWarning() << "*** QtRtspCapture::onErrorOccurred ***" << "slot" << m_slotId
               << "error:" << errorType << "-" << errorString;
    
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

