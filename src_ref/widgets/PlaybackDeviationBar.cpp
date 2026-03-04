// Author: SeungJae Lee
// PlaybackDeviationBar: renders pass markers and current position along a horizontal timeline.

#include "PlaybackDeviationBar.h"

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>
#include <QtGlobal>

namespace
{
constexpr int kHorizontalPadding = 6;
constexpr int kBarHeight = 4;
constexpr qreal kCornerRadius = 2.0;
}

PlaybackDeviationBar::PlaybackDeviationBar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(10);
}

void PlaybackDeviationBar::setMarkers(const QVector<double> &markers)
{
    QVector<double> clamped;
    clamped.reserve(markers.size());
    for (double value : markers)
    {
        if (std::isfinite(value))
            clamped.append(std::clamp(value, 0.0, 1.0));
    }
    std::sort(clamped.begin(), clamped.end());
    if (m_markers == clamped)
        return;
    m_markers = clamped;
    update();
}

void PlaybackDeviationBar::setPosition(double ratio)
{
    const double clamped = std::clamp(ratio, 0.0, 1.0);
    if (qFuzzyCompare(m_position + 1.0, clamped + 1.0))
        return;
    m_position = clamped;
    update();
}

void PlaybackDeviationBar::setTrailingPadding(int px)
{
    const int clamped = std::max(0, px);
    if (m_trailingPaddingPx == clamped)
        return;
    m_trailingPaddingPx = clamped;
    update();
}

void PlaybackDeviationBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF outerRect = rect();
    outerRect.setRight(std::max(outerRect.left(), outerRect.right() - static_cast<qreal>(m_trailingPaddingPx)));
    if (outerRect.width() <= 0.0)
        return;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 140)); // shadow background for readability
    painter.drawRoundedRect(outerRect, kCornerRadius * 2, kCornerRadius * 2);

    QRectF barRect = outerRect.adjusted(kHorizontalPadding,
                                        (outerRect.height() - kBarHeight) / 2.0,
                                        -kHorizontalPadding,
                                        -(outerRect.height() - kBarHeight) / 2.0);
    painter.setBrush(QColor(110, 113, 120, 180));
    painter.drawRoundedRect(barRect, kCornerRadius, kCornerRadius);

    painter.setBrush(QColor(229, 72, 77)); // deviation markers
    for (double marker : m_markers)
    {
        const qreal x = barRect.left() + marker * barRect.width();
        QRectF markerRect(x - 1.0, barRect.top(), 2.0, barRect.height());
        painter.drawRoundedRect(markerRect, kCornerRadius, kCornerRadius);
    }

    const qreal positionX = barRect.left() + m_position * barRect.width();
    painter.setBrush(QColor(255, 255, 255));
    painter.drawEllipse(QPointF(positionX, barRect.center().y()), 3.0, 3.0);
}

QSize PlaybackDeviationBar::sizeHint() const
{
    const int baseWidth = 200;
    return QSize(baseWidth + m_trailingPaddingPx, 10);
}
