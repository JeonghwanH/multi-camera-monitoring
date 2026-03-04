// Author: SeungJae Lee
// SessionPlaybackWidget: manages recorded session playback, overlays, and camera selection UI.

#include "SessionPlaybackWidget.h"

#include "TwoCameraSplitter.h"
#include "AnalysisOverlayWidget.h"
#include "AnalysisStatusPanel.h"
#include "PlaybackDeviationBar.h"
#include "ZoomableVideoView.h"
#include "utils/ConfigUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QFile>

#include <QAudioOutput>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QPushButton>
#include <QToolButton>
#include <QIcon>
#include <QDebug>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QShowEvent>
#include <QTimer>
#include <QStringList>

#include <algorithm>
#include <optional>
#include <limits>
#include <utility>
#include <cmath>
#include <QtMath>

namespace
{
QString speedLabel(double rate)
{
    if (qFuzzyCompare(rate, 1.0))
        return QStringLiteral("1x");
    if (qFuzzyCompare(rate, 2.0))
        return QStringLiteral("2x");
    if (qFuzzyCompare(rate, 0.5))
        return QStringLiteral("0.5x");
    return QString::number(rate, 'f', 1) + QStringLiteral("x");
}
constexpr qreal kPlaybackZoomFactor = 1.5;

void styleCameraSelectionButton(QPushButton *button)
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

QVector<AnalysisOverlayWidget::Shape> parseOverlayShapes(const QJsonObject &payload)
{
    QVector<AnalysisOverlayWidget::Shape> shapes;
    const QJsonObject frameAnalysis = payload.value(QStringLiteral("frameAnalysis")).toObject();
    const QJsonArray objects = frameAnalysis.value(QStringLiteral("objects")).toArray();
    for (const QJsonValue &value : objects)
    {
        const QJsonObject obj = value.toObject();
        AnalysisOverlayWidget::Shape shape;
        shape.cls = obj.value(QStringLiteral("className")).toString();

        QVector<QPointF> points;
        const QJsonObject polygon = obj.value(QStringLiteral("polygon")).toObject();
        const QJsonArray coordinates = polygon.value(QStringLiteral("coordinates")).toArray();
        if (!coordinates.isEmpty())
        {
            const QJsonValue first = coordinates.at(0);
            if (first.isArray() && first.toArray().size() > 0 && first.toArray().at(0).isArray())
            {
                for (const QJsonValue &ringVal : coordinates)
                {
                    const QJsonArray ring = ringVal.toArray();
                    for (const QJsonValue &ptVal : ring)
                    {
                        const QJsonArray pt = ptVal.toArray();
                        if (pt.size() >= 2)
                            points.append(QPointF(pt.at(0).toDouble(), pt.at(1).toDouble()));
                    }
                }
            }
            else
            {
                for (const QJsonValue &ptVal : coordinates)
                {
                    const QJsonArray pt = ptVal.toArray();
                    if (pt.size() >= 2)
                        points.append(QPointF(pt.at(0).toDouble(), pt.at(1).toDouble()));
                }
            }
        }

        if (points.isEmpty())
        {
            const QJsonArray center = obj.value(QStringLiteral("center")).toArray();
            if (center.size() >= 2)
                points.append(QPointF(center.at(0).toDouble(), center.at(1).toDouble()));
        }

        if (points.isEmpty())
            continue;

        double maxX = 0.0;
        double maxY = 0.0;
        for (const QPointF &pt : std::as_const(points))
        {
            maxX = std::max(maxX, pt.x());
            maxY = std::max(maxY, pt.y());
        }
        if (maxX <= 2.0 && maxY <= 2.0)
        {
            // Some payloads encode points normalized to 0-2; upscale to 512 grid for overlay widget.
            for (QPointF &pt : points)
            {
                pt.setX(pt.x() * 512.0);
                pt.setY(pt.y() * 512.0);
            }
        }

        shape.pts512 = points;
        shapes.append(shape);
    }
    return shapes;
}

QRect parseRoi(const QJsonObject &roiObj)
{
    if (roiObj.isEmpty())
        return {};
    const int x = roiObj.value(QStringLiteral("x")).toInt(roiObj.value(QStringLiteral("left")).toInt(-1));
    const int y = roiObj.value(QStringLiteral("y")).toInt(roiObj.value(QStringLiteral("top")).toInt(-1));
    const int w = roiObj.value(QStringLiteral("w")).toInt(roiObj.value(QStringLiteral("width")).toInt(-1));
    const int h = roiObj.value(QStringLiteral("h")).toInt(roiObj.value(QStringLiteral("height")).toInt(-1));
    if (x < 0 || y < 0 || w <= 0 || h <= 0)
        return {};
    return QRect(x, y, w, h);
}

QSize parseFrameSize(const QJsonObject &frameSizeObj, const QSize &fallback)
{
    if (frameSizeObj.isEmpty())
        return fallback;
    const int w = frameSizeObj.value(QStringLiteral("width")).toInt(fallback.width());
    const int h = frameSizeObj.value(QStringLiteral("height")).toInt(fallback.height());
    if (w > 0 && h > 0)
        return QSize(w, h);
    return fallback;
}
} // namespace

SessionPlaybackWidget::SessionPlaybackWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    for (auto &context : m_players)
        connectPlayerSignals(context);

    applyFocus(-1);
    syncPlayPauseButton();
    updateTimeLabels();
}

void SessionPlaybackWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        for (int i = 0; i < m_players.size(); ++i)
        {
            auto &ctx = m_players[i];
            if (ctx.titleLabel && ctx.track.displayName.isEmpty())
                ctx.titleLabel->setText(tr("Camera %1").arg(i + 1));
            updateZoomButton(ctx);
        }
        if (m_speedButton)
            m_speedButton->setText(speedLabel(m_playbackRate));
        if (m_playPauseButton)
            syncPlayPauseButton();
        if (m_allTracksButton)
            m_allTracksButton->setText(tr("ALL"));
        for (int i = 0; i < m_cameraButtons.size(); ++i)
        {
            QPushButton *button = m_cameraButtons.value(i);
            if (!button)
                continue;
            const auto &ctx = m_players.at(i);
            const QString title = ctx.track.displayName.isEmpty() ? tr("Camera %1").arg(i + 1) : ctx.track.displayName;
            button->setText(title);
        }
        updateCameraSelectionState();
    }

    QWidget::changeEvent(event);
}

void SessionPlaybackWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    scheduleSplitterEqualization();
}

