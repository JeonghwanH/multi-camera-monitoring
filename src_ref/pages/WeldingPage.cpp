// Author: SeungJae Lee
// WeldingPage: orchestrates camera previews, recording controls, and AI/PLC state.

#include "WeldingPage.h"

#include "managers/RecordingManager.h"
#include "managers/AiClient.h"
#include "managers/PLCClient.h"
#include "widgets/CameraPreviewWidget.h"
#include "widgets/TwoCameraSplitter.h"
#include "utils/ConfigUtils.h"
#include "utils/DebugConfig.h"

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCamera>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QPair>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QMargins>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <QHash>
#include <QSet>
#include <QDebug>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

WeldingPage::WeldingPage(CameraManager *cameraManager, RecordingManager *recordingManager,
                         AiClient *aiClient, PLCClient *plcClient, QWidget *parent)
    : QWidget(parent)
    , m_cameraManager(cameraManager)
    , m_recordingManager(recordingManager)
    , m_aiClient(aiClient)
    , m_plcClient(plcClient)
{
    buildUi();

    m_showLegendPreference = ConfigUtils::showLegend();

    // Connect back-end managers so that widget state tracks camera/AI/PLC events in real time.
    if (m_cameraManager)
    {
        connect(m_cameraManager, &CameraManager::cameraAdded, this, &WeldingPage::addCameraWidget);
        connect(m_cameraManager, &CameraManager::cameraRemoved, this, &WeldingPage::removeCameraWidget);
        connect(m_cameraManager, &CameraManager::cameraUpdated, this, &WeldingPage::updateCameraWidget);
        connect(m_cameraManager, &CameraManager::cameraVisibilityChanged, this, &WeldingPage::updateCameraVisibility);
        connect(m_cameraManager, &CameraManager::cameraHandleAssigned, this, [this](const QString &cameraId, QCamera *camera) {
            qInfo() << "[WeldingPage] camera handle assigned" << cameraId << camera;
            auto *widget = m_previewWidgets.value(cameraId, nullptr);
            if (widget)
            {
                qInfo() << "[WeldingPage] setCamera on preview" << cameraId << camera;
                widget->setCamera(camera);
            }
        });
    }

    if (m_aiClient)
    {
        const auto normalizePassLevel = [](const QString &value) -> QString {
            const QString trimmed = value.trimmed();
            if (trimmed.compare(QStringLiteral("Second"), Qt::CaseInsensitive) == 0)
                return QStringLiteral("Second");
            if (trimmed.compare(QStringLiteral("Root"), Qt::CaseInsensitive) == 0)
                return QStringLiteral("Root");
            return trimmed.isEmpty() ? QStringLiteral("Root") : trimmed;
        };

        const auto settings = m_aiClient->settings();
        m_currentAiEnabled = settings.enableAnalysis;
        m_currentPassLevel = normalizePassLevel(settings.passLevel);
        m_currentConfidenceThreshold = settings.confidenceThreshold;
        m_detectionDotSizePx = std::clamp(settings.detectionDotSizePx, 1.0, 64.0);

        connect(m_aiClient, &AiClient::settingsChanged, this, [this, normalizePassLevel](const AiClient::Settings &settings) {
            m_currentAiEnabled = settings.enableAnalysis;
            m_currentPassLevel = normalizePassLevel(settings.passLevel);
            m_currentConfidenceThreshold = settings.confidenceThreshold;
            m_detectionDotSizePx = std::clamp(settings.detectionDotSizePx, 1.0, 64.0);
            updateAllPreviewAnalysisStates();
        });
    }

    if (m_plcClient)
    {
        m_currentPlcEnabled = m_plcClient->isConnected();
        connect(m_plcClient, &PLCClient::connectionStateChanged, this,
                [this](const QString &, PLCClient::State state) {
                    if (state == PLCClient::State::Connected)
                        m_currentPlcEnabled = true;
                    else if (state == PLCClient::State::Disconnected || state == PLCClient::State::Error)
                        m_currentPlcEnabled = false;
                    updateAllPreviewAnalysisStates();
                });
    }

    if (m_recordingManager)
    {
        connect(m_recordingManager, &RecordingManager::recordingStarted, this, [this](const QString &cameraId) {
            m_recordingCameras.insert(cameraId);
            if (auto *widget = m_previewWidgets.value(cameraId, nullptr))
                widget->setRecordingActive(true);
            updateRecordButtonState();
        });
        connect(m_recordingManager, &RecordingManager::recordingStopped, this, [this](const QString &cameraId) {
            m_recordingCameras.remove(cameraId);
            if (auto *widget = m_previewWidgets.value(cameraId, nullptr))
                widget->setRecordingActive(false);
            if (m_pendingCameraRemovals.contains(cameraId))
                finalizeCameraWidgetRemoval(cameraId);
            updateRecordButtonState();
        });
    }

    if (m_cameraManager)
    {
        const auto cameras = m_cameraManager->cameras();
        for (const auto &info : cameras)
            addCameraWidget(info);
    }

    rebuildSlotFilters();
    updateAllPreviewAnalysisStates();
}

