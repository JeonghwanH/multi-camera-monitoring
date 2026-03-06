#include "CameraSlot.h"
#include "OptimizedVideoWidget.h"
#include "RtspInputDialog.h"
#include "capture/QtCameraCapture.h"
#include "capture/QtRtspCapture.h"
#include "core/QtVideoRecorder.h"
#include "utils/DeviceDetector.h"

#include <QCameraDevice>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
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
            // Also update m_currentSourceType since signals are blocked
            m_currentSourceType = slotConfig.type;
            m_currentSource = slotConfig.source;
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
    m_statusLabel->hide();  // Hide initially, show after layout is complete
    
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
    
    // Create hardware-accelerated video recorder
    m_qtRecorder = new QtVideoRecorder(m_slotIndex, this);
    
    // Connect recorder signals
    connect(m_qtRecorder, &QtVideoRecorder::chunkStarted,
            this, [this](int chunk, const QString& filename) {
                qDebug() << "CameraSlot" << m_slotIndex << "recording chunk" << chunk << "started:" << filename;
            });
    connect(m_qtRecorder, &QtVideoRecorder::chunkCompleted,
            this, [this](int chunk, const QString& filename) {
                qDebug() << "CameraSlot" << m_slotIndex << "recording chunk" << chunk << "completed:" << filename;
            });
    connect(m_qtRecorder, &QtVideoRecorder::errorOccurred,
            this, [this](const QString& error) {
                qWarning() << "CameraSlot" << m_slotIndex << "recording error:" << error;
            });
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
    
    if (m_qtRecorder) {
        m_qtRecorder->stopRecording();
        delete m_qtRecorder;
        m_qtRecorder = nullptr;
    }
}

