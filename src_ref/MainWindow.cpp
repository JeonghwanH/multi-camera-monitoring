// Author: SeungJae Lee
// MainWindow: top-level controller that binds navigation, managers, and pages.

#include "MainWindow.h"

#include "managers/AiClient.h"
#include "managers/CameraManager.h"
#include "managers/PLCClient.h"
#include "managers/RecordingManager.h"
#include "managers/LocalizationManager.h"
#include "utils/ConfigUtils.h"
#include "utils/DebugConfig.h"
#include "utils/StringUtils.h"
#include "pages/SettingsPage.h"
#include "pages/StoragePage.h"
#include "pages/WeldingPage.h"

#include <QApplication>
#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QEvent>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QIcon>
#include <QMediaDevices>
#include <QMessageBox>
#if defined(QT_CONFIG) && defined(QT_FEATURE_permissions)
#define WB_HAS_QT_PERMISSIONS QT_CONFIG(permissions)
#else
#define WB_HAS_QT_PERMISSIONS 0
#endif

#if WB_HAS_QT_PERMISSIONS
#include <QPermissions>
#endif
#include <QPushButton>
#include <QSet>
#include <QVector>
#include <QStringList>
#include <QSize>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QToolButton>
#include <QSignalBlocker>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QBuffer>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <utility>
#include <QWidget>

namespace
{
constexpr int kNavigationExpandedWidth = 236;
constexpr int kNavigationCollapsedWidth = 64;
constexpr int kNavigationButtonExpandedWidth = 212;
constexpr int kNavigationButtonExpandedHeight = 64;
constexpr int kNavigationButtonCollapsedSide = 40;
const QSize kNavigationIconSizeExpanded(24, 24);
const QSize kNavigationIconSizeCollapsed(24, 24);

QRectF clampNormalizedRect(const QRectF &rect)
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

QRectF toNormalizedRect(const QRect &pixelRect, const QSize &frameSize)
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

QRect toPixelRect(const QRectF &normalizedRect, const QSize &frameSize)
{
    if (!normalizedRect.isValid() || frameSize.width() <= 0 || frameSize.height() <= 0)
        return QRect();
    const QRectF clamped = clampNormalizedRect(normalizedRect);
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

QRect captureRectFromNormalized(const QRectF &normalizedRect, const QSize &frameSize)
{
    constexpr int kCaptureSide = 512;
    const QSize captureSize(kCaptureSide, kCaptureSide);

    if (!normalizedRect.isValid() || frameSize.width() <= 0 || frameSize.height() <= 0)
    {
        const int width = std::min(frameSize.width(), captureSize.width());
        const int height = std::min(frameSize.height(), captureSize.height());
        if (width > 0 && height > 0)
            return QRect(QPoint(0, 0), QSize(width, height));
        return QRect();
    }

    QRect pixelRect = toPixelRect(normalizedRect, frameSize);
    if (!pixelRect.isValid())
    {
        const int width = std::min(frameSize.width(), captureSize.width());
        const int height = std::min(frameSize.height(), captureSize.height());
        if (width > 0 && height > 0)
            return QRect(QPoint(0, 0), QSize(width, height));
        return QRect();
    }

    QSize targetSize = captureSize;
    if (frameSize.width() < captureSize.width() || frameSize.height() < captureSize.height())
    {
        targetSize.setWidth(std::min(frameSize.width(), captureSize.width()));
        targetSize.setHeight(std::min(frameSize.height(), captureSize.height()));
    }

    QPoint origin = pixelRect.topLeft();
    const int maxX = std::max(0, frameSize.width() - targetSize.width());
    const int maxY = std::max(0, frameSize.height() - targetSize.height());
    origin.setX(std::clamp(origin.x(), 0, maxX));
    origin.setY(std::clamp(origin.y(), 0, maxY));
    return QRect(origin, targetSize);
}

using StringUtils::canonicalString;
using StringUtils::sanitizeForStreamKey;
using StringUtils::streamKeyFromAlias;

bool extractNumeric(const QJsonValue &value, double &out)
{
    if (value.isDouble())
    {
        out = value.toDouble();
        return true;
    }
    if (value.isString())
    {
        bool ok = false;
        const double converted = value.toString().toDouble(&ok);
        if (ok)
        {
            out = converted;
            return true;
        }
    }
    if (value.isArray())
    {
        const QJsonArray arr = value.toArray();
        for (const QJsonValue &entry : arr)
        {
            if (extractNumeric(entry, out))
                return true;
        }
    }
    if (value.isObject())
    {
        const QJsonObject obj = value.toObject();
        static const QStringList keys = {
            QStringLiteral("value"),
            QStringLiteral("mm"),
            QStringLiteral("amount"),
            QStringLiteral("current"),
            QStringLiteral("target")
        };
        for (const QString &key : keys)
        {
            if (!obj.contains(key))
                continue;
            if (extractNumeric(obj.value(key), out))
                return true;
        }
    }
    return false;
}

struct CameraConfig
{
    int width = 1920;
    int height = 1080;
    double fps = 30.0;
    QString nameOverride;
    QString aliasOverride;
    QString slotIdOverride;
    bool hasNameOverride = false;
    bool hasAliasOverride = false;
    bool hasSlotOverride = false;
};

CameraConfig loadCameraConfig(const QString &cameraId)
{
    CameraConfig config;

    const QJsonDocument doc = ConfigUtils::loadConfig();
    if (!doc.isObject())
    {
        qWarning() << "Camera config is not a JSON object";
        return config;
    }

    const QJsonObject root = doc.object();
    const QJsonObject camerasObj = root.value(QStringLiteral("cameras")).toObject();
    const QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    const QJsonArray profilesArray = settingsObj.value(QStringLiteral("cameraProfiles")).toArray();

    auto applyResolutionOverrides = [&config](const QJsonObject &object) {
        if (object.isEmpty())
            return;

        const QJsonObject resolutionObj = object.value(QStringLiteral("resolution")).toObject();
        if (!resolutionObj.isEmpty())
        {
            const int width = resolutionObj.value(QStringLiteral("width")).toInt(config.width);
            const int height = resolutionObj.value(QStringLiteral("height")).toInt(config.height);
            if (width > 0)
                config.width = width;
            if (height > 0)
                config.height = height;
        }

        const QJsonValue fpsValue = object.value(QStringLiteral("fps"));
        if (fpsValue.isDouble())
        {
            const double fps = fpsValue.toDouble(config.fps);
            if (fps > 0.0)
                config.fps = fps;
        }
    };

    auto applyNameSlotOverrides = [&config](const QJsonObject &object) {
        if (object.isEmpty())
            return;

        const QString name = object.value(QStringLiteral("name")).toString();
        const QString alias = object.value(QStringLiteral("alias")).toString();
        if (!alias.isEmpty())
        {
            config.aliasOverride = alias;
            config.hasAliasOverride = true;
        }
        if (!name.isEmpty())
        {
            config.nameOverride = name;
            config.hasNameOverride = true;
        }
        else if (!alias.isEmpty() && !config.hasNameOverride)
        {
            config.nameOverride = alias;
            config.hasNameOverride = true;
        }

        const QString slotId = object.value(QStringLiteral("slotId")).toString();
        if (!slotId.isEmpty())
        {
            config.slotIdOverride = slotId;
            config.hasSlotOverride = true;
        }
    };

    applyResolutionOverrides(camerasObj.value(QStringLiteral("default")).toObject());

    if (!cameraId.isEmpty())
    {
        QJsonObject profileObj;
        for (const QJsonValue &value : profilesArray)
        {
            const QJsonObject candidate = value.toObject();
            if (candidate.value(QStringLiteral("id")).toString() == cameraId)
            {
                profileObj = candidate;
                break;
            }
        }

        if (!profileObj.isEmpty())
        {
            applyNameSlotOverrides(profileObj);
        }
        else
        {
            const QJsonObject legacySettingsCamera =
                settingsObj.value(QStringLiteral("camera")).toObject().value(cameraId).toObject();
            if (!legacySettingsCamera.isEmpty())
            {
                applyNameSlotOverrides(legacySettingsCamera);
            }
            else
            {
                const QJsonObject legacyCameraObj = camerasObj.value(cameraId).toObject();
                if (!legacyCameraObj.isEmpty())
                {
                    applyNameSlotOverrides(legacyCameraObj);
                    applyResolutionOverrides(legacyCameraObj);
                }
            }
        }
    }

    qInfo() << "Camera config resolved for" << (cameraId.isEmpty() ? QStringLiteral("default") : cameraId)
            << "->" << config.width << "x" << config.height << "@" << config.fps << "fps";

    return config;
}

QString navigationButtonStyle(bool collapsed)
{
    if (collapsed)
    {
        return QStringLiteral(
            "QPushButton{"
            " background:none;"
            " color:#B0B4BA;"
            " border:none;"
            " border-radius:10px;"
            " padding:0;"
            " font-size:20px;"
            " font-weight:700;"
            " text-align:center;"
            "}"
            "QPushButton:hover{background:rgba(34, 255, 162, 0.12); color:rgba(70, 254, 177, 0.83);}" 
            "QPushButton:checked{background:rgba(34, 255, 162, 0.22); color:#22FFA2;}" );
    }

    return QStringLiteral(
        "QPushButton{"
        " background:none;"
        " color:#B0B4BA;"
        " border:none;"
        " border-radius:10px;"
        " padding:0 16px;"
        " font-size:20px;"
        " font-weight:700;"
        " text-align:left;"
        "}"
        "QPushButton:hover{background:rgba(34, 255, 162, 0.12); color:rgba(70, 254, 177, 0.83);}" 
        "QPushButton:checked{background:rgba(34, 255, 162, 0.18); color:#22FFA2;}" );
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_cameraManager = std::make_unique<CameraManager>();
    m_recordingManager = std::make_unique<RecordingManager>();
    m_aiClient = std::make_unique<AiClient>();
    m_plcClient = std::make_unique<PLCClient>();
    m_localizationManager = std::make_unique<LocalizationManager>();
    applySavedLanguagePreference();

    setupUi();
    setupNavigation();
    setupPages();
    makeConnections();
    loadAnalysisConfigs();
    updateAiState();

    if (m_localizationManager)
    {
        connect(m_localizationManager.get(), &LocalizationManager::languageChanged, this, [this](const QString &) {
            if (m_camerasInitialized)
                refreshCameraDevices();
        });
    }

    retranslateUi();

    QTimer::singleShot(0, this, &MainWindow::ensureCameraPermission);
}

MainWindow::~MainWindow()
{
    stopAiSession(true);
}

void MainWindow::applySavedLanguagePreference()
{
    if (!m_localizationManager)
        return;

    const QJsonDocument doc = ConfigUtils::loadConfig();
    if (!doc.isObject())
        return;

    const QJsonObject root = doc.object();
    const QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    const QString languageCode = settingsObj.value(QStringLiteral("language")).toString().trimmed();
    if (languageCode.isEmpty())
        return;

    if (languageCode == m_localizationManager->currentLanguageCode())
        return;

    if (!m_localizationManager->applyLanguage(languageCode))
        qWarning() << "[MainWindow] Failed to apply saved language preference" << languageCode;
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    central->setStyleSheet(QStringLiteral("background-color:#000000;"));
    auto *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(12);

    m_navigationWidget = new QWidget(central);
    m_navigationWidget->setFixedWidth(kNavigationExpandedWidth);
    m_navigationWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    m_stackedWidget = new QStackedWidget(central);

    mainLayout->addWidget(m_navigationWidget);
    mainLayout->addWidget(m_stackedWidget, 1);

    setCentralWidget(central);
    resize(1920, 1080);
}

void MainWindow::setupNavigation()
{
    m_navigationWidget->setStyleSheet(QString("background:rgba(17,17,19,1.0);border:none;border-radius:12px;"));

    auto *layout = new QVBoxLayout(m_navigationWidget);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto *headerWidget = new QWidget(m_navigationWidget);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);

    m_companyAvatar = new QLabel(headerWidget);
    m_companyAvatar->setFixedSize(48, 48);
    m_companyAvatar->setAlignment(Qt::AlignCenter);
    m_companyAvatar->setStyleSheet("QLabel{border-radius:12px;background-color:#232527;}");

    QSvgRenderer avatarRenderer(QStringLiteral(":/icons/avatar.svg"));
    if (avatarRenderer.isValid()) {
        QPixmap avatarPixmap(m_companyAvatar->size());
        avatarPixmap.fill(Qt::transparent);

        QPainter painter(&avatarPixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        avatarRenderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(m_companyAvatar->size())));

        m_companyAvatar->setPixmap(avatarPixmap);
    }