void WeldingPage::buildUi()
{
    // Scrollable grid of previews with a persistent bottom control bar.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setFrameShadow(QFrame::Plain);
    m_scrollArea->setLineWidth(0);

    m_gridContainer = new QWidget(m_scrollArea);
    m_gridContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_gridLayout = new QGridLayout(m_gridContainer);
    m_gridLayout->setSpacing(12);
    m_gridLayout->setContentsMargins(12, 0, 4, 12);

    m_gridContainer->setLayout(m_gridLayout);
    m_scrollArea->setWidget(m_gridContainer);
    if (auto *viewport = m_scrollArea->viewport())
        viewport->installEventFilter(this);

    layout->addWidget(m_scrollArea);

    m_bottomBar = new QWidget(this);
    m_bottomBar->setObjectName(QStringLiteral("weldingBottomBar"));
    m_bottomBar->setFixedHeight(72);
    m_bottomBar->setStyleSheet(QStringLiteral(
        "QWidget#weldingBottomBar{"
        " background:rgba(17,17,19,1.0);"
        " border:1px solid rgba(33,34,37,1.0);"
        " border-radius:16px;"
        "}"
    )) ;

    auto *bottomLayout = new QHBoxLayout(m_bottomBar);
    bottomLayout->setContentsMargins(12, 12, 12, 12);
    bottomLayout->setSpacing(12);

    m_slotButtonGroup = new QButtonGroup(this);
    m_slotButtonGroup->setExclusive(true);
    m_filterLayout = new QHBoxLayout();
    m_filterLayout->setContentsMargins(0, 0, 0, 0);
    m_filterLayout->setSpacing(12);
    m_filterLayout->setAlignment(Qt::AlignVCenter);

    auto *filterContainer = new QWidget(m_bottomBar);
    filterContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    filterContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *filterContainerLayout = new QHBoxLayout(filterContainer);
    filterContainerLayout->setContentsMargins(0, 0, 0, 0);
    filterContainerLayout->setSpacing(0);
    filterContainerLayout->addLayout(m_filterLayout);
    filterContainerLayout->setAlignment(m_filterLayout, Qt::AlignVCenter);

    m_allFilterButton = new QPushButton(tr("ALL"), m_bottomBar);
    m_allFilterButton->setCheckable(true);
    m_allFilterButton->setChecked(true);
    m_allFilterButton->setProperty("slotId", QString());
    styleSlotButton(m_allFilterButton);
    m_slotButtonGroup->addButton(m_allFilterButton);
    m_filterLayout->addWidget(m_allFilterButton, 0, Qt::AlignVCenter);

    bottomLayout->addWidget(filterContainer);
    bottomLayout->setAlignment(filterContainer, Qt::AlignVCenter);
    bottomLayout->addStretch();

    m_recordButton = new QPushButton(m_bottomBar);
    m_recordButton->setCheckable(true);
    m_recordButton->setEnabled(false);
    styleRecordButton();
    bottomLayout->addWidget(m_recordButton);

    auto *bottomContainer = new QWidget(this);
    auto *bottomContainerLayout = new QHBoxLayout(bottomContainer);
    bottomContainerLayout->setContentsMargins(12, 0, 0, 0);
    bottomContainerLayout->setSpacing(0);
    bottomContainerLayout->addWidget(m_bottomBar);
    layout->addWidget(bottomContainer);

    connect(m_slotButtonGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this, [this](QAbstractButton *button) {
        if (!button)
            return;
        const QString slotId = button->property("slotId").toString();
        applySlotFilter(slotId);
    });

    connect(m_recordButton, &QPushButton::toggled, this, &WeldingPage::onRecordButtonToggled);
}

void WeldingPage::addCameraWidget(const CameraManager::CameraInfo &info)
{
    if (m_previewWidgets.contains(info.id))
        return;

    auto *preview = new CameraPreviewWidget(info.id, m_gridContainer);
    preview->updateInfo(info);
    preview->setCamera(m_cameraManager->cameraHandle(info.id));
    preview->setRecordingActive(m_recordingCameras.contains(info.id));
    m_previewWidgets.insert(info.id, preview);
    m_cameraOrder.append(info.id);

    // Forward preview-level capture actions back to managers.
    connect(preview, &CameraPreviewWidget::startRecordingRequested, this, [this](const QString &cameraId) {
        if (!m_cameraManager || !m_recordingManager)
            return;
        const auto info = m_cameraManager->camera(cameraId);
        auto cameraHandle = m_cameraManager->cameraHandle(cameraId);
        auto *previewWidget = m_previewWidgets.value(cameraId, nullptr);
        auto *captureSession = previewWidget ? previewWidget->captureSession() : nullptr;
        m_recordingManager->startRecording(cameraId, cameraHandle, captureSession, info.slotId, m_currentPassLevel);
    });

    connect(preview, &CameraPreviewWidget::stopRecordingRequested, this, [this](const QString &cameraId) {
        if (!m_recordingManager)
            return;
        m_recordingManager->stopRecording(cameraId);
    });

    connect(preview, &CameraPreviewWidget::snapshotRequested, this, [this](const QString &cameraId) {
        if (!m_cameraManager || !m_recordingManager)
            return;
        const auto info = m_cameraManager->camera(cameraId);
        auto cameraHandle = m_cameraManager->cameraHandle(cameraId);
        m_recordingManager->captureSnapshot(cameraId, cameraHandle, info.slotId);
    });

    connect(preview, &CameraPreviewWidget::zoomToggled, this, [this](bool) {
        rebuildGrid();
    });

    connectPreviewSignals(preview);
    connect(preview, &CameraPreviewWidget::frameAvailable, this, &WeldingPage::handleFrameAvailable);
    updatePreviewAnalysisState(preview);
    if (m_overlayStates.contains(info.id))
    {
        const AnalysisOverlayState state = m_overlayStates.value(info.id);
        if (state.normalizedRoi.isValid() || !state.shapes.isEmpty())
        {
            QSize frameSize = preview->currentFrameSize();
            if (!frameSize.isValid() || frameSize.isEmpty())
                frameSize = state.displayFrameSize;
            if (!frameSize.isValid() || frameSize.isEmpty())
                frameSize = state.captureFrameSize;
            if (frameSize.isValid() && frameSize.width() > 0 && frameSize.height() > 0)
            {
                const QRect roiPx = toPixelRect(state.normalizedRoi, frameSize);
                preview->setAnalysisOverlay(state.shapes, roiPx, frameSize);
            }
        }
    }

    rebuildSlotFilters();
    scheduleViewportRefresh();
}

void WeldingPage::removeCameraWidget(const QString &cameraId)
{
    auto it = m_previewWidgets.find(cameraId);
    if (it == m_previewWidgets.end())
        return;

    CameraPreviewWidget *widget = it.value();
    const bool isRecording = m_recordingCameras.contains(cameraId);

    if (isRecording)
    {
        if (!m_recordingManager)
        {
            finalizeCameraWidgetRemoval(cameraId);
            return;
        }
        if (m_recordingManager && !m_pendingCameraRemovals.contains(cameraId))
        {
            m_pendingCameraRemovals.insert(cameraId);
            widget->hide();
            widget->setEnabled(false);
            m_recordingManager->stopRecording(cameraId);
            scheduleViewportRefresh();
        }
        return;
    }

    finalizeCameraWidgetRemoval(cameraId);
}

void WeldingPage::finalizeCameraWidgetRemoval(const QString &cameraId)
{
    auto it = m_previewWidgets.find(cameraId);
    CameraPreviewWidget *widget = (it == m_previewWidgets.end()) ? nullptr : it.value();
    if (it != m_previewWidgets.end())
        m_previewWidgets.erase(it);

    m_pendingCameraRemovals.remove(cameraId);

    if (widget)
    {
        widget->hide();
        widget->setCamera(nullptr);
        widget->deleteLater();
    }

    m_cameraOrder.removeAll(cameraId);
    m_recordingCameras.remove(cameraId);
    m_alignmentOffsets.remove(cameraId);
    m_cameraAiEnabled.remove(cameraId);
    m_overlayStates.remove(cameraId);
    m_previousZoomStates.remove(cameraId);
    m_previousZoomCenters.remove(cameraId);
    rebuildSlotFilters();
    scheduleViewportRefresh();
}

void WeldingPage::updateCameraWidget(const CameraManager::CameraInfo &info)
{
    auto *widget = m_previewWidgets.value(info.id, nullptr);
    if (!widget)
        return;

    widget->updateInfo(info);
    widget->setRecordingActive(m_recordingCameras.contains(info.id));
    updatePreviewAnalysisState(widget);
    rebuildSlotFilters();
}

