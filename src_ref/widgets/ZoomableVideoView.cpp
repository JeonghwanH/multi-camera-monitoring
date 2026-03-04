// Author: SeungJae Lee
// ZoomableVideoView: QGraphicsView wrapper to display and zoom QGraphicsVideoItem content.

#include "ZoomableVideoView.h"

#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QPainter>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QtGlobal>

#include <algorithm>

namespace
{
constexpr qreal kInitialSceneMargin = 0.5; // Avoids fitInView warnings when native size is zero.
}

ZoomableVideoView::ZoomableVideoView(QWidget *parent)
    : QGraphicsView(parent)
{
    initializeScene();
    setFrameStyle(QFrame::NoFrame);
    setBackgroundBrush(Qt::black);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setDragMode(QGraphicsView::NoDrag);
    setInteractive(false);
}

void ZoomableVideoView::setHorizontalPadding(int left, int right)
{
    left = std::max(0, left);
    right = std::max(0, right);
    if (m_horizontalPaddingLeft == left && m_horizontalPaddingRight == right)
        return;
    m_horizontalPaddingLeft = left;
    m_horizontalPaddingRight = right;
    setViewportMargins(m_horizontalPaddingLeft, 0, m_horizontalPaddingRight, 0);
    updateViewTransform();
}

void ZoomableVideoView::initializeScene()
{
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(QRectF(-kInitialSceneMargin, -kInitialSceneMargin, kInitialSceneMargin * 2, kInitialSceneMargin * 2));
    setScene(m_scene);

    m_videoItem = new QGraphicsVideoItem();
    m_scene->addItem(m_videoItem);

    connect(m_videoItem, &QGraphicsVideoItem::nativeSizeChanged, this, [this](const QSizeF &) {
        updateViewTransform();
    });
}

QGraphicsVideoItem *ZoomableVideoView::videoItem() const
{
    return m_videoItem;
}

QRectF ZoomableVideoView::videoViewportRect() const
{
    if (!m_videoItem || !viewport())
        return QRectF();

    const QRectF sceneRect = m_videoItem->mapRectToScene(m_videoItem->boundingRect());
    const QPolygonF mapped = mapFromScene(sceneRect);
    if (mapped.isEmpty())
        return QRectF();

    QRectF viewportRect = mapped.boundingRect();
    viewportRect &= QRectF(QPointF(0.0, 0.0), QSizeF(viewport()->size()));
    return viewportRect;
}

QRectF ZoomableVideoView::visibleSceneRect() const
{
    if (!viewport())
        return QRectF();

    const QPolygonF mapped = mapToScene(viewport()->rect());
    if (mapped.isEmpty())
        return QRectF();
    return mapped.boundingRect();
}

QPointF ZoomableVideoView::visibleSceneCenter() const
{
    const QRectF rect = visibleSceneRect();
    if (rect.isValid())
        return rect.center();
    if (m_videoItem)
        return m_videoItem->boundingRect().center();
    return QPointF();
}

void ZoomableVideoView::restoreVisibleSceneCenter(const QPointF &center)
{
    if (!m_videoItem)
        return;

    const QRectF bounds = m_videoItem->boundingRect();
    if (bounds.isEmpty())
        return;

    QPointF target = center;
    if (!bounds.contains(target))
    {
        target.setX(std::clamp(target.x(), bounds.left(), bounds.right()));
        target.setY(std::clamp(target.y(), bounds.top(), bounds.bottom()));
    }

    centerOn(target);
    emit viewRectChanged();
}

void ZoomableVideoView::updateVideoItemSize(const QSize &frameSize)
{
    if (!m_videoItem)
        return;

    if (frameSize.width() <= 0 || frameSize.height() <= 0)
        return;

    const QSize current = m_videoItem->size().toSize();
    if (current == frameSize)
        return;

    m_videoItem->setSize(frameSize);
    updateViewTransform();
}

void ZoomableVideoView::setZoomed(bool zoomed)
{
    if (m_zoomed == zoomed)
        return;

    m_zoomed = zoomed;
    setDragMode(m_zoomed ? QGraphicsView::ScrollHandDrag : QGraphicsView::NoDrag);
    setInteractive(m_zoomed);
    updateViewTransform();
}

bool ZoomableVideoView::isZoomed() const
{
    return m_zoomed;
}

void ZoomableVideoView::setZoomFactor(qreal factor)
{
    const qreal clamped = std::clamp(factor, 1.0, 4.0);
    if (qFuzzyCompare(m_zoomFactor, clamped))
        return;
    m_zoomFactor = clamped;
    if (m_zoomed)
        updateViewTransform();
}

void ZoomableVideoView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    updateViewTransform();
}

void ZoomableVideoView::updateViewTransform()
{
    if (!m_videoItem)
        return;

    QRectF previousViewSceneRect;
    if (m_zoomed && viewport())
    {
        const QPolygonF mapped = mapToScene(viewport()->rect());
        if (!mapped.isEmpty())
            previousViewSceneRect = mapped.boundingRect();
    }

    const QRectF bounds = m_videoItem->boundingRect();
    if (bounds.isEmpty())
        return;

    resetTransform();
    m_scene->setSceneRect(bounds);

    const Qt::AspectRatioMode mode = m_zoomed ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio;
    fitInView(bounds, mode);

    if (m_zoomed && m_zoomFactor > 1.0)
        scale(m_zoomFactor, m_zoomFactor);

    if (m_zoomed && viewport())
    {
        // Prevent blank margins when the zoom leaves empty space by ensuring the scaled scene covers viewport.
        QRectF visibleScene = mapToScene(viewport()->rect()).boundingRect();
        if (visibleScene.isValid())
        {
            if (visibleScene.height() > bounds.height())
            {
                const qreal factor = visibleScene.height() / bounds.height();
                scale(factor, factor);
                visibleScene = mapToScene(viewport()->rect()).boundingRect();
            }
            if (visibleScene.width() > bounds.width())
            {
                const qreal factor = visibleScene.width() / bounds.width();
                scale(factor, factor);
            }
        }
    }

    if (!m_zoomed)
        centerOn(bounds.center());
    else if (previousViewSceneRect.isValid())
        centerOn(previousViewSceneRect.center());

    emit viewRectChanged();

}

void ZoomableVideoView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    emit viewRectChanged();
}
