#ifndef EXPANDEDVIEW_H
#define EXPANDEDVIEW_H

#include <QMainWindow>
#include <QLabel>
#include <QImage>

namespace MCM {

/**
 * @brief Expanded view window for a single camera
 * 
 * Shows a larger view of a camera slot with:
 * - Frame fitted to window while maintaining aspect ratio
 * - ESC key to close
 * - Slot number in title
 */
class ExpandedView : public QMainWindow {
    Q_OBJECT

public:
    explicit ExpandedView(int slotIndex, QWidget* parent = nullptr);

    /**
     * @brief Update the displayed frame
     */
    void updateFrame(const QImage& frame);

    /**
     * @brief Get the slot index
     */
    int slotIndex() const { return m_slotIndex; }

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void fitFrameToWindow();

    int m_slotIndex;
    QLabel* m_videoLabel;
    QImage m_currentFrame;
};

} // namespace MCM

#endif // EXPANDEDVIEW_H

