// Author: SeungJae Lee
// ToggleSwitch: minimal pill-shaped toggle used throughout the UI.

#include "ToggleSwitch.h"

#include <QPainter>
#include <QPaintEvent>

namespace
{
constexpr int kTrackWidth = 48;
constexpr int kTrackHeight = 24;
constexpr int kTrackMargin = 2;
constexpr int kKnobMargin = 2;
}

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(kTrackWidth, kTrackHeight);
}

QSize ToggleSwitch::minimumSizeHint() const
{
    return sizeHint();
}

void ToggleSwitch::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = kTrackHeight / 2.0;
    QRectF trackRect(kTrackMargin, kTrackMargin,
                     width() - 2 * kTrackMargin,
                     height() - 2 * kTrackMargin);

    QColor trackColor = isChecked() ? QColor(QStringLiteral("#00FFB7"))
                                    : QColor(QStringLiteral("#3A3D44"));
    if (!isEnabled())
        trackColor.setAlphaF(0.35);

    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor);
    painter.drawRoundedRect(trackRect, radius, radius);

    const qreal knobDiameter = trackRect.height() - 2 * kKnobMargin;
    qreal knobX = isChecked()
                      ? trackRect.right() - knobDiameter - kKnobMargin
                      : trackRect.left() + kKnobMargin;
    QRectF knobRect(knobX, trackRect.top() + kKnobMargin, knobDiameter, knobDiameter);

    QColor knobColor = QColor(QStringLiteral("#F5F5F5"));
    if (!isEnabled())
        knobColor.setAlphaF(0.55);

    painter.setBrush(knobColor);
    painter.drawEllipse(knobRect);
}
