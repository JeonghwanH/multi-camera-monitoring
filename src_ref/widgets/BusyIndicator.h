// Author: SeungJae Lee
// BusyIndicator interface: simple spinner controlled via start/stop.

#pragma once

#include <QWidget>
#include <QTimer>

class BusyIndicator : public QWidget
{
    Q_OBJECT
public:
    explicit BusyIndicator(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    void start();
    void stop();
    bool isSpinning() const { return m_running; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTimer m_timer;
    int m_angle = 0;      // current rotation angle for spokes
    bool m_running = false;
};
