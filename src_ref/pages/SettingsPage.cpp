// Author: SeungJae Lee
// SettingsPage: central configuration hub for cameras, AI analysis, PLC, and localization.

#include <QListView>
#include <QAbstractItemView>
#include <QPushButton>
#include <QFrame>
#include <QComboBox>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QDebug>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QLabel>
#include <QIcon>
#include <QCamera>
#include <QLineEdit>
#include <QDoubleValidator>

#include <QMessageBox>
#include <QPainter>
#include <QPixmap>

#include <QHideEvent>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QScrollBar>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QFont>
#include <QStyle>
#include <QStringList>
#include <QVBoxLayout>
#include <QTimer>
#include <QPointer>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <QStyle>

#include <algorithm>
#include <functional>
#include <cmath>
#include <utility>
#include <QtMath>

#include "SettingsPage.h"
#include "managers/AiClient.h"
#include "managers/LocalizationManager.h"
#include "managers/PLCClient.h"
#include "managers/CameraManager.h"

#include "utils/ConfigUtils.h"
#include "utils/StringUtils.h"
#include "widgets/BusyIndicator.h"
#include "widgets/CameraRoiWidget.h"
#include "widgets/CameraPreviewWidget.h"
#include "widgets/ToggleSwitch.h"

namespace {
// Utility helpers and protocol constants for serial PLC/camera configuration.
QString rgbaString(const QColor &color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 2));
}

using StringUtils::canonicalString;

constexpr quint8 STX = 0x02;
constexpr quint8 ETX = 0x03;
constexpr quint8 CMD_READ = 0x20;
constexpr quint8 CMD_WRITE = 0x10;
constexpr int RESPONSE_SIZE = 9;
constexpr int SERIAL_TIMEOUT = 3000;
constexpr int SERIAL_OPEN_RETRIES = 10;
constexpr int SERIAL_OPEN_DELAY_MS = 100;
constexpr int BAUD_RATE = 115200;
constexpr int SERIAL_RETRY_DELAY_MS = 200;
constexpr int SLOT_LABEL_WIDTH_PX = 140;
constexpr const char *kCameraIconButtonHoverStyle =
    "QPushButton {"
    " background: transparent;"
    " border: none;"
    " border-radius: 12px;"
    " min-width: 44px;"
    " max-width: 44px;"
    " min-height: 44px;"
    " max-height: 44px;"
    "}"
    "QPushButton:hover { background: rgba(34, 255, 162, 0.12); }"
    "QPushButton:pressed { background: rgba(34, 255, 162, 0.18); }";

constexpr quint16 ADDR_WLD_SHAD = 0x8011;
constexpr quint16 ADDR_WLD_SENS = 0x8012;
constexpr quint16 ADDR_WLD_FCSA_MIRR_FLIP = 0x8018;
constexpr quint16 ADDR_WLD_SAVE = 0x801F;

bool isAllowedSerialPort(const QSerialPortInfo &port)
{
#if defined(Q_OS_MACOS)
    return port.systemLocation().startsWith(QStringLiteral("/dev/cu.usbserial"));
#elif defined(Q_OS_LINUX)
    const QString name = port.portName();
    if (name.startsWith(QStringLiteral("ttyUSB")))
        return true;
    return port.systemLocation().startsWith(QStringLiteral("/dev/ttyUSB"));
#else
    Q_UNUSED(port);
    return true;
#endif
}

void applyLevelButtonStyles(const QVector<QPushButton *> &buttons, int activeValue)
{
    const int clampedValue = std::clamp(activeValue, 1, 10);
    for (int i = 0; i < buttons.size(); ++i)
    {
        QPushButton *button = buttons.at(i);
        if (!button)
            continue;
        const bool selected = (i + 1) <= clampedValue;
        const QString textColor = selected ? QStringLiteral("#111113") : QStringLiteral("#777B84");
        const QString backgroundColor = selected ? QStringLiteral("#00FFB7") : QStringLiteral("#2E3135");
        button->setStyleSheet(QStringLiteral(
                                  "QPushButton { color: %1; background-color: %2; border: none; border-radius: 8px; font-weight: 600; }"
                                  "QPushButton:disabled { color: rgba(119, 123, 132, 0.4); background-color: rgba(46, 49, 53, 0.4); }")
                                  .arg(textColor, backgroundColor));
    }
}

static SerialCamState readSerialOptionsBlocking(const QString &portName)
{
    SerialCamState st;
    QSerialPort port;
    const QString base = QFileInfo(portName).fileName();
    QSerialPortInfo chosenInfo;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const auto &inf : infos)
    {
        if (inf.systemLocation() == portName || inf.portName() == base)
        {
            chosenInfo = inf;
            break;
        }
    }
    if (chosenInfo.isNull())
        port.setPortName(portName.startsWith(QStringLiteral("/dev/")) ? base : portName);
    else
        port.setPort(chosenInfo);
    port.setBaudRate(BAUD_RATE);
    port.setDataBits(QSerialPort::Data8);
    port.setParity(QSerialPort::NoParity);
    port.setStopBits(QSerialPort::OneStop);
    for (int i = 0; i < SERIAL_OPEN_RETRIES; ++i)
    {
        if (port.open(QIODevice::ReadWrite))
            break;
        QThread::msleep(SERIAL_OPEN_DELAY_MS);
    }
    if (!port.isOpen())
    {
        st.error = QStringLiteral("Failed to open serial port: %1").arg(port.errorString());
        return st;
    }

    auto readValue = [&](quint16 addr, quint32 &out) -> bool {
        QByteArray req(5, 0);
        req[0] = STX;
        req[1] = CMD_READ;
        req[2] = (addr >> 8) & 0xFF;
        req[3] = addr & 0xFF;
        req[4] = ETX;
        port.readAll();
        if (port.write(req) != req.size())
        {
            st.error = QStringLiteral("Failed to write read command.");
            return false;
        }
        if (!port.waitForBytesWritten(SERIAL_TIMEOUT))
        {
            st.error = QStringLiteral("Timed out sending read command.");
            return false;
        }
        QByteArray resp;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < SERIAL_TIMEOUT)
        {
            if (port.waitForReadyRead(100))
            {
                resp += port.readAll();
                if (resp.size() >= RESPONSE_SIZE)
                    break;
            }
        }
        if (resp.size() < RESPONSE_SIZE)
        {
            st.error = QStringLiteral("No response from camera.");
            return false;
        }
        if (static_cast<quint8>(resp.at(0)) != STX || static_cast<quint8>(resp.at(8)) != ETX)
        {
            st.error = QStringLiteral("Invalid response header/footer.");
            return false;
        }
        out = (static_cast<quint8>(resp[4]) << 24)
            | (static_cast<quint8>(resp[5]) << 16)
            | (static_cast<quint8>(resp[6]) << 8)
            | static_cast<quint8>(resp[7]);
        return true;
    };

    quint32 data = 0;
    bool any = false;
    if (readValue(ADDR_WLD_SHAD, data))
    {
        const int v2 = (data >> 8) & 0xFF;
        const int vn = (data >> 16) & 0x0F;
        st.brightness = v2 ? v2 : vn;
        any = true;
    }
    if (readValue(ADDR_WLD_SENS, data))
    {
        const int v2 = (data >> 8) & 0xFF;
        const int vn = (data >> 16) & 0x0F;
        st.sensitivity = v2 ? v2 : vn;
        any = true;
    }
    if (readValue(ADDR_WLD_FCSA_MIRR_FLIP, data))
    {
        st.focusAssist = ((data >> 16) & 0xFF) == 1;
        st.mirror = ((data >> 8) & 0xFF) == 1;
        st.flip = (data & 0x01) == 1;
        any = true;
    }

    port.close();
    if (!any && st.error.isEmpty())
        st.error = QStringLiteral("Camera did not return any values.");
    st.ok = any;
    return st;
}

static bool writeSerialOptionsBlocking(const QString &portName,
                                       int brightness,
                                       int sensitivity,
                                       bool focusAssist,
                                       bool mirror,
                                       bool flip)
{
    QSerialPort port;
    const QString base = QFileInfo(portName).fileName();
    QSerialPortInfo chosenInfo;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const auto &inf : infos)
    {
        if (inf.systemLocation() == portName || inf.portName() == base)
        {
            chosenInfo = inf;
            break;
        }
    }
    if (chosenInfo.isNull())
        port.setPortName(portName.startsWith(QStringLiteral("/dev/")) ? base : portName);
    else
        port.setPort(chosenInfo);
    port.setBaudRate(BAUD_RATE);
    port.setDataBits(QSerialPort::Data8);
    port.setParity(QSerialPort::NoParity);
    port.setStopBits(QSerialPort::OneStop);
    for (int i = 0; i < SERIAL_OPEN_RETRIES; ++i)
    {
        if (port.open(QIODevice::ReadWrite))
            break;
        QThread::msleep(SERIAL_OPEN_DELAY_MS);
    }
    if (!port.isOpen())
        return false;

    auto writeValue = [&](quint16 addr, quint32 data) -> bool {
        QByteArray req(9, 0);
        req[0] = STX;
        req[1] = CMD_WRITE;
        req[2] = (addr >> 8) & 0xFF;
        req[3] = addr & 0xFF;
        req[4] = (data >> 24) & 0xFF;
        req[5] = (data >> 16) & 0xFF;
        req[6] = (data >> 8) & 0xFF;
        req[7] = data & 0xFF;
        req[8] = ETX;
        if (port.write(req) != req.size())
            return false;
        return port.waitForBytesWritten(SERIAL_TIMEOUT);
    };

    auto makeCamVal = [](int header, int value) -> quint32 {
        return (quint32(header) << 24)
            | (quint32(value & 0x0F) << 16)
            | (quint32(value) << 8);
    };
    auto makeFocusMirrorFlipVal = [&](int focus, int m, int f) -> quint32 {
        return (quint32(focus & 0x01) << 16)
            | (quint32(m & 0x01) << 8)
            | quint32(f & 0x01);
    };

    if (brightness >= 1 && brightness <= 10)
    {
        if (!writeValue(ADDR_WLD_SHAD, makeCamVal(0x34, brightness)))
        {
            port.close();
            return false;
        }
        QThread::msleep(SERIAL_RETRY_DELAY_MS);
    }
    if (sensitivity >= 1 && sensitivity <= 10)
    {
        if (!writeValue(ADDR_WLD_SENS, makeCamVal(0x40, sensitivity)))
        {
            port.close();
            return false;
        }
        QThread::msleep(SERIAL_RETRY_DELAY_MS);
    }
    if (!writeValue(ADDR_WLD_FCSA_MIRR_FLIP, makeFocusMirrorFlipVal(focusAssist ? 1 : 0, mirror ? 1 : 0, flip ? 1 : 0)))
    {
        qWarning() << "[CameraDetail] Failed to write focusAssist/mirror/flip value.";
        port.close();
        return false;
    }
    QThread::msleep(SERIAL_RETRY_DELAY_MS);
    port.flush();
    if (!writeValue(ADDR_WLD_SAVE, 0x00000001))
    {
        qWarning() << "[CameraDetail] Failed to issue SAVE command.";
        port.close();
        return false;
    }
    QThread::msleep(SERIAL_RETRY_DELAY_MS * 2);
    port.close();
    qInfo() << "[CameraDetail] Serial write success (raw) brightness=" << brightness
            << "sensitivity=" << sensitivity << "focusAssist=" << focusAssist
            << "mirror=" << mirror << "flip=" << flip;
    return true;
}

static bool verifySerialOptionsBlocking(const QString &portName,
                                        int targetBrightness,
                                        int targetSensitivity,
                                        bool targetFocusAssist,
                                        bool targetMirror,
                                        bool targetFlip,
                                        int timeoutMs = 3000,
                                        int intervalMs = 20)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs)
    {
        SerialCamState st = readSerialOptionsBlocking(portName);
        if (st.ok)
        {
            const bool bOk = (targetBrightness <= 0) ? true : (st.brightness == targetBrightness);
            const bool sOk = (targetSensitivity <= 0) ? true : (st.sensitivity == targetSensitivity);
            const bool faOk = (st.focusAssist == targetFocusAssist);
            const bool mOk = (st.mirror == targetMirror);
            const bool fOk = (st.flip == targetFlip);
            if (bOk && sOk && faOk && mOk && fOk)
            {
                qInfo() << "[CameraDetail] verify success port=" << portName
                        << "brightness=" << st.brightness << "sensitivity=" << st.sensitivity
                        << "focusAssist=" << st.focusAssist
                        << "mirror=" << st.mirror << "flip=" << st.flip;
                return true;
            }
            qInfo() << "[CameraDetail] verify mismatch port=" << portName
                    << "read brightness=" << st.brightness << "sensitivity=" << st.sensitivity
                    << "focusAssist=" << st.focusAssist
                    << "mirror=" << st.mirror << "flip=" << st.flip
                    << "target brightness=" << targetBrightness << "sensitivity=" << targetSensitivity
                    << "focusAssist=" << targetFocusAssist
                    << "mirror=" << targetMirror << "flip=" << targetFlip;
        }
        QThread::msleep(intervalMs);
    }
    qWarning() << "[CameraDetail] verify timed out port=" << portName;
    return false;
}
} // namespace

SettingsPage::SettingsPage(CameraManager *cameraManager, AiClient *aiClient, PLCClient *plcClient,
                           LocalizationManager *localizationManager, QWidget *parent)
    : QWidget(parent)
    , m_cameraManager(cameraManager)
    , m_aiClient(aiClient)
    , m_plcClient(plcClient)
    , m_localizationManager(localizationManager)
{
    // Bootstrap managers, pre-load persisted settings, and wire async watchers.
    if (m_localizationManager)
        m_localizationManager->refreshLanguages();

    buildUi();
    retranslateUi();
    loadPlcSettings();
    loadCameraSettings();
    populateCameraList();

    connect(&m_serialReadWatcher, &QFutureWatcher<SerialCamState>::finished, this, &SettingsPage::handleCameraDetailSerialReadFinished);
    connect(&m_serialWriteWatcher, &QFutureWatcher<SerialWriteResult>::finished, this, &SettingsPage::handleCameraDetailSerialWriteFinished);

    if (m_cameraManager)
    {
        connect(m_cameraManager, &CameraManager::cameraAdded, this, [this](const CameraManager::CameraInfo &info) {
            applyStoredCameraVisibility(info);
            populateCameraList();
        });
        connect(m_cameraManager, &CameraManager::cameraRemoved, this, [this](const QString &) {
            populateCameraList();
        });
        connect(m_cameraManager, &CameraManager::cameraUpdated, this, [this](const CameraManager::CameraInfo &) {
            populateCameraList();
        });
        connect(m_cameraManager, &CameraManager::cameraVisibilityChanged, this, [this](const QString &, bool) {
            populateCameraList();
        });
        connect(m_cameraManager, &CameraManager::cameraHandleAssigned, this,
                [this](const QString &cameraId, QCamera *camera) {
                    if (m_cameraDetailVisible && m_detailState.cameraId == cameraId)
                        attachCameraToDetailPreview(camera);
                });
        connect(m_cameraManager, &CameraManager::cameraUpdated, this,
                [this](const CameraManager::CameraInfo &info) {
                    if (m_cameraDetailVisible && info.id == m_detailState.cameraId)
                    {
                        m_detailState.displayName = displayNameForCamera(info);
                        if (!info.slotId.isEmpty())
                            m_detailState.slotLabel = info.slotId;
                        updateCameraDetailTexts();
                        if (m_cameraDetailPreview)
                            m_cameraDetailPreview->updateInfo(info);
                    }
                });
    }

    if (m_aiClient)
    {
        loadAnalysisParametersFromConfig();
        const auto settings = m_aiClient->settings();
        if (m_aiEnabledCheck)
        {
            QSignalBlocker block(m_aiEnabledCheck);
            m_aiEnabledCheck->setChecked(settings.enableAnalysis);
        }
        if (m_aiModelEdit)
        {
            QSignalBlocker block(m_aiModelEdit);
            m_aiModelEdit->setText(settings.modelName);
        }
        if (m_allowableEdit)
        {
            QSignalBlocker block(m_allowableEdit);
            m_allowableEdit->setText(QString::number(settings.confidenceThreshold, 'f', 2));
        }
        if (m_passNumberSpin)
        {
            QSignalBlocker block(m_passNumberSpin);
            m_passNumberSpin->setValue(settings.passNumber);
        }
        if (m_passTypeCombo)
        {
            QSignalBlocker block(m_passTypeCombo);
            const QString level = settings.passLevel.isEmpty() ? QStringLiteral("Root") : settings.passLevel;
            int index = m_passTypeCombo->findData(level, Qt::UserRole, Qt::MatchFixedString);
            if (index >= 0)
                m_passTypeCombo->setCurrentIndex(index);
        }
        if (m_torchLengthEdit)
        {
            QSignalBlocker block(m_torchLengthEdit);
            m_torchLengthEdit->setText(QString::number(settings.torchLengthMm, 'f', 1));
        }

        persistAnalysisParameters(settings);
        updatePassUsageLabel();

        connect(m_aiClient, &AiClient::settingsChanged, this, [this](const AiClient::Settings &settings) {
            if (m_aiEnabledCheck)
            {
                QSignalBlocker block(m_aiEnabledCheck);
                m_aiEnabledCheck->setChecked(settings.enableAnalysis);
            }
            if (m_aiModelEdit)
            {
                QSignalBlocker block(m_aiModelEdit);
                m_aiModelEdit->setText(settings.modelName);
            }
            if (m_allowableEdit)
            {
                QSignalBlocker block(m_allowableEdit);
                m_allowableEdit->setText(QString::number(settings.confidenceThreshold, 'f', 2));
            }
            if (m_passNumberSpin)
            {
                QSignalBlocker block(m_passNumberSpin);
                m_passNumberSpin->setValue(settings.passNumber);
            }
            if (m_passTypeCombo)
            {
                QSignalBlocker block(m_passTypeCombo);
                const QString level = settings.passLevel.isEmpty() ? QStringLiteral("Root") : settings.passLevel;
                int index = m_passTypeCombo->findData(level, Qt::UserRole, Qt::MatchFixedString);
                if (index >= 0)
                    m_passTypeCombo->setCurrentIndex(index);
            }
            if (m_torchLengthEdit)
            {
                QSignalBlocker block(m_torchLengthEdit);
                m_torchLengthEdit->setText(QString::number(settings.torchLengthMm, 'f', 1));
            }
            persistAnalysisParameters(settings);
            updatePassUsageLabel();
            updateAiStatusButton();
        });
    }

    if (m_aiEnabledCheck)
    {
        connect(m_aiEnabledCheck, &QCheckBox::toggled, this, [this](bool) {
            updateAiStatusButton();
            applyAiSettings();
        });
    }
    if (m_allowableEdit)
        connect(m_allowableEdit, &QLineEdit::editingFinished, this, &SettingsPage::applyAiSettings);
    if (m_passTypeCombo)
        connect(m_passTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            updatePassUsageLabel();
            applyAiSettings();
        });
    if (m_torchLengthEdit)
        connect(m_torchLengthEdit, &QLineEdit::editingFinished, this, &SettingsPage::applyAiSettings);

    if (m_plcClient)
    {
        connect(m_plcClient, &PLCClient::connectionStateChanged, this,
                [this](const QString &id, PLCClient::State state, const QString &message) {
                    handlePlcConnectionStateChanged(id, state, message);
                });
    }

    if (m_languageCombo)
        connect(m_languageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsPage::handleLanguageSelectionChanged);

    if (m_languageRefreshButton)
        connect(m_languageRefreshButton, &QPushButton::clicked, this, [this]() {
            if (m_localizationManager)
            {
                QStringList paths = m_localizationManager->searchPaths();
                const QString resourcePath = QStringLiteral(":/resources/languages");
                if (!paths.contains(resourcePath))
                    paths.prepend(resourcePath);
                m_localizationManager->setSearchPaths(paths);
                m_localizationManager->refreshLanguages();
            }
            populateLanguageList();
        });

    if (m_localizationManager)
    {
        connect(m_localizationManager, &LocalizationManager::languagesChanged, this, &SettingsPage::populateLanguageList);
        connect(m_localizationManager, &LocalizationManager::languageChanged, this, [this](const QString &code) {
            persistLanguagePreference(code);

            if (!m_languageCombo)
                return;
            const int index = m_languageCombo->findData(code);
            if (index >= 0 && index != m_languageCombo->currentIndex())
                m_languageCombo->setCurrentIndex(index);
        });
        populateLanguageList();
    }
    else
    {
        populateLanguageList();
    }

    updatePassUsageLabel();
    updateAiStatusButton();

    if (m_languageStatusButton)
    {
        const QColor color = cameraConnectivityColor(true);
        const QString colorText = rgbaString(color);
        m_languageStatusButton->setText(QStringLiteral("✓"));
        m_languageStatusButton->setStyleSheet(QStringLiteral(
            "QPushButton { color: %1; font-size: 20px; font-weight: 700; border: none; background: transparent; }")
                                                  .arg(colorText));
        m_languageStatusButton->setEnabled(false);
    }

    if (m_languageNameLabel)
    {
        const QColor color = cameraConnectivityColor(true);
        m_languageNameLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 16px; font-weight: 600;")
                                               .arg(rgbaString(color)));
    }

}

SettingsPage::~SettingsPage()
{
    if (m_cameraRoiDirty)
        saveCameraSettings();
}

