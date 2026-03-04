// Author: SeungJae Lee
// CameraPreviewWidget interface: composite preview with overlays, toggles, and zoom controls.

#pragma once

#include <QFrame>
#include <QPointer>
#include <QString>
#include <QMargins>
#include <QRect>
#include <QSize>
#include <QVector>
#include <QPointF>
#include <QImage>
#include <optional>

#include <QMediaCaptureSession>

#include <memory>

#include "managers/CameraManager.h"

class QCamera;
class QHBoxLayout;
class QWidget;
class QToolButton;
class ZoomableVideoView;
class QResizeEvent;
class QVBoxLayout;
class QMouseEvent;
class QWheelEvent;
class QLabel;
class ToggleSwitch;
class QKeyEvent;
class AnalysisOverlayWidget;
class QVideoSink;
class QVideoFrame;
class AnalysisStatusPanel;

class CameraPreviewWidget : public QFrame
{
    Q_OBJECT

public:
    explicit CameraPreviewWidget(const QString &cameraId, QWidget *parent = nullptr);

    struct AnalysisShape
    {
        QString cls;
        QVector<QPointF> pts512;
    };

    QString cameraId() const;
    void updateInfo(const CameraManager::CameraInfo &info);
    void setCamera(QCamera *camera);
    QMediaCaptureSession *captureSession() const;
    void setRecordingActive(bool recording);
    bool isZoomed() const;
    void setViewportContext(QWidget *viewportWidget, bool fillViewportWidth, const QMargins &outerMargins);
    void setZoomControlsVisible(bool visible);
    void setZoomState(bool zoomed, bool notify = false);
    void setAnalysisPassInfo(const QString &passLabel);
    void setAlignmentOffset(double offsetMm, bool fromUser = false);
    void setAlignmentWarningThreshold(double thresholdMm);
    void setPlcControlEnabled(bool enabled);
    void setAiAnalysisEnabled(bool enabled);
    void setAiToggleEnabled(bool enabled);
    void setAiToggleTooltip(const QString &text);
    void setAnalysisOverlay(const QVector<AnalysisShape> &shapes, const QRect &roi, const QSize &frameSize);
    void setVideoHorizontalPadding(int left, int right);
    void setAnalysisPointSize(double sizePx);
    void setShowLegend(bool show);
    QSize currentFrameSize() const;
    bool isRoiOverlayEnabled() const { return m_roiOverlayEnabled; }
    std::optional<QPointF> zoomSceneCenter() const;
    void restoreZoomSceneCenter(const std::optional<QPointF> &center);

signals:
    void startRecordingRequested(const QString &cameraId);
    void stopRecordingRequested(const QString &cameraId);
    void snapshotRequested(const QString &cameraId);
    void zoomToggled(bool zoomed);
    void plcControlToggled(const QString &cameraId, bool enabled);
    void aiAnalysisToggled(const QString &cameraId, bool enabled);
    void alignmentOffsetAdjusted(const QString &cameraId, double offsetMm);
    void frameAvailable(const QString &cameraId, const QImage &frame);
    void roiOverlayToggled(bool visible);

private:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void buildUi();
    void retranslateUi();
    void updateHeader();
    void updateRecordingBadge();
    void rebuildSettingIndicators();
    void updateRoiButtonState();
    void updateZoomButtonIcon();
    void applyZoomState(bool zoomed);
    void updateZoomSizing();
    void updateOverlayGeometry();
    void updateOverlayChrome();
    void updateCornerMask();
    void buildAnalysisOverlay();
    void refreshOverlayContent();
    void updateAnalysisOverlay();
    void forwardMouseEventToVideoView(QMouseEvent *event);
    void forwardWheelEventToVideoView(QWheelEvent *event);
    void handleVideoFrameChanged(const QVideoFrame &frame);
    void scheduleZoomSizingUpdate();

private:
    QString m_cameraId;
    CameraManager::CameraInfo m_info;

    QPointer<QCamera> m_camera;
    std::unique_ptr<QMediaCaptureSession> m_captureSession;

    QWidget *m_headerBar = nullptr;
    QLabel *m_recordBadge = nullptr;
    QLabel *m_headerTitle = nullptr;
    QWidget *m_settingsContainer = nullptr;
    QHBoxLayout *m_settingsLayout = nullptr;
    QWidget *m_overlayWidget = nullptr;
    AnalysisOverlayWidget *m_analysisOverlay = nullptr;
    QWidget *m_videoContainer = nullptr;
    ZoomableVideoView *m_videoView = nullptr;
    QVBoxLayout *m_overlayLayout = nullptr;
    AnalysisStatusPanel *m_analysisStatusPanel = nullptr;
    ToggleSwitch *m_plcToggle = nullptr;
    ToggleSwitch *m_aiToggle = nullptr;
    QToolButton *m_roiButton = nullptr;
    QToolButton *m_zoomButton = nullptr;
    bool m_recordingActive = false;
    bool m_zoomed = false;
    bool m_zoomControlsVisible = true;
    bool m_roiOverlayEnabled = false;
    QPointer<QWidget> m_viewportWidget;
    bool m_fillViewportWidth = false;
    QMargins m_outerMargins;
    bool m_pendingZoomSizingUpdate = false;
    QString m_passLabel;
    double m_alignmentOffsetMm = 0.0;
    bool m_plcEnabled = false;
    bool m_aiEnabled = false;
    double m_alignmentWarningThreshold = 0.0;
    QVector<AnalysisShape> m_analysisShapes;
    QRect m_analysisRoi;
    QSize m_analysisFrameSize;
    int m_videoPaddingLeft = 0;
    int m_videoPaddingRight = 0;
    double m_analysisPointSizePx = 8.0;
    QSize m_lastFrameSize;
    bool m_seenFirstFrame = false;
    bool m_showLegend = true;
};
