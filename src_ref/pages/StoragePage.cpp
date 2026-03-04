// Author: SeungJae Lee
// StoragePage: manages recorded session browsing, summaries, and playback overlays.

#include "StoragePage.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QCryptographicHash>
#include <QImage>
#include <QFontMetrics>
#include <QPainterPath>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <QAction>
#include <QMenu>
#include <QInputDialog>
#include <QSettings>
#include <QSet>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QToolButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStorageInfo>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QPointer>
#include <QRegion>
#include <QResizeEvent>
#include <QTransform>
#include <QtGlobal>
#include <QVideoSink>
#include <QVideoFrame>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "managers/CameraManager.h"
#include "widgets/SessionPlaybackWidget.h"
#include "managers/AiClient.h"
namespace
{
// Helpers for formatting and lightweight view widgets used in the storage UI.
constexpr int kMaxRecentSessions = 5;
constexpr qint64 kSessionGroupingToleranceMs = 1500; // Group recordings starting within 1.5 seconds.
constexpr int kContentWidth = 960;

QString formattedDateTime(const QDateTime &time)
{
    if (!time.isValid())
        return QString();
    return time.toLocalTime().toString("yyyy-MM-dd hh:mm");
}

class RoundedClipFrame : public QFrame
{
public:
    explicit RoundedClipFrame(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setAttribute(Qt::WA_StyledBackground, true);
    }

    void setCornerRadii(int topLeft, int topRight, int bottomRight, int bottomLeft)
    {
        if (m_topLeft == topLeft && m_topRight == topRight && m_bottomRight == bottomRight && m_bottomLeft == bottomLeft)
            return;

        m_topLeft = topLeft;
        m_topRight = topRight;
        m_bottomRight = bottomRight;
        m_bottomLeft = bottomLeft;
        applyMask();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QFrame::resizeEvent(event);
        applyMask();
    }

private:
    void applyMask()
    {
        const QRect rect = this->rect();
        if (rect.isEmpty())
        {
            clearMask();
            return;
        }

        const int width = rect.width();
        const int height = rect.height();

        const int tl = qBound(0, m_topLeft, qMin(width, height) / 2);
        const int tr = qBound(0, m_topRight, qMin(width, height) / 2);
        const int br = qBound(0, m_bottomRight, qMin(width, height) / 2);
        const int bl = qBound(0, m_bottomLeft, qMin(width, height) / 2);

        QPainterPath path;
        path.moveTo(rect.left() + tl, rect.top());
        path.lineTo(rect.right() - tr, rect.top());
        if (tr > 0)
            path.quadTo(rect.right(), rect.top(), rect.right(), rect.top() + tr);
        else
            path.lineTo(rect.right(), rect.top());

        path.lineTo(rect.right(), rect.bottom() - br);
        if (br > 0)
            path.quadTo(rect.right(), rect.bottom(), rect.right() - br, rect.bottom());
        else
            path.lineTo(rect.right(), rect.bottom());

        path.lineTo(rect.left() + bl, rect.bottom());
        if (bl > 0)
            path.quadTo(rect.left(), rect.bottom(), rect.left(), rect.bottom() - bl);
        else
            path.lineTo(rect.left(), rect.bottom());

        path.lineTo(rect.left(), rect.top() + tl);
        if (tl > 0)
            path.quadTo(rect.left(), rect.top(), rect.left() + tl, rect.top());
        else
            path.lineTo(rect.left(), rect.top());

        path.closeSubpath();
        setMask(QRegion(path.toFillPolygon(QTransform()).toPolygon()));
    }

    int m_topLeft = 0;
    int m_topRight = 0;
    int m_bottomRight = 0;
    int m_bottomLeft = 0;
};

class PreviewImageLabel : public QLabel
{
public:
    explicit PreviewImageLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setStyleSheet(QStringLiteral("background:#1E1F24;border:none;"));
    }

    void setImage(const QImage &image)
    {
        m_image = image;
        updateScaledPixmap();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateScaledPixmap();
    }

private:
    void updateScaledPixmap()
    {
        if (m_image.isNull())
        {
            clear();
            return;
        }

        const QSize targetSize = size();
        if (!targetSize.isValid())
            return;

        const QPixmap pixmap = QPixmap::fromImage(m_image);
        setPixmap(pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    QImage m_image;
};

class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setFullText(const QString &text)
    {
        if (m_fullText == text)
            return;
        m_fullText = text;
        setToolTip(m_fullText);
        updateElision();
    }

    void setElideMode(Qt::TextElideMode mode)
    {
        if (m_elideMode == mode)
            return;
        m_elideMode = mode;
        updateElision();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateElision();
    }

private:
    void updateElision()
    {
        const int availableWidth = qMax(0, width());
        const QFontMetrics metrics(font());
        const QString elided = metrics.elidedText(m_fullText, m_elideMode, availableWidth);
        QLabel::setText(elided);
    }

    QString m_fullText;
    Qt::TextElideMode m_elideMode = Qt::ElideRight;
};
} // namespace

StoragePage::StoragePage(CameraManager *cameraManager, RecordingManager *recordingManager, AiClient *aiClient, QWidget *parent)
    : QWidget(parent)
    , m_recordingManager(recordingManager)
    , m_cameraManager(cameraManager)
    , m_aiClient(aiClient)
{
    buildUi();
    retranslateUi();

    if (m_recordingManager)
    {
        connect(m_recordingManager, &RecordingManager::recordingAdded, this, &StoragePage::addOrUpdateRecording);
        connect(m_recordingManager, &RecordingManager::recordingRemoved, this, &StoragePage::removeRecording);
    }

    if (m_aiClient)
    {
        connect(m_aiClient, &AiClient::settingsChanged, this, [this](const AiClient::Settings &settings) {
            if (!m_fullscreenPlaybackWidget)
                return;
            m_fullscreenPlaybackWidget->setConfidenceThreshold(settings.confidenceThreshold);
            m_fullscreenPlaybackWidget->setDefaultPassInfo(settings.passLevel.trimmed());
            m_fullscreenPlaybackWidget->setOverlayPointSize(settings.detectionDotSizePx);
        });
    }

    if (m_cameraManager)
    {
        connect(m_cameraManager, &CameraManager::cameraAdded, this, [this](const CameraManager::CameraInfo &) {
            refreshContent();
        });
        connect(m_cameraManager, &CameraManager::cameraUpdated, this, [this](const CameraManager::CameraInfo &) {
            refreshContent();
        });
        connect(m_cameraManager, &CameraManager::cameraRemoved, this, [this](const QString &) {
            refreshContent();
        });
    }

    loadCustomSessionTitles();
    refreshContent();
}

void StoragePage::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        QWidget::changeEvent(event);
        retranslateUi();
        refreshSections();
        return;
    }

    QWidget::changeEvent(event);
}

