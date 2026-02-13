#include "CameraSlot.h"
#include "VideoWidget.h"
#include "RtspInputDialog.h"
#include "capture/DeviceCapture.h"
#include "capture/RtspCapture.h"
#include "core/FrameBuffer.h"
#include "core/VideoRecorder.h"
#include "utils/DeviceDetector.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QDebug>
#include <cstdlib>

namespace MCM {

CameraSlot::CameraSlot(int slotIndex, DeviceDetector* detector, QWidget* parent)
    : QWidget(parent)
    , m_slotIndex(slotIndex)
    , m_deviceDetector(detector)
    , m_displayTimer(new QTimer(this))
    , m_debugMode(std::getenv("MCM_DEBUG") != nullptr)
{
    setupUi();
    setupCapture();
    
    // Display update timer (from config)
    connect(m_displayTimer, &QTimer::timeout, this, &CameraSlot::updateDisplay);
    int displayFps = Config::instance().buffer().displayFps;
    m_displayTimer->setInterval(1000 / displayFps);  // Convert FPS to interval
    
    // Start FPS tracking timer
    m_fpsTimer.start();
    
    // Load slot configuration
    const auto& slotConfig = Config::instance().slot(m_slotIndex);
    updateSourceSelector();
    
    // Find and select the configured source (block signals to prevent premature stream start)
    m_sourceSelector->blockSignals(true);
    for (int i = 0; i < m_sourceItems.size(); ++i) {
        if (m_sourceItems[i].type == slotConfig.type && 
            m_sourceItems[i].source == slotConfig.source) {
            m_sourceSelector->setCurrentIndex(i);
            break;
        }
    }
    m_sourceSelector->blockSignals(false);
}

CameraSlot::~CameraSlot() {
    stopStream();
    cleanupCapture();
}

void CameraSlot::setupUi() {
    setMinimumSize(240, 180);
    setObjectName("cameraSlot");
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);
    
    // Video display area (optimized VideoWidget)
    m_videoWidget = new VideoWidget(this);
    m_videoWidget->setObjectName("videoDisplay");
    m_videoWidget->setMinimumSize(200, 150);
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Slot number overlay (will be painted)
    m_slotNumberLabel = new QLabel(QString::number(m_slotIndex), m_videoWidget);
    m_slotNumberLabel->setObjectName("slotNumber");
    m_slotNumberLabel->setAlignment(Qt::AlignCenter);
    m_slotNumberLabel->setFixedSize(32, 24);
    m_slotNumberLabel->move(8, 8);
    m_slotNumberLabel->setStyleSheet(
        "background-color: rgba(0, 0, 0, 0.7); "
        "color: white; "
        "border-radius: 4px; "
        "font-weight: bold; "
        "font-size: 14px;"
    );
    
    // Debug label (top-right, shows buffer size)
    m_debugLabel = new QLabel("", m_videoWidget);
    m_debugLabel->setObjectName("debugLabel");
    m_debugLabel->setAlignment(Qt::AlignCenter);
    m_debugLabel->setStyleSheet(
        "background-color: rgba(255, 165, 0, 0.85); "
        "color: black; "
        "border-radius: 4px; "
        "font-weight: bold; "
        "font-size: 12px; "
        "padding: 2px 6px;"
    );
    m_debugLabel->setVisible(m_debugMode);
    
    // Status label (centered overlay for Connecting, Buffering, etc.)
    m_statusLabel = new QLabel("No Signal", m_videoWidget);
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setMinimumWidth(150);
    m_statusLabel->setStyleSheet(
        "color: #aaaaaa; "
        "font-size: 16px; "
        "font-weight: bold; "
        "background-color: rgba(0, 0, 0, 0.6); "
        "border-radius: 8px; "
        "padding: 10px 20px;"
    );
    
    mainLayout->addWidget(m_videoWidget, 1);
    
    // Source selector
    m_sourceSelector = new QComboBox(this);
    m_sourceSelector->setObjectName("sourceSelector");
    m_sourceSelector->setCursor(Qt::PointingHandCursor);
    connect(m_sourceSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraSlot::onSourceSelectorChanged);
    
    mainLayout->addWidget(m_sourceSelector);
}

