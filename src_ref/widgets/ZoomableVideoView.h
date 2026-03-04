// Author: SeungJae Lee
// ZoomableVideoView interface: exposes helper signals for overlay alignment while zooming video.

#pragma once

#include <QGraphicsView>

#include <QGraphicsVideoItem>
#include <QSize>

class QGraphicsScene;

class ZoomableVideoView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit ZoomableVideoView(QWidget *parent = nullptr);

    QGraphicsVideoItem *videoItem() const;
    void setZoomed(bool zoomed);
    bool isZoomed() const;
    QRectF videoViewportRect() const;
    void updateVideoItemSize(const QSize &frameSize);
    void setHorizontalPadding(int left, int right);
    int horizontalPaddingLeft() const { return m_horizontalPaddingLeft; }
    int horizontalPaddingRight() const { return m_horizontalPaddingRight; }
    void setZoomFactor(qreal factor);
    qreal zoomFactor() const { return m_zoomFactor; }
    QRectF visibleSceneRect() const;
    QPointF visibleSceneCenter() const;
    void restoreVisibleSceneCenter(const QPointF &center);

signals:
    void viewRectChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    void initializeScene();
    void updateViewTransform();

    QGraphicsScene *m_scene = nullptr;
    QGraphicsVideoItem *m_videoItem = nullptr;
    bool m_zoomed = false;
    int m_horizontalPaddingLeft = 0;
    int m_horizontalPaddingRight = 0;
    qreal m_zoomFactor = 1.5;
};