void StoragePage::buildUi()
{
    // Construct stacked layout: main list view with scrollable sections plus an overlay playback layer.
    m_rootStack = new QStackedLayout(this);
    m_rootStack->setContentsMargins(0, 0, 0, 0);
    m_rootStack->setStackingMode(QStackedLayout::StackOne);

    m_mainPage = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(m_mainPage);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    m_mainPage->setStyleSheet(QStringLiteral("background-color:#111113;border-radius:12px;"));

    m_scrollArea = new QScrollArea(m_mainPage);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea{background-color:#111113;border:none;}"
        "QScrollArea > QWidget{background-color:#111113;}"));
    mainLayout->addWidget(m_scrollArea);

    m_contentWidget = new QWidget(m_scrollArea);
    m_scrollArea->setWidget(m_contentWidget);
    m_contentWidget->setStyleSheet(QStringLiteral("background-color:#111113;"));

    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(32, 32, 32, 40);
    m_contentLayout->setSpacing(32);

    auto *titleWrapper = new QWidget(m_contentWidget);
    titleWrapper->setFixedWidth(kContentWidth);
    auto *titleLayout = new QHBoxLayout(titleWrapper);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);

    m_titleLabel = new QLabel(titleWrapper);
    m_titleLabel->setObjectName(QStringLiteral("storagePageTitle"));
    m_titleLabel->setStyleSheet(QStringLiteral(
        "#storagePageTitle{font-size:18px;font-weight:600;color:rgba(255,255,255,0.4);background:transparent;}"));
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();
    m_contentLayout->addWidget(titleWrapper, 0, Qt::AlignHCenter);

    // Recent recordings section
    auto *recentContainer = new QWidget(m_contentWidget);
    recentContainer->setFixedWidth(kContentWidth);
    auto *recentContainerLayout = new QVBoxLayout(recentContainer);
    recentContainerLayout->setContentsMargins(0, 0, 0, 0);
    recentContainerLayout->setSpacing(12);

    m_recentTitleLabel = new QLabel(recentContainer);
    m_recentTitleLabel->setStyleSheet(QStringLiteral("font-size:24px;font-weight:700;color:#FFFFFF;background:transparent;"));
    recentContainerLayout->addWidget(m_recentTitleLabel);

    m_recentSection = new QWidget(recentContainer);
    m_recentSection->setObjectName(QStringLiteral("recentSessions"));
    m_recentSection->setFixedSize(kContentWidth, 322);
    m_recentSection->setAttribute(Qt::WA_StyledBackground, true);
    m_recentSection->setStyleSheet(QStringLiteral("#recentSessions{background:transparent;}"));
    m_recentLayout = new QVBoxLayout(m_recentSection);
    m_recentLayout->setContentsMargins(0, 0, 0, 0);
    m_recentLayout->setSpacing(0);

    m_recentScrollArea = new QScrollArea(m_recentSection);
    m_recentScrollArea->setWidgetResizable(false);
    m_recentScrollArea->setFrameShape(QFrame::NoFrame);
    m_recentScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recentScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recentScrollArea->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_recentScrollArea->setFixedSize(kContentWidth, 322);
    m_recentScrollArea->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_recentScrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea{border:none;background:transparent;}"
        "QScrollArea > QWidget{background:transparent;}"));

    m_recentScrollContent = new QWidget();
    m_recentScrollContent->setObjectName(QStringLiteral("recentScrollContent"));
    m_recentScrollContent->setAttribute(Qt::WA_StyledBackground, true);
    m_recentScrollContent->setStyleSheet(QStringLiteral("#recentScrollContent{background:transparent;}"));
    m_recentScrollContent->setFixedHeight(322);

    m_recentCardsLayout = new QHBoxLayout(m_recentScrollContent);
    m_recentCardsLayout->setContentsMargins(0, 0, 0, 0);
    m_recentCardsLayout->setSpacing(16);

    m_recentScrollArea->setWidget(m_recentScrollContent);
    if (m_recentScrollArea->viewport())
        m_recentScrollArea->viewport()->installEventFilter(this);
    m_recentLayout->addWidget(m_recentScrollArea);

    recentContainerLayout->addWidget(m_recentSection, 0, Qt::AlignHCenter);

    m_recentEmptyLabel = new QLabel(recentContainer);
    m_recentEmptyLabel->setAlignment(Qt::AlignCenter);
    m_recentEmptyLabel->setStyleSheet(QStringLiteral("color:#8F9399;font-size:16px;font-weight:500;background:transparent;"));
    m_recentEmptyLabel->hide();
    recentContainerLayout->addWidget(m_recentEmptyLabel, 0, Qt::AlignHCenter);

    m_contentLayout->addWidget(recentContainer, 0, Qt::AlignHCenter);

    // Storage summary section
    m_storageSummarySection = new QWidget(m_contentWidget);
    auto *storageLayout = new QVBoxLayout(m_storageSummarySection);
    storageLayout->setContentsMargins(24, 24, 24, 16);
    storageLayout->setSpacing(12);
    m_storageSummarySection->setObjectName(QStringLiteral("storageSummary"));
    m_storageSummarySection->setFixedSize(kContentWidth, 114);
    m_storageSummarySection->setStyleSheet(QStringLiteral(
        "#storageSummary{background:#212225;border:1px solid #696E77;border-radius:12px;}"));

    auto *storageHeaderLayout = new QHBoxLayout();
    storageHeaderLayout->setContentsMargins(0, 0, 0, 0);
    storageHeaderLayout->setSpacing(8);

    m_storageTitleLabel = new QLabel(m_storageSummarySection);
    m_storageTitleLabel->setStyleSheet(QStringLiteral("font-size:20px;font-weight:600;color:#FFFFFF;background:transparent;"));
    storageHeaderLayout->addWidget(m_storageTitleLabel);

    storageHeaderLayout->addStretch();

    m_storageUsageLabel = new QLabel(m_storageSummarySection);
    m_storageUsageLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_storageUsageLabel->setStyleSheet(QStringLiteral("font-size:15px;font-weight:500;color:#A6ABB2;background:transparent;"));
    storageHeaderLayout->addWidget(m_storageUsageLabel);

    storageLayout->addLayout(storageHeaderLayout);

    m_storageProgress = new QProgressBar(m_storageSummarySection);
    m_storageProgress->setRange(0, 1000);
    m_storageProgress->setTextVisible(false);
    m_storageProgress->setFixedHeight(12);
    m_storageProgress->setStyleSheet(QStringLiteral(
        "QProgressBar{background:#696E77;border-radius:4px;}"
        "QProgressBar::chunk{background:#00FFB7;border-radius:4px;}"));
    storageLayout->addWidget(m_storageProgress);

    auto *storageHintLayout = new QHBoxLayout();
    storageHintLayout->setContentsMargins(0, 6, 0, 6);
    storageHintLayout->setSpacing(8);
    storageHintLayout->setAlignment(Qt::AlignVCenter);

    auto *hintIcon = new QLabel(m_storageSummarySection);
    hintIcon->setFixedSize(24, 24);
    hintIcon->setAlignment(Qt::AlignCenter);
    hintIcon->setStyleSheet(QStringLiteral(
        "QLabel{color:#9EA3AA;font-size:14px;font-weight:700;background:rgba(105,110,119,0.18);"
        "border-radius:10px;margin-top:-4px;}"));
    hintIcon->setText(QStringLiteral("!"));
    storageHintLayout->addWidget(hintIcon);

    auto *hintLabel = new QLabel(m_storageSummarySection);
    hintLabel->setStyleSheet(QStringLiteral("color:#9EA3AA;font-size:14px;font-weight:500;background:transparent;"));
    hintLabel->setText(tr("Disk space may run low if unnecessary files or applications are not removed."));
    hintLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    hintLabel->setMinimumHeight(24);
    hintLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    storageHintLayout->addWidget(hintLabel);

    storageLayout->addLayout(storageHintLayout);

    m_contentLayout->addWidget(m_storageSummarySection, 0, Qt::AlignHCenter);

    // Main session list
    auto *sessionContainer = new QWidget(m_contentWidget);
    sessionContainer->setFixedWidth(kContentWidth);
    auto *sessionLayout = new QVBoxLayout(sessionContainer);
    sessionLayout->setContentsMargins(0, 0, 0, 0);
    sessionLayout->setSpacing(12);

    m_sessionListTitleLabel = new QLabel(sessionContainer);
    m_sessionListTitleLabel->setStyleSheet(QStringLiteral("font-size:22px;font-weight:600;color:#FFFFFF;background:transparent;"));
    sessionLayout->addWidget(m_sessionListTitleLabel);

    m_sessionListContainer = new QWidget(sessionContainer);
    m_sessionListContainer->setObjectName(QStringLiteral("sessionListContainer"));
    m_sessionListContainer->setStyleSheet(QStringLiteral(
        "#sessionListContainer{background:transparent;border:none;}"));
    m_sessionListLayout = new QVBoxLayout(m_sessionListContainer);
    m_sessionListLayout->setContentsMargins(0, 16, 0, 16);
    m_sessionListLayout->setSpacing(12);
    sessionLayout->addWidget(m_sessionListContainer, 0, Qt::AlignHCenter);

    m_contentLayout->addWidget(sessionContainer, 0, Qt::AlignHCenter);

    // Corrupted session list
    auto *corruptedContainer = new QWidget(m_contentWidget);
    corruptedContainer->setFixedWidth(kContentWidth);
    auto *corruptedLayout = new QVBoxLayout(corruptedContainer);
    corruptedLayout->setContentsMargins(0, 0, 0, 0);
    corruptedLayout->setSpacing(12);

    m_corruptedTitleLabel = new QLabel(corruptedContainer);
    m_corruptedTitleLabel->setStyleSheet(QStringLiteral("font-size:22px;font-weight:600;color:#FF7878;background:transparent;"));
    corruptedLayout->addWidget(m_corruptedTitleLabel);

    m_corruptedListContainer = new QWidget(corruptedContainer);
    m_corruptedListContainer->setObjectName(QStringLiteral("corruptedListContainer"));
    m_corruptedListContainer->setStyleSheet(QStringLiteral(
        "#corruptedListContainer{background:transparent;border:none;}"));
    m_corruptedListLayout = new QVBoxLayout(m_corruptedListContainer);
    m_corruptedListLayout->setContentsMargins(0, 16, 0, 16);
    m_corruptedListLayout->setSpacing(12);
    corruptedLayout->addWidget(m_corruptedListContainer, 0, Qt::AlignHCenter);

    m_contentLayout->addWidget(corruptedContainer, 0, Qt::AlignHCenter);

    m_corruptedListContainer->hide();

    m_rootStack->addWidget(m_mainPage);
    buildOverlayUi();
    if (m_overlayPage)
        m_rootStack->addWidget(m_overlayPage);
    if (m_mainPage)
        m_rootStack->setCurrentWidget(m_mainPage);
}

