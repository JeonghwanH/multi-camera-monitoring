// Author: SeungJae Lee
// CameraPreviewWidget: composite widget combining live video, overlays, and control chrome.

#include "CameraPreviewWidget.h"

#include "utils/DebugConfig.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QCoreApplication>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QMediaCaptureSession>
#include <QHash>
#include <QSize>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <QMargins>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QVideoSink>
#include <QVideoFrame>
#include <QPainterPath>
#include <QRegion>
#include <QTimer>
#include <QtMath>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <optional>

#include "ZoomableVideoView.h"
#include "ToggleSwitch.h"
#include "WeldAlignmentWidget.h"
#include "AnalysisOverlayWidget.h"
#include "AnalysisStatusPanel.h"

namespace
{
// Layout constants for preview geometry and interactions.
constexpr double kMaxAlignmentOffsetMm = 1.0;
constexpr double kAlignmentStepMm = 0.05;
constexpr int kPreviewCornerRadiusPx = 12;
constexpr int kStatusSectionSpacingPx = 3;
const char kOverlayToolButtonStyle[] =
    "QToolButton{background-color:rgba(0,0,0,0.32);border:0px;border-radius:24px;padding:0;}"
    "QToolButton:hover{background-color:rgba(34,255,162,0.24);border:0px;}"
    "QToolButton:pressed{background-color:rgba(34,255,162,0.32);border:0px;}"
    "QToolButton:checked{background-color:rgba(34,255,162,0.32);border:0px;}"
    "QToolButton::menu-indicator{image:none;}";
}