void SettingsPage::setCameraAnalysisEnabled(const QString &cameraId, bool enabled,
                                            const QString &url,
                                            const QString &streamKey,
                                            double fps)
{
    if (cameraId.isEmpty())
        return;

    CameraManager::CameraInfo info = m_cameraManager ? m_cameraManager->camera(cameraId) : CameraManager::CameraInfo();
    if (info.id.isEmpty())
        info.id = cameraId;

    int profileIndex = findCameraProfile(info);
    if (profileIndex < 0)
    {
        for (int i = 0; i < m_cameraRoiProfiles.size(); ++i)
        {
            if (m_cameraRoiProfiles.at(i).id == cameraId)
            {
                profileIndex = i;
                break;
            }
        }
    }

    bool appendedProfile = false;
    if (profileIndex < 0 || profileIndex >= m_cameraRoiProfiles.size())
    {
        CameraRoiProfile profile;
        profile.id = cameraId;
        profile.slotId = info.slotId;
        profile.name = info.name;

        QString aliasSeed = info.alias;
        if (aliasSeed.trimmed().isEmpty())
        {
            if (!info.name.trimmed().isEmpty())
                aliasSeed = info.name;
            else if (!info.slotId.trimmed().isEmpty())
                aliasSeed = info.slotId;
            else
                aliasSeed = cameraId;
        }
        profile.alias = uniqueAlias(aliasSeed);
        profile.roi = QRectF();
        profile.analysis = QJsonObject();
        profile.enabled = info.visible;
        m_cameraRoiProfiles.append(profile);
        profileIndex = m_cameraRoiProfiles.size() - 1;
        appendedProfile = true;
        qInfo() << "[SettingsPage] Added new camera profile for analysis" << cameraId
                << "alias" << profile.alias << "slot" << profile.slotId;
    }

    CameraRoiProfile &profile = m_cameraRoiProfiles[profileIndex];
    QJsonObject analysis = profile.analysis;

    bool modified = appendedProfile;

    if (analysis.contains(QStringLiteral("enabled")))
    {
        analysis.remove(QStringLiteral("enabled"));
        modified = true;
    }

    if (!url.isNull())
    {
        const QString trimmedUrl = url.trimmed();
        if (!trimmedUrl.isEmpty() || analysis.contains(QStringLiteral("url")))
        {
            if (analysis.value(QStringLiteral("url")).toString() != trimmedUrl)
            {
                analysis.insert(QStringLiteral("url"), trimmedUrl);
                modified = true;
            }
        }
    }
    else if (!analysis.contains(QStringLiteral("url")))
    {
        analysis.insert(QStringLiteral("url"), QString());
        modified = true;
    }

    if (!streamKey.isNull())
    {
        const QString trimmedKey = streamKey.trimmed();
        if (!trimmedKey.isEmpty() || analysis.contains(QStringLiteral("streamKey")))
        {
            if (analysis.value(QStringLiteral("streamKey")).toString() != trimmedKey)
            {
                analysis.insert(QStringLiteral("streamKey"), trimmedKey);
                modified = true;
            }
        }
    }
    else if (!analysis.contains(QStringLiteral("streamKey")))
    {
        analysis.insert(QStringLiteral("streamKey"), QString());
        modified = true;
    }

    const double targetFps = fps > 0.0 ? fps : analysis.value(QStringLiteral("fps")).toDouble(-1.0);
    const double resolvedFps = targetFps > 0.0 ? targetFps : 5.0;
    const double currentFps = analysis.value(QStringLiteral("fps")).toDouble(-1.0);
    if (!analysis.contains(QStringLiteral("fps")) || !qFuzzyCompare(currentFps + 1.0, resolvedFps + 1.0))
    {
        analysis.insert(QStringLiteral("fps"), resolvedFps);
        modified = true;
    }

    if (!modified)
        return;

    profile.analysis = analysis;
    m_cameraRoiDirty = true;
    rebuildCameraRoiAliasIndex();
    saveCameraSettings();
    qInfo() << "[SettingsPage] Updated analysis payload for" << cameraId
            << "enabled" << enabled << "url" << analysis.value(QStringLiteral("url")).toString()
            << "streamKey" << analysis.value(QStringLiteral("streamKey")).toString()
            << "fps" << analysis.value(QStringLiteral("fps")).toDouble();
}

QJsonObject SettingsPage::cameraAnalysisConfig(const QString &cameraId) const
{
    if (cameraId.isEmpty())
        return QJsonObject();

    CameraManager::CameraInfo info = m_cameraManager ? m_cameraManager->camera(cameraId) : CameraManager::CameraInfo();
    if (info.id.isEmpty())
        info.id = cameraId;

    int profileIndex = findCameraProfile(info);
    if (profileIndex < 0)
    {
        for (int i = 0; i < m_cameraRoiProfiles.size(); ++i)
        {
            if (m_cameraRoiProfiles.at(i).id == cameraId)
            {
                profileIndex = i;
                break;
            }
        }
    }
    if (profileIndex < 0 || profileIndex >= m_cameraRoiProfiles.size())
        return QJsonObject();

    return m_cameraRoiProfiles.at(profileIndex).analysis;
}

void SettingsPage::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        QWidget::changeEvent(event);
        retranslateUi();
        return;
    }

    QWidget::changeEvent(event);
}

void SettingsPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);

    if (m_cameraDetailVisible)
        hideCameraDetail();

    if (!m_activeRoiCameraId.isEmpty())
        hideRoiEditor();
}

void SettingsPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_busyOverlay)
        m_busyOverlay->setGeometry(rect());
    if (m_roiOverlay)
        m_roiOverlay->setGeometry(rect());
    if (m_cameraDetailOverlay)
        m_cameraDetailOverlay->setGeometry(rect());
    if (m_cameraDetailVisible)
        updateCameraDetailPreviewGeometry();
    updateCameraDetailBusyOverlayGeometry();
}

void SettingsPage::buildUi()
{
    // Assemble the multi-section settings layout with global styling.
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(R"(
SettingsPage {
    background-color: #111113;
    border-radius: 12px;
    border: 1px solid #212225;
}
QWidget[settingsItem="true"] {
    background-color: #111113;
    border-radius: 12px;
}
QLabel[role="pageTitle"] {
    color: rgba(255, 255, 255, 0.4);
    font-size: 18px;
    font-weight: 600;
    background: transparent;
    border-radius: 5px;
}
QLabel[role="sectionTitle"] {
    color: #FFFFFF;
    font-size: 24px;
    font-weight: 700;
    background: transparent;
    border-radius: 5px;
}
QLabel[role="sectionHint"] {
    color: rgba(255, 255, 255, 0.65);
    font-size: 14px;
    font-weight: 500;
    background: transparent;
    border-radius: 4px;
}
QLabel[role="fieldLabel"] {
    color: rgba(255, 255, 255, 0.55);
    font-size: 12px;
    font-weight: 500;
    background: transparent;
    border-radius: 5px;
}
QLabel[role="inlineLabel"] {
    color: rgba(255, 255, 255, 0.70);
    font-size: 16px;
    font-weight: 600;
    background: transparent;
    border-radius: 5px;
}
QFrame[role="divider"] {
    background-color: #2C2D30;
    min-height: 1px;
    max-height: 1px;
    border: none;
}
QWidget[role="inputFrame"] {
    background-color: #1A1B1D;
    border: 1px solid rgba(217, 217, 237, 0.9);
    border-radius: 5px;
}
QWidget[role="inputFrame"][hover-active="true"] {
    background-color: rgba(34, 255, 162, 0.12);
    border-color: rgba(34, 255, 162, 0.35);
}
QLineEdit[role="settingsEdit"],
QSpinBox[role="settingsSpin"],
QDoubleSpinBox[role="settingsSpin"] {
    background-color: transparent;
    border: none;
    font-size: 16px;
    font-weight: 600;
    border-radius: 5px;
    color: #FFFFFF;
    min-height: 36px;
    padding: 0 12px;
}
QLineEdit[role="settingsEdit"][inputError="true"] {
    border-color: #FF5A5F;
}
QComboBox[role="settingsCombo"]::drop-down {
    border: none;
    width: 24px;
}
QSpinBox[role="settingsSpin"]::up-button,
QSpinBox[role="settingsSpin"]::down-button,
QDoubleSpinBox[role="settingsSpin"]::up-button,
QDoubleSpinBox[role="settingsSpin"]::down-button {
    background: transparent;
    border: none;
}
QWidget[role="detailItem"] {
    background-color: #212225;
    border-radius: 12px;
    border: none;
}
QLabel[role="detailHint"] {
    color: rgba(255, 255, 255, 0.65);
    font-size: 14px;
    line-height: 1.5;
    background: transparent;
}
QPushButton[role="iconButton"] {
    background-color: #1A1B1D;
    border: none;
    border-radius: 12px;
    min-width: 44px;
    max-width: 44px;
    min-height: 44px;
    max-height: 44px;
}
QPushButton[role="iconButton"]:hover {
    border-color: #00FFB7;
}
QPushButton[role="primaryAction"] {
    background-color: #00FFB7;
    color: #121212;
    border: none;
    border-radius: 8px;
    font-weight: 600;
    min-height: 36px;
    padding: 0 24px;
}
QPushButton[role="primaryAction"]:disabled {
    background-color: rgba(0, 255, 183, 0.3);
    color: rgba(18, 18, 18, 0.5);
}
QPushButton[role="secondaryAction"] {
    background-color: #1A1B1D;
    color: #FFFFFF;
    border: 1px solid #2C2D30;
    border-radius: 8px;
    font-weight: 600;
    min-height: 36px;
    padding: 0 24px;
}
QPushButton[role="secondaryAction"]:disabled {
    color: rgba(255, 255, 255, 0.4);
    border-color: rgba(44, 45, 48, 0.6);
}
QPushButton[role="statusButton"] {
    background: transparent;
    border: none;
    font-size: 20px;
    font-weight: 700;
    min-width: 32px;
    max-width: 32px;
}
QPushButton[role="statusButton"]:hover {
    border: none;
}
QCheckBox {
    color: #FFFFFF;
    font-size: 14px;
}
)");

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(28);
    rootLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    m_pageTitleLabel = new QLabel(this);
    m_pageTitleLabel->setProperty("role", "pageTitle");
    m_pageTitleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    rootLayout->addWidget(m_pageTitleLabel, 0, Qt::AlignLeft);

    buildCameraSection(rootLayout);
    rootLayout->addWidget(createDivider(), 0, Qt::AlignLeft);
    buildAnalysisSection(rootLayout);
    rootLayout->addWidget(createDivider(), 0, Qt::AlignLeft);
    buildPlcSection(rootLayout);
    rootLayout->addWidget(createDivider(), 0, Qt::AlignLeft);
    buildLanguageSection(rootLayout);
    rootLayout->addStretch();

    m_busyOverlay = new QWidget(this);
    m_busyOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_busyOverlay->setStyleSheet(QStringLiteral("background-color: rgba(17, 17, 19, 0.55); border-radius: 12px;"));
    m_busyOverlay->hide();
    m_busyOverlay->setGeometry(rect());

    auto *overlayLayout = new QVBoxLayout(m_busyOverlay);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->setAlignment(Qt::AlignCenter);

    m_globalSpinner = new BusyIndicator(m_busyOverlay);
    overlayLayout->addWidget(m_globalSpinner);

    m_roiOverlay = new QWidget(this);
    m_roiOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_roiOverlay->setStyleSheet(QStringLiteral("background-color: rgba(17, 17, 19, 0.94); border-radius: 12px;"));
    m_roiOverlay->hide();
    m_roiOverlay->setGeometry(rect());

    auto *roiLayout = new QVBoxLayout(m_roiOverlay);
    roiLayout->setContentsMargins(32, 32, 32, 32);
    roiLayout->setSpacing(24);

    auto *roiHeader = new QWidget(m_roiOverlay);
    auto *roiHeaderLayout = new QHBoxLayout(roiHeader);
    roiHeaderLayout->setContentsMargins(0, 0, 0, 0);
    roiHeaderLayout->setSpacing(16);

    m_roiTitleLabel = new QLabel(roiHeader);
    m_roiTitleLabel->setStyleSheet(QStringLiteral("color: #FFFFFF; font-size: 26px; font-weight: 700;"));
    roiHeaderLayout->addWidget(m_roiTitleLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);

    m_roiCameraLabel = new QLabel(roiHeader);
    m_roiCameraLabel->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 0.65); font-size: 16px; font-weight: 500;"));
    roiHeaderLayout->addWidget(m_roiCameraLabel, 1, Qt::AlignLeft | Qt::AlignVCenter);

    roiHeaderLayout->addStretch();

    m_roiCloseButton = new QPushButton(roiHeader);
    m_roiCloseButton->setProperty("role", "secondaryAction");
    m_roiCloseButton->setCursor(Qt::PointingHandCursor);
    roiHeaderLayout->addWidget(m_roiCloseButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    connect(m_roiCloseButton, &QPushButton::clicked, this, &SettingsPage::hideRoiEditor);

    roiLayout->addWidget(roiHeader, 0, Qt::AlignTop);

    m_roiEditor = new CameraRoiWidget(m_roiOverlay);
    roiLayout->addWidget(m_roiEditor, 1);
    connect(m_roiEditor, &CameraRoiWidget::normalizedRoiChanged, this, &SettingsPage::handleRoiNormalizedChanged);

    buildCameraDetailOverlay();
}

void SettingsPage::buildCameraDetailOverlay()
{
    if (m_cameraDetailOverlay)
        return;

    m_cameraDetailOverlay = new QWidget(this);
    m_cameraDetailOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_cameraDetailOverlay->setStyleSheet(QStringLiteral("background-color: #111113;"));
    m_cameraDetailOverlay->hide();
    m_cameraDetailOverlay->setGeometry(rect());

    auto *overlayLayout = new QVBoxLayout(m_cameraDetailOverlay);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->setSpacing(0);
    overlayLayout->setAlignment(Qt::AlignTop);

    m_cameraDetailPanel = new QWidget(m_cameraDetailOverlay);
    m_cameraDetailPanel->setStyleSheet(QStringLiteral("background-color: #111113; border-radius: 12px;"));
    m_cameraDetailPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_cameraDetailPanel->installEventFilter(this);
    overlayLayout->addWidget(m_cameraDetailPanel, 1);

    auto *panelLayout = new QVBoxLayout(m_cameraDetailPanel);
    panelLayout->setContentsMargins(32, 32, 32, 32);
    panelLayout->setSpacing(24);

    m_cameraDetailBusyOverlay = new QWidget(m_cameraDetailPanel);
    m_cameraDetailBusyOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_cameraDetailBusyOverlay->setStyleSheet(QStringLiteral("background-color: rgba(17, 17, 19, 0.55); border-radius: 12px;"));
    m_cameraDetailBusyOverlay->hide();
    auto *busyLayout = new QVBoxLayout(m_cameraDetailBusyOverlay);
    busyLayout->setContentsMargins(0, 0, 0, 0);
    busyLayout->setSpacing(16);
    busyLayout->setAlignment(Qt::AlignCenter);
    m_cameraDetailBusySpinner = new BusyIndicator(m_cameraDetailBusyOverlay);
    busyLayout->addWidget(m_cameraDetailBusySpinner, 0, Qt::AlignHCenter);
    m_cameraDetailBusyLabel = new QLabel(m_cameraDetailBusyOverlay);
    m_cameraDetailBusyLabel->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 0.85); font-size: 13px; background-color: rgba(17, 17, 19, 0.3); padding: 8px 16px; border-radius: 8px;"));
    m_cameraDetailBusyLabel->setAlignment(Qt::AlignCenter);
    busyLayout->addWidget(m_cameraDetailBusyLabel, 0, Qt::AlignHCenter);
    m_cameraDetailBusyLabel->setVisible(false);

    auto *header = new QWidget(m_cameraDetailPanel);
    header->setFixedWidth(960);
    header->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(16);

    m_cameraDetailBreadcrumb = new QLabel(header);
    m_cameraDetailBreadcrumb->setProperty("role", "pageTitle");
    headerLayout->addWidget(m_cameraDetailBreadcrumb, 0, Qt::AlignLeft | Qt::AlignVCenter);

    headerLayout->addStretch();

    panelLayout->addWidget(header, 0, Qt::AlignTop | Qt::AlignHCenter);

    m_cameraDetailScroll = new QScrollArea(m_cameraDetailPanel);
    m_cameraDetailScroll->setFixedWidth(960);
    m_cameraDetailScroll->setWidgetResizable(true);
    m_cameraDetailScroll->setFrameShape(QFrame::NoFrame);
    m_cameraDetailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_cameraDetailScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_cameraDetailScroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: #111113; }"));
    panelLayout->addWidget(m_cameraDetailScroll, 1, Qt::AlignHCenter);

    m_cameraDetailContent = new QWidget(m_cameraDetailScroll);
    m_cameraDetailContent->setFixedWidth(960);
    m_cameraDetailContent->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_cameraDetailContent->setStyleSheet(QStringLiteral("background-color: #111113;"));
    auto *contentLayout = new QVBoxLayout(m_cameraDetailContent);
    contentLayout->setContentsMargins(0, 12, 0, 12);
    contentLayout->setSpacing(12);
    contentLayout->setSizeConstraint(QLayout::SetFixedSize);
    contentLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_cameraDetailScroll->setWidget(m_cameraDetailContent);

    m_cameraDetailSection = new QLabel(m_cameraDetailContent);
    m_cameraDetailSection->setProperty("role", "sectionTitle");
    m_cameraDetailSection->setFixedWidth(960);
    m_cameraDetailSection->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    contentLayout->addWidget(m_cameraDetailSection);

    m_cameraDetailHint = new QLabel(m_cameraDetailContent);
    m_cameraDetailHint->setWordWrap(true);
    m_cameraDetailHint->setProperty("role", "detailHint");
    m_cameraDetailHint->setFixedWidth(960);
    contentLayout->addWidget(m_cameraDetailHint);

    m_cameraDetailPreviewContainer = new QWidget(m_cameraDetailContent);
    m_cameraDetailPreviewContainer->setFixedWidth(960);
    m_cameraDetailPreviewContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_cameraDetailPreviewContainer->setProperty("role", "detailItem");
    m_cameraDetailPreviewContainer->setStyleSheet(QStringLiteral("background-color: #212225; border-radius: 12px;"));
    m_cameraDetailPreviewLayout = new QVBoxLayout(m_cameraDetailPreviewContainer);
    m_cameraDetailPreviewLayout->setContentsMargins(0, 0, 0, 0);
    m_cameraDetailPreviewLayout->setSpacing(0);
    m_cameraDetailPreviewContainer->installEventFilter(this);
    contentLayout->addWidget(m_cameraDetailPreviewContainer);

    auto createDetailRow = [this](const QString &objectName) {
        auto *row = new QWidget(m_cameraDetailContent);
        row->setObjectName(objectName);
        row->setProperty("role", "detailItem");
        row->setFixedSize(960, 72);
        row->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        row->setStyleSheet(QStringLiteral("background-color: #212225; border-radius: 12px;"));
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(20, 0, 20, 0);
        layout->setSpacing(16);
        return std::make_pair(row, layout);
    };

    // Serial port row
    auto [serialRow, serialLayout] = createDetailRow(QStringLiteral("serialRow"));
    m_cameraDetailSerialLabel = new QLabel(serialRow);
    m_cameraDetailSerialLabel->setProperty("role", "inlineLabel");
    serialLayout->addWidget(m_cameraDetailSerialLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);

    m_cameraDetailSerialCombo = new QComboBox(serialRow);
    m_cameraDetailSerialCombo->setProperty("role", "settingsCombo");
    m_cameraDetailSerialCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    serialLayout->addWidget(m_cameraDetailSerialCombo, 1);
    connect(m_cameraDetailSerialCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::handleCameraDetailSerialChanged);

    m_cameraDetailSerialRefreshButton = new QPushButton(serialRow);
    m_cameraDetailSerialRefreshButton->setProperty("role", "iconButton");
    m_cameraDetailSerialRefreshButton->setAttribute(Qt::WA_Hover, true);
    m_cameraDetailSerialRefreshButton->setAttribute(Qt::WA_StyledBackground, true);
    m_cameraDetailSerialRefreshButton->setIcon(QIcon(QStringLiteral(":/icons/refresh.svg")));
    m_cameraDetailSerialRefreshButton->setIconSize(QSize(40, 40));
    m_cameraDetailSerialRefreshButton->setCursor(Qt::PointingHandCursor);
    m_cameraDetailSerialRefreshButton->setStyleSheet(QString::fromLatin1(kCameraIconButtonHoverStyle));
    m_cameraDetailSerialRefreshButton->setToolTip(tr("Refresh serial ports"));
    serialLayout->addWidget(m_cameraDetailSerialRefreshButton, 0, Qt::AlignRight | Qt::AlignVCenter);
    connect(m_cameraDetailSerialRefreshButton, &QPushButton::clicked, this, [this]() {
        if (!m_cameraDetailSerialCombo)
            return;

        const QString previousPort = m_detailState.serialPort;
        const bool available = isSerialPortCurrentlyAvailable(previousPort);

        populateSerialPortCombo(m_cameraDetailSerialCombo, previousPort);

        if (!available)
        {
            QSignalBlocker blocker(m_cameraDetailSerialCombo);
            m_cameraDetailSerialCombo->setCurrentIndex(0);
        }

        handleCameraDetailSerialChanged(m_cameraDetailSerialCombo->currentIndex());
    });

    contentLayout->addWidget(serialRow);

    // Invert row
    auto [invertRow, invertLayout] = createDetailRow(QStringLiteral("invertRow"));
    m_cameraDetailInvertLabel = new QLabel(invertRow);
    m_cameraDetailInvertLabel->setProperty("role", "inlineLabel");
    invertLayout->addWidget(m_cameraDetailInvertLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);

    auto *invertControls = new QWidget(invertRow);
    auto *invertControlsLayout = new QHBoxLayout(invertControls);
    invertControlsLayout->setContentsMargins(0, 0, 0, 0);
    invertControlsLayout->setSpacing(32);

    auto createToggleField = [&](const QString &objectName, QLabel **labelPtr, ToggleSwitch **togglePtr) {
        auto *wrapper = new QWidget(invertControls);
        wrapper->setObjectName(objectName);
        auto *wrapperLayout = new QHBoxLayout(wrapper);
        wrapperLayout->setContentsMargins(0, 0, 0, 0);
        wrapperLayout->setSpacing(12);
        auto *label = new QLabel(wrapper);
        label->setProperty("role", "inlineLabel");
        wrapperLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        auto *toggle = new ToggleSwitch(wrapper);
        wrapperLayout->addWidget(toggle, 0, Qt::AlignLeft | Qt::AlignVCenter);
        invertControlsLayout->addWidget(wrapper);
        if (labelPtr)
            *labelPtr = label;
        if (togglePtr)
            *togglePtr = toggle;
        return wrapper;
    };

    createToggleField(QStringLiteral("verticalToggle"), &m_cameraDetailVerticalLabel, &m_cameraDetailVerticalFlipSwitch);
    createToggleField(QStringLiteral("horizontalToggle"), &m_cameraDetailHorizontalLabel, &m_cameraDetailHorizontalFlipSwitch);

    connect(m_cameraDetailVerticalFlipSwitch, &ToggleSwitch::toggled, this, [this](bool checked) {
        if (m_updatingCameraDetailUi)
            return;
        m_detailState.invertVertical = checked;
        qInfo() << "[CameraDetail] Vertical flip toggled =" << checked;
        requestSerialWrite();
    });
    connect(m_cameraDetailHorizontalFlipSwitch, &ToggleSwitch::toggled, this, [this](bool checked) {
        if (m_updatingCameraDetailUi)
            return;
        m_detailState.invertHorizontal = checked;
        qInfo() << "[CameraDetail] Horizontal flip toggled =" << checked;
        requestSerialWrite();
    });

    invertLayout->addWidget(invertControls, 1, Qt::AlignLeft | Qt::AlignVCenter);

    m_cameraDetailInvertRefresh = new QPushButton(invertRow);
    m_cameraDetailInvertRefresh->setProperty("role", "iconButton");
    m_cameraDetailInvertRefresh->setAttribute(Qt::WA_Hover, true);
    m_cameraDetailInvertRefresh->setAttribute(Qt::WA_StyledBackground, true);
    m_cameraDetailInvertRefresh->setIcon(QIcon(QStringLiteral(":/icons/refresh.svg")));
    m_cameraDetailInvertRefresh->setIconSize(QSize(40, 40));
    m_cameraDetailInvertRefresh->setCursor(Qt::PointingHandCursor);
    m_cameraDetailInvertRefresh->setStyleSheet(QString::fromLatin1(kCameraIconButtonHoverStyle));
    invertLayout->addWidget(m_cameraDetailInvertRefresh, 0, Qt::AlignRight | Qt::AlignVCenter);
    connect(m_cameraDetailInvertRefresh, &QPushButton::clicked, this, &SettingsPage::handleCameraDetailDefaultsRequested);

    contentLayout->addWidget(invertRow);

    auto createLevelRow = [&](const QString &objectName,
                              QLabel **labelPtr,
                              QVector<QPushButton *> &buttons,
                              QPushButton **minusPtr,
                              QPushButton **plusPtr,
                              QPushButton **refreshPtr,
                              const std::function<int()> &currentValue,
                              const std::function<void(int)> &onValueSelected) {
        auto [row, layout] = createDetailRow(objectName);
        layout->setSpacing(12);

        auto *label = new QLabel(row);
        label->setProperty("role", "inlineLabel");
        layout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);

        auto *minusButton = new QPushButton(row);
        minusButton->setProperty("role", "iconButton");
        minusButton->setText(QStringLiteral("-"));
        minusButton->setCursor(Qt::PointingHandCursor);
        minusButton->setAttribute(Qt::WA_Hover, true);
        minusButton->setAttribute(Qt::WA_StyledBackground, true);
        minusButton->setStyleSheet(QString::fromLatin1(kCameraIconButtonHoverStyle)
                                   + QStringLiteral("QPushButton { color: #B0B4BA; }"
                                                    "QPushButton:disabled { color: rgba(176, 180, 186, 0.4); }"));
        {
            QFont font = minusButton->font();
            if (font.pointSizeF() > 0)
                font.setPointSizeF(font.pointSizeF() * 2.0);
            else if (font.pointSize() > 0)
                font.setPointSize(font.pointSize() * 2);
            minusButton->setFont(font);
        }
        layout->addWidget(minusButton, 0, Qt::AlignLeft | Qt::AlignVCenter);

        auto *buttonsContainer = new QWidget(row);
        buttonsContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *buttonsLayout = new QHBoxLayout(buttonsContainer);
        buttonsLayout->setContentsMargins(0, 0, 0, 0);
        buttonsLayout->setSpacing(6);
        layout->addWidget(buttonsContainer, 1, Qt::AlignVCenter);

        buttons.clear();
        buttons.reserve(10);
        for (int index = 1; index <= 10; ++index)
        {
            auto *button = new QPushButton(QString::number(index), buttonsContainer);
            button->setProperty("role", "levelButton");
            button->setCursor(Qt::PointingHandCursor);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            button->setMinimumHeight(44);
            button->setFocusPolicy(Qt::NoFocus);
            buttonsLayout->addWidget(button);
            buttons.append(button);

            const std::function<void(int)> handler = onValueSelected;
            connect(button, &QPushButton::clicked, this, [handler, index]() {
                if (handler)
                    handler(index);
            });
        }

        auto *plusButton = new QPushButton(row);
        plusButton->setProperty("role", "iconButton");
        plusButton->setText(QStringLiteral("+"));
        plusButton->setCursor(Qt::PointingHandCursor);
        plusButton->setAttribute(Qt::WA_Hover, true);
        plusButton->setAttribute(Qt::WA_StyledBackground, true);
        plusButton->setStyleSheet(QString::fromLatin1(kCameraIconButtonHoverStyle)
                                  + QStringLiteral("QPushButton { color: #B0B4BA; }"
                                                   "QPushButton:disabled { color: rgba(176, 180, 186, 0.4); }"));
        {
            QFont font = plusButton->font();
            if (font.pointSizeF() > 0)
                font.setPointSizeF(font.pointSizeF() * 2.0);
            else if (font.pointSize() > 0)
                font.setPointSize(font.pointSize() * 2);
            plusButton->setFont(font);
        }
        layout->addWidget(plusButton, 0, Qt::AlignLeft | Qt::AlignVCenter);

        auto *refreshButton = new QPushButton(row);
        refreshButton->setProperty("role", "iconButton");
        refreshButton->setAttribute(Qt::WA_Hover, true);
        refreshButton->setAttribute(Qt::WA_StyledBackground, true);
        refreshButton->setIcon(QIcon(QStringLiteral(":/icons/refresh.svg")));
        refreshButton->setIconSize(QSize(40, 40));
        refreshButton->setCursor(Qt::PointingHandCursor);
        refreshButton->setStyleSheet(QString::fromLatin1(kCameraIconButtonHoverStyle));
        layout->addWidget(refreshButton, 0, Qt::AlignRight | Qt::AlignVCenter);
        connect(refreshButton, &QPushButton::clicked, this, &SettingsPage::handleCameraDetailDefaultsRequested);

        if (labelPtr)
            *labelPtr = label;
        if (minusPtr)
            *minusPtr = minusButton;
        if (plusPtr)
            *plusPtr = plusButton;
        if (refreshPtr)
            *refreshPtr = refreshButton;

        const std::function<void(int)> handler = onValueSelected;
        const std::function<int()> current = currentValue;

        connect(minusButton, &QPushButton::clicked, this, [handler, current]() {
            if (!handler || !current)
                return;
            handler(std::max(1, current() - 1));
        });
        connect(plusButton, &QPushButton::clicked, this, [handler, current]() {
            if (!handler || !current)
                return;
            handler(std::min(10, current() + 1));
        });

        contentLayout->addWidget(row);
    };

    const auto brightnessGetter = [this]() {
        return std::clamp(m_detailState.brightness, 1, 10);
    };
    const auto brightnessSetter = [this](int value) {
        const int clamped = std::clamp(value, 1, 10);
        if (m_updatingCameraDetailUi)
            return;
        if (m_detailState.brightness == clamped)
            return;
        m_detailState.brightness = clamped;
        updateCameraDetailControls();
        requestSerialWrite();
    };

    createLevelRow(QStringLiteral("brightnessRow"),
                   &m_cameraDetailBrightnessLabel,
                   m_cameraDetailBrightnessButtons,
                   &m_cameraDetailBrightnessMinus,
                   &m_cameraDetailBrightnessPlus,
                   &m_cameraDetailBrightnessRefresh,
                   brightnessGetter,
                   brightnessSetter);

    const auto sensitivityGetter = [this]() {
        return std::clamp(m_detailState.sensitivity, 1, 10);
    };
    const auto sensitivitySetter = [this](int value) {
        const int clamped = std::clamp(value, 1, 10);
        if (m_updatingCameraDetailUi)
            return;
        if (m_detailState.sensitivity == clamped)
            return;
        m_detailState.sensitivity = clamped;
        updateCameraDetailControls();
        requestSerialWrite();
    };

    createLevelRow(QStringLiteral("sensitivityRow"),
                   &m_cameraDetailSensitivityLabel,
                   m_cameraDetailSensitivityButtons,
                   &m_cameraDetailSensitivityMinus,
                   &m_cameraDetailSensitivityPlus,
                   &m_cameraDetailSensitivityRefresh,
                   sensitivityGetter,
                   sensitivitySetter);

    auto [focusAssistRow, focusAssistLayout] = createDetailRow(QStringLiteral("focusAssistRow"));
    m_cameraDetailFocusAssistLabel = new QLabel(focusAssistRow);
    m_cameraDetailFocusAssistLabel->setProperty("role", "inlineLabel");
    focusAssistLayout->addWidget(m_cameraDetailFocusAssistLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);

    m_cameraDetailFocusAssistSwitch = new ToggleSwitch(focusAssistRow);
    focusAssistLayout->addWidget(m_cameraDetailFocusAssistSwitch, 0, Qt::AlignLeft | Qt::AlignVCenter);
    focusAssistLayout->addStretch(1);

    connect(m_cameraDetailFocusAssistSwitch, &ToggleSwitch::toggled, this, [this](bool checked) {
        if (m_updatingCameraDetailUi)
            return;
        m_detailState.focusAssist = checked;
        qInfo() << "[CameraDetail] Focus assist toggled =" << checked;
        requestSerialWrite();
    });

    contentLayout->addWidget(focusAssistRow);

    auto *actionsRow = new QWidget(m_cameraDetailPanel);
    actionsRow->setFixedWidth(960);
    actionsRow->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    auto *actionsLayout = new QHBoxLayout(actionsRow);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(16);
    actionsLayout->addStretch();

    m_cameraDetailCancelButton = new QPushButton(actionsRow);
    m_cameraDetailCancelButton->setProperty("role", "secondaryAction");
    m_cameraDetailCancelButton->setCursor(Qt::PointingHandCursor);
    m_cameraDetailCancelButton->setFixedSize(119, 48);
    m_cameraDetailCancelButton->setStyleSheet(QStringLiteral(
        "QPushButton[role=\"secondaryAction\"] { background-color: #1A1B1D; color: #FFFFFF; border: none; border-radius: 12px; font-weight: 600; }"
        "QPushButton[role=\"secondaryAction\"]:disabled { color: rgba(255, 255, 255, 0.4); }"));
    connect(m_cameraDetailCancelButton, &QPushButton::clicked, this, &SettingsPage::hideCameraDetail);
    actionsLayout->addWidget(m_cameraDetailCancelButton, 0, Qt::AlignRight);

    m_cameraDetailSaveButton = new QPushButton(actionsRow);
    m_cameraDetailSaveButton->setProperty("role", "primaryAction");
    m_cameraDetailSaveButton->setCursor(Qt::PointingHandCursor);
    m_cameraDetailSaveButton->setFixedSize(119, 48);
    m_cameraDetailSaveButton->setStyleSheet(QStringLiteral(
        "QPushButton[role=\"primaryAction\"] { background-color: #00FFB7; color: #121212; border-radius: 12px; font-weight: 600; border: none; }"
        "QPushButton[role=\"primaryAction\"]:disabled { background-color: rgba(0, 255, 183, 0.35); color: rgba(18, 18, 18, 0.45); }"));
    connect(m_cameraDetailSaveButton, &QPushButton::clicked, this, &SettingsPage::handleCameraDetailSaveRequested);
    actionsLayout->addWidget(m_cameraDetailSaveButton, 0, Qt::AlignRight);

    panelLayout->addWidget(actionsRow, 0, Qt::AlignHCenter);

    updateCameraDetailBusyOverlayGeometry();
}

