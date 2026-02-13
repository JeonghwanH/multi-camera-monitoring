#ifndef CAMERASLOT_H
#define CAMERASLOT_H

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include "core/Config.h"

namespace MCM {

class CaptureThread;
class DeviceCapture;
class RtspCapture;
class FrameBuffer;
class VideoRecorder;
class DeviceDetector;
class VideoWidget;

/**
 * @brief Individual camera slot widget
 * 
 * Displays video from a camera source with:
 * - Slot number overlay
 * - Source type selector (None, Auto, Wired 0-7, RTSP)
 * - Status indicators (connected, buffering, disconnected)
 * - Double-click to expand
 */
class CameraSlot : public QWidget {
    Q_OBJECT

public:
    explicit CameraSlot(int slotIndex, DeviceDetector* detector, QWidget* parent = nullptr);
    ~CameraSlot();

    /**
     * @brief Get the slot index
     */
    int slotIndex() const { return m_slotIndex; }

    /**
     * @brief Start streaming from configured source
     */
    void startStream();

    /**
     * @brief Stop streaming
     */
    void stopStream();

    /**
     * @brief Check if currently streaming
     */
    bool isStreaming() const { return m_streaming; }

    /**
     * @brief Refresh the device list in the selector
     */
    void refreshDeviceList();

    /**
     * @brief Update buffer settings from config
     */
    void updateBufferSettings();

signals:
    /**
     * @brief Emitted when slot is double-clicked
     */
    void doubleClicked(int slotIndex);

    /**
     * @brief Emitted when a new frame is available
     */
    void frameUpdated(const QImage& frame);

    /**
     * @brief Emitted when source selection changes
     */
    void sourceChanged(int slotIndex, SourceType type, const QString& source);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSourceSelectorChanged(int index);
    void onFrameReady(const QImage& frame);
    void onConnectionEstablished();
    void onConnectionLost();
    void onBufferHealthChanged(bool isHealthy);
    void updateDisplay();

private:
    void setupUi();
    void setupCapture();
    void cleanupCapture();
    void updateSourceSelector();
    void applySourceSelection(SourceType type, const QString& source);
    void showRtspInputDialog();
    void updateStatusLabel(const QString& text, bool show = true);
    void updateDebugLabel();

    int m_slotIndex;
    DeviceDetector* m_deviceDetector;
    
    // UI components
    VideoWidget* m_videoWidget;
    QLabel* m_statusLabel;
    QLabel* m_slotNumberLabel;
    QLabel* m_debugLabel;
    QComboBox* m_sourceSelector;
    
    // Debug mode
    bool m_debugMode{false};
    
    // Input FPS tracking (for debug display)
    QElapsedTimer m_fpsTimer;
    int m_inputFrameCount{0};
    double m_inputFps{0.0};
    
    // Capture components
    DeviceCapture* m_deviceCapture{nullptr};
    RtspCapture* m_rtspCapture{nullptr};
    CaptureThread* m_activeCapture{nullptr};
    FrameBuffer* m_buffer{nullptr};
    VideoRecorder* m_recorder{nullptr};
    
    // State
    bool m_streaming{false};
    bool m_connected{false};
    bool m_bufferHealthy{false};
    QImage m_currentFrame;
    
    // Display update timer
    QTimer* m_displayTimer;
    
    // Source selector items data
    struct SourceItem {
        SourceType type;
        QString source;
        QString displayText;
    };
    QVector<SourceItem> m_sourceItems;
};

} // namespace MCM

#endif // CAMERASLOT_H