CameraPreviewWidget::CameraPreviewWidget(const QString &cameraId, QWidget *parent)
    : QFrame(parent)
    , m_cameraId(cameraId)
{
    setObjectName(QStringLiteral("cameraPreviewRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#cameraPreviewRoot{background-color:#000000;border-radius:12px;}"));
    setFrameShape(QFrame::NoFrame);
    setFrameShadow(QFrame::Plain);
    setLineWidth(0);
    setMidLineWidth(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    buildUi();
    retranslateUi();

    // Capture session keeps a single pipeline for video and overlays per widget.
    m_captureSession = std::make_unique<QMediaCaptureSession>();
    if (m_videoView)
    {
        m_captureSession->setVideoOutput(m_videoView->videoItem());
        if (auto *sink = m_videoView->videoItem()->videoSink())
        {
            connect(sink, &QVideoSink::videoFrameChanged,
                    this, &CameraPreviewWidget::handleVideoFrameChanged);
        }
    }
}

QString CameraPreviewWidget::cameraId() const
{
    return m_cameraId;
}

void CameraPreviewWidget::updateInfo(const CameraManager::CameraInfo &info)
{
    m_info = info;
    retranslateUi();
    setVisible(info.visible);
}

void CameraPreviewWidget::setCamera(QCamera *camera)
{
    QCamera *previousCamera = m_camera;

    if (previousCamera)
        QObject::disconnect(previousCamera, nullptr, this, nullptr);

    m_camera = camera;

    if (m_captureSession)
    {
        m_captureSession->setCamera(m_camera);
        if (m_videoView)
            m_captureSession->setVideoOutput(m_videoView->videoItem());
    }

    qInfo() << "[CameraPreviewWidget] setCamera" << m_cameraId << m_camera;

    if (m_camera)
    {
        connect(m_camera, &QCamera::errorOccurred, this, [this](QCamera::Error error) {
            qWarning() << "[CameraPreviewWidget] camera error" << m_cameraId << error << m_camera->errorString();
        });
        connect(m_camera, &QCamera::activeChanged, this, [this](bool active) {
            qInfo() << "[CameraPreviewWidget] active" << m_cameraId << active;
        });
        if (DebugConfig::isDebugLoggingEnabled())
        {
            const QCameraDevice device = m_camera->cameraDevice();
            const QString description = device.isNull() ? QStringLiteral("<unknown>") : device.description();
            const QCameraFormat format = m_camera->cameraFormat();
            const QSize resolution = format.isNull() ? QSize() : format.resolution();
            const QString resolutionText = resolution.isValid()
                                               ? QStringLiteral("%1x%2").arg(resolution.width()).arg(resolution.height())
                                               : QStringLiteral("-");
            const double minFps = format.isNull() ? 0.0 : format.minFrameRate();
            const double maxFps = format.isNull() ? 0.0 : format.maxFrameRate();
            qInfo().nospace() << "[CameraPreviewWidget] camera attached id=" << m_cameraId
                              << " device=" << description
                              << " resolution=" << resolutionText
                              << " fpsRange=" << QString::number(minFps, 'f', 2) << "-"
                              << QString::number(maxFps, 'f', 2);
        }
        qInfo() << "[CameraPreviewWidget] starting camera" << m_cameraId;
        m_camera->start();
    }
}

void CameraPreviewWidget::handleVideoFrameChanged(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;

    static QHash<QString, int> s_frameCounters;
    int count = ++s_frameCounters[m_cameraId];
    if (DebugConfig::isDebugLoggingEnabled())
    {
        const bool shouldLog = (count <= 5) || (count % 120 == 0);
        if (shouldLog)
        {
            qInfo().nospace() << "[CameraPreviewWidget] " << m_cameraId
                              << " frame#" << count
                              << " size=" << frame.width() << "x" << frame.height()
                              << " pixelFormat=" << static_cast<int>(frame.pixelFormat());
        }
    }

    const QSize frameSize(frame.width(), frame.height());
    bool sizeChanged = false;

    if (m_videoView)
    {
        if (frameSize.isValid() && frameSize != m_lastFrameSize)
            sizeChanged = true;
        m_videoView->updateVideoItemSize(frameSize);
    }

    if (!m_seenFirstFrame || sizeChanged)
        scheduleZoomSizingUpdate();

    if (frameSize.isValid())
        m_lastFrameSize = frameSize;
    m_seenFirstFrame = true;

    QVideoFrame copy(frame);
    QImage image = copy.toImage();
    if (image.isNull())
        return;

    emit frameAvailable(m_cameraId, image);
}

QMediaCaptureSession *CameraPreviewWidget::captureSession() const
{
    return m_captureSession.get();
}

void CameraPreviewWidget::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_videoContainer = new QWidget(this);
    m_videoContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoContainer->installEventFilter(this);
    auto *videoLayout = new QVBoxLayout(m_videoContainer);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(0);

    m_videoView = new ZoomableVideoView(m_videoContainer);
    m_videoView->setMinimumHeight(480);
    m_videoView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoLayout->addWidget(m_videoView, 1);
    m_videoView->setHorizontalPadding(m_videoPaddingLeft, m_videoPaddingRight);

    connect(m_videoView, &ZoomableVideoView::viewRectChanged, this, [this]() {
        if (m_analysisOverlay)
            m_analysisOverlay->update();
    });

    layout->addWidget(m_videoContainer, 1);

    m_overlayWidget = new QWidget(m_videoContainer);
    m_overlayWidget->setAttribute(Qt::WA_StyledBackground, true);
    m_overlayWidget->setStyleSheet(QStringLiteral("background:transparent;"));
    m_overlayWidget->installEventFilter(this);

    m_analysisOverlay = new AnalysisOverlayWidget(m_overlayWidget);
    m_analysisOverlay->setVideoView(m_videoView);
    m_analysisOverlay->setPointRadius(m_analysisPointSizePx * 0.5);
    m_analysisOverlay->setGeometry(m_overlayWidget->rect());
    m_analysisOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_overlayLayout = new QVBoxLayout(m_overlayWidget);
    m_overlayLayout->setContentsMargins(0, 12, 0, 12);
    m_overlayLayout->setSpacing(0);

    auto *topOverlayRow = new QHBoxLayout();
    topOverlayRow->setContentsMargins(16, 0, 16, 0);
    topOverlayRow->setSpacing(16);

    m_headerBar = new QWidget(m_overlayWidget);
    m_headerBar->setObjectName(QStringLiteral("cameraPreviewHeader"));
    m_headerBar->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    auto *headerLayout = new QHBoxLayout(m_headerBar);
    headerLayout->setContentsMargins(12, 6, 12, 6);
    headerLayout->setSpacing(12);

    m_recordBadge = new QLabel(m_headerBar);
    m_recordBadge->setObjectName(QStringLiteral("cameraPreviewBadge"));
    m_recordBadge->setAlignment(Qt::AlignCenter);
    m_recordBadge->setMinimumWidth(72);
    m_recordBadge->setFixedHeight(28);
    headerLayout->addWidget(m_recordBadge);

    m_headerTitle = new QLabel(m_headerBar);
    m_headerTitle->setObjectName(QStringLiteral("cameraPreviewTitle"));
    m_headerTitle->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_headerTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_headerTitle->setWordWrap(true);
    headerLayout->addWidget(m_headerTitle, 1);

    headerLayout->addStretch();

    m_settingsContainer = new QWidget(m_headerBar);
    m_settingsContainer->setObjectName(QStringLiteral("cameraPreviewSettings"));
    m_settingsContainer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    m_settingsLayout = new QHBoxLayout(m_settingsContainer);
    m_settingsLayout->setContentsMargins(0, 0, 0, 0);
    m_settingsLayout->setSpacing(16);
    m_settingsLayout->setAlignment(Qt::AlignVCenter);
    headerLayout->addWidget(m_settingsContainer);

    topOverlayRow->addWidget(m_headerBar, 1);
    topOverlayRow->addStretch();

    m_zoomButton = new QToolButton(m_overlayWidget);
    m_zoomButton->setAutoRaise(false);
    m_zoomButton->setCursor(Qt::PointingHandCursor);
    m_zoomButton->setIconSize(QSize(40, 40));
    m_zoomButton->setFixedSize(QSize(48, 48));
    m_zoomButton->setFocusPolicy(Qt::NoFocus);
    m_zoomButton->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_zoomButton->setStyleSheet(QString::fromLatin1(kOverlayToolButtonStyle));

    m_roiButton = new QToolButton(m_overlayWidget);
    m_roiButton->setCheckable(true);
    m_roiButton->setAutoRaise(false);
    m_roiButton->setCursor(Qt::PointingHandCursor);
    m_roiButton->setIconSize(QSize(40, 40));
    m_roiButton->setFixedSize(QSize(48, 48));
    m_roiButton->setFocusPolicy(Qt::NoFocus);
    m_roiButton->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_roiButton->setStyleSheet(QString::fromLatin1(kOverlayToolButtonStyle));
    m_roiButton->setIcon(QIcon(QStringLiteral(":/icons/roi.svg")));

    m_overlayLayout->addLayout(topOverlayRow);
    m_overlayLayout->addStretch();
    buildAnalysisOverlay();
    m_overlayWidget->raise();

    connect(m_zoomButton, &QToolButton::clicked, this, [this]() {
        applyZoomState(!m_zoomed);
    });

    if (m_roiButton)
    {
    connect(m_roiButton, &QToolButton::toggled, this, [this](bool checked) {
        if (m_roiOverlayEnabled == checked)
            return;
        m_roiOverlayEnabled = checked;
        refreshOverlayContent();
        emit roiOverlayToggled(m_roiOverlayEnabled);
    });
    }

    applyZoomState(false);
    updateRoiButtonState();
    updateOverlayGeometry();
    updateCornerMask();
}

