// Author: SeungJae Lee
// WeldingPage interface: manages live preview grid, recording controls, and analysis state.

#pragma once

#include <QMap>
#include <QSet>
#include <QStringList>
#include <QWidget>
#include <QEvent>
#include <QRect>
#include <QRectF>
#include <QJsonObject>
#include <QSize>
#include <QVector>
#include <QImage>
#include <optional>

#include "managers/CameraManager.h"
#include "widgets/CameraPreviewWidget.h"

class QGridLayout;
class QScrollArea;
class RecordingManager;
class CameraPreviewWidget;
class QButtonGroup;
class QPushButton;
class QHBoxLayout;
class QVariantAnimation;
class TwoCameraSplitter;
class AiClient;
class PLCClient;

class WeldingPage : public QWidget
{
    Q_OBJECT

public:
    WeldingPage(CameraManager *cameraManager, RecordingManager *recordingManager,
                AiClient *aiClient, PLCClient *plcClient, QWidget *parent = nullptr);
    void updateAnalysisMetrics(const QString &cameraId, double weldPoolWidthMm);
    void setAnalysisOverlay(const QString &cameraId,
                            const QVector<CameraPreviewWidget::AnalysisShape> &shapes,
                            const QRect &roi,
                            const QSize &frameSize);
    void setCameraAiEnabled(const QString &cameraId, bool enabled);
    void applyAiToggleLock(const QString &activeCameraId, const QString &message);
    void applyPlcControlData(const QString &cameraId,
                             const std::optional<double> &deviationMm = std::nullopt,
                             const std::optional<double> &warningThresholdMm = std::nullopt);
signals:
    void frameCaptured(const QString &cameraId, const QImage &frame);
    void aiToggleRequested(const QString &cameraId, bool enabled);

private:
    void buildUi();
    void addCameraWidget(const CameraManager::CameraInfo &info);
    void removeCameraWidget(const QString &cameraId);
    void updateCameraWidget(const CameraManager::CameraInfo &info);
    void updateCameraVisibility(const QString &cameraId, bool visible);
    void rebuildGrid();
    int columnCountFor(int visibleCameraCount) const;
    void rebuildSlotFilters();
    void finalizeCameraWidgetRemoval(const QString &cameraId);
    void applySlotFilter(const QString &slotId);
    QStringList filteredCameraIds() const;
    void onRecordButtonToggled(bool recording);
    void updateRecordButtonState();
    void styleSlotButton(QPushButton *button) const;
    void styleRecordButton();
    void applyRecordButtonStyle(double glowAmount);
    void updatePreviewViewportContexts(int visibleCount);
    void scheduleViewportRefresh();
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void updateZoomControlsForFilter();
    void updatePreviewAnalysisState(CameraPreviewWidget *preview);
    void updateAllPreviewAnalysisStates();
    void connectPreviewSignals(CameraPreviewWidget *preview);
    void handleAiToggleRequested(const QString &cameraId, bool enabled);
    void handlePlcToggleRequested(bool enabled);
    void handleAlignmentOffsetChanged(const QString &cameraId, double offsetMm);
    void handleFrameAvailable(const QString &cameraId, const QImage &frame);
    void applyConfigRoiToPreview(CameraPreviewWidget *preview);
    QRectF normalizedRoiFromConfig(const QJsonObject &root, const QString &cameraId) const;
    QSize frameSizeFromConfig(const QJsonObject &root, const QString &cameraId, const QSize &previewSize) const;
    QRectF defaultNormalizedRoi(const QSize &frameSize) const;
    bool writeNormalizedRoiToConfig(QJsonObject &root, const QString &cameraId, const QRectF &roi) const;
    QRectF clampNormalizedRect(const QRectF &rect) const;
    QRectF toNormalizedRect(const QRect &pixelRect, const QSize &frameSize) const;
    QRect toPixelRect(const QRectF &normalizedRect, const QSize &frameSize) const;
    QSize displayFrameSizeFor(CameraPreviewWidget *preview, const QSize &fallback) const;

    // Live managers feeding camera, recording, AI, and PLC state into the UI.
    CameraManager *m_cameraManager = nullptr;
    RecordingManager *m_recordingManager = nullptr;
    AiClient *m_aiClient = nullptr;
    PLCClient *m_plcClient = nullptr;

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_gridContainer = nullptr;
    QGridLayout *m_gridLayout = nullptr;
    QWidget *m_bottomBar = nullptr;
    QHBoxLayout *m_filterLayout = nullptr;
    QButtonGroup *m_slotButtonGroup = nullptr;
    QPushButton *m_allFilterButton = nullptr;
    QPushButton *m_recordButton = nullptr;
    TwoCameraSplitter *m_twoCameraSplitter = nullptr;
    double m_twoCameraSplitRatio = 0.5;

    // Cached previews and ordering metadata used when rebuilding the grid.
    QMap<QString, CameraPreviewWidget *> m_previewWidgets;
    QStringList m_cameraOrder;
    QMap<QString, QPushButton *> m_slotButtons;
    QString m_activeSlotFilter;
    QSet<QString> m_recordingCameras;
    QVariantAnimation *m_recordPulseAnimation = nullptr;
    double m_idleRecordGlow = 0.15;
    QMap<QString, bool> m_previousZoomStates;
    QMap<QString, std::optional<QPointF>> m_previousZoomCenters;
    bool m_forceZoomActive = false;
    QString m_currentPassLevel = QStringLiteral("Root");
    double m_currentConfidenceThreshold = 0.0;
    bool m_currentAiEnabled = false;
    bool m_currentPlcEnabled = false;
    double m_detectionDotSizePx = 8.0;
    QMap<QString, double> m_alignmentOffsets;
    QMap<QString, bool> m_cameraAiEnabled;
    bool m_showLegendPreference = true;
    struct AnalysisOverlayState
    {
        QVector<CameraPreviewWidget::AnalysisShape> shapes;
        QRectF normalizedRoi; // stored in [0,1] coordinates
        QSize captureFrameSize;
        QSize displayFrameSize;
    };
    QMap<QString, AnalysisOverlayState> m_overlayStates;
    bool m_pendingViewportRefresh = false;
    QSet<QString> m_pendingCameraRemovals;
};