void SettingsPage::buildCameraSection(QVBoxLayout *rootLayout)
{
    // Camera slot assignments, visibility, and ROI/serial controls live in this collapsible section.
    rootLayout->addWidget(createSectionTitle(QString(), &m_cameraSectionTitle), 0, Qt::AlignLeft);

    auto *listContainer = new QWidget(this);
    listContainer->setFixedWidth(960);
    listContainer->setStyleSheet(QStringLiteral("background-color:transparent; border-radius: 12px;"));
    
    auto *listLayout = new QVBoxLayout(listContainer);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(12);
    
    m_cameraRowsLayout = listLayout;

    rootLayout->addWidget(listContainer, 0, Qt::AlignLeft);
}

void SettingsPage::buildAnalysisSection(QVBoxLayout *rootLayout)
{
    // AI analysis parameters (model, thresholds, pass info) with inline validation.
    rootLayout->addWidget(createSectionTitle(QString(), &m_analysisSectionTitle), 0, Qt::AlignLeft);

    auto *analysisHintContainer = new QWidget(this);
    analysisHintContainer->setFixedWidth(960);
    analysisHintContainer->setAttribute(Qt::WA_StyledBackground, true);
    analysisHintContainer->setStyleSheet(QStringLiteral("background-color: #111113;"));

    auto *analysisHintLayout = new QVBoxLayout(analysisHintContainer);
    analysisHintLayout->setContentsMargins(0, 8, 0, 8);
    analysisHintLayout->setSpacing(0);

    m_analysisHintLabel = new QLabel(analysisHintContainer);
    m_analysisHintLabel->setProperty("role", "sectionHint");
    m_analysisHintLabel->setWordWrap(true);
    m_analysisHintLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_analysisHintLabel->setTextFormat(Qt::RichText);
    analysisHintLayout->addWidget(m_analysisHintLabel);
    updateAnalysisHintText();

    rootLayout->addWidget(analysisHintContainer, 0, Qt::AlignLeft);

    auto *rowContainer = createSettingsRowContainer(this);
    rowContainer->setFixedWidth(960);
    rowContainer->setStyleSheet(QStringLiteral("background-color: #212225;"));

    auto *layout = new QHBoxLayout(rowContainer);
    layout->setContentsMargins(20, 12, 20, 12);
    layout->setSpacing(12);

    m_aiStatusButton = new QPushButton(rowContainer);
    m_aiStatusButton->setProperty("role", "statusButton");
    m_aiStatusButton->setFlat(true);
    m_aiStatusButton->setCursor(Qt::PointingHandCursor);
    m_aiStatusButton->setToolTip(tr("Send startup/shutdown requests to the AI analysis server."));
    layout->addWidget(m_aiStatusButton);

    connect(m_aiStatusButton, &QPushButton::clicked, this, [this]() {
        if (!m_aiEnabledCheck)
            return;
        m_aiEnabledCheck->setChecked(!m_aiEnabledCheck->isChecked());
    });

    auto *passGroup = new QWidget(rowContainer);
    auto *passGroupLayout = new QHBoxLayout(passGroup);
    passGroupLayout->setContentsMargins(0, 0, 0, 0);
    passGroupLayout->setSpacing(8);

    m_passTypeCombo = new QComboBox();
    m_passTypeCombo->setProperty("role", "settingsCombo");
    m_passTypeCombo->setMinimumWidth(140);
    m_passTypeCombo->addItem(tr("Root"), QStringLiteral("Root"));
    m_passTypeCombo->addItem(tr("Second"), QStringLiteral("Second"));
    auto *passListView = new QListView(m_passTypeCombo);
    passListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    passListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_passTypeCombo->setView(passListView);

    auto *passComboContainer = createComboFrame(m_passTypeCombo, passGroup, std::min(4, m_passTypeCombo->count()));
    m_passTypeFrame = passComboContainer;
    setInputFrameEditable(m_passTypeFrame, m_passTypeCombo && m_passTypeCombo->isEnabled());
    passGroupLayout->addWidget(passComboContainer);
    passGroupLayout->setAlignment(passComboContainer, Qt::AlignVCenter);
    m_passUsageLabel = new QLabel(passGroup);
    m_passUsageLabel->setProperty("role", "inlineLabel");
    m_passUsageLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    passGroupLayout->addWidget(m_passUsageLabel);
    layout->addWidget(passGroup, 1);
    auto *passDivider = createVerticalDivider(rowContainer);
    layout->addWidget(passDivider);
    layout->setAlignment(passDivider, Qt::AlignVCenter);

    m_aiEnabledCheck = new QCheckBox(rowContainer);
    m_aiEnabledCheck->setCursor(Qt::PointingHandCursor);
    m_aiEnabledCheck->setToolTip(tr("Send startup/shutdown requests to the AI analysis server."));
    m_aiEnabledCheck->hide();

    m_torchLengthEdit = new QLineEdit();
    m_torchLengthEdit->setValidator(new QDoubleValidator(0.0, 1000.0, 2, m_torchLengthEdit));
    m_torchLengthEdit->setText(QString::number(26.0, 'f', 1));
    auto *torchField = new QWidget(rowContainer);
    auto *torchLayout = new QHBoxLayout(torchField);
    torchLayout->setContentsMargins(12, 0, 12, 0);
    torchLayout->setSpacing(4);
    m_torchLabel = new QLabel(tr("Torch"), torchField);
    m_torchLabel->setProperty("role", "inlineLabel");
    m_torchLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    torchLayout->addWidget(m_torchLabel, 0, Qt::AlignVCenter);
    auto *torchContainer = createLineEditFrame(m_torchLengthEdit, torchField, false);
    m_torchInputFrame = torchContainer;
    m_torchLengthEdit->setTextMargins(16, 0, 4, 0);
    torchLayout->addWidget(torchContainer, 1, Qt::AlignVCenter);
    m_torchUnitLabel = new QLabel(tr("mm"), torchField);
    m_torchUnitLabel->setProperty("role", "inlineLabel");
    m_torchUnitLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    torchLayout->addWidget(m_torchUnitLabel, 0, Qt::AlignVCenter);
    torchLayout->addStretch();
    layout->addWidget(torchField, 1);
    auto *torchDivider = createVerticalDivider(rowContainer);
    layout->addWidget(torchDivider);
    layout->setAlignment(torchDivider, Qt::AlignVCenter);

    m_allowableEdit = new QLineEdit();
    m_allowableEdit->setValidator(new QDoubleValidator(0.0, 1.0, 3, m_allowableEdit));
    m_allowableEdit->setAlignment(Qt::AlignCenter);
    auto *allowableField = new QWidget(rowContainer);
    auto *allowableLayout = new QHBoxLayout(allowableField);
    allowableLayout->setContentsMargins(12, 0, 12, 0);
    allowableLayout->setSpacing(4);
    m_aiConfidenceLabel = new QLabel(tr("Allowable Limit"), allowableField);
    m_aiConfidenceLabel->setProperty("role", "inlineLabel");
    m_aiConfidenceLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    allowableLayout->addWidget(m_aiConfidenceLabel, 0, Qt::AlignVCenter);
    auto *allowableContainer = createLineEditFrame(m_allowableEdit, allowableField, false);
    m_allowableInputFrame = allowableContainer;
    m_allowableEdit->setTextMargins(16, 0, 4, 0);
    allowableLayout->addWidget(allowableContainer, 1, Qt::AlignVCenter);
    m_allowableUnitLabel = new QLabel(tr("mm"), allowableField);
    m_allowableUnitLabel->setProperty("role", "inlineLabel");
    m_allowableUnitLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    allowableLayout->addWidget(m_allowableUnitLabel, 0, Qt::AlignVCenter);
    allowableLayout->addStretch();
    layout->addWidget(allowableField, 1);

    layout->addStretch();

    rootLayout->addWidget(rowContainer, 0, Qt::AlignLeft);
}