void CameraSlot::setupCapture() {
    const auto& config = Config::instance();
    
    // Create frame buffer
    m_buffer = new FrameBuffer(
        config.buffer().frameCount,
        config.buffer().minMaintenance,
        this
    );
    
    connect(m_buffer, &FrameBuffer::healthChanged,
            this, &CameraSlot::onBufferHealthChanged);
    
    // Create video recorder
    m_recorder = new VideoRecorder(m_slotIndex, this);
    
    // Create capture threads
    m_deviceCapture = new DeviceCapture(m_slotIndex, this);
    m_deviceCapture->setFrameBuffer(m_buffer);
    m_deviceCapture->setVideoRecorder(m_recorder);
    
    m_rtspCapture = new RtspCapture(m_slotIndex, this);
    m_rtspCapture->setFrameBuffer(m_buffer);
    m_rtspCapture->setVideoRecorder(m_recorder);
    
    // Connect signals
    connect(m_deviceCapture, &DeviceCapture::frameReady,
            this, &CameraSlot::onFrameReady);
    connect(m_deviceCapture, &DeviceCapture::connectionEstablished,
            this, &CameraSlot::onConnectionEstablished);
    connect(m_deviceCapture, &DeviceCapture::connectionLost,
            this, &CameraSlot::onConnectionLost);
    
    connect(m_rtspCapture, &RtspCapture::frameReady,
            this, &CameraSlot::onFrameReady);
    connect(m_rtspCapture, &RtspCapture::connectionEstablished,
            this, &CameraSlot::onConnectionEstablished);
    connect(m_rtspCapture, &RtspCapture::connectionLost,
            this, &CameraSlot::onConnectionLost);
}

void CameraSlot::cleanupCapture() {
    if (m_deviceCapture) {
        m_deviceCapture->stopCapture();
        delete m_deviceCapture;
        m_deviceCapture = nullptr;
    }
    
    if (m_rtspCapture) {
        m_rtspCapture->stopCapture();
        delete m_rtspCapture;
        m_rtspCapture = nullptr;
    }
    
    if (m_buffer) {
        m_buffer->stop();
        delete m_buffer;
        m_buffer = nullptr;
    }
    
    if (m_recorder) {
        m_recorder->stopRecording();
        delete m_recorder;
        m_recorder = nullptr;
    }
}

void CameraSlot::updateSourceSelector() {
    m_sourceSelector->blockSignals(true);
    m_sourceSelector->clear();
    m_sourceItems.clear();
    
    // None option
    m_sourceItems.append({SourceType::None, "", "None"});
    m_sourceSelector->addItem("None");
    
    // Auto option (slot index = device index)
    m_sourceItems.append({SourceType::Auto, QString::number(m_slotIndex), 
                         QString("Auto (Device %1)").arg(m_slotIndex)});
    m_sourceSelector->addItem(QString("Auto (Device %1)").arg(m_slotIndex));
    
    // Wired devices
    if (m_deviceDetector) {
        const auto& devices = m_deviceDetector->lastKnownDevices();
        for (const auto& device : devices) {
            QString displayText = QString("Wired %1: %2").arg(device.index).arg(device.name);
            m_sourceItems.append({SourceType::Wired, QString::number(device.index), displayText});
            m_sourceSelector->addItem(displayText);
        }
    }
    
    // Add some wired options even if not detected (for manual selection)
    for (int i = 0; i < 8; ++i) {
        bool exists = false;
        for (const auto& item : m_sourceItems) {
            if (item.type == SourceType::Wired && item.source == QString::number(i)) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            QString displayText = QString("Wired %1").arg(i);
            m_sourceItems.append({SourceType::Wired, QString::number(i), displayText});
            m_sourceSelector->addItem(displayText);
        }
    }
    
    // RTSP option
    m_sourceItems.append({SourceType::Rtsp, "", "RTSP Stream..."});
    m_sourceSelector->addItem("RTSP Stream...");
    
    m_sourceSelector->blockSignals(false);
}

void CameraSlot::refreshDeviceList() {
    int currentIndex = m_sourceSelector->currentIndex();
    SourceItem currentItem;
    if (currentIndex >= 0 && currentIndex < m_sourceItems.size()) {
        currentItem = m_sourceItems[currentIndex];
    }
    
    updateSourceSelector();
    
    // Try to restore selection
    for (int i = 0; i < m_sourceItems.size(); ++i) {
        if (m_sourceItems[i].type == currentItem.type && 
            m_sourceItems[i].source == currentItem.source) {
            m_sourceSelector->setCurrentIndex(i);
            break;
        }
    }
}

void CameraSlot::updateBufferSettings() {
    if (!m_buffer) return;
    
    const auto& config = Config::instance();
    m_buffer->setMaxSize(config.buffer().frameCount);
    m_buffer->setMinMaintenance(config.buffer().minMaintenance);
    
    // Update display timer interval
    int displayFps = config.buffer().displayFps;
    m_displayTimer->setInterval(1000 / displayFps);
    
    qDebug() << "CameraSlot" << m_slotIndex << "buffer updated:"
             << "maxSize=" << config.buffer().frameCount
             << "minMaintenance=" << config.buffer().minMaintenance
             << "displayFps=" << displayFps;
}

