#include "VideoWidget.h"
#include <QPainter>
#include <QResizeEvent>
#include <opencv2/imgproc.hpp>

namespace MCM {

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
{
    // Enable
    setAttribute(Qt::WA_OpaquePaintEvent);  // No background erase needed
    setMinimumSize(160, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

VideoWidget::~VideoWidget() = default;

void VideoWidget::displayFrame(const cv::Mat& frame) {
    if (frame.empty()) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // Reuse or resize conversion buffer
    int w = frame.cols;
    int h = frame.rows;
    
    if (m_conversionBuffer.isNull() || 
        m_conversionBuffer.width() != w || 
        m_conversionBuffer.height() != h) {
        m_conversionBuffer = QImage(w, h, QImage::Format_RGB888);
    }
    
    // Convert BGR to RGB directly into our buffer (no allocation)
    if (frame.type() == CV_8UC3) {
        // BGR -> RGB conversion
        cv::Mat rgb(h, w, CV_8UC3, m_conversionBuffer.bits(), m_conversionBuffer.bytesPerLine());
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    } else if (frame.type() == CV_8UC1) {
        // Grayscale -> RGB
        cv::Mat rgb(h, w, CV_8UC3, m_conversionBuffer.bits(), m_conversionBuffer.bytesPerLine());
        cv::cvtColor(frame, rgb, cv::COLOR_GRAY2RGB);
    } else {
        return;  // Unsupported format
    }
    
    // Store current frame
    m_currentFrame = m_conversionBuffer;  // Shallow copy (shares data)
    m_needsRescale = true;
    
    // Trigger repaint
    update();
}

void VideoWidget::displayFrame(const QImage& frame) {
    if (frame.isNull()) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // Convert to RGB888 if needed (for consistent handling)
    if (frame.format() != QImage::Format_RGB888) {
        m_currentFrame = frame.convertToFormat(QImage::Format_RGB888);
    } else {
        m_currentFrame = frame;
    }
    
    m_needsRescale = true;
    
    // Trigger repaint
    update();
}

void VideoWidget::clear() {
    QMutexLocker locker(&m_mutex);
    m_currentFrame = QImage();
    m_scaledFrame = QImage();
    m_displayImage = QImage();
    m_needsRescale = true;
    update();
}

void VideoWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // Fill background
    painter.fillRect(rect(), QColor(26, 26, 46));  // Dark background
    
    QMutexLocker locker(&m_mutex);
    
    if (m_currentFrame.isNull()) {
        return;
    }
    
    // Rescale only if needed
    if (m_needsRescale || m_lastSize != size()) {
        updateScaledImage();
        m_lastSize = size();
        m_needsRescale = false;
    }
    
    if (!m_displayImage.isNull()) {
        // Center the image
        int x = (width() - m_displayImage.width()) / 2;
        int y = (height() - m_displayImage.height()) / 2;
        
        // Draw directly (no QPixmap conversion)
        painter.drawImage(x, y, m_displayImage);
    }
}

void VideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    m_needsRescale = true;
}

void VideoWidget::updateScaledImage() {
    if (m_currentFrame.isNull()) {
        return;
    }
    
    QSize targetSize = size();
    QSize frameSize = m_currentFrame.size();
    
    // Calculate scaled size maintaining aspect ratio
    QSize scaledSize = frameSize.scaled(targetSize, Qt::KeepAspectRatio);
    
    // Only rescale if size actually changed
    if (m_scaledFrame.size() != scaledSize) {
        m_scaledFrame = m_currentFrame.scaled(
            scaledSize,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation  // Keep quality
        );
    }
    
    m_displayImage = m_scaledFrame;
}

} // namespace MCM

