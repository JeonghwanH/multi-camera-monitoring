// Author: SeungJae Lee
// AnalysisOverlayWidget interface: maintains detection shapes and renders them atop the video viewport.

#pragma once

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QString>
#include <QRect>

class ZoomableVideoView;

class AnalysisOverlayWidget : public QWidget
{
    Q_OBJECT

public:
    struct Shape
    {
        QString cls;             // semantic label for choosing marker colour
        QVector<QPointF> pts512; // detection points expressed in 512x512 normalized grid
    };

    explicit AnalysisOverlayWidget(QWidget *parent = nullptr);

    void setVideoView(ZoomableVideoView *view);
    void setNativeSize(const QSize &size);
    void setRoiRect(const QRect &roi);
    void setShapes(const QVector<Shape> &shapes);
    void setRoiVisible(bool visible);
    void setPointRadius(double radius);
    void setSectionsEnabled(bool enabled);
    void setShowLegend(bool show);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRectF videoContentRect() const;
    QColor colorForClass(const QString &cls) const;

    ZoomableVideoView *m_view = nullptr;
    QSize m_nativeSize;        // native frame resolution used for ROI scaling
    QRect m_roiRect;           // ROI rectangle in native coordinates
    QVector<Shape> m_shapes;   // current detection shapes to render
    bool m_roiVisible = false; // whether to draw ROI boundary
    double m_pointRadius = 4.0;
    bool m_drawSections = false;
    bool m_showLegend = true;
};