void CameraPreviewWidget::buildAnalysisOverlay()
{
    if (!m_overlayLayout)
        return;

    auto *bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(16, 0, 16, 0);
    bottomRow->setSpacing(16);

    m_analysisStatusPanel = new AnalysisStatusPanel(m_overlayWidget);
    m_analysisStatusPanel->setAttribute(Qt::WA_TransparentForMouseEvents, false);

    m_analysisStatusPanel->addCustomSection([&](QWidget *section, QVBoxLayout *sectionLayout) {
        auto *rowLayout = new QHBoxLayout();
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto *label = new QLabel(tr("PLC Control"), section);
        label->setStyleSheet(QStringLiteral("background: transparent;color:rgba(255,255,255,0.8);font-size:12px;font-weight:600;"));
        rowLayout->addWidget(label, 0, Qt::AlignVCenter);
        rowLayout->addStretch();

        m_plcToggle = new ToggleSwitch(section);
        rowLayout->addWidget(m_plcToggle, 0, Qt::AlignVCenter);

        sectionLayout->addLayout(rowLayout);
    });

    m_analysisStatusPanel->addCustomSection([&](QWidget *section, QVBoxLayout *sectionLayout) {
        auto *rowLayout = new QHBoxLayout();
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto *label = new QLabel(tr("AI Analysis"), section);
        label->setStyleSheet(QStringLiteral("background: transparent;color:rgba(255,255,255,0.8);font-size:12px;font-weight:600;"));
        rowLayout->addWidget(label, 0, Qt::AlignVCenter);
        rowLayout->addStretch();

        m_aiToggle = new ToggleSwitch(section);
        rowLayout->addWidget(m_aiToggle, 0, Qt::AlignVCenter);

        sectionLayout->addLayout(rowLayout);
    });

    if (m_plcToggle)
    {
        connect(m_plcToggle, &QAbstractButton::toggled, this, [this](bool checked) {
            if (m_plcEnabled == checked)
                return;
            m_plcEnabled = checked;
            updateAnalysisOverlay();
            rebuildSettingIndicators();
            emit plcControlToggled(m_cameraId, checked);
        });
    }

    if (m_aiToggle)
    {
        connect(m_aiToggle, &QAbstractButton::toggled, this, [this](bool checked) {
            if (m_aiEnabled == checked)
                return;
            m_aiEnabled = checked;
            refreshOverlayContent();
            updateAnalysisOverlay();
            rebuildSettingIndicators();
            emit aiAnalysisToggled(m_cameraId, checked);
        });
    }

    bottomRow->addWidget(m_analysisStatusPanel, 0, Qt::AlignLeft | Qt::AlignBottom);
    bottomRow->addStretch();
    if (m_zoomButton || m_roiButton)
    {
        auto *buttonColumnWidget = new QWidget(m_overlayWidget);
        auto *buttonColumnLayout = new QVBoxLayout(buttonColumnWidget);
        buttonColumnLayout->setContentsMargins(0, 0, 0, 0);
        buttonColumnLayout->setSpacing(12);
        buttonColumnLayout->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        if (m_roiButton)
            buttonColumnLayout->addWidget(m_roiButton, 0, Qt::AlignRight);
        if (m_zoomButton)
            buttonColumnLayout->addWidget(m_zoomButton, 0, Qt::AlignRight);
        bottomRow->addWidget(buttonColumnWidget, 0, Qt::AlignRight | Qt::AlignBottom);
    }

    m_overlayLayout->addLayout(bottomRow);

    if (m_analysisOverlay)
        m_analysisOverlay->lower();

    updateAnalysisOverlay();
    updateOverlayChrome();
    refreshOverlayContent();
}