    m_companyName = new QLabel(headerWidget);
    m_companyName->setStyleSheet(
            "QLabel{"
                " background:none;"
                " color:#B0B4BA;"
                " border:none;"
                " border-radius:10px;"
                " padding:0 0px;"
                " font-size:20px;"
                " font-weight:700;"
                " text-align: left;"
                "}"
        );

    headerLayout->addWidget(m_companyAvatar, 0, Qt::AlignVCenter);
    headerLayout->addWidget(m_companyName, 1, Qt::AlignVCenter);

    m_toggleNavigationButton = new QToolButton(headerWidget);
    m_toggleNavigationButton->setIcon(QIcon(QStringLiteral(":/icons/nav_expansion.svg")));
    m_toggleNavigationButton->setIconSize(kNavigationIconSizeExpanded);
    m_toggleNavigationButton->setCheckable(true);
    m_toggleNavigationButton->setAutoRaise(true);
    m_toggleNavigationButton->setCursor(Qt::PointingHandCursor);
    m_toggleNavigationButton->setStyleSheet(
        "QToolButton{"
        " background:transparent;"
        " border:none;"
        " padding:4px;"
        " border-radius:8px;"
        "}"
        "QToolButton:hover{background:rgba(34, 255, 162, 0.12);}"
        "QToolButton:checked{background:transparent;}"
        "QToolButton:checked:hover{background:rgba(34, 255, 162, 0.12);}" );
    m_toggleNavigationButton->setFixedSize(kNavigationButtonCollapsedSide, kNavigationButtonCollapsedSide);
    headerLayout->addWidget(m_toggleNavigationButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    layout->addWidget(headerWidget);

    m_navigationContent = new QWidget(m_navigationWidget);
    auto *contentLayout = new QVBoxLayout(m_navigationContent);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    m_navigationButtonGroup = new QButtonGroup(this);
    m_navigationButtonGroup->setExclusive(true);

    m_weldingButton = new QPushButton(m_navigationContent);
    m_weldingButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_weldingButton->setFixedSize(kNavigationButtonExpandedWidth, kNavigationButtonExpandedHeight);
    m_weldingButton->setStyleSheet(navigationButtonStyle(false));
    m_weldingButton->setIcon(QIcon(QStringLiteral(":/icons/nav_project.svg")));
    m_weldingButton->setIconSize(kNavigationIconSizeExpanded);
    m_weldingButton->setCursor(Qt::PointingHandCursor);
    m_weldingButton->setCheckable(true);
    m_navigationButtonGroup->addButton(m_weldingButton);

    m_storageButton = new QPushButton(m_navigationContent);
    m_storageButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_storageButton->setFixedSize(kNavigationButtonExpandedWidth, kNavigationButtonExpandedHeight);
    m_storageButton->setStyleSheet(navigationButtonStyle(false));
    m_storageButton->setIcon(QIcon(QStringLiteral(":/icons/nav_storage.svg")));
    m_storageButton->setIconSize(kNavigationIconSizeExpanded);
    m_storageButton->setCursor(Qt::PointingHandCursor);
    m_storageButton->setCheckable(true);
    m_navigationButtonGroup->addButton(m_storageButton);

    contentLayout->addWidget(m_weldingButton);
    contentLayout->addWidget(m_storageButton);
    contentLayout->addStretch();

    m_settingsButton = new QPushButton(m_navigationContent);
    m_settingsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_settingsButton->setFixedSize(kNavigationButtonExpandedWidth, kNavigationButtonExpandedHeight);
    m_settingsButton->setStyleSheet(navigationButtonStyle(false));
    m_settingsButton->setIcon(QIcon(QStringLiteral(":/icons/nav_settings.svg")));
    m_settingsButton->setIconSize(kNavigationIconSizeExpanded);
    m_settingsButton->setCursor(Qt::PointingHandCursor);
    m_settingsButton->setCheckable(true);
    m_navigationButtonGroup->addButton(m_settingsButton);

    m_appVersion = new QLabel(m_navigationContent);
    m_appVersion->setStyleSheet(QStringLiteral("color: #B0B4BA;"));

    contentLayout->addWidget(m_settingsButton);
    contentLayout->addWidget(m_appVersion);

    layout->addWidget(m_navigationContent, 1);

    updateNavigationVisibility();
}

void MainWindow::setupPages()
{
    m_weldingPage = new WeldingPage(m_cameraManager.get(), m_recordingManager.get(),
                                     m_aiClient.get(), m_plcClient.get(), m_stackedWidget);
    m_storagePage = new StoragePage(m_cameraManager.get(), m_recordingManager.get(), m_aiClient.get(), m_stackedWidget);
    m_settingsPage = new SettingsPage(m_cameraManager.get(), m_aiClient.get(), m_plcClient.get(),
                                      m_localizationManager.get(), m_stackedWidget);

    m_stackedWidget->addWidget(m_weldingPage);
    m_stackedWidget->addWidget(m_storagePage);
    m_stackedWidget->addWidget(m_settingsPage);
    m_stackedWidget->setCurrentWidget(m_weldingPage);

    if (m_weldingPage)
    {
        connect(m_weldingPage, &WeldingPage::frameCaptured, this, &MainWindow::onFrameCaptured);
        connect(m_weldingPage, &WeldingPage::aiToggleRequested, this, &MainWindow::handleCameraAiToggle);
    }

    if (m_weldingButton)
        m_weldingButton->setChecked(true);
}

void MainWindow::makeConnections()
{
    connect(m_weldingButton, &QPushButton::clicked, this, [this]() {
        m_stackedWidget->setCurrentWidget(m_weldingPage);
    });

    connect(m_storageButton, &QPushButton::clicked, this, [this]() {
        m_stackedWidget->setCurrentWidget(m_storagePage);
    });

    connect(m_settingsButton, &QPushButton::clicked, this, [this]() {
        m_stackedWidget->setCurrentWidget(m_settingsPage);
    });

    if (m_toggleNavigationButton)
    {
        connect(m_toggleNavigationButton, &QToolButton::toggled, this, [this](bool checked) {
            m_navigationCollapsed = checked;
            updateNavigationVisibility();
            retranslateUi();
        });
    }

    connect(m_stackedWidget, &QStackedWidget::currentChanged, this, [this](int index) {
        if (!m_navigationButtonGroup)
            return;
        QWidget *current = m_stackedWidget->widget(index);
        if (current == m_weldingPage)
            m_weldingButton->setChecked(true);
        else if (current == m_storagePage)
            m_storageButton->setChecked(true);
        else if (current == m_settingsPage)
            m_settingsButton->setChecked(true);
    });

    if (m_cameraManager)
    {
        connect(m_cameraManager.get(), &CameraManager::cameraUpdated, this, [this](const CameraManager::CameraInfo &info) {
            persistCameraOverrides(info.id, info.name, info.slotId);
        });
    }

    if (m_aiClient)
    {
        connect(m_aiClient.get(), &AiClient::settingsChanged, this, [this](const AiClient::Settings &) {
            updateCameraFeatureStates();
            updateAiState();
        });
        connect(m_aiClient.get(), &AiClient::modelStartupFinished, this, &MainWindow::handleAiModelStartupFinished);
        connect(m_aiClient.get(), &AiClient::modelShutdownFinished, this, &MainWindow::handleAiModelShutdownFinished);
        connect(m_aiClient.get(), &AiClient::analysisFinished, this,
                [this](bool ok, const QJsonObject &results, const QString &message) {
                    handleAnalysisFinished(ok, results, message);
                });
        connect(m_aiClient.get(), &AiClient::infoMessage, this, [](const QString &text) {
            qInfo() << "[AiClient]" << text;
        });
        connect(m_aiClient.get(), &AiClient::errorMessage, this, [](const QString &text) {
            qWarning() << "[AiClient]" << text;
        });
    }

    if (m_plcClient)
    {
        connect(m_plcClient.get(), &PLCClient::connectionStateChanged, this,
                [this](const QString &, PLCClient::State state) {
                    if (state == PLCClient::State::Connected || state == PLCClient::State::Disconnected
                        || state == PLCClient::State::Error)
                    {
                        updateCameraFeatureStates();
                    }
                });
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        QMainWindow::changeEvent(event);
        retranslateUi();
        return;
    }

    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("Weldbeing"));