void WeldingPage::updateCameraVisibility(const QString &cameraId, bool visible)
{
    auto *widget = m_previewWidgets.value(cameraId, nullptr);
    if (!widget)
        return;

    if (m_cameraManager && m_cameraManager->hasCamera(cameraId))
        widget->updateInfo(m_cameraManager->camera(cameraId));
    rebuildGrid();
    updateRecordButtonState();
}

void WeldingPage::updateAnalysisMetrics(const QString &cameraId, double weldPoolWidthMm)
{
    Q_UNUSED(cameraId);
    Q_UNUSED(weldPoolWidthMm);
}

void WeldingPage::setAnalysisOverlay(const QString &cameraId,
                                     const QVector<CameraPreviewWidget::AnalysisShape> &shapes,
                                     const QRect &roi,
                                     const QSize &frameSize)
{
    if (cameraId.isEmpty())
        return;

    AnalysisOverlayState &state = m_overlayStates[cameraId];

    state.shapes = shapes;
    CameraPreviewWidget *preview = m_previewWidgets.value(cameraId, nullptr);

    if (frameSize.isValid() && frameSize.width() > 0 && frameSize.height() > 0)
        state.captureFrameSize = frameSize;

    const bool roiProvided = roi.isValid() && roi.width() > 0 && roi.height() > 0;
    if (roiProvided)
    {
        QSize basis = state.captureFrameSize;
        if (!basis.isValid() || basis.isEmpty())
            basis = frameSize;
        if (!basis.isValid() || basis.isEmpty())
            basis = displayFrameSizeFor(preview, state.displayFrameSize);
        if (!basis.isValid() || basis.isEmpty())
            basis = QSize(1920, 1080);

        if (basis.isValid() && basis.width() > 0 && basis.height() > 0)
        {
            const QRectF normalized = toNormalizedRect(roi, basis);
            const bool shouldUpdateNormalized = !state.normalizedRoi.isValid();
            if (normalized.isValid() && shouldUpdateNormalized)
                state.normalizedRoi = normalized;
        }
    }

    if (preview)
    {
        QSize displaySize = preview->currentFrameSize();
        if (!displaySize.isValid() || displaySize.isEmpty())
            displaySize = state.displayFrameSize;
        if (!displaySize.isValid() || displaySize.isEmpty())
            displaySize = state.captureFrameSize;
        if (!displaySize.isValid() || displaySize.isEmpty())
            displaySize = frameSize;
        if (!displaySize.isValid() || displaySize.isEmpty())
            displaySize = QSize(1920, 1080);

        state.displayFrameSize = displaySize;

        const QRect roiForDisplay = state.normalizedRoi.isValid()
            ? toPixelRect(state.normalizedRoi, state.displayFrameSize)
            : QRect();
        preview->setAnalysisOverlay(state.shapes, roiForDisplay, state.displayFrameSize);
    }
}

void WeldingPage::applyPlcControlData(const QString &cameraId,
                                      const std::optional<double> &deviationMm,
                                      const std::optional<double> &warningThresholdMm)
{
    if (cameraId.isEmpty())
        return;

    if (deviationMm.has_value())
    {
        m_alignmentOffsets.insert(cameraId, deviationMm.value());
        if (auto *preview = m_previewWidgets.value(cameraId, nullptr))
            preview->setAlignmentOffset(deviationMm.value());

        if (m_plcClient && m_plcClient->isConnected())
        {
            QString plcError;
            if (!m_plcClient->sendDeviation(deviationMm.value(), &plcError))
            {
                qWarning() << "[WeldingPage] Failed to send deviation to PLC" << plcError;
            }
            else if (DebugConfig::isDebugLoggingEnabled())
            {
                qInfo() << "[WeldingPage] Sent deviation to PLC" << deviationMm.value();
            }
        }
    }

    if (warningThresholdMm.has_value())
    {
        const double threshold = std::max(0.0, warningThresholdMm.value());
        if (auto *preview = m_previewWidgets.value(cameraId, nullptr))
            preview->setAlignmentWarningThreshold(threshold);
    }
}