void CameraPreviewWidget::refreshOverlayContent()
{
    if (!m_analysisOverlay)
        return;

    m_analysisOverlay->setVideoView(m_videoView);
    m_analysisOverlay->setNativeSize(m_analysisFrameSize);
    m_analysisOverlay->setShowLegend(m_showLegend);

    const bool hasRoi = m_analysisRoi.width() > 0 && m_analysisRoi.height() > 0;

    m_analysisOverlay->setRoiRect(m_analysisRoi);
    m_analysisOverlay->setRoiVisible(m_roiOverlayEnabled && hasRoi);
    m_analysisOverlay->setSectionsEnabled(m_roiOverlayEnabled && hasRoi);

    QVector<AnalysisOverlayWidget::Shape> overlayShapes;
    if (m_aiEnabled)
    {
        overlayShapes.reserve(m_analysisShapes.size());
        for (const AnalysisShape &shape : m_analysisShapes)
        {
            AnalysisOverlayWidget::Shape converted;
            converted.cls = shape.cls;
            converted.pts512 = shape.pts512;
            overlayShapes.append(converted);
        }
    }

    m_analysisOverlay->setShapes(overlayShapes);

    const bool showOverlay = (m_aiEnabled && !overlayShapes.isEmpty()) || (m_roiOverlayEnabled && hasRoi);
    m_analysisOverlay->setVisible(showOverlay);
    m_analysisOverlay->update();

    updateRoiButtonState();
}

void CameraPreviewWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        QFrame::changeEvent(event);
        retranslateUi();
        return;
    }

    QFrame::changeEvent(event);
}

void CameraPreviewWidget::retranslateUi()
{
    updateHeader();
    updateAnalysisOverlay();
}

void CameraPreviewWidget::setAnalysisPassInfo(const QString &passLabel)
{
    if (m_passLabel.compare(passLabel, Qt::CaseSensitive) == 0)
        return;

    m_passLabel = passLabel;
    updateAnalysisOverlay();
}

void CameraPreviewWidget::setAlignmentOffset(double offsetMm, bool fromUser)
{
    const double clamped = std::clamp(offsetMm, -kMaxAlignmentOffsetMm, kMaxAlignmentOffsetMm);
    if (qFuzzyCompare(m_alignmentOffsetMm + 1.0, clamped + 1.0))
        return;

    m_alignmentOffsetMm = clamped;
    updateAnalysisOverlay();

    if (fromUser)
        emit alignmentOffsetAdjusted(m_cameraId, m_alignmentOffsetMm);
}

void CameraPreviewWidget::setAlignmentWarningThreshold(double thresholdMm)
{
    if (qFuzzyCompare(m_alignmentWarningThreshold + 1.0, thresholdMm + 1.0))
        return;

    m_alignmentWarningThreshold = std::max(0.0, thresholdMm);
    updateAnalysisOverlay();
}

void CameraPreviewWidget::setPlcControlEnabled(bool enabled)
{
    m_plcEnabled = enabled;
    if (m_plcToggle)
    {
        QSignalBlocker blocker(m_plcToggle);
        m_plcToggle->setChecked(enabled);
    }
    updateAnalysisOverlay();
    rebuildSettingIndicators();
}

void CameraPreviewWidget::setAiAnalysisEnabled(bool enabled)
{
    m_aiEnabled = enabled;
    if (m_aiToggle)
    {
        QSignalBlocker blocker(m_aiToggle);
        m_aiToggle->setChecked(enabled);
    }
    refreshOverlayContent();
    updateAnalysisOverlay();
    rebuildSettingIndicators();
}