    if (m_companyName)
        m_companyName->setText(tr("Doowon"));

    const QString projectLabel = tr("Project");
    const QString storageLabel = tr("Storage");
    const QString settingsLabel = tr("Settings");

    if (m_weldingButton)
    {
        if (m_navigationCollapsed)
        {
            m_weldingButton->setText(QString());
            m_weldingButton->setToolTip(projectLabel);
        }
        else
        {
            m_weldingButton->setText(projectLabel);
            m_weldingButton->setToolTip(QString());
        }
    }

    if (m_storageButton)
    {
        if (m_navigationCollapsed)
        {
            m_storageButton->setText(QString());
            m_storageButton->setToolTip(storageLabel);
        }
        else
        {
            m_storageButton->setText(storageLabel);
            m_storageButton->setToolTip(QString());
        }
    }

    if (m_settingsButton)
    {
        if (m_navigationCollapsed)
        {
            m_settingsButton->setText(QString());
            m_settingsButton->setToolTip(settingsLabel);
        }
        else
        {
            m_settingsButton->setText(settingsLabel);
            m_settingsButton->setToolTip(QString());
        }
    }

    if (m_appVersion)
        m_appVersion->setText(tr("Weldbeing V v1.0"));

    if (m_toggleNavigationButton)
        m_toggleNavigationButton->setToolTip(tr("Toggle Navigation"));
}

void MainWindow::updateNavigationVisibility()
{
    if (!m_navigationWidget)
        return;

    const int targetWidth = m_navigationCollapsed ? kNavigationCollapsedWidth : kNavigationExpandedWidth;
    m_navigationWidget->setFixedWidth(targetWidth);

    if (m_navigationContent)
        m_navigationContent->setVisible(true);
    if (m_companyAvatar)
        m_companyAvatar->setVisible(!m_navigationCollapsed);
    if (m_companyName)
        m_companyName->setVisible(!m_navigationCollapsed);
    if (m_appVersion)
        m_appVersion->setVisible(!m_navigationCollapsed);

    if (auto *contentLayout = qobject_cast<QVBoxLayout *>(m_navigationContent ? m_navigationContent->layout() : nullptr))
    {
        const Qt::Alignment expandedAlign = Qt::AlignLeft | Qt::AlignVCenter;
        const Qt::Alignment collapsedAlign = Qt::AlignHCenter | Qt::AlignVCenter;
        contentLayout->setAlignment(m_weldingButton, m_navigationCollapsed ? collapsedAlign : expandedAlign);
        contentLayout->setAlignment(m_storageButton, m_navigationCollapsed ? collapsedAlign : expandedAlign);
        contentLayout->setAlignment(m_settingsButton, m_navigationCollapsed ? collapsedAlign : expandedAlign);
    }

    if (m_toggleNavigationButton)
    {
        m_toggleNavigationButton->setIconSize(m_navigationCollapsed ? kNavigationIconSizeCollapsed : kNavigationIconSizeExpanded);
        m_toggleNavigationButton->setFixedSize(kNavigationButtonCollapsedSide, kNavigationButtonCollapsedSide);
    }

    const QString buttonStyle = navigationButtonStyle(m_navigationCollapsed);
    for (QPushButton *button : {m_weldingButton, m_storageButton, m_settingsButton})
    {
        if (!button)
            continue;
        button->setStyleSheet(buttonStyle);
        if (m_navigationCollapsed)
        {
            button->setIconSize(kNavigationIconSizeCollapsed);
            button->setFixedSize(kNavigationButtonCollapsedSide, kNavigationButtonCollapsedSide);
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }
        else
        {
            button->setIconSize(kNavigationIconSizeExpanded);
            button->setFixedSize(kNavigationButtonExpandedWidth, kNavigationButtonExpandedHeight);
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }
    }
    if (m_toggleNavigationButton)
    {
        QSignalBlocker blocker(m_toggleNavigationButton);
        m_toggleNavigationButton->setChecked(m_navigationCollapsed);
    }

    updateGeometry();
}

void MainWindow::ensureCameraPermission()
{
#if WB_HAS_QT_PERMISSIONS
    QCameraPermission permission;
    const auto status = qApp->checkPermission(permission);

    if (status == Qt::PermissionStatus::Granted)
    {
        ensureMicrophonePermission();
        return;
    }

    if (status == Qt::PermissionStatus::Undetermined)
    {
        qApp->requestPermission(permission, this, [this](const QPermission &result) {
            if (result.status() == Qt::PermissionStatus::Granted)
            {
                ensureMicrophonePermission();
            }
            else
                showCameraPermissionWarning();
        });
        return;
    }

    showCameraPermissionWarning();
    return;
#else
    ensureMicrophonePermission();
    return;
#endif
}

void MainWindow::showCameraPermissionWarning()
{
    if (m_cameraPermissionWarningShown)
        return;

    m_cameraPermissionWarningShown = true;

#if defined(Q_OS_MACOS)
    const QString guidance = tr("Weldbeing needs camera access to show live previews and record welding sessions. "
                                "Grant access in System Settings > Privacy & Security > Camera, then restart the application.");
#else
    const QString guidance = tr("Weldbeing needs camera access to show live previews and record welding sessions. "
                                "Please enable camera permissions for this application.");
#endif

    QMessageBox::warning(this, tr("Camera Access Required"), guidance);
}

void MainWindow::ensureMicrophonePermission()
{
#if WB_HAS_QT_PERMISSIONS
    QMicrophonePermission permission;
    const auto status = qApp->checkPermission(permission);

    if (status == Qt::PermissionStatus::Granted)
    {
        initializeCameras();
        return;
    }

    if (status == Qt::PermissionStatus::Undetermined)
    {
        qApp->requestPermission(permission, this, [this](const QPermission &result) {
            if (result.status() == Qt::PermissionStatus::Granted)
            {
                initializeCameras();
            }
            else
            {
                showMicrophonePermissionWarning();
                initializeCameras();
            }
        });
        return;
    }

    showMicrophonePermissionWarning();
    initializeCameras();
    return;
#else
    initializeCameras();
    return;
#endif
}

void MainWindow::showMicrophonePermissionWarning()
{
    if (m_microphonePermissionWarningShown)
        return;

    m_microphonePermissionWarningShown = true;

#if defined(Q_OS_MACOS)
    const QString guidance = tr("Weldbeing uses the microphone to pair with Continuity and external cameras. "
                                "Grant access in System Settings > Privacy & Security > Microphone for smooth live previews.");
#else
    const QString guidance = tr("Weldbeing uses the microphone to pair with connected cameras. "
                                "Please enable microphone permissions for this application.");
#endif

    QMessageBox::warning(this, tr("Microphone Access Recommended"), guidance);
}

void MainWindow::initializeCameras()
{
    if (m_camerasInitialized)
        return;

    m_camerasInitialized = true;

    if (!m_mediaDevices)
    {
        m_mediaDevices = new QMediaDevices(this);
        connect(m_mediaDevices, &QMediaDevices::videoInputsChanged, this, &MainWindow::refreshCameraDevices);
    }

    refreshCameraDevices();
}