void StoragePage::buildOverlayUi()
{
    if (!m_rootStack)
        return;

    m_overlayPage = new QWidget(this);
    m_overlayPage->setObjectName(QStringLiteral("storagePlaybackOverlay"));
    m_overlayPage->setStyleSheet(QStringLiteral("#storagePlaybackOverlay{background-color:#111113;}"));

    auto *overlayLayout = new QVBoxLayout(m_overlayPage);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->setSpacing(0);

    auto *headerWidget = new QWidget(m_overlayPage);
    headerWidget->setAttribute(Qt::WA_StyledBackground, true);
    headerWidget->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(24, 16, 24, 12);
    headerLayout->setSpacing(16);

    m_overlayTitleLabel = new QLabel(headerWidget);
    m_overlayTitleLabel->setStyleSheet(QStringLiteral("color:#E2FFF2;font-size:20px;font-weight:600;background:transparent;"));
    m_overlayTitleLabel->setText(tr("Playback"));
    headerLayout->addWidget(m_overlayTitleLabel, 0, Qt::AlignVCenter);
    headerLayout->addStretch();

    m_overlayCloseButton = new QToolButton(m_overlayPage);
    m_overlayCloseButton->setCursor(Qt::PointingHandCursor);
    m_overlayCloseButton->setAutoRaise(false);
    m_overlayCloseButton->setFocusPolicy(Qt::NoFocus);
    m_overlayCloseButton->setFixedSize(48, 48);
    m_overlayCloseButton->setIcon(QIcon(QStringLiteral(":/icons/close.svg")));
    m_overlayCloseButton->setIconSize(QSize(24, 24));
    m_overlayCloseButton->setStyleSheet(QStringLiteral(
        "QToolButton{background:rgba(28,29,33,0.92);border:1px solid rgba(52,56,62,1.0);"
        " border-radius:12px;padding:0;}"
        "QToolButton:hover{background:rgba(34,255,162,0.16);border-color:rgba(34,255,162,0.55);}"
        "QToolButton:pressed{background:rgba(34,255,162,0.24);border-color:rgba(34,255,162,0.55);}"
        "QToolButton:disabled{background:rgba(28,29,33,0.4);border-color:rgba(52,56,62,0.4);}"
    ));
    m_overlayCloseButton->setToolTip(tr("Close"));
    m_overlayCloseButton->setAccessibleName(tr("Close"));
    headerLayout->addWidget(m_overlayCloseButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    overlayLayout->addWidget(headerWidget);

    auto *contentContainer = new QWidget(m_overlayPage);
    auto *contentLayout = new QVBoxLayout(contentContainer);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(20);

    m_overlayStatusLabel = new QLabel(contentContainer);
    m_overlayStatusLabel->setStyleSheet(QStringLiteral("color:#9EA3AA;font-size:15px;font-weight:500;background:transparent;"));
    m_overlayStatusLabel->setWordWrap(true);
    m_overlayStatusLabel->setAlignment(Qt::AlignCenter);
    m_overlayStatusLabel->setVisible(false);
    contentLayout->addWidget(m_overlayStatusLabel, 0, Qt::AlignCenter);

    m_fullscreenPlaybackWidget = new SessionPlaybackWidget(contentContainer);
    m_fullscreenPlaybackWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    contentLayout->addWidget(m_fullscreenPlaybackWidget, 1);

    overlayLayout->addWidget(contentContainer);

    if (m_aiClient)
    {
        const auto settings = m_aiClient->settings();
        m_fullscreenPlaybackWidget->setConfidenceThreshold(settings.confidenceThreshold);
        m_fullscreenPlaybackWidget->setDefaultPassInfo(settings.passLevel.trimmed());
        m_fullscreenPlaybackWidget->setOverlayPointSize(settings.detectionDotSizePx);
    }

    connect(m_overlayCloseButton, &QToolButton::clicked, this, &StoragePage::hidePlaybackOverlay);
}

void StoragePage::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(tr("Storage"));
    if (m_recentTitleLabel)
        m_recentTitleLabel->setText(tr("Recently Saved Videos"));
    if (m_recentEmptyLabel)
        m_recentEmptyLabel->setText(tr("No recently saved videos."));
    if (m_storageTitleLabel)
        m_storageTitleLabel->setText(tr("My Storage"));
    if (m_sessionListTitleLabel)
        m_sessionListTitleLabel->setText(tr("Recording Sessions"));
    if (m_corruptedTitleLabel)
        m_corruptedTitleLabel->setText(tr("Corrupted Files"));
    if (m_overlayCloseButton)
    {
        m_overlayCloseButton->setToolTip(tr("Close"));
        m_overlayCloseButton->setAccessibleName(tr("Close"));
    }
    if (m_overlayCloseButton)
    {
        m_overlayCloseButton->setToolTip(tr("Close"));
        m_overlayCloseButton->setAccessibleName(tr("Close"));
    }
    const QString statusText = (m_overlayStatusLabel && m_overlayStatusLabel->isVisible()) ? m_overlayStatusLabel->text() : QString();
    const RecordingSession *selectedSession = nullptr;
    if (!m_selectedSessionId.isEmpty())
    {
        const auto it = std::find_if(m_sessions.begin(), m_sessions.end(), [this](const RecordingSession &session) {
            return session.id == m_selectedSessionId;
        });
        if (it != m_sessions.end())
            selectedSession = &(*it);
    }
    updateOverlayForSession(selectedSession, statusText);
}

void StoragePage::refreshContent()
{
    // Regenerate session model and rebuild visible sections in one pass.
    rebuildSessions();
    refreshSections();
}