void SettingsPage::buildPlcSection(QVBoxLayout *rootLayout)
{
    // PLC connectivity grid allowing operators to edit IP/port and manage runtime connections.
    rootLayout->addWidget(createSectionTitle(QString(), &m_plcSectionTitle), 0, Qt::AlignLeft);

    auto *listContainer = new QWidget(this);
    listContainer->setFixedWidth(960);
    listContainer->setStyleSheet(QStringLiteral("background-color:#212225; border-radius: 12px;"));
    
    auto *layout = new QVBoxLayout(listContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    m_plcRowsLayout = layout;

    rootLayout->addWidget(listContainer, 0, Qt::AlignLeft);
}

void SettingsPage::buildLanguageSection(QVBoxLayout *rootLayout)
{
    // Language selector integrates LocalizationManager for runtime language switching.
    rootLayout->addWidget(createSectionTitle(QString(), &m_languageSectionTitle), 0, Qt::AlignLeft);

    auto *rowContainer = createSettingsRowContainer(this);
    rowContainer->setFixedWidth(960);
    rowContainer->setStyleSheet(QStringLiteral("background-color:#212225"));

    auto *layout = new QHBoxLayout(rowContainer);
    layout->setContentsMargins(20, 12, 20, 12);
    layout->setSpacing(16);

    m_languageStatusButton = new QPushButton(rowContainer);
    m_languageStatusButton->setProperty("role", "statusButton");
    m_languageStatusButton->setFlat(true);
    layout->addWidget(m_languageStatusButton);

    m_languageNameLabel = new QLabel(rowContainer);
    m_languageNameLabel->setMinimumWidth(120);
    layout->addWidget(m_languageNameLabel);
    auto *languageDivider = createVerticalDivider(rowContainer);
    layout->addWidget(languageDivider);
    layout->setAlignment(languageDivider, Qt::AlignVCenter);

    auto *languageField = new QWidget(rowContainer);
    auto *languageLayout = new QHBoxLayout(languageField);
    languageLayout->setContentsMargins(0, 0, 0, 0);
    languageLayout->setSpacing(12);

    m_languageCombo = new QComboBox();
    m_languageCombo->setProperty("role", "settingsCombo");
    m_languageCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_languageCombo->setMinimumContentsLength(18);
    m_languageCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_languageCombo->setMinimumWidth(320);

    auto *languageListView = new QListView(m_languageCombo);
    languageListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    languageListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_languageCombo->setView(languageListView);

    auto *languageComboContainer = createComboFrame(m_languageCombo, languageField);
    m_languageComboFrame = languageComboContainer;
    setInputFrameEditable(m_languageComboFrame, m_languageCombo && m_languageCombo->isEnabled());
    languageLayout->addWidget(languageComboContainer, 1);
    languageLayout->setAlignment(languageComboContainer, Qt::AlignVCenter);
    layout->addWidget(languageField, 1);

    layout->addStretch();

    auto *languageActionDivider = createVerticalDivider(rowContainer);
    layout->addWidget(languageActionDivider);
    layout->setAlignment(languageActionDivider, Qt::AlignVCenter);

    m_languageRefreshButton = new QPushButton(rowContainer);
    m_languageRefreshButton->setProperty("role", "iconButton");
    m_languageRefreshButton->setAttribute(Qt::WA_Hover, true);
    m_languageRefreshButton->setAttribute(Qt::WA_StyledBackground, true);
    m_languageRefreshButton->setIcon(QIcon(QStringLiteral(":/icons/refresh.svg")));
    m_languageRefreshButton->setIconSize(QSize(40, 40));
    m_languageRefreshButton->setCursor(Qt::PointingHandCursor);
    m_languageRefreshButton->setStyleSheet(QString::fromLatin1(kCameraIconButtonHoverStyle));
    layout->addWidget(m_languageRefreshButton);

    rootLayout->addWidget(rowContainer, 0, Qt::AlignLeft);
}

QWidget *SettingsPage::createSectionTitle(const QString &text, QLabel **titleLabel)
{
    auto *wrapper = new QWidget(this);
    wrapper->setFixedWidth(960);
    wrapper->setAttribute(Qt::WA_StyledBackground, true);
    wrapper->setStyleSheet(QStringLiteral("background-color: #111113;"));

    auto *layout = new QHBoxLayout(wrapper);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *label = new QLabel(text, wrapper);
    label->setProperty("role", "sectionTitle");
    label->setStyleSheet(QStringLiteral("background-color: #111113;"));
    layout->addWidget(label);
    layout->addStretch();

    if (titleLabel)
        *titleLabel = label;
    return wrapper;
}

QWidget *SettingsPage::createSettingsRowContainer(QWidget *parent) const
{
    auto *container = new QWidget(parent ? parent : const_cast<SettingsPage *>(this));
    container->setProperty("settingsItem", true);
    container->setMinimumHeight(72);
    container->setMaximumHeight(72);
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return container;
}

QFrame *SettingsPage::createDivider(QWidget *parent) const
{
    auto *line = new QFrame(parent ? parent : const_cast<SettingsPage *>(this));
    line->setProperty("role", "divider");
    line->setFixedWidth(960);
    line->setFrameShape(QFrame::NoFrame);
    return line;
}

QFrame *SettingsPage::createVerticalDivider(QWidget *parent) const
{
    auto *line = new QFrame(parent ? parent : const_cast<SettingsPage *>(this));
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedSize(1, 48);
    line->setStyleSheet(QStringLiteral("background-color: #43484E;"));
    line->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return line;
}

QIcon SettingsPage::createRoiIcon(const QSize &size) const
{
    QIcon icon(QStringLiteral(":/icons/roi.svg"));
    if (!size.isValid())
        return icon;

    const QPixmap pixmap = icon.pixmap(size);
    if (pixmap.isNull())
        return icon;

    QIcon sizedIcon;
    sizedIcon.addPixmap(pixmap);
    return sizedIcon;
}

QIcon SettingsPage::createGearIcon(const QSize &size) const
{
    const QSize iconSize = size.isValid() ? size : QSize(18, 18);
    QPixmap pixmap(iconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor color(223, 229, 235, 230);
    painter.setPen(QPen(color, 2));
    painter.setBrush(Qt::NoBrush);

    const QPointF center(iconSize.width() / 2.0, iconSize.height() / 2.0);
    const double radius = std::min(iconSize.width(), iconSize.height()) / 2.0 - 3.0;
    painter.drawEllipse(center, radius, radius);

    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < 6; ++i)
    {
        const double angle = (kPi / 3.0) * i;
        const QPointF outer(center.x() + std::cos(angle) * radius, center.y() + std::sin(angle) * radius);
        const QPointF inner(center.x() + std::cos(angle) * (radius - 4.0), center.y() + std::sin(angle) * (radius - 4.0));
        painter.drawLine(inner, outer);
    }

    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(center, radius / 3.0, radius / 3.0);

    return QIcon(pixmap);
}

void SettingsPage::rebuildCameraRows(const QVector<CameraManager::CameraInfo> &cameras)
{
    if (!m_cameraRowsLayout)
        return;

    while (QLayoutItem *item = m_cameraRowsLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_cameraRows.clear();

    QSet<QString> activeIds;
    for (const auto &info : cameras)
        activeIds.insert(info.id);

    for (auto it = m_cameraSerialSelection.begin(); it != m_cameraSerialSelection.end();)
    {
        if (!activeIds.contains(it.key()))
            it = m_cameraSerialSelection.erase(it);
        else
            ++it;
    }

    if (cameras.isEmpty())
    {
        auto *placeholder = createSettingsRowContainer(this);
        placeholder->setFixedWidth(960);
        auto *layout = new QHBoxLayout(placeholder);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *label = new QLabel(tr("No cameras connected."), placeholder);
        label->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 0.55); font-size: 14px;"));
        layout->addStretch();
        layout->addWidget(label);
        layout->addStretch();
        m_cameraRowsLayout->addWidget(placeholder);
        return;
    }

    QStringList cameraNames;
    cameraNames.reserve(cameras.size());
    for (const auto &info : cameras)
        cameraNames.append(displayNameForCamera(info));

    for (int index = 0; index < cameras.size(); ++index)
    {
        const auto &info = cameras.at(index);
        CameraRow row;
        row.cameraId = info.id;
        row.visible = info.visible;

        row.container = createSettingsRowContainer(this);
        row.container->setStyleSheet(QStringLiteral("background-color: #212225;"));
        row.container->setFixedWidth(960);

        auto *layout = new QHBoxLayout(row.container);
        layout->setContentsMargins(20, 0, 20, 0);
        layout->setSpacing(16);

        row.statusButton = new QPushButton(row.container);
        row.statusButton->setProperty("role", "statusButton");
        row.statusButton->setFlat(true);
        row.statusButton->setCursor(Qt::PointingHandCursor);
        layout->addWidget(row.statusButton);

        connect(row.statusButton, &QPushButton::clicked, this, [this, cameraId = info.id]() {
            if (!m_cameraManager)
                return;
            const auto snapshot = m_cameraManager->camera(cameraId);
            applyCameraVisibility(cameraId, !snapshot.visible);
        });

        row.slotLabel = new QLabel(row.container);
        row.slotLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        row.slotLabel->setFixedWidth(SLOT_LABEL_WIDTH_PX);
        row.slotLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        row.slotLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        layout->addWidget(row.slotLabel);
        auto *slotDivider = createVerticalDivider(row.container);
        layout->addWidget(slotDivider);
        layout->setAlignment(slotDivider, Qt::AlignVCenter);

        auto createField = [&](const QString &labelText, QWidget *control, QWidget *&frameOut, int stretch = 0) -> QLabel * {
            auto *field = new QWidget(row.container);
            auto *fieldLayout = new QHBoxLayout(field);
            fieldLayout->setContentsMargins(12, 0, 12, 0);
            fieldLayout->setSpacing(6);
            auto *label = new QLabel(labelText, field);
            label->setProperty("role", "inlineLabel");
            label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            fieldLayout->addWidget(label, 0, Qt::AlignVCenter);

            QWidget *widgetToAdd = control;
            if (auto *combo = qobject_cast<QComboBox *>(control))
            {
                int visible = combo->maxVisibleItems();
                if (visible <= 0)
                    visible = -1;
                widgetToAdd = createComboFrame(combo, field, visible);
            }
            else if (auto *lineEdit = qobject_cast<QLineEdit *>(control))
            {
                widgetToAdd = createLineEditFrame(lineEdit, field, lineEdit->isReadOnly());
            }

            if (widgetToAdd)
                fieldLayout->addWidget(widgetToAdd, stretch > 0 ? 1 : 0, Qt::AlignVCenter);

            if (stretch > 0)
            {
                field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                layout->addWidget(field, stretch);
            }
            else
            {
                layout->addWidget(field);
            }
            frameOut = widgetToAdd;
            return label;
        };

        row.cameraCombo = new QComboBox(row.container);
        row.cameraCombo->setEditable(true);
        row.cameraCombo->setInsertPolicy(QComboBox::NoInsert);
        row.cameraCombo->setProperty("role", "settingsCombo");
        row.cameraCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row.cameraCombo->addItems(cameraNames);
        row.cameraFieldLabel = createField(tr("Camera Name"), row.cameraCombo, row.cameraFieldFrame, 2);

        if (auto *lineEdit = row.cameraCombo->lineEdit())
        {
            connect(lineEdit, &QLineEdit::editingFinished, this, [this, combo = row.cameraCombo]() {
                if (!combo)
                    return;

                const QString text = combo->lineEdit() ? combo->lineEdit()->text() : QString();

                QString cameraId;
                for (const auto &entry : std::as_const(m_cameraRows))
                {
                    if (entry.cameraCombo == combo)
                    {
                        cameraId = entry.cameraId;
                        break;
                    }
                }
                if (cameraId.isEmpty())
                    return;

                const QString trimmed = text.trimmed();
                if (trimmed.isEmpty())
                {
                    CameraManager::CameraInfo info;
                    if (m_cameraManager)
                        info = m_cameraManager->camera(cameraId);
                    if (info.id.isEmpty())
                        info.id = cameraId;
                    const QString fallback = displayNameForCamera(info);
                    QSignalBlocker blocker(combo);
                    combo->setEditText(fallback);
                    return;
                }

                handleCameraNameEdited(cameraId, text);
            });
        }

        row.serialEdit = new QLineEdit(row.container);
        row.serialEdit->setReadOnly(true);
        row.serialEdit->setCursor(Qt::ArrowCursor);
        row.serialFieldLabel = createField(tr("Serial Port"), row.serialEdit, row.serialFieldFrame, 2);

        auto *serialDivider = createVerticalDivider(row.container);
        layout->addWidget(serialDivider);
        layout->setAlignment(serialDivider, Qt::AlignVCenter);

        layout->addStretch();

        row.roiButton = new QPushButton(row.container);
        row.roiButton->setProperty("role", "iconButton");
        row.roiButton->setAttribute(Qt::WA_Hover, true);
        row.roiButton->setAttribute(Qt::WA_StyledBackground, true);
        row.roiButton->setIcon(createRoiIcon(QSize(40, 40)));
        row.roiButton->setIconSize(QSize(40, 40));
        row.roiButton->setCursor(Qt::PointingHandCursor);
        row.roiButton->setStyleSheet(QString::fromLatin1(kCameraIconButtonHoverStyle));
        const QString roiTooltip = tr("Edit ROI for this camera");
        row.roiButton->setToolTip(roiTooltip);
        row.roiButton->setAccessibleName(roiTooltip);
        layout->addWidget(row.roiButton);

        row.roiButton->setProperty("cameraId", info.id);
        row.roiButton->setProperty("cameraAlias", QString());
        connect(row.roiButton, &QPushButton::clicked, this, [this, button = row.roiButton]() {
            const QString cameraId = button ? button->property("cameraId").toString() : QString();
            const QString cameraAlias = button ? button->property("cameraAlias").toString() : QString();
            showRoiEditor(cameraId, cameraAlias);
        });

        row.detailButton = new QPushButton(row.container);
        row.detailButton->setProperty("role", "iconButton");
        row.detailButton->setAttribute(Qt::WA_Hover, true);
        row.detailButton->setAttribute(Qt::WA_StyledBackground, true);
        row.detailButton->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
        row.detailButton->setIconSize(QSize(40, 40));
        row.detailButton->setCursor(Qt::PointingHandCursor);
        row.detailButton->setStyleSheet(QString::fromLatin1(kCameraIconButtonHoverStyle));
        const QString detailTooltip = tr("Open camera settings");
        row.detailButton->setToolTip(detailTooltip);
        row.detailButton->setAccessibleName(detailTooltip);
        layout->addWidget(row.detailButton);

        row.detailButton->setProperty("cameraId", info.id);
        connect(row.detailButton, &QPushButton::clicked, this, [this, button = row.detailButton]() {
            const QString cameraId = button ? button->property("cameraId").toString() : QString();
            showCameraDetail(cameraId);
        });

        m_cameraRows.append(row);
        m_cameraRowsLayout->addWidget(row.container);

        refreshCameraRow(info, index);
    }
}

void SettingsPage::refreshCameraRow(const CameraManager::CameraInfo &info, int index)
{
    if (index < 0 || index >= m_cameraRows.size())
        return;

    CameraRow &row = m_cameraRows[index];
    row.cameraId = info.id;
    row.visible = info.visible;

    const QString slotText = !info.slotId.isEmpty()
                                 ? info.slotId
                                 : tr("Slot %1").arg(index + 1, 2, 10, QLatin1Char('0'));
    if (row.slotLabel)
    {
        const QColor color = cameraConnectivityColor(info.visible);
        const QMargins margins = row.slotLabel->contentsMargins();
        const int availableWidth = std::max(0, SLOT_LABEL_WIDTH_PX - margins.left() - margins.right());
        const QString elided = row.slotLabel->fontMetrics().elidedText(slotText, Qt::ElideRight, availableWidth);
        row.slotLabel->setToolTip(slotText);
        row.slotLabel->setText(elided);
        row.slotLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 16px; font-weight: 600;")
                                         .arg(rgbaString(color)));
    }

    if (row.cameraFieldLabel)
        row.cameraFieldLabel->setText(tr("Camera Name"));
    if (row.serialFieldLabel)
        row.serialFieldLabel->setText(tr("Serial Port"));

    const QString displayName = displayNameForCamera(info);
    if (row.roiButton)
    {
        row.roiButton->setProperty("cameraId", info.id);
        QString alias = QString();
        const int aliasIndex = findCameraProfile(info);
        if (aliasIndex >= 0 && aliasIndex < m_cameraRoiProfiles.size())
            alias = m_cameraRoiProfiles.at(aliasIndex).alias.trimmed();
        if (alias.isEmpty())
            alias = displayName;
        row.roiButton->setProperty("cameraAlias", alias);
    }
    if (row.detailButton)
        row.detailButton->setProperty("cameraId", info.id);
    if (row.cameraCombo)
    {
        QSignalBlocker blocker(row.cameraCombo);
        const int existingIndex = row.cameraCombo->findText(displayName);
        if (existingIndex < 0)
            row.cameraCombo->addItem(displayName);
        row.cameraCombo->setEditText(displayName);
        if (row.cameraCombo->isEditable() && row.cameraCombo->lineEdit())
            row.cameraCombo->lineEdit()->setPlaceholderText(tr("Select a camera"));
    }

    if (row.serialFieldLabel)
        row.serialFieldLabel->setText(tr("Serial Port"));
    if (row.serialEdit)
    {
        QString displayPort = serialPortForCamera(info);
        if (displayPort.isEmpty())
            displayPort = tr("Not configured");
        row.serialEdit->setToolTip(displayPort);
        row.serialEdit->setText(displayPort);
        row.serialEdit->setCursorPosition(0);
    }

    setInputFrameEditable(row.cameraFieldFrame, row.cameraCombo && row.cameraCombo->isEnabled());
    setInputFrameEditable(row.serialFieldFrame, row.serialEdit && row.serialEdit->isEnabled() && !row.serialEdit->isReadOnly());

    updateCameraRowStatus(info, row.statusButton);
}

void SettingsPage::updateCameraRowStatus(const CameraManager::CameraInfo &info, QPushButton *statusButton) const
{
    if (!statusButton)
        return;

    const QColor color = cameraConnectivityColor(info.visible);
    const QString colorText = rgbaString(color);

    statusButton->setText(info.visible ? QStringLiteral("✓") : QStringLiteral("✕"));
    statusButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: 20px; font-weight: 700; border: none; background: transparent; }")
                                    .arg(colorText));
    statusButton->setToolTip(info.visible ? tr("Active") : tr("Inactive"));
}

QColor SettingsPage::cameraConnectivityColor(bool connected) const
{
    if (connected)
        return QColor(QStringLiteral("#00FFB7"));

    QColor color(223, 235, 253);
    color.setAlphaF(0.4275);
    return color;
}

void SettingsPage::populateSerialPortCombo(QComboBox *combo, const QString &selectedPort)
{
    if (!combo)
        return;

    const QString target = selectedPort.isEmpty() ? combo->currentData().toString() : selectedPort;

    QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItem(tr("Select a serial port"), QString());

    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports)
    {
        if (!isAllowedSerialPort(port))
            continue;
        const QString display = port.description().isEmpty()
                                    ? port.portName()
                                    : QStringLiteral("%1 (%2)").arg(port.portName(), port.description());
        combo->addItem(display, port.portName());
    }

    int index = combo->findData(target);
    if (index < 0 && !target.isEmpty())
    {
        const QString label = tr("%1 (disconnected)").arg(target);
        combo->addItem(label, target);
        index = combo->findData(target);
    }
    if (index < 0)
        index = 0;
    combo->setCurrentIndex(index);

    applyComboPopupStyle(combo, std::min(4, combo->count()));
}

void SettingsPage::applyCameraAssignment(const QString &cameraId, const QString &cameraName)
{
    if (!m_cameraManager || cameraId.isEmpty())
        return;

    auto info = m_cameraManager->camera(cameraId);
    if (info.id.isEmpty())
        return;

    const QString trimmed = cameraName.trimmed();
    if (info.name == trimmed)
        return;

    info.name = trimmed;
    m_cameraManager->updateCamera(info);
}

void SettingsPage::applyCameraAlias(const QString &cameraId, const QString &alias)
{
    if (!m_cameraManager || cameraId.isEmpty())
        return;

    auto info = m_cameraManager->camera(cameraId);
    if (info.id.isEmpty())
        return;

    const QString trimmed = alias.trimmed();
    if (info.alias == trimmed)
        return;

    info.alias = trimmed;
    m_cameraManager->updateCamera(info);
}

void SettingsPage::handleCameraNameEdited(const QString &cameraId, const QString &newName)
{
    if (cameraId.isEmpty())
        return;

    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty())
        return;

    bool modified = false;
    CameraManager::CameraInfo info = m_cameraManager ? m_cameraManager->camera(cameraId) : CameraManager::CameraInfo();
    if (info.id.isEmpty())
        info.id = cameraId;

    int profileIndex = findCameraProfile(info);
    if (profileIndex < 0)
    {
        CameraRoiProfile profile;
        profile.id = info.id;
        profile.slotId = info.slotId;
        profile.name = info.name;
        profile.alias = uniqueAlias(trimmed);
        profile.enabled = info.visible;
        m_cameraRoiProfiles.append(profile);
        profileIndex = m_cameraRoiProfiles.size() - 1;
        modified = true;
    }
    else
    {
        CameraRoiProfile &profile = m_cameraRoiProfiles[profileIndex];
        bool metaChanged = false;
        if (!info.id.isEmpty() && profile.id != info.id)
        {
            profile.id = info.id;
            metaChanged = true;
        }
        if (profile.slotId != info.slotId)
        {
            profile.slotId = info.slotId;
            metaChanged = true;
        }
        if (profile.name != info.name)
        {
            profile.name = info.name;
            metaChanged = true;
        }
        if (profile.enabled != info.visible)
        {
            profile.enabled = info.visible;
            metaChanged = true;
        }

        const QString updatedAlias = uniqueAlias(trimmed, profileIndex);
        if (profile.alias.compare(updatedAlias, Qt::CaseInsensitive) != 0)
        {
            profile.alias = updatedAlias;
            modified = true;
        }

        if (metaChanged)
            modified = true;
    }

    if (modified)
    {
        rebuildCameraRoiAliasIndex();
        m_cameraRoiDirty = true;
        QString appliedAlias;
        if (profileIndex >= 0 && profileIndex < m_cameraRoiProfiles.size())
            appliedAlias = m_cameraRoiProfiles.at(profileIndex).alias;
        applyCameraAlias(cameraId, appliedAlias);
        saveCameraSettings();
        populateCameraList();
    }
}

void SettingsPage::applyCameraVisibility(const QString &cameraId, bool visible)
{
    if (!m_cameraManager || cameraId.isEmpty())
        return;

    auto info = m_cameraManager->camera(cameraId);
    if (info.id.isEmpty() || info.visible == visible)
        return;

    info.visible = visible;
    updateCameraProfileVisibility(info, visible);
    m_cameraManager->updateCamera(info);
    m_cameraManager->setCameraVisibility(cameraId, visible);
}

void SettingsPage::populateCameraList()
{
    QVector<CameraManager::CameraInfo> cameras;
    if (m_cameraManager)
        cameras = m_cameraManager->cameras();

    rebuildCameraRows(cameras);
}

void SettingsPage::loadAnalysisParametersFromConfig()
{
    if (!m_aiClient)
        return;

    AiClient::Settings settings = m_aiClient->settings();

    const QJsonDocument doc = ConfigUtils::loadConfig();
    if (doc.isObject())
    {
        const QJsonObject root = doc.object();
        const QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
        const QJsonObject analysisParam = settingsObj.value(QStringLiteral("analysisParam")).toObject();

        if (!analysisParam.isEmpty())
        {
            if (analysisParam.contains(QStringLiteral("enableAnalysis")))
                settings.enableAnalysis = analysisParam.value(QStringLiteral("enableAnalysis")).toBool(settings.enableAnalysis);

            if (analysisParam.contains(QStringLiteral("confidence")))
            {
                const QJsonValue value = analysisParam.value(QStringLiteral("confidence"));
                double confidence = settings.confidenceThreshold;
                if (value.isDouble())
                    confidence = value.toDouble(confidence);
                else if (value.isString())
                {
                    bool ok = false;
                    const double converted = value.toString().toDouble(&ok);
                    if (ok)
                        confidence = converted;
                }
                settings.confidenceThreshold = std::clamp(confidence, 0.0, 1.0);
            }

            if (analysisParam.contains(QStringLiteral("passLevel")))
            {
                const QString stored = analysisParam.value(QStringLiteral("passLevel"))
                                           .toString(settings.passLevel);
                if (!stored.isEmpty())
                {
                    if (stored.compare(QStringLiteral("Second"), Qt::CaseInsensitive) == 0)
                        settings.passLevel = QStringLiteral("Second");
                    else
                        settings.passLevel = QStringLiteral("Root");
                }
            }

            if (analysisParam.contains(QStringLiteral("torchLength")))
            {
                const QJsonValue value = analysisParam.value(QStringLiteral("torchLength"));
                double length = settings.torchLengthMm;
                if (value.isDouble())
                    length = value.toDouble(length);
                else if (value.isString())
                {
                    bool ok = false;
                    const double converted = value.toString().toDouble(&ok);
                    if (ok)
                        length = converted;
                }
                settings.torchLengthMm = std::clamp(length, 0.0, 1000.0);
            }

            if (analysisParam.contains(QStringLiteral("dotSize")))
            {
                const QJsonValue value = analysisParam.value(QStringLiteral("dotSize"));
                double dotSize = settings.detectionDotSizePx;
                if (value.isDouble())
                    dotSize = value.toDouble(dotSize);
                else if (value.isString())
                {
                    bool ok = false;
                    const double converted = value.toString().toDouble(&ok);
                    if (ok)
                        dotSize = converted;
                }
                settings.detectionDotSizePx = std::clamp(dotSize, 1.0, 64.0);
            }
        }
    }

    m_aiClient->setSettings(settings);

    if (m_aiEnabledCheck)
    {
        QSignalBlocker block(m_aiEnabledCheck);
        m_aiEnabledCheck->setChecked(settings.enableAnalysis);
    }
    if (m_allowableEdit)
    {
        QSignalBlocker block(m_allowableEdit);
        m_allowableEdit->setText(QString::number(settings.confidenceThreshold, 'f', 2));
    }

    updateAiStatusButton();
}

void SettingsPage::persistAnalysisParameters(const AiClient::Settings &settings) const
{
    QJsonDocument doc = ConfigUtils::loadConfig();
    if (!doc.isObject())
        doc = QJsonDocument(QJsonObject());

    QJsonObject root = doc.object();
    QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    QJsonObject analysisParam = settingsObj.value(QStringLiteral("analysisParam")).toObject();

    bool changed = false;

    const bool storedEnable = analysisParam.value(QStringLiteral("enableAnalysis")).toBool(false);
    if (!analysisParam.contains(QStringLiteral("enableAnalysis")) || storedEnable != settings.enableAnalysis)
    {
        analysisParam.insert(QStringLiteral("enableAnalysis"), settings.enableAnalysis);
        changed = true;
    }

    const double nextConfidence = std::clamp(settings.confidenceThreshold, 0.0, 1.0);
    double storedConfidence = nextConfidence;
    bool hasStoredConfidence = false;
    if (analysisParam.contains(QStringLiteral("confidence")))
    {
        const QJsonValue value = analysisParam.value(QStringLiteral("confidence"));
        if (value.isDouble())
        {
            storedConfidence = value.toDouble(nextConfidence);
            hasStoredConfidence = true;
        }
        else if (value.isString())
        {
            bool ok = false;
            const double converted = value.toString().toDouble(&ok);
            if (ok)
            {
                storedConfidence = converted;
                hasStoredConfidence = true;
            }
        }
    }

    if (!hasStoredConfidence || std::abs(storedConfidence - nextConfidence) > 1e-6)
    {
        analysisParam.insert(QStringLiteral("confidence"), nextConfidence);
        changed = true;
    }

    const QString nextPassLevel = settings.passLevel.isEmpty() ? QStringLiteral("Root") : settings.passLevel;
    const QString storedPassLevel = analysisParam.value(QStringLiteral("passLevel")).toString();
    if (!analysisParam.contains(QStringLiteral("passLevel")) ||
        storedPassLevel.compare(nextPassLevel, Qt::CaseInsensitive) != 0)
    {
        analysisParam.insert(QStringLiteral("passLevel"), nextPassLevel);
        changed = true;
    }

    const double nextTorch = std::clamp(settings.torchLengthMm, 0.0, 1000.0);
    double storedTorch = nextTorch;
    bool hasStoredTorch = false;
    if (analysisParam.contains(QStringLiteral("torchLength")))
    {
        const QJsonValue value = analysisParam.value(QStringLiteral("torchLength"));
        if (value.isDouble())
        {
            storedTorch = value.toDouble(nextTorch);
            hasStoredTorch = true;
        }
        else if (value.isString())
        {
            bool ok = false;
            const double converted = value.toString().toDouble(&ok);
            if (ok)
            {
                storedTorch = converted;
                hasStoredTorch = true;
            }
        }
    }

    if (!hasStoredTorch || std::abs(storedTorch - nextTorch) > 1e-6)
    {
        analysisParam.insert(QStringLiteral("torchLength"), nextTorch);
        changed = true;
    }

    const double nextDotSize = std::clamp(settings.detectionDotSizePx, 1.0, 64.0);
    double storedDotSize = nextDotSize;
    bool hasStoredDotSize = false;
    if (analysisParam.contains(QStringLiteral("dotSize")))
    {
        const QJsonValue value = analysisParam.value(QStringLiteral("dotSize"));
        if (value.isDouble())
        {
            storedDotSize = value.toDouble(nextDotSize);
            hasStoredDotSize = true;
        }
        else if (value.isString())
        {
            bool ok = false;
            const double converted = value.toString().toDouble(&ok);
            if (ok)
            {
                storedDotSize = converted;
                hasStoredDotSize = true;
            }
        }
    }

    if (!hasStoredDotSize || std::abs(storedDotSize - nextDotSize) > 1e-6)
    {
        analysisParam.insert(QStringLiteral("dotSize"), nextDotSize);
        changed = true;
    }

    if (!changed)
        return;

    settingsObj.insert(QStringLiteral("analysisParam"), analysisParam);
    root.insert(QStringLiteral("settings"), settingsObj);
    doc.setObject(root);
    if (!ConfigUtils::saveConfig(doc))
        qWarning() << "[SettingsPage] Failed to persist analysis parameters";
}