void MainWindow::refreshCameraDevices()
{
    if (!m_cameraManager)
        return;

    const auto devices = QMediaDevices::videoInputs();
    qInfo() << "Refreshing camera devices. Found" << devices.size() << "video inputs.";

    QSet<QString> activeIds;
    activeIds.reserve(devices.size());

    for (const QCameraDevice &device : devices)
    {
        const QString cameraId = device.id();
        qInfo() << "Processing camera" << device.description() << "(" << cameraId << ")";
        activeIds.insert(cameraId);

        const CameraConfig overrides = loadCameraConfig(cameraId);
        const QString configuredName = overrides.hasNameOverride ? overrides.nameOverride : device.description();
        const QString configuredAlias = overrides.hasAliasOverride ? overrides.aliasOverride : QString();
        const QString configuredSlot = overrides.hasSlotOverride ? overrides.slotIdOverride : device.description();

        if (!m_cameraManager->hasCamera(cameraId))
        {
            CameraManager::CameraInfo info;
            info.id = cameraId;
            info.name = configuredName;
            info.alias = configuredAlias;
            info.slotId = configuredSlot;
            info.status = tr("Ready");
            m_cameraManager->addCamera(info);
        }
        else
        {
            auto info = m_cameraManager->camera(cameraId);
            info.name = configuredName;
            info.alias = configuredAlias;
            info.slotId = configuredSlot;
            info.status = tr("Ready");
            m_cameraManager->updateCamera(info);
        }

        QPointer<QCamera> cameraPtr = m_cameraControllers.value(cameraId);

        if (!cameraPtr)
        {
            auto *camera = new QCamera(device, this);
            const auto format = preferredCameraFormat(device);
            if (!format.isNull())
            {
                camera->setCameraFormat(format);
                qInfo() << "Applied preferred format" << format.resolution() << "fps range" << format.minFrameRate() << "-" << format.maxFrameRate();
            }
            cameraPtr = camera;
            m_cameraControllers.insert(cameraId, cameraPtr);
            m_cameraManager->assignCameraHandle(cameraId, camera);
            camera->start();
            qInfo() << "Started camera" << cameraId;
        }
        else
        {
            m_cameraManager->assignCameraHandle(cameraId, cameraPtr);
            if (!cameraPtr->isActive())
            {
                const auto format = preferredCameraFormat(device);
                if (!format.isNull() && cameraPtr->cameraFormat() != format)
                {
                    cameraPtr->setCameraFormat(format);
                    qInfo() << "Updated camera" << cameraId << "format to" << format.resolution() << "fps range" << format.minFrameRate() << "-" << format.maxFrameRate();
                }
                cameraPtr->start();
                qInfo() << "Restarted inactive camera" << cameraId;
            }
        }
    }

    const auto currentCameras = m_cameraManager->cameras();
    for (const auto &info : currentCameras)
    {
        if (!activeIds.contains(info.id))
        {
            m_cameraManager->removeCamera(info.id);
            QPointer<QCamera> cameraPtr = m_cameraControllers.take(info.id);
            if (cameraPtr)
            {
                cameraPtr->stop();
                cameraPtr->deleteLater();
                qInfo() << "Removed camera" << info.id;
            }
            m_latestFrames.remove(info.id);
        }
    }

    updateCameraFeatureStates();
}

QCameraFormat MainWindow::preferredCameraFormat(const QCameraDevice &device) const
{
    const CameraConfig config = loadCameraConfig(device.id());

    QCameraFormat fallback;

    const auto formats = device.videoFormats();
    for (const QCameraFormat &format : formats)
    {
        const QSize resolution = format.resolution();
        if (resolution.width() != config.width || resolution.height() != config.height)
            continue;

        const double minFps = format.minFrameRate();
        const double maxFps = format.maxFrameRate();

        if (minFps <= config.fps && maxFps >= config.fps)
        {
            qInfo() << "Selected format" << resolution << "with fps range" << minFps << "-" << maxFps << "for" << device.description();
            return format;
        }

        if (fallback.isNull())
            fallback = format;
    }

    if (fallback.isNull())
        qWarning() << "No matching camera format found for" << device.description() << "using device default.";
    else
        qWarning() << "Exact match not found for" << device.description() << "falling back to" << fallback.resolution() << "fps" << fallback.minFrameRate() << "-" << fallback.maxFrameRate();

    return fallback;
}

void MainWindow::updateCameraFeatureStates()
{
    if (!m_cameraManager)
        return;

    const bool aiEnabled = m_aiClient ? m_aiClient->settings().enableAnalysis : false;
    const bool plcEnabled = m_plcClient ? m_plcClient->isConnected() : false;

    const auto cameras = m_cameraManager->cameras();
    for (auto info : cameras)
    {
        QVector<CameraManager::CameraInfo::SettingState> states;

        CameraManager::CameraInfo::SettingState aiState;
        aiState.name = tr("AI Analysis");
        aiState.enabled = aiEnabled;
        states.append(aiState);

        CameraManager::CameraInfo::SettingState plcState;
        plcState.name = tr("PLC Control");
        plcState.enabled = plcEnabled;
        states.append(plcState);

        bool changed = info.settings.size() != states.size();
        if (!changed)
        {
            for (int index = 0; index < states.size(); ++index)
            {
                const auto &existing = info.settings.at(index);
                const auto &target = states.at(index);
                if (existing.name != target.name || existing.enabled != target.enabled)
                {
                    changed = true;
                    break;
                }
            }
        }

        if (!changed)
            continue;

        info.settings = states;
        m_cameraManager->updateCamera(info);
    }
}

void MainWindow::persistCameraOverrides(const QString &cameraId, const QString &name, const QString &slotId)
{
    if (cameraId.isEmpty())
        return;

    QJsonDocument doc = ConfigUtils::loadConfig();
    if (!doc.isObject())
        doc = QJsonDocument(QJsonObject());

    QJsonObject root = doc.object();
    QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    QJsonArray profilesArray = settingsObj.value(QStringLiteral("cameraProfiles")).toArray();

    int profileIndex = -1;
    for (int index = 0; index < profilesArray.size(); ++index)
    {
        const QJsonObject profile = profilesArray.at(index).toObject();
        if (profile.value(QStringLiteral("id")).toString() == cameraId)
        {
            profileIndex = index;
            break;
        }
    }

    QJsonObject profileObj;
    if (profileIndex >= 0)
        profileObj = profilesArray.at(profileIndex).toObject();

    const QString existingName = profileObj.value(QStringLiteral("name")).toString();
    const QString existingSlot = profileObj.value(QStringLiteral("slotId")).toString();

    bool profileChanged = false;

    if (existingName != name)
    {
        if (name.isEmpty())
            profileObj.remove(QStringLiteral("name"));
        else
            profileObj.insert(QStringLiteral("name"), name);
        profileChanged = true;
    }

    if (existingSlot != slotId)
    {
        if (slotId.isEmpty())
            profileObj.remove(QStringLiteral("slotId"));
        else
            profileObj.insert(QStringLiteral("slotId"), slotId);
        profileChanged = true;
    }

    if (profileIndex < 0 && profileChanged)
    {
        profileObj.insert(QStringLiteral("id"), cameraId);
        profilesArray.append(profileObj);
    }
    else if (profileIndex >= 0 && profileChanged)
    {
        profilesArray.replace(profileIndex, profileObj);
    }

    bool legacyRemoved = false;
    if (!cameraId.isEmpty())
    {
        QJsonObject camerasObj = root.value(QStringLiteral("cameras")).toObject();
        if (cameraId != QStringLiteral("default") && camerasObj.contains(cameraId))
        {
            camerasObj.remove(cameraId);
            root.insert(QStringLiteral("cameras"), camerasObj);
            legacyRemoved = true;
        }
    }

    if (!profileChanged && !legacyRemoved)
        return;

    settingsObj.insert(QStringLiteral("cameraProfiles"), profilesArray);
    root.insert(QStringLiteral("settings"), settingsObj);
    doc.setObject(root);

    if (!ConfigUtils::saveConfig(doc))
        qWarning() << "Failed to persist camera overrides for" << cameraId;
}

QString MainWindow::aiExclusiveMessage() const
{
    return tr("AI analysis can only run on one camera at a time.\n"
              "Stop analysis on the active camera before starting it here.");
}

void MainWindow::updateAiToggleLocking()
{
    if (!m_weldingPage)
        return;

    QString exclusiveCameraId;
    if (!m_enabledAnalysisCameras.isEmpty())
    {
        if (!m_activeAnalysisCameraId.isEmpty() && m_enabledAnalysisCameras.contains(m_activeAnalysisCameraId))
            exclusiveCameraId = m_activeAnalysisCameraId;
        else
            exclusiveCameraId = *m_enabledAnalysisCameras.constBegin();
    }

    m_weldingPage->applyAiToggleLock(exclusiveCameraId, aiExclusiveMessage());
}

