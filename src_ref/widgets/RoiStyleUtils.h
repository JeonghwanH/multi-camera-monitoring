// Author: SeungJae Lee
// RoiStyleUtils: shared helpers for drawing ROI overlays consistently across widgets.

#pragma once

#include <QPen>
#include <QRectF>
#include <QVector>
#include <QString>
#include <QColor>

class QPainter;

namespace RoiStyleUtils
{
QPen roiOutlinePen();
void drawRoiSections(QPainter &painter, const QRectF &roiRect);
struct LegendItem
{
    QString label;
    QColor color;
};
QVector<LegendItem> detectionLegendItems();
QRectF drawLegend(QPainter &painter, const QRectF &availableRect, const QVector<LegendItem> &items, double margin = 12.0);
}
