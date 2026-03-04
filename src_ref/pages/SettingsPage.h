// Author: SeungJae Lee
// SettingsPage interface: orchestrates configuration for cameras, AI analysis, PLC links, and localization.

#pragma once

#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QVector>
#include <QWidget>
#include <QFutureWatcher>

#include "managers/AiClient.h"
#include "managers/CameraManager.h"
#include "managers/LocalizationManager.h"
#include "managers/PLCClient.h"

class QCheckBox;
class QIcon;
class QComboBox;
class QSpinBox;
class QColor;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QListView;
class BusyIndicator;
class CameraRoiWidget;
class CameraPreviewWidget;
class ToggleSwitch;
class QScrollArea;
class QCamera;
class QHideEvent;

struct SerialCamState
{
    bool ok = false;
    int brightness = -1;
    int sensitivity = -1;
    bool focusAssist = false;
    bool mirror = false;
    bool flip = false;
    QString error;
};

enum class SerialWriteError
{
    None,
    WriteFailed,
    VerifyFailed
};

struct SerialWriteResult
{
    bool ok = false;
    SerialWriteError error = SerialWriteError::None;
};

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    SettingsPage(CameraManager *cameraManager, AiClient *aiClient, PLCClient *plcClient,
                 LocalizationManager *localizationManager, QWidget *parent = nullptr);
    ~SettingsPage() override;

    void setCameraAnalysisEnabled(const QString &cameraId, bool enabled,
                                  const QString &url = QString(),
                                  const QString &streamKey = QString(),
                                  double fps = -1.0);
    QJsonObject cameraAnalysisConfig(const QString &cameraId) const;
    void setAnalysisBusy(bool busy);