void MainWindow::loadAnalysisConfigs(const QSet<QString> &enabledCameras)
{
    m_analysisConfigs.clear();
    m_enabledAnalysisCameras.clear();
    m_enabledAnalysisOrder.clear();

    const QJsonDocument doc = ConfigUtils::loadConfig();
    if (!doc.isObject())
    {
        ensureActiveAnalysisCamera();
        updateAiState();
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    const QJsonArray profilesArray = settingsObj.value(QStringLiteral("cameraProfiles")).toArray();

    auto parsePixelRoi = [](const QJsonObject &roiObj) -> QRect {
        if (roiObj.isEmpty())
            return {};
        const int x = roiObj.value(QStringLiteral("x")).toInt(-1);
        const int y = roiObj.value(QStringLiteral("y")).toInt(-1);
        const int w = roiObj.value(QStringLiteral("width")).toInt(-1);
        const int h = roiObj.value(QStringLiteral("height")).toInt(-1);
        if (x < 0 || y < 0 || w <= 0 || h <= 0)
            return {};
        return QRect(x, y, w, h);
    };


    for (const QJsonValue &value : profilesArray)
    {
        const QJsonObject profile = value.toObject();
        const QString id = profile.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;

        const QJsonObject analysisObj = profile.value(QStringLiteral("analysis")).toObject();
        AnalysisConfig config;
        config.alias = profile.value(QStringLiteral("alias")).toString();
        config.url = analysisObj.value(QStringLiteral("url")).toString();
        config.streamKey = analysisObj.value(QStringLiteral("streamKey")).toString();
        if (config.streamKey.trimmed().isEmpty())
            config.streamKey = streamKeyFromAlias(config.alias, id);

        const QJsonValue typesValue = analysisObj.value(QStringLiteral("weldAnalysisType"));
        if (typesValue.isArray())
        {
            for (const QJsonValue &typeVal : typesValue.toArray())
            {
                const QString type = typeVal.toString().trimmed();
                if (!type.isEmpty())
                    config.weldTypes.append(type);
            }
        }
        else if (typesValue.isString())
        {
            const QString type = typesValue.toString().trimmed();
            if (!type.isEmpty())
                config.weldTypes.append(type);
        }

        config.refType = analysisObj.value(QStringLiteral("refType")).toString();
        const QJsonValue refScaleValue = analysisObj.value(QStringLiteral("refScale"));
        if (refScaleValue.isDouble())
            config.refScale = refScaleValue.toDouble(config.refScale);
        else if (refScaleValue.isString())
        {
            bool ok = false;
            const double converted = refScaleValue.toString().toDouble(&ok);
            if (ok)
                config.refScale = converted;
        }

        const CameraConfig cameraCfg = loadCameraConfig(id);
        config.frameSize = QSize(cameraCfg.width, cameraCfg.height);
        if (!config.frameSize.isValid())
            config.frameSize = QSize(1920, 1080);

        QRectF normalizedRoi;
        const QRect analysisPixelRoi = parsePixelRoi(analysisObj.value(QStringLiteral("roi")).toObject());
        if (analysisPixelRoi.isValid())
            normalizedRoi = toNormalizedRect(analysisPixelRoi, config.frameSize);

        if (!normalizedRoi.isValid())
        {
            const QJsonObject profileRoi = profile.value(QStringLiteral("roi")).toObject();
            if (!profileRoi.isEmpty())
            {
                const double nx = profileRoi.value(QStringLiteral("x")).toDouble(-1.0);
                const double ny = profileRoi.value(QStringLiteral("y")).toDouble(-1.0);
                const double nw = profileRoi.value(QStringLiteral("width")).toDouble(-1.0);
                const double nh = profileRoi.value(QStringLiteral("height")).toDouble(-1.0);
                normalizedRoi = clampNormalizedRect(QRectF(nx, ny, nw, nh));
            }
        }

        if (normalizedRoi.isValid())
        {
            const double pixelWidth = normalizedRoi.width() * static_cast<double>(std::max(1, config.frameSize.width()));
            const double pixelHeight = normalizedRoi.height() * static_cast<double>(std::max(1, config.frameSize.height()));
            constexpr double kCaptureSide = 512.0;
            constexpr double kTolerance = 32.0; // allow small deviations when users tweak ROI manually
            if (pixelWidth <= 0.0 || pixelHeight <= 0.0 ||
                pixelWidth > (kCaptureSide + kTolerance) || pixelHeight > (kCaptureSide + kTolerance))
            {
                normalizedRoi = QRectF();
            }
        }

        if (!normalizedRoi.isValid())
            normalizedRoi = defaultNormalizedRoi(config.frameSize);

        config.normalizedRoi = normalizedRoi;

        const QRect captureRect = captureRectFromNormalized(config.normalizedRoi, config.frameSize);
        config.roi = captureRect;

        const bool isEnabled = enabledCameras.contains(id);
        config.enabled = isEnabled;
        const double rawFps = analysisObj.value(QStringLiteral("fps")).toDouble(config.fps);
        config.fps = rawFps > 0.0 ? rawFps : 5.0;

        m_analysisConfigs.insert(id, config);

        if (config.enabled)
        {
            m_enabledAnalysisCameras.insert(id);
            if (!m_enabledAnalysisOrder.contains(id))
                m_enabledAnalysisOrder.append(id);
        }

        if (m_weldingPage)
        {
            m_weldingPage->setCameraAiEnabled(id, config.enabled);
            if (config.normalizedRoi.isValid())
            {
                const QRect overlayRect = toPixelRect(config.normalizedRoi, config.frameSize);
                m_weldingPage->setAnalysisOverlay(id, {}, overlayRect, config.frameSize);
            }
        }
    }

    ensureActiveAnalysisCamera();
    if (!m_activeAnalysisCameraId.isEmpty())
        applyAnalysisConfig(m_activeAnalysisCameraId);
    else
        updateAiTimerForActiveCamera();
    updateAiToggleLocking();
}

MainWindow::AnalysisConfig MainWindow::analysisConfigForCamera(const QString &cameraId) const
{
    const auto it = m_analysisConfigs.constFind(cameraId);
    if (it != m_analysisConfigs.constEnd())
        return it.value();
    return AnalysisConfig{};
}

void MainWindow::ensureActiveAnalysisCamera()
{
    if (m_enabledAnalysisCameras.isEmpty())
    {
        m_activeAnalysisCameraId.clear();
        return;
    }

    if (!m_enabledAnalysisCameras.contains(m_activeAnalysisCameraId))
    {
        m_activeAnalysisCameraId.clear();
        for (const QString &id : m_enabledAnalysisOrder)
        {
            if (m_enabledAnalysisCameras.contains(id))
            {
                m_activeAnalysisCameraId = id;
                break;
            }
        }
        if (m_activeAnalysisCameraId.isEmpty())
            m_activeAnalysisCameraId = *m_enabledAnalysisCameras.constBegin();
    }
}

QString MainWindow::cameraIdForStartup() const
{
    auto hasStreamKey = [this](const QString &id) -> bool {
        if (!m_analysisConfigs.contains(id))
            return false;
        return !m_analysisConfigs.value(id).streamKey.trimmed().isEmpty();
    };

    if (!m_activeAnalysisCameraId.isEmpty() && hasStreamKey(m_activeAnalysisCameraId))
        return m_activeAnalysisCameraId;

    for (const QString &id : m_enabledAnalysisOrder)
    {
        if (hasStreamKey(id))
            return id;
    }

    for (auto it = m_analysisConfigs.constBegin(); it != m_analysisConfigs.constEnd(); ++it)
    {
        if (!it.value().streamKey.trimmed().isEmpty())
            return it.key();
    }

    if (!m_activeAnalysisCameraId.isEmpty())
        return m_activeAnalysisCameraId;

    if (!m_enabledAnalysisCameras.isEmpty())
        return *m_enabledAnalysisCameras.constBegin();

    if (!m_analysisConfigs.isEmpty())
        return m_analysisConfigs.constBegin().key();

    return QString();
}

void MainWindow::applyAnalysisConfig(const QString &cameraId)
{
    if (!m_aiClient)
        return;

    if (!m_analysisConfigs.contains(cameraId))
    {
        qWarning() << "[MainWindow] Cannot apply analysis config; unknown camera" << cameraId;
        return;
    }

    const AnalysisConfig config = m_analysisConfigs.value(cameraId);
    if (!config.enabled)
        return;

    if (!config.url.isEmpty())
        m_aiClient->setBaseUrl(config.url);
    if (!config.streamKey.isEmpty())
        m_aiClient->setStreamKey(config.streamKey);
    m_activeAnalysisCameraId = cameraId;
    updateAiTimerForActiveCamera();
}

void MainWindow::updateAiTimerForActiveCamera()
{
    if (!m_aiTimer)
        return;

    if (m_activeAnalysisCameraId.isEmpty() || !m_analysisConfigs.contains(m_activeAnalysisCameraId))
        return;

    const AnalysisConfig config = m_analysisConfigs.value(m_activeAnalysisCameraId);
    const double fps = config.fps > 0.0 ? config.fps : 5.0;
    int intervalMs = static_cast<int>(std::round(1000.0 / fps));
    intervalMs = std::clamp(intervalMs, 100, 5000);
    if (intervalMs <= 0)
        intervalMs = 500;
    if (m_aiTimer->interval() != intervalMs)
        m_aiTimer->setInterval(intervalMs);
}

QVector<CameraPreviewWidget::AnalysisShape> MainWindow::parseAnalysisShapes(const QJsonObject &results,
                                                                           const AnalysisConfig &config,
                                                                           QRect *roiOut,
                                                                           QSize *frameSizeOut) const
{
    QVector<CameraPreviewWidget::AnalysisShape> shapes;

    QRect roi = config.roi;
    if (!roi.isValid() && config.normalizedRoi.isValid() && config.frameSize.isValid())
        roi = toPixelRect(config.normalizedRoi, config.frameSize);
    if (!roi.isValid())
        roi = captureRectFromNormalized(config.normalizedRoi, config.frameSize);

    QSize frameSize = config.frameSize;
    const int fw = results.value(QStringLiteral("frameWidth")).toInt(-1);
    const int fh = results.value(QStringLiteral("frameHeight")).toInt(-1);
    const QSize reportedSize = (fw > 0 && fh > 0) ? QSize(fw, fh) : QSize();
    constexpr int kCaptureSide = 512;
    constexpr int kTolerance = 4;

    auto isCaptureSize = [&](const QSize &size) -> bool {
        if (!size.isValid())
            return false;
        if (std::abs(size.width() - kCaptureSide) <= kTolerance &&
            std::abs(size.height() - kCaptureSide) <= kTolerance)
        {
            return true;
        }
        if (roi.isValid())
        {
            return std::abs(size.width() - roi.width()) <= kTolerance &&
                   std::abs(size.height() - roi.height()) <= kTolerance;
        }
        return false;
    };

    if (reportedSize.isValid() && !isCaptureSize(reportedSize))
    {
        frameSize = reportedSize;
    }
    else if (!frameSize.isValid())
    {
        frameSize = reportedSize.isValid() ? reportedSize : roi.size();
    }

    if (!roi.isValid())
        roi = QRect(QPoint(0, 0), QSize(kCaptureSide, kCaptureSide));

    if (frameSize.isValid() && frameSize.width() > 0 && frameSize.height() > 0 && roi.isValid())
    {
        roi = roi.intersected(QRect(QPoint(0, 0), frameSize));
        if (!roi.isValid())
            roi = config.roi.isValid() ? config.roi : QRect(QPoint(0, 0), QSize(kCaptureSide, kCaptureSide));
    }

    const QJsonObject frameAnalysis = results.value(QStringLiteral("frameAnalysis")).toObject();
    const QJsonArray objects = frameAnalysis.value(QStringLiteral("objects")).toArray();

    for (const QJsonValue &value : objects)
    {
        const QJsonObject obj = value.toObject();
        CameraPreviewWidget::AnalysisShape shape;
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
            for (QPointF &pt : points)
            {
                pt.setX(pt.x() * 512.0);
                pt.setY(pt.y() * 512.0);
            }
        }

        shape.pts512 = points;
        shapes.append(shape);
    }

    if (roiOut)
        *roiOut = roi;
    if (frameSizeOut)
        *frameSizeOut = frameSize;

    return shapes;
}

