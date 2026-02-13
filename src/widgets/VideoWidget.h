#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include <QImage>
#include <QMutex>
#include <opencv2/core.hpp>

namespace MCM {

/**
 * @brief High-performance video display widget
 * 
 * Optimizations:
 * - Direct QPainter rendering (no QLabel/QPixmap overhead)
 * - BGR to RGB conversion only when needed
 * - Scaling only on resize (cached)
 * - Reusable frame buffer
 */
class VideoWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget() override;

    /**
     * @brief Display a frame (BGR format from OpenCV)
     * @param frame BGR cv::Mat from camera
     */
    void displayFrame(const cv::Mat& frame);

    /**
     * @brief Display a frame (QImage format)
     * @param frame QImage to display
     */
    void displayFrame(const QImage& frame);

    /**
     * @brief Clear the display
     */
    void clear();

    /**
     * @brief Check if widget has a valid frame
     */
    bool hasFrame() const { return !m_displayImage.isNull(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateScaledImage();

    // Current frame (original resolution)
    QImage m_currentFrame;
    
    // Scaled frame for display (cached)
    QImage m_scaledFrame;
    
    // Display image (what gets painted)
    QImage m_displayImage;
    
    // Reusable buffer for BGR->RGB conversion
    QImage m_conversionBuffer;
    
    // Last known size (for resize detection)
    QSize m_lastSize;
    
    // Flag to indicate frame needs rescaling
    bool m_needsRescale{true};
    
    // Thread safety
    mutable QMutex m_mutex;
};

} // namespace MCM

#endif // VIDEOWIDGET_H