void StoragePage::rebuildSessions()
{
    // Collapse individual recording files into logical sessions grouped by start time and slot.
    m_sessions.clear();
    m_fileToSessionIndex.clear();

    if (!m_recordingManager)
        return;

    QSet<QString> validSessionIds;

    auto recordings = m_recordingManager->recordings();
    std::sort(recordings.begin(), recordings.end(), [](const RecordingManager::RecordingMetadata &a, const RecordingManager::RecordingMetadata &b) {
        return a.startedAt < b.startedAt;
    });

    for (const auto &metadata : recordings)
    {
        const QString cameraLabel = displayNameForCamera(metadata.cameraId);

        int targetIndex = -1;
        for (int index = m_sessions.size() - 1; index >= 0; --index)
        {
            auto &session = m_sessions[index];
            const qint64 diff = std::llabs(session.startedAtUtc.msecsTo(metadata.startedAt));
            if (diff <= kSessionGroupingToleranceMs)
            {
                targetIndex = index;
                break;
            }
        }

        if (targetIndex == -1)
        {
            RecordingSession session;
            if (!metadata.slotId.isEmpty())
                session.slotIds.append(metadata.slotId);
            session.startedAtUtc = metadata.startedAt;
            session.finishedAtUtc = metadata.finishedAt.isValid() ? metadata.finishedAt : metadata.startedAt.addMSecs(metadata.durationMs);
            session.recordings.append(metadata);
            session.totalDurationMs = metadata.durationMs;

            session.title = sessionTitleFor(metadata);
            session.cameraSummary = cameraLabel;
            QFileInfo info(metadata.filePath);
            session.totalSizeBytes = info.exists() ? info.size() : 0;
            session.hasMissingFiles = !info.exists();

            m_sessions.append(session);
            targetIndex = m_sessions.size() - 1;
        }
        else
        {
            auto &session = m_sessions[targetIndex];
            session.recordings.append(metadata);
            if (metadata.startedAt < session.startedAtUtc)
                session.startedAtUtc = metadata.startedAt;
            if (metadata.finishedAt.isValid() && metadata.finishedAt > session.finishedAtUtc)
                session.finishedAtUtc = metadata.finishedAt;
            session.totalDurationMs = std::max(session.totalDurationMs, metadata.durationMs);

            QFileInfo info(metadata.filePath);
            session.totalSizeBytes += info.exists() ? info.size() : 0;
            if (!info.exists())
                session.hasMissingFiles = true;

            QStringList cameras = session.cameraSummary.split(", ", Qt::SkipEmptyParts);
            const QString summaryLabel = cameraLabel.isEmpty() ? metadata.cameraId : cameraLabel;
            if (!summaryLabel.isEmpty() && !cameras.contains(summaryLabel, Qt::CaseInsensitive))
            {
                cameras.append(summaryLabel);
                session.cameraSummary = cameras.join(", ");
            }

            if (!metadata.slotId.isEmpty() && !session.slotIds.contains(metadata.slotId))
                session.slotIds.append(metadata.slotId);
        }
    }

    for (int index = 0; index < m_sessions.size(); ++index)
    {
        auto &session = m_sessions[index];
        std::sort(session.recordings.begin(), session.recordings.end(), [](const RecordingManager::RecordingMetadata &a, const RecordingManager::RecordingMetadata &b) {
            return a.cameraId.localeAwareCompare(b.cameraId) < 0;
        });

        QSet<QString> uniqueTrackKeys;
        for (const auto &metadata : session.recordings)
        {
            const QString trackKey = metadata.slotId.isEmpty() ? metadata.cameraId : metadata.slotId;
            if (!trackKey.isEmpty())
                uniqueTrackKeys.insert(trackKey);
        }
        session.expectedTrackCount = uniqueTrackKeys.size();
        if (session.expectedTrackCount <= 0)
            session.expectedTrackCount = session.recordings.size();

        int availableTrackCount = 0;
        for (const auto &metadata : session.recordings)
        {
            if (metadata.filePath.isEmpty())
                continue;
            QFileInfo fileInfo(metadata.filePath);
            if (fileInfo.exists() && fileInfo.isFile() && fileInfo.size() > 0)
            {
                ++availableTrackCount;
            }
            else
            {
                session.hasMissingFiles = true;
            }
        }

        if (availableTrackCount < session.expectedTrackCount)
            session.incomplete = true;
    }

    std::sort(m_sessions.begin(), m_sessions.end(), [](const RecordingSession &a, const RecordingSession &b) {
        return a.startedAtUtc > b.startedAtUtc; // Newest first
    });

    // Rebuild map after reordering
    m_fileToSessionIndex.clear();
    for (int index = 0; index < m_sessions.size(); ++index)
    {
        auto &session = m_sessions[index];

        QStringList uniqueSlots = session.slotIds;
        uniqueSlots.removeAll(QString());
        std::sort(uniqueSlots.begin(), uniqueSlots.end(), [](const QString &a, const QString &b) {
            return a.localeAwareCompare(b) < 0;
        });
        uniqueSlots.erase(std::unique(uniqueSlots.begin(), uniqueSlots.end()), uniqueSlots.end());
        session.slotIds = uniqueSlots;

        QStringList cameras = session.cameraSummary.split(", ", Qt::SkipEmptyParts);
        std::sort(cameras.begin(), cameras.end(), [](const QString &a, const QString &b) {
            return a.localeAwareCompare(b) < 0;
        });
        cameras.erase(std::unique(cameras.begin(), cameras.end()), cameras.end());
        session.cameraSummary = cameras.join(", ");

        session.title = buildSessionTitle(session);

        QCryptographicHash hash(QCryptographicHash::Sha1);
        for (const auto &track : std::as_const(session.recordings))
        {
            hash.addData(track.filePath.toUtf8());
            m_fileToSessionIndex.insert(track.filePath, index);
        }
        const QString hashFragment = QString::fromLatin1(hash.result().toHex().left(12));
        session.id = QStringLiteral("%1_%2").arg(QString::number(session.startedAtUtc.toMSecsSinceEpoch()), hashFragment);
        validSessionIds.insert(session.id);
        if (m_customSessionTitles.contains(session.id))
            session.title = m_customSessionTitles.value(session.id);
    }

    bool titlesPruned = false;
    for (auto it = m_customSessionTitles.begin(); it != m_customSessionTitles.end();)
    {
        if (!validSessionIds.contains(it.key()))
        {
            it = m_customSessionTitles.erase(it);
            titlesPruned = true;
        }
        else
        {
            ++it;
        }
    }

    if (titlesPruned)
        saveCustomSessionTitles();
}

void StoragePage::refreshSections()
{
    populateRecentSessions();
    populateSessionList();
    populateCorruptedList();
    updateStorageSummary();
}

void StoragePage::populateRecentSessions()
{
    // Render horizontal carousel of most recent sessions for quick access.
    if (!m_recentCardsLayout)
        return;

    clearLayout(m_recentCardsLayout);
    m_recentCardByWidget.clear();

    const int recentCount = std::min(kMaxRecentSessions, static_cast<int>(m_sessions.size()));
    if (recentCount == 0)
    {
        if (m_recentSection)
            m_recentSection->hide();
        if (m_recentEmptyLabel)
            m_recentEmptyLabel->show();
        return;
    }

    if (m_recentSection)
        m_recentSection->show();
    if (m_recentEmptyLabel)
        m_recentEmptyLabel->hide();

    for (int i = 0; i < recentCount; ++i)
    {
        auto *card = createRecentSessionCard(i);
        if (!card)
            continue;

        const auto &session = m_sessions.at(i);
        m_recentCardByWidget.insert(card, session.id);

        card->installEventFilter(this);
        const auto children = card->findChildren<QWidget *>();
        for (auto *child : children)
        {
            if (child && child != card)
                child->installEventFilter(this);
        }

        m_recentCardsLayout->addWidget(card);
    }

    m_recentCardsLayout->addStretch();

    if (m_recentScrollContent)
    {
        constexpr int kCardWidth = 320;
        constexpr int kScrollWidth = 960;
        const int spacing = m_recentCardsLayout->spacing();
        const int totalWidth = recentCount > 0 ? (kCardWidth * recentCount) + spacing * (recentCount - 1) : 0;
        const int minimumWidth = std::max(totalWidth, kScrollWidth);
        m_recentScrollContent->setFixedWidth(minimumWidth);
        m_recentScrollContent->setFixedHeight(322);
        m_recentScrollContent->updateGeometry();
    }

    updateRecentCardSelectionStyles();
}

void StoragePage::populateSessionList()
{
    // Rebuild detailed session rows with per-camera metadata and actions.
    if (!m_sessionListLayout)
        return;

    m_sessionRowByWidget.clear();
    clearLayout(m_sessionListLayout);

    RecordingSession const *selectedSession = nullptr;

    for (int index = 0; index < m_sessions.size(); ++index)
    {
        const auto &session = m_sessions.at(index);
        auto *row = createSessionListRow(index);
        if (!row)
            continue;

        row->installEventFilter(this);
        const auto children = row->findChildren<QWidget *>();
        for (auto *child : children)
        {
            if (child && child != row)
                child->installEventFilter(this);
        }

        m_sessionRowByWidget.insert(row, session.id);
        applySessionRowStyle(row, false);
        m_sessionListLayout->addWidget(row);

        if (session.id == m_selectedSessionId)
            selectedSession = &session;
    }

    if (!m_sessions.isEmpty())
    {
        if (!selectedSession)
            selectSessionById(m_sessions.first().id, false);
        else
            selectSessionById(selectedSession->id, false);
    }
    else
    {
        m_selectedSessionId.clear();
        updateSessionRowSelectionStyles();
        updateRecentCardSelectionStyles();
        updatePlaybackForSelection(nullptr);
        hidePlaybackOverlay();
    }

    if (m_sessionListLayout && m_sessionListLayout->count() > 0)
        m_sessionListLayout->addStretch();
}

void StoragePage::populateCorruptedList()
{
    // Highlight sessions with missing files separately so operators can triage storage issues.
    if (!m_corruptedListLayout)
        return;

    clearLayout(m_corruptedListLayout);

    int corruptedCount = 0;
    for (int index = 0; index < m_sessions.size(); ++index)
    {
        if (!m_sessions.at(index).isCorrupted())
            continue;

        auto *row = createCorruptedRow(index);
        if (!row)
            continue;
        m_corruptedListLayout->addWidget(row);
        ++corruptedCount;
    }

    if (m_corruptedListLayout && corruptedCount > 0)
        m_corruptedListLayout->addStretch();

    const bool hasCorrupted = corruptedCount > 0;
    if (m_corruptedListContainer)
        m_corruptedListContainer->setVisible(hasCorrupted);
    if (m_corruptedTitleLabel)
        m_corruptedTitleLabel->setVisible(hasCorrupted);
}

void StoragePage::updateStorageSummary()
{
    // Aggregate disk usage and present capacity overview for the operator.
    if (!m_storageProgress || !m_storageUsageLabel)
        return;

    const QString outputDir = m_recordingManager ? m_recordingManager->outputDirectory() : QString();
    if (outputDir.isEmpty())
    {
        m_totalStorageBytes = 0;
        m_usedStorageBytes = 0;
        m_storageProgress->setValue(0);
        m_storageUsageLabel->setText(tr("No storage location is configured."));
        return;
    }

    QStorageInfo storageInfo{QDir(outputDir)};
    if (!storageInfo.isValid())
    {
        m_storageProgress->setValue(0);
        m_storageUsageLabel->setText(tr("Unable to retrieve storage device information."));
        return;
    }

    m_totalStorageBytes = storageInfo.bytesTotal();
    m_usedStorageBytes = storageInfo.bytesTotal() - storageInfo.bytesAvailable();

    if (m_totalStorageBytes > 0)
    {
        const int value = static_cast<int>((m_usedStorageBytes * 1000) / m_totalStorageBytes);
        m_storageProgress->setValue(std::clamp(value, 0, 1000));
    }
    else
    {
        m_storageProgress->setValue(0);
    }

    const QString totalText = formatFileSize(m_totalStorageBytes);
    const QString usedText = formatFileSize(m_usedStorageBytes);
    m_storageUsageLabel->setText(tr("%2 used of %1").arg(totalText, usedText));
}

