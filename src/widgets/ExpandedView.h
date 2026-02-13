#ifndef EXPANDEDVIEW_H
#define EXPANDEDVIEW_H

#include <QMainWindow>
#include <QVideoFrame>

namespace MCM {

class OptimizedVideoWidget;

/**
 * @brief Expanded view window for a single camera (GPU-accelerated)
 * 
 * Shows a larger view of a camera slot with:
 * - GPU-accelerated video display
 * - Frame fitted to window while maintaining aspect ratio
 * - ESC key to close, F key for fullscreen
 * - Slot number in title
 */
class ExpandedView : public QMainWindow {
    Q_OBJECT

public:
    explicit ExpandedView(int slotIndex, QWidget* parent = nullptr);

    /**
     * @brief Update the displayed frame (QVideoFrame version for GPU pipeline)
     */
    void updateFrame(const QVideoFrame& frame);

    /**
     * @brief Get the slot index
     */
    int slotIndex() const { return m_slotIndex; }
    
    /**
     * @brief Get the video widget for direct connection
     */
    OptimizedVideoWidget* videoWidget() const { return m_videoWidget; }

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    int m_slotIndex;
    OptimizedVideoWidget* m_videoWidget;
};

} // namespace MCM

#endif // EXPANDEDVIEW_H
