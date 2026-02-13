#include "OptimizedVideoWidget.h"
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QDebug>

namespace MCM {

OptimizedVideoWidget::OptimizedVideoWidget(QWidget* parent)
    : QWidget(parent)
    , m_aspectMode(Qt::KeepAspectRatio)
{
    // Create graphics scene
    m_scene = new QGraphicsScene(this);
    m_scene->setBackgroundBrush(QColor(26, 26, 46));  // Dark background matching VideoWidget
    
    // Create graphics view (the actual display widget)
    m_view = new QGraphicsView(m_scene, this);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setRenderHint(QPainter::Antialiasing, false);  // Video doesn't need antialiasing
    m_view->setRenderHint(QPainter::SmoothPixmapTransform, true);
    m_view->setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);
    m_view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    m_view->setBackgroundBrush(QColor(26, 26, 46));
    
    // Create video item (GPU-accelerated video rendering)
    m_videoItem = new QGraphicsVideoItem();
    m_videoItem->setAspectRatioMode(m_aspectMode);
    m_scene->addItem(m_videoItem);
    qDebug() << "OptimizedVideoWidget: Created QGraphicsVideoItem" << m_videoItem;
    
    // Connect to native size changes for proper scaling
    connect(m_videoItem, &QGraphicsVideoItem::nativeSizeChanged,
            this, &OptimizedVideoWidget::onNativeSizeChanged);
    qDebug() << "OptimizedVideoWidget: Connected nativeSizeChanged signal";
    
    // Layout
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_view);
    
    // Widget settings
    setMinimumSize(160, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

OptimizedVideoWidget::~OptimizedVideoWidget() {
    // QGraphicsScene owns the video item, will be cleaned up automatically
}

QVideoSink* OptimizedVideoWidget::videoSink() const {
    return m_videoItem->videoSink();
}

void OptimizedVideoWidget::clear() {
    qDebug() << "OptimizedVideoWidget::clear() - resetting video item";
    // Reset the video item to clear display
    // The background brush will show through
    m_nativeSize = QSizeF();
    m_videoItem->setSize(QSizeF(0, 0));
}

void OptimizedVideoWidget::resetVideoItem() {
    qDebug() << "OptimizedVideoWidget::resetVideoItem() - creating fresh video item";
    qDebug() << "  Old videoItem:" << m_videoItem;
    
    // Disconnect old signals
    disconnect(m_videoItem, &QGraphicsVideoItem::nativeSizeChanged,
               this, &OptimizedVideoWidget::onNativeSizeChanged);
    
    // Remove and delete old item
    m_scene->removeItem(m_videoItem);
    delete m_videoItem;
    
    // Create fresh video item
    m_videoItem = new QGraphicsVideoItem();
    m_videoItem->setAspectRatioMode(m_aspectMode);
    m_scene->addItem(m_videoItem);
    
    // Reconnect signal
    connect(m_videoItem, &QGraphicsVideoItem::nativeSizeChanged,
            this, &OptimizedVideoWidget::onNativeSizeChanged);
    
    // Reset state
    m_nativeSize = QSizeF();
    
    qDebug() << "  New videoItem:" << m_videoItem;
}

bool OptimizedVideoWidget::hasVideo() const {
    return m_nativeSize.isValid() && !m_nativeSize.isEmpty();
}

void OptimizedVideoWidget::setAspectRatioMode(Qt::AspectRatioMode mode) {
    m_aspectMode = mode;
    m_videoItem->setAspectRatioMode(mode);
    fitVideoInView();
}

void OptimizedVideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    fitVideoInView();
}

void OptimizedVideoWidget::onNativeSizeChanged(const QSizeF& size) {
    qDebug() << "*** OptimizedVideoWidget::onNativeSizeChanged ***" << size;
    qDebug() << "  VideoItem:" << m_videoItem << "previous nativeSize:" << m_nativeSize;
    m_nativeSize = size;
    fitVideoInView();
}

void OptimizedVideoWidget::fitVideoInView() {
    if (!m_nativeSize.isValid() || m_nativeSize.isEmpty()) {
        return;
    }
    
    QSize viewSize = m_view->size();
    if (viewSize.isEmpty()) {
        return;
    }
    
    // Calculate the scaled size maintaining aspect ratio
    QSizeF scaledSize = m_nativeSize.scaled(
        QSizeF(viewSize),
        m_aspectMode
    );
    
    // Set the video item size
    m_videoItem->setSize(scaledSize);
    
    // Center the video item in the scene
    qreal x = (viewSize.width() - scaledSize.width()) / 2.0;
    qreal y = (viewSize.height() - scaledSize.height()) / 2.0;
    m_videoItem->setPos(x, y);
    
    // Update scene rect to match view
    m_scene->setSceneRect(0, 0, viewSize.width(), viewSize.height());
}

} // namespace MCM