void SettingsPage::applyAiSettings()
{
    if (!m_aiClient)
        return;

    AiClient::Settings settings = m_aiClient->settings();
    settings.enableAnalysis = m_aiEnabledCheck && m_aiEnabledCheck->isChecked();
    if (m_aiModelEdit)
        settings.modelName = m_aiModelEdit->text();
    if (m_allowableEdit)
    {
        bool ok = false;
        const double value = m_allowableEdit->text().toDouble(&ok);
        if (ok)
            settings.confidenceThreshold = std::clamp(value, 0.0, 1.0);
    }
    if (m_passNumberSpin)
        settings.passNumber = m_passNumberSpin->value();
    settings.passLevel = m_passTypeCombo
                             ? m_passTypeCombo->currentData(Qt::UserRole).toString()
                             : QStringLiteral("Root");
    if (m_torchLengthEdit)
    {
        bool ok = false;
        const double value = m_torchLengthEdit->text().toDouble(&ok);
        if (ok)
            settings.torchLengthMm = std::clamp(value, 0.0, 1000.0);
    }
    m_aiClient->setSettings(settings);
}

void SettingsPage::applyComboPopupStyle(QComboBox *combo, int visibleRows)
{
    if (!combo)
        return;

    combo->setFixedHeight(40);
    combo->setStyleSheet(QStringLiteral(
        "QComboBox[role=\"settingsCombo\"] { color: rgba(255, 255, 255, 0.70); background-color: transparent; border: none; padding: 0 16px; font-size: 16px; font-weight: 600; min-height: 40px; height: 40px; }"
        "QComboBox[role=\"settingsCombo\"]::drop-down { border: none; background: transparent; width: 0; }"
        "QComboBox[role=\"settingsCombo\"]::down-arrow { image: none; width: 0; height: 0; }"
        "QComboBox[role=\"settingsCombo\"] QAbstractItemView { background-color: #111113; color: rgba(255, 255, 255, 0.70); border: 1px solid rgba(217, 217, 237, 0.9); font-size: 16px; font-weight: 600; padding: 0; margin: 0; }"
        "QComboBox[role=\"settingsCombo\"] QAbstractItemView::item { padding: 8px 16px; margin: 2px 0; }"
        "QComboBox[role=\"settingsCombo\"] QAbstractItemView::item:selected { background-color: rgba(0, 170, 120, 0.35); color: rgba(255, 255, 255, 0.90); }"
        "QComboBox[role=\"settingsCombo\"] QAbstractItemView::viewport { background-color: #111113; }"
        "QComboBox[role=\"settingsCombo\"] QAbstractItemView::up-arrow, QComboBox[role=\"settingsCombo\"] QAbstractItemView::down-arrow { image: none; width: 0; height: 0; }"));

    int rows = visibleRows;
    if (rows <= 0)
    {
        const int maxRows = combo->maxVisibleItems() > 0 ? combo->maxVisibleItems() : combo->count();
        rows = std::clamp(std::max(combo->count(), 1), 1, maxRows > 0 ? maxRows : 1);
    }
    else
    {
        combo->setMaxVisibleItems(rows);
    }
    combo->setMaxVisibleItems(rows);

    if (auto *view = qobject_cast<QListView *>(combo->view()))
    {
        view->setFrameShape(QFrame::NoFrame);
        view->setFrameShadow(QFrame::Plain);
        view->setSpacing(6);

        int rowHeight = view->sizeHintForRow(0);
        if (rowHeight <= 0)
            rowHeight = static_cast<int>(std::round(combo->fontMetrics().height() * 1.45));

        const int padding = 4;
        const int popupHeight = rowHeight * rows + view->spacing() * std::max(0, rows - 1) + padding * 2;
        view->setMinimumHeight(popupHeight);
        view->setMaximumHeight(popupHeight);
        view->setVerticalScrollBarPolicy(combo->count() > rows ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);

        view->setStyleSheet(QStringLiteral(
                                "QListView { padding: %1px 0; background-color: #111113; border: none; }"
                                "QListView::item { background-color: #111113; margin: 2px 0; padding: 8px 16px; }"
                                "QListView::item:selected { background-color: rgba(0, 170, 120, 0.35); color: rgba(255, 255, 255, 0.90); }"
                                "QScrollBar { width: 0px; height: 0px; }"
                                "QScrollBar::handle { background: transparent; }"
                                "QScrollBar::add-line, QScrollBar::sub-line { height: 0px; width: 0px; }"
                                "QAbstractItemView::up-arrow, QAbstractItemView::down-arrow { image: none; height: 0px; width: 0px; }")
                                .arg(padding));
    }

    if (auto *container = combo->view() ? combo->view()->window() : nullptr)
    {
        container->setStyleSheet(QStringLiteral(
            "QFrame { background-color: #111113; border: 1px solid rgba(217, 217, 237, 0.9); border-radius: 3px; }"));
    }
}

QWidget *SettingsPage::createComboFrame(QComboBox *combo, QWidget *parent, int visibleRows)
{
    if (!combo)
        return nullptr;

    auto *container = new QWidget(parent);
    configureInputFrame(container, combo->isEnabled());

    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    combo->setProperty("role", "settingsCombo");
    combo->setParent(container);
    combo->setFrame(false);
    combo->setMouseTracking(true);
    layout->addWidget(combo, 1);
    registerInputChild(combo, container);

    if (auto *lineEdit = combo->lineEdit())
    {
        lineEdit->setProperty("role", "settingsEdit");
        lineEdit->setFrame(false);
        lineEdit->setAttribute(Qt::WA_StyledBackground, false);
        lineEdit->setMouseTracking(true);
        registerInputChild(lineEdit, container);
    }

    applyComboPopupStyle(combo, visibleRows);

    return container;
}

QWidget *SettingsPage::createLineEditFrame(QLineEdit *edit, QWidget *parent, bool readOnly, const QString &suffixText, QLabel **suffixLabelOut)
{
    if (!edit)
        return nullptr;

    auto *container = new QWidget(parent);
    configureInputFrame(container, !readOnly);

    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    edit->setProperty("role", "settingsEdit");
    edit->setParent(container);
    edit->setReadOnly(readOnly);
    edit->setFrame(false);
    edit->setFixedHeight(40);
    edit->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    edit->setCursor(readOnly ? Qt::ArrowCursor : Qt::IBeamCursor);
    edit->setMouseTracking(true);
    const bool hasSuffix = !suffixText.isEmpty();
    edit->setTextMargins(16, 0, hasSuffix ? 6 : 16, 0);
    edit->setStyleSheet(QStringLiteral(
        "QLineEdit { color: rgba(255, 255, 255, 0.70); background-color: transparent; border: none; font-size: 16px; font-weight: 600; }"
        "QLineEdit:read-only { color: rgba(255, 255, 255, 0.55); }"
        "QLineEdit::selection { background-color: rgba(0, 170, 120, 0.35); }"));
    layout->addWidget(edit, 1, Qt::AlignVCenter);
    registerInputChild(edit, container);

    if (hasSuffix)
    {
        auto *suffixLabel = new QLabel(QStringLiteral(" %1").arg(suffixText), container);
        suffixLabel->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 0.55); font-size: 16px; font-weight: 600; padding: 0 16px 0 0;"));
        suffixLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        layout->addWidget(suffixLabel, 0, Qt::AlignVCenter);
        if (suffixLabelOut)
            *suffixLabelOut = suffixLabel;
        registerInputChild(suffixLabel, container);
    }

    return container;
}

void SettingsPage::registerInputChild(QWidget *child, QWidget *frame)
{
    if (!child || !frame)
        return;

    if (m_inputFrameChildren.contains(child))
        return;

    child->setAttribute(Qt::WA_Hover, true);
    child->setMouseTracking(true);
    child->installEventFilter(this);
    m_inputFrameChildren.insert(child, frame);
}

void SettingsPage::updateInputFrameHoverState(QWidget *frame)
{
    if (!frame)
        return;

    const int depth = m_inputFrameHoverDepth.value(frame, 0);
    const bool hoverEnabled = frame->property("hover-enabled").toBool();
    const bool active = hoverEnabled && depth > 0;
    frame->setProperty("hover-active", active);

    if (frame->style())
    {
        frame->style()->unpolish(frame);
        frame->style()->polish(frame);
    }
    frame->update();
}

void SettingsPage::configureInputFrame(QWidget *frame, bool editable)
{
    if (!frame)
        return;

    frame->setProperty("comboFrame", true);
    frame->setProperty("role", "inputFrame");
    frame->setProperty("hover-enabled", editable);
    frame->setProperty("editable", editable);
    frame->setProperty("hover-active", false);
    frame->setAttribute(Qt::WA_StyledBackground, true);
    frame->setAttribute(Qt::WA_Hover, true);
    frame->setMouseTracking(true);
    frame->setFixedHeight(40);
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    if (!m_inputFrames.contains(frame))
    {
        frame->installEventFilter(this);
        m_inputFrames.insert(frame);
        m_inputFrameHoverDepth.insert(frame, 0);
    }
    else
    {
        m_inputFrameHoverDepth[frame] = 0;
    }

    updateInputFrameHoverState(frame);
}

void SettingsPage::setInputFrameEditable(QWidget *frame, bool editable)
{
    if (!frame)
        return;

    frame->setProperty("hover-enabled", editable);
    frame->setProperty("editable", editable);
    updateInputFrameHoverState(frame);
}

void SettingsPage::updateAnalysisHintText()
{
    if (!m_analysisHintLabel)
        return;

    const QString rootTitle = tr("Root");
    const QString secondTitle = tr("Second");
    const QString rootDescription = tr("Tack seam recognition wire nozzle position control");
    const QString secondDescription = tr("Bead recognition wire nozzle position control");
    const QString lineFormat = tr("%1: %2");

    const auto makeHighlighted = [](const QString &text) {
        return QStringLiteral("<span style=\"color:%1;font-weight:700;\">%2</span>")
            .arg(QStringLiteral("#FFFFFF"), text.toHtmlEscaped());
    };

    const QString rootLine = lineFormat.arg(makeHighlighted(rootTitle), rootDescription.toHtmlEscaped());
    const QString secondLine = lineFormat.arg(makeHighlighted(secondTitle), secondDescription.toHtmlEscaped());
    m_analysisHintLabel->setText(rootLine + QStringLiteral("<br/>") + secondLine);
}

void SettingsPage::persistLanguagePreference(const QString &code) const
{
    const QString trimmedCode = code.trimmed();
    if (trimmedCode.isEmpty())
        return;

    QJsonDocument doc = ConfigUtils::loadConfig();
    if (!doc.isObject())
        doc = QJsonDocument(QJsonObject());

    QJsonObject root = doc.object();
    QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    const QString storedCode = settingsObj.value(QStringLiteral("language")).toString();
    if (storedCode == trimmedCode)
        return;

    settingsObj.insert(QStringLiteral("language"), trimmedCode);
    root.insert(QStringLiteral("settings"), settingsObj);

    if (!ConfigUtils::saveConfig(QJsonDocument(root)))
        qWarning() << "[SettingsPage] Failed to persist language preference";
}

void SettingsPage::updatePassUsageLabel()
{
    if (!m_passUsageLabel)
        return;

    QString level = QStringLiteral("Root");
    if (m_passTypeCombo)
        level = m_passTypeCombo->currentData(Qt::UserRole).toString();

    const bool isSecond = level.compare(QStringLiteral("Second"), Qt::CaseInsensitive) == 0;
    const QString text = isSecond ? tr("Bead") : tr("Seam, Tack");
    m_passUsageLabel->setText(text);
}

void SettingsPage::updateAnalysisControlState()
{
    const bool editable = !(m_aiEnabledCheck && m_aiEnabledCheck->isChecked());
    if (m_passTypeCombo)
        m_passTypeCombo->setEnabled(editable);
    setInputFrameEditable(m_passTypeFrame, editable);
    if (m_torchLengthEdit)
    {
        m_torchLengthEdit->setReadOnly(!editable);
        m_torchLengthEdit->setCursor(editable ? Qt::IBeamCursor : Qt::ArrowCursor);
        setInputFrameEditable(m_torchInputFrame, editable);
    }
    if (m_allowableEdit)
    {
        m_allowableEdit->setReadOnly(!editable);
        m_allowableEdit->setCursor(editable ? Qt::IBeamCursor : Qt::ArrowCursor);
        setInputFrameEditable(m_allowableInputFrame, editable);
    }
}

void SettingsPage::updateAiStatusButton()
{
    const bool enabled = m_aiEnabledCheck && m_aiEnabledCheck->isChecked();
    const QColor color = cameraConnectivityColor(enabled);
    const QString colorText = rgbaString(color);

    if (m_aiStatusButton)
    {
        m_aiStatusButton->setText(enabled ? QStringLiteral("✓") : QStringLiteral("✕"));
        m_aiStatusButton->setStyleSheet(QStringLiteral(
            "QPushButton { color: %1; font-size: 20px; font-weight: 700; border: none; background: transparent; }")
                                            .arg(colorText));
        m_aiStatusButton->setToolTip(enabled ? tr("Active") : tr("Inactive"));
    }

    if (m_passUsageLabel)
        m_passUsageLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 16px; font-weight: 600;")
                                            .arg(colorText));


    updateAnalysisControlState();
}

void SettingsPage::loadPlcSettings()
{
    m_plcEntries.clear();

    const QJsonDocument doc = ConfigUtils::loadConfig();
    const QJsonObject root = doc.object();
    const QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    const QJsonObject plcObj = settingsObj.value(QStringLiteral("plc")).toObject();

    const bool hadExistingEntries = !plcObj.isEmpty();

    if (!hadExistingEntries)
    {
        PlcEntry entry;
        entry.id = QStringLiteral("PLC1");
        entry.name = QStringLiteral("PLC 1");
        entry.ip = QStringLiteral("192.168.0.10");
        entry.port = 502;
        m_plcEntries.append(entry);
    }
    else
    {
        for (auto it = plcObj.constBegin(); it != plcObj.constEnd(); ++it)
        {
            PlcEntry entry;
            entry.id = it.key();
            const QJsonObject obj = it.value().toObject();
            entry.name = obj.value(QStringLiteral("name")).toString(entry.id);
            entry.ip = obj.value(QStringLiteral("ip")).toString();
            entry.port = static_cast<quint16>(obj.value(QStringLiteral("port")).toInt());
            m_plcEntries.append(entry);
        }
    }

    rebuildPlcRows();

    if (!hadExistingEntries)
        savePlcSettings();

    updateGlobalBusyIndicator();
}

void SettingsPage::savePlcSettings() const
{
    QJsonDocument doc = ConfigUtils::loadConfig();
    if (!doc.isObject())
        doc = QJsonDocument(QJsonObject());

    QJsonObject root = doc.object();
    QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    QJsonObject plcObj;

    for (const auto &entry : m_plcEntries)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), entry.name);
        obj.insert(QStringLiteral("ip"), entry.ip);
        obj.insert(QStringLiteral("port"), static_cast<int>(entry.port));
        plcObj.insert(entry.id, obj);
    }

    settingsObj.insert(QStringLiteral("plc"), plcObj);
    root.insert(QStringLiteral("settings"), settingsObj);
    doc.setObject(root);

    if (!ConfigUtils::saveConfig(doc))
        qWarning() << "Failed to save PLC settings";
}

void SettingsPage::loadCameraSettings()
{
    m_cameraRoiProfiles.clear();
    m_roiAliasIndex.clear();
    m_cameraRoiDirty = false;

    const QJsonDocument doc = ConfigUtils::loadConfig();
    if (!doc.isObject())
        return;

    const QJsonObject root = doc.object();
    const QJsonObject globalCameraMeta = root.value(QStringLiteral("cameras")).toObject();
    const QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();
    const QJsonArray profilesArray = settingsObj.value(QStringLiteral("cameraProfiles")).toArray();

    auto readRoi = [this](const QJsonObject &roiObj) -> QRectF {
        if (roiObj.isEmpty())
            return QRectF();
        const double x = roiObj.value(QStringLiteral("x")).toDouble(-1.0);
        const double y = roiObj.value(QStringLiteral("y")).toDouble(-1.0);
        const double width = roiObj.value(QStringLiteral("width")).toDouble(-1.0);
        const double height = roiObj.value(QStringLiteral("height")).toDouble(-1.0);
        return sanitizeNormalizedRect(QRectF(x, y, width, height));
    };

    m_cameraSerialSelection.clear();

    auto appendProfile = [&](const QString &id,
                             const QString &slotId,
                             const QString &name,
                             const QString &alias,
                             const QRectF &roi,
                             const QJsonObject &analysis,
                             const QString &serialPort,
                             bool enabled,
                             bool hadLegacyDetail) {
        CameraRoiProfile profile;
        profile.id = id;
        profile.slotId = slotId;
        profile.name = name;
        QString preferredAlias = alias.trimmed();
        if (preferredAlias.isEmpty())
            preferredAlias = !name.trimmed().isEmpty() ? name : (!slotId.trimmed().isEmpty() ? slotId : id);
        const QString unique = uniqueAlias(preferredAlias);
        profile.alias = unique;
        if (unique.compare(preferredAlias, Qt::CaseInsensitive) != 0)
            m_cameraRoiDirty = true;
        profile.roi = roi;
        profile.analysis = analysis;
        profile.serialPort = serialPort;
        profile.enabled = enabled;
        if (hadLegacyDetail)
            m_cameraRoiDirty = true;
        m_cameraRoiProfiles.append(profile);
    };

    bool legacyDetected = false;

    if (!profilesArray.isEmpty())
    {
        for (const QJsonValue &value : profilesArray)
        {
            const QJsonObject obj = value.toObject();
            const QString id = obj.value(QStringLiteral("id")).toString();
            if (id.isEmpty())
                continue;
            const QRectF roi = readRoi(obj.value(QStringLiteral("roi")).toObject());
            const QString slotId = obj.value(QStringLiteral("slotId")).toString();
            const QString name = obj.value(QStringLiteral("name")).toString();
            const QString alias = obj.value(QStringLiteral("alias")).toString();
            const QJsonObject analysis = obj.value(QStringLiteral("analysis")).toObject();
            const QJsonObject legacyDetail = obj.value(QStringLiteral("detail")).toObject();
            QString serialPort = obj.value(QStringLiteral("serialPort")).toString().trimmed();
            if (serialPort.isEmpty())
                serialPort = legacyDetail.value(QStringLiteral("serialPort")).toString().trimmed();
            if (serialPort.isEmpty())
                serialPort = analysis.value(QStringLiteral("serialPort")).toString().trimmed();
            if (serialPort.isEmpty())
            {
                const QJsonObject meta = globalCameraMeta.value(id).toObject();
                serialPort = meta.value(QStringLiteral("serialPort")).toString().trimmed();
            }
            bool enabled = true;
            if (obj.contains(QStringLiteral("enabled")))
                enabled = obj.value(QStringLiteral("enabled")).toBool(true);
            else if (obj.contains(QStringLiteral("enable")))
                enabled = obj.value(QStringLiteral("enable")).toBool(true);
            const bool hadLegacyDetail = obj.contains(QStringLiteral("detail"));
            appendProfile(id, slotId, name, alias, roi, analysis, serialPort, enabled, hadLegacyDetail);
        }
    }
    else
    {
        const QJsonObject legacyObj = settingsObj.value(QStringLiteral("camera")).toObject();
        if (!legacyObj.isEmpty())
            legacyDetected = true;

        for (auto it = legacyObj.constBegin(); it != legacyObj.constEnd(); ++it)
        {
            const QString id = it.key();
            const QJsonObject cameraEntry = it.value().toObject();
            const QRectF roi = readRoi(cameraEntry.value(QStringLiteral("roi")).toObject());

            QString slotId = cameraEntry.value(QStringLiteral("slotId")).toString();
            QString name = cameraEntry.value(QStringLiteral("name")).toString();
            QString alias = cameraEntry.value(QStringLiteral("alias")).toString();
            const QJsonObject meta = globalCameraMeta.value(id).toObject();
            if (slotId.isEmpty())
                slotId = meta.value(QStringLiteral("slotId")).toString();
            const QString originalName = meta.value(QStringLiteral("name")).toString();
            if (!originalName.isEmpty())
                name = originalName;
            else if (name.isEmpty())
                name = id;
            if (alias.isEmpty())
                alias = cameraEntry.value(QStringLiteral("name")).toString();
            if (alias.isEmpty())
                alias = name.isEmpty() ? slotId : name;

            QString serialPort = cameraEntry.value(QStringLiteral("serialPort")).toString().trimmed();
            if (serialPort.isEmpty())
                serialPort = meta.value(QStringLiteral("serialPort")).toString().trimmed();

            appendProfile(id, slotId, name, alias, roi, QJsonObject(), serialPort, true, false);
        }
    }

    rebuildCameraRoiAliasIndex();

    if (m_cameraManager)
    {
        const auto cameras = m_cameraManager->cameras();
        for (const auto &camera : cameras)
            applyStoredCameraVisibility(camera);
    }

    if (legacyDetected)
        m_cameraRoiDirty = true;

    for (const CameraRoiProfile &profile : std::as_const(m_cameraRoiProfiles))
    {
        if (!profile.id.isEmpty())
            m_cameraSerialSelection.insert(profile.id, profile.serialPort);
    }
}