void WeldingPage::rebuildGrid()
{
    // Recalculate layout each time visibility/zoom/filter changes; handles splitter mode and grid mode.
    QLayoutItem *child;
    while ((child = m_gridLayout->takeAt(0)) != nullptr)
    {
        if (child->widget())
            child->widget()->hide();
        delete child;
    }

    if (m_twoCameraSplitter)
    {
        const QList<int> sizes = m_twoCameraSplitter->sizes();
        if (sizes.size() >= 2)
        {
            const int total = sizes.at(0) + sizes.at(1);
            if (total > 0)
                m_twoCameraSplitRatio = static_cast<double>(sizes.at(0)) / static_cast<double>(total);
        }

        while (m_twoCameraSplitter->count() > 0)
        {
            if (auto *splitChild = m_twoCameraSplitter->widget(0))
            {
                splitChild->hide();
                splitChild->setParent(m_gridContainer);
            }
        }

        m_twoCameraSplitter->hide();
    }

    QList<CameraPreviewWidget *> orderedWidgets;
    orderedWidgets.reserve(m_cameraOrder.size());
    for (const auto &cameraId : m_cameraOrder)
    {
        auto *widget = m_previewWidgets.value(cameraId, nullptr);
        if (widget)
            orderedWidgets.append(widget);
    }

    struct LayoutEntry
    {
        CameraPreviewWidget *widget = nullptr;
        bool shouldShow = false;
    };

    QVector<LayoutEntry> layoutEntries;
    layoutEntries.reserve(orderedWidgets.size());

    int visibleCount = 0;
    bool anyZoomed = false;

    for (auto *widget : orderedWidgets)
    {
        const QString cameraId = widget->cameraId();

        bool managerVisible = true;
        QString slotId;

        if (m_cameraManager && m_cameraManager->hasCamera(cameraId))
        {
            const auto info = m_cameraManager->camera(cameraId);
            managerVisible = info.visible;
            slotId = info.slotId;
        }

        bool slotMatches = m_activeSlotFilter.isEmpty();
        if (!m_activeSlotFilter.isEmpty())
            slotMatches = (slotId == m_activeSlotFilter);

        bool shouldShow = managerVisible && slotMatches;
        if (!m_cameraManager)
            shouldShow = slotMatches || m_activeSlotFilter.isEmpty();

        if (shouldShow)
        {
            ++visibleCount;
            anyZoomed = anyZoomed || widget->isZoomed();
        }

        layoutEntries.append({widget, shouldShow});
    }

    if (visibleCount == 0)
    {
        updatePreviewViewportContexts(visibleCount);
        if (m_scrollArea)
        {
            m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            m_gridContainer->setMinimumHeight(0);
        }
        m_gridLayout->setRowStretch(0, 1);
        return;
    }

    if (visibleCount == 2)
    {
        if (!m_twoCameraSplitter)
        {
            m_twoCameraSplitter = new TwoCameraSplitter(m_gridContainer);
            m_twoCameraSplitter->setObjectName(QStringLiteral("twoCameraSplitter"));
            connect(m_twoCameraSplitter, &QSplitter::splitterMoved, this, [this]() {
                if (!m_twoCameraSplitter)
                    return;
                const QList<int> sizes = m_twoCameraSplitter->sizes();
                if (sizes.size() >= 2)
                {
                    const int total = sizes.at(0) + sizes.at(1);
                    if (total > 0)
                        m_twoCameraSplitRatio = static_cast<double>(sizes.at(0)) / static_cast<double>(total);
                }
            });
        }

        int addedWidgets = 0;
        for (const auto &entry : std::as_const(layoutEntries))
        {
            if (!entry.shouldShow || !entry.widget)
                continue;

            auto *widget = entry.widget;
            if (addedWidgets == 0)
                widget->setVideoHorizontalPadding(0, 4);
            else
                widget->setVideoHorizontalPadding(4, 0);
            widget->setVisible(true);
            m_twoCameraSplitter->addWidget(widget);
            widget->show();
            ++addedWidgets;
        }

        if (addedWidgets == 1)
        {
            // Prevent a single widget from sticking to one edge if the other camera is hidden mid-drag.
            m_twoCameraSplitter->setSizes({1, 1});
        }
        else if (addedWidgets >= 2)
        {
            const int base = 1000;
            int firstSize = static_cast<int>(m_twoCameraSplitRatio * base);
            firstSize = std::clamp(firstSize, 1, base - 1);
            int secondSize = base - firstSize;
            m_twoCameraSplitter->setSizes({firstSize, secondSize});
        }

        m_twoCameraSplitter->show();

        m_gridLayout->addWidget(m_twoCameraSplitter, 0, 0, 1, 2);
        m_gridLayout->setColumnStretch(0, 1);
        m_gridLayout->setColumnStretch(1, 1);
        m_gridLayout->setRowStretch(0, 1);
        m_gridLayout->setRowStretch(1, 0);
        updatePreviewViewportContexts(visibleCount);

        if (m_scrollArea)
        {
            m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            if (auto *viewport = m_scrollArea->viewport())
            {
                const int minHeight = anyZoomed ? 0 : viewport->height();
                m_gridContainer->setMinimumHeight(minHeight);
            }
        }
        return;
    }

    const int columnCount = columnCountFor(visibleCount);

    int row = 0;
    int column = 0;
    int lastRowIndex = -1;

    for (const auto &entry : std::as_const(layoutEntries))
    {
        auto *widget = entry.widget;
        const bool shouldShow = entry.shouldShow;

        widget->setVisible(shouldShow);
        if (!shouldShow)
            continue;

        widget->setVideoHorizontalPadding(0, 0);

        m_gridLayout->addWidget(widget, row, column);
        widget->show();
        lastRowIndex = row;

        ++column;
        if (column >= columnCount)
        {
            column = 0;
            ++row;
        }
    }

    for (int c = 0; c < columnCount; ++c)
        m_gridLayout->setColumnStretch(c, 1);

    const bool fillAvailableRows = anyZoomed || (visibleCount == 1);
    if (lastRowIndex >= 0)
    {
        if (fillAvailableRows)
        {
            for (int stretchRow = 0; stretchRow <= lastRowIndex; ++stretchRow)
                m_gridLayout->setRowStretch(stretchRow, 1);
            m_gridLayout->setRowStretch(lastRowIndex + 1, 0);
        }
        else
        {
            for (int stretchRow = 0; stretchRow <= lastRowIndex; ++stretchRow)
                m_gridLayout->setRowStretch(stretchRow, 0);
            m_gridLayout->setRowStretch(lastRowIndex + 1, 1);
        }
    }

    updatePreviewViewportContexts(visibleCount);

    if (m_scrollArea)
    {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (auto *viewport = m_scrollArea->viewport())
        {
            const int minHeight = anyZoomed ? 0 : viewport->height();
            m_gridContainer->setMinimumHeight(minHeight);
        }
    }
}

void WeldingPage::updatePreviewViewportContexts(int visibleCount)
{
    // Provide scroll viewport context so previews can align overlays when zoomed or single view.
    QWidget *viewport = m_scrollArea ? m_scrollArea->viewport() : nullptr;
    const QMargins outerMargins = m_gridLayout ? m_gridLayout->contentsMargins() : QMargins();
    const bool fillViewportWidth = (visibleCount == 1);

    for (auto *preview : std::as_const(m_previewWidgets))
    {
        if (!preview)
            continue;

        preview->setViewportContext(viewport, fillViewportWidth, outerMargins);

        const bool zoomed = preview->isZoomed();

        // Force a sizing recalculation once the event loop has applied the new viewport size.
        preview->setZoomState(zoomed);
        QTimer::singleShot(0, preview, [preview, zoomed]() {
            if (preview)
                preview->setZoomState(zoomed);
        });
    }
}

void WeldingPage::scheduleViewportRefresh()
{
    if (m_pendingViewportRefresh)
        return;

    m_pendingViewportRefresh = true;
    QTimer::singleShot(0, this, [this]() {
        m_pendingViewportRefresh = false;
        rebuildGrid();
    });
}

void WeldingPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    for (auto *preview : std::as_const(m_previewWidgets))
    {
        if (!preview)
            continue;
        if (preview->isVisible() && preview->isRoiOverlayEnabled())
            applyConfigRoiToPreview(preview);
    }
    scheduleViewportRefresh();
}

int WeldingPage::columnCountFor(int visibleCameraCount) const
{
    if (visibleCameraCount <= 1)
        return 1;
    if (visibleCameraCount == 2)
        return 2;
    if (visibleCameraCount <= 4)
        return 2;
    if (visibleCameraCount <= 6)
        return 3;
    return 4;
}