void CameraPreviewWidget::setAiToggleEnabled(bool enabled)
{
    if (!m_aiToggle)
        return;
    if (m_aiToggle->isEnabled() == enabled)
        return;
    m_aiToggle->setEnabled(enabled);
}

void CameraPreviewWidget::setAiToggleTooltip(const QString &text)
{
    if (!m_aiToggle)
        return;
    if (m_aiToggle->toolTip() == text)
        return;
    m_aiToggle->setToolTip(text);
}

void CameraPreviewWidget::setAnalysisOverlay(const QVector<AnalysisShape> &shapes, const QRect &roi, const QSize &frameSize)
{
    m_analysisShapes = shapes;
    m_analysisRoi = roi;
    m_analysisFrameSize = frameSize;
    refreshOverlayContent();
}

void CameraPreviewWidget::setVideoHorizontalPadding(int left, int right)
{
    left = std::max(0, left);
    right = std::max(0, right);
    if (m_videoPaddingLeft == left && m_videoPaddingRight == right)
        return;
    m_videoPaddingLeft = left;
    m_videoPaddingRight = right;
    if (m_videoView)
        m_videoView->setHorizontalPadding(m_videoPaddingLeft, m_videoPaddingRight);
}

void CameraPreviewWidget::setAnalysisPointSize(double sizePx)
{
    sizePx = std::clamp(sizePx, 1.0, 64.0);
    if (qFuzzyCompare(m_analysisPointSizePx, sizePx))
        return;

    m_analysisPointSizePx = sizePx;
    if (m_analysisOverlay)
        m_analysisOverlay->setPointRadius(m_analysisPointSizePx * 0.5);
}

void CameraPreviewWidget::setShowLegend(bool show)
{
    if (m_showLegend == show)
        return;
    m_showLegend = show;
    if (m_analysisOverlay)
        m_analysisOverlay->setShowLegend(m_showLegend);
}

QSize CameraPreviewWidget::currentFrameSize() const
{
    if (m_videoView && m_videoView->videoItem())
    {
        const QSizeF sizeF = m_videoView->videoItem()->size();
        if (sizeF.width() > 0.0 && sizeF.height() > 0.0)
            return sizeF.toSize();
    }

    if (m_analysisFrameSize.isValid())
        return m_analysisFrameSize;

    return QSize();
}

std::optional<QPointF> CameraPreviewWidget::zoomSceneCenter() const
{
    if (!m_videoView || !m_zoomed)
        return std::nullopt;

    const QRectF rect = m_videoView->visibleSceneRect();
    if (!rect.isValid())
        return std::nullopt;

    return m_videoView->visibleSceneCenter();
}

void CameraPreviewWidget::restoreZoomSceneCenter(const std::optional<QPointF> &center)
{
    if (!m_videoView || !m_zoomed || !center.has_value())
        return;
    m_videoView->restoreVisibleSceneCenter(center.value());
}

void CameraPreviewWidget::updateAnalysisOverlay()
{
    if (m_analysisStatusPanel)
    {
        m_analysisStatusPanel->setPassInfo(m_passLabel);

        std::optional<double> alignmentOpt = m_alignmentOffsetMm;
        std::optional<double> thresholdOpt;
        if (m_alignmentWarningThreshold > 0.0)
            thresholdOpt = m_alignmentWarningThreshold;
        m_analysisStatusPanel->setAlignmentOffset(alignmentOpt, thresholdOpt);
    }

    if (m_plcToggle)
    {
        QSignalBlocker blocker(m_plcToggle);
        m_plcToggle->setChecked(m_plcEnabled);
    }

    if (m_aiToggle)
    {
        QSignalBlocker blocker(m_aiToggle);
        m_aiToggle->setChecked(m_aiEnabled);
    }
}

void CameraPreviewWidget::keyPressEvent(QKeyEvent *event)
{
    const bool isResetShortcut = (event->key() == Qt::Key_0) &&
        (event->modifiers().testFlag(Qt::MetaModifier) || event->modifiers().testFlag(Qt::ControlModifier));
    if (isResetShortcut)
    {
        setAlignmentOffset(0.0, true);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_A)
    {
        setAlignmentOffset(m_alignmentOffsetMm - kAlignmentStepMm, true);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Right || event->key() == Qt::Key_D)
    {
        setAlignmentOffset(m_alignmentOffsetMm + kAlignmentStepMm, true);
        event->accept();
        return;
    }

    QFrame::keyPressEvent(event);
}

void CameraPreviewWidget::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    updateCornerMask();
    updateOverlayGeometry();
}

