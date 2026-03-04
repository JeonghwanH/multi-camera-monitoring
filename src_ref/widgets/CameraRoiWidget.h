// Author: SeungJae Lee
// CameraRoiWidget interface: orchestrates ROI editing around a zoomable video view.

#pragma once

#include <QCamera>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QRectF>
#include <QTimer>
#include <QWidget>

#include <memory>

class QCamera;
class QMediaCaptureSession;
class QResizeEvent;
class CameraManager;
class ZoomableVideoView;
class RoiSelectionOverlay;

class CameraRoiWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CameraRoiWidget(QWidget *parent = nullptr);

    void setCamera(CameraManager *manager, const QString &cameraId);
    void clearCamera();
    void resetRoi();
    void applyNormalizedRoi(const QRectF &normalizedRoi);
    QRectF currentNormalizedRoi() const;
    void setShowLegend(bool show);

signals:
    void roiChanged(const QRectF &roiRect);
    void normalizedRoiChanged(const QRectF &roiRect);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void updateOverlayGeometry();
    void applyCameraHandle(QCamera *camera);
    void ensureConnections();
    void disconnectCameraSignals();
    void applyDefaultRoi(const QRectF &videoRect);
    void handleResizeFreeze(const QRect &targetRect);
    void beginResizeFreeze(const QRect &targetRect);
    void updateFrozenFrameGeometry(const QRect &targetRect);
    void endResizeFreeze();

    CameraManager *m_cameraManager = nullptr;
    QString m_cameraId;
    QPointer<QCamera> m_cameraHandle;
    std::unique_ptr<QMediaCaptureSession> m_captureSession; // owned session when preview needs isolation

    QWidget *m_videoContainer = nullptr;
    ZoomableVideoView *m_videoView = nullptr;
    RoiSelectionOverlay *m_roiOverlay = nullptr;
    QMetaObject::Connection m_cameraActiveConnection;
    QRectF m_normalizedRoi;
    bool m_hasNormalizedRoi = false;
    QRectF m_pendingNormalizedRoi;
    bool m_hasPendingNormalizedRoi = false;
    QLabel *m_frozenFrameLabel = nullptr;
    QTimer *m_resizeFreezeTimer = nullptr;
    bool m_resizeFrozen = false;
    bool m_pendingFreezeCapture = false;
    QSize m_lastViewportSize;
    bool m_showLegend = true;
};