void StoragePage::selectSessionById(const QString &sessionId, bool userInitiated)
{
    const auto it = std::find_if(m_sessions.begin(), m_sessions.end(), [&sessionId](const RecordingSession &session) {
        return session.id == sessionId;
    });
    if (it == m_sessions.end())
        return;

    m_selectedSessionId = sessionId;

    updateSessionRowSelectionStyles();
    updateRecentCardSelectionStyles();
    updatePlaybackForSelection(&(*it));

    const bool shouldOpenOverlay = userInitiated || (m_rootStack && m_rootStack->currentWidget() == m_overlayPage);
    if (shouldOpenOverlay)
        showPlaybackOverlay(&(*it));
}

void StoragePage::updatePlaybackForSelection(const RecordingSession *session)
{
    if (!session)
    {
        if (m_fullscreenPlaybackWidget)
            m_fullscreenPlaybackWidget->setTracks({});
        updateOverlayForSession(nullptr);
        return;
    }

    QVector<SessionPlaybackWidget::Track> tracks;
    tracks.reserve(session->recordings.size());
    for (const auto &metadata : session->recordings)
    {
        if (metadata.filePath.isEmpty())
            continue;

        SessionPlaybackWidget::Track track;
        track.cameraId = metadata.cameraId;
        QString cameraLabel = displayNameForCamera(metadata.cameraId);
        if (cameraLabel.isEmpty())
            cameraLabel = metadata.cameraId;
        track.displayName = cameraLabel;
        track.filePath = metadata.filePath;
        track.analysisPath = metadata.analysisPath;
        track.passLevel = metadata.passLevel.trimmed();
        if (metadata.startedAt.isValid())
            track.startedAtMs = metadata.startedAt.toMSecsSinceEpoch();
        else
            track.startedAtMs = 0;
        tracks.append(track);
    }

    if (tracks.isEmpty())
    {
        if (m_fullscreenPlaybackWidget)
            m_fullscreenPlaybackWidget->setTracks({});
        QString message = tr("No playable files were found in the selected session.");
        if (session->isCorrupted())
            message.append(QLatin1String("\n")).append(tr("Please remove the corrupted files first."));
        updateOverlayForSession(session, message);
        return;
    }

    if (m_fullscreenPlaybackWidget)
        m_fullscreenPlaybackWidget->setTracks(tracks);

    updateOverlayForSession(session);
}

void StoragePage::showPlaybackOverlay(const RecordingSession *session)
{
    if (!m_rootStack || !m_overlayPage)
        return;

    updateOverlayForSession(session);
    m_rootStack->setCurrentWidget(m_overlayPage);
}

void StoragePage::hidePlaybackOverlay()
{
    if (m_fullscreenPlaybackWidget)
        m_fullscreenPlaybackWidget->setTracks({});

    updateOverlayForSession(nullptr);

    if (m_rootStack && m_mainPage)
        m_rootStack->setCurrentWidget(m_mainPage);
}

void StoragePage::updateOverlayForSession(const RecordingSession *session, const QString &statusMessage)
{
    // Keep playback overlay title/status in sync with selected session or transient status messages.
    if (m_overlayTitleLabel)
    {
        QString title;
        if (session)
        {
            title = session->title.trimmed();
            if (title.isEmpty())
                title = buildSessionTitle(*session);
        }
        if (title.isEmpty())
            title = tr("Playback");
        m_overlayTitleLabel->setText(title);
        m_overlayTitleLabel->setVisible(!title.isEmpty());
    }

    if (!m_overlayStatusLabel)
        return;

    if (statusMessage.isEmpty())
    {
        m_overlayStatusLabel->clear();
        m_overlayStatusLabel->setVisible(false);
    }
    else
    {
        m_overlayStatusLabel->setText(statusMessage);
        m_overlayStatusLabel->setVisible(true);
    }
}

QString StoragePage::slotSummary(const RecordingSession &session) const
{
    QStringList slotList = session.slotIds;
    slotList.removeAll(QString());
    if (slotList.isEmpty())
        return QString();

    std::sort(slotList.begin(), slotList.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });
    slotList.erase(std::unique(slotList.begin(), slotList.end()), slotList.end());
    return slotList.join(QStringLiteral(", "));
}

QString StoragePage::buildSessionTitle(const RecordingSession &session) const
{
    const QString slotLabel = slotSummary(session);
    const QString cameras = session.cameraSummary;

    if (!slotLabel.isEmpty())
    {
        if (cameras.isEmpty())
            return QStringLiteral("[%1]").arg(slotLabel);
        return QStringLiteral("[%1] %2").arg(slotLabel, cameras);
    }

    return cameras.isEmpty() ? tr("Session") : cameras;
}

void StoragePage::updateSessionRowSelectionStyles()
{
    for (auto it = m_sessionRowByWidget.cbegin(); it != m_sessionRowByWidget.cend(); ++it)
    {
        QWidget *row = it.key();
        if (!row)
            continue;
        const bool selected = (it.value() == m_selectedSessionId);
        applySessionRowStyle(row, selected);
    }
}

void StoragePage::updateRecentCardSelectionStyles()
{
    const QString selectedColor = QStringLiteral("rgba(34,255,162,0.55)");
    const QString defaultColor = QStringLiteral("#212225");

    for (auto it = m_recentCardByWidget.cbegin(); it != m_recentCardByWidget.cend(); ++it)
    {
        auto *card = qobject_cast<QWidget *>(it.key());
        if (!card)
            continue;

        const bool selected = (it.value() == m_selectedSessionId);
        const QString borderColor = selected ? selectedColor : defaultColor;

        if (auto *preview = card->findChild<QFrame *>(QStringLiteral("recentSessionPreview")))
        {
            preview->setStyleSheet(QStringLiteral(
                "#recentSessionPreview{border:1px solid %1;border-bottom:none;"
                "border-top-left-radius:12px;border-top-right-radius:12px;"
                "border-bottom-left-radius:0px;border-bottom-right-radius:0px;background:#16171A;}").arg(borderColor));
        }

        if (auto *info = card->findChild<QFrame *>(QStringLiteral("recentSessionInfo")))
        {
            info->setStyleSheet(QStringLiteral(
                "#recentSessionInfo{background:#212225;border:1px solid %1;border-top:none;"
                "border-top-left-radius:0px;border-top-right-radius:0px;"
                "border-bottom-left-radius:12px;border-bottom-right-radius:12px;}").arg(borderColor));
        }
    }
}

void StoragePage::applySessionRowStyle(QWidget *row, bool selected) const
{
    if (!row)
        return;

    const QString borderColor = selected ? QStringLiteral("rgba(34,255,162,0.55)")
                                         : QStringLiteral("rgba(39,42,46,0.7)");
    const QString background = selected ? QStringLiteral("#202228") : QStringLiteral("#18191D");
    const QString style = QStringLiteral(
        "#sessionRow{background:%1;border-radius:12px;padding:4px;border:1px solid %2;}");
    row->setStyleSheet(style.arg(background, borderColor));
}

QString StoragePage::elidedText(const QString &text, int maxLength) const
{
    if (maxLength <= 0 || text.length() <= maxLength)
        return text;
    if (maxLength <= 3)
        return text.left(maxLength);
    return text.left(maxLength - 3) + QStringLiteral("...");
}

void StoragePage::deleteSession(const QString &sessionId)
{
    if (!m_recordingManager || sessionId.isEmpty())
        return;

    const auto it = std::find_if(m_sessions.begin(), m_sessions.end(), [&sessionId](const RecordingSession &session) {
        return session.id == sessionId;
    });
    if (it == m_sessions.end())
        return;

    const auto releasePreviewMedia = [this, &sessionId]() {
        for (auto cardIt = m_recentCardByWidget.cbegin(); cardIt != m_recentCardByWidget.cend(); ++cardIt)
        {
            if (cardIt.value() != sessionId)
                continue;

            if (QWidget *card = cardIt.key())
            {
                const auto players = card->findChildren<QMediaPlayer *>();
                for (auto *player : players)
                {
                    player->stop();
                    player->setSource(QUrl());
                }
            }
        }
    };

    QVector<QString> filePaths;
    filePaths.reserve(it->recordings.size());
    for (const auto &metadata : it->recordings)
    {
        if (!metadata.filePath.isEmpty())
            filePaths.append(metadata.filePath);
    }

    releasePreviewMedia();

    bool deletedAny = false;
    for (const auto &path : std::as_const(filePaths))
    {
        if (m_recordingManager->deleteRecording(path))
            deletedAny = true;
    }

    if (!deletedAny)
        refreshContent();
}

