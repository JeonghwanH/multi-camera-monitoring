#include "QtCameraCapture.h"
#include "widgets/OptimizedVideoWidget.h"
#include <QDebug>
#include <QCameraFormat>
#include <QGraphicsVideoItem>

namespace MCM {

QtCameraCapture::QtCameraCapture(int slotId, QObject* parent)
    : QObject(parent)
    , m_slotId(slotId)
{
    // Create capture session (manages the pipeline)
    m_session = new QMediaCaptureSession(this);
    
    // Create a video sink for frame access (needed for recording)
    m_frameSink = new QVideoSink(this);
    connect(m_frameSink, &QVideoSink::videoFrameChanged,
            this, &QtCameraCapture::onVideoFrameChanged);
}

QtCameraCapture::~QtCameraCapture() {
    stop();
    cleanupCamera();
}

QList<QCameraDevice> QtCameraCapture::availableDevices() {
    return QMediaDevices::videoInputs();
}

void QtCameraCapture::setDeviceIndex(int index) {
    qDebug() << "=== QtCameraCapture::setDeviceIndex ===" << "slot" << m_slotId << "index:" << index;
    m_deviceIndex = index;
    
    auto devices = QMediaDevices::videoInputs();
    qDebug() << "  Available devices:" << devices.size();
    for (int i = 0; i < devices.size(); ++i) {
        qDebug() << "    [" << i << "]" << devices[i].description();
    }
    
    if (index >= 0 && index < devices.size()) {
        qDebug() << "  Using device:" << devices[index].description();
        setupCamera(devices[index]);
    } else {
        qWarning() << "  ERROR: Invalid device index" << index 
                   << ", available:" << devices.size();
        // Clear any existing camera when device is invalid
        cleanupCamera();
        emit errorOccurred(QString("Invalid device index %1").arg(index));
    }
}

void QtCameraCapture::setCameraDevice(const QCameraDevice& device) {
    if (device.isNull()) {
        qWarning() << "QtCameraCapture: Null camera device";
        emit errorOccurred("Null camera device");
        return;
    }
    
    m_useV4L2Pipeline = false;  // Use QCamera
    m_devicePath.clear();
    
    // Find the index for this device
    auto devices = QMediaDevices::videoInputs();
    for (int i = 0; i < devices.size(); ++i) {
        if (devices[i].id() == device.id()) {
            m_deviceIndex = i;
            break;
        }
    }
    
    setupCamera(device);
}

void QtCameraCapture::setDevicePath(const QString& path) {
    qDebug() << "=== QtCameraCapture::setDevicePath ===" << "slot" << m_slotId << "path:" << path;
    
    if (path.isEmpty()) {
        qWarning() << "  ERROR: Empty device path";
        emit errorOccurred("Empty device path");
        return;
    }
    
    m_devicePath = path;
    m_useV4L2Pipeline = true;  // Use QMediaPlayer with GStreamer pipeline
    
    setupV4L2Pipeline(path);
}

void QtCameraCapture::setupV4L2Pipeline(const QString& devicePath) {
    qDebug() << "=== QtCameraCapture::setupV4L2Pipeline ===" << "slot" << m_slotId;
    qDebug() << "  Device path:" << devicePath;
    
    // Clean up any existing QCamera setup
    cleanupCamera();
    
    // Also clean up old session
    if (m_session) {
        m_session->setVideoOutput(nullptr);
        delete m_session;
        m_session = nullptr;
    }
    
    // Clean up old player
    if (m_player) {
        m_player->stop();
        delete m_player;
        m_player = nullptr;
    }
    
    m_videoOutput = nullptr;
    m_connected = false;
    
    // Create QMediaPlayer for GStreamer pipeline
    m_player = new QMediaPlayer(this);
    qDebug() << "  Created QMediaPlayer:" << m_player;
    
    // Connect player signals
    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, &QtCameraCapture::onPlayerStateChanged);
    connect(m_player, &QMediaPlayer::errorOccurred,
            this, &QtCameraCapture::onPlayerErrorOccurred);
    
    // Build GStreamer pipeline URL
    // Format: gst-pipeline: v4l2src device=/dev/video0 ! videoconvert ! video/x-raw,format=RGB ! appsink name=sink
    QString pipeline = QString("gst-pipeline: v4l2src device=%1 ! videoconvert ! videoscale ! video/x-raw ! appsink name=qtvideosink").arg(devicePath);
    
    qDebug() << "  GStreamer pipeline:" << pipeline;
    m_player->setSource(QUrl(pipeline));
    
    qDebug() << "=== setupV4L2Pipeline END ===" << "slot" << m_slotId;
}