void CameraSlot::onSourceSelectorChanged(int index) {
    if (index < 0 || index >= m_sourceItems.size()) {
        return;
    }
    
    const auto& item = m_sourceItems[index];
    
    // Handle RTSP selection (show dialog)
    if (item.type == SourceType::Rtsp && item.source.isEmpty()) {
        showRtspInputDialog();
        return;
    }
    
    // Save to config
    SlotConfig slotConfig;
    slotConfig.type = item.type;
    slotConfig.source = item.source;
    Config::instance().setSlot(m_slotIndex, slotConfig);
    
    // Stop current stream if running
    if (m_streaming) {
        stopStream();
    }
    
    // Start new stream if source is valid (not None)
    if (item.type != SourceType::None) {
        startStream();
    }
    
    emit sourceChanged(m_slotIndex, item.type, item.source);
}

void CameraSlot::showRtspInputDialog() {
    RtspInputDialog dialog(this);
    
    // Pre-fill with existing URL if any
    const auto& slotConfig = Config::instance().slot(m_slotIndex);
    if (slotConfig.type == SourceType::Rtsp && !slotConfig.source.isEmpty()) {
        dialog.setUrl(slotConfig.source);
    }
    
    if (dialog.exec() == QDialog::Accepted) {
        QString url = dialog.url();
        if (!url.isEmpty()) {
            // Block signals to prevent triggering onSourceSelectorChanged (would show dialog again)
            m_sourceSelector->blockSignals(true);
            
            // Add or update RTSP item
            bool found = false;
            for (int i = 0; i < m_sourceItems.size(); ++i) {
                if (m_sourceItems[i].type == SourceType::Rtsp && !m_sourceItems[i].source.isEmpty()) {
                    m_sourceItems[i].source = url;
                    m_sourceSelector->setItemText(i, QString("RTSP: %1").arg(url));
                    m_sourceSelector->setCurrentIndex(i);
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                // Insert before the "RTSP Stream..." option
                int insertIndex = m_sourceItems.size() - 1;
                m_sourceItems.insert(insertIndex, {SourceType::Rtsp, url, QString("RTSP: %1").arg(url)});
                m_sourceSelector->insertItem(insertIndex, QString("RTSP: %1").arg(url));
                m_sourceSelector->setCurrentIndex(insertIndex);
            }
            
            m_sourceSelector->blockSignals(false);
            
            // Save and apply
            SlotConfig slotConfig;
            slotConfig.type = SourceType::Rtsp;
            slotConfig.source = url;
            Config::instance().setSlot(m_slotIndex, slotConfig);
            
            // Stop current stream if running, then start RTSP
            if (m_streaming) {
                stopStream();
            }
            startStream();  // Always start after RTSP URL is entered
        }
    } else {
        // User cancelled, revert to previous selection
        const auto& slotConfig = Config::instance().slot(m_slotIndex);
        for (int i = 0; i < m_sourceItems.size(); ++i) {
            if (m_sourceItems[i].type == slotConfig.type && 
                m_sourceItems[i].source == slotConfig.source) {
                m_sourceSelector->blockSignals(true);
                m_sourceSelector->setCurrentIndex(i);
                m_sourceSelector->blockSignals(false);
                break;
            }
        }
    }
}

void CameraSlot::startStream() {
    if (m_streaming) {
        return;
    }
    
    const auto& slotConfig = Config::instance().slot(m_slotIndex);
    
    if (slotConfig.type == SourceType::None) {
        updateStatusLabel("No Signal", true);
        return;
    }
    
    m_streaming = true;
    m_buffer->reset();
    
    // Initialize FPS tracking
    m_inputFrameCount = 0;
    m_inputFps = 0.0;
    m_fpsTimer.start();
    
    // Note: Recording will start when connection is established (see onConnectionEstablished)
    
    // Start appropriate capture
    if (slotConfig.type == SourceType::Rtsp) {
        m_rtspCapture->setRtspUrl(slotConfig.source);
        m_activeCapture = m_rtspCapture;
        m_rtspCapture->start();
    } else {
        // Auto or Wired
        int deviceIndex = slotConfig.source.toInt();
        m_deviceCapture->setDeviceIndex(deviceIndex);
        m_activeCapture = m_deviceCapture;
        m_deviceCapture->start();
    }
    
    m_displayTimer->start();
    updateStatusLabel("Connecting...", true);
    
    qDebug() << "CameraSlot" << m_slotIndex << "started streaming";
}

void CameraSlot::stopStream() {
    if (!m_streaming) {
        return;
    }
    
    m_streaming = false;
    m_displayTimer->stop();
    
    // Stop capture
    if (m_deviceCapture->isRunning()) {
        m_deviceCapture->stopCapture();
    }
    if (m_rtspCapture->isRunning()) {
        m_rtspCapture->stopCapture();
    }
    
    m_activeCapture = nullptr;
    
    // Stop recording
    m_recorder->stopRecording();
    
    // Clear buffer
    m_buffer->clear();
    
    m_currentFrame = QImage();
    m_videoWidget->clear();
    updateStatusLabel("No Signal", true);
    m_connected = false;
    m_bufferHealthy = false;
    
    qDebug() << "CameraSlot" << m_slotIndex << "stopped streaming";
}

void CameraSlot::onFrameReady(const QImage& frame) {
    Q_UNUSED(frame);
    
    // Always count frames (camera is feeding regardless of buffer state)
    m_inputFrameCount++;
    
    // Update FPS calculation every second (rolling 1-second average)
    qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed >= 1000) {
        m_inputFps = (m_inputFrameCount * 1000.0) / elapsed;
        m_inputFrameCount = 0;
        m_fpsTimer.restart();
        
        // Adapt display timer to match input FPS (only when we have valid FPS)
        if (m_inputFps > 1.0 && m_bufferHealthy) {
            int interval = static_cast<int>(1000.0 / m_inputFps);
            m_displayTimer->setInterval(qBound(16, interval, 200));  // Clamp ~5-60fps range
        }
    }
}