void StoragePage::openSessionFolder(const QString &sessionId)
{
    const auto it = std::find_if(m_sessions.begin(), m_sessions.end(), [&sessionId](const RecordingSession &session) {
        return session.id == sessionId;
    });

    if (it == m_sessions.end())
        return;

    for (const auto &metadata : it->recordings)
    {
        if (metadata.filePath.isEmpty())
            continue;
        QFileInfo info(metadata.filePath);
        if (!info.exists())
            continue;
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
        return;
    }
}

void StoragePage::showSessionOptionsMenu(const QString &sessionId, QWidget *anchor)
{
    if (!anchor)
        return;

    QMenu menu(anchor);
    menu.setAttribute(Qt::WA_TranslucentBackground, true);
    menu.setStyleSheet(QStringLiteral(
        "QMenu{background:#1E1F24;color:#FFFFFF;border:1px solid rgba(80,82,90,0.8);border-radius:8px;}\n"
        "QMenu::item{padding:8px 20px;border-radius:6px;}\n"
        "QMenu::item:selected{background:#22FFA2;color:#111113;}"));
    QAction *playAction = menu.addAction(tr("Play"));
    QAction *renameAction = menu.addAction(tr("Rename"));
    QAction *deleteAction = menu.addAction(tr("Delete"));

    const QPoint globalPos = anchor->mapToGlobal(anchor->rect().center());
    QAction *selected = menu.exec(globalPos);
    if (!selected)
        return;

    if (selected == playAction)
    {
        selectSessionById(sessionId, true);
        return;
    }

    if (selected == renameAction)
    {
        renameSession(sessionId, anchor);
        return;
    }

    if (selected == deleteAction)
        deleteSession(sessionId);
}

void StoragePage::renameSession(const QString &sessionId, QWidget *dialogParent)
{
    auto it = std::find_if(m_sessions.begin(), m_sessions.end(), [&sessionId](const RecordingSession &session) {
        return session.id == sessionId;
    });

    if (it == m_sessions.end())
        return;

    const QString currentTitle = it->title;
    bool ok = false;
    QString newTitle = QInputDialog::getText(dialogParent ? dialogParent : this,
                                            tr("Rename Session"),
                                            tr("Session name"),
                                            QLineEdit::Normal,
                                            currentTitle,
                                            &ok);
    if (!ok)
        return;

    newTitle = newTitle.trimmed();

    if (newTitle == currentTitle)
        return;

    auto &session = *it;
    if (newTitle.isEmpty())
    {
        if (!m_customSessionTitles.contains(sessionId))
            return;

        m_customSessionTitles.remove(sessionId);
        session.title = buildSessionTitle(session);
    }
    else
    {
        m_customSessionTitles.insert(sessionId, newTitle);
        session.title = newTitle;
    }

    saveCustomSessionTitles();
    refreshSections();
}

void StoragePage::loadCustomSessionTitles()
{
    m_customSessionTitles.clear();
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Weldbeing"), QStringLiteral("StoragePage"));
    settings.beginGroup(QStringLiteral("sessionTitles"));
    const QStringList keys = settings.childKeys();
    for (const QString &key : keys)
    {
        const QString value = settings.value(key).toString();
        if (!key.isEmpty() && !value.isEmpty())
            m_customSessionTitles.insert(key, value);
    }
    settings.endGroup();
}

void StoragePage::saveCustomSessionTitles() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Weldbeing"), QStringLiteral("StoragePage"));
    settings.beginGroup(QStringLiteral("sessionTitles"));
    settings.remove(QString());
    for (auto it = m_customSessionTitles.cbegin(); it != m_customSessionTitles.cend(); ++it)
        settings.setValue(it.key(), it.value());
    settings.endGroup();
}

QWidget *StoragePage::createSessionListRow(int sessionIndex) const
{
    if (sessionIndex < 0 || sessionIndex >= m_sessions.size())
        return new QWidget();

    const auto &session = m_sessions.at(sessionIndex);

    auto *container = new QFrame();
    container->setObjectName(QStringLiteral("sessionRow"));
    container->setCursor(Qt::PointingHandCursor);
    container->setFixedSize(960, 72);
    container->setStyleSheet(QStringLiteral("#sessionRow{background:#18191D;border-radius:12px;padding:4px;border:1px solid #696E77;}"));

    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(18, 12, 18, 12);
    layout->setSpacing(16);

    auto *statusLabel = new QLabel(container);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    if (session.isCorrupted())
    {
        statusLabel->setText(tr("Corrupted"));
        statusLabel->setStyleSheet(QStringLiteral(
            "QLabel{padding:6px 14px;font-size:14px;font-weight:600;border-radius:14px;"
            "background:transparent;color:#FF7878;border:1px solid rgba(255,120,120,0.6);}"));
    }
    else
    {
        statusLabel->setText(tr("Archived"));
        statusLabel->setStyleSheet(QStringLiteral(
            "QLabel{padding:6px 14px;font-size:14px;font-weight:600;border-radius:14px;"
            "background:transparent;color:#22FFA2;border:1px solid rgba(34,255,162,0.6);}"));
    }

    const QFontMetrics badgeMetrics(statusLabel->font());
    int badgeWidth = 0;
    const QStringList badgeTexts = {tr("Archived"), tr("Corrupted")};
    for (const auto &text : badgeTexts)
        badgeWidth = std::max(badgeWidth, badgeMetrics.horizontalAdvance(text));
    statusLabel->setFixedWidth(badgeWidth + 28); // padding 14px on each side

    layout->addWidget(statusLabel);

    auto *textContainer = new QWidget(container);
    textContainer->setObjectName(QStringLiteral("sessionTextContainer"));
    textContainer->setAttribute(Qt::WA_TranslucentBackground, true);
    textContainer->setStyleSheet(QStringLiteral("#sessionTextContainer{background:transparent;}"));
    textContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *textLayout = new QVBoxLayout(textContainer);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    auto *titleLabel = new ElidedLabel(textContainer);
    titleLabel->setStyleSheet(QStringLiteral("color:#FFFFFF;font-size:17px;font-weight:600;background:transparent;"));
    titleLabel->setWordWrap(false);
    titleLabel->setElideMode(Qt::ElideRight);
    titleLabel->setFullText(session.title);
    textLayout->addWidget(titleLabel);

    layout->addWidget(textContainer, 1);

    auto *timeLabel = new QLabel(container);
    timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    timeLabel->setStyleSheet(QStringLiteral("color:#8F9399;font-size:14px;font-weight:500;background:transparent;"));
    const QString startText = formattedDateTime(session.startedAtUtc);
    const QString durationText = formatDuration(session.totalDurationMs);
    timeLabel->setText(tr("%1\n%2").arg(startText, durationText));
    layout->addWidget(timeLabel);

    auto *actionsContainer = new QWidget(container);
    actionsContainer->setObjectName(QStringLiteral("sessionActionsContainer"));
    actionsContainer->setAttribute(Qt::WA_TranslucentBackground, true);
    actionsContainer->setStyleSheet(QStringLiteral("#sessionActionsContainer{background:transparent;}"));
    auto *actionsLayout = new QHBoxLayout(actionsContainer);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);

    auto configureIconButton = [](QPushButton *button, const QString &iconPath, const QString &tooltip) {
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(36, 36));
        button->setToolTip(tooltip);
        button->setCursor(Qt::PointingHandCursor);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setFixedSize(40, 40);
        button->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;border:none;padding:0;}"
            "QPushButton:hover{background:rgba(34,255,162,0.08);border:none;}"));
        button->setProperty("suppressRowSelection", true);
    };

    auto makeActionButton = [actionsContainer, configureIconButton](const QString &label, const QString &iconPath = QString()) {
        auto *button = new QPushButton(actionsContainer);
        button->setCursor(Qt::PointingHandCursor);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        button->setMinimumHeight(32);

        const QString baseStyle = QStringLiteral(
            "QPushButton{background:transparent;color:#B0B4BA;border:1px solid rgba(80,82,90,0.8);"
            "border-radius:14px;padding:%1;font-size:13px;font-weight:600;}"
            "QPushButton:hover{color:#22FFA2;border-color:rgba(34,255,162,0.65);}"
            "QPushButton:disabled{color:rgba(176,180,186,0.4);border-color:rgba(80,82,90,0.4);}");

        if (iconPath.isEmpty())
        {
            button->setText(label);
            button->setMinimumWidth(64);
            button->setStyleSheet(baseStyle.arg(QStringLiteral("6px 10px")));
        }
        else
        {
            configureIconButton(button, iconPath, label);
        }

        return button;
    };

    auto *self = const_cast<StoragePage *>(this);

    auto *folderButton = makeActionButton(tr("Folder"), QStringLiteral(":/icons/folder.svg"));
    connect(folderButton, &QPushButton::clicked, self, [self, sessionId = session.id]() {
        if (self)
            self->openSessionFolder(sessionId);
    });
    actionsLayout->addWidget(folderButton);

    auto *moreButton = makeActionButton(tr("More"), QStringLiteral(":/icons/more_vert.svg"));
    connect(moreButton, &QPushButton::clicked, self, [self, sessionId = session.id, moreButton]() {
        if (self)
            self->showSessionOptionsMenu(sessionId, moreButton);
    });
    actionsLayout->addWidget(moreButton);

    layout->addWidget(actionsContainer);

    return container;
}

