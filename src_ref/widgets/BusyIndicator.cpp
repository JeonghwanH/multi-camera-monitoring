// Author: SeungJae Lee
// BusyIndicator: animated radial indicator used while long-running tasks are in progress.

#include "BusyIndicator.h"

#include <QPainter>
#include <QStyleOption>

BusyIndicator::BusyIndicator(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFixedSize(60, 60);
    m_timer.setInterval(80); // advance rotation roughly 12 frames per second
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        m_angle = (m_angle + 30) % 360;
        update();
    });
}

QSize BusyIndicator::sizeHint() const
{
    return QSize(60, 60);
}

void BusyIndicator::start()
{
    if (m_running)
        return;
    m_running = true;
    m_timer.start();
    update();
}

void BusyIndicator::stop()

{
    if (!m_running)
        return;
    m_running = false;
    m_timer.stop();
    update();
}

void BusyIndicator::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const QRectF bounds = rect().adjusted(4, 4, -4, -4);
    const QColor baseColor = QColor(0, 255, 183);
    const double radius = bounds.height() / 2.0;
    const double barHeight = bounds.height() / 3.0;
    const double barWidth = bounds.width() / 12.0;
    const double cornerRadius = barWidth / 2.0;

    for (int i = 0; i < 12; ++i)
    {
        const int angle = (m_angle + i * 30) % 360;
        QColor color = baseColor;
        color.setAlphaF(1.0 - (i / 12.0));
        painter.setBrush(color);

        painter.save();
        painter.translate(bounds.center());
        painter.rotate(angle);
        painter.drawRoundedRect(QRectF(-barWidth / 2.0, -radius, barWidth, barHeight), cornerRadius, cornerRadius);
        painter.restore();
    }

    if (!m_running)
    {
        painter.setBrush(QColor(0, 255, 183, 150));
        const double dotRadius = bounds.width() / 8.0;
        painter.drawEllipse(bounds.center(), dotRadius, dotRadius);
    }
}