void QtCameraCapture::setupCamera(const QCameraDevice& device) {
    qDebug() << "=== QtCameraCapture::setupCamera START ===" << "slot" << m_slotId;
    qDebug() << "  Device:" << device.description() << "ID:" << device.id();
    
    // Don't store old video output - we always get a fresh one from CameraSlot
    // after resetVideoItem() creates a new QGraphicsVideoItem
    qDebug() << "  Clearing stored video output (will be set fresh)";
    m_videoOutput = nullptr;
    
    // Full cleanup - delete camera and session
    if (m_camera) {
        qDebug() << "  Stopping and deleting old camera...";
        // Disconnect signals first to avoid callbacks during destruction
        disconnect(m_camera, nullptr, this, nullptr);
        if (m_camera->isActive()) {
            m_camera->stop();
        }
        delete m_camera;
        m_camera = nullptr;
    }
    if (m_session) {
        // IMPORTANT: Disconnect video output BEFORE deleting session
        // This prevents QGraphicsVideoItem from getting into an inconsistent state
        qDebug() << "  Clearing video output from old session...";
        m_session->setVideoOutput(nullptr);
        qDebug() << "  Deleting old session...";
        delete m_session;
        m_session = nullptr;
    }
    
    // Reset connected state for clean start
    m_connected = false;
    
    qDebug() << "  Creating NEW session and camera for slot" << m_slotId;
    
    // Create FRESH session (identical to constructor)
    m_session = new QMediaCaptureSession(this);
    qDebug() << "  NEW session created:" << m_session;
    
    // Create new camera
    m_camera = new QCamera(device, this);
    qDebug() << "  NEW camera created:" << m_camera;
    
    // Connect camera signals
    connect(m_camera, &QCamera::activeChanged,
            this, &QtCameraCapture::onCameraActiveChanged);
    connect(m_camera, &QCamera::errorOccurred,
            this, &QtCameraCapture::onCameraErrorOccurred);
    
    // Set camera to capture session
    m_session->setCamera(m_camera);
    qDebug() << "  Camera set to session";
    qDebug() << "  Video output will be set later via setVideoOutput()";
    qDebug() << "  Frame access will be connected to video item's sink";
    
    // Configure camera format (prefer 720p @ 30fps for performance)
    auto formats = device.videoFormats();
    QCameraFormat bestFormat;
    int bestScore = -1;
    
    for (const auto& format : formats) {
        QSize res = format.resolution();
        float fps = format.maxFrameRate();
        
        // Score based on: prefer 720p, then 1080p, then others
        // Also prefer 30fps
        int score = 0;
        
        if (res.height() == 720) {
            score += 1000;  // Prefer 720p (good balance)
        } else if (res.height() == 1080) {
            score += 500;   // 1080p is okay too
        } else if (res.height() >= 480 && res.height() <= 1080) {
            score += 100;   // Acceptable range
        }
        
        // Prefer ~30 fps
        if (fps >= 25 && fps <= 35) {
            score += 100;
        }
        
        if (score > bestScore) {
            bestScore = score;
            bestFormat = format;
        }
    }
    
    if (!bestFormat.isNull()) {
        m_camera->setCameraFormat(bestFormat);
        qDebug() << "  Selected format:" << bestFormat.resolution() 
                 << "@" << bestFormat.maxFrameRate() << "fps";
    } else {
        qDebug() << "  WARNING: No suitable format found, using default";
    }
    
    qDebug() << "=== QtCameraCapture::setupCamera END ===" << "slot" << m_slotId;
}

void QtCameraCapture::cleanupCamera() {
    if (m_camera) {
        m_camera->stop();
        delete m_camera;
        m_camera = nullptr;
    }
    // Don't delete session here - it's managed by setupCamera or destructor
    m_connected = false;
}

void QtCameraCapture::setVideoOutput(QObject* videoOutput) {
    qDebug() << "QtCameraCapture::setVideoOutput" << "slot" << m_slotId 
             << "videoOutput:" << videoOutput 
             << "useV4L2Pipeline:" << m_useV4L2Pipeline;
    
    m_videoOutput = videoOutput;  // Store for later use
    
    if (m_useV4L2Pipeline && m_player) {
        // V4L2 mode: use QMediaPlayer
        m_player->setVideoOutput(videoOutput);
        qDebug() << "  Video output SET on player (V4L2 mode)";
    } else if (m_session) {
        // QCamera mode: use session
        m_session->setVideoOutput(videoOutput);
        qDebug() << "  Video output SET on session (QCamera mode)";
    } else {
        qDebug() << "  WARNING: No session or player to set video output on!";
    }
    
    // Connect to the video item's internal sink for frame access
    QGraphicsVideoItem* videoItem = qobject_cast<QGraphicsVideoItem*>(videoOutput);
    if (videoItem) {
        QVideoSink* itemSink = videoItem->videoSink();
        if (itemSink) {
            // Disconnect any previous connection to avoid duplicates
            disconnect(itemSink, &QVideoSink::videoFrameChanged,
                      this, &QtCameraCapture::onVideoFrameChanged);
            connect(itemSink, &QVideoSink::videoFrameChanged,
                   this, &QtCameraCapture::onVideoFrameChanged);
            qDebug() << "  Connected to video item's sink for frame access";
        }
    }
}

void QtCameraCapture::setVideoSink(QVideoSink* sink) {
    // Connect external sink in addition to our frame sink
    if (sink && sink != m_frameSink) {
        connect(m_frameSink, &QVideoSink::videoFrameChanged,
                sink, &QVideoSink::setVideoFrame);
    }
}

