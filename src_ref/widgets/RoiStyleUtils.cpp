// Author: SeungJae Lee
// RoiStyleUtils: shared helpers for drawing ROI overlays consistently across widgets.

#include "RoiStyleUtils.h"

#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QCoreApplication>

namespace
{
constexpr double kOutlineWidth = 3.0;
const QColor kOutlineColor(QStringLiteral("#00FFB7"));
const QColor kTorchFill(17, 132, 252, 20);
const QColor kSeamFill(250, 130, 0, 34);
const QColor kTorchLabel(QStringLiteral("#7CE2FE"));
const QColor kSeamLabel(QStringLiteral("#FFC53D"));
const QColor kNozzleLabel(210, 226, 255);
const QColor kTorchDivider(QStringLiteral("#7CE2FE"));
const QColor kSeamDivider(QStringLiteral("#FFC53D"));
const QColor kLegendBackground(17, 17, 19, 100);
const QColor kLegendOutline(0, 0, 0, 220);
}

namespace RoiStyleUtils
{
QPen roiOutlinePen()
{
    QPen pen(kOutlineColor);
    pen.setWidthF(kOutlineWidth);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

void drawRoiSections(QPainter &painter, const QRectF &roiRect)
{
    if (!roiRect.isValid() || roiRect.width() <= 0.0 || roiRect.height() <= 0.0)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal top = roiRect.top();
    const qreal left = roiRect.left();
    const qreal width = roiRect.width();
    const qreal height = roiRect.height();

    const qreal quarterHeight = height / 4.0;
    const qreal halfHeight = height / 2.0;

    const QRectF torchArea(left, top, width, quarterHeight);
    const QRectF nozzleArea(left, top + quarterHeight, width, quarterHeight);
    const QRectF seamArea(left, top + halfHeight, width, height - halfHeight);

    painter.fillRect(torchArea, kTorchFill);
    painter.fillRect(nozzleArea, Qt::transparent);
    painter.fillRect(seamArea, kSeamFill);

    painter.setPen(QPen(kTorchDivider, 2));
    painter.drawLine(QPointF(left, top + quarterHeight), QPointF(left + width, top + quarterHeight));

    painter.setPen(QPen(kSeamDivider, 2));
    painter.drawLine(QPointF(left, top + halfHeight), QPointF(left + width, top + halfHeight));

    const QFont originalFont = painter.font();
    QFont scaledFont = originalFont;
    scaledFont.setPointSizeF(originalFont.pointSizeF() * 1.5);
    painter.setFont(scaledFont);

    auto drawLabel = [&](const QRectF &area, const QString &text, const QColor &color) {
        painter.setPen(color);
        painter.drawText(area, Qt::AlignCenter, text);
    };

    drawLabel(torchArea, QCoreApplication::translate("RoiLegend", "Torch Area"), kTorchLabel);
    drawLabel(nozzleArea, QCoreApplication::translate("RoiLegend", "Nozzle Area"), kNozzleLabel);
    drawLabel(seamArea, QCoreApplication::translate("RoiLegend", "Seam, Tack, Bead Area"), kSeamLabel);

    painter.setFont(originalFont);
    painter.restore();
}

QVector<LegendItem> detectionLegendItems()
{
    return {
        {QCoreApplication::translate("RoiLegend", "Seam"), QColor(QStringLiteral("#00FF5E"))},
        {QCoreApplication::translate("RoiLegend", "Tack"), QColor(QStringLiteral("#0000FF"))},
        {QCoreApplication::translate("RoiLegend", "Bead"), QColor(QStringLiteral("#fffb00"))}
    };
}

QRectF drawLegend(QPainter &painter, const QRectF &availableRect, const QVector<LegendItem> &items, double margin)
{
    if (items.isEmpty())
        return QRectF();

    painter.save();

    const QFontMetrics fm(painter.font());
    const double swatchSize = 12.0;
    const double swatchSpacing = 8.0;
    const double rowSpacing = 4.0;
    const double lineHeight = std::max(static_cast<double>(fm.height()), 16.0);

    double textWidth = 0.0;
    for (const LegendItem &item : items)
        textWidth = std::max(textWidth, static_cast<double>(fm.horizontalAdvance(item.label)));

    const double boxWidth = margin * 2.0 + swatchSize + swatchSpacing + textWidth;
    const double boxHeight = margin * 2.0 + items.size() * lineHeight + (items.size() - 1) * rowSpacing;

    const double alignX = availableRect.left() + margin + 14.0;
    QPointF topLeft(alignX,
                    availableRect.top() + margin + 40.0);
    QRectF boxRect(topLeft, QSizeF(boxWidth, boxHeight));

    painter.setPen(Qt::NoPen);
    painter.setBrush(kLegendBackground);
    painter.drawRoundedRect(boxRect, 8.0, 8.0);

    double cursorY = boxRect.top() + margin;
    for (const LegendItem &item : items)
    {
        QRectF swatchRect(boxRect.left() + margin,
                          cursorY + (lineHeight - swatchSize) / 2.0,
                          swatchSize,
                          swatchSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(item.color);
        painter.drawRoundedRect(swatchRect, 3.0, 3.0);
        painter.setPen(QPen(kLegendOutline, 1.0));
        painter.drawRoundedRect(swatchRect, 3.0, 3.0);

        painter.setPen(Qt::white);
        QRectF textRect(swatchRect.right() + swatchSpacing,
                        cursorY,
                        textWidth,
                        lineHeight);
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, item.label);

        cursorY += lineHeight + rowSpacing;
    }

    painter.restore();
    return boxRect;
}
}