bool SessionPlaybackWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize)
    {
        for (auto &ctx : m_players)
        {
            if (watched == ctx.videoContainer)
            {
                updateHudGeometry(ctx);
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SessionPlaybackWidget::equalizeSplitter()
{
    if (!m_splitter)
        return;

    if (m_activeTrackCount <= 1)
    {
        QList<int> sizes;
        sizes << m_splitter->width();
        for (int i = 1; i < m_players.size(); ++i)
            sizes << 0;
        m_splitter->setSizes(sizes);

        const int handleCount = m_splitter->count() - 1;
        for (int handleIndex = 1; handleIndex <= handleCount; ++handleIndex)
        {
            if (QSplitterHandle *handle = m_splitter->handle(handleIndex))
                handle->setVisible(false);
        }
        return;
    }

    const int totalWidth = m_splitter->width();
    if (totalWidth <= 0)
    {
        scheduleSplitterEqualization();
        return;
    }

    const int handleWidth = m_splitter->handleWidth();
    const int available = totalWidth - handleWidth;
    if (available <= 0)
        return;

    const int first = available / 2;
    const int second = available - first;
    m_splitter->setSizes({first, second});
}

void SessionPlaybackWidget::scheduleSplitterEqualization()
{
    if (!m_splitter)
        return;
    QTimer::singleShot(0, this, [this]() {
        equalizeSplitter();
    });
}

void SessionPlaybackWidget::buildUi()
{
    setStyleSheet(QStringLiteral("background-color:#000000;"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    m_splitter = new TwoCameraSplitter(this);
    m_splitter->setObjectName(QStringLiteral("sessionPlaybackSplitter"));
    layout->addWidget(m_splitter, 1);

    m_players.resize(2);
    m_cameraButtons.resize(m_players.size());
    std::fill(m_cameraButtons.begin(), m_cameraButtons.end(), nullptr);

    const bool showLegend = ConfigUtils::showLegend();

    for (int i = 0; i < m_players.size(); ++i)
    {
        PlayerContext &ctx = m_players[i];
        ctx.player = new QMediaPlayer(this);
        ctx.audioOutput = new QAudioOutput(this);
        ctx.player->setAudioOutput(ctx.audioOutput);
        ctx.audioOutput->setVolume(i == 0 ? 1.0 : 0.0);

        auto *panel = new QWidget(m_splitter);
        ctx.panel = panel;
        panel->setStyleSheet(QStringLiteral("background-color:#000000;"));
        panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto *panelLayout = new QVBoxLayout(panel);
        const int leftMargin = (i == 0) ? 16 : 4;
        const int rightMargin = (i == m_players.size() - 1) ? 16 : 4;
        panelLayout->setContentsMargins(leftMargin, 16, rightMargin, 16);
        panelLayout->setSpacing(12);

        auto *headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(8);

        ctx.titleLabel = new QLabel(panel);
        ctx.titleLabel->setStyleSheet(QStringLiteral("color:#FFFFFF;font-size:16px;font-weight:600;"));
        ctx.titleLabel->setText(tr("Camera %1").arg(i + 1));
        headerLayout->addWidget(ctx.titleLabel);
        headerLayout->addStretch();

        panelLayout->addLayout(headerLayout);

        ctx.videoContainer = new QWidget(panel);
        ctx.videoContainer->setStyleSheet(QStringLiteral("background-color:#000000;border-radius:16px;"));
        ctx.videoContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        ctx.videoContainer->setMinimumHeight(320);

        auto *videoLayout = new QVBoxLayout(ctx.videoContainer);
        videoLayout->setContentsMargins(0, 0, 0, 0);
        videoLayout->setSpacing(0);

        ctx.videoView = new ZoomableVideoView(ctx.videoContainer);
        ctx.videoView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        videoLayout->addWidget(ctx.videoView);

        ctx.overlayWidget = new AnalysisOverlayWidget(ctx.videoContainer);
        ctx.overlayWidget->setShowLegend(showLegend);
        ctx.overlayWidget->setGeometry(ctx.videoContainer->rect());
        ctx.overlayWidget->setVideoView(ctx.videoView);
        ctx.overlayWidget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        ctx.overlayWidget->setPointRadius(m_overlayPointSizePx * 0.5);

        ctx.hudOverlay = new QWidget(ctx.videoContainer);
        ctx.hudOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        ctx.hudOverlay->setAttribute(Qt::WA_NoSystemBackground, true);
        ctx.hudOverlay->setGeometry(ctx.videoContainer->rect());

        auto *hudLayout = new QVBoxLayout(ctx.hudOverlay);
        hudLayout->setContentsMargins(16, 16, 16, 16);
        hudLayout->setSpacing(12);
        hudLayout->addStretch();

        auto *bottomOverlayLayout = new QVBoxLayout();
        bottomOverlayLayout->setContentsMargins(0, 0, 0, 0);
        bottomOverlayLayout->setSpacing(8);

        ctx.zoomButtonOverlay = new QWidget(ctx.videoContainer);
        ctx.zoomButtonOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        ctx.zoomButtonOverlay->setAttribute(Qt::WA_NoSystemBackground, true);
        ctx.zoomButtonOverlay->setStyleSheet(QStringLiteral("background:transparent;"));
        auto *zoomButtonLayout = new QHBoxLayout(ctx.zoomButtonOverlay);
        zoomButtonLayout->setContentsMargins(0, 0, 0, 0);
        zoomButtonLayout->setSpacing(0);

        ctx.zoomButton = new QToolButton(ctx.zoomButtonOverlay);
        ctx.zoomButton->setCursor(Qt::PointingHandCursor);
        ctx.zoomButton->setCheckable(true);
        ctx.zoomButton->setIconSize(QSize(40, 40));
        ctx.zoomButton->setFixedSize(48, 48);
        ctx.zoomButton->setFocusPolicy(Qt::NoFocus);
        ctx.zoomButton->setStyleSheet(QStringLiteral(
            "QToolButton{background-color:rgba(0,0,0,0.32);border:0px;"
            " border-radius:24px;padding:0;}"
            "QToolButton:hover{background-color:rgba(34,255,162,0.24);border:0px;}"
            "QToolButton:pressed{background-color:rgba(34,255,162,0.32);border:0px;}"
            "QToolButton:disabled{background-color:rgba(28,29,33,0.4);color:rgba(158,163,170,0.45);}"
        ));
        connect(ctx.zoomButton, &QToolButton::clicked, this, [this, index = i]() {
            handleZoomToggle(index);
        });
        ctx.zoomed = false;
        zoomButtonLayout->addWidget(ctx.zoomButton, 0, Qt::AlignRight | Qt::AlignBottom);

        auto *statusRow = new QHBoxLayout();
        statusRow->setContentsMargins(0, 0, 0, 0);
        statusRow->setSpacing(16);

        ctx.statusPanel = new AnalysisStatusPanel(ctx.hudOverlay);
        ctx.statusPanel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        ctx.statusPanel->setStyleSheet(QStringLiteral(
            "QWidget#analysisStatusPanel{background:transparent;border-radius:12px;}"
            "QWidget#analysisStatusPanel QWidget#analysisSection{background:transparent;border-radius:10px;}"
            "QWidget#analysisStatusPanel QLabel{color:#FFFFFF;}"));
        ctx.statusPanel->hide();
        statusRow->addWidget(ctx.statusPanel, 0, Qt::AlignLeft | Qt::AlignBottom);
        statusRow->addStretch();
        updateZoomButton(ctx);
        bottomOverlayLayout->addLayout(statusRow);

        ctx.deviationBar = new PlaybackDeviationBar(ctx.hudOverlay);
        ctx.deviationBar->hide();
        bottomOverlayLayout->addWidget(ctx.deviationBar, 0, Qt::AlignBottom);

        hudLayout->addLayout(bottomOverlayLayout);

        if (ctx.overlayWidget)
            ctx.overlayWidget->raise();
        if (ctx.hudOverlay)
            ctx.hudOverlay->raise();
        if (ctx.zoomButtonOverlay)
            ctx.zoomButtonOverlay->raise();

        if (ctx.videoView)
        {
            QObject::connect(ctx.videoView, &ZoomableVideoView::viewRectChanged, ctx.overlayWidget, [overlay = ctx.overlayWidget]() {
                if (overlay)
                    overlay->update();
            });
        }

        ctx.videoContainer->installEventFilter(this);

        panelLayout->addWidget(ctx.videoContainer, 1);

        if (ctx.player && ctx.videoView)
        {
            ctx.player->setVideoOutput(ctx.videoView->videoItem());
            if (!ctx.lastFrameSize.isEmpty())
                ctx.videoView->updateVideoItemSize(ctx.lastFrameSize);
        }

        if (ctx.videoView)
        {
            if (auto *videoItem = ctx.videoView->videoItem())
            {
                if (auto *sink = videoItem->videoSink())
                {
                    auto *contextPtr = &ctx;
                    connect(sink, &QVideoSink::videoFrameChanged, this, [this, contextPtr](const QVideoFrame &frame) {
                        if (!contextPtr || !contextPtr->videoView || !frame.isValid())
                            return;

                        const QSize frameSize(frame.width(), frame.height());
                        if (frameSize.isEmpty())
                            return;

                        if (contextPtr->lastFrameSize != frameSize)
                        {
                            contextPtr->lastFrameSize = frameSize;
                            contextPtr->videoView->updateVideoItemSize(frameSize);
                            if (contextPtr->overlayWidget)
                            {
                                contextPtr->overlayWidget->setNativeSize(frameSize);
                                contextPtr->overlayWidget->update();
                            }
                        }
                    });
                }
            }
        }

        m_splitter->addWidget(panel);

        updateHudGeometry(ctx);
    }

    for (int i = 0; i < m_players.size(); ++i)
        m_splitter->setStretchFactor(i, 1);

    QList<int> initialSizes;
    initialSizes.reserve(m_players.size());
    for (int i = 0; i < m_players.size(); ++i)
        initialSizes << 1000;
    m_splitter->setSizes(initialSizes);

    auto *footerContainer = new QWidget(this);
    auto *footerLayout = new QVBoxLayout(footerContainer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(12);

    m_bottomBar = new QWidget(footerContainer);
    m_bottomBar->setObjectName(QStringLiteral("sessionPlaybackBottomBar"));
    m_bottomBar->setFixedHeight(72);
    m_bottomBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_bottomBar->setStyleSheet(QStringLiteral(
        "QWidget#sessionPlaybackBottomBar{"
        " background:rgba(17,17,19,1.0);"
        " border:1px solid rgba(33,34,37,1.0);"
        " border-radius:16px;"
        "}"
    ));
    m_bottomBarLayout = new QHBoxLayout(m_bottomBar);
    m_bottomBarLayout->setContentsMargins(12, 12, 12, 12);
    m_bottomBarLayout->setSpacing(12);
    m_bottomBarLayout->setAlignment(Qt::AlignVCenter);

    m_cameraButtonsLayout = new QHBoxLayout();
    m_cameraButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_cameraButtonsLayout->setSpacing(12);
    m_cameraButtonsLayout->setAlignment(Qt::AlignVCenter);
    m_bottomBarLayout->addLayout(m_cameraButtonsLayout);

    auto *progressLayout = new QHBoxLayout();
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(8);
    progressLayout->setAlignment(Qt::AlignVCenter);

    m_currentTimeLabel = new QLabel(m_bottomBar);
    m_currentTimeLabel->setStyleSheet(QStringLiteral("color:#9EA3AA;font-size:14px;font-weight:600;"));
    m_currentTimeLabel->setText(QStringLiteral("00:00"));
    progressLayout->addWidget(m_currentTimeLabel);

    m_positionSlider = new QSlider(Qt::Horizontal, m_bottomBar);
    m_positionSlider->setRange(0, 1000);
    m_positionSlider->setStyleSheet(QStringLiteral(
        "QSlider{min-height:24px;background:transparent;}"
        "QSlider::groove:horizontal{background:rgba(35,37,41,1.0);height:6px;border-radius:3px;}"
        "QSlider::handle:horizontal{background:#22FFA2;width:16px;height:16px;margin:-6px 0;border-radius:8px;}"));
    m_positionSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    progressLayout->addWidget(m_positionSlider, 1);

    m_totalTimeLabel = new QLabel(m_bottomBar);
    m_totalTimeLabel->setStyleSheet(QStringLiteral("color:#9EA3AA;font-size:14px;font-weight:600;"));
    m_totalTimeLabel->setText(QStringLiteral("00:00"));
    progressLayout->addWidget(m_totalTimeLabel);

    m_bottomBarLayout->addLayout(progressLayout, 1);

    m_speedButton = new QPushButton(m_bottomBar);
    m_speedButton->setCursor(Qt::PointingHandCursor);
    m_speedButton->setFixedSize(48, 48);
    m_speedButton->setStyleSheet(QStringLiteral(
        "QPushButton{background:rgba(28,29,33,0.92);color:#9EA3AA;border:1px solid rgba(52,56,62,1.0);"
        "border-radius:8px;font-size:14px;font-weight:600;}"
        "QPushButton:hover{border-color:rgba(34,255,162,0.55);color:#22FFA2;}"));
    m_speedButton->setText(speedLabel(m_playbackRate));
    m_bottomBarLayout->addWidget(m_speedButton);

    m_playPauseButton = new QPushButton(m_bottomBar);
    m_playPauseButton->setCursor(Qt::PointingHandCursor);
    m_playPauseButton->setFixedSize(56, 56);
    m_playPauseButton->setIconSize(QSize(48, 48));
    m_playPauseButton->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;border:none;border-radius:16px;color:#E2FFF2;}"
        "QPushButton:hover{background:transparent;}"
        "QPushButton:disabled{background:transparent;color:#6E7178;}"));
    m_playPauseButton->setText(QString());
    m_playIcon = QIcon(QStringLiteral(":/icons/play.svg"));
    m_pauseIcon = QIcon(QStringLiteral(":/icons/wd_pause.svg"));
    m_bottomBarLayout->addWidget(m_playPauseButton);

    m_cameraButtonGroup = new QButtonGroup(this);
    m_cameraButtonGroup->setExclusive(true);
    connect(m_cameraButtonGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this, [this](QAbstractButton *button) {
        if (!button || m_updatingCameraBar)
            return;
        const int trackIndex = button->property("trackIndex").toInt();
        applyFocus(trackIndex);
    });

    footerLayout->addWidget(m_bottomBar);

    layout->addWidget(footerContainer);

    rebuildCameraSelectionBar();

    connect(m_playPauseButton, &QPushButton::clicked, this, &SessionPlaybackWidget::handlePlayPauseRequested);
    connect(m_speedButton, &QPushButton::clicked, this, &SessionPlaybackWidget::handleSpeedToggle);
    connect(m_positionSlider, &QSlider::sliderPressed, this, &SessionPlaybackWidget::handleSliderPressed);
    connect(m_positionSlider, &QSlider::sliderReleased, this, &SessionPlaybackWidget::handleSliderReleased);
    connect(m_positionSlider, &QSlider::sliderMoved, this, &SessionPlaybackWidget::handleSeekRequested);

    scheduleSplitterEqualization();
}

void SessionPlaybackWidget::connectPlayerSignals(PlayerContext &context)
{
    if (!context.player)
        return;

    connect(context.player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        handlePlayerPositionChanged(position);
    });

    connect(context.player, &QMediaPlayer::durationChanged, this, [this](qint64) {
        handlePlayerDurationChanged();
    });

    connect(context.player, &QMediaPlayer::errorOccurred, this, [this, ctx = &context](QMediaPlayer::Error error, const QString &message) {
        handlePlayerError(*ctx, error, message);
    });

    connect(context.player, &QMediaPlayer::playbackStateChanged, this, [this, ctx = &context](QMediaPlayer::PlaybackState state) {
        handlePlaybackStateChanged(*ctx, state);
    });

    connect(context.player, &QMediaPlayer::mediaStatusChanged, this, [this, ctx = &context](QMediaPlayer::MediaStatus status) {
        if (!ctx || !ctx->player)
            return;
        if (status == QMediaPlayer::NoMedia || status == QMediaPlayer::InvalidMedia)
            clearDisplayedOverlay(*ctx);
    });
}

void SessionPlaybackWidget::clearTracks()
{
    for (int i = 0; i < m_players.size(); ++i)
    {
        auto &ctx = m_players[i];
        if (ctx.player)
        {
            ctx.player->stop();
            ctx.player->setSource(QUrl());
        }
        ctx.track = Track{};
        if (ctx.titleLabel)
            ctx.titleLabel->setText(tr("Camera %1").arg(i + 1));
        if (ctx.overlayWidget)
            ctx.overlayWidget->setShapes({});
        if (ctx.statusPanel)
        {
            ctx.statusPanel->clear();
            ctx.statusPanel->setVisible(false);
        }
        if (ctx.deviationBar)
        {
            ctx.deviationBar->setMarkers({});
            ctx.deviationBar->setPosition(0.0);
            ctx.deviationBar->setVisible(false);
        }
        ctx.overlays.clear();
        ctx.currentOverlayIndex = -1;
        ctx.lastFrameSize = QSize();
        ctx.zoomed = false;
        ctx.alignmentOffsetMs = 0;
        if (ctx.videoView)
        {
            ctx.videoView->setZoomed(false);
            ctx.videoView->setZoomFactor(1.0);
        }
        updateZoomButton(ctx);
        if (ctx.panel)
            ctx.panel->setVisible(i == 0);
    }

    m_durationMs = 0;
    if (m_positionSlider)
        m_positionSlider->setValue(0);
    updateTimeLabels();
    m_activeTrackCount = 0;
    m_cameraButtons.fill(nullptr);
    rebuildCameraSelectionBar();
    applyFocus(-1);
    scheduleSplitterEqualization();
}

void SessionPlaybackWidget::setTracks(const QVector<Track> &tracks)
{
    clearTracks();

    const int maxPlayers = m_players.size();
    const int count = std::min(maxPlayers, static_cast<int>(tracks.size()));
    int activeTracks = 0;

    for (int i = 0; i < count; ++i)
    {
        auto &ctx = m_players[i];
        ctx.track = tracks.at(i);
        if (ctx.panel)
            ctx.panel->setVisible(true);
        if (ctx.titleLabel)
        {
            const QString title = ctx.track.displayName.isEmpty() ? ctx.track.cameraId : ctx.track.displayName;
            ctx.titleLabel->setText(title);
        }
        ctx.zoomed = false;
        ctx.alignmentOffsetMs = 0;
        if (ctx.overlayWidget)
            ctx.overlayWidget->setPointRadius(m_overlayPointSizePx * 0.5);
        if (ctx.videoView)
        {
            ctx.videoView->setZoomed(false);
            ctx.videoView->setZoomFactor(1.0);
        }
        updateZoomButton(ctx);

        if (ctx.player)
        {
            ctx.player->stop();
            ctx.player->setSource(QUrl::fromLocalFile(ctx.track.filePath));
            ctx.player->setPlaybackRate(m_playbackRate);
            ctx.player->pause();
            ctx.player->setPosition(0);
        }

        if (ctx.audioOutput)
            ctx.audioOutput->setVolume(i == 0 ? 1.0 : 0.0);

        loadOverlayData(ctx);
        updateOverlayForPosition(ctx, 0);
        if (ctx.deviationBar)
            ctx.deviationBar->setPosition(0.0);
        updateDeviationMarkers(ctx);

        if (!ctx.track.filePath.isEmpty())
            ++activeTracks;
    }

    for (int i = count; i < maxPlayers; ++i)
    {
        auto &ctx = m_players[i];
        if (ctx.panel)
            ctx.panel->setVisible(false);
        ctx.zoomed = false;
        ctx.alignmentOffsetMs = 0;
        updateZoomButton(ctx);
    }

    // Align playback starts based on recording metadata
    qint64 referenceStartMs = 0;
    bool haveReference = false;
    for (const auto &ctx : m_players)
    {
        if (ctx.track.startedAtMs <= 0)
            continue;
        if (!haveReference || ctx.track.startedAtMs > referenceStartMs)
        {
            referenceStartMs = ctx.track.startedAtMs;
            haveReference = true;
        }
    }

    if (haveReference)
    {
        const qint64 remainder = referenceStartMs % 1000;
        if (remainder != 0)
            referenceStartMs += (1000 - remainder);
    }

    for (auto &ctx : m_players)
    {
        qint64 offset = 0;
        if (haveReference && ctx.track.startedAtMs > 0)
        {
            offset = referenceStartMs - ctx.track.startedAtMs;
            if (offset < 0)
                offset = 0;
        }
        ctx.alignmentOffsetMs = offset;
        if (haveReference && ctx.track.startedAtMs > 0)
        {
            qInfo().nospace() << "[SessionPlaybackWidget] align camera "
                              << ctx.track.cameraId << " by " << offset
                              << " ms (reference " << referenceStartMs << ")";
        }
        else if (haveReference)
        {
            qInfo().nospace() << "[SessionPlaybackWidget] align camera "
                              << ctx.track.cameraId << " skipped (missing startedAt)";
        }
        if (ctx.player)
        {
            ctx.player->setPosition(offset);
        }
        updateOverlayForPosition(ctx, offset);
        if (ctx.deviationBar)
            ctx.deviationBar->setPosition(0.0);
        updateDeviationMarkers(ctx);
    }

    m_activeTrackCount = activeTracks;

    m_durationMs = 0;
    handlePlayerDurationChanged();
    handlePlayerPositionChanged(0);
    syncPlayPauseButton();
    rebuildCameraSelectionBar();
    applyFocus(-1);
    scheduleSplitterEqualization();
}

void SessionPlaybackWidget::setConfidenceThreshold(double threshold)
{
    const double clamped = std::max(0.0, threshold);
    if (qFuzzyCompare(m_confidenceThreshold + 1.0, clamped + 1.0))
        return;

    m_confidenceThreshold = clamped;
    for (auto &ctx : m_players)
        updateDeviationMarkers(ctx);
    refreshAllStatusPanels();
}

void SessionPlaybackWidget::setDefaultPassInfo(const QString &passLevel)
{
    const QString trimmed = passLevel.trimmed();
    QString normalized = trimmed;
    if (normalized.compare(QStringLiteral("Second"), Qt::CaseInsensitive) == 0)
        normalized = QStringLiteral("Second");
    else if (normalized.compare(QStringLiteral("Root"), Qt::CaseInsensitive) == 0)
        normalized = QStringLiteral("Root");

    if (normalized.isEmpty())
        normalized = QStringLiteral("Root");

    if (m_defaultPassLevel.compare(normalized, Qt::CaseSensitive) != 0)
    {
        m_defaultPassLevel = normalized;
        refreshAllStatusPanels();
    }
}

void SessionPlaybackWidget::setOverlayPointSize(double sizePx)
{
    sizePx = std::clamp(sizePx, 1.0, 64.0);
    if (qFuzzyCompare(m_overlayPointSizePx, sizePx))
        return;

    m_overlayPointSizePx = sizePx;
    for (auto &ctx : m_players)
    {
        if (ctx.overlayWidget)
            ctx.overlayWidget->setPointRadius(m_overlayPointSizePx * 0.5);
    }
}

void SessionPlaybackWidget::loadOverlayData(PlayerContext &context)
{
    context.overlays.clear();
    context.currentOverlayIndex = -1;
    if (context.overlayWidget)
        context.overlayWidget->setShapes({});
    if (context.statusPanel)
        context.statusPanel->clear();

    const QString path = context.track.analysisPath;
    if (path.isEmpty())
    {
        refreshStatusPanel(context, nullptr);
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        refreshStatusPanel(context, nullptr);
        return;
    }

    auto extractNumeric = [](const QJsonValue &value, double &out) -> bool {
        if (value.isDouble())
        {
            out = value.toDouble();
            return true;
        }
        if (value.isString())
        {
            bool ok = false;
            const double candidate = value.toString().toDouble(&ok);
            if (ok)
            {
                out = candidate;
                return true;
            }
        }
        return false;
    };

    auto appendOverlay = [&](const QJsonObject &root) {
        QJsonObject payload = root.value(QStringLiteral("results")).toObject();
        if (payload.isEmpty())
            payload = root;

        PlayerContext::TimedOverlay overlay;
        overlay.timestampMs = payload.value(QStringLiteral("timestampMs")).toVariant().toLongLong();
        if (overlay.timestampMs <= 0)
            overlay.timestampMs = payload.value(QStringLiteral("pts")).toVariant().toLongLong();
        if (overlay.timestampMs <= 0)
            overlay.timestampMs = payload.value(QStringLiteral("timeMs")).toVariant().toLongLong();
        if (overlay.timestampMs < 0)
            overlay.timestampMs = 0;

        overlay.roi = parseRoi(payload.value(QStringLiteral("roi")).toObject());
        if (!overlay.roi.isValid())
            overlay.roi = parseRoi(root.value(QStringLiteral("roi")).toObject());

        overlay.frameSize = parseFrameSize(payload.value(QStringLiteral("frameSize")).toObject(), QSize(1920, 1080));
        if (!overlay.frameSize.isValid())
            overlay.frameSize = QSize(1920, 1080);

        overlay.shapes = parseOverlayShapes(payload);
        if (overlay.shapes.isEmpty())
            overlay.shapes = parseOverlayShapes(root);

        const QJsonObject metrics = payload.value(QStringLiteral("metrics")).toObject();
        overlay.passLevel = payload.value(QStringLiteral("passLevel")).toString();
        if (overlay.passLevel.isEmpty())
            overlay.passLevel = metrics.value(QStringLiteral("passLevel")).toString();

        auto fetchDeviationFromObject = [&](const QJsonObject &obj) {
            double numericValue = 0.0;
            const QStringList deviationKeys = {
                QStringLiteral("deviation"),
                QStringLiteral("deviationMm"),
                QStringLiteral("offset"),
                QStringLiteral("offsetMm"),
                QStringLiteral("alignmentOffset"),
                QStringLiteral("alignmentOffsetMm"),
                QStringLiteral("plcControlMm"),
                QStringLiteral("value"),
                QStringLiteral("mm")
            };
            for (const QString &key : deviationKeys)
            {
                if (!obj.contains(key))
                    continue;
                if (extractNumeric(obj.value(key), numericValue))
                {
                    overlay.plcDeviationMm = numericValue;
                    break;
                }
            }

            const QStringList thresholdKeys = {
                QStringLiteral("warningThreshold"),
                QStringLiteral("warningThresholdMm"),
                QStringLiteral("threshold"),
                QStringLiteral("thresholdMm"),
                QStringLiteral("limit"),
                QStringLiteral("confidence")
            };
            for (const QString &key : thresholdKeys)
            {
                if (!obj.contains(key))
                    continue;
                if (extractNumeric(obj.value(key), numericValue))
                {
                    overlay.warningThresholdMm = numericValue;
                    break;
                }
            }
        };

        const QJsonObject plcControl = payload.value(QStringLiteral("plcControl")).toObject();
        if (!plcControl.isEmpty())
            fetchDeviationFromObject(plcControl);

        if (!overlay.plcDeviationMm.has_value() && metrics.contains(QStringLiteral("plcControlMm")))
        {
            double metricValue = 0.0;
            if (extractNumeric(metrics.value(QStringLiteral("plcControlMm")), metricValue))
                overlay.plcDeviationMm = metricValue;
        }

        if (!overlay.warningThresholdMm.has_value())
        {
            double metricThreshold = 0.0;
            if (metrics.contains(QStringLiteral("warningThresholdMm")) &&
                extractNumeric(metrics.value(QStringLiteral("warningThresholdMm")), metricThreshold))
            {
                overlay.warningThresholdMm = metricThreshold;
            }
        }

        if (!overlay.plcDeviationMm.has_value())
        {
            double topLevel = 0.0;
            if (extractNumeric(payload.value(QStringLiteral("plcControlMm")), topLevel))
                overlay.plcDeviationMm = topLevel;
        }

        if (overlay.shapes.isEmpty() && !overlay.plcDeviationMm.has_value())
            return;

        context.overlays.append(std::move(overlay));
    };

    while (!file.atEnd())
    {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;

        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        appendOverlay(doc.object());
    }

    std::sort(context.overlays.begin(), context.overlays.end(), [](const PlayerContext::TimedOverlay &a, const PlayerContext::TimedOverlay &b) {
        return a.timestampMs < b.timestampMs;
    });

    refreshStatusPanel(context, nullptr);
    updateDeviationMarkers(context);
}

void SessionPlaybackWidget::updateOverlayForPosition(PlayerContext &context, qint64 positionMs)
{
    if (!context.overlayWidget)
        return;

    if (context.overlays.isEmpty())
    {
        context.currentOverlayIndex = -1;
        context.overlayWidget->setShapes({});
        refreshStatusPanel(context, nullptr);
        return;
    }

    int bestIndex = context.currentOverlayIndex;
    const auto withinCurrent = [&]() -> bool {
        if (bestIndex < 0 || bestIndex >= context.overlays.size())
            return false;
        const qint64 currentTs = context.overlays.at(bestIndex).timestampMs;
        const qint64 nextTs = (bestIndex + 1 < context.overlays.size())
                                  ? context.overlays.at(bestIndex + 1).timestampMs
                                  : std::numeric_limits<qint64>::max();
        return currentTs <= positionMs && positionMs < nextTs;
    };

    if (!withinCurrent())
    {
        bestIndex = -1;
        for (int i = 0; i < context.overlays.size(); ++i)
        {
            if (context.overlays.at(i).timestampMs <= positionMs)
                bestIndex = i;
            else
                break;
        }
    }

    if (bestIndex == context.currentOverlayIndex)
        return;

    context.currentOverlayIndex = bestIndex;
    if (bestIndex < 0)
    {
        context.overlayWidget->setShapes({});
        refreshStatusPanel(context, nullptr);
        return;
    }

    const auto &overlay = context.overlays.at(bestIndex);
    if (context.videoView && overlay.frameSize.isValid() && context.lastFrameSize.isEmpty())
    {
        context.lastFrameSize = overlay.frameSize;
        context.videoView->updateVideoItemSize(overlay.frameSize);
    }
    context.overlayWidget->setNativeSize(overlay.frameSize);
    context.overlayWidget->setRoiRect(overlay.roi);
    context.overlayWidget->setShapes(overlay.shapes);
    refreshStatusPanel(context, &overlay);
}

void SessionPlaybackWidget::clearDisplayedOverlay(PlayerContext &context)
{
    context.currentOverlayIndex = -1;
    if (context.overlayWidget)
    {
        context.overlayWidget->setShapes({});
        context.overlayWidget->setRoiRect(QRect());
    }
    refreshStatusPanel(context, nullptr);
}

void SessionPlaybackWidget::refreshStatusPanel(PlayerContext &context, const PlayerContext::TimedOverlay *overlay)
{
    if (!context.statusPanel)
        return;

    if (context.track.filePath.isEmpty())
    {
        context.statusPanel->clear();
        context.statusPanel->setVisible(false);
        return;
    }

    QString passLevel = context.track.passLevel.trimmed();
    if (passLevel.isEmpty())
        passLevel = m_defaultPassLevel;
    if (overlay && !overlay->passLevel.trimmed().isEmpty())
        passLevel = overlay->passLevel.trimmed();

    context.statusPanel->setPassInfo(passLevel);

    std::optional<double> deviation = overlay ? overlay->plcDeviationMm : std::nullopt;

    std::optional<double> threshold;
    if (overlay && overlay->warningThresholdMm.has_value() && overlay->warningThresholdMm.value() > 0.0)
        threshold = overlay->warningThresholdMm;
    else if (m_confidenceThreshold > 0.0)
        threshold = m_confidenceThreshold;

    context.statusPanel->setAlignmentOffset(deviation, threshold);
    context.statusPanel->setVisible(true);
}

void SessionPlaybackWidget::updateDeviationMarkers(PlayerContext &context)
{
    if (!context.deviationBar)
        return;

    const qint64 duration = context.player ? context.player->duration() : 0;
    const qint64 effectiveDuration = duration > context.alignmentOffsetMs ? duration - context.alignmentOffsetMs : 0;
    if (effectiveDuration <= 0 || context.track.filePath.isEmpty())
    {
        context.deviationBar->setMarkers({});
        context.deviationBar->setVisible(false);
        return;
    }

    QVector<double> markers;
    markers.reserve(context.overlays.size());
    for (const auto &overlay : context.overlays)
    {
        double threshold = 0.0;
        if (overlay.warningThresholdMm.has_value() && overlay.warningThresholdMm.value() > 0.0)
            threshold = overlay.warningThresholdMm.value();
        else
            threshold = m_confidenceThreshold;

        if (threshold <= 0.0)
            continue;
        if (!overlay.plcDeviationMm.has_value())
            continue;
        if (std::abs(overlay.plcDeviationMm.value()) <= threshold)
            continue;

        const qint64 adjustedTs = overlay.timestampMs - context.alignmentOffsetMs;
        if (adjustedTs < 0)
            continue;
        const qint64 clampedTs = std::clamp<qint64>(adjustedTs, 0, effectiveDuration);
        const double ratio = static_cast<double>(clampedTs) / std::max<qint64>(1, effectiveDuration);
        markers.append(ratio);
    }

    context.deviationBar->setMarkers(markers);
    context.deviationBar->setVisible(true);
}

void SessionPlaybackWidget::updateHudGeometry(PlayerContext &context)
{
    if (!context.videoContainer)
        return;

    const QRect bounds = context.videoContainer->rect();
    if (context.overlayWidget)
    {
        context.overlayWidget->setGeometry(bounds);
        context.overlayWidget->update();
    }
    if (context.hudOverlay)
    {
        context.hudOverlay->setGeometry(bounds);
    }
    int reservedRightMargin = 0;
    if (context.zoomButtonOverlay)
    {
        constexpr int kHudSideMargin = 16;
        constexpr int kProgressMinWidth = 160;
        constexpr int kProgressGapPx = 6;

        const int margin = kHudSideMargin;
        const QSize sizeHint = context.zoomButtonOverlay->sizeHint();
        const QSize overlaySize = QSize(
            std::max(sizeHint.width(), context.zoomButton ? context.zoomButton->width() : 0),
            std::max(sizeHint.height(), context.zoomButton ? context.zoomButton->height() : 0));
        const int x = std::max(margin, bounds.width() - overlaySize.width() - margin);
        const int y = std::max(margin, bounds.height() - overlaySize.height() - margin);
        context.zoomButtonOverlay->setGeometry(QRect(x, y, overlaySize.width(), overlaySize.height()));

        const int layoutWidth = std::max(0, bounds.width() - (kHudSideMargin * 2));
        const int desiredPadding = overlaySize.width() + kProgressGapPx;
        if (layoutWidth <= kProgressMinWidth)
        {
            reservedRightMargin = std::max(0, layoutWidth - kProgressMinWidth);
        }
        else if (layoutWidth - desiredPadding < kProgressMinWidth)
        {
            reservedRightMargin = std::max(0, layoutWidth - kProgressMinWidth);
        }
        else
        {
            reservedRightMargin = desiredPadding;
        }
    }
    if (context.deviationBar)
        context.deviationBar->setTrailingPadding(reservedRightMargin);
    if (context.overlayWidget)
        context.overlayWidget->raise();
    if (context.hudOverlay)
        context.hudOverlay->raise();
    if (context.zoomButtonOverlay)
        context.zoomButtonOverlay->raise();
}

void SessionPlaybackWidget::refreshAllStatusPanels()
{
    for (auto &ctx : m_players)
    {
        const PlayerContext::TimedOverlay *overlay = nullptr;
        if (ctx.currentOverlayIndex >= 0 && ctx.currentOverlayIndex < ctx.overlays.size())
            overlay = &ctx.overlays.at(ctx.currentOverlayIndex);
        refreshStatusPanel(ctx, overlay);
    }
}

bool SessionPlaybackWidget::hasActiveTracks() const
{
    return std::any_of(m_players.begin(), m_players.end(), [](const PlayerContext &ctx) {
        return !ctx.track.filePath.isEmpty();
    });
}

void SessionPlaybackWidget::applyFocus(int index)
{
    if (!m_splitter)
        return;

    const int activeCount = std::max(1, m_activeTrackCount);
    const bool validFocus = (index >= 0 && index < m_activeTrackCount);
    m_focusedIndex = validFocus ? index : -1;

    for (int i = 0; i < m_players.size(); ++i)
    {
        const bool withinActive = (m_activeTrackCount == 0) ? (i == 0) : (i < m_activeTrackCount);
        const bool shouldShow = withinActive && (!validFocus || i == index);
        if (QWidget *panel = m_splitter->widget(i))
            panel->setVisible(shouldShow);
    }

    QList<int> sizes;
    if (m_activeTrackCount <= 1)
    {
        sizes << 1;
        for (int i = 1; i < m_players.size(); ++i)
            sizes << 0;
    }
    else if (validFocus)
    {
        for (int i = 0; i < m_players.size(); ++i)
            sizes << (i == index ? 1 : 0);
    }
    else
    {
        for (int i = 0; i < m_players.size(); ++i)
            sizes << (i < m_activeTrackCount ? 1 : 0);
    }
    m_splitter->setSizes(sizes);

    if (!validFocus)
        scheduleSplitterEqualization();

    const int handleCount = m_splitter->count() - 1;
    for (int handleIndex = 1; handleIndex <= handleCount; ++handleIndex)
    {
        if (QSplitterHandle *handle = m_splitter->handle(handleIndex))
            handle->setVisible(m_activeTrackCount > 1 && !validFocus);
    }

    updateCameraSelectionState();
}

void SessionPlaybackWidget::updateTimeLabels()
{
    if (!m_currentTimeLabel || !m_totalTimeLabel)
        return;

    qint64 masterPosition = 0;
    for (const auto &ctx : m_players)
    {
        if (!ctx.player)
            continue;
        qint64 relative = ctx.player->position() - ctx.alignmentOffsetMs;
        if (relative < 0)
            relative = 0;
        masterPosition = std::max(masterPosition, relative);
    }

    m_currentTimeLabel->setText(formatTime(masterPosition));
    m_totalTimeLabel->setText(formatTime(m_durationMs));
}

void SessionPlaybackWidget::syncPlayPauseButton()
{
    if (!m_playPauseButton)
        return;

    bool anyPlaying = false;
    for (const auto &ctx : m_players)
    {
        if (ctx.player && ctx.player->playbackState() == QMediaPlayer::PlayingState)
        {
            anyPlaying = true;
            break;
        }
    }

    const bool usePauseIcon = anyPlaying;
    m_playPauseButton->setIcon(usePauseIcon ? m_pauseIcon : m_playIcon);
    const QString tooltip = usePauseIcon ? tr("Pause") : tr("Play");
    m_playPauseButton->setToolTip(tooltip);
    m_playPauseButton->setAccessibleName(tooltip);
}

QString SessionPlaybackWidget::formatTime(qint64 positionMs) const
{
    if (positionMs <= 0)
        return QStringLiteral("00:00");

    const qint64 totalSeconds = positionMs / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;

    if (hours > 0)
        return QStringLiteral("%1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));

    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

void SessionPlaybackWidget::handlePlayerPositionChanged(qint64)
{
    if (!m_positionSlider || m_sliderPressed)
        return;

    if (m_durationMs <= 0)
    {
        m_positionSlider->setValue(0);
        updateTimeLabels();
        return;
    }

    qint64 masterPosition = 0;
    for (const auto &ctx : m_players)
    {
        if (!ctx.player)
            continue;
        qint64 relative = ctx.player->position() - ctx.alignmentOffsetMs;
        if (relative < 0)
            relative = 0;
        masterPosition = std::max(masterPosition, relative);
    }

    const int sliderValue = static_cast<int>((masterPosition * 1000) / std::max<qint64>(1, m_durationMs));
    m_internalSliderUpdate = true;
    m_positionSlider->setValue(std::clamp(sliderValue, 0, 1000));
    m_internalSliderUpdate = false;
    updateTimeLabels();

    for (auto &ctx : m_players)
    {
        const qint64 actualPosition = ctx.player ? ctx.player->position() : (masterPosition + ctx.alignmentOffsetMs);
        const qint64 relative = std::max<qint64>(0, actualPosition - ctx.alignmentOffsetMs);
        updateOverlayForPosition(ctx, actualPosition);
        if (ctx.deviationBar)
        {
            const qint64 duration = ctx.player ? ctx.player->duration() : 0;
            const qint64 effective = duration > ctx.alignmentOffsetMs ? duration - ctx.alignmentOffsetMs : 0;
            if (effective > 0)
            {
                const qint64 clamped = std::clamp<qint64>(relative, 0, effective);
                ctx.deviationBar->setPosition(static_cast<double>(clamped) / static_cast<double>(effective));
            }
            else
            {
                ctx.deviationBar->setPosition(0.0);
            }
        }
    }
}

void SessionPlaybackWidget::rebuildCameraSelectionBar()
{
    if (!m_cameraButtonsLayout || !m_bottomBar || !m_cameraButtonGroup)
        return;

    m_updatingCameraBar = true;

    const auto existingButtons = m_cameraButtonGroup->buttons();
    for (QAbstractButton *btn : existingButtons)
        m_cameraButtonGroup->removeButton(btn);

    while (QLayoutItem *item = m_cameraButtonsLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    m_allTracksButton = nullptr;
    m_cameraButtons.fill(nullptr);

    m_allTracksButton = new QPushButton(tr("ALL"), m_bottomBar);
    m_allTracksButton->setCheckable(true);
    m_allTracksButton->setProperty("trackIndex", -1);
    styleCameraSelectionButton(m_allTracksButton);
    m_cameraButtonsLayout->addWidget(m_allTracksButton);
    m_cameraButtonGroup->addButton(m_allTracksButton);

    for (int i = 0; i < m_players.size(); ++i)
    {
        const auto &ctx = m_players.at(i);
        if (ctx.track.filePath.isEmpty())
            continue;

        auto *button = new QPushButton(ctx.track.displayName.isEmpty() ? tr("Camera %1").arg(i + 1) : ctx.track.displayName, m_bottomBar);
        button->setCheckable(true);
        button->setProperty("trackIndex", i);
        styleCameraSelectionButton(button);
        m_cameraButtonsLayout->addWidget(button);
        m_cameraButtonGroup->addButton(button);
        if (i < m_cameraButtons.size())
            m_cameraButtons[i] = button;
    }

    m_updatingCameraBar = false;
    updateCameraSelectionState();
}

void SessionPlaybackWidget::updateCameraSelectionState()
{
    if (!m_cameraButtonGroup)
        return;

    m_updatingCameraBar = true;

    if (m_allTracksButton)
    {
        const bool hasTracks = m_activeTrackCount > 0;
        m_allTracksButton->setEnabled(hasTracks);
        m_allTracksButton->setChecked(m_focusedIndex < 0 || !hasTracks);
    }

    for (int i = 0; i < m_cameraButtons.size(); ++i)
    {
        QPushButton *button = m_cameraButtons.value(i);
        if (!button)
            continue;

        const bool active = (i < m_players.size() && !m_players[i].track.filePath.isEmpty());
        button->setVisible(active);
        button->setEnabled(active);
        button->setChecked(active && m_focusedIndex == i);
    }

    m_updatingCameraBar = false;
}

void SessionPlaybackWidget::handleZoomToggle(int index)
{
    if (index < 0 || index >= m_players.size())
        return;

    auto &ctx = m_players[index];
    if (ctx.track.filePath.isEmpty())
        return;
    ctx.zoomed = !ctx.zoomed;
    if (ctx.videoView)
    {
        ctx.videoView->setZoomed(ctx.zoomed);
        ctx.videoView->setZoomFactor(ctx.zoomed ? kPlaybackZoomFactor : 1.0);
    }
    updateZoomButton(ctx);
}

void SessionPlaybackWidget::updateZoomButton(PlayerContext &context)
{
    if (!context.zoomButton)
        return;

    const QString iconPath = context.zoomed ? QStringLiteral(":/icons/wd_cp_zoomout.svg")
                                            : QStringLiteral(":/icons/wd_cp_zoomin.svg");
    context.zoomButton->setIcon(QIcon(iconPath));
    context.zoomButton->setChecked(context.zoomed);
    const QString tooltip = context.zoomed ? tr("Zoom out") : tr("Zoom in");
    context.zoomButton->setToolTip(tooltip);
    context.zoomButton->setAccessibleName(tooltip);
    const bool hasTrack = !context.track.filePath.isEmpty();
    context.zoomButton->setEnabled(hasTrack);
}

void SessionPlaybackWidget::handlePlaybackStateChanged(PlayerContext &context, QMediaPlayer::PlaybackState state)
{
    syncPlayPauseButton();

    if (!context.player)
        return;

    if (state == QMediaPlayer::StoppedState)
    {
        const QMediaPlayer::MediaStatus status = context.player->mediaStatus();
        if (status == QMediaPlayer::EndOfMedia && !context.track.filePath.isEmpty())
        {
            const qint64 duration = context.player->duration();
            if (duration > 0)
            {
                const qint64 lastFrame = std::max<qint64>(0, duration - 1);
                context.player->setPosition(lastFrame);
                context.player->setPlaybackRate(m_playbackRate);
                context.player->play();
                context.player->pause();
                context.player->setPosition(lastFrame);
                updateOverlayForPosition(context, lastFrame);
            }
            return;
        }

        clearDisplayedOverlay(context);
    }
}

void SessionPlaybackWidget::handlePlayerDurationChanged()
{
    qint64 maxDuration = 0;
    for (const auto &ctx : m_players)
    {
        if (!ctx.player)
            continue;
        const qint64 rawDuration = ctx.player->duration();
        if (rawDuration <= 0)
            continue;
        const qint64 effective = std::max<qint64>(0, rawDuration - ctx.alignmentOffsetMs);
        maxDuration = std::max(maxDuration, effective);
    }

    m_durationMs = maxDuration;
    if (m_durationMs <= 0 && m_positionSlider)
        m_positionSlider->setValue(0);

    updateTimeLabels();

    for (auto &ctx : m_players)
        updateDeviationMarkers(ctx);
}

void SessionPlaybackWidget::handlePlayerError(PlayerContext &context, QMediaPlayer::Error error, const QString &message)
{
    Q_UNUSED(error);
    emit playbackFailed(context.track.cameraId, message);
}

void SessionPlaybackWidget::handlePlayPauseRequested()
{
    const bool anyPlaying = std::any_of(m_players.begin(), m_players.end(), [](const PlayerContext &ctx) {
        return ctx.player && ctx.player->playbackState() == QMediaPlayer::PlayingState;
    });

    for (auto &ctx : m_players)
    {
        if (!ctx.player || ctx.track.filePath.isEmpty())
            continue;
        if (anyPlaying)
            ctx.player->pause();
        else
        {
            ctx.player->setPlaybackRate(m_playbackRate);
            ctx.player->play();
        }
    }

    syncPlayPauseButton();
}

void SessionPlaybackWidget::handleSeekRequested(int sliderValue)
{
    if (!m_positionSlider || m_internalSliderUpdate || m_durationMs <= 0)
        return;

    const qint64 target = (m_durationMs * sliderValue) / 1000;
    for (auto &ctx : m_players)
    {
        const qint64 offset = ctx.alignmentOffsetMs;
        const qint64 effectiveDuration = ctx.player ? std::max<qint64>(0, ctx.player->duration() - offset) : m_durationMs;
        qint64 actualTarget = target + offset;
        if (ctx.player && !ctx.track.filePath.isEmpty())
        {
            if (ctx.player->duration() > 0)
                actualTarget = std::clamp<qint64>(actualTarget, 0, ctx.player->duration());
            ctx.player->setPosition(actualTarget);
            if (ctx.player->playbackState() == QMediaPlayer::StoppedState)
            {
                ctx.player->setPlaybackRate(m_playbackRate);
                ctx.player->play();
                ctx.player->pause();
                ctx.player->setPosition(actualTarget);
            }
        }
        updateOverlayForPosition(ctx, actualTarget);
        if (ctx.deviationBar)
        {
            if (effectiveDuration > 0)
            {
                const qint64 clamped = std::clamp<qint64>(target, 0, effectiveDuration);
                ctx.deviationBar->setPosition(static_cast<double>(clamped) / static_cast<double>(effectiveDuration));
            }
            else
            {
                ctx.deviationBar->setPosition(0.0);
            }
        }
    }

    updateTimeLabels();
}

void SessionPlaybackWidget::handleSliderPressed()
{
    m_sliderPressed = true;
}

void SessionPlaybackWidget::handleSliderReleased()
{
    m_sliderPressed = false;
    if (m_positionSlider)
        handleSeekRequested(m_positionSlider->value());
}

void SessionPlaybackWidget::handleSpeedToggle()
{
    static const QList<double> rates{0.5, 1.0, 1.5, 2.0};
    int index = rates.indexOf(m_playbackRate);
    if (index < 0)
        index = 0;
    index = (index + 1) % rates.size();
    m_playbackRate = rates.at(index);

    for (auto &ctx : m_players)
    {
        if (ctx.player)
            ctx.player->setPlaybackRate(m_playbackRate);
    }

    if (m_speedButton)
        m_speedButton->setText(speedLabel(m_playbackRate));
}
