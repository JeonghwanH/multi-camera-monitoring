// Author: SeungJae Lee
// SessionPlaybackWidget interface: playback surface for recorded weld sessions with overlays and metrics.

#pragma once

#include <QWidget>
#include <QVector>
#include <QString>
#include <QMediaPlayer>
#include <QRect>
#include <QSize>
#include <QIcon>

#include "AnalysisOverlayWidget.h"

#include <optional>

class QLabel;
class QPushButton;
class QSlider;
class QAudioOutput;
class QMediaPlayer;
class QToolButton;
class TwoCameraSplitter;
class ZoomableVideoView;
class QShowEvent;
class AnalysisStatusPanel;
class PlaybackDeviationBar;
class QButtonGroup;
class QHBoxLayout;
class QVBoxLayout;

class SessionPlaybackWidget : public QWidget
{
    Q_OBJECT

public:
    struct Track
    {
        QString cameraId;
        QString displayName;
        QString filePath;
        QString analysisPath;
        qint64 startedAtMs = 0;
        QString passLevel;
    };

    explicit SessionPlaybackWidget(QWidget *parent = nullptr);

    void setTracks(const QVector<Track> &tracks);
    bool hasActiveTracks() const;
    void setConfidenceThreshold(double threshold);
    void setDefaultPassInfo(const QString &passLevel);
    void setOverlayPointSize(double sizePx);

signals:
    void playbackFailed(const QString &cameraId, const QString &message);

private:
    struct PlayerContext
    {
        QMediaPlayer *player = nullptr;
        QAudioOutput *audioOutput = nullptr;
        QWidget *videoContainer = nullptr;
        ZoomableVideoView *videoView = nullptr;
        AnalysisOverlayWidget *overlayWidget = nullptr;
        QWidget *hudOverlay = nullptr;
        QWidget *zoomButtonOverlay = nullptr;
        QWidget *panel = nullptr;
        AnalysisStatusPanel *statusPanel = nullptr;
        PlaybackDeviationBar *deviationBar = nullptr;
        QLabel *titleLabel = nullptr;
        QToolButton *zoomButton = nullptr;
        Track track;
        QSize lastFrameSize;
        bool zoomed = false;
        qint64 alignmentOffsetMs = 0;
        struct TimedOverlay
        {
            qint64 timestampMs = 0;
            QVector<AnalysisOverlayWidget::Shape> shapes;
            QRect roi;
            QSize frameSize;
            QString passLevel;
            std::optional<double> plcDeviationMm;
            std::optional<double> warningThresholdMm;
        };
        QVector<TimedOverlay> overlays; // cached overlay frames keyed by timestamp
        int currentOverlayIndex = -1;
    };

    void buildUi();
    void connectPlayerSignals(PlayerContext &context);
    void clearTracks();
    void applyFocus(int index);
    void updateTimeLabels();
    void syncPlayPauseButton();
    QString formatTime(qint64 positionMs) const;
    void loadOverlayData(PlayerContext &context);
    void updateOverlayForPosition(PlayerContext &context, qint64 positionMs);
    void refreshStatusPanel(PlayerContext &context, const PlayerContext::TimedOverlay *overlay);
    void updateDeviationMarkers(PlayerContext &context);
    void updateHudGeometry(PlayerContext &context);
    void refreshAllStatusPanels();
    void rebuildCameraSelectionBar();
    void updateCameraSelectionState();
    void handleZoomToggle(int index);
    void updateZoomButton(PlayerContext &context);
    void handlePlaybackStateChanged(PlayerContext &context, QMediaPlayer::PlaybackState state);
    void clearDisplayedOverlay(PlayerContext &context);

    void handlePlayerPositionChanged(qint64 position);
    void handlePlayerDurationChanged();
    void handlePlayerError(PlayerContext &context, QMediaPlayer::Error error, const QString &message);

    void handlePlayPauseRequested();
    void handleSeekRequested(int sliderValue);
    void handleSliderPressed();
    void handleSliderReleased();
    void handleSpeedToggle();

    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    void equalizeSplitter();
    void scheduleSplitterEqualization();

    QVector<PlayerContext> m_players;
    TwoCameraSplitter *m_splitter = nullptr;
    QSlider *m_positionSlider = nullptr;
    QPushButton *m_playPauseButton = nullptr;
    QPushButton *m_speedButton = nullptr;
    QLabel *m_currentTimeLabel = nullptr;
    QLabel *m_totalTimeLabel = nullptr;
    QWidget *m_bottomBar = nullptr;
    QHBoxLayout *m_bottomBarLayout = nullptr;
    QHBoxLayout *m_cameraButtonsLayout = nullptr;
    QButtonGroup *m_cameraButtonGroup = nullptr;
    QPushButton *m_allTracksButton = nullptr;
    QVector<QPushButton *> m_cameraButtons;
    bool m_updatingCameraBar = false;
    QIcon m_playIcon;
    QIcon m_pauseIcon;

    qint64 m_durationMs = 0;
    double m_playbackRate = 1.0;
    int m_focusedIndex = -1;
    bool m_sliderPressed = false;
    bool m_internalSliderUpdate = false;
    double m_confidenceThreshold = 0.0;
    QString m_defaultPassLevel = QStringLiteral("Root");
    int m_activeTrackCount = 0;
    double m_overlayPointSizePx = 8.0;
};
