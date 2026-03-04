// Author: SeungJae Lee
// AnalysisOverlayWidget: draws AI ROI boundaries and detection markers over the zoomable video view.

#include "AnalysisOverlayWidget.h"

#include "ZoomableVideoView.h"
#include "RoiStyleUtils.h"

#include <QGraphicsVideoItem>
#include <QPainter>
#include <QPolygonF>
#include <QPen>

#include <algorithm>
#include <QtMath>

AnalysisOverlayWidget::AnalysisOverlayWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
}

void AnalysisOverlayWidget::setVideoView(ZoomableVideoView *view)
{
    if (m_view == view)
        return;
    m_view = view;
    update();
}

void AnalysisOverlayWidget::setNativeSize(const QSize &size)
{
    if (m_nativeSize == size)
        return;
    m_nativeSize = size;
    update();
}

void AnalysisOverlayWidget::setRoiRect(const QRect &roi)
{
    if (m_roiRect == roi)
        return;
    m_roiRect = roi;
    update();
}

void AnalysisOverlayWidget::setShapes(const QVector<Shape> &shapes)
{
    m_shapes = shapes;
    update();
}

void AnalysisOverlayWidget::setRoiVisible(bool visible)
{
    if (m_roiVisible == visible)
        return;
    m_roiVisible = visible;
    update();
}

void AnalysisOverlayWidget::setPointRadius(double radius)
{
    radius = std::clamp(radius, 0.0, 64.0);
    if (qFuzzyCompare(m_pointRadius, radius))
        return;
    m_pointRadius = radius;
    update();
}

void AnalysisOverlayWidget::setSectionsEnabled(bool enabled)
{
    if (m_drawSections == enabled)
        return;
    m_drawSections = enabled;
    update();
}

void AnalysisOverlayWidget::setShowLegend(bool show)
{
    if (m_showLegend == show)
        return;
    m_showLegend = show;
    update();
}

QRectF AnalysisOverlayWidget::videoContentRect() const
{
    if (!m_view)
        return QRectF();

    const QRectF viewportRect = m_view->videoViewportRect();
    QWidget *viewport = m_view->viewport();
    if (!viewport || viewportRect.isEmpty())
        return QRectF();

    const QPoint topLeftGlobal = viewport->mapToGlobal(viewportRect.topLeft().toPoint());
    const QPoint bottomRightGlobal = viewport->mapToGlobal(viewportRect.bottomRight().toPoint());

    const QPoint topLeft = mapFromGlobal(topLeftGlobal);
    const QPoint bottomRight = mapFromGlobal(bottomRightGlobal);
    return QRectF(topLeft, bottomRight);
}

QColor AnalysisOverlayWidget::colorForClass(const QString &cls) const
{
    const QString key = cls.trimmed().toLower();
    if (key == QLatin1String("bead"))
        return QColor(QStringLiteral("#fffb00ff"));
    if (key == QLatin1String("pool"))
        return QColor(QStringLiteral("#FF0000"));
    if (key == QLatin1String("seam"))
        return QColor(QStringLiteral("#00FF5E"));
    if (key == QLatin1String("tack"))
        return QColor(QStringLiteral("#0000FF"));
    return QColor(QStringLiteral("#ff22ffff"));
}

void AnalysisOverlayWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QRectF targetRect = videoContentRect();
    if (targetRect.isEmpty())
        targetRect = rect();

    QSizeF native = m_nativeSize.isValid() ? QSizeF(m_nativeSize) : QSizeF(1920, 1080);

    QRect roi = m_roiRect;
    if (roi.width() <= 0 || roi.height() <= 0)
        roi = QRect(QPoint(0, 0), native.toSize());

    const double sx = roi.width() / 512.0;
    const double sy = roi.height() / 512.0;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    painter.setRenderHint(QPainter::Antialiasing, true);
#endif

    QGraphicsVideoItem *videoItem = m_view ? m_view->videoItem() : nullptr;
    QWidget *viewport = m_view ? m_view->viewport() : nullptr;

    // Convert native-frame coordinates into widget coordinates, respecting the current zoom viewport.
    auto mapVideoToOverlay = [&](const QPointF &videoPt) -> QPointF {
        if (m_view && videoItem && viewport)
        {
            const QPointF scenePoint = videoItem->mapToScene(videoPt);
            const QPoint viewPoint = m_view->mapFromScene(scenePoint);
            const QPoint globalPoint = viewport->mapToGlobal(viewPoint);
            return mapFromGlobal(globalPoint);
        }

        const double normX = native.width() > 0.0 ? videoPt.x() / native.width() : 0.0;
        const double normY = native.height() > 0.0 ? videoPt.y() / native.height() : 0.0;
        const double screenX = targetRect.left() + normX * targetRect.width();
        const double screenY = targetRect.top() + normY * targetRect.height();
        return QPointF(screenX, screenY);
    };

    QPolygonF roiPolygon;
    if (roi.width() > 0 && roi.height() > 0)
    {
        const double roiRight = roi.left() + roi.width();
        const double roiBottom = roi.top() + roi.height();
        roiPolygon << mapVideoToOverlay(QPointF(roi.left(), roi.top()))
                   << mapVideoToOverlay(QPointF(roiRight, roi.top()))
                   << mapVideoToOverlay(QPointF(roiRight, roiBottom))
                   << mapVideoToOverlay(QPointF(roi.left(), roiBottom));
    }

    if (m_roiVisible && !roiPolygon.isEmpty())
    {
        painter.save();
        QPainterPath dimmedPath;
        dimmedPath.addRect(rect());
        QPainterPath roiPath;
        roiPath.addPolygon(roiPolygon);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 90));
        painter.drawPath(dimmedPath.subtracted(roiPath));
        painter.restore();

        const QPointF topLeft = roiPolygon.first();
        const QPointF bottomRight = roiPolygon.at(2);
        const QRectF roiRect = QRectF(topLeft, bottomRight).normalized();

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(RoiStyleUtils::roiOutlinePen());
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(roiRect, 6, 6);

        if (m_drawSections)
            RoiStyleUtils::drawRoiSections(painter, roiRect);

        painter.restore();

        if (m_showLegend)
        {
            const auto legendItems = RoiStyleUtils::detectionLegendItems();
            if (!legendItems.isEmpty())
                RoiStyleUtils::drawLegend(painter, rect(), legendItems);
        }
    }

    if (!m_shapes.isEmpty())
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::NoBrush);

        const qreal pointRadius = std::clamp(static_cast<qreal>(m_pointRadius), static_cast<qreal>(0.5), static_cast<qreal>(64.0));

        for (const Shape &shape : m_shapes)
        {
            if (shape.pts512.isEmpty())
                continue;

            const QColor color = colorForClass(shape.cls);
            painter.setBrush(color);

            for (const QPointF &pt : shape.pts512)
            {
                const double videoX = roi.x() + pt.x() * sx;
                const double videoY = roi.y() + pt.y() * sy;
                const QPointF mapped = mapVideoToOverlay(QPointF(videoX, videoY));
                painter.drawEllipse(mapped, pointRadius, pointRadius);
            }
        }
    }

}