void SettingsPage::saveCameraSettings()
{
    if (!m_cameraRoiDirty)
        return;

    QJsonDocument doc = ConfigUtils::loadConfig();
    if (!doc.isObject())
        doc = QJsonDocument(QJsonObject());

    QJsonObject root = doc.object();
    QJsonObject settingsObj = root.value(QStringLiteral("settings")).toObject();

    auto writeRoi = [](const QRectF &rect) {
        QJsonObject roi;
        roi.insert(QStringLiteral("x"), rect.x());
        roi.insert(QStringLiteral("y"), rect.y());
        roi.insert(QStringLiteral("width"), rect.width());
        roi.insert(QStringLiteral("height"), rect.height());
        return roi;
    };

    QJsonArray profileArray;
    for (const CameraRoiProfile &profile : std::as_const(m_cameraRoiProfiles))
    {
        QJsonObject obj;
        if (!profile.id.isEmpty())
            obj.insert(QStringLiteral("id"), profile.id);
        if (!profile.slotId.isEmpty())
            obj.insert(QStringLiteral("slotId"), profile.slotId);
        if (!profile.name.isEmpty())
            obj.insert(QStringLiteral("name"), profile.name);
        if (!profile.alias.isEmpty())
            obj.insert(QStringLiteral("alias"), profile.alias);
        obj.insert(QStringLiteral("enabled"), profile.enabled);
        if (profile.roi.isValid())
            obj.insert(QStringLiteral("roi"), writeRoi(profile.roi));
        if (!profile.analysis.isEmpty())
            obj.insert(QStringLiteral("analysis"), profile.analysis);
        if (!profile.serialPort.isEmpty())
            obj.insert(QStringLiteral("serialPort"), profile.serialPort);
        profileArray.append(obj);
    }

    settingsObj.insert(QStringLiteral("cameraProfiles"), profileArray);
    settingsObj.remove(QStringLiteral("camera"));
    root.insert(QStringLiteral("settings"), settingsObj);
    doc.setObject(root);

    if (!ConfigUtils::saveConfig(doc))
    {
        qWarning() << "Failed to save camera ROI settings";
    }
    else
    {
        m_cameraRoiDirty = false;
    }
}

QString SettingsPage::cameraAliasKey(const QString &prefix, const QString &value) const
{
    const QString normalized = canonicalString(value);
    if (normalized.isEmpty())
        return QString();
    return prefix + QLatin1Char(':') + normalized;
}

void SettingsPage::rebuildCameraRoiAliasIndex()
{
    m_roiAliasIndex.clear();
    for (int i = 0; i < m_cameraRoiProfiles.size(); ++i)
    {
        const auto &profile = m_cameraRoiProfiles.at(i);
        auto addAlias = [&](const QString &prefix, const QString &value) {
            const QString key = cameraAliasKey(prefix, value);
            if (key.isEmpty())
                return;
            m_roiAliasIndex.insert(key, i);
        };
        addAlias(QStringLiteral("id"), profile.id);
        addAlias(QStringLiteral("slot"), profile.slotId);
        addAlias(QStringLiteral("name"), profile.name);
        addAlias(QStringLiteral("alias"), profile.alias);
    }
}

QRectF SettingsPage::sanitizeNormalizedRect(const QRectF &rect) const
{
    if (!rect.isValid())
        return QRectF();

    QRectF normalized = rect.normalized();
    const auto clamp = [](double value) {
        if (value < 0.0)
            return 0.0;
        if (value > 1.0)
            return 1.0;
        return value;
    };
    double width = std::clamp(normalized.width(), 0.0, 1.0);
    double height = std::clamp(normalized.height(), 0.0, 1.0);
    double x = clamp(normalized.x());
    double y = clamp(normalized.y());
    if (x + width > 1.0)
        x = 1.0 - width;
    if (y + height > 1.0)
        y = 1.0 - height;
    if (width <= 0.0 || height <= 0.0)
        return QRectF();
    return QRectF(x, y, width, height);
}

int SettingsPage::findCameraProfile(const CameraManager::CameraInfo &info) const
{
    const auto lookup = [&](const QString &prefix, const QString &value) -> int {
        const QString key = cameraAliasKey(prefix, value);
        if (key.isEmpty())
            return -1;
        const auto it = m_roiAliasIndex.constFind(key);
        return it != m_roiAliasIndex.constEnd() ? it.value() : -1;
    };

    if (!info.id.isEmpty())
    {
        const int idx = lookup(QStringLiteral("id"), info.id);
        if (idx >= 0)
            return idx;
    }
    if (!info.slotId.isEmpty())
    {
        const int idx = lookup(QStringLiteral("slot"), info.slotId);
        if (idx >= 0)
            return idx;
    }
    if (!info.alias.isEmpty())
    {
        const int idx = lookup(QStringLiteral("alias"), info.alias);
        if (idx >= 0)
            return idx;
    }
    if (!info.name.isEmpty())
    {
        int idx = lookup(QStringLiteral("name"), info.name);
        if (idx >= 0)
            return idx;
        idx = lookup(QStringLiteral("alias"), info.name);
        if (idx >= 0)
            return idx;
    }
    if (!info.slotId.isEmpty())
    {
        int idx = lookup(QStringLiteral("alias"), info.slotId);
        if (idx >= 0)
            return idx;
    }
    if (!info.id.isEmpty())
    {
        const int idx = lookup(QStringLiteral("alias"), info.id);
        if (idx >= 0)
            return idx;
    }
    return -1;
}

int SettingsPage::findCameraProfileByAlias(const QString &alias) const
{
    const QString key = cameraAliasKey(QStringLiteral("alias"), alias);
    if (key.isEmpty())
        return -1;
    const auto it = m_roiAliasIndex.constFind(key);
    return it != m_roiAliasIndex.constEnd() ? it.value() : -1;
}

QString SettingsPage::uniqueAlias(const QString &base, int excludeIndex) const
{
    QString seed = base.trimmed();
    if (seed.isEmpty())
        seed = QStringLiteral("Camera");

    for (QChar &ch : seed)
    {
        if (ch.isSpace())
            ch = QLatin1Char(' ');
    }

    auto conflict = [&](const QString &candidate) {
        const QString candidateKey = canonicalString(candidate);
        for (int i = 0; i < m_cameraRoiProfiles.size(); ++i)
        {
            if (i == excludeIndex)
                continue;
            const QString &existing = m_cameraRoiProfiles.at(i).alias;
            if (existing.isEmpty())
                continue;
            const QString existingKey = canonicalString(existing);
            if (!existingKey.isEmpty() && existingKey == candidateKey)
                return true;
        }
        return false;
    };

    QString candidate = seed;
    int suffix = 1;
    while (conflict(candidate))
    {
        ++suffix;
        candidate = QStringLiteral("%1-%2").arg(seed, QString::number(suffix));
    }
    return candidate;
}

QString SettingsPage::displayNameForCamera(const CameraManager::CameraInfo &info) const
{
    int index = findCameraProfile(info);
    if (index >= 0 && index < m_cameraRoiProfiles.size())
    {
        const QString alias = m_cameraRoiProfiles.at(index).alias.trimmed();
        if (!alias.isEmpty())
            return alias;
    }

    if (!info.alias.trimmed().isEmpty())
        return info.alias.trimmed();
    if (!info.name.trimmed().isEmpty())
        return info.name;
    if (!info.slotId.trimmed().isEmpty())
        return info.slotId;
    return info.id;
}

QString SettingsPage::serialPortForCamera(const CameraManager::CameraInfo &info) const
{
    auto normalized = [](const QString &value) {
        const QString trimmed = value.trimmed();
        return trimmed;
    };

    const QString direct = normalized(m_cameraSerialSelection.value(info.id));
    if (!direct.isEmpty())
        return direct;

    const int profileIndex = findCameraProfile(info);
    if (profileIndex >= 0 && profileIndex < m_cameraRoiProfiles.size())
    {
        const QString profilePort = normalized(m_cameraRoiProfiles.at(profileIndex).serialPort);
        if (!profilePort.isEmpty())
            return profilePort;
    }

    if (!info.slotId.isEmpty())
    {
        const QString slot = info.slotId.trimmed();
        for (const auto &profile : m_cameraRoiProfiles)
        {
            if (profile.serialPort.trimmed().isEmpty())
                continue;
            if (profile.slotId.compare(slot, Qt::CaseInsensitive) == 0)
                return profile.serialPort.trimmed();
        }
    }

    if (!info.alias.isEmpty())
    {
        const QString alias = info.alias.trimmed();
        for (const auto &profile : m_cameraRoiProfiles)
        {
            if (profile.serialPort.trimmed().isEmpty())
                continue;
            if (profile.alias.compare(alias, Qt::CaseInsensitive) == 0)
                return profile.serialPort.trimmed();
        }
    }

    if (!info.name.isEmpty())
    {
        const QString name = info.name.trimmed();
        for (const auto &profile : m_cameraRoiProfiles)
        {
            if (profile.serialPort.trimmed().isEmpty())
                continue;
            if (profile.name.compare(name, Qt::CaseInsensitive) == 0)
                return profile.serialPort.trimmed();
        }
    }

    return QString();
}

void SettingsPage::applyStoredCameraVisibility(const CameraManager::CameraInfo &info)
{
    if (!m_cameraManager)
        return;

    int profileIndex = findCameraProfile(info);
    if (profileIndex < 0 && !info.alias.isEmpty())
        profileIndex = findCameraProfileByAlias(info.alias);
    if (profileIndex < 0 || profileIndex >= m_cameraRoiProfiles.size())
        return;

    const bool desiredVisible = m_cameraRoiProfiles.at(profileIndex).enabled;
    if (m_cameraManager->isCameraVisible(info.id) == desiredVisible)
        return;

    m_cameraManager->setCameraVisibility(info.id, desiredVisible);
}

void SettingsPage::updateCameraProfileVisibility(const CameraManager::CameraInfo &info, bool visible)
{
    auto aliasSeed = [&]() -> QString {
        const QString trimmedAlias = info.alias.trimmed();
        if (!trimmedAlias.isEmpty())
            return trimmedAlias;
        if (!info.name.trimmed().isEmpty())
            return info.name.trimmed();
        if (!info.slotId.trimmed().isEmpty())
            return info.slotId.trimmed();
        return info.id.trimmed();
    };

    int profileIndex = findCameraProfile(info);
    if (profileIndex < 0 && !info.alias.isEmpty())
        profileIndex = findCameraProfileByAlias(info.alias);

    bool aliasIndexNeedsRebuild = false;
    bool changed = false;

    if (profileIndex < 0)
    {
        CameraRoiProfile profile;
        profile.id = info.id;
        profile.slotId = info.slotId;
        profile.name = info.name;
        profile.alias = uniqueAlias(aliasSeed());
        profile.roi = QRectF();
        profile.analysis = QJsonObject();
        profile.serialPort = QString();
        profile.enabled = visible;
        m_cameraRoiProfiles.append(profile);
        aliasIndexNeedsRebuild = true;
        changed = true;
    }
    else
    {
        CameraRoiProfile &profile = m_cameraRoiProfiles[profileIndex];
        if (!info.id.isEmpty() && profile.id != info.id)
        {
            profile.id = info.id;
            aliasIndexNeedsRebuild = true;
            changed = true;
        }
        if (profile.slotId != info.slotId)
        {
            profile.slotId = info.slotId;
            aliasIndexNeedsRebuild = true;
            changed = true;
        }
        if (profile.name != info.name)
        {
            profile.name = info.name;
            aliasIndexNeedsRebuild = true;
            changed = true;
        }
        if (profile.alias.isEmpty())
        {
            profile.alias = uniqueAlias(aliasSeed(), profileIndex);
            aliasIndexNeedsRebuild = true;
            changed = true;
        }
        if (profile.enabled != visible)
        {
            profile.enabled = visible;
            changed = true;
        }
    }

    if (aliasIndexNeedsRebuild)
        rebuildCameraRoiAliasIndex();

    if (changed)
    {
        m_cameraRoiDirty = true;
        saveCameraSettings();
    }
}

void SettingsPage::rebuildPlcRows()
{
    if (!m_plcRowsLayout)
        return;

    while (QLayoutItem *item = m_plcRowsLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_plcRows.clear();

    if (m_plcEntries.isEmpty())
    {
        auto *placeholder = createSettingsRowContainer(this);
        placeholder->setFixedWidth(960);
        auto *layout = new QHBoxLayout(placeholder);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *label = new QLabel(tr("No PLC connections configured."), placeholder);
        label->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 0.55); font-size: 14px;"));
        layout->addStretch();
        layout->addWidget(label);
        layout->addStretch();
        m_plcRowsLayout->addWidget(placeholder);
        return;
    }

    for (int index = 0; index < m_plcEntries.size(); ++index)
    {
        const auto &entry = m_plcEntries.at(index);

        PlcRow row;
        row.id = entry.id;
        row.container = createSettingsRowContainer(this);
        row.container->setFixedWidth(960);
        row.container->setStyleSheet(QStringLiteral("background-color: #212225;"));

        auto *layout = new QHBoxLayout(row.container);
        layout->setContentsMargins(20, 12, 20, 12);
        layout->setSpacing(16);

        row.statusButton = new QPushButton(row.container);
        row.statusButton->setProperty("role", "statusButton");
        row.statusButton->setFlat(true);
        layout->addWidget(row.statusButton);

        row.nameLabel = new QLabel(entry.name, row.container);
        row.nameLabel->setMinimumWidth(120);
        layout->addWidget(row.nameLabel);
        auto *plcNameDivider = createVerticalDivider(row.container);
        layout->addWidget(plcNameDivider);
        layout->setAlignment(plcNameDivider, Qt::AlignVCenter);

        auto createField = [&](const QString &labelText, QWidget *editor, QWidget *&frameOut, int stretch) -> QLabel * {
            auto *field = new QWidget(row.container);
            auto *fieldLayout = new QHBoxLayout(field);
            fieldLayout->setContentsMargins(12, 0, 12, 0);
            fieldLayout->setSpacing(6);
            auto *label = new QLabel(labelText, field);
            label->setProperty("role", "inlineLabel");
            label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            fieldLayout->addWidget(label, 0, Qt::AlignVCenter);

            QWidget *widgetToAdd = editor;
            if (auto *combo = qobject_cast<QComboBox *>(editor))
            {
                int visible = combo->maxVisibleItems();
                if (visible <= 0)
                    visible = -1;
                widgetToAdd = createComboFrame(combo, field, visible);
            }
            else if (auto *lineEdit = qobject_cast<QLineEdit *>(editor))
            {
                widgetToAdd = createLineEditFrame(lineEdit, field, lineEdit->isReadOnly());
            }

            if (widgetToAdd)
                fieldLayout->addWidget(widgetToAdd, stretch > 0 ? 1 : 0, Qt::AlignVCenter);

            if (stretch > 0)
            {
                field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                layout->addWidget(field, stretch);
            }
            else
            {
                layout->addWidget(field);
            }
            frameOut = widgetToAdd;
            return label;
        };

        row.ipEdit = new QLineEdit(row.container);
        row.ipEdit->setProperty("role", "settingsEdit");
        row.ipEdit->setPlaceholderText(tr("e.g., 192.168.0.10"));
        row.ipLabel = createField(tr("IP"), row.ipEdit, row.ipFrame, 2);

        row.portEdit = new QLineEdit(row.container);
        row.portEdit->setProperty("role", "settingsEdit");
        row.portEdit->setPlaceholderText(tr("e.g., 502"));
        row.portEdit->setMaximumWidth(120);
        row.portLabel = createField(tr("Port"), row.portEdit, row.portFrame, 1);

        const int rowIndex = index;
        connect(row.ipEdit, &QLineEdit::editingFinished, this, [this, rowIndex]() {
            handlePlcFieldEdited(rowIndex);
        });
        connect(row.portEdit, &QLineEdit::editingFinished, this, [this, rowIndex]() {
            handlePlcFieldEdited(rowIndex);
        });
        connect(row.statusButton, &QPushButton::clicked, this, [this, rowIndex]() {
            if (rowIndex < 0 || rowIndex >= m_plcEntries.size())
                return;
            const auto &entry = m_plcEntries.at(rowIndex);
            if (entry.busy)
                return;
            if (entry.connected)
                handlePlcDisconnectRequested(rowIndex);
            else
                handlePlcConnectRequested(rowIndex);
        });

        m_plcRows.append(row);
        m_plcRowsLayout->addWidget(row.container);
        refreshPlcRow(rowIndex);
    }
}

void SettingsPage::refreshPlcRow(int index)
{
    if (index < 0 || index >= m_plcRows.size())
        return;

    PlcRow &row = m_plcRows[index];
    PlcEntry &entry = m_plcEntries[index];

    if (row.nameLabel)
        row.nameLabel->setText(entry.name);
    if (row.ipEdit)
        row.ipEdit->setText(entry.ip);
    if (row.portEdit)
        row.portEdit->setText(entry.port > 0 ? QString::number(entry.port) : QString());

    const bool ipValid = validateIpAddress(entry.ip);
    const bool portValid = entry.port > 0;

    if (row.ipEdit)
    {
        row.ipEdit->setProperty("inputError", !ipValid);
        row.ipEdit->style()->unpolish(row.ipEdit);
        row.ipEdit->style()->polish(row.ipEdit);
    }
    if (row.portEdit)
    {
        row.portEdit->setProperty("inputError", !portValid);
        row.portEdit->style()->unpolish(row.portEdit);
        row.portEdit->style()->polish(row.portEdit);
    }

    setInputFrameEditable(row.ipFrame, row.ipEdit && row.ipEdit->isEnabled() && !row.ipEdit->isReadOnly());
    setInputFrameEditable(row.portFrame, row.portEdit && row.portEdit->isEnabled() && !row.portEdit->isReadOnly());

    updatePlcRowStatusIndicator(index);
}

void SettingsPage::updatePlcRowStatusIndicator(int index)
{
    if (index < 0 || index >= m_plcRows.size())
        return;

    const PlcEntry &entry = m_plcEntries.at(index);
    PlcRow &row = m_plcRows[index];

    const QColor color = cameraConnectivityColor(entry.connected);
    const QString colorText = rgbaString(color);

    if (row.statusButton)
    {
        row.statusButton->setText(entry.connected ? QStringLiteral("✓") : QStringLiteral("✕"));
        row.statusButton->setStyleSheet(QStringLiteral(
            "QPushButton { color: %1; font-size: 20px; font-weight: 700; border: none; background: transparent; }")
                                              .arg(colorText));
        row.statusButton->setEnabled(!entry.busy);
        if (entry.busy)
            row.statusButton->setToolTip(tr("Processing..."));
        else if (entry.connected)
            row.statusButton->setToolTip(tr("Click to disconnect"));
        else
            row.statusButton->setToolTip(tr("Click to connect"));
    }

    if (row.nameLabel)
        row.nameLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 16px; font-weight: 600;")
                                         .arg(colorText));
}

void SettingsPage::setPlcBusy(int index, bool busy)
{
    if (index < 0 || index >= m_plcEntries.size())
        return;

    PlcEntry &entry = m_plcEntries[index];
    if (entry.busy == busy)
        return;

    entry.busy = busy;
    refreshPlcRow(index);
    updateGlobalBusyIndicator();
}

void SettingsPage::setAnalysisBusy(bool busy)
{
    if (m_analysisBusy == busy)
        return;

    m_analysisBusy = busy;
    updateGlobalBusyIndicator();
}

int SettingsPage::plcIndexById(const QString &id) const
{
    for (int i = 0; i < m_plcEntries.size(); ++i)
    {
        if (m_plcEntries.at(i).id == id)
            return i;
    }
    return -1;
}

bool SettingsPage::validateIpAddress(const QString &address) const
{
    QHostAddress ip;
    return !address.isEmpty() && ip.setAddress(address);
}

bool SettingsPage::validatePortValue(const QString &value) const
{
    bool ok = false;
    const int port = value.toInt(&ok);
    return ok && port > 0 && port <= 65535;
}

void SettingsPage::handlePlcFieldEdited(int index)
{
    if (index < 0 || index >= m_plcEntries.size())
        return;

    PlcEntry &entry = m_plcEntries[index];
    PlcRow &row = m_plcRows[index];

    const QString ipText = row.ipEdit ? row.ipEdit->text().trimmed() : QString();
    const QString portText = row.portEdit ? row.portEdit->text().trimmed() : QString();

    const bool ipValid = validateIpAddress(ipText);
    const bool portValid = validatePortValue(portText);

    entry.ip = ipText;
    entry.port = portValid ? static_cast<quint16>(portText.toUShort()) : 0;

    if (row.ipEdit)
    {
        row.ipEdit->setProperty("inputError", !ipValid);
        row.ipEdit->style()->unpolish(row.ipEdit);
        row.ipEdit->style()->polish(row.ipEdit);
    }
    if (row.portEdit)
    {
        row.portEdit->setProperty("inputError", !portValid);
        row.portEdit->style()->unpolish(row.portEdit);
        row.portEdit->style()->polish(row.portEdit);
    }

    if (ipValid && portValid)
        savePlcSettings();

    refreshPlcRow(index);
}

void SettingsPage::handlePlcConnectRequested(int index)
{
    if (!m_plcClient || index < 0 || index >= m_plcEntries.size())
        return;

    handlePlcFieldEdited(index);

    const PlcEntry &entry = m_plcEntries.at(index);
    if (!validateIpAddress(entry.ip) || entry.port == 0)
    {
        QMessageBox::warning(this, tr("Invalid PLC Configuration"), tr("Please enter a valid IP address and port."));
        return;
    }

    setPlcBusy(index, true);

    PLCClient::ConnectionInfo info;
    info.id = entry.id;
    info.ipAddress = entry.ip;
    info.port = entry.port;
    m_plcClient->connectToController(info);
}

void SettingsPage::handlePlcDisconnectRequested(int index)
{
    if (!m_plcClient || index < 0 || index >= m_plcEntries.size())
        return;

    if (!m_plcEntries.at(index).connected)
        return;

    setPlcBusy(index, true);
    m_plcClient->disconnectFromController(m_plcEntries.at(index).id);
}

void SettingsPage::handlePlcConnectionStateChanged(const QString &id, PLCClient::State state, const QString &message)
{
    const int index = plcIndexById(id);
    if (index < 0)
        return;

    PlcEntry &entry = m_plcEntries[index];

    switch (state)
    {
    case PLCClient::State::Connecting:
        entry.busy = true;
        break;
    case PLCClient::State::Disconnecting:
        entry.busy = true;
        updateGlobalBusyIndicator();
        break;
    case PLCClient::State::Connected:
        entry.busy = false;
        entry.connected = true;
        // disconnect other entries
        for (int i = 0; i < m_plcEntries.size(); ++i)
        {
            if (i == index)
                continue;
            m_plcEntries[i].connected = false;
            m_plcEntries[i].busy = false;
            refreshPlcRow(i);
        }
        break;
    case PLCClient::State::Disconnected:
        entry.busy = false;
        entry.connected = false;
        break;
    case PLCClient::State::Error:
        entry.busy = false;
        entry.connected = false;
        if (!message.isEmpty())
            QMessageBox::critical(this, tr("PLC Connection Error"), message);
        break;
    }

    refreshPlcRow(index);
    updateGlobalBusyIndicator();
}