bool CameraSlot::hasSourceSelected() const {
    // Check the saved config, not runtime state
    // This ensures "Play All" works even after stopStream() was called
    const auto& slotConfig = Config::instance().slot(m_slotIndex);
    return slotConfig.type != SourceType::None;
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
    
    // Wired devices (from DeviceDetector - filtered to only include capture devices)
    // On Linux, this excludes metadata nodes and provides sequential numbering
    QList<DeviceInfo> devices = m_deviceDetector->lastKnownDevices();
    for (const auto& device : devices) {
        QString displayText = QString("Wired %1: %2").arg(device.index).arg(device.name);
        m_sourceItems.append({SourceType::Wired, QString::number(device.index), displayText});
        m_sourceSelector->addItem(displayText);
    }
    
    // Add some wired options even if not detected (for manual selection)
    int startIndex = devices.isEmpty() ? 0 : devices.last().index + 1;
    for (int i = startIndex; i < 8; ++i) {
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
    
    // Try to restore selection WITHOUT triggering stream start
    // Block signals to prevent onSourceSelectorChanged from being called
    m_sourceSelector->blockSignals(true);
    for (int i = 0; i < m_sourceItems.size(); ++i) {
        if (m_sourceItems[i].type == currentItem.type && 
            m_sourceItems[i].source == currentItem.source) {
            m_sourceSelector->setCurrentIndex(i);
            // Also update m_currentSourceType since signals are blocked
            m_currentSourceType = currentItem.type;
            m_currentSource = currentItem.source;
            break;
        }
    }
    m_sourceSelector->blockSignals(false);
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
    
    // Check if we were streaming before any changes
    bool wasStreaming = m_streaming;
    qDebug() << "  Current m_streaming:" << m_streaming << "wasStreaming:" << wasStreaming;
    
    // Stop current stream if running (BEFORE updating m_currentSourceType!)
    // stopStream() uses m_currentSourceType to know which capture to stop
    if (m_streaming) {
        qDebug() << "  >>> STOPPING current stream (was type:" << static_cast<int>(m_currentSourceType) << ") <<<";
        stopStream();  // Uses OLD m_currentSourceType to stop correct capture
    }
    
    // NOW update current source type (after stopping)
    m_currentSourceType = item.type;
    m_currentSource = item.source;
    
    // Handle new source
    if (item.type == SourceType::None) {
        // User selected None - just show No Signal, don't restart
        qDebug() << "  Source is None, showing No Signal";
        updateStatusLabel("No Signal", true);
        m_videoWidget->clear();  // Ensure display is cleared
    } else if (wasStreaming) {
        // Was streaming before, restart with new source
        qDebug() << "  >>> RESTARTING with new source <<<";
        startStream();
    } else {
        // Not streaming - just update UI, don't auto-start
        // User needs to click "Play All" button to start
        qDebug() << "  Source selected (not auto-starting, use Play All button)";
        updateStatusLabel("Ready", true);
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
    QElapsedTimer timer;
    timer.start();
    
    qDebug() << "########## CameraSlot" << m_slotIndex << "startStream() ##########";
    qDebug() << "  m_streaming:" << m_streaming;
    qDebug() << "  Thread:" << QThread::currentThread();
    
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
    
    // DON'T reset video item - reuse the same one like test_qt_only does
    // Resetting breaks the GStreamer pipeline on Linux USB capture cards
    // Just clear the display and show connecting status
    m_videoWidget->clear();
    updateStatusLabel("Connecting...", true);
    
    // Get the NEW video item for pipeline
    QGraphicsVideoItem* videoItem = m_videoWidget->videoItem();
    qDebug() << "  [" << timer.elapsed() << "ms] VideoWidget videoItem:" << videoItem;
    qDebug() << "  VideoItem size:" << (videoItem ? videoItem->size() : QSizeF());
    qDebug() << "  VideoItem nativeSize:" << (videoItem ? videoItem->nativeSize() : QSizeF());
    qDebug() << "  VideoItem visible:" << (videoItem ? videoItem->isVisible() : false);
    qDebug() << "  VideoItem videoSink:" << (videoItem ? videoItem->videoSink() : nullptr);
    
    // Start appropriate capture with direct GPU pipeline
    if (slotConfig.type == SourceType::Rtsp) {
        // RTSP stream via QMediaPlayer
        // IMPORTANT: For QMediaPlayer, video output should be set BEFORE source
        qDebug() << "  >>> Starting RTSP pipeline <<<";
        m_rtspCapture->setVideoOutput(videoItem);  // Set video output FIRST
        m_rtspCapture->setRtspUrl(slotConfig.source);  // Then set source
        m_rtspCapture->start();
        qDebug() << "  RTSP stream started:" << slotConfig.source;
    } else {
        // Wired camera via QCamera + QMediaCaptureSession
        int deviceIndex = slotConfig.source.toInt();
        qDebug() << "  >>> Starting Camera pipeline <<<";
        qDebug() << "  Device index (filtered):" << deviceIndex;
        
        // Use DeviceDetector to get the actual QCameraDevice by filtered index
        // This handles Linux V4L2 metadata node filtering and sequential numbering
        qDebug() << "  [" << timer.elapsed() << "ms] Getting camera device from DeviceDetector...";
        QCameraDevice cameraDevice = m_deviceDetector->cameraDeviceByIndex(deviceIndex);
        qDebug() << "  [" << timer.elapsed() << "ms] Got camera device";
        
        if (cameraDevice.isNull()) {
            qDebug() << "  Device index" << deviceIndex << "not available";
            qDebug() << "  Available devices:" << m_deviceDetector->deviceCount();
            qDebug() << "  Showing No Signal instead of Connecting";
            m_streaming = false;  // Not actually streaming
            updateStatusLabel("No Signal", true);
            return;
        }
        
        qDebug() << "  Resolved to device:" << cameraDevice.description() << "id:" << cameraDevice.id();
        
        // Match test_qt_only approach: setCameraDevice -> setVideoOutput -> start()
        // Keep it simple and immediate like the working test code
        qDebug() << "  [" << timer.elapsed() << "ms] Calling setCameraDevice...";
        m_cameraCapture->setCameraDevice(cameraDevice);
        qDebug() << "  [" << timer.elapsed() << "ms] Calling setVideoOutput...";
        m_cameraCapture->setVideoOutput(videoItem);
        qDebug() << "  [" << timer.elapsed() << "ms] Calling start...";
        m_cameraCapture->start();
        qDebug() << "  [" << timer.elapsed() << "ms] Camera started";
    }
    
    qDebug() << "########## CameraSlot" << m_slotIndex << "startStream() DONE - total:" << timer.elapsed() << "ms ##########";
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
    if (m_qtRecorder && m_qtRecorder->isRecording()) {
        qDebug() << "  Stopping recorder...";
        m_qtRecorder->stopRecording();
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
    
    // Delay recording start to let video surface fully initialize
    // This prevents "Failed to start video surface due to main thread blocked"
    const auto& recordingConfig = Config::instance().recording();
    if (recordingConfig.enabled && m_currentSourceType != SourceType::Rtsp) {
        if (m_cameraCapture && m_cameraCapture->captureSession()) {
            // Capture config values by copy for safe use in lambda
            QString outputDir = recordingConfig.outputDirectory;
            int chunkDuration = recordingConfig.chunkDurationSeconds;
            
            qDebug() << "  Scheduling recording start (200ms delay)...";
            QTimer::singleShot(200, this, [this, outputDir, chunkDuration]() {
                if (m_connected && m_cameraCapture && m_cameraCapture->captureSession()) {
                    qDebug() << "  Starting hardware-accelerated recording for slot" << m_slotIndex;
                    m_qtRecorder->setSession(m_cameraCapture->captureSession());
                    m_qtRecorder->startRecording(outputDir, chunkDuration);
                }
            });
        }
    }
    
    qDebug() << "  Status label hidden, connection complete";
}

void CameraSlot::onConnectionLost() {
    qDebug() << "*** CameraSlot" << m_slotIndex << "onConnectionLost() ***";
    m_connected = false;
    
    // Clear the video display so last frame doesn't remain visible
    m_videoWidget->clear();
    updateStatusLabel("No Signal", true);
    
    // Stop recording
    if (m_qtRecorder && m_qtRecorder->isRecording()) {
        m_qtRecorder->stopRecording();
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

void CameraSlot::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    
    // Show and center the "No Signal" label now that the widget has proper size
    if (m_statusLabel && !m_streaming) {
        updateStatusLabel("No Signal", true);
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