void CameraSlot::onConnectionEstablished() {
    m_connected = true;
    
    // Start recording now that we have a valid connection
    const auto& recordConfig = Config::instance().recording();
    if (recordConfig.enabled && !m_recorder->isRecording()) {
        m_recorder->startRecording(
            recordConfig.outputDirectory,
            recordConfig.fps,
            recordConfig.codec,
            recordConfig.chunkDurationSeconds
        );
    }
    
    // Clear status - no buffering sign needed
    updateStatusLabel("", false);
    qDebug() << "CameraSlot" << m_slotIndex << "connected";
}

void CameraSlot::onConnectionLost() {
    m_connected = false;
    updateStatusLabel("Disconnected", true);
    qDebug() << "CameraSlot" << m_slotIndex << "disconnected";
}

void CameraSlot::onBufferHealthChanged(bool isHealthy) {
    m_bufferHealthy = isHealthy;
    
    // Don't reset FPS - it tracks camera input rate regardless of buffer state
    // No buffering sign - just update internal state
}

void CameraSlot::updateDisplay() {
    if (!m_streaming || !m_buffer) {
        return;
    }
    
    // Update debug label if in debug mode
    if (m_debugMode) {
        updateDebugLabel();
    }
    
    // Only display if buffer is healthy
    if (!m_bufferHealthy) {
        return;
    }
    
    QImage frame = m_buffer->tryPop();
    if (!frame.isNull()) {
        m_currentFrame = frame;
        
        // Display using optimized VideoWidget
        // (handles scaling internally, only on resize)
        m_videoWidget->displayFrame(m_currentFrame);
        emit frameUpdated(m_currentFrame);
    }
}

void CameraSlot::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
}

void CameraSlot::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked(m_slotIndex);
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CameraSlot::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    
    // Re-center status label
    if (m_statusLabel && m_videoWidget && m_statusLabel->isVisible()) {
        updateStatusLabel(m_statusLabel->text(), true);
    }
}

void CameraSlot::updateStatusLabel(const QString& text, bool show) {
    if (!m_statusLabel || !m_videoWidget) return;
    
    m_statusLabel->setText(text);
    
    if (show) {
        // Adjust size to fit text
        m_statusLabel->adjustSize();
        
        QSize videoSize = m_videoWidget->size();
        QSize statusSize = m_statusLabel->size();
        
        // Ensure minimum dimensions
        int width = qMax(statusSize.width() + 20, 150);  // Add padding
        int height = qMax(statusSize.height() + 10, 44);
        
        // Center in video display
        int x = (videoSize.width() - width) / 2;
        int y = (videoSize.height() - height) / 2;
        
        m_statusLabel->setGeometry(x, y, width, height);
        m_statusLabel->show();
    } else {
        m_statusLabel->hide();
    }
}

void CameraSlot::updateDebugLabel() {
    if (!m_debugLabel || !m_videoWidget || !m_buffer) return;
    
    int bufferSize = m_buffer->size();
    int maxSize = m_buffer->maxSize();
    
    // Show buffer size and input FPS
    m_debugLabel->setText(QString("Buf: %1/%2 | In: %3fps")
        .arg(bufferSize)
        .arg(maxSize)
        .arg(m_inputFps, 0, 'f', 1));
    m_debugLabel->adjustSize();
    
    // Position in top-right corner
    QSize videoSize = m_videoWidget->size();
    int x = videoSize.width() - m_debugLabel->width() - 8;
    int y = 8;
    
    m_debugLabel->move(x, y);
    m_debugLabel->show();
}

} // namespace MCM