void SettingsPage::updateGlobalBusyIndicator()
{
    if (!m_busyOverlay || !m_globalSpinner)
        return;

    const bool plcBusy = std::any_of(m_plcEntries.begin(), m_plcEntries.end(), [](const PlcEntry &entry) {
        return entry.busy;
    });
    const bool anyBusy = plcBusy || m_analysisBusy;

    if (anyBusy)
    {
        if (!m_busyOverlay->isVisible())
        {
            m_busyOverlay->setGeometry(rect());
            m_busyOverlay->raise();
            m_busyOverlay->show();
        }
        else
        {
            m_busyOverlay->raise();
        }
        if (!m_globalSpinner->isSpinning())
            m_globalSpinner->start();
    }
    else
    {
        m_busyOverlay->hide();
        m_globalSpinner->stop();
    }
}

void SettingsPage::showRoiEditor(const QString &cameraId, const QString &cameraAlias)
{
    if (!m_roiOverlay || !m_roiEditor)
        return;

    m_activeRoiCameraId = cameraId;
    m_activeRoiAlias = cameraAlias.trimmed();
    m_waitingForStoredRoi = false;
    m_expectedRoiRect = QRectF();

    const bool showLegend = ConfigUtils::showLegend();
    if (m_roiEditor)
        m_roiEditor->setShowLegend(showLegend);

    CameraManager::CameraInfo info;
    if (m_cameraManager && !cameraId.isEmpty())
    {
        info = m_cameraManager->camera(cameraId);
    }

    int profileIndex = -1;
    if (m_cameraManager && !info.id.isEmpty())
        profileIndex = findCameraProfile(info);
    if (profileIndex < 0 && !m_activeRoiAlias.isEmpty())
        profileIndex = findCameraProfileByAlias(m_activeRoiAlias);
    if (profileIndex >= 0 && profileIndex < m_cameraRoiProfiles.size() && m_activeRoiAlias.isEmpty())
        m_activeRoiAlias = m_cameraRoiProfiles.at(profileIndex).alias.trimmed();

    QString cameraDisplayName;
    if (profileIndex >= 0 && profileIndex < m_cameraRoiProfiles.size())
        cameraDisplayName = m_cameraRoiProfiles.at(profileIndex).alias.trimmed();
    if (cameraDisplayName.isEmpty())
        cameraDisplayName = displayNameForCamera(info);
    if (cameraDisplayName.isEmpty())
        cameraDisplayName = m_activeRoiAlias;

    if (m_roiTitleLabel)
        m_roiTitleLabel->setText(tr("ROI Settings"));
    if (m_roiCameraLabel)
    {
        if (cameraDisplayName.isEmpty())
            m_roiCameraLabel->setText(tr("Camera unavailable"));
        else
            m_roiCameraLabel->setText(tr("Camera: %1").arg(cameraDisplayName));
    }
    if (m_roiCloseButton)
        m_roiCloseButton->setText(tr("Close"));

    m_activeRoiProfileIndex = profileIndex;

    m_roiEditor->setCamera(m_cameraManager, cameraId);
    if (m_activeRoiProfileIndex >= 0 && m_activeRoiProfileIndex < m_cameraRoiProfiles.size())
    {
        const QRectF storedRoi = m_cameraRoiProfiles.at(m_activeRoiProfileIndex).roi;
        if (storedRoi.isValid())
        {
            m_expectedRoiRect = storedRoi;
            m_waitingForStoredRoi = true;
            QTimer::singleShot(0, this, [this, storedRoi]() {
                if (m_roiEditor)
                    m_roiEditor->applyNormalizedRoi(storedRoi);
            });
        }
        else
        {
            m_roiEditor->resetRoi();
        }
    }
    else
    {
        m_roiEditor->resetRoi();
    }
    m_roiOverlay->setGeometry(rect());
    m_roiOverlay->show();
    m_roiOverlay->raise();
}

void SettingsPage::handleRoiNormalizedChanged(const QRectF &rect)
{
    const bool hasCameraId = !m_activeRoiCameraId.isEmpty();
    m_activeRoiAlias = m_activeRoiAlias.trimmed();

    if (!hasCameraId && m_activeRoiAlias.isEmpty())
        return;

    if (!m_cameraManager)
        return;

    CameraManager::CameraInfo info;
    if (hasCameraId)
        info = m_cameraManager->camera(m_activeRoiCameraId);

    const bool hasInfo = !(info.id.isEmpty() && info.slotId.isEmpty() && info.name.isEmpty());
    if (!hasInfo && m_activeRoiAlias.isEmpty())
        return;

    const QRectF sanitized = sanitizeNormalizedRect(rect);

    auto nearlyEqual = [](double a, double b) {
        return std::abs(a - b) < 1e-6;
    };

    auto nearlyEqualRect = [&](const QRectF &lhs, const QRectF &rhs) {
        return nearlyEqual(lhs.x(), rhs.x()) && nearlyEqual(lhs.y(), rhs.y())
            && nearlyEqual(lhs.width(), rhs.width()) && nearlyEqual(lhs.height(), rhs.height());
    };

    if (m_waitingForStoredRoi)
    {
        if (sanitized.isValid() && m_expectedRoiRect.isValid() && nearlyEqualRect(sanitized, m_expectedRoiRect))
            m_waitingForStoredRoi = false;
        return;
    }

    int profileIndex = (m_activeRoiProfileIndex >= 0 && m_activeRoiProfileIndex < m_cameraRoiProfiles.size())
                           ? m_activeRoiProfileIndex
                           : (hasInfo ? findCameraProfile(info) : -1);
    if (profileIndex < 0 && !m_activeRoiAlias.isEmpty())
        profileIndex = findCameraProfileByAlias(m_activeRoiAlias);

    if (!sanitized.isValid())
    {
        if (profileIndex >= 0 && profileIndex < m_cameraRoiProfiles.size())
        {
            m_cameraRoiProfiles.removeAt(profileIndex);
            rebuildCameraRoiAliasIndex();
            m_activeRoiProfileIndex = -1;
            m_activeRoiAlias.clear();
            m_cameraRoiDirty = true;
        }
        return;
    }

    bool changed = false;

    if (profileIndex < 0)
    {
        CameraRoiProfile profile;
        profile.id = info.id;
        profile.slotId = info.slotId;
        profile.name = info.name;
        QString aliasSeed = !m_activeRoiAlias.isEmpty()
                                ? m_activeRoiAlias
                                : (!info.name.trimmed().isEmpty()
                                       ? info.name
                                       : (!info.slotId.trimmed().isEmpty() ? info.slotId : info.id));
        profile.alias = uniqueAlias(aliasSeed);
        profile.roi = sanitized;
        profile.enabled = hasInfo ? info.visible : true;
        m_cameraRoiProfiles.append(profile);
        rebuildCameraRoiAliasIndex();
        m_activeRoiProfileIndex = m_cameraRoiProfiles.size() - 1;
        m_activeRoiAlias = profile.alias;
        changed = true;
    }
    else if (profileIndex < m_cameraRoiProfiles.size())
    {
        CameraRoiProfile &profile = m_cameraRoiProfiles[profileIndex];

        if (!nearlyEqualRect(profile.roi, sanitized))
        {
            profile.roi = sanitized;
            changed = true;
        }

        bool aliasChanged = false;
        if (hasInfo)
        {
            if (profile.id != info.id)
            {
                profile.id = info.id;
                aliasChanged = true;
            }
            if (profile.slotId != info.slotId)
            {
                profile.slotId = info.slotId;
                aliasChanged = true;
            }
            if (profile.name != info.name)
            {
                profile.name = info.name;
                aliasChanged = true;
            }
        }

        if (!m_activeRoiAlias.isEmpty() &&
            profile.alias.compare(m_activeRoiAlias, Qt::CaseInsensitive) != 0)
        {
            const QString updatedAlias = uniqueAlias(m_activeRoiAlias, profileIndex);
            if (profile.alias.compare(updatedAlias, Qt::CaseInsensitive) != 0)
            {
                profile.alias = updatedAlias;
                m_activeRoiAlias = updatedAlias;
                aliasChanged = true;
            }
        }
        else if (profile.alias.trimmed().isEmpty())
        {
            const QString seed = !m_activeRoiAlias.isEmpty()
                                     ? m_activeRoiAlias
                                     : (!info.name.trimmed().isEmpty()
                                            ? info.name
                                            : (!info.slotId.trimmed().isEmpty() ? info.slotId : info.id));
            const QString updatedAlias = uniqueAlias(seed, profileIndex);
            if (profile.alias.compare(updatedAlias, Qt::CaseInsensitive) != 0)
            {
                profile.alias = updatedAlias;
                m_activeRoiAlias = updatedAlias;
                aliasChanged = true;
            }
        }

        if (hasInfo && profile.enabled != info.visible)
        {
            profile.enabled = info.visible;
            changed = true;
        }

        if (aliasChanged)
        {
            rebuildCameraRoiAliasIndex();
            if (hasInfo)
                m_activeRoiProfileIndex = findCameraProfile(info);
            else
                m_activeRoiProfileIndex = findCameraProfileByAlias(m_activeRoiAlias);
            changed = true;
        }
        else
        {
            m_activeRoiProfileIndex = profileIndex;
        }
    }

    if (changed)
        m_cameraRoiDirty = true;
}

void SettingsPage::hideRoiEditor()
{
    const QString cameraId = m_activeRoiCameraId;

    if (m_cameraRoiDirty)
    {
        saveCameraSettings();
    }
    if (m_roiEditor)
        m_roiEditor->clearCamera();
    if (m_roiOverlay)
        m_roiOverlay->hide();
    m_activeRoiCameraId.clear();
    m_activeRoiAlias.clear();
    m_activeRoiProfileIndex = -1;
    m_waitingForStoredRoi = false;
    m_expectedRoiRect = QRectF();
    if (m_roiCameraLabel)
        m_roiCameraLabel->setText(tr("Camera unavailable"));

    if (m_cameraManager && !cameraId.isEmpty())
    {
        if (QCamera *camera = m_cameraManager->cameraHandle(cameraId))
        {
            qInfo() << "[ROIEditor] reassigning camera to previews after hide" << cameraId << camera;
            m_cameraManager->assignCameraHandle(cameraId, camera);
        }
    }
}

void SettingsPage::showCameraDetail(const QString &cameraId)
{
    if (!m_cameraDetailOverlay || !m_cameraManager || cameraId.isEmpty())
        return;

    const CameraManager::CameraInfo info = m_cameraManager->camera(cameraId);
    if (info.id.isEmpty())
        return;

    m_activeDetailCameraId = cameraId;
    m_lastDetailCameraId = cameraId;
    loadCameraDetailState(info);
    updateCameraDetailTexts();

    if (m_cameraDetailSerialCombo)
        populateSerialPortCombo(m_cameraDetailSerialCombo, m_detailState.serialPort);

    if (m_cameraDetailPreviewLayout)
    {
        while (QLayoutItem *item = m_cameraDetailPreviewLayout->takeAt(0))
        {
            if (QWidget *widget = item->widget())
            {
                if (auto *preview = qobject_cast<CameraPreviewWidget *>(widget))
                    preview->setCamera(nullptr);
                widget->deleteLater();
            }
            delete item;
        }
        m_cameraDetailPreview = nullptr;
    }

    if (m_cameraDetailPreviewLayout && m_cameraDetailPreviewContainer)
    {
        m_cameraDetailPreview = new CameraPreviewWidget(cameraId, m_cameraDetailPreviewContainer);
        m_cameraDetailPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_cameraDetailPreviewLayout->addWidget(m_cameraDetailPreview);
        m_cameraDetailPreview->updateInfo(info);
    }

    updateCameraDetailControls();
    QTimer::singleShot(0, this, [this]() { updateCameraDetailPreviewGeometry(); });

    if (m_cameraDetailOverlay)
    {
        m_cameraDetailOverlay->setGeometry(rect());
        m_cameraDetailOverlay->show();
        m_cameraDetailOverlay->raise();
    }
    m_cameraDetailVisible = true;

    if (auto *cameraHandle = m_cameraManager->cameraHandle(cameraId))
        attachCameraToDetailPreview(cameraHandle);

    updateCameraDetailBusyOverlayGeometry();

    if (!m_detailState.serialPort.trimmed().isEmpty())
        startSerialRead(m_detailState.serialPort);
}