private:
    void changeEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

    void buildUi();
    void buildCameraSection(QVBoxLayout *rootLayout);
    void buildAnalysisSection(QVBoxLayout *rootLayout);
    void buildPlcSection(QVBoxLayout *rootLayout);
    void buildLanguageSection(QVBoxLayout *rootLayout);
    QWidget *createSectionTitle(const QString &text, QLabel **titleLabel);
    QWidget *createSettingsRowContainer(QWidget *parent = nullptr) const;
    QFrame *createDivider(QWidget *parent = nullptr) const;
    QFrame *createVerticalDivider(QWidget *parent = nullptr) const;
    QIcon createRoiIcon(const QSize &size) const;
    QIcon createGearIcon(const QSize &size) const;
    void rebuildCameraRows(const QVector<CameraManager::CameraInfo> &cameras);
    void refreshCameraRow(const CameraManager::CameraInfo &info, int index);
    void updateCameraRowStatus(const CameraManager::CameraInfo &info, QPushButton *statusButton) const;
    QColor cameraConnectivityColor(bool connected) const;
    void populateSerialPortCombo(QComboBox *combo, const QString &selectedPort);
    QWidget *createComboFrame(QComboBox *combo, QWidget *parent, int visibleRows = -1);
    QWidget *createLineEditFrame(QLineEdit *edit, QWidget *parent, bool readOnly = false, const QString &suffixText = QString(), QLabel **suffixLabelOut = nullptr);
    void configureInputFrame(QWidget *frame, bool editable);
    void setInputFrameEditable(QWidget *frame, bool editable);
    void registerInputChild(QWidget *child, QWidget *frame);
    void updateInputFrameHoverState(QWidget *frame);
    void applyComboPopupStyle(QComboBox *combo, int visibleRows = -1);
    void updateAiStatusButton();
    void updateAnalysisControlState();
    void updateAnalysisHintText();
    void updatePassUsageLabel();
    void persistLanguagePreference(const QString &code) const;
    void applyCameraAssignment(const QString &cameraId, const QString &cameraName);
    void applyCameraAlias(const QString &cameraId, const QString &alias);
    void handleCameraNameEdited(const QString &cameraId, const QString &newName);
    void applyCameraVisibility(const QString &cameraId, bool visible);
    void populateCameraList();
    void applyAiSettings();
    void loadAnalysisParametersFromConfig();
    void persistAnalysisParameters(const AiClient::Settings &settings) const;
    void populateLanguageList();
    void retranslateUi();
    void handleLanguageSelectionChanged(int index);
    void loadPlcSettings();
    void savePlcSettings() const;
    void loadCameraSettings();
    void saveCameraSettings();
    void handleRoiNormalizedChanged(const QRectF &rect);
    QString cameraAliasKey(const QString &prefix, const QString &value) const;
    void rebuildCameraRoiAliasIndex();
    int findCameraProfile(const CameraManager::CameraInfo &info) const;
    int findCameraProfileByAlias(const QString &alias) const;
    QRectF sanitizeNormalizedRect(const QRectF &rect) const;
    QString uniqueAlias(const QString &base, int excludeIndex = -1) const;
    QString displayNameForCamera(const CameraManager::CameraInfo &info) const;
    QString serialPortForCamera(const CameraManager::CameraInfo &info) const;
    void applyStoredCameraVisibility(const CameraManager::CameraInfo &info);
    void updateCameraProfileVisibility(const CameraManager::CameraInfo &info, bool visible);
    void rebuildPlcRows();
    void refreshPlcRow(int index);
    void updatePlcRowStatusIndicator(int index);
    void setPlcBusy(int index, bool busy);
    int plcIndexById(const QString &id) const;
    bool validateIpAddress(const QString &address) const;
    bool validatePortValue(const QString &value) const;
    void handlePlcFieldEdited(int index);
    void handlePlcConnectRequested(int index);
    void handlePlcDisconnectRequested(int index);
    void handlePlcConnectionStateChanged(const QString &id, PLCClient::State state, const QString &message);
    void updateGlobalBusyIndicator();
    void resizeEvent(QResizeEvent *event) override;
    void showRoiEditor(const QString &cameraId, const QString &cameraAlias = QString());
    void hideRoiEditor();
    void buildCameraDetailOverlay();
    void showCameraDetail(const QString &cameraId);
    void hideCameraDetail();
    void updateCameraDetailTexts();
    void updateCameraDetailControls();
    void updateCameraDetailAvailability();
    void handleCameraDetailDefaultsRequested();
    void handleCameraDetailSaveRequested();
    void handleCameraDetailSerialChanged(int index);
    void persistCameraProfilesIfNeeded();
    void applyDetailChangesToProfile();
    void loadCameraDetailState(const CameraManager::CameraInfo &info);
    void attachCameraToDetailPreview(QCamera *camera);
    void updateCameraDetailPreviewGeometry();
    void updateCameraDetailBusyOverlayGeometry();
    void setCameraDetailBusy(bool busy, const QString &message = QString());
    void startSerialRead(const QString &portName);
    void startSerialWrite();
    void requestSerialWrite();
    void restartCameraStream(const QString &cameraId);
    void handleCameraDetailSerialReadFinished();
    void handleCameraDetailSerialWriteFinished();
    bool isSerialPortCurrentlyAvailable(const QString &port) const;
    bool clearDetailSerialPortIfDisconnected();
    bool eventFilter(QObject *watched, QEvent *event) override;

    // Service pointers supplying state for configuration panes.
    CameraManager *m_cameraManager = nullptr;
    AiClient *m_aiClient = nullptr;
    PLCClient *m_plcClient = nullptr;
    LocalizationManager *m_localizationManager = nullptr;

    struct CameraRow
    {
        QString cameraId;
        QWidget *container = nullptr;
        QPushButton *statusButton = nullptr;
        QLabel *slotLabel = nullptr;
        QLabel *cameraFieldLabel = nullptr;
        QComboBox *cameraCombo = nullptr;
        QLabel *serialFieldLabel = nullptr;
        QLineEdit *serialEdit = nullptr;
        QWidget *cameraFieldFrame = nullptr;
        QWidget *serialFieldFrame = nullptr;
        QPushButton *roiButton = nullptr;
        QPushButton *detailButton = nullptr;
        bool visible = true;
    };

    // UI caches for dynamically generated rows and overlay state.
    QVector<CameraRow> m_cameraRows;
    QHash<QString, QString> m_cameraSerialSelection;
    QVBoxLayout *m_cameraRowsLayout = nullptr;
    QLabel *m_pageTitleLabel = nullptr;
    QLabel *m_cameraSectionTitle = nullptr;
    QLabel *m_analysisSectionTitle = nullptr;
    QLabel *m_analysisHintLabel = nullptr;
    QLabel *m_plcSectionTitle = nullptr;
    QLabel *m_languageSectionTitle = nullptr;
    QComboBox *m_passTypeCombo = nullptr;
    QWidget *m_passTypeFrame = nullptr;
    QPushButton *m_languageStatusButton = nullptr;
    QLabel *m_languageNameLabel = nullptr;

    QCheckBox *m_aiEnabledCheck = nullptr;
    QLineEdit *m_aiModelEdit = nullptr;
    QLineEdit *m_allowableEdit = nullptr;
    QSpinBox *m_passNumberSpin = nullptr;
    QLineEdit *m_torchLengthEdit = nullptr;
    QLabel *m_torchUnitLabel = nullptr;
    QWidget *m_torchInputFrame = nullptr;
    QWidget *m_allowableInputFrame = nullptr;
    QLabel *m_allowableUnitLabel = nullptr;
    QPushButton *m_aiStatusButton = nullptr;
    QLabel *m_passUsageLabel = nullptr;
    QLabel *m_aiModelLabel = nullptr;
    QLabel *m_aiConfidenceLabel = nullptr;
    QLabel *m_passNumberLabel = nullptr;
    QLabel *m_torchLabel = nullptr;

    QComboBox *m_languageCombo = nullptr;
    QPushButton *m_languageRefreshButton = nullptr;
    QWidget *m_languageComboFrame = nullptr;

    struct PlcEntry
    {
        QString id;
        QString name;
        QString ip;
        quint16 port = 0;
        bool connected = false;
        bool busy = false;
    };

    struct PlcRow
    {
        QString id;
        QWidget *container = nullptr;
        QPushButton *statusButton = nullptr;
        QLabel *nameLabel = nullptr;
        QLineEdit *ipEdit = nullptr;
        QLineEdit *portEdit = nullptr;
        QLabel *ipLabel = nullptr;
        QLabel *portLabel = nullptr;
        QWidget *ipFrame = nullptr;
        QWidget *portFrame = nullptr;
    };

    QVector<PlcEntry> m_plcEntries;
    QVector<PlcRow> m_plcRows;
    QVBoxLayout *m_plcRowsLayout = nullptr;
    QWidget *m_busyOverlay = nullptr;
    BusyIndicator *m_globalSpinner = nullptr;
    bool m_analysisBusy = false;

    QSet<QWidget *> m_inputFrames;
    QHash<QWidget *, QWidget *> m_inputFrameChildren;
    QHash<QWidget *, int> m_inputFrameHoverDepth;

    QWidget *m_roiOverlay = nullptr;
    QLabel *m_roiTitleLabel = nullptr;
    QLabel *m_roiCameraLabel = nullptr;
    QPushButton *m_roiCloseButton = nullptr;
    CameraRoiWidget *m_roiEditor = nullptr;
    QString m_activeRoiCameraId;
    QString m_activeRoiAlias;

    struct CameraRoiProfile
    {
        QString id;
        QString slotId;
        QString name;
        QString alias;
        QRectF roi;
        QJsonObject analysis;
        QString serialPort;
        bool enabled = true;
    };

    QVector<CameraRoiProfile> m_cameraRoiProfiles;
    QHash<QString, int> m_roiAliasIndex;
    int m_activeRoiProfileIndex = -1;
    bool m_cameraRoiDirty = false;
    bool m_waitingForStoredRoi = false;
    QRectF m_expectedRoiRect;

    QWidget *m_cameraDetailOverlay = nullptr;
    QWidget *m_cameraDetailPanel = nullptr;
    QScrollArea *m_cameraDetailScroll = nullptr;
    QWidget *m_cameraDetailContent = nullptr;
    QLabel *m_cameraDetailBreadcrumb = nullptr;
    QLabel *m_cameraDetailSection = nullptr;
    QLabel *m_cameraDetailHint = nullptr;
    QWidget *m_cameraDetailPreviewContainer = nullptr;
    QVBoxLayout *m_cameraDetailPreviewLayout = nullptr;
    CameraPreviewWidget *m_cameraDetailPreview = nullptr;
    QPushButton *m_cameraDetailCancelButton = nullptr;
    QPushButton *m_cameraDetailSaveButton = nullptr;
    QComboBox *m_cameraDetailSerialCombo = nullptr;
    QLabel *m_cameraDetailSerialLabel = nullptr;
    QPushButton *m_cameraDetailSerialRefreshButton = nullptr;
    ToggleSwitch *m_cameraDetailVerticalFlipSwitch = nullptr;
    ToggleSwitch *m_cameraDetailHorizontalFlipSwitch = nullptr;
    QLabel *m_cameraDetailInvertLabel = nullptr;
    QLabel *m_cameraDetailVerticalLabel = nullptr;
    QLabel *m_cameraDetailHorizontalLabel = nullptr;
    QLabel *m_cameraDetailFocusAssistLabel = nullptr;
    QPushButton *m_cameraDetailInvertRefresh = nullptr;
    QLabel *m_cameraDetailBrightnessLabel = nullptr;
    QPushButton *m_cameraDetailBrightnessMinus = nullptr;
    QPushButton *m_cameraDetailBrightnessPlus = nullptr;
    QPushButton *m_cameraDetailBrightnessRefresh = nullptr;
    QVector<QPushButton *> m_cameraDetailBrightnessButtons;
    QLabel *m_cameraDetailSensitivityLabel = nullptr;
    QPushButton *m_cameraDetailSensitivityMinus = nullptr;
    QPushButton *m_cameraDetailSensitivityPlus = nullptr;
    QPushButton *m_cameraDetailSensitivityRefresh = nullptr;
    QVector<QPushButton *> m_cameraDetailSensitivityButtons;
    ToggleSwitch *m_cameraDetailFocusAssistSwitch = nullptr;
    QWidget *m_cameraDetailBusyOverlay = nullptr;
    BusyIndicator *m_cameraDetailBusySpinner = nullptr;
    QLabel *m_cameraDetailBusyLabel = nullptr;
    bool m_cameraDetailBusy = false;
    bool m_pendingSerialWrite = false;
    bool m_needCameraRestart = false;
    QString m_activeDetailCameraId;
    QString m_lastDetailCameraId;
    int m_activeDetailProfileIndex = -1;
    bool m_cameraDetailVisible = false;
    bool m_updatingCameraDetailUi = false;
    struct CameraDetailState
    {
        QString cameraId;
        QString slotLabel;
        QString displayName;
        QString serialPort;
        bool invertVertical = false;
        bool invertHorizontal = false;
        bool focusAssist = false;
        int brightness = 5;
        int sensitivity = 5;
    } m_detailState;
    QFutureWatcher<SerialCamState> m_serialReadWatcher;
    QFutureWatcher<SerialWriteResult> m_serialWriteWatcher;
};
