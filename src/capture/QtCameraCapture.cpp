#include "QtCameraCapture.h"
#include "widgets/OptimizedVideoWidget.h"
#include <QDebug>
#include <QCameraFormat>
#include <QGraphicsVideoItem>
#include <QElapsedTimer>
#include <QThread>
#include <QCoreApplication>
#include <QDateTime>

namespace MCM {

QtCameraCapture::QtCameraCapture(int slotId, QObject* parent)
    : QObject(parent)
    , m_slotId(slotId)
    , m_frameCount(0)
    , m_lastFrameTime(QDateTime::currentMSecsSinceEpoch())
{
    // Create capture session (manages the pipeline)
    m_session = new QMediaCaptureSession(this);
    
    // Create a video sink for frame access (needed for recording)
    m_frameSink = new QVideoSink(this);
    connect(m_frameSink, &QVideoSink::videoFrameChanged,
            this, &QtCameraCapture::onVideoFrameChanged);
    
    qDebug() << "QtCameraCapture::Constructor slot" << m_slotId 
             << "thread:" << QThread::currentThread()
             << "main:" << QCoreApplication::instance()->thread();
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
    QElapsedTimer timer;
    timer.start();
    
    qDebug() << "=== QtCameraCapture::setupCamera START ===" << "slot" << m_slotId;
    qDebug() << "  Device:" << device.description() << "ID:" << device.id();
    qDebug() << "  Thread:" << QThread::currentThread() 
             << "isMainThread:" << (QThread::currentThread() == QCoreApplication::instance()->thread());
    
    // Reset frame counter for new camera
    m_frameCount = 0;
    m_lastFrameTime = QDateTime::currentMSecsSinceEpoch();
    
    // Store video output to restore after camera swap (like test_qt_only approach)
    QObject* savedVideoOutput = m_videoOutput;
    
    // Stop and delete old camera only (KEEP the session - this is key!)
    if (m_camera) {
        qDebug() << "  [" << timer.elapsed() << "ms] Stopping and deleting old camera...";
        disconnect(m_camera, nullptr, this, nullptr);
        if (m_camera->isActive()) {
            m_camera->stop();
        }
        m_session->setCamera(nullptr);  // Disconnect from session before delete
        delete m_camera;
        m_camera = nullptr;
        qDebug() << "  [" << timer.elapsed() << "ms] Old camera deleted";
    }
    
    // Reset connected state for clean start
    m_connected = false;
    
    // Reuse existing session or create if needed (first time)
    if (!m_session) {
        qDebug() << "  [" << timer.elapsed() << "ms] Creating session for slot" << m_slotId;
        m_session = new QMediaCaptureSession(this);
    }
    qDebug() << "  Using session:" << m_session;
    
    // Create new camera
    qDebug() << "  [" << timer.elapsed() << "ms] Creating QCamera...";
    m_camera = new QCamera(device, this);
    qDebug() << "  [" << timer.elapsed() << "ms] NEW camera created:" << m_camera;
    
    // Connect camera signals
    connect(m_camera, &QCamera::activeChanged,
            this, &QtCameraCapture::onCameraActiveChanged);
    connect(m_camera, &QCamera::errorOccurred,
            this, &QtCameraCapture::onCameraErrorOccurred);
    
    // Set camera to capture session
    // WARNING: This call can block for 1-4 seconds on Linux with USB capture cards!
    // It triggers GStreamer pipeline negotiation which is synchronous.
    qDebug() << "  [" << timer.elapsed() << "ms] Setting camera to session (may block)...";
    m_session->setCamera(m_camera);
    qDebug() << "  [" << timer.elapsed() << "ms] Camera set to session";
    
    // CRITICAL: Process events after the blocking setCamera() call
    // This allows video surfaces for OTHER cameras to initialize
    // Without this, "Failed to start video surface due to main thread blocked" occurs
    QCoreApplication::processEvents();
    qDebug() << "  [" << timer.elapsed() << "ms] Events processed after setCamera";
    
    // Configure camera format (prefer 1080p @ 30fps for quality)
    auto formats = device.videoFormats();
    QCameraFormat bestFormat;
    int bestScore = -1;
    
    for (const auto& format : formats) {
        QSize res = format.resolution();
        float fps = format.maxFrameRate();
        
        int score = 0;
        // Prefer 720p to reduce buffer pool contention with multiple cameras
        if (res.height() == 720) {
            score += 1000;  // 720p highest priority
        } else if (res.height() == 1080) {
            score += 800;   // 1080p as fallback
        } else if (res.height() >= 480 && res.height() <= 1080) {
            score += 100;
        }
        // Prefer frame rates around 30fps
        if (fps >= 25 && fps <= 35) {
            score += 100;
        } else if (fps >= 50 && fps <= 65) {
            score += 50;    // 60fps acceptable but not preferred (higher bandwidth)
        }
        
        if (score > bestScore) {
            bestScore = score;
            bestFormat = format;
        }
    }
    
    if (!bestFormat.isNull()) {
        qDebug() << "  [" << timer.elapsed() << "ms] Setting camera format...";
        m_camera->setCameraFormat(bestFormat);
        qDebug() << "  [" << timer.elapsed() << "ms] Selected format:" << bestFormat.resolution() 
                 << "@" << bestFormat.maxFrameRate() << "fps";
        
        // Process events after format change to allow other video surfaces to initialize
        QCoreApplication::processEvents();
    }
    
    // Restore video output if it was set (like test_qt_only does)
    if (savedVideoOutput) {
        qDebug() << "  [" << timer.elapsed() << "ms] Restoring video output...";
        m_session->setVideoOutput(savedVideoOutput);
        m_videoOutput = savedVideoOutput;
        qDebug() << "  [" << timer.elapsed() << "ms] Restored video output:" << savedVideoOutput;
    }
    
    qDebug() << "=== QtCameraCapture::setupCamera END ===" << "slot" << m_slotId 
             << "total:" << timer.elapsed() << "ms";
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
    QElapsedTimer timer;
    timer.start();
    m_startRequestTime = QDateTime::currentMSecsSinceEpoch();
    
    qDebug() << "=== QtCameraCapture::start ===" << "slot" << m_slotId;
    qDebug() << "  Camera:" << m_camera << "Session:" << m_session << "VideoOutput:" << m_videoOutput;
    qDebug() << "  Thread:" << QThread::currentThread()
             << "isMainThread:" << (QThread::currentThread() == QCoreApplication::instance()->thread());
    
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
    } else {
        QGraphicsVideoItem* videoItem = qobject_cast<QGraphicsVideoItem*>(m_videoOutput);
        if (videoItem) {
            qDebug() << "  VideoItem state: size=" << videoItem->size() 
                     << "nativeSize=" << videoItem->nativeSize()
                     << "visible=" << videoItem->isVisible()
                     << "opacity=" << videoItem->opacity();
        }
    }
    
