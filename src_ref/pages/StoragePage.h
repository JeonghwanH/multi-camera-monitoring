// Author: SeungJae Lee
// StoragePage interface: presents recording sessions, storage metrics, and playback overlay controls.

#pragma once

#include <QHash>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "managers/RecordingManager.h"

class QLabel;
class QProgressBar;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QPushButton;
class QToolButton;
class QStackedLayout;
class QFrame;
class QScrollArea;
class SessionPlaybackWidget;
class AiClient;
class CameraManager;

class StoragePage : public QWidget
{
    Q_OBJECT

public:
    explicit StoragePage(CameraManager *cameraManager, RecordingManager *recordingManager, AiClient *aiClient, QWidget *parent = nullptr);

private:
    struct RecordingSession;

    void changeEvent(QEvent *event) override;
    void buildUi();
    void buildOverlayUi();
    void retranslateUi();
    void refreshContent();
    void rebuildSessions();
    void refreshSections();
    void populateRecentSessions();
    void populateSessionList();
    void populateCorruptedList();
    void updateStorageSummary();
    void selectSessionById(const QString &sessionId, bool userInitiated = false);
    void updatePlaybackForSelection(const RecordingSession *session);
    void showPlaybackOverlay(const RecordingSession *session);
    void hidePlaybackOverlay();
    void updateOverlayForSession(const RecordingSession *session, const QString &statusMessage = QString());
    QString slotSummary(const RecordingSession &session) const;
    QString buildSessionTitle(const RecordingSession &session) const;
    void updateSessionRowSelectionStyles();
    void updateRecentCardSelectionStyles();
    void applySessionRowStyle(QWidget *row, bool selected) const;
    QString elidedText(const QString &text, int maxLength) const;
    void deleteSession(const QString &sessionId);
    void openSessionFolder(const QString &sessionId);
    void showSessionOptionsMenu(const QString &sessionId, QWidget *anchor);
    void renameSession(const QString &sessionId, QWidget *dialogParent);
    void loadCustomSessionTitles();
    void saveCustomSessionTitles() const;
    QWidget *createSessionListRow(int sessionIndex) const;
    QWidget *createCorruptedRow(int sessionIndex) const;
    QWidget *createRecentSessionCard(int sessionIndex) const;
    QString formatDuration(qint64 durationMs) const;
    QString formatFileSize(qint64 bytes) const;
    void clearLayout(QLayout *layout);
    QString sessionTitleFor(const RecordingManager::RecordingMetadata &metadata) const;
    void addOrUpdateRecording(const RecordingManager::RecordingMetadata &metadata);
    void removeRecording(const QString &filePath);
    bool eventFilter(QObject *watched, QEvent *event) override;
    QString displayNameForCamera(const QString &cameraId) const;

    struct RecordingSession
    {
        QString id;
        QStringList slotIds;
        QString title;
        QString cameraSummary;
        QDateTime startedAtUtc;
        QDateTime finishedAtUtc;
        qint64 totalDurationMs = 0;
        qint64 totalSizeBytes = 0;
        bool hasMissingFiles = false;
        bool incomplete = false;
        int expectedTrackCount = 0;
        QVector<RecordingManager::RecordingMetadata> recordings;

        bool isCorrupted() const
        {
            return hasMissingFiles || incomplete;
        }
    };

    // Managers providing recording metadata and camera labels for the page.
    RecordingManager *m_recordingManager = nullptr;
    CameraManager *m_cameraManager = nullptr;
    AiClient *m_aiClient = nullptr;
    QStackedLayout *m_rootStack = nullptr;
    QWidget *m_mainPage = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_contentWidget = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_recentTitleLabel = nullptr;
    QWidget *m_recentSection = nullptr;
    QVBoxLayout *m_recentLayout = nullptr;
    QScrollArea *m_recentScrollArea = nullptr;
    QWidget *m_recentScrollContent = nullptr;
    QHBoxLayout *m_recentCardsLayout = nullptr;
    QLabel *m_recentEmptyLabel = nullptr;
    QWidget *m_storageSummarySection = nullptr;
    QLabel *m_storageTitleLabel = nullptr;
    QLabel *m_storageUsageLabel = nullptr;
    QProgressBar *m_storageProgress = nullptr;
    QLabel *m_sessionListTitleLabel = nullptr;
    QWidget *m_sessionListContainer = nullptr;
    QVBoxLayout *m_sessionListLayout = nullptr;
    QLabel *m_corruptedTitleLabel = nullptr;
    QWidget *m_corruptedListContainer = nullptr;
    QVBoxLayout *m_corruptedListLayout = nullptr;
    QWidget *m_overlayPage = nullptr;
    QLabel *m_overlayStatusLabel = nullptr;
    QLabel *m_overlayTitleLabel = nullptr;
    QToolButton *m_overlayCloseButton = nullptr;
    SessionPlaybackWidget *m_fullscreenPlaybackWidget = nullptr;

    // Cached session model and lookup tables used to drive the UI.
    QVector<RecordingSession> m_sessions;
    QHash<QString, int> m_fileToSessionIndex;
    QHash<QWidget *, QString> m_sessionRowByWidget;
    QHash<QWidget *, QString> m_recentCardByWidget;
    QString m_selectedSessionId;
    qint64 m_totalStorageBytes = 0;
    qint64 m_usedStorageBytes = 0;
    QHash<QString, QString> m_customSessionTitles;
};