bool CameraPreviewWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_videoContainer && event->type() == QEvent::Resize)
        updateOverlayGeometry();

    if (watched == m_overlayWidget)
    {
        switch (event->type())
        {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (!m_overlayWidget->childAt(mouseEvent->position().toPoint()))
            {
                forwardMouseEventToVideoView(mouseEvent);
                return true;
            }
            break;
        }
        case QEvent::Wheel:
        {
            auto *wheelEvent = static_cast<QWheelEvent *>(event);
            if (!m_overlayWidget->childAt(wheelEvent->position().toPoint()))
            {
                forwardWheelEventToVideoView(wheelEvent);
                return true;
            }
            break;
        }
        default:
            break;
        }
    }

    return QFrame::eventFilter(watched, event);
}

void CameraPreviewWidget::setRecordingActive(bool recording)
{
    if (m_recordingActive == recording)
        return;

    m_recordingActive = recording;
    updateRecordingBadge();
}

bool CameraPreviewWidget::isZoomed() const
{
    return m_zoomed;
}

void CameraPreviewWidget::setViewportContext(QWidget *viewportWidget, bool fillViewportWidth, const QMargins &outerMargins)
{
    m_viewportWidget = viewportWidget;
    m_fillViewportWidth = fillViewportWidth;
    m_outerMargins = outerMargins;
    updateZoomSizing();
    scheduleZoomSizingUpdate();
}

void CameraPreviewWidget::setZoomControlsVisible(bool visible)
{
    if (m_zoomControlsVisible == visible)
        return;
    m_zoomControlsVisible = visible;
    if (m_zoomButton)
        m_zoomButton->setVisible(visible);
    if (m_roiButton)
        m_roiButton->setVisible(visible);
}

void CameraPreviewWidget::setZoomState(bool zoomed, bool notify)
{
    if (notify)
    {
        applyZoomState(zoomed);
        return;
    }

    if (m_zoomed == zoomed)
    {
        updateZoomSizing();
        scheduleZoomSizingUpdate();
        return;
    }

    QSignalBlocker blocker(this);
    applyZoomState(zoomed);
    scheduleZoomSizingUpdate();
}

void CameraPreviewWidget::updateHeader()
{
    if (!m_headerTitle)
        return;

    QString headerText;
    const QString alias = m_info.alias.trimmed();
    if (!alias.isEmpty())
    {
        headerText = alias;
    }
    else
    {
        const QString slotName = m_info.slotId;
        const QString cameraName = m_info.name.isEmpty() ? m_cameraId : m_info.name;
        if (!slotName.isEmpty() && !cameraName.isEmpty())
            headerText = tr("[%1] %2").arg(slotName, cameraName);
        else if (!slotName.isEmpty())
            headerText = slotName;
        else
            headerText = cameraName;
    }

    m_headerTitle->setText(headerText.trimmed());

    updateRecordingBadge();
    rebuildSettingIndicators();
}

void CameraPreviewWidget::updateRecordingBadge()
{
    if (!m_recordBadge)
        return;

    const bool isRecording = m_recordingActive;
    const QString badgeText = QStringLiteral("%1 %2").arg(QChar(0x25CF), isRecording ? tr("REC") : tr("LIVE"));
    const QString backgroundColor = isRecording ? QStringLiteral("#E5484D") : QStringLiteral("#FFFFFF");
    const QString textColor = isRecording ? QStringLiteral("#FFFFFF") : QStringLiteral("#E5484D");

    m_recordBadge->setText(badgeText);
    m_recordBadge->setStyleSheet(QStringLiteral(
        "#cameraPreviewBadge{background:%1;color:%2;font-size:13px;font-weight:600;padding:0 12px;border-radius:3px;}"
    ).arg(backgroundColor, textColor));
}