QWidget *StoragePage::createCorruptedRow(int sessionIndex) const
{
    if (sessionIndex < 0 || sessionIndex >= m_sessions.size())
        return new QWidget();

    const auto &session = m_sessions.at(sessionIndex);

    auto *container = new QFrame();
    container->setObjectName(QStringLiteral("corruptedRow"));
    container->setStyleSheet(QStringLiteral(
        "#corruptedRow{background:#181417;border-radius:12px;padding:4px;border:1px solid #696E77;}"));
    container->setFixedSize(960, 72);

    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(18, 12, 18, 12);
    layout->setSpacing(16);

    auto *titleLabel = new QLabel(container);
    titleLabel->setStyleSheet(QStringLiteral("color:#FFC2C2;font-size:16px;font-weight:600;background:transparent;"));
    titleLabel->setText(elidedText(session.title, 80));
    titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    titleLabel->setWordWrap(false);
    layout->addWidget(titleLabel, 1);

    auto *detailsLabel = new QLabel(container);
    detailsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    detailsLabel->setStyleSheet(QStringLiteral("color:#FF8E8E;font-size:14px;font-weight:500;background:transparent;"));
    QStringList detailParts;
    detailParts << tr("Cameras: %1").arg(session.cameraSummary);
    if (session.hasMissingFiles)
        detailParts << tr("Missing Files");
    if (session.incomplete)
        detailParts << tr("Incomplete Tracks");
    detailsLabel->setText(elidedText(detailParts.join(QLatin1String("  |  ")), 110));
    detailsLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    detailsLabel->setWordWrap(false);
    layout->addWidget(detailsLabel);

    return container;
}

QWidget *StoragePage::createRecentSessionCard(int sessionIndex) const
{
    if (sessionIndex < 0 || sessionIndex >= m_sessions.size())
        return nullptr;

    const auto &session = m_sessions.at(sessionIndex);

    auto *card = new QWidget(m_recentScrollContent);
    card->setObjectName(QStringLiteral("recentSessionCard"));
    card->setFixedSize(320, 322);
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setFocusPolicy(Qt::NoFocus);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("sessionId", session.id);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);
    card->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    struct SlotPreview
    {
        QString slotId;
        QString cameraId;
        QString displayName;
        QString filePath;
    };

    QVector<SlotPreview> slotPreviews;
    slotPreviews.reserve(2);

    QHash<QString, SlotPreview> previewBySlot;
    for (const auto &metadata : session.recordings)
    {
        QString slotKey = metadata.slotId.isEmpty() ? metadata.cameraId : metadata.slotId;
        if (slotKey.isEmpty() || previewBySlot.contains(slotKey))
            continue;

        QFileInfo info(metadata.filePath);
        const QString displayName = displayNameForCamera(metadata.cameraId);
        SlotPreview preview{slotKey, metadata.cameraId, displayName, info.exists() ? metadata.filePath : QString()};
        previewBySlot.insert(slotKey, preview);
    }

    for (const QString &slotId : session.slotIds)
    {
        if (!previewBySlot.contains(slotId))
            continue;
        slotPreviews.push_back(previewBySlot.value(slotId));
        if (slotPreviews.size() >= 2)
            break;
    }

    if (slotPreviews.isEmpty())
    {
        const auto values = previewBySlot.values();
        for (const auto &preview : values)
        {
            slotPreviews.push_back(preview);
            if (slotPreviews.size() >= 2)
                break;
        }
    }

    while (slotPreviews.size() < 2)
    {
        const int index = slotPreviews.size() + 1;
        slotPreviews.push_back({tr("Slot %1").arg(index), QString(), QString(), QString()});
    }

    auto configureIconButton = [](QPushButton *button, const QString &iconPath, const QString &tooltip) {
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(36, 36));
        button->setToolTip(tooltip);
        button->setCursor(Qt::PointingHandCursor);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setFixedSize(40, 40);
        button->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;border:none;padding:0;}"
            "QPushButton:hover{background:rgba(34,255,162,0.08);border:none;}"));
        button->setProperty("suppressRowSelection", true);
    };

    auto *previewContainer = new QFrame(card);
    previewContainer->setObjectName(QStringLiteral("recentSessionPreview"));
    previewContainer->setFixedSize(320, 200);
    previewContainer->setStyleSheet(QStringLiteral(
        "#recentSessionPreview{border:1px solid #212225;border-bottom:none;"
        "border-top-left-radius:12px;border-top-right-radius:12px;"
        "border-bottom-left-radius:0px;border-bottom-right-radius:0px;background:#16171A;}"));

    auto *previewLayout = new QHBoxLayout(previewContainer);
    previewLayout->setContentsMargins(1, 1, 1, 1);
    previewLayout->setSpacing(0);

    auto buildSlotPane = [this](const SlotPreview &preview, QWidget *parent, bool roundLeft, bool roundRight, int width) {
        auto *frame = new RoundedClipFrame(parent);
        frame->setObjectName(QStringLiteral("recentSlotPane"));
        frame->setFixedSize(width, 198);
        frame->setAttribute(Qt::WA_StyledBackground, true);
        frame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        frame->setCornerRadii(roundLeft ? 11 : 0, roundRight ? 11 : 0, 0, 0);

        QString style = QStringLiteral("#recentSlotPane{background:#111217;border:none;}");
        if (roundLeft)
            style.append(QStringLiteral("#recentSlotPane{border-top-left-radius:11px;}"));
        if (roundRight)
            style.append(QStringLiteral("#recentSlotPane{border-top-right-radius:11px;}"));
        frame->setStyleSheet(style);

        auto *stackLayout = new QStackedLayout(frame);
        stackLayout->setContentsMargins(0, 0, 0, 0);
        stackLayout->setStackingMode(QStackedLayout::StackAll);

        auto *previewLabel = new PreviewImageLabel(frame);
        previewLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        stackLayout->addWidget(previewLabel);

        auto *overlay = new QWidget(frame);
        overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto *overlayLayout = new QVBoxLayout(overlay);
        overlayLayout->setContentsMargins(12, 12, 12, 12);
        overlayLayout->setSpacing(4);

        auto *slotLabel = new QLabel(overlay);
        slotLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        slotLabel->setStyleSheet(QStringLiteral("color:#FFFFFF;font-size:13px;font-weight:600;background:rgba(0,0,0,0.35);padding:2px 8px;border-radius:10px;"));
        const QString slotText = preview.slotId.isEmpty() ? tr("Unknown") : QStringLiteral("[%1]").arg(preview.slotId);
        slotLabel->setText(slotText);
        overlayLayout->addWidget(slotLabel, 0, Qt::AlignLeft);

        auto *cameraLabel = new QLabel(overlay);
        cameraLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        cameraLabel->setWordWrap(true);
        cameraLabel->setStyleSheet(QStringLiteral("color:#D4D7DD;font-size:11px;font-weight:500;background:rgba(0,0,0,0.25);padding:2px 8px;border-radius:8px;"));
        const QString cameraSource = !preview.displayName.isEmpty() ? preview.displayName : preview.cameraId;
        const QString cameraText = cameraSource.isEmpty() ? tr("Unknown") : elidedText(cameraSource, 28);
        cameraLabel->setText(cameraText);
        if (!preview.cameraId.isEmpty())
            cameraLabel->setToolTip(preview.cameraId);
        overlayLayout->addWidget(cameraLabel, 0, Qt::AlignLeft);

        overlayLayout->addStretch();
        stackLayout->addWidget(overlay);

        QLabel *fallbackLabel = nullptr;
        if (!preview.filePath.isEmpty())
        {
            auto *audioOutput = new QAudioOutput(frame);
            audioOutput->setVolume(0.0);
            audioOutput->setMuted(true);

            auto *player = new QMediaPlayer(frame);
            player->setAudioOutput(audioOutput);

            auto *videoSink = new QVideoSink(frame);
            player->setVideoSink(videoSink);
            player->setSource(QUrl::fromLocalFile(preview.filePath));

            QPointer<QMediaPlayer> guard(player);
            QPointer<QVideoSink> sinkGuard(videoSink);

            QObject::connect(videoSink, &QVideoSink::videoFrameChanged, frame, [previewLabel, sinkGuard](const QVideoFrame &frame) {
                if (!sinkGuard)
                    return;
                QVideoFrame copy(frame);
                if (!copy.isValid())
                    return;
                QImage image = copy.toImage();
                if (image.isNull())
                    return;
                previewLabel->setImage(image);
            });

            QObject::connect(player, &QMediaPlayer::mediaStatusChanged, frame, [guard, frame](QMediaPlayer::MediaStatus status) {
                if (!guard)
                    return;
                if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)
                {
                    QTimer::singleShot(120, frame, [guard]() {
                        if (!guard)
                            return;
                        guard->pause();
                        guard->setPosition(0);
                    });
                }
            });

            player->play();
        }
        else
        {
            fallbackLabel = new QLabel(overlay);
            fallbackLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            fallbackLabel->setStyleSheet(QStringLiteral("color:#9EA3AA;font-size:11px;font-weight:500;background:rgba(0,0,0,0.25);padding:2px 8px;border-radius:8px;"));
            fallbackLabel->setText(tr("No preview"));
            overlayLayout->addWidget(fallbackLabel, 0, Qt::AlignLeft);
        }

        return frame;
    };

    const int realPreviewCount = previewBySlot.size();
    if (realPreviewCount <= 1)
    {
        auto *singlePane = buildSlotPane(slotPreviews.at(0), previewContainer, true, true, 318);
        previewLayout->addWidget(singlePane);
    }
    else
    {
        auto *leftPane = buildSlotPane(slotPreviews.at(0), previewContainer, true, false, 159);
        previewLayout->addWidget(leftPane);

        auto *divider = new QFrame(previewContainer);
        divider->setFixedWidth(1);
        divider->setStyleSheet(QStringLiteral("background-color:#212225;"));
        divider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        previewLayout->addWidget(divider);

        auto *rightPane = buildSlotPane(slotPreviews.at(1), previewContainer, false, true, 159);
        previewLayout->addWidget(rightPane);
    }

    cardLayout->addWidget(previewContainer);

    auto *self = const_cast<StoragePage *>(this);
    auto *cardMoreButton = new QPushButton(previewContainer);
    configureIconButton(cardMoreButton, QStringLiteral(":/icons/more_vert.svg"), tr("More options"));
    cardMoreButton->move(previewContainer->width() - cardMoreButton->width() - 12, 12);
    cardMoreButton->raise();
    connect(cardMoreButton, &QPushButton::clicked, self, [self, sessionId = session.id, cardMoreButton]() {
        if (self)
            self->showSessionOptionsMenu(sessionId, cardMoreButton);
    });

    auto *infoContainer = new QFrame(card);
    infoContainer->setObjectName(QStringLiteral("recentSessionInfo"));
    infoContainer->setFixedSize(320, 122);
    infoContainer->setStyleSheet(QStringLiteral(
        "#recentSessionInfo{background:#212225;border:1px solid #212225;border-top:none;"
        "border-top-left-radius:0px;border-top-right-radius:0px;"
        "border-bottom-left-radius:12px;border-bottom-right-radius:12px;}"));

    auto *infoLayout = new QVBoxLayout(infoContainer);
    infoLayout->setContentsMargins(20, 18, 20, 18);
    infoLayout->setSpacing(6);

    auto *titleLabel = new QLabel(infoContainer);
    titleLabel->setStyleSheet(QStringLiteral("color:#FFFFFF;font-size:15px;font-weight:600;background:transparent;"));
    titleLabel->setText(elidedText(session.title, 60));
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    titleLabel->setWordWrap(true);
    infoLayout->addWidget(titleLabel);

    auto *startLabel = new QLabel(infoContainer);
    startLabel->setStyleSheet(QStringLiteral("color:#9EA3AA;font-size:13px;font-weight:500;background:transparent;"));
    startLabel->setText(tr("Start %1").arg(formattedDateTime(session.startedAtUtc)));
    startLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    startLabel->setWordWrap(true);
    infoLayout->addWidget(startLabel);

    auto *metaLabel = new QLabel(infoContainer);
    metaLabel->setStyleSheet(QStringLiteral("color:#9EA3AA;font-size:13px;font-weight:500;background:transparent;"));
    metaLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    const QString durationText = tr("Duration: %1").arg(formatDuration(session.totalDurationMs));
    const QString sizeText = tr("Size: %1").arg(formatFileSize(session.totalSizeBytes));
    metaLabel->setText(tr("%1 | %2").arg(durationText, sizeText));
    metaLabel->setWordWrap(true);
    infoLayout->addWidget(metaLabel);

    infoLayout->addStretch();

    cardLayout->addWidget(infoContainer);

    return card;
}

