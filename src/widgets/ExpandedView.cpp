#include "ExpandedView.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QResizeEvent>

namespace MCM {

ExpandedView::ExpandedView(int slotIndex, QWidget* parent)
    : QMainWindow(parent)
    , m_slotIndex(slotIndex)
{
    setWindowTitle(QString("Camera %1 - Expanded View").arg(slotIndex));
    setMinimumSize(640, 480);
    resize(1280, 720);
    
    // Set window flags for independent window
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | 
                   Qt::WindowMinMaxButtonsHint);
    
    // Central widget
    QWidget* centralWidget = new QWidget(this);
    centralWidget->setStyleSheet("background-color: #000;");
    setCentralWidget(centralWidget);
    
    QVBoxLayout* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    
    // Video label
    m_videoLabel = new QLabel(this);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setStyleSheet("background-color: #000;");
    m_videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    layout->addWidget(m_videoLabel);
    
    // Show "No Signal" initially
    m_videoLabel->setText("No Signal");
    m_videoLabel->setStyleSheet("background-color: #000; color: #666; font-size: 24px;");
}

void ExpandedView::updateFrame(const QImage& frame) {
    if (frame.isNull()) {
        return;
    }
    
    m_currentFrame = frame;
    fitFrameToWindow();
}

void ExpandedView::fitFrameToWindow() {
    if (m_currentFrame.isNull()) {
        return;
    }
    
    // Scale frame to fit window while maintaining aspect ratio
    QSize windowSize = m_videoLabel->size();
    QImage scaledFrame = m_currentFrame.scaled(
        windowSize,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    );
    
    m_videoLabel->setPixmap(QPixmap::fromImage(scaledFrame));
    m_videoLabel->setStyleSheet("background-color: #000;");
}

void ExpandedView::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    fitFrameToWindow();
}

void ExpandedView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    
    // Toggle fullscreen with F key
    if (event->key() == Qt::Key_F) {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
        return;
    }
    
    QMainWindow::keyPressEvent(event);
}

} // namespace MCM