QJsonObject MainWindow::rectToJson(const QRect &rect)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("x"), rect.x());
    obj.insert(QStringLiteral("y"), rect.y());
    obj.insert(QStringLiteral("w"), rect.width());
    obj.insert(QStringLiteral("h"), rect.height());
    return obj;
}

void MainWindow::onFrameCaptured(const QString &cameraId, const QImage &frame)
{
    if (frame.isNull())
        return;

    m_latestFrames.insert(cameraId, frame);

    if ((m_activeAnalysisCameraId.isEmpty() || !m_enabledAnalysisCameras.contains(m_activeAnalysisCameraId))
        && m_enabledAnalysisCameras.contains(cameraId))
    {
        m_activeAnalysisCameraId = cameraId;
        applyAnalysisConfig(cameraId);
    }
}

void MainWindow::updateAiState()
{
    if (!m_aiClient)
        return;

    const bool enabled = m_aiClient->settings().enableAnalysis;
    if (!enabled)
    {
        stopAiSession();
        return;
    }

    if (!m_aiSessionActive)
        startAiSession();

    if (!m_aiSessionActive)
        return;

    if (m_enabledAnalysisCameras.isEmpty())
    {
        if (m_aiTimer && m_aiTimer->isActive())
            m_aiTimer->stop();
        m_analysisPending = false;
        return;
    }

    if (m_aiTimer && !m_aiTimer->isActive())
        m_aiTimer->start();
}

void MainWindow::handleCameraAiToggle(const QString &cameraId, bool enabled)
{
    if (cameraId.isEmpty())
        return;

    qInfo() << "[MainWindow] AI toggle" << (enabled ? "ON" : "OFF") << "for camera" << cameraId;

    if (enabled)
    {
        QString blockingCameraId;
        for (const QString &id : std::as_const(m_enabledAnalysisCameras))
        {
            if (id != cameraId)
            {
                blockingCameraId = id;
                break;
            }
        }

        if (!blockingCameraId.isEmpty())
        {
            QMessageBox::information(this, tr("AI Analysis"), aiExclusiveMessage());
            if (m_weldingPage)
                m_weldingPage->setCameraAiEnabled(cameraId, false);
            updateAiToggleLocking();
            return;
        }
    }

    QString url;
    QString streamKey;
    double fps = -1.0;

    if (m_settingsPage)
    {
        const QJsonObject existing = m_settingsPage->cameraAnalysisConfig(cameraId);
        url = existing.value(QStringLiteral("url")).toString();
        streamKey = existing.value(QStringLiteral("streamKey")).toString();
        fps = existing.value(QStringLiteral("fps")).toDouble(-1.0);
    }

    if (url.isEmpty() && m_aiClient)
        url = m_aiClient->baseUrl();
    if (streamKey.isEmpty() && m_aiClient)
        streamKey = m_aiClient->streamKey();
    if (fps <= 0.0)
        fps = 5.0;

    CameraManager::CameraInfo cameraInfo;
    if (m_cameraManager)
        cameraInfo = m_cameraManager->camera(cameraId);
    if (cameraInfo.id.isEmpty())
        cameraInfo.id = cameraId;

    QString aliasName = !cameraInfo.alias.trimmed().isEmpty()
                            ? cameraInfo.alias.trimmed()
                            : (!cameraInfo.name.trimmed().isEmpty()
                                   ? cameraInfo.name.trimmed()
                                   : (!cameraInfo.slotId.trimmed().isEmpty() ? cameraInfo.slotId.trimmed()
                                                                             : cameraId));
    for (QChar &ch : aliasName)
    {
        if (ch.isSpace())
            ch = QLatin1Char(' ');
    }

    streamKey = streamKeyFromAlias(aliasName, cameraId);

    if (m_settingsPage)
        m_settingsPage->setCameraAnalysisEnabled(cameraId, enabled, url, streamKey, fps);

    QJsonDocument doc = ConfigUtils::loadConfig();
    if (!doc.isObject())
        doc = QJsonDocument(QJsonObject());

    QJsonObject root = doc.object();
    QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    QJsonArray profilesArray = settingsObj.value(QStringLiteral("cameraProfiles")).toArray();

    const QString canonicalAlias = canonicalString(aliasName);
    const QString canonicalName = canonicalString(cameraInfo.name);
    const QString canonicalSlot = canonicalString(cameraInfo.slotId);

    const double resolvedFps = fps > 0.0 ? fps : 5.0;

    auto applyAnalysisPayload = [&](QJsonObject &analysisObj) {
        if (analysisObj.contains(QStringLiteral("enabled")))
            analysisObj.remove(QStringLiteral("enabled"));
        analysisObj.insert(QStringLiteral("fps"), resolvedFps);
        if (!url.isEmpty())
            analysisObj.insert(QStringLiteral("url"), url);
        else if (!analysisObj.contains(QStringLiteral("url")))
            analysisObj.insert(QStringLiteral("url"), QString());
        if (!streamKey.isEmpty())
            analysisObj.insert(QStringLiteral("streamKey"), streamKey);
        else if (!analysisObj.contains(QStringLiteral("streamKey")))
            analysisObj.insert(QStringLiteral("streamKey"), QString());
    };

    bool modified = false;
    for (int i = 0; i < profilesArray.size(); ++i)
    {
        QJsonObject profile = profilesArray.at(i).toObject();
        const QString profileId = profile.value(QStringLiteral("id")).toString();
        const QString profileAlias = profile.value(QStringLiteral("alias")).toString();
        const QString profileName = profile.value(QStringLiteral("name")).toString();
        const QString profileSlot = profile.value(QStringLiteral("slotId")).toString();

        const QString profileAliasKey = canonicalString(profileAlias);
        const QString profileNameKey = canonicalString(profileName);
        const QString profileSlotKey = canonicalString(profileSlot);

        bool matches = false;
        if (!profileId.isEmpty() && profileId == cameraId)
            matches = true;
        else if (!canonicalAlias.isEmpty() && !profileAliasKey.isEmpty() && profileAliasKey == canonicalAlias)
            matches = true;
        else if (!canonicalName.isEmpty() && !profileNameKey.isEmpty() && profileNameKey == canonicalName)
            matches = true;
        else if (!canonicalSlot.isEmpty() && !profileSlotKey.isEmpty() && profileSlotKey == canonicalSlot)
            matches = true;

        if (!matches)
            continue;

        qInfo() << "[MainWindow] Updating existing analysis profile" << profileId
                << "alias" << profileAlias << "name" << profileName << "slot" << profileSlot;

        if (profileId.isEmpty())
            profile.insert(QStringLiteral("id"), cameraId);
        if (profileAlias.isEmpty() && !aliasName.isEmpty())
            profile.insert(QStringLiteral("alias"), aliasName);
        if (!cameraInfo.slotId.trimmed().isEmpty())
            profile.insert(QStringLiteral("slotId"), cameraInfo.slotId);
        if (!cameraInfo.name.trimmed().isEmpty())
            profile.insert(QStringLiteral("name"), cameraInfo.name);

        QJsonObject analysisObj = profile.value(QStringLiteral("analysis")).toObject();
        applyAnalysisPayload(analysisObj);
        profile.insert(QStringLiteral("analysis"), analysisObj);
        profilesArray.replace(i, profile);
        modified = true;
        break;
    }

    if (!modified)
    {
        qInfo() << "[MainWindow] No existing profile matched. Appending new analysis profile for" << cameraId;
        QJsonObject profile;
        profile.insert(QStringLiteral("id"), cameraId);
        if (!aliasName.isEmpty())
            profile.insert(QStringLiteral("alias"), aliasName);
        if (!cameraInfo.slotId.trimmed().isEmpty())
            profile.insert(QStringLiteral("slotId"), cameraInfo.slotId);
        if (!cameraInfo.name.trimmed().isEmpty())
            profile.insert(QStringLiteral("name"), cameraInfo.name);
        QJsonObject analysisObj;
        applyAnalysisPayload(analysisObj);
        profile.insert(QStringLiteral("analysis"), analysisObj);
        profilesArray.append(profile);
        modified = true;
    }

    if (modified)
    {
        settingsObj.insert(QStringLiteral("cameraProfiles"), profilesArray);
        root.insert(QStringLiteral("settings"), settingsObj);
        doc.setObject(root);
        if (!ConfigUtils::saveConfig(doc))
            qWarning() << "Failed to persist analysis settings for camera" << cameraId;
        else
            qInfo() << "[MainWindow] Persisted analysis settings for" << cameraId << "enabled" << enabled
                    << "fps" << resolvedFps << "url" << url << "streamKey" << streamKey;
    }

    if (enabled)
        m_activeAnalysisCameraId = cameraId;
    else if (m_activeAnalysisCameraId == cameraId)
        m_activeAnalysisCameraId.clear();

    QSet<QString> nextEnabled = m_enabledAnalysisCameras;
    if (enabled)
        nextEnabled.insert(cameraId);
    else
        nextEnabled.remove(cameraId);

    loadAnalysisConfigs(nextEnabled);
    updateAiState();
}