QString StoragePage::formatDuration(qint64 durationMs) const
{
    if (durationMs <= 0)
        return tr("Unknown");

    const qint64 totalSeconds = durationMs / 1000;
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

QString StoragePage::formatFileSize(qint64 bytes) const
{
    if (bytes <= 0)
        return tr("0 B");

    constexpr qint64 kKB = 1024;
    constexpr qint64 kMB = kKB * 1024;
    constexpr qint64 kGB = kMB * 1024;

    if (bytes >= kGB)
        return QStringLiteral("%1 GB").arg(QString::number(bytes / static_cast<double>(kGB), 'f', 2));
    if (bytes >= kMB)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / static_cast<double>(kMB), 'f', 1));
    if (bytes >= kKB)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / static_cast<double>(kKB), 'f', 1));
    return QStringLiteral("%1 B").arg(bytes);
}

bool StoragePage::eventFilter(QObject *watched, QEvent *event)
{
    if (event && event->type() == QEvent::Wheel)
    {
        if (m_recentScrollArea && watched == m_recentScrollArea->viewport())
        {
            auto *wheelEvent = static_cast<QWheelEvent *>(event);
            if (wheelEvent)
            {
                QPoint delta = wheelEvent->angleDelta();
                int horizontalDelta = delta.x();
                if (horizontalDelta == 0)
                    horizontalDelta = delta.y();

                if (horizontalDelta != 0)
                {
                    if (auto *scrollBar = m_recentScrollArea->horizontalScrollBar())
                    {
                        scrollBar->setValue(scrollBar->value() - horizontalDelta);
                        return true;
                    }
                }
            }
        }
    }

    if (event && event->type() == QEvent::MouseButtonRelease)
    {
        auto *mouseEvent = dynamic_cast<QMouseEvent *>(event);
        if (mouseEvent && mouseEvent->button() == Qt::LeftButton)
        {
            bool suppressSelection = false;

            if (auto *widget = qobject_cast<QWidget *>(watched))
            {
                QWidget *target = widget->childAt(mouseEvent->position().toPoint());
                while (target && !suppressSelection)
                {
                    if (target->property("suppressRowSelection").toBool())
                    {
                        suppressSelection = true;
                        break;
                    }
                    target = target->parentWidget();
                }

                if (!suppressSelection && widget->property("suppressRowSelection").toBool())
                    suppressSelection = true;
            }

            if (!suppressSelection)
            {
                QObject *obj = watched;
                while (obj)
                {
                    if (auto *widget = qobject_cast<QWidget *>(obj))
                    {
                        const auto cardIt = m_recentCardByWidget.constFind(widget);
                        if (cardIt != m_recentCardByWidget.cend())
                        {
                            selectSessionById(cardIt.value(), false);
                            break;
                        }

                        const auto rowIt = m_sessionRowByWidget.constFind(widget);
                        if (rowIt != m_sessionRowByWidget.cend())
                        {
                            selectSessionById(rowIt.value(), false);
                            break;
                        }
                    }
                    obj = obj->parent();
                }
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void StoragePage::clearLayout(QLayout *layout)
{
    if (!layout)
        return;

    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

QString StoragePage::displayNameForCamera(const QString &cameraId) const
{
    const QString trimmedId = cameraId.trimmed();
    if (m_cameraManager && !trimmedId.isEmpty() && m_cameraManager->hasCamera(trimmedId))
    {
        const auto info = m_cameraManager->camera(trimmedId);
        const QString alias = info.alias.trimmed();
        if (!alias.isEmpty())
            return alias;

        const QString name = info.name.trimmed();
        if (!name.isEmpty())
            return name;

        const QString fallbackId = info.id.trimmed();
        if (!fallbackId.isEmpty())
            return fallbackId;
    }
    return trimmedId;
}

QString StoragePage::sessionTitleFor(const RecordingManager::RecordingMetadata &metadata) const
{
    const QString displayName = displayNameForCamera(metadata.cameraId);
    if (metadata.slotId.isEmpty())
        return displayName.isEmpty() ? metadata.cameraId : displayName;
    if (displayName.isEmpty())
        return metadata.slotId;
    return QStringLiteral("[%1] %2").arg(metadata.slotId, displayName);
}

void StoragePage::addOrUpdateRecording(const RecordingManager::RecordingMetadata &metadata)
{
    Q_UNUSED(metadata);
    refreshContent();
}

void StoragePage::removeRecording(const QString &filePath)
{
    Q_UNUSED(filePath);
    refreshContent();
}
