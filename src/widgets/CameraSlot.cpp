#include "CameraSlot.h"
#include "OptimizedVideoWidget.h"
#include "RtspInputDialog.h"
#include "capture/QtCameraCapture.h"
#include "capture/QtRtspCapture.h"
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
    , m_debugMode(std::getenv("MCM_DEBUG") != nullptr)
{
    setupUi();
    setupCapture();
    
    // Start FPS tracking timer
    m_fpsTimer.start();
    
    // Debug timer for FPS updates
    if (m_debugMode) {
        m_debugTimer = new QTimer(this);
        connect(m_debugTimer, &QTimer::timeout, this, &CameraSlot::updateDebugLabel);
        m_debugTimer->start(500);  // Update debug info 2x per second
    }
    
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
    
    // GPU-accelerated video display (replaces VideoWidget)
    m_videoWidget = new OptimizedVideoWidget(this);
    m_videoWidget->setObjectName("videoDisplay");
    m_videoWidget->setMinimumSize(200, 150);
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Slot number overlay
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
    
    // Debug label (top-right, shows FPS)
    m_debugLabel = new QLabel("", m_videoWidget);
    m_debugLabel->setObjectName("debugLabel");
    m_debugLabel->setAlignment(Qt::AlignCenter);
    m_debugLabel->setStyleSheet(
        "background-color: rgba(0, 200, 100, 0.85); "
        "color: black; "
        "border-radius: 4px; "
        "font-weight: bold; "
        "font-size: 12px; "
        "padding: 2px 6px;"
    );
    m_debugLabel->setVisible(m_debugMode);
    
    // Status label (centered overlay)
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
    // Create Qt Multimedia capture objects
    // QMediaCaptureSession is created inside these and replaces FrameBuffer
    
    m_cameraCapture = new QtCameraCapture(m_slotIndex, this);
    m_rtspCapture = new QtRtspCapture(m_slotIndex, this);
    
    // NOTE: Video output is set in startStream() AFTER device is configured
    // This follows the test_camera pattern: setCamera -> setVideoOutput -> start
    
    // Connect camera capture signals
    connect(m_cameraCapture, &QtCameraCapture::connectionEstablished,
            this, &CameraSlot::onConnectionEstablished);
    connect(m_cameraCapture, &QtCameraCapture::connectionLost,
            this, &CameraSlot::onConnectionLost);
    connect(m_cameraCapture, &QtCameraCapture::frameReady,
            this, &CameraSlot::onFrameReady);
    connect(m_cameraCapture, &QtCameraCapture::errorOccurred,
            this, [this](const QString& error) {
                qWarning() << "CameraSlot" << m_slotIndex << "camera error:" << error;
            });
    
    // Connect RTSP capture signals
    connect(m_rtspCapture, &QtRtspCapture::connectionEstablished,
            this, &CameraSlot::onConnectionEstablished);
    connect(m_rtspCapture, &QtRtspCapture::connectionLost,
            this, &CameraSlot::onConnectionLost);
    connect(m_rtspCapture, &QtRtspCapture::frameReady,
            this, &CameraSlot::onFrameReady);
    connect(m_rtspCapture, &QtRtspCapture::errorOccurred,
            this, [this](const QString& error) {
                qWarning() << "CameraSlot" << m_slotIndex << "RTSP error:" << error;
            });
    
    // Create video recorder (will be migrated to QMediaRecorder in Step 4)
    m_recorder = new VideoRecorder(m_slotIndex, this);
}