void WeldingPage::rebuildSlotFilters()
{
    if (!m_slotButtonGroup || !m_filterLayout)
        return;

    QStringList slotIds;
    QHash<QString, QStringList> displayNamesBySlot;
    QHash<QString, bool> slotEnabledStates;
    if (m_cameraManager)
    {
        QSet<QString> uniqueSlots;
        const auto cameras = m_cameraManager->cameras();
        for (const auto &info : cameras)
        {
            const QString slotId = info.slotId.trimmed();
            if (slotId.isEmpty())
                continue;

            uniqueSlots.insert(slotId);

            QString display = info.alias.trimmed();
            if (display.isEmpty())
                display = info.name.trimmed();
            if (display.isEmpty())
                display = info.id.trimmed();

            if (display.isEmpty())
                continue;

            auto &names = displayNamesBySlot[slotId];
            if (!names.contains(display, Qt::CaseInsensitive))
                names.append(display);

            const bool visible = info.visible;
            slotEnabledStates.insert(slotId, slotEnabledStates.value(slotId, false) || visible);
        }
        slotIds = uniqueSlots.values();
        std::sort(slotIds.begin(), slotIds.end(), [](const QString &a, const QString &b) {
            return a.localeAwareCompare(b) < 0;
        });

        for (auto it = displayNamesBySlot.begin(); it != displayNamesBySlot.end(); ++it)
        {
            auto &names = it.value();
            std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
                return a.localeAwareCompare(b) < 0;
            });
        }
    }

    {
        QSignalBlocker blocker(m_slotButtonGroup);
        for (auto button : std::as_const(m_slotButtons))
        {
            if (!button)
                continue;
            m_slotButtonGroup->removeButton(button);
            m_filterLayout->removeWidget(button);
            button->deleteLater();
        }
        m_slotButtons.clear();

        for (const auto &slotId : slotIds)
        {
            QString buttonText = slotId;
            if (displayNamesBySlot.contains(slotId))
            {
                const QStringList names = displayNamesBySlot.value(slotId);
                if (!names.isEmpty())
                    buttonText = names.join(QStringLiteral(", "));
            }

            auto *button = new QPushButton(buttonText, m_bottomBar);
            button->setCheckable(true);
            button->setProperty("slotId", slotId);
            if (!slotId.isEmpty())
                button->setToolTip(slotId);
            styleSlotButton(button);
            m_slotButtonGroup->addButton(button);
            m_filterLayout->addWidget(button, 0, Qt::AlignVCenter);
            const bool slotEnabled = slotEnabledStates.value(slotId, true);
            button->setEnabled(slotEnabled);
            m_slotButtons.insert(slotId, button);
        }
    }

    if (m_slotButtonGroup)
    {
        QSignalBlocker blocker(m_slotButtonGroup);
        const bool hasSelection = m_activeSlotFilter.isEmpty() || slotIds.contains(m_activeSlotFilter);
        if (!hasSelection)
            m_activeSlotFilter.clear();
        else
        {
            auto *activeButton = m_slotButtons.value(m_activeSlotFilter, nullptr);
            if (!activeButton || !activeButton->isEnabled())
                m_activeSlotFilter.clear();
        }

        if (m_allFilterButton)
            m_allFilterButton->setChecked(m_activeSlotFilter.isEmpty());

        for (auto it = m_slotButtons.begin(); it != m_slotButtons.end(); ++it)
        {
            if (!it.value())
                continue;
            it.value()->setChecked(it.key() == m_activeSlotFilter);
        }
    }

    updateZoomControlsForFilter();
    rebuildGrid();
    updateRecordButtonState();
}

void WeldingPage::applySlotFilter(const QString &slotId)
{
    const QString normalized = slotId.trimmed();
    if (m_activeSlotFilter == normalized)
        return;

    m_activeSlotFilter = normalized;

    if (m_slotButtonGroup)
    {
        QSignalBlocker blocker(m_slotButtonGroup);
        if (m_allFilterButton)
            m_allFilterButton->setChecked(m_activeSlotFilter.isEmpty());
        for (auto it = m_slotButtons.begin(); it != m_slotButtons.end(); ++it)
        {
            if (it.value())
                it.value()->setChecked(it.key() == m_activeSlotFilter);
        }
    }

    updateZoomControlsForFilter();
    rebuildGrid();
    updateRecordButtonState();
}

QStringList WeldingPage::filteredCameraIds() const
{
    QStringList ids;
    for (const auto &cameraId : m_cameraOrder)
    {
        auto *widget = m_previewWidgets.value(cameraId, nullptr);
        if (!widget)
            continue;

        if (m_cameraManager && m_cameraManager->hasCamera(cameraId))
        {
            const auto info = m_cameraManager->camera(cameraId);
            if (!info.visible)
                continue;
            if (!m_activeSlotFilter.isEmpty() && info.slotId != m_activeSlotFilter)
                continue;
        }
        else if (!m_activeSlotFilter.isEmpty())
        {
            continue;
        }

        ids.append(cameraId);
    }
    return ids;
}

void WeldingPage::onRecordButtonToggled(bool recording)
{
    if (!m_cameraManager || !m_recordingManager)
        return;

    const auto targetIds = filteredCameraIds();
    if (targetIds.isEmpty())
    {
        updateRecordButtonState();
        return;
    }

    if (recording)
    {
        for (const auto &cameraId : targetIds)
        {
            if (!m_cameraManager->hasCamera(cameraId))
                continue;
            if (m_recordingCameras.contains(cameraId))
                continue;
            auto cameraHandle = m_cameraManager->cameraHandle(cameraId);
            auto *previewWidget = m_previewWidgets.value(cameraId, nullptr);
            auto *captureSession = previewWidget ? previewWidget->captureSession() : nullptr;
            const auto info = m_cameraManager->camera(cameraId);
            m_recordingManager->startRecording(cameraId, cameraHandle, captureSession, info.slotId, m_currentPassLevel);
        }
    }
    else
    {
        for (const auto &cameraId : targetIds)
        {
            if (!m_cameraManager->hasCamera(cameraId))
                continue;
            m_recordingManager->stopRecording(cameraId);
        }
    }

    updateRecordButtonState();
}

void WeldingPage::updateRecordButtonState()
{
    if (!m_recordButton)
        return;

    const auto ids = filteredCameraIds();
    bool allRecording = !ids.isEmpty();
    for (const auto &id : ids)
    {
        if (!m_recordingCameras.contains(id))
        {
            allRecording = false;
            break;
        }
    }

    {
        QSignalBlocker blocker(m_recordButton);
        m_recordButton->setChecked(allRecording && !ids.isEmpty());
        m_recordButton->setEnabled(!ids.isEmpty());
        const auto iconPath = allRecording ? QStringLiteral(":/icons/wd_pause.svg")
                                           : QStringLiteral(":/icons/wd_record.svg");
        m_recordButton->setIcon(QIcon(iconPath));
    }

    if (m_recordPulseAnimation)
    {
        if (allRecording && !ids.isEmpty())
        {
            if (m_recordPulseAnimation->state() != QAbstractAnimation::Running)
                m_recordPulseAnimation->start();
        }
        else
        {
            if (m_recordPulseAnimation->state() == QAbstractAnimation::Running)
                m_recordPulseAnimation->stop();
            else
                applyRecordButtonStyle(m_idleRecordGlow);
        }
    }
}

