#include "QtCameraCapture.h"
#include "widgets/OptimizedVideoWidget.h"
#include <QDebug>
#include <QCameraFormat>

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
    m_deviceIndex = index;
    
    auto devices = QMediaDevices::videoInputs();
    if (index >= 0 && index < devices.size()) {
        setupCamera(devices[index]);
    } else {
        qWarning() << "QtCameraCapture: Invalid device index" << index 
                   << ", available:" << devices.size();
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
    cleanupCamera();
    
    qDebug() << "QtCameraCapture: Setting up camera" << device.description() 
             << "for slot" << m_slotId;
    
    // Create new camera
    m_camera = new QCamera(device, this);
    
    // Connect camera signals
    connect(m_camera, &QCamera::activeChanged,
            this, &QtCameraCapture::onCameraActiveChanged);
    connect(m_camera, &QCamera::errorOccurred,
            this, &QtCameraCapture::onCameraErrorOccurred);
    
    // Set camera to capture session
    m_session->setCamera(m_camera);
    
    // Also set the frame sink for recording access
    m_session->setVideoSink(m_frameSink);
    
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
        qDebug() << "QtCameraCapture: Selected format" 
                 << bestFormat.resolution() << "@" << bestFormat.maxFrameRate() << "fps";
    }
}

void QtCameraCapture::cleanupCamera() {
    if (m_camera) {
        m_camera->stop();
        m_session->setCamera(nullptr);
        delete m_camera;
        m_camera = nullptr;
    }
    m_connected = false;
}

void QtCameraCapture::setVideoOutput(QObject* videoOutput) {
    m_session->setVideoOutput(videoOutput);
}

void QtCameraCapture::setVideoSink(QVideoSink* sink) {
    // Connect external sink in addition to our frame sink
    if (sink && sink != m_frameSink) {
        connect(m_frameSink, &QVideoSink::videoFrameChanged,
                sink, &QVideoSink::setVideoFrame);
    }
}

void QtCameraCapture::start() {
    if (!m_camera) {
        qWarning() << "QtCameraCapture: No camera set, cannot start";
        emit errorOccurred("No camera device set");
        return;
    }
    
    qDebug() << "QtCameraCapture: Starting camera for slot" << m_slotId;
    m_camera->start();
}

void QtCameraCapture::stop() {
    if (m_camera && m_camera->isActive()) {
        qDebug() << "QtCameraCapture: Stopping camera for slot" << m_slotId;
        m_camera->stop();
    }
    m_connected = false;
}

bool QtCameraCapture::isActive() const {
    return m_camera && m_camera->isActive();
}

void QtCameraCapture::onCameraActiveChanged(bool active) {
    qDebug() << "QtCameraCapture: Camera active changed to" << active 
             << "for slot" << m_slotId;
    
    if (active && !m_connected) {
        m_connected = true;
        emit connectionEstablished();
    } else if (!active && m_connected) {
        m_connected = false;
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
    // Emit frame for recording (if needed)
    if (frame.isValid()) {
        emit frameReady(frame);
    }
}

} // namespace MCM

