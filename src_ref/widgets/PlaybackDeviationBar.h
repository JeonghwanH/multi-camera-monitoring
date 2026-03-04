// Author: SeungJae Lee
// PlaybackDeviationBar interface: shows deviation markers and playback cursor per timeline.

#pragma once

#include <QWidget>
#include <QVector>

class PlaybackDeviationBar : public QWidget
{
    Q_OBJECT

public:
    explicit PlaybackDeviationBar(QWidget *parent = nullptr);

    void setMarkers(const QVector<double> &markers);
    void setPosition(double ratio);
    void setTrailingPadding(int px);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;

private:
    QVector<double> m_markers; // normalized marker positions [0,1]
    double m_position = 0.0;
    int m_trailingPaddingPx = 0;
};