void MainWindow::startAiSession()
{
    if (!m_aiClient)
        return;

    if (m_aiSessionActive)
        return;

    const QString startupCameraId = cameraIdForStartup();
    if (startupCameraId.isEmpty())
    {
        qWarning() << "[MainWindow] No camera configuration available for AI model startup.";
        return;
    }

    if (!m_analysisConfigs.contains(startupCameraId))
    {
        qWarning() << "[MainWindow] Cannot start AI model; no config for camera" << startupCameraId;
        return;
    }

    const AnalysisConfig config = m_analysisConfigs.value(startupCameraId);
    if (config.streamKey.trimmed().isEmpty())
    {
        qWarning() << "[MainWindow] Cannot start AI model; stream key missing for camera" << startupCameraId;
        return;
    }

    QStringList weldTypes = config.weldTypes;
    const QString passLevel = m_aiClient->settings().passLevel;
    const bool isSecondPass = passLevel.compare(QStringLiteral("Second"), Qt::CaseInsensitive) == 0;
    const QString effectivePass = isSecondPass ? QStringLiteral("Second") : QStringLiteral("First");
    if (weldTypes.isEmpty())
        weldTypes << effectivePass;
    else
        weldTypes.replace(0, effectivePass);

    m_aiStartupContext.cameraId = startupCameraId;
    m_aiStartupContext.baseUrl = config.url;
    m_aiStartupContext.streamKey = config.streamKey;
    m_aiStartupContext.weldTypes = weldTypes;
    m_aiStartupContext.refType = config.refType;
    m_aiStartupContext.refScale = config.refScale;

    m_aiStartupAttemptCount = 0;
    m_aiStartupWaitingForShutdown = false;
    m_aiSessionActive = true;
    m_aiReady = false;
    m_analysisPending = false;

    requestAiStartup();

    if (m_activeAnalysisCameraId.isEmpty())
        m_activeAnalysisCameraId = startupCameraId;

    if (!m_aiTimer)
    {
        m_aiTimer = new QTimer(this);
        connect(m_aiTimer, &QTimer::timeout, this, &MainWindow::onAiTimerTick);
    }

    if (m_enabledAnalysisCameras.isEmpty())
    {
        if (m_aiTimer->isActive())
            m_aiTimer->stop();
        return;
    }

    ensureActiveAnalysisCamera();
    updateAiTimerForActiveCamera();
    if (!m_aiTimer->isActive())
        m_aiTimer->start();
}

void MainWindow::stopAiSession(bool forceShutdown)
{
    if (!m_aiClient)
        return;

    if (m_settingsPage)
        m_settingsPage->setAnalysisBusy(false);

    if (m_aiTimer && m_aiTimer->isActive())
        m_aiTimer->stop();

    const bool shouldShutdown = m_aiSessionActive ||
                                (forceShutdown && m_aiClient->settings().enableAnalysis);

    resetAiStartupState();

    if (shouldShutdown)
        m_aiClient->modelShutdown();

    m_aiSessionActive = false;
    m_aiReady = false;
    m_analysisPending = false;
}

void MainWindow::requestAiStartup()
{
    if (!m_aiClient)
        return;

    if (m_aiStartupAttemptCount >= kAiStartupMaxAttempts)
    {
        qWarning() << "[MainWindow] Skipping AI model startup; maximum retry attempts reached.";
        if (m_settingsPage)
            m_settingsPage->setAnalysisBusy(false);
        return;
    }

    if (m_aiStartupContext.streamKey.trimmed().isEmpty())
    {
        qWarning() << "[MainWindow] Cannot start AI model; stream key is empty.";
        if (m_settingsPage)
            m_settingsPage->setAnalysisBusy(false);
        resetAiStartupState();
        m_aiSessionActive = false;
        return;
    }

    if (!m_aiStartupContext.baseUrl.isEmpty())
        m_aiClient->setBaseUrl(m_aiStartupContext.baseUrl);
    if (!m_aiStartupContext.streamKey.isEmpty())
        m_aiClient->setStreamKey(m_aiStartupContext.streamKey);

    ++m_aiStartupAttemptCount;
    qInfo() << "[MainWindow] Requesting AI model startup attempt" << m_aiStartupAttemptCount
            << "for camera" << m_aiStartupContext.cameraId;

    m_aiClient->modelStartup(m_aiStartupContext.weldTypes,
                             m_aiStartupContext.refType,
                             m_aiStartupContext.refScale);
    m_aiReady = false;
    m_analysisPending = false;

    if (m_settingsPage)
        m_settingsPage->setAnalysisBusy(true);
}

void MainWindow::handleAiModelStartupFinished(bool ok, const QString &message, const QJsonObject &modelInfo)
{
    Q_UNUSED(modelInfo);

    if (!m_aiSessionActive)
    {
        if (m_settingsPage)
            m_settingsPage->setAnalysisBusy(false);
        return;
    }

    m_aiReady = ok;
    if (ok)
    {
        m_analysisPending = false;
        m_aiStartupWaitingForShutdown = false;
        m_aiStartupAttemptCount = 0;
        if (m_settingsPage)
            m_settingsPage->setAnalysisBusy(false);
        return;
    }

    m_analysisPending = false;
    qWarning() << "[MainWindow] AI model startup attempt" << m_aiStartupAttemptCount << "failed:" << message;

    const bool stillEnabled = m_aiClient ? m_aiClient->settings().enableAnalysis : false;
    if (!stillEnabled)
    {
        resetAiStartupState();
        m_aiSessionActive = false;
        if (m_settingsPage)
            m_settingsPage->setAnalysisBusy(false);
        return;
    }

    if (m_aiStartupAttemptCount < kAiStartupMaxAttempts)
    {
        if (!m_aiStartupWaitingForShutdown)
        {
            m_aiStartupWaitingForShutdown = true;
            qInfo() << "[MainWindow] Requesting AI model shutdown before retry.";
            m_aiClient->modelShutdown();
        }
        return;
    }

    qWarning() << "[MainWindow] AI model startup failed after" << m_aiStartupAttemptCount << "attempts.";
    if (m_settingsPage)
        m_settingsPage->setAnalysisBusy(false);
    resetAiStartupState();
    m_aiSessionActive = false;
}

void MainWindow::handleAiModelShutdownFinished(bool ok, const QString &message)
{
    Q_UNUSED(ok);
    Q_UNUSED(message);

    m_aiReady = false;
    m_analysisPending = false;

    if (m_aiStartupWaitingForShutdown)
    {
        m_aiStartupWaitingForShutdown = false;

        const bool stillEnabled = m_aiClient ? m_aiClient->settings().enableAnalysis : false;
        if (!stillEnabled)
        {
            resetAiStartupState();
            m_aiSessionActive = false;
            if (m_settingsPage)
                m_settingsPage->setAnalysisBusy(false);
            return;
        }

        if (m_aiStartupAttemptCount < kAiStartupMaxAttempts)
        {
            requestAiStartup();
        }
        else
        {
            qWarning() << "[MainWindow] AI model startup retry skipped; maximum attempts already reached.";
            resetAiStartupState();
            m_aiSessionActive = false;
            if (m_settingsPage)
                m_settingsPage->setAnalysisBusy(false);
        }
        return;
    }

    if (!m_aiClient || !m_aiClient->settings().enableAnalysis)
    {
        if (m_settingsPage)
            m_settingsPage->setAnalysisBusy(false);
    }
}

void MainWindow::resetAiStartupState()
{
    m_aiStartupAttemptCount = 0;
    m_aiStartupWaitingForShutdown = false;
    m_aiStartupContext = {};
}