void QtCameraCapture::start() {
    qDebug() << "=== QtCameraCapture::start ===" << "slot" << m_slotId;
    qDebug() << "  useV4L2Pipeline:" << m_useV4L2Pipeline;
    qDebug() << "  Camera:" << m_camera << "Player:" << m_player << "VideoOutput:" << m_videoOutput;
    
    if (!m_videoOutput) {
        qWarning() << "  WARNING: No video output set - frames won't be displayed!";
    }
    
    if (m_useV4L2Pipeline) {
        // V4L2 mode: start QMediaPlayer
        if (!m_player) {
            qWarning() << "  ERROR: No player set for V4L2 mode";
            emit errorOccurred("No player for V4L2 capture");
            return;
        }
        
        qDebug() << "  Calling m_player->play() (V4L2 mode)...";
        qDebug() << "  Player source:" << m_player->source();
        m_player->play();
        qDebug() << "  Player play() called, state:" << m_player->playbackState();
    } else {
        // QCamera mode
        if (!m_camera) {
            qWarning() << "  ERROR: No camera set, cannot start";
            emit errorOccurred("No camera device set");
            return;
        }
        
        if (!m_session) {
            qWarning() << "  ERROR: No session!";
        }
        
        qDebug() << "  Calling m_camera->start() (QCamera mode)...";
        m_camera->start();
        qDebug() << "  Camera start() called, active:" << m_camera->isActive();
    }
}

void QtCameraCapture::stop() {
    qDebug() << "=== QtCameraCapture::stop ===" << "slot" << m_slotId;
    qDebug() << "  useV4L2Pipeline:" << m_useV4L2Pipeline;
    
    if (m_useV4L2Pipeline) {
        // V4L2 mode: stop QMediaPlayer
        if (m_player) {
            qDebug() << "  Stopping player...";
            m_player->stop();
            m_player->setVideoOutput(nullptr);
        }
    } else {
        // QCamera mode
        qDebug() << "  Camera:" << m_camera << "active:" << (m_camera ? m_camera->isActive() : false);
        if (m_camera && m_camera->isActive()) {
            qDebug() << "  Stopping camera...";
            m_camera->stop();
        }
        if (m_session) {
            qDebug() << "  Clearing video output from session...";
            m_session->setVideoOutput(nullptr);
        }
    }
    
    m_videoOutput = nullptr;  // Clear stored pointer - video item will be recreated
    m_connected = false;
    qDebug() << "  Stop complete";
}

bool QtCameraCapture::isActive() const {
    if (m_useV4L2Pipeline) {
        return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    }
    return m_camera && m_camera->isActive();
}

void QtCameraCapture::onCameraActiveChanged(bool active) {
    qDebug() << "*** QtCameraCapture::onCameraActiveChanged ***" << "slot" << m_slotId
             << "active:" << active << "was_connected:" << m_connected;
    
    if (active && !m_connected) {
        m_connected = true;
        qDebug() << "  Emitting connectionEstablished signal";
        emit connectionEstablished();
    } else if (!active && m_connected) {
        m_connected = false;
        qDebug() << "  Emitting connectionLost signal";
        emit connectionLost();
    }
}

void QtCameraCapture::onCameraErrorOccurred(QCamera::Error error, const QString& errorString) {
    qWarning() << "QtCameraCapture: Error" << error << "-" << errorString 
               << "for slot" << m_slotId;
    
    if (m_connected) {
        m_connected = false;
        emit connectionLost();
    }
    
    emit errorOccurred(errorString);
}

void QtCameraCapture::onVideoFrameChanged(const QVideoFrame& frame) {
    // Debug: Log first few frames to confirm pipeline is working
    static int frameCount = 0;
    if (frameCount < 5) {
        qDebug() << "QtCameraCapture::onVideoFrameChanged slot" << m_slotId
                 << "frame#" << frameCount << "valid:" << frame.isValid()
                 << "size:" << frame.size();
        frameCount++;
    }
    
    // Emit frame for recording (if needed)
    if (frame.isValid()) {
        emit frameReady(frame);
    }
}

void QtCameraCapture::onPlayerStateChanged(QMediaPlayer::PlaybackState state) {
    qDebug() << "*** QtCameraCapture::onPlayerStateChanged ***" << "slot" << m_slotId
             << "state:" << state << "was_connected:" << m_connected;
    
    if (state == QMediaPlayer::PlayingState && !m_connected) {
        m_connected = true;
        qDebug() << "  Emitting connectionEstablished signal (V4L2 mode)";
        emit connectionEstablished();
    } else if (state == QMediaPlayer::StoppedState && m_connected) {
        m_connected = false;
        qDebug() << "  Emitting connectionLost signal (V4L2 mode)";
        emit connectionLost();
    }
}

void QtCameraCapture::onPlayerErrorOccurred(QMediaPlayer::Error error, const QString& errorString) {
    qWarning() << "QtCameraCapture: Player error" << error << "-" << errorString 
               << "for slot" << m_slotId;
    
    if (m_connected) {
        m_connected = false;
        emit connectionLost();
    }
    
    emit errorOccurred(errorString);
}

} // namespace MCM