void CameraPreviewWidget::rebuildSettingIndicators()
{
    if (!m_settingsLayout || !m_settingsContainer)
        return;

    while (auto *item = m_settingsLayout->takeAt(0))
    {
        if (auto *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    if (m_info.settings.isEmpty())
    {
        m_settingsContainer->setVisible(false);
        return;
    }

    m_settingsContainer->setVisible(true);

    for (const auto &setting : m_info.settings)
    {
        auto *itemWidget = new QWidget(m_settingsContainer);
        itemWidget->setObjectName(QStringLiteral("cameraPreviewSettingItem"));
        itemWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        auto *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(6);

        bool enabled = setting.enabled;
        const QString aiLabel = tr("AI Analysis");
        const QString plcLabel = tr("PLC Control");
        if (setting.name.compare(aiLabel, Qt::CaseInsensitive) == 0)
            enabled = m_aiEnabled;
        else if (setting.name.compare(plcLabel, Qt::CaseInsensitive) == 0)
            enabled = m_plcEnabled;

        auto *indicator = new QWidget(itemWidget);
        indicator->setFixedSize(10, 10);
        indicator->setStyleSheet(QStringLiteral("background:%1;border-radius:5px;")
                                     .arg(enabled ? QStringLiteral("#00FFB7") : QStringLiteral("#696E77")));
        itemLayout->addWidget(indicator);

        auto *label = new QLabel(setting.name, itemWidget);
        label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("color:#FFFFFF;font-size:13px;font-weight:500;"));
        itemLayout->addWidget(label);

        m_settingsLayout->addWidget(itemWidget);
    }
}

void CameraPreviewWidget::updateRoiButtonState()
{
    if (!m_roiButton)
        return;

    const bool hasRoi = m_analysisRoi.width() > 0 && m_analysisRoi.height() > 0;

    {
        QSignalBlocker blocker(m_roiButton);
        m_roiButton->setChecked(m_roiOverlayEnabled);
    }

    const QString tooltip = hasRoi
        ? (m_roiOverlayEnabled ? tr("Hide ROI") : tr("Show ROI"))
        : tr("ROI unavailable");
    m_roiButton->setToolTip(tooltip);
    if (!m_roiButton->isEnabled())
        m_roiButton->setEnabled(true);
}

void CameraPreviewWidget::updateZoomButtonIcon()
{
    if (!m_zoomButton)
        return;

    const QString iconPath = m_zoomed ? QStringLiteral(":/icons/wd_cp_zoomout.svg") : QStringLiteral(":/icons/wd_cp_zoomin.svg");
    m_zoomButton->setIcon(QIcon(iconPath));
    m_zoomButton->setToolTip(m_zoomed ? tr("Zoom out") : tr("Zoom in"));
}

void CameraPreviewWidget::applyZoomState(bool zoomed)
{
    const bool changed = (m_zoomed != zoomed);
    m_zoomed = zoomed;
    if (m_videoView)
        m_videoView->setZoomed(m_zoomed);
    updateZoomSizing();
    updateZoomButtonIcon();
    if (changed)
        emit zoomToggled(m_zoomed);
}

void CameraPreviewWidget::updateOverlayGeometry()
{
    if (!m_overlayWidget || !m_videoContainer)
        return;

    m_overlayWidget->setGeometry(m_videoContainer->rect());
    m_overlayWidget->raise();
    if (m_analysisOverlay)
    {
        m_analysisOverlay->setGeometry(m_overlayWidget->rect());
        m_analysisOverlay->update();
    }
}

void CameraPreviewWidget::updateOverlayChrome()
{
    if (m_headerBar)
    {
        const QString headerStyle = QStringLiteral(
            "#cameraPreviewHeader{background:transparent;border-radius:12px;}"
            "#cameraPreviewTitle{color:#FFFFFF;font-size:15px;font-weight:600;}"
            "#cameraPreviewSettings QLabel{color:#FFFFFF;font-size:13px;font-weight:500;}"
        );
        m_headerBar->setStyleSheet(headerStyle);
    }

    if (m_analysisStatusPanel)
    {
        const QString statusStyle = QStringLiteral(
            "QWidget#analysisStatusPanel{background:transparent;border-radius:12px;}"
            "QWidget#analysisStatusPanel QWidget#analysisSection{background:rgba(34,255,162,0.12);border-radius:10px;}"
            "QWidget#analysisStatusPanel QLabel{color:#FFFFFF;}"
        );
        m_analysisStatusPanel->setStyleSheet(statusStyle);
    }
}

void CameraPreviewWidget::updateCornerMask()
{
    if (width() <= 0 || height() <= 0)
    {
        setMask(QRegion());
        return;
    }

    QPainterPath roundedPath;
    roundedPath.addRoundedRect(rect(), kPreviewCornerRadiusPx, kPreviewCornerRadiusPx);
    const QRegion roundedRegion = QRegion(roundedPath.toFillPolygon().toPolygon());
    setMask(roundedRegion);
}

void CameraPreviewWidget::updateZoomSizing()
{
    if (!m_videoContainer || !m_videoView)
        return;

    if (!m_viewportWidget)
    {
        m_videoContainer->setMinimumSize(QSize(0, 0));
        m_videoContainer->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        m_videoView->setMinimumSize(QSize(0, 480));
        m_videoView->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        updateOverlayGeometry();
        return;
    }

    if (!m_zoomed)
    {
        if (m_fillViewportWidth)
        {
            const int viewportWidth = std::max(0, m_viewportWidget->width() - (m_outerMargins.left() + m_outerMargins.right()));
            const int viewportHeight = std::max(0, m_viewportWidget->height() - (m_outerMargins.top() + m_outerMargins.bottom()));

            QSizeF frameSize = QSizeF(16.0, 9.0);
            if (auto *videoItem = m_videoView->videoItem())
            {
                const QSizeF native = videoItem->size();
                if (native.width() > 0.0 && native.height() > 0.0)
                    frameSize = native;
            }

            const double aspectRatio = frameSize.width() > 0.0 && frameSize.height() > 0.0
                ? frameSize.width() / frameSize.height()
                : 16.0 / 9.0;

            int targetWidth = viewportWidth;
            int targetHeight = static_cast<int>(std::round(targetWidth / aspectRatio));

            if (viewportHeight > 0 && targetHeight > viewportHeight)
            {
                targetHeight = viewportHeight;
                targetWidth = static_cast<int>(std::round(targetHeight * aspectRatio));
            }

            targetWidth = std::max(targetWidth, 0);
            targetHeight = std::max(targetHeight, 0);

            if (targetHeight > 0 && targetWidth > 0)
            {
                const QSize targetSize(targetWidth, targetHeight);
                m_videoContainer->setMinimumSize(targetSize);
                m_videoContainer->setMaximumSize(targetSize);
                m_videoView->setMinimumSize(targetSize);
                m_videoView->setMaximumSize(targetSize);
            }
            else
            {
                m_videoContainer->setMinimumSize(QSize(0, 0));
                m_videoContainer->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
                m_videoView->setMinimumSize(QSize(0, 480));
                m_videoView->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
            }
        }
        else
        {
            m_videoContainer->setMinimumSize(QSize(0, 0));
            m_videoContainer->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
            m_videoView->setMinimumSize(QSize(0, 480));
            m_videoView->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        }

        updateOverlayGeometry();
        return;
    }

    QSize viewportSize = m_viewportWidget->size();
    int horizontalPadding = m_outerMargins.left() + m_outerMargins.right();
    int verticalPadding = m_outerMargins.top() + m_outerMargins.bottom();

    if (auto *outerLayout = layout())
    {
        const QMargins innerMargins = outerLayout->contentsMargins();
        horizontalPadding += innerMargins.left() + innerMargins.right();
        verticalPadding += innerMargins.top() + innerMargins.bottom();
    }

    int availableWidth = std::max(0, viewportSize.width() - horizontalPadding);
    int availableHeight = std::max(0, viewportSize.height() - verticalPadding);

    int minWidth = m_fillViewportWidth ? availableWidth : 0;
    int minHeight = availableHeight;

    minHeight = std::max(minHeight, 0);

    m_videoContainer->setMinimumSize(minWidth, minHeight);
    m_videoContainer->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    m_videoView->setMinimumSize(minWidth, minHeight);
    m_videoView->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    updateOverlayGeometry();
}

void CameraPreviewWidget::scheduleZoomSizingUpdate()
{
    if (m_pendingZoomSizingUpdate)
        return;

    m_pendingZoomSizingUpdate = true;
    QTimer::singleShot(0, this, [this]() {
        m_pendingZoomSizingUpdate = false;
        updateZoomSizing();
    });
}

void CameraPreviewWidget::forwardMouseEventToVideoView(QMouseEvent *event)
{
    if (!m_videoView || !m_overlayWidget)
        return;

    QWidget *viewport = m_videoView->viewport();
    if (!viewport)
        return;

    const QPoint globalPos = m_overlayWidget->mapToGlobal(event->position().toPoint());
    const QPoint viewportPos = viewport->mapFromGlobal(globalPos);
    const QPointF localPos = viewportPos;
    const QPointF scenePos = m_videoView->mapToScene(viewportPos);
    const QPointF globalPosF = globalPos;

    QMouseEvent forwarded(event->type(), localPos, scenePos, globalPosF, event->button(),
                          event->buttons(), event->modifiers(), event->source());
    QCoreApplication::sendEvent(viewport, &forwarded);

    if (!hasFocus())
        setFocus(Qt::MouseFocusReason);
}

void CameraPreviewWidget::forwardWheelEventToVideoView(QWheelEvent *event)
{
    if (!m_videoView || !m_overlayWidget)
        return;

    QWidget *viewport = m_videoView->viewport();
    if (!viewport)
        return;

    const QPoint globalPos = m_overlayWidget->mapToGlobal(event->position().toPoint());
    const QPoint viewportPos = viewport->mapFromGlobal(globalPos);
    const QPointF localPos = viewportPos;
    const QPointF globalPosF = globalPos;

    QWheelEvent forwarded(localPos, globalPosF, event->pixelDelta(), event->angleDelta(),
                          event->buttons(), event->modifiers(), event->phase(),
                          event->inverted(), event->source());
    QCoreApplication::sendEvent(viewport, &forwarded);
}