void MainWindow::onAiTimerTick()
{
    if (!m_aiClient)
        return;

    if (!m_aiClient->settings().enableAnalysis)
    {
        stopAiSession();
        return;
    }

    if (m_enabledAnalysisCameras.isEmpty())
    {
        m_analysisPending = false;
        return;
    }

    if (!m_aiSessionActive)
    {
        startAiSession();
        return;
    }

    if (!m_aiReady)
        return;

    if (m_activeAnalysisCameraId.isEmpty() || !m_enabledAnalysisCameras.contains(m_activeAnalysisCameraId))
    {
        ensureActiveAnalysisCamera();
        if (!m_activeAnalysisCameraId.isEmpty() && m_enabledAnalysisCameras.contains(m_activeAnalysisCameraId))
        {
            applyAnalysisConfig(m_activeAnalysisCameraId);
        }
        else
        {
            for (auto it = m_latestFrames.constBegin(); it != m_latestFrames.constEnd(); ++it)
            {
                if (m_enabledAnalysisCameras.contains(it.key()))
                {
                    m_activeAnalysisCameraId = it.key();
                    applyAnalysisConfig(m_activeAnalysisCameraId);
                    break;
                }
            }
        }
    }

    if (m_activeAnalysisCameraId.isEmpty())
        return;

    if (!m_analysisConfigs.contains(m_activeAnalysisCameraId))
        return;

    const AnalysisConfig config = m_analysisConfigs.value(m_activeAnalysisCameraId);
    if (!config.enabled)
        return;
    if (!m_latestFrames.contains(m_activeAnalysisCameraId))
        return;

    QImage frame = m_latestFrames.value(m_activeAnalysisCameraId);
    if (frame.isNull())
        return;

    if (frame.format() != QImage::Format_RGB32 && frame.format() != QImage::Format_ARGB32 && frame.format() != QImage::Format_RGB888)
        frame = frame.convertToFormat(QImage::Format_RGB32);

    constexpr int kCaptureSide = 512;
    const QSize captureSize(kCaptureSide, kCaptureSide);
    if (frame.width() < captureSize.width() || frame.height() < captureSize.height())
    {
        qWarning() << "[MainWindow] Frame too small for analysis capture."
                   << frame.size();
        return;
    }

    QRect captureRect = captureRectFromNormalized(config.normalizedRoi, frame.size());
    if (!captureRect.isValid())
    {
        captureRect = QRect(QPoint(0, 0), captureSize);
        captureRect = captureRect.intersected(QRect(QPoint(0, 0), frame.size()));
    }

    if (m_analysisConfigs.contains(m_activeAnalysisCameraId))
    {
        m_analysisConfigs[m_activeAnalysisCameraId].roi = captureRect;
        // Do not modify normalizedRoi here; keep user-defined ROI for display purposes.
    }

    QImage patch = frame.copy(captureRect);
    if (patch.size() != captureSize)
        patch = patch.scaled(captureSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QByteArray jpegData;
    QBuffer buffer(&jpegData);
    buffer.open(QIODevice::WriteOnly);
    patch.save(&buffer, "JPEG", 90);

    if (DebugConfig::isDebugLoggingEnabled())
    {
        qInfo().nospace() << "[MainWindow] Analysis capture camera=" << m_activeAnalysisCameraId
                          << " origin=(" << captureRect.x() << "," << captureRect.y() << ")"
                          << " size=" << captureRect.width() << "x" << captureRect.height()
                          << " jpegBytes=" << jpegData.size();
    }

    m_aiClient->sendFrame(jpegData);

    if (!m_analysisPending)
    {
        m_aiClient->analyzeData();
        m_analysisPending = true;
    }
}

void MainWindow::handleAnalysisFinished(bool ok, const QJsonObject &results, const QString &message)
{
    m_analysisPending = false;

    if (m_activeAnalysisCameraId.isEmpty())
        return;

    QString resultCameraId = results.value(QStringLiteral("cameraId")).toString();
    if (DebugConfig::isDebugLoggingEnabled())
    {
        QString payloadPreview = QString::fromUtf8(QJsonDocument(results).toJson(QJsonDocument::Compact));
        if (payloadPreview.size() > 1024)
        {
            payloadPreview.truncate(1024);
            payloadPreview.append(QStringLiteral("..."));
        }
        const QString targetCamera = resultCameraId.isEmpty() ? m_activeAnalysisCameraId : resultCameraId;
        qInfo().nospace() << "[MainWindow] Analysis finished ok=" << (ok ? "true" : "false")
                          << " camera=" << targetCamera
                          << " message=" << (message.isEmpty() ? QStringLiteral("-") : message)
                          << " payload=" << payloadPreview;
    }

    if (!ok)
    {
        qWarning() << "Analysis failed:" << message;
        return;
    }

    QString cameraId = resultCameraId;
    if (cameraId.isEmpty())
        cameraId = m_activeAnalysisCameraId;

    if (!m_analysisConfigs.contains(cameraId))
    {
        qWarning() << "[MainWindow] Dropping analysis results; no config for camera" << cameraId;
        return;
    }

    const AnalysisConfig config = m_analysisConfigs.value(cameraId);

    QRect roi;
    QSize frameSize;
    const QVector<CameraPreviewWidget::AnalysisShape> shapes = parseAnalysisShapes(results, config, &roi, &frameSize);

    if (!roi.isValid() && config.normalizedRoi.isValid())
        roi = toPixelRect(config.normalizedRoi, frameSize.isValid() ? frameSize : config.frameSize);
    if (!roi.isValid())
        roi = config.roi;
    if (!frameSize.isValid())
        frameSize = config.frameSize;

    if (DebugConfig::isDebugLoggingEnabled())
    {
        qInfo().nospace() << "[MainWindow] Analysis overlay camera=" << cameraId
                          << " shapes=" << shapes.size()
                          << " roiValid=" << (roi.isValid() ? "true" : "false")
                          << " frameSize=" << frameSize.width() << "x" << frameSize.height();
    }

    if (m_weldingPage)
        m_weldingPage->setAnalysisOverlay(cameraId, shapes, roi, frameSize);

    const QJsonObject metrics = results.value(QStringLiteral("metrics")).toObject();
    if (!metrics.isEmpty())
    {
        const double weldPoolWidth = metrics.value(QStringLiteral("weldPoolWidthMm")).toDouble(-1.0);
        if (weldPoolWidth >= 0.0 && m_weldingPage)
            m_weldingPage->updateAnalysisMetrics(cameraId, weldPoolWidth);
        if (DebugConfig::isDebugLoggingEnabled())
        {
            const QStringList metricKeys = metrics.keys();
            qInfo().nospace() << "[MainWindow] Analysis metrics camera=" << cameraId
                              << " weldPoolWidthMm=" << QString::number(weldPoolWidth, 'f', 2)
                              << " keys=" << (metricKeys.isEmpty() ? QStringLiteral("-") : metricKeys.join(QLatin1Char(',')));
        }
    }

    const QJsonObject plcControl = results.value(QStringLiteral("plcControl")).toObject();
    if (!plcControl.isEmpty() && m_weldingPage)
    {
        std::optional<double> deviationMm;
        std::optional<double> warningThresholdMm;
        double numericValue = 0.0;

        const QStringList deviationKeys = {
            QStringLiteral("deviation"),
            QStringLiteral("deviationMm"),
            QStringLiteral("offset"),
            QStringLiteral("offsetMm"),
            QStringLiteral("alignmentOffset"),
            QStringLiteral("alignmentOffsetMm")
        };
        for (const QString &key : deviationKeys)
        {
            if (!plcControl.contains(key))
                continue;
            if (extractNumeric(plcControl.value(key), numericValue))
            {
                deviationMm = numericValue;
                break;
            }
        }

        const QStringList thresholdKeys = {
            QStringLiteral("warningThreshold"),
            QStringLiteral("warningThresholdMm"),
            QStringLiteral("threshold"),
            QStringLiteral("thresholdMm")
        };
        for (const QString &key : thresholdKeys)
        {
            if (!plcControl.contains(key))
                continue;
            if (extractNumeric(plcControl.value(key), numericValue))
            {
                warningThresholdMm = numericValue;
                break;
            }
        }

        if (deviationMm.has_value() || warningThresholdMm.has_value())
            m_weldingPage->applyPlcControlData(cameraId, deviationMm, warningThresholdMm);
    }
    if (DebugConfig::isDebugLoggingEnabled() && !plcControl.isEmpty())
    {
        const QStringList plcKeys = plcControl.keys();
        qInfo().nospace() << "[MainWindow] PLC directives camera=" << cameraId
                          << " keys=" << (plcKeys.isEmpty() ? QStringLiteral("-") : plcKeys.join(QLatin1Char(',')));
    }

    if (m_recordingManager)
    {
        QJsonObject payload = results;
        if (!payload.contains(QStringLiteral("cameraId")))
            payload.insert(QStringLiteral("cameraId"), cameraId);
        if (!payload.contains(QStringLiteral("roi")) && roi.isValid())
            payload.insert(QStringLiteral("roi"), rectToJson(roi));
        if (!payload.contains(QStringLiteral("frameSize")))
        {
            QJsonObject frameObj;
            frameObj.insert(QStringLiteral("width"), frameSize.width());
            frameObj.insert(QStringLiteral("height"), frameSize.height());
            payload.insert(QStringLiteral("frameSize"), frameObj);
        }
        m_recordingManager->appendAnalysisResult(cameraId, payload);
    }

    m_activeAnalysisCameraId = cameraId;
}