void WeldingPage::updateZoomControlsForFilter()
{
    const bool forceZoom = !m_activeSlotFilter.isEmpty();

    if (forceZoom && !m_forceZoomActive)
    {
        m_previousZoomStates.clear();
        m_previousZoomCenters.clear();
        for (auto it = m_previewWidgets.cbegin(); it != m_previewWidgets.cend(); ++it)
        {
            auto *widget = it.value();
            if (!widget)
                continue;
            const bool zoomed = widget->isZoomed();
            m_previousZoomStates.insert(it.key(), zoomed);
            if (zoomed)
            {
                const auto center = widget->zoomSceneCenter();
                if (center.has_value())
                    m_previousZoomCenters.insert(it.key(), center);
            }
        }
    }

    for (auto it = m_previewWidgets.begin(); it != m_previewWidgets.end(); ++it)
    {
        auto *widget = it.value();
        if (!widget)
            continue;

        if (forceZoom && !m_previousZoomStates.contains(it.key()))
        {
            const bool zoomed = widget->isZoomed();
            m_previousZoomStates.insert(it.key(), zoomed);
            if (zoomed)
            {
                const auto center = widget->zoomSceneCenter();
                if (center.has_value())
                    m_previousZoomCenters.insert(it.key(), center);
            }
        }

        widget->setZoomControlsVisible(!forceZoom);

        if (forceZoom)
        {
            widget->setZoomState(false);
        }
        else
        {
            const bool restoreZoom = m_previousZoomStates.value(it.key(), widget->isZoomed());
            widget->setZoomState(restoreZoom);
            if (restoreZoom)
            {
                const auto center = m_previousZoomCenters.value(it.key());
                widget->restoreZoomSceneCenter(center);
                if (center.has_value())
                {
                    QTimer::singleShot(0, widget, [widget, center]() {
                        if (widget)
                            widget->restoreZoomSceneCenter(center);
                    });
                }
            }
        }
    }

    m_forceZoomActive = forceZoom;
    if (!forceZoom)
    {
        m_previousZoomStates.clear();
        m_previousZoomCenters.clear();
    }
}

void WeldingPage::updatePreviewAnalysisState(CameraPreviewWidget *preview)
{
    if (!preview)
        return;

    preview->setAnalysisPassInfo(m_currentPassLevel);
    const QString cameraId = preview->cameraId();
    const bool cameraAiEnabled = m_cameraAiEnabled.value(cameraId, false);
    preview->setShowLegend(m_showLegendPreference);
    preview->setAiAnalysisEnabled(cameraAiEnabled);
    preview->setPlcControlEnabled(m_currentPlcEnabled);
    preview->setAlignmentWarningThreshold(m_currentConfidenceThreshold);
    preview->setAlignmentOffset(m_alignmentOffsets.value(cameraId, 0.0));
    preview->setAnalysisPointSize(m_detectionDotSizePx);
}

void WeldingPage::updateAllPreviewAnalysisStates()
{
    for (auto *preview : std::as_const(m_previewWidgets))
        updatePreviewAnalysisState(preview);
}

void WeldingPage::setCameraAiEnabled(const QString &cameraId, bool enabled)
{
    if (cameraId.isEmpty())
        return;

    const bool previous = m_cameraAiEnabled.value(cameraId, false);
    if (previous == enabled && m_cameraAiEnabled.contains(cameraId))
    {
        if (auto *preview = m_previewWidgets.value(cameraId, nullptr))
            preview->setAiAnalysisEnabled(enabled);
        return;
    }

    m_cameraAiEnabled.insert(cameraId, enabled);
    if (auto *preview = m_previewWidgets.value(cameraId, nullptr))
        preview->setAiAnalysisEnabled(enabled);
}

void WeldingPage::applyAiToggleLock(const QString &activeCameraId, const QString &message)
{
    for (auto it = m_previewWidgets.begin(); it != m_previewWidgets.end(); ++it)
    {
        auto *preview = it.value();
        if (!preview)
            continue;

        const bool allowToggle = activeCameraId.isEmpty() || it.key() == activeCameraId;
        preview->setAiToggleEnabled(allowToggle);
        preview->setAiToggleTooltip(allowToggle ? QString() : message);
    }
}

void WeldingPage::connectPreviewSignals(CameraPreviewWidget *preview)
{
    if (!preview)
        return;

    connect(preview, &CameraPreviewWidget::plcControlToggled, this,
            [this](const QString &, bool enabled) { handlePlcToggleRequested(enabled); });

    connect(preview, &CameraPreviewWidget::aiAnalysisToggled, this,
            [this](const QString &cameraId, bool enabled) { handleAiToggleRequested(cameraId, enabled); });

    connect(preview, &CameraPreviewWidget::alignmentOffsetAdjusted, this,
            [this](const QString &cameraId, double offsetMm) { handleAlignmentOffsetChanged(cameraId, offsetMm); });

    connect(preview, &CameraPreviewWidget::roiOverlayToggled, this,
            [this, preview](bool visible) {
                if (visible)
                    applyConfigRoiToPreview(preview);
            });
}

void WeldingPage::handleAiToggleRequested(const QString &cameraId, bool enabled)
{
    if (cameraId.isEmpty())
        return;

    const bool previous = m_cameraAiEnabled.value(cameraId, false);
    if (previous == enabled)
        return;

    m_cameraAiEnabled.insert(cameraId, enabled);

    qInfo() << "[WeldingPage] AI toggle request" << (enabled ? "ON" : "OFF") << "camera" << cameraId;

    if (auto *preview = m_previewWidgets.value(cameraId, nullptr))
        preview->setAiAnalysisEnabled(enabled);

    emit aiToggleRequested(cameraId, enabled);
}

void WeldingPage::handlePlcToggleRequested(bool enabled)
{
    if (!m_plcClient)
        return;

    if (enabled)
    {
        const auto current = m_plcClient->activeConnection();
        if (!current.id.isEmpty())
            m_plcClient->connectToController(current);
    }
    else
    {
        m_plcClient->disconnectFromController();
    }

    updateAllPreviewAnalysisStates();
}

void WeldingPage::handleAlignmentOffsetChanged(const QString &cameraId, double offsetMm)
{
    m_alignmentOffsets.insert(cameraId, offsetMm);
}

void WeldingPage::handleFrameAvailable(const QString &cameraId, const QImage &frame)
{
    emit frameCaptured(cameraId, frame);
}