void CameraSlot::cleanupCapture() {
    if (m_cameraCapture) {
        m_cameraCapture->stop();
        delete m_cameraCapture;
        m_cameraCapture = nullptr;
    }
    
    if (m_rtspCapture) {
        m_rtspCapture->stop();
        delete m_rtspCapture;
        m_rtspCapture = nullptr;
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
    
    // Wired devices (from Qt's QMediaDevices)
    auto devices = QtCameraCapture::availableDevices();
    for (int i = 0; i < devices.size(); ++i) {
        QString displayText = QString("Wired %1: %2").arg(i).arg(devices[i].description());
        m_sourceItems.append({SourceType::Wired, QString::number(i), displayText});
        m_sourceSelector->addItem(displayText);
    }
    
    // Add some wired options even if not detected (for manual selection)
    for (int i = devices.size(); i < 8; ++i) {
        QString displayText = QString("Wired %1").arg(i);
        m_sourceItems.append({SourceType::Wired, QString::number(i), displayText});
        m_sourceSelector->addItem(displayText);
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

void CameraSlot::onSourceSelectorChanged(int index) {
    qDebug() << "";
    qDebug() << "========== SOURCE CHANGE TRIGGERED ==========";
    qDebug() << "CameraSlot" << m_slotIndex << "source selector changed to index" << index;
    
    if (index < 0 || index >= m_sourceItems.size()) {
        qDebug() << "  ERROR: Invalid index" << index << "(max:" << m_sourceItems.size() << ")";
        return;
    }
    
    const auto& item = m_sourceItems[index];
    qDebug() << "  Selected: type=" << static_cast<int>(item.type) << "source=" << item.source << "display=" << item.displayText;
    
    // Handle RTSP selection (show dialog)
    if (item.type == SourceType::Rtsp && item.source.isEmpty()) {
        qDebug() << "  RTSP selected, showing dialog...";
        showRtspInputDialog();
        return;
    }
    
    // Save to config
    qDebug() << "  Saving to config...";
    SlotConfig slotConfig;
    slotConfig.type = item.type;
    slotConfig.source = item.source;
    Config::instance().setSlot(m_slotIndex, slotConfig);
    
    // Stop current stream if running
    qDebug() << "  Current m_streaming:" << m_streaming;
    if (m_streaming) {
        qDebug() << "  >>> STOPPING current stream <<<";
        stopStream();
    }
    
    // Start new stream if source is valid (not None)
    if (item.type != SourceType::None) {
        qDebug() << "  >>> STARTING new stream <<<";
        startStream();
    } else {
        qDebug() << "  Source is None, resetting video item for clean state";
        // Reset video item when setting to None to ensure clean state
        // This prevents stale QGraphicsVideoItem references
        m_videoWidget->resetVideoItem();
    }
    
    emit sourceChanged(m_slotIndex, item.type, item.source);
    qDebug() << "========== SOURCE CHANGE COMPLETE ==========";
    qDebug() << "";
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
            
            if (m_streaming) {
                stopStream();
            }
            startStream();
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
    qDebug() << "########## CameraSlot" << m_slotIndex << "startStream() ##########";
    qDebug() << "  m_streaming:" << m_streaming;
    
    if (m_streaming) {
        qDebug() << "  Already streaming, returning early";
        return;
    }
    
    const auto& slotConfig = Config::instance().slot(m_slotIndex);
    qDebug() << "  Config - type:" << static_cast<int>(slotConfig.type) << "source:" << slotConfig.source;
    
    if (slotConfig.type == SourceType::None) {
        updateStatusLabel("No Signal", true);
        qDebug() << "  Source is None, not starting";
        return;
    }
    
    m_streaming = true;
    m_currentSourceType = slotConfig.type;
    m_currentSource = slotConfig.source;
    
    // Reset FPS tracking
    m_frameCount = 0;
    m_currentFps = 0.0;
    m_fpsTimer.restart();
    
    // Reset video item to ensure clean state for new source
    // This creates a fresh QGraphicsVideoItem because reusing one across
    // different QMediaCaptureSession instances doesn't work properly
    qDebug() << "  Resetting video item for clean state...";
    m_videoWidget->resetVideoItem();
    updateStatusLabel("Connecting...", true);
    
    // Get the NEW video item for pipeline
    QGraphicsVideoItem* videoItem = m_videoWidget->videoItem();
    qDebug() << "  VideoWidget videoItem:" << videoItem;
    qDebug() << "  VideoItem size:" << (videoItem ? videoItem->size() : QSizeF());
    qDebug() << "  VideoItem nativeSize:" << (videoItem ? videoItem->nativeSize() : QSizeF());
    
    // Start appropriate capture with direct GPU pipeline
    // IMPORTANT: Set video output AFTER configuring the device (order matters for Qt)
    if (slotConfig.type == SourceType::Rtsp) {
        // RTSP stream via QMediaPlayer
        qDebug() << "  >>> Starting RTSP pipeline <<<";
        m_rtspCapture->setRtspUrl(slotConfig.source);
        m_rtspCapture->setVideoOutput(videoItem);  // Set AFTER source
        m_rtspCapture->start();
        qDebug() << "  RTSP stream started:" << slotConfig.source;
    } else {
        // Wired camera via QCamera + QMediaCaptureSession
        int deviceIndex = slotConfig.source.toInt();
        qDebug() << "  >>> Starting Camera pipeline <<<";
        qDebug() << "  Device index:" << deviceIndex;
        
        // Check if device exists before attempting to connect
        auto availableDevices = QtCameraCapture::availableDevices();
        if (deviceIndex < 0 || deviceIndex >= availableDevices.size()) {
            qDebug() << "  Device index" << deviceIndex << "not available (have" << availableDevices.size() << "devices)";
            qDebug() << "  Showing No Signal instead of Connecting";
            m_streaming = false;  // Not actually streaming
            updateStatusLabel("No Signal", true);
            return;
        }
        
        m_cameraCapture->setDeviceIndex(deviceIndex);  // Configure device first
        qDebug() << "  Device configured, now setting video output...";
        m_cameraCapture->setVideoOutput(videoItem);  // Set AFTER device
        qDebug() << "  Video output set, now calling start()...";
        m_cameraCapture->start();
        qDebug() << "  Camera start() called";
    }
    
    qDebug() << "########## CameraSlot" << m_slotIndex << "startStream() DONE ##########";
}

void CameraSlot::stopStream() {
    qDebug() << "########## CameraSlot" << m_slotIndex << "stopStream() ##########";
    qDebug() << "  m_streaming:" << m_streaming << "m_currentSourceType:" << static_cast<int>(m_currentSourceType);
    
    if (!m_streaming) {
        qDebug() << "  Not streaming, returning early";
        return;
    }
    
    m_streaming = false;
    
    // Stop the active capture based on current source type
    if (m_currentSourceType == SourceType::Rtsp) {
        qDebug() << "  Stopping RTSP capture...";
        if (m_rtspCapture && m_rtspCapture->isActive()) {
            m_rtspCapture->stop();
        }
    } else if (m_currentSourceType != SourceType::None) {
        qDebug() << "  Stopping camera capture...";
        qDebug() << "  m_cameraCapture:" << m_cameraCapture << "isActive:" << (m_cameraCapture ? m_cameraCapture->isActive() : false);
        if (m_cameraCapture && m_cameraCapture->isActive()) {
            m_cameraCapture->stop();
        }
    }
    
    // Stop recording
    if (m_recorder && m_recorder->isRecording()) {
        qDebug() << "  Stopping recorder...";
        m_recorder->stopRecording();
    }
    
    // Clear display
    qDebug() << "  Clearing video widget...";
    m_videoWidget->clear();
    updateStatusLabel("No Signal", true);
    m_connected = false;
    m_currentSourceType = SourceType::None;
    m_currentSource.clear();
    
    qDebug() << "########## CameraSlot" << m_slotIndex << "stopStream() DONE ##########";
}

void CameraSlot::onConnectionEstablished() {
    qDebug() << "*** CameraSlot" << m_slotIndex << "onConnectionEstablished() ***";
    qDebug() << "  VideoItem:" << m_videoWidget->videoItem();
    qDebug() << "  VideoItem nativeSize:" << m_videoWidget->videoItem()->nativeSize();
    
    m_connected = true;
    updateStatusLabel("", false);  // Hide status on successful connection
    
    // Start recording if enabled
    // Note: Recording with QVideoFrame will be migrated to QMediaRecorder in Step 4
    // For now, we keep VideoRecorder but it won't receive frames (recording disabled temporarily)
    
    qDebug() << "  Status label hidden, connection complete";
}

void CameraSlot::onConnectionLost() {
    qDebug() << "*** CameraSlot" << m_slotIndex << "onConnectionLost() ***";
    m_connected = false;
    
    // Clear the video display so last frame doesn't remain visible
    m_videoWidget->clear();
    updateStatusLabel("No Signal", true);
    
    // Stop recording
    if (m_recorder && m_recorder->isRecording()) {
        m_recorder->stopRecording();
    }
    
    qDebug() << "  Display cleared, showing No Signal";
}

void CameraSlot::onFrameReady(const QVideoFrame& frame) {
    // Count frames for FPS calculation
    m_frameCount++;
    
    // Calculate FPS every second
    qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed >= 1000) {
        m_currentFps = (m_frameCount * 1000.0) / elapsed;
        m_frameCount = 0;
        m_fpsTimer.restart();
    }
    
    // Emit frame for external use (e.g., expanded view, recording)
    emit frameUpdated(frame);
    
    // Note: Display is handled directly by QMediaCaptureSession -> QGraphicsVideoItem
    // No need to manually push frames to the widget (GPU pipeline)
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
    
    // Update debug label position
    if (m_debugMode) {
        updateDebugLabel();
    }
}

void CameraSlot::updateStatusLabel(const QString& text, bool show) {
    if (!m_statusLabel || !m_videoWidget) return;
    
    m_statusLabel->setText(text);
    
    if (show) {
        m_statusLabel->adjustSize();
        
        QSize videoSize = m_videoWidget->size();
        QSize statusSize = m_statusLabel->size();
        
        int width = qMax(statusSize.width() + 20, 150);
        int height = qMax(statusSize.height() + 10, 44);
        
        int x = (videoSize.width() - width) / 2;
        int y = (videoSize.height() - height) / 2;
        
        m_statusLabel->setGeometry(x, y, width, height);
        m_statusLabel->show();
        m_statusLabel->raise();  // Ensure it's on top
    } else {
        m_statusLabel->hide();
    }
}

void CameraSlot::updateDebugLabel() {
    if (!m_debugLabel || !m_videoWidget) return;
    
    // Show FPS (no buffer info - Qt handles buffering internally)
    QString mode = m_connected ? "GPU" : "---";
    m_debugLabel->setText(QString("%1 | %2 fps")
        .arg(mode)
        .arg(m_currentFps, 0, 'f', 1));
    m_debugLabel->adjustSize();
    
    // Position in top-right corner
    QSize videoSize = m_videoWidget->size();
    int x = videoSize.width() - m_debugLabel->width() - 8;
    int y = 8;
    
    m_debugLabel->move(x, y);
    m_debugLabel->show();
    m_debugLabel->raise();
}

} // namespace MCM