void SettingsPage::hideCameraDetail()
{
    const QString cameraId = m_activeDetailCameraId;
    const bool clearedSerial = clearDetailSerialPortIfDisconnected();
    if (clearedSerial)
    {
        applyDetailChangesToProfile();
        persistCameraProfilesIfNeeded();
    }
    qInfo() << "[CameraDetail] hide detail for camera" << m_activeDetailCameraId;
    setCameraDetailBusy(false);
    m_cameraDetailVisible = false;
    m_pendingSerialWrite = false;
    if (m_cameraDetailOverlay)
        m_cameraDetailOverlay->hide();

    if (m_cameraDetailPreview)
    {
        m_cameraDetailPreview->setCamera(nullptr);
        m_cameraDetailPreview->deleteLater();
        m_cameraDetailPreview = nullptr;
    }

    if (m_cameraDetailPreviewLayout)
    {
        while (QLayoutItem *item = m_cameraDetailPreviewLayout->takeAt(0))
        {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
    }

    if (!m_activeDetailCameraId.isEmpty())
        m_lastDetailCameraId = m_activeDetailCameraId;

    m_activeDetailCameraId.clear();
    m_activeDetailProfileIndex = -1;
    m_detailState = CameraDetailState();

    if (m_cameraManager && !cameraId.isEmpty())
    {
        if (QCamera *camera = m_cameraManager->cameraHandle(cameraId))
        {
            qInfo() << "[CameraDetail] reassigning camera to previews after hide" << cameraId << camera;
            m_cameraManager->assignCameraHandle(cameraId, camera);
        }

        qInfo() << "[CameraDetail] restarting camera after hide" << cameraId;
        restartCameraStream(cameraId);
    }
}

void SettingsPage::updateCameraDetailTexts()
{
    if (m_cameraDetailBreadcrumb)
        m_cameraDetailBreadcrumb->setText(tr("Settings / Camera Settings"));
    if (m_cameraDetailSerialLabel)
        m_cameraDetailSerialLabel->setText(tr("Serial Port"));
    if (m_cameraDetailSerialRefreshButton)
        m_cameraDetailSerialRefreshButton->setToolTip(tr("Refresh serial ports"));
    if (m_cameraDetailInvertLabel)
        m_cameraDetailInvertLabel->setText(tr("Flip Settings"));
    if (m_cameraDetailVerticalLabel)
        m_cameraDetailVerticalLabel->setText(tr("Vertical Flip"));
    if (m_cameraDetailHorizontalLabel)
        m_cameraDetailHorizontalLabel->setText(tr("Horizontal Flip"));
    if (m_cameraDetailBrightnessLabel)
        m_cameraDetailBrightnessLabel->setText(tr("Brightness"));
    if (m_cameraDetailSensitivityLabel)
        m_cameraDetailSensitivityLabel->setText(tr("Sensitivity"));
    if (m_cameraDetailFocusAssistLabel)
        m_cameraDetailFocusAssistLabel->setText(tr("Focus Assist"));
    if (m_cameraDetailCancelButton)
        m_cameraDetailCancelButton->setText(tr("Cancel"));
    if (m_cameraDetailSaveButton)
        m_cameraDetailSaveButton->setText(tr("Save"));
    if (m_cameraDetailHint)
        m_cameraDetailHint->setText(tr("To adjust brightness and sensitivity, the camera must be connected through the selected serial port. If the camera does not react, verify the serial connection and try again."));

    if (m_cameraDetailSection)
    {
        QString slot = m_detailState.slotLabel;
        QString name = m_detailState.displayName;
        QString text;
        if (!slot.isEmpty() && !name.isEmpty())
            text = tr("[%1] %2").arg(slot, name);
        else if (!name.isEmpty())
            text = name;
        else
            text = slot;
        m_cameraDetailSection->setText(text);
    }
}

void SettingsPage::updateCameraDetailControls()
{
    m_updatingCameraDetailUi = true;

    if (m_cameraDetailSerialCombo)
    {
        QSignalBlocker blocker(m_cameraDetailSerialCombo);
        const QString target = m_detailState.serialPort;
        int index = m_cameraDetailSerialCombo->findData(target);
        if (index < 0 && !target.isEmpty())
        {
            const QString label = tr("%1 (disconnected)").arg(target);
            m_cameraDetailSerialCombo->addItem(label, target);
            index = m_cameraDetailSerialCombo->findData(target);
        }
        if (index >= 0)
            m_cameraDetailSerialCombo->setCurrentIndex(index);
        else
            m_cameraDetailSerialCombo->setCurrentIndex(0);
    }

    if (m_cameraDetailVerticalFlipSwitch)
    {
        QSignalBlocker blocker(m_cameraDetailVerticalFlipSwitch);
        m_cameraDetailVerticalFlipSwitch->setChecked(m_detailState.invertVertical);
    }
    if (m_cameraDetailHorizontalFlipSwitch)
    {
        QSignalBlocker blocker(m_cameraDetailHorizontalFlipSwitch);
        m_cameraDetailHorizontalFlipSwitch->setChecked(m_detailState.invertHorizontal);
    }
    if (m_cameraDetailFocusAssistSwitch)
    {
        QSignalBlocker blocker(m_cameraDetailFocusAssistSwitch);
        m_cameraDetailFocusAssistSwitch->setChecked(m_detailState.focusAssist);
    }

    applyLevelButtonStyles(m_cameraDetailBrightnessButtons, m_detailState.brightness);
    applyLevelButtonStyles(m_cameraDetailSensitivityButtons, m_detailState.sensitivity);

    m_updatingCameraDetailUi = false;

    updateCameraDetailAvailability();
}

void SettingsPage::updateCameraDetailAvailability()
{
    const bool hasSerial = !m_detailState.serialPort.isEmpty();
    const bool enable = hasSerial && !m_cameraDetailBusy;

    const QList<QWidget *> widgets = {
        m_cameraDetailVerticalFlipSwitch,
        m_cameraDetailHorizontalFlipSwitch,
        m_cameraDetailFocusAssistSwitch,
        m_cameraDetailInvertRefresh,
        m_cameraDetailBrightnessMinus,
        m_cameraDetailBrightnessPlus,
        m_cameraDetailBrightnessRefresh,
        m_cameraDetailSensitivityMinus,
        m_cameraDetailSensitivityPlus,
        m_cameraDetailSensitivityRefresh
    };

    for (QWidget *widget : widgets)
    {
        if (widget)
            widget->setEnabled(enable);
    }

    if (m_cameraDetailSerialRefreshButton)
        m_cameraDetailSerialRefreshButton->setEnabled(!m_cameraDetailBusy);
    if (m_cameraDetailSerialCombo)
        m_cameraDetailSerialCombo->setEnabled(!m_cameraDetailBusy);
    if (m_cameraDetailCancelButton)
        m_cameraDetailCancelButton->setEnabled(!m_cameraDetailBusy);
    if (m_cameraDetailSaveButton)
        m_cameraDetailSaveButton->setEnabled(!m_cameraDetailBusy);

    for (QPushButton *button : std::as_const(m_cameraDetailBrightnessButtons))
    {
        if (button)
            button->setEnabled(enable);
    }
    for (QPushButton *button : std::as_const(m_cameraDetailSensitivityButtons))
    {
        if (button)
            button->setEnabled(enable);
    }
}

void SettingsPage::handleCameraDetailDefaultsRequested()
{
    QObject *origin = sender();
    if (!origin)
        return;

    if (origin == m_cameraDetailInvertRefresh)
    {
        m_detailState.invertVertical = false;
        m_detailState.invertHorizontal = false;
    }
    else if (origin == m_cameraDetailBrightnessRefresh)
    {
        m_detailState.brightness = 5;
    }
    else if (origin == m_cameraDetailSensitivityRefresh)
    {
        m_detailState.sensitivity = 5;
    }

    updateCameraDetailControls();
    requestSerialWrite();
}

void SettingsPage::handleCameraDetailSaveRequested()
{
    if (m_cameraDetailBusy)
        return;

    clearDetailSerialPortIfDisconnected();
    applyDetailChangesToProfile();
    persistCameraProfilesIfNeeded();

    hideCameraDetail();
}

void SettingsPage::handleCameraDetailSerialChanged(int index)
{
    if (!m_cameraDetailSerialCombo || m_updatingCameraDetailUi)
        return;

    const QString previousPort = m_detailState.serialPort;
    QString port;
    if (index >= 0)
        port = m_cameraDetailSerialCombo->itemData(index).toString();

    m_detailState.serialPort = port;
    updateCameraDetailAvailability();
    m_pendingSerialWrite = false;

    if (port == previousPort)
        return;

    applyDetailChangesToProfile();
    persistCameraProfilesIfNeeded();

    if (!port.isEmpty())
        startSerialRead(port);
}

void SettingsPage::persistCameraProfilesIfNeeded()
{
    if (!m_cameraRoiDirty)
        return;

    saveCameraSettings();
}

void SettingsPage::applyDetailChangesToProfile()
{
    if (m_detailState.cameraId.isEmpty())
        return;

    CameraManager::CameraInfo info;
    if (m_cameraManager)
        info = m_cameraManager->camera(m_detailState.cameraId);

    int profileIndex = m_activeDetailProfileIndex;
    if (profileIndex < 0 && m_cameraManager)
        profileIndex = findCameraProfile(info);
    if (profileIndex < 0 && !m_detailState.displayName.isEmpty())
        profileIndex = findCameraProfileByAlias(m_detailState.displayName);

    if (profileIndex < 0)
    {
        CameraRoiProfile profile;
        profile.id = info.id;
        profile.slotId = info.slotId;
        profile.name = info.name;
        const QString aliasSeed = !info.alias.isEmpty() ? info.alias : displayNameForCamera(info);
        profile.alias = uniqueAlias(aliasSeed.isEmpty() ? info.id : aliasSeed);
        profile.roi = QRectF();
        profile.analysis = QJsonObject();
        profile.serialPort = QString();
        profile.enabled = info.visible;
        m_cameraRoiProfiles.append(profile);
        rebuildCameraRoiAliasIndex();
        profileIndex = m_cameraRoiProfiles.size() - 1;
    }

    if (profileIndex < 0 || profileIndex >= m_cameraRoiProfiles.size())
        return;

    CameraRoiProfile &profile = m_cameraRoiProfiles[profileIndex];
    bool changed = false;

    if (!info.id.isEmpty() && profile.id != info.id)
    {
        profile.id = info.id;
        changed = true;
    }
    if (profile.slotId != info.slotId)
    {
        profile.slotId = info.slotId;
        changed = true;
    }
    if (profile.name != info.name)
    {
        profile.name = info.name;
        changed = true;
    }
    if (profile.enabled != info.visible)
    {
        profile.enabled = info.visible;
        changed = true;
    }

    if (profile.serialPort != m_detailState.serialPort)
    {
        profile.serialPort = m_detailState.serialPort;
        changed = true;
    }

    if (changed)
        m_cameraRoiDirty = true;

    m_activeDetailProfileIndex = profileIndex;

    if (!m_detailState.serialPort.isEmpty())
        m_cameraSerialSelection.insert(m_detailState.cameraId, m_detailState.serialPort);
    else
        m_cameraSerialSelection.remove(m_detailState.cameraId);

    if (m_cameraManager && !info.id.isEmpty())
    {
        int rowIndex = -1;
        for (int i = 0; i < m_cameraRows.size(); ++i)
        {
            if (m_cameraRows.at(i).cameraId == info.id)
            {
                rowIndex = i;
                break;
            }
        }
        if (rowIndex >= 0)
            refreshCameraRow(info, rowIndex);
    }
}

void SettingsPage::loadCameraDetailState(const CameraManager::CameraInfo &info)
{
    m_detailState = CameraDetailState();
    m_detailState.cameraId = info.id;
    m_detailState.displayName = displayNameForCamera(info);
    if (m_detailState.displayName.isEmpty())
        m_detailState.displayName = info.name.isEmpty() ? info.id : info.name;

    if (!info.slotId.isEmpty())
    {
        m_detailState.slotLabel = info.slotId;
    }
    else
    {
        for (const CameraRow &row : std::as_const(m_cameraRows))
        {
            if (row.cameraId == info.id && row.slotLabel)
            {
                m_detailState.slotLabel = row.slotLabel->text();
                break;
            }
        }
        if (m_detailState.slotLabel.isEmpty() && m_cameraManager)
        {
            const auto cameras = m_cameraManager->cameras();
            for (int index = 0; index < cameras.size(); ++index)
            {
                if (cameras.at(index).id == info.id)
                {
                    m_detailState.slotLabel = tr("Slot %1").arg(index + 1, 2, 10, QLatin1Char('0'));
                    break;
                }
            }
        }
    }

    m_detailState.serialPort = serialPortForCamera(info);
    m_activeDetailProfileIndex = findCameraProfile(info);
    if (m_activeDetailProfileIndex < 0 && !m_detailState.displayName.isEmpty())
        m_activeDetailProfileIndex = findCameraProfileByAlias(m_detailState.displayName);

    if (m_activeDetailProfileIndex >= 0 && m_activeDetailProfileIndex < m_cameraRoiProfiles.size())
    {
        const CameraRoiProfile &profile = m_cameraRoiProfiles.at(m_activeDetailProfileIndex);
        if (!profile.serialPort.isEmpty())
            m_detailState.serialPort = profile.serialPort;
    }
}

void SettingsPage::attachCameraToDetailPreview(QCamera *camera)
{
    if (!m_cameraDetailPreview)
        return;

    qInfo() << "[CameraDetail] attachCameraToDetailPreview camera=" << camera;
    m_cameraDetailPreview->setCamera(camera);
}

void SettingsPage::updateCameraDetailPreviewGeometry()
{
    if (!m_cameraDetailVisible || !m_cameraDetailPreview || !m_cameraDetailPreviewContainer)
        return;

    const int containerWidth = std::max(960, m_cameraDetailPreviewContainer->width());
    const int desiredHeight = static_cast<int>(std::round(containerWidth * 9.0 / 16.0));
    m_cameraDetailPreview->setMinimumWidth(containerWidth);
    m_cameraDetailPreview->setMaximumWidth(containerWidth);
    m_cameraDetailPreview->setMinimumHeight(desiredHeight);
    m_cameraDetailPreview->setMaximumHeight(desiredHeight);
    m_cameraDetailPreviewContainer->setMinimumHeight(desiredHeight);
    m_cameraDetailPreviewContainer->setMaximumHeight(desiredHeight);
}

void SettingsPage::updateCameraDetailBusyOverlayGeometry()
{
    if (!m_cameraDetailBusyOverlay || !m_cameraDetailPanel)
        return;

    m_cameraDetailBusyOverlay->setGeometry(m_cameraDetailPanel->rect());
    m_cameraDetailBusyOverlay->raise();
}

void SettingsPage::setCameraDetailBusy(bool busy, const QString &message)
{
    qInfo() << "[CameraDetail] busy =" << busy << "message =" << message;

    m_cameraDetailBusy = busy;

    if (busy)
    {
        if (m_cameraDetailBusyLabel)
        {
            m_cameraDetailBusyLabel->setText(message);
            m_cameraDetailBusyLabel->setVisible(!message.isEmpty());
        }
        if (m_cameraDetailBusySpinner)
            m_cameraDetailBusySpinner->start();
        updateCameraDetailBusyOverlayGeometry();
        if (m_cameraDetailBusyOverlay)
            m_cameraDetailBusyOverlay->show();
    }
    else
    {
        if (m_cameraDetailBusySpinner)
            m_cameraDetailBusySpinner->stop();
        if (m_cameraDetailBusyOverlay)
            m_cameraDetailBusyOverlay->hide();
        if (m_cameraDetailBusyLabel)
        {
            m_cameraDetailBusyLabel->clear();
            m_cameraDetailBusyLabel->setVisible(false);
        }
    }

    updateCameraDetailAvailability();
}

void SettingsPage::startSerialRead(const QString &portName)
{
    // Kick off background read of camera serial settings; UI shows busy overlay meanwhile.
    if (portName.trimmed().isEmpty())
        return;
    if (m_serialReadWatcher.isRunning() || m_serialWriteWatcher.isRunning())
        return;

    qInfo() << "[CameraDetail] startSerialRead port =" << portName;
    setCameraDetailBusy(true, tr("Reading camera settings..."));
    auto future = QtConcurrent::run([portName]() -> SerialCamState {
        qInfo() << "[CameraDetail] read thread started for" << portName;
        return readSerialOptionsBlocking(portName);
    });
    m_serialReadWatcher.setFuture(future);
}

void SettingsPage::requestSerialWrite()
{
    // Queue write if another job is running; otherwise start immediately.
    if (m_detailState.serialPort.trimmed().isEmpty())
        return;

    if (m_cameraDetailBusy || m_serialWriteWatcher.isRunning() || m_serialReadWatcher.isRunning())
    {
        m_pendingSerialWrite = true;
        qInfo() << "[CameraDetail] serial write queued (busy)" << m_detailState.serialPort;
        return;
    }

    m_pendingSerialWrite = false;
    qInfo() << "[CameraDetail] serial write request immediate" << m_detailState.serialPort;
    startSerialWrite();
}

void SettingsPage::startSerialWrite()
{
    // Dispatch asynchronous serial write with verification, ensuring camera restarts if needed.
    const QString port = m_detailState.serialPort.trimmed();
    if (port.isEmpty())
        return;
    if (!m_detailState.cameraId.isEmpty())
        m_lastDetailCameraId = m_detailState.cameraId;
    else if (!m_activeDetailCameraId.isEmpty())
        m_lastDetailCameraId = m_activeDetailCameraId;

    if (m_cameraDetailBusy || m_serialWriteWatcher.isRunning() || m_serialReadWatcher.isRunning())
    {
        m_pendingSerialWrite = true;
        return;
    }

    m_needCameraRestart = true;
    qInfo() << "[CameraDetail] executing serial write" << port
            << "(camera" << m_lastDetailCameraId << ")"
            << "values" << m_detailState.brightness << m_detailState.sensitivity
            << m_detailState.invertHorizontal << m_detailState.invertVertical;

    const int brightness = std::clamp(m_detailState.brightness, 1, 10);
    const int sensitivity = std::clamp(m_detailState.sensitivity, 1, 10);
    const bool focusAssist = m_detailState.focusAssist;
    const bool mirror = m_detailState.invertHorizontal;   // hardware maps mirror->horizontal
    const bool flip = m_detailState.invertVertical;        // hardware maps flip->vertical

    qInfo() << "[CameraDetail] startSerialWrite port =" << port
            << "brightness =" << brightness << "sensitivity =" << sensitivity
            << "focusAssist =" << focusAssist
            << "mirror =" << mirror << "flip =" << flip;

    setCameraDetailBusy(true, tr("Applying camera settings..."));

    auto future = QtConcurrent::run([port, brightness, sensitivity, focusAssist, mirror, flip]() -> SerialWriteResult {
        qInfo() << "[CameraDetail] write thread started for" << port;
        SerialWriteResult result;
        if (!writeSerialOptionsBlocking(port, brightness, sensitivity, focusAssist, mirror, flip))
        {
            qWarning() << "[CameraDetail] write failed";
            result.ok = false;
            result.error = SerialWriteError::WriteFailed;
            return result;
        }
        if (!verifySerialOptionsBlocking(port, brightness, sensitivity, focusAssist, mirror, flip))
        {
            qWarning() << "[CameraDetail] verify failed";
            result.ok = false;
            result.error = SerialWriteError::VerifyFailed;
            return result;
        }
        result.ok = true;
        result.error = SerialWriteError::None;
        return result;
    });
    m_serialWriteWatcher.setFuture(future);
    m_pendingSerialWrite = false;
}

void SettingsPage::restartCameraStream(const QString &cameraId)
{
    if (!m_cameraManager || cameraId.isEmpty())
        return;

    QCamera *camera = m_cameraManager->cameraHandle(cameraId);
    if (!camera)
        return;

    qInfo() << "[CameraDetail] restarting camera stream (stop)" << cameraId;
    camera->stop();

    QPointer<QCamera> cameraPtr(camera);
    QTimer::singleShot(750, this, [this, cameraPtr, cameraId]() {
        if (!cameraPtr)
        {
            qWarning() << "[CameraDetail] restart skipped, camera pointer invalid" << cameraId;
            m_needCameraRestart = false;
            return;
        }
        qInfo() << "[CameraDetail] restarting camera stream (start)" << cameraId;
        cameraPtr->start();
        if (m_cameraManager)
        {
            qInfo() << "[CameraDetail] reassigning camera handle" << cameraId;
            m_cameraManager->assignCameraHandle(cameraId, cameraPtr);
        }
        m_needCameraRestart = false;
    });
}

void SettingsPage::handleCameraDetailSerialReadFinished()
{
    // Consume async read result, update UI controls, and optionally trigger pending writes.
    SerialCamState state = m_serialReadWatcher.result();
    qInfo() << "[CameraDetail] read finished ok =" << state.ok
            << "brightness =" << state.brightness << "sensitivity =" << state.sensitivity
            << "focusAssist =" << state.focusAssist
            << "mirror =" << state.mirror << "flip =" << state.flip
            << "error =" << state.error;
    setCameraDetailBusy(false);

    if (!m_cameraDetailVisible)
    {
        return;
    }

    if (!state.ok)
    {
        const QString message = state.error.isEmpty()
                                     ? tr("Failed to read camera settings from the connected device.")
                                     : state.error;
        QMessageBox::warning(this, tr("Serial Read Failed"), message);
        return;
    }

    if (state.brightness >= 1 && state.brightness <= 10)
        m_detailState.brightness = state.brightness;
    if (state.sensitivity >= 1 && state.sensitivity <= 10)
        m_detailState.sensitivity = state.sensitivity;
    m_detailState.focusAssist = state.focusAssist;
    m_detailState.invertVertical = state.flip;
    m_detailState.invertHorizontal = state.mirror;

    updateCameraDetailControls();

    if (m_pendingSerialWrite)
    {
        m_pendingSerialWrite = false;
        requestSerialWrite();
    }
}

void SettingsPage::handleCameraDetailSerialWriteFinished()
{
    // Finalize async write; restart streams when hardware accepts new settings.
    SerialWriteResult result = m_serialWriteWatcher.result();
    qInfo() << "[CameraDetail] write finished ok =" << result.ok
            << "error =" << static_cast<int>(result.error);
    setCameraDetailBusy(false);

    if (!result.ok)
    {
        QString message;
        switch (result.error)
        {
        case SerialWriteError::WriteFailed:
            message = tr("Failed to send camera settings via serial port.");
            break;
        case SerialWriteError::VerifyFailed:
            message = tr("Camera did not report the expected settings after saving.");
            break;
        default:
            message = tr("An unknown serial error occurred.");
            break;
        }
        QMessageBox::warning(this, tr("Serial Write Failed"), message);
        m_needCameraRestart = false;
        return;
    }

    qInfo() << "[CameraDetail] Serial write success.";
    updateCameraDetailControls();

    const QString cameraId = !m_detailState.cameraId.isEmpty()
                             ? m_detailState.cameraId
                             : (!m_activeDetailCameraId.isEmpty() ? m_activeDetailCameraId : m_lastDetailCameraId);
    if (m_needCameraRestart && !cameraId.isEmpty())
        restartCameraStream(cameraId);

    m_needCameraRestart = false;

    if (m_pendingSerialWrite)
    {
        m_pendingSerialWrite = false;
        requestSerialWrite();
    }
}

bool SettingsPage::isSerialPortCurrentlyAvailable(const QString &port) const
{
    const QString trimmed = port.trimmed();
    if (trimmed.isEmpty())
        return false;

    const QString baseName = QFileInfo(trimmed).fileName();
    QStringList targets;
    targets << trimmed;
    if (!baseName.isEmpty() && !targets.contains(baseName))
        targets << baseName;

    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports)
    {
        if (!isAllowedSerialPort(info))
            continue;

        const QString systemLocation = info.systemLocation();
        const QString portName = info.portName();
        const QString systemBase = QFileInfo(systemLocation).fileName();

        QStringList candidates;
        candidates << portName << systemLocation;
        if (!systemBase.isEmpty())
            candidates << systemBase;

        for (const QString &candidate : std::as_const(candidates))
        {
            if (candidate.isEmpty())
                continue;
            for (const QString &target : std::as_const(targets))
            {
                if (!target.isEmpty() && candidate.compare(target, Qt::CaseInsensitive) == 0)
                    return true;
            }
        }
    }

    return false;
}

bool SettingsPage::clearDetailSerialPortIfDisconnected()
{
    if (m_detailState.serialPort.trimmed().isEmpty())
        return false;

    if (isSerialPortCurrentlyAvailable(m_detailState.serialPort))
        return false;

    qInfo() << "[CameraDetail] clearing disconnected serial port for"
            << m_detailState.cameraId << "previous port =" << m_detailState.serialPort;
    m_detailState.serialPort.clear();

    if (m_cameraDetailSerialCombo)
        populateSerialPortCombo(m_cameraDetailSerialCombo, QString());

    return true;
}

bool SettingsPage::eventFilter(QObject *watched, QEvent *event)
{
    if (auto *widget = qobject_cast<QWidget *>(watched))
    {
        QWidget *frame = nullptr;
        if (m_inputFrames.contains(widget))
            frame = widget;
        else
        {
            const auto it = m_inputFrameChildren.constFind(widget);
            if (it != m_inputFrameChildren.constEnd())
                frame = it.value();
        }

        if (frame)
        {
            auto adjustHover = [&](int delta) {
                int count = m_inputFrameHoverDepth.value(frame, 0) + delta;
                if (count < 0)
                    count = 0;
                m_inputFrameHoverDepth.insert(frame, count);
                updateInputFrameHoverState(frame);
            };

            switch (event->type())
            {
            case QEvent::Enter:
            case QEvent::HoverEnter:
                adjustHover(1);
                break;
            case QEvent::Leave:
            case QEvent::HoverLeave:
                adjustHover(-1);
                break;
            case QEvent::Destroy:
                if (m_inputFrames.contains(widget))
                {
                    m_inputFrames.remove(widget);
                    m_inputFrameHoverDepth.remove(widget);
                    for (auto it = m_inputFrameChildren.begin(); it != m_inputFrameChildren.end(); )
                    {
                        if (it.value() == widget)
                            it = m_inputFrameChildren.erase(it);
                        else
                            ++it;
                    }
                }
                else if (m_inputFrameChildren.contains(widget))
                {
                    m_inputFrameChildren.remove(widget);
                }
                break;
            default:
                break;
            }
        }
        else if (event->type() == QEvent::Destroy)
        {
            m_inputFrameChildren.remove(widget);
        }
    }

    if (watched == m_cameraDetailPreviewContainer && event->type() == QEvent::Resize)
    {
        if (m_cameraDetailVisible)
            updateCameraDetailPreviewGeometry();
    }
    else if (watched == m_cameraDetailPanel && event->type() == QEvent::Resize)
    {
        updateCameraDetailBusyOverlayGeometry();
    }
    return QWidget::eventFilter(watched, event);
}
void SettingsPage::populateLanguageList()
{
    if (!m_languageCombo)
        return;

    const QString currentCode = m_localizationManager ? m_localizationManager->currentLanguageCode() : QStringLiteral("en");
    const QString previousCode = m_languageCombo->currentData().toString();

    QSignalBlocker blocker(m_languageCombo);
    m_languageCombo->clear();

    if (m_localizationManager)
    {
        const auto languages = m_localizationManager->languages();
        for (const auto &language : languages)
        {
            QString displayName = language.builtIn ? tr("English") : language.name;
            if (!language.nativeName.isEmpty() && language.nativeName.compare(language.name, Qt::CaseInsensitive) != 0)
                displayName += QStringLiteral(" (%1)").arg(language.nativeName);
            const QString optionText = QStringLiteral("%1 [%2]").arg(displayName, language.code);
            m_languageCombo->addItem(optionText, language.code);
        }
    }
    else
    {
        const QString optionText = QStringLiteral("%1 [%2]").arg(tr("English"), QStringLiteral("en"));
        m_languageCombo->addItem(optionText, QStringLiteral("en"));
    }

    QStringList entryTexts;
    for (int i = 0; i < m_languageCombo->count(); ++i)
        entryTexts << m_languageCombo->itemText(i);
    qInfo() << "[SettingsPage] languages available:" << entryTexts;

    const int count = m_languageCombo->count();
    const int rowsToShow = std::clamp(count, 1, 4);
    m_languageCombo->setMaxVisibleItems(rowsToShow);
    if (auto *view = qobject_cast<QListView *>(m_languageCombo->view()))
    {
        view->setTextElideMode(Qt::ElideNone);
        const QScrollBar *scrollBar = view->verticalScrollBar();
        int scrollbarWidth = 0;
        if (scrollBar)
        {
            scrollbarWidth = scrollBar->isVisible()
                                  ? scrollBar->sizeHint().width()
                                  : view->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, view);
        }
        const int contentWidth = view->sizeHintForColumn(0) + scrollbarWidth + 16;
        const int baseWidth = std::max(m_languageCombo->width(), 320);
        view->setMinimumWidth(std::max(baseWidth, contentWidth));
    }
    applyComboPopupStyle(m_languageCombo, rowsToShow);
    int index = m_languageCombo->findData(previousCode);
    if (index < 0)
        index = m_languageCombo->findData(currentCode);
    if (index < 0)
        index = m_languageCombo->findData(QStringLiteral("en"));
    if (index >= 0)
        m_languageCombo->setCurrentIndex(index);

    setInputFrameEditable(m_languageComboFrame, m_languageCombo && m_languageCombo->isEnabled());
}

void SettingsPage::retranslateUi()
{
    if (m_pageTitleLabel)
        m_pageTitleLabel->setText(tr("Settings"));
    if (m_cameraSectionTitle)
        m_cameraSectionTitle->setText(tr("Camera Connections"));
    if (m_analysisSectionTitle)
        m_analysisSectionTitle->setText(tr("Analysis Settings"));
    if (m_analysisHintLabel)
        updateAnalysisHintText();
    if (m_plcSectionTitle)
        m_plcSectionTitle->setText(tr("PLC Connection Settings"));
    if (m_languageSectionTitle)
        m_languageSectionTitle->setText(tr("Language Settings"));
    if (m_passTypeCombo)
    {
        const int rootIndex = m_passTypeCombo->findData(QStringLiteral("Root"), Qt::UserRole, Qt::MatchFixedString);
        if (rootIndex >= 0)
            m_passTypeCombo->setItemText(rootIndex, tr("Root"));
        const int secondIndex = m_passTypeCombo->findData(QStringLiteral("Second"), Qt::UserRole, Qt::MatchFixedString);
        if (secondIndex >= 0)
            m_passTypeCombo->setItemText(secondIndex, tr("Second"));
    }
    if (m_passUsageLabel)
        updatePassUsageLabel();
    if (m_passNumberLabel)
        m_passNumberLabel->setText(tr("Pass #"));
    if (m_torchLabel)
        m_torchLabel->setText(tr("Torch"));
    if (m_torchUnitLabel)
        m_torchUnitLabel->setText(tr("mm"));
    if (m_aiModelLabel)
        m_aiModelLabel->setText(tr("Model"));
    if (m_aiConfidenceLabel)
        m_aiConfidenceLabel->setText(tr("Allowable Limit"));
    if (m_allowableUnitLabel)
        m_allowableUnitLabel->setText(tr("mm"));
    if (m_aiEnabledCheck)
        m_aiEnabledCheck->setToolTip(tr("Send startup/shutdown requests to the AI analysis server."));
    if (m_aiModelEdit)
        m_aiModelEdit->setPlaceholderText(tr("Enter a model"));
    if (m_languageNameLabel)
        m_languageNameLabel->setText(tr("Language Selection"));
    if (m_languageRefreshButton)
        m_languageRefreshButton->setToolTip(tr("Refresh language list"));
    if (m_roiTitleLabel)
        m_roiTitleLabel->setText(tr("ROI Settings"));
    if (m_roiCloseButton)
        m_roiCloseButton->setText(tr("Close"));
    if (m_roiCameraLabel)
    {
        QString cameraDisplayName;
        if (m_cameraManager && !m_activeRoiCameraId.isEmpty())
        {
            const auto info = m_cameraManager->camera(m_activeRoiCameraId);
            cameraDisplayName = info.name.isEmpty() ? info.id : info.name;
        }
        if (cameraDisplayName.isEmpty())
            m_roiCameraLabel->setText(tr("Camera unavailable"));
        else
            m_roiCameraLabel->setText(tr("Camera: %1").arg(cameraDisplayName));
    }

    for (int i = 0; i < m_plcRows.size(); ++i)
    {
        auto &row = m_plcRows[i];
        if (row.ipLabel)
            row.ipLabel->setText(tr("IP"));
        if (row.portLabel)
            row.portLabel->setText(tr("Port"));
        if (row.ipEdit)
            row.ipEdit->setPlaceholderText(tr("e.g., 192.168.0.10"));
        if (row.portEdit)
            row.portEdit->setPlaceholderText(tr("e.g., 502"));
        refreshPlcRow(i);
    }

    updateCameraDetailTexts();
    if (m_cameraDetailBreadcrumb)
        updateCameraDetailControls();

    populateCameraList();
    populateLanguageList();
    updateAiStatusButton();
}

void SettingsPage::handleLanguageSelectionChanged(int index)
{
    if (!m_localizationManager || index < 0 || !m_languageCombo)
        return;

    const QString code = m_languageCombo->itemData(index).toString();
    if (code.isEmpty())
        return;

    if (!m_localizationManager->applyLanguage(code))
        qWarning() << "[SettingsPage] Failed to apply language" << code;
}