void WeldingPage::applyConfigRoiToPreview(CameraPreviewWidget *preview)
{
    if (!preview)
        return;

    const QString cameraId = preview->cameraId();
    if (cameraId.isEmpty())
        return;

    QJsonDocument doc = ConfigUtils::loadConfig();
    if (!doc.isObject())
        return;

    QJsonObject root = doc.object();

    QJsonObject globalObj = root.value(QStringLiteral("global")).toObject();
    bool showLegend = true;
    bool insertedLegendKey = false;
    if (globalObj.contains(QStringLiteral("showLegend")))
    {
        showLegend = globalObj.value(QStringLiteral("showLegend")).toBool(true);
    }
    else
    {
        globalObj.insert(QStringLiteral("showLegend"), true);
        root.insert(QStringLiteral("global"), globalObj);
        insertedLegendKey = true;
    }

    const bool legendChanged = (m_showLegendPreference != showLegend);
    m_showLegendPreference = showLegend;
    preview->setShowLegend(showLegend);

    QSize frameSize = frameSizeFromConfig(root, cameraId, preview->currentFrameSize());
    if (!frameSize.isValid() || frameSize.isEmpty())
        frameSize = QSize(1920, 1080);

    QRectF normalized = normalizedRoiFromConfig(root, cameraId);
    bool needsSave = insertedLegendKey;

    if (!normalized.isValid())
    {
        QRectF fallback = defaultNormalizedRoi(frameSize);
        fallback = clampNormalizedRect(fallback);
        if (fallback.isValid() && writeNormalizedRoiToConfig(root, cameraId, fallback))
        {
            normalized = fallback;
            needsSave = true;
        }
    }

    if (needsSave)
    {
        doc.setObject(root);
        ConfigUtils::saveConfig(doc);
    }

    if (legendChanged)
        updateAllPreviewAnalysisStates();

    normalized = clampNormalizedRect(normalized);
    if (!normalized.isValid())
        return;

    if (legendChanged)
        updateAllPreviewAnalysisStates();

    AnalysisOverlayState &state = m_overlayStates[cameraId];
    state.normalizedRoi = clampNormalizedRect(normalized);
    state.captureFrameSize = frameSize;

    QSize displaySize = preview->currentFrameSize();
    if (!displaySize.isValid() || displaySize.isEmpty())
        displaySize = frameSize;
    if (!displaySize.isValid() || displaySize.isEmpty())
        displaySize = QSize(1920, 1080);
    state.displayFrameSize = displaySize;

    const QRect roiPx = toPixelRect(state.normalizedRoi, state.displayFrameSize);
    preview->setAnalysisOverlay(state.shapes, roiPx, state.displayFrameSize);
}

QRectF WeldingPage::normalizedRoiFromConfig(const QJsonObject &root, const QString &cameraId) const
{
    auto readRoi = [](const QJsonObject &roiObj) -> QRectF {
        if (roiObj.isEmpty())
            return QRectF();
        const double x = roiObj.value(QStringLiteral("x")).toDouble(-1.0);
        const double y = roiObj.value(QStringLiteral("y")).toDouble(-1.0);
        const double width = roiObj.value(QStringLiteral("width")).toDouble(-1.0);
        const double height = roiObj.value(QStringLiteral("height")).toDouble(-1.0);
        if (x < 0.0 || y < 0.0 || width <= 0.0 || height <= 0.0)
            return QRectF();
        return QRectF(x, y, width, height);
    };

    const QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    const QJsonArray profiles = settingsObj.value(QStringLiteral("cameraProfiles")).toArray();
    for (const QJsonValue &value : profiles)
    {
        const QJsonObject profile = value.toObject();
        if (profile.value(QStringLiteral("id")).toString().compare(cameraId, Qt::CaseInsensitive) != 0)
            continue;

        const QRectF roi = readRoi(profile.value(QStringLiteral("roi")).toObject());
        if (roi.isValid())
            return roi;

        const QRectF analysisRoi = readRoi(profile.value(QStringLiteral("analysis")).toObject().value(QStringLiteral("roi")).toObject());
        if (analysisRoi.isValid())
            return analysisRoi;
    }

    const QJsonObject legacyCameras = settingsObj.value(QStringLiteral("camera")).toObject();
    if (legacyCameras.contains(cameraId))
    {
        const QJsonObject legacyEntry = legacyCameras.value(cameraId).toObject();
        const QRectF legacyRoi = readRoi(legacyEntry.value(QStringLiteral("roi")).toObject());
        if (legacyRoi.isValid())
            return legacyRoi;
    }

    return QRectF();
}

QSize WeldingPage::frameSizeFromConfig(const QJsonObject &root, const QString &cameraId, const QSize &previewSize) const
{
    if (previewSize.isValid() && !previewSize.isEmpty())
        return previewSize;

    auto readResolution = [](const QJsonObject &cameraObj) -> QSize {
        const QJsonObject resolution = cameraObj.value(QStringLiteral("resolution")).toObject();
        const int width = resolution.value(QStringLiteral("width")).toInt(0);
        const int height = resolution.value(QStringLiteral("height")).toInt(0);
        if (width > 0 && height > 0)
            return QSize(width, height);
        return QSize();
    };

    const QJsonObject camerasObj = root.value(QStringLiteral("cameras")).toObject();
    QSize specified = readResolution(camerasObj.value(cameraId).toObject());
    if (!specified.isValid() || specified.isEmpty())
        specified = readResolution(camerasObj.value(QStringLiteral("default")).toObject());
    if (!specified.isValid() || specified.isEmpty())
        specified = QSize(1920, 1080);
    return specified;
}

QRectF WeldingPage::defaultNormalizedRoi(const QSize &frameSize) const
{
    constexpr double kCaptureSide = 512.0;
    const double frameWidth = std::max(1, frameSize.width());
    const double frameHeight = std::max(1, frameSize.height());

    double normalizedWidth = std::min(kCaptureSide, frameWidth) / frameWidth;
    double normalizedHeight = std::min(kCaptureSide, frameHeight) / frameHeight;

    normalizedWidth = std::clamp(normalizedWidth, 0.0, 1.0);
    normalizedHeight = std::clamp(normalizedHeight, 0.0, 1.0);

    double normalizedX = (1.0 - normalizedWidth) * 0.5;
    double normalizedY = (1.0 - normalizedHeight) * 0.5;

    normalizedX = std::clamp(normalizedX, 0.0, 1.0 - normalizedWidth);
    normalizedY = std::clamp(normalizedY, 0.0, 1.0 - normalizedHeight);

    return QRectF(normalizedX, normalizedY, normalizedWidth, normalizedHeight);
}

