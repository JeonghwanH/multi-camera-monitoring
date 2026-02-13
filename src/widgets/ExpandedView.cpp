#include "ExpandedView.h"
#include "OptimizedVideoWidget.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QVideoSink>
#include <QDebug>

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
    centralWidget->setStyleSheet("background-color: #1a1a2e;");
    setCentralWidget(centralWidget);
    
    QVBoxLayout* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    
    // GPU-accelerated video widget
    m_videoWidget = new OptimizedVideoWidget(this);
    layout->addWidget(m_videoWidget);
}

void ExpandedView::updateFrame(const QVideoFrame& frame) {
    if (!frame.isValid()) {
        return;
    }
    
    // Push frame directly to the video widget's sink
    // This allows GPU-accelerated display
    QVideoSink* sink = m_videoWidget->videoSink();
    if (sink) {
        sink->setVideoFrame(frame);
    }
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