    // Start immediately like test_qt_only does
    qDebug() << "  [" << timer.elapsed() << "ms] Calling m_camera->start()...";
    m_camera->start();
    qDebug() << "  [" << timer.elapsed() << "ms] Camera start() returned, active:" << m_camera->isActive();
    
    // Log camera state
    qDebug() << "  Camera error:" << m_camera->error() << m_camera->errorString();
    
    // Process events to allow video surface to receive first frames
    // This is critical for allowing GStreamer to complete pipeline setup
    QCoreApplication::processEvents();
    qDebug() << "  [" << timer.elapsed() << "ms] Events processed after start";
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
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 timeSinceStart = m_startRequestTime > 0 ? (now - m_startRequestTime) : 0;
    
    qDebug() << "*** QtCameraCapture::onCameraActiveChanged ***" << "slot" << m_slotId
             << "active:" << active << "was_connected:" << m_connected
             << "timeSinceStart:" << timeSinceStart << "ms"
             << "framesReceived:" << m_frameCount;
    
    if (active && !m_connected) {
        m_connected = true;
        qDebug() << "  ✓ Camera ACTIVE for slot" << m_slotId << "- emitting connectionEstablished";
        emit connectionEstablished();
    } else if (!active && m_connected) {
        m_connected = false;
        qDebug() << "  ✗ Camera INACTIVE for slot" << m_slotId << "- emitting connectionLost";
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
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    
    // Log first 10 frames AND every 100th frame with timing info
    if (m_frameCount < 10 || (m_frameCount % 100 == 0)) {
        qint64 timeSinceStart = m_startRequestTime > 0 ? (now - m_startRequestTime) : 0;
        qint64 timeSinceLastFrame = now - m_lastFrameTime;
        
        qDebug() << "*** FRAME RECEIVED *** slot" << m_slotId
                 << "frame#" << m_frameCount 
                 << "valid:" << frame.isValid()
                 << "size:" << frame.size()
                 << "sinceStart:" << timeSinceStart << "ms"
                 << "interval:" << timeSinceLastFrame << "ms";
        
        // First frame is especially important - log more details
        if (m_frameCount == 0) {
            qDebug() << "  ★★★ FIRST FRAME for slot" << m_slotId << "★★★"
                     << "Time from start() to first frame:" << timeSinceStart << "ms";
        }
    }
    
    m_frameCount++;
    m_lastFrameTime = now;
    
    // Emit frame for recording (if needed)
    if (frame.isValid()) {
        emit frameReady(frame);
    }
}

} // namespace MCM