bool WeldingPage::writeNormalizedRoiToConfig(QJsonObject &root, const QString &cameraId, const QRectF &roi) const
{
    if (cameraId.isEmpty() || !roi.isValid())
        return false;

    auto serializeRoi = [&roi]() {
        QJsonObject roiObj;
        roiObj.insert(QStringLiteral("x"), roi.x());
        roiObj.insert(QStringLiteral("y"), roi.y());
        roiObj.insert(QStringLiteral("width"), roi.width());
        roiObj.insert(QStringLiteral("height"), roi.height());
        return roiObj;
    };

    QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    QJsonArray profiles = settingsObj.value(QStringLiteral("cameraProfiles")).toArray();
    bool updated = false;
    for (int i = 0; i < profiles.size(); ++i)
    {
        QJsonObject profile = profiles.at(i).toObject();
        if (profile.value(QStringLiteral("id")).toString().compare(cameraId, Qt::CaseInsensitive) != 0)
            continue;

        QJsonObject analysisObj = profile.value(QStringLiteral("analysis")).toObject();
        analysisObj.insert(QStringLiteral("roi"), serializeRoi());
        profile.insert(QStringLiteral("analysis"), analysisObj);
        profiles.replace(i, profile);
        updated = true;
        break;
    }

    if (!updated)
    {
        QJsonObject profile;
        profile.insert(QStringLiteral("id"), cameraId);
        QJsonObject analysisObj;
        analysisObj.insert(QStringLiteral("roi"), serializeRoi());
        profile.insert(QStringLiteral("analysis"), analysisObj);
        profiles.append(profile);
        updated = true;
    }

    if (updated)
    {
        settingsObj.insert(QStringLiteral("cameraProfiles"), profiles);
        root.insert(QStringLiteral("settings"), settingsObj);
    }

    return updated;
}

QRectF WeldingPage::clampNormalizedRect(const QRectF &rect) const
{
    if (!rect.isValid())
        return QRectF();

    const double x = std::clamp(rect.x(), 0.0, 1.0);
    const double y = std::clamp(rect.y(), 0.0, 1.0);
    const double width = std::clamp(rect.width(), 0.0, 1.0 - x);
    const double height = std::clamp(rect.height(), 0.0, 1.0 - y);

    if (width <= 0.0 || height <= 0.0)
        return QRectF();

    return QRectF(x, y, width, height);
}

QRectF WeldingPage::toNormalizedRect(const QRect &pixelRect, const QSize &frameSize) const
{
    if (!pixelRect.isValid() || frameSize.width() <= 0 || frameSize.height() <= 0)
        return QRectF();

    QRect bounded = pixelRect.intersected(QRect(QPoint(0, 0), frameSize));
    if (!bounded.isValid())
        return QRectF();

    const double x = static_cast<double>(bounded.x()) / frameSize.width();
    const double y = static_cast<double>(bounded.y()) / frameSize.height();
    const double width = static_cast<double>(bounded.width()) / frameSize.width();
    const double height = static_cast<double>(bounded.height()) / frameSize.height();

    return clampNormalizedRect(QRectF(x, y, width, height));
}

QRect WeldingPage::toPixelRect(const QRectF &normalizedRect, const QSize &frameSize) const
{
    if (!normalizedRect.isValid() || frameSize.width() <= 0 || frameSize.height() <= 0)
        return QRect();

    const QRectF clamped = clampNormalizedRect(normalizedRect);
    if (!clamped.isValid())
        return QRect();

    auto toPixel = [](double value, int max) -> int {
        return static_cast<int>(std::round(value * static_cast<double>(max)));
    };

    QRect rect(
        toPixel(clamped.x(), frameSize.width()),
        toPixel(clamped.y(), frameSize.height()),
        toPixel(clamped.width(), frameSize.width()),
        toPixel(clamped.height(), frameSize.height()));

    rect.setWidth(std::clamp(rect.width(), 1, frameSize.width()));
    rect.setHeight(std::clamp(rect.height(), 1, frameSize.height()));
    rect.moveLeft(std::clamp(rect.left(), 0, frameSize.width() - rect.width()));
    rect.moveTop(std::clamp(rect.top(), 0, frameSize.height() - rect.height()));

    return rect;
}

QSize WeldingPage::displayFrameSizeFor(CameraPreviewWidget *preview, const QSize &fallback) const
{
    if (preview)
    {
        const QSize current = preview->currentFrameSize();
        if (current.isValid() && !current.isEmpty())
            return current;
    }
    if (fallback.isValid() && !fallback.isEmpty())
        return fallback;
    return QSize();
}

void WeldingPage::styleSlotButton(QPushButton *button) const
{
    if (!button)
        return;

    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(48);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    button->setStyleSheet(QStringLiteral(
        "QPushButton{"
        " background:rgba(28,29,33,1.0);"
        " color:#B0B4BA;"
        " border:none;"
        " border-radius:18px;"
        " padding:0 24px;"
        " font-size:16px;"
        " font-weight:600;"
        "}"
        "QPushButton:hover{background:rgba(34,255,162,0.08); color:#E2FFF2;}"
        "QPushButton:checked{background:rgba(34,255,162,0.22); color:#22FFA2;}"
        "QPushButton:disabled{color:rgba(176,180,186,0.4);}" 
    ));
}

void WeldingPage::styleRecordButton()
{
    if (!m_recordButton)
        return;

    m_recordButton->setCursor(Qt::PointingHandCursor);
    m_recordButton->setFixedSize(48, 48);
    m_recordButton->setIconSize(QSize(48, 48));
    m_recordButton->setIcon(QIcon(QStringLiteral(":/icons/wd_record.svg")));

    applyRecordButtonStyle(m_idleRecordGlow);

    if (!m_recordPulseAnimation)
    {
        m_recordPulseAnimation = new QVariantAnimation(this);
        m_recordPulseAnimation->setStartValue(m_idleRecordGlow);
        m_recordPulseAnimation->setEndValue(0.45);
        m_recordPulseAnimation->setEasingCurve(QEasingCurve::InOutSine);
        m_recordPulseAnimation->setDuration(1400);
        m_recordPulseAnimation->setLoopCount(-1);
        connect(m_recordPulseAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            applyRecordButtonStyle(value.toDouble());
        });
        connect(m_recordPulseAnimation, &QVariantAnimation::stateChanged, this, [this](QAbstractAnimation::State newState, QAbstractAnimation::State) {
            if (newState == QAbstractAnimation::Stopped)
                applyRecordButtonStyle(m_idleRecordGlow);
        });
    }
}

void WeldingPage::applyRecordButtonStyle(double glowAmount)
{
    if (!m_recordButton)
        return;

    const QString style = QStringLiteral(
        "QPushButton{"
        " background:transparent;"
        " border:none;"
        " padding:0;"
        "}"
        "QPushButton:hover{background:transparent;}"
        "QPushButton:checked{background:transparent;}"
        "QPushButton:disabled{background:transparent; opacity:0.4;}"
    );

    m_recordButton->setStyleSheet(style);
}

bool WeldingPage::eventFilter(QObject *watched, QEvent *event)
{
    if (m_scrollArea && watched == m_scrollArea->viewport() && event->type() == QEvent::Resize)
    {
        updatePreviewViewportContexts(filteredCameraIds().size());
        if (m_gridContainer)
        {
            auto *viewport = m_scrollArea->viewport();
            if (viewport)
                m_gridContainer->setMinimumHeight(viewport->height());
        }
    }
    return QWidget::eventFilter(watched, event);
}
#include <QShowEvent>
