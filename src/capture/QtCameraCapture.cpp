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
             << "videoOutput:" << videoOutput << "session:" << m_session;
    m_videoOutput = videoOutput;  // Store for session recreation
    if (m_session) {
        m_session->setVideoOutput(videoOutput);
        qDebug() << "  Video output SET on session";
        
        // Connect to the video item's internal sink for frame access
        // This allows us to get frames for FPS calculation without conflicting with display
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
    } else {
        qDebug() << "  WARNING: No session to set video output on!";
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
    qDebug() << "  Camera:" << m_camera << "Session:" << m_session << "VideoOutput:" << m_videoOutput;
    
    if (!m_camera) {
        qWarning() << "  ERROR: No camera set, cannot start";
        emit errorOccurred("No camera device set");
        return;
    }
    
    if (!m_session) {
        qWarning() << "  ERROR: No session!";
    }
    
    if (!m_videoOutput) {
        qWarning() << "  WARNING: No video output set - frames won't be displayed!";
    }
    
    qDebug() << "  Calling m_camera->start()...";
    m_camera->start();
    qDebug() << "  Camera start() called, active:" << m_camera->isActive();
}

void QtCameraCapture::stop() {
    qDebug() << "=== QtCameraCapture::stop ===" << "slot" << m_slotId;
    qDebug() << "  Camera:" << m_camera << "active:" << (m_camera ? m_camera->isActive() : false);
    
    if (m_camera && m_camera->isActive()) {
        qDebug() << "  Stopping camera...";
        m_camera->stop();
    }
    
    // Clear video output to ensure clean state for source switching
    // Also clear stored pointer since the video item may be deleted
    if (m_session) {
        qDebug() << "  Clearing video output from session...";
        m_session->setVideoOutput(nullptr);
    }
    m_videoOutput = nullptr;  // Clear stored pointer - video item will be recreated
    
    m_connected = false;
    qDebug() << "  Stop complete";
}

bool QtCameraCapture::isActive() const {
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

} // namespace MCM

