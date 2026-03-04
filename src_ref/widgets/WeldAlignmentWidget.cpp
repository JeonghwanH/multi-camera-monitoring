// Author: SeungJae Lee
// WeldAlignmentWidget: visualizes nozzle/wire offset relative to seam with warning state.

#include "WeldAlignmentWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

#include <QtMath>

#include <algorithm>
namespace
{
constexpr double kClampRange = 1.0;
constexpr int kMargin = 8;
constexpr QColor kBackgroundColor(0x2A, 0x2C, 0x31, 200);
constexpr QColor kSeamColor(0x69, 0x6E, 0x77);
constexpr QColor kBaseMetalColor(0x2F, 0x31, 0x36);
constexpr QColor kWireColor(0x00, 0xFF, 0xB7);
constexpr QColor kWireWarningColor(0xE5, 0x48, 0x4D);
constexpr QColor kNozzleColor(0xAA, 0xAA, 0xAA);
}

WeldAlignmentWidget::WeldAlignmentWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(70);
}

double WeldAlignmentWidget::offset() const
{
    return m_offset;
}

void WeldAlignmentWidget::setOffset(double offset)
{
    const double clamped = std::clamp(offset, -kClampRange, kClampRange);
    if (qFuzzyCompare(m_offset, clamped))
        return;
    m_offset = clamped;
    update();
}

void WeldAlignmentWidget::setWarningActive(bool warning)
{
    if (m_warningActive == warning)
        return;
    m_warningActive = warning;
    update();
}

void WeldAlignmentWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds = rect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(kBackgroundColor);
    painter.drawRoundedRect(bounds, 8, 8);

    const QRectF inner = bounds.adjusted(kMargin, kMargin, -kMargin, -kMargin);
    const qreal centerX = inner.center().x();

    painter.setBrush(kBaseMetalColor);
    painter.drawRoundedRect(QRectF(inner.left(), inner.center().y() + inner.height() * 0.08,
                                   inner.width(), inner.height() * 0.32),
                            6, 6);

    painter.setPen(QPen(kSeamColor, 2));
    const qreal seamY = inner.center().y();
    painter.drawLine(QPointF(inner.left(), seamY), QPointF(inner.right(), seamY));

    const qreal nozzleWidth = inner.width() * 0.18;
    const qreal nozzleHeight = inner.height() * 0.4;
    const qreal wireHeight = inner.height() * 0.55;
    const qreal maxOffsetPx = inner.width() * 0.35;
    const qreal offsetPx = std::clamp(m_offset, -kClampRange, kClampRange) * maxOffsetPx; // convert normalized offset to pixels
    const qreal nozzleCenter = centerX + offsetPx;

    QPainterPath nozzlePath;
    nozzlePath.moveTo(nozzleCenter - nozzleWidth * 0.5, inner.top());
    nozzlePath.lineTo(nozzleCenter + nozzleWidth * 0.5, inner.top());
    nozzlePath.lineTo(nozzleCenter + nozzleWidth * 0.25, inner.top() + nozzleHeight);
    nozzlePath.lineTo(nozzleCenter - nozzleWidth * 0.25, inner.top() + nozzleHeight);
    nozzlePath.closeSubpath();
    painter.setBrush(kNozzleColor);
    painter.setPen(Qt::NoPen);
    painter.drawPath(nozzlePath);

    const QColor wireColor = m_warningActive ? kWireWarningColor : kWireColor;
    painter.setPen(QPen(wireColor, 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(nozzleCenter, inner.top() + nozzleHeight),
                     QPointF(nozzleCenter, inner.top() + wireHeight));
}
