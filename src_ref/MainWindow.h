// Author: SeungJae Lee
// MainWindow interface: coordinates navigation, camera management, and page stack wiring.

#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QLabel>
#include <QJsonObject>
#include <QVector>
#include <QImage>
#include <QTimer>
#include <QSet>
#include "widgets/CameraPreviewWidget.h"
#include <memory>
#include <QStringList>
#include <QRect>
#include <QSize>
#include <optional>
#include <algorithm>

class QStackedWidget;
class QPushButton;

class WeldingPage;
class StoragePage;
class SettingsPage;
class CameraManager;
class RecordingManager;
class AiClient;
class PLCClient;
class LocalizationManager;
class QCamera;
class QCameraDevice;
class QCameraFormat;
class QMediaDevices;
class QToolButton;
class QButtonGroup;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    static QRectF defaultNormalizedRoi(const QSize &frameSize)
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

protected:
    void changeEvent(QEvent *event) override;

private:
    void setupUi();
    void setupNavigation();
    void setupPages();
    void makeConnections();
    void retranslateUi();
    void applySavedLanguagePreference();
    void updateNavigationVisibility();
    void ensureCameraPermission();
    void ensureMicrophonePermission();
    void showCameraPermissionWarning();
    void showMicrophonePermissionWarning();
    void initializeCameras();
    void refreshCameraDevices();
    QCameraFormat preferredCameraFormat(const QCameraDevice &device) const;
    void updateCameraFeatureStates();
    void persistCameraOverrides(const QString &cameraId, const QString &name, const QString &slotId);
    void loadAnalysisConfigs(const QSet<QString> &enabledCameras = QSet<QString>());
    struct AnalysisConfig
    {
        QString alias;
        QString url;
        QString streamKey;
        QStringList weldTypes;
        QString refType;
        double refScale = 26.0;
        QRect roi;               // capture ROI used for 512x512 extraction
        QRectF normalizedRoi;    // ROI expressed in 0..1 space (full frame reference)
        QSize frameSize = QSize(1920, 1080);
        bool enabled = false;
        double fps = 5.0; // desired AI request cadence for this camera
    };
    AnalysisConfig analysisConfigForCamera(const QString &cameraId) const;
    void applyAnalysisConfig(const QString &cameraId);
    void updateAiTimerForActiveCamera();
    void ensureActiveAnalysisCamera();
    QString cameraIdForStartup() const;
    void handleCameraAiToggle(const QString &cameraId, bool enabled);
    void handleAnalysisFinished(bool ok, const QJsonObject &results, const QString &message);
    void updateAiToggleLocking();
    QString aiExclusiveMessage() const;
    QVector<CameraPreviewWidget::AnalysisShape> parseAnalysisShapes(const QJsonObject &results,
                                                                    const AnalysisConfig &config,
                                                                    QRect *roiOut,
                                                                    QSize *frameSizeOut) const;
    static QJsonObject rectToJson(const QRect &rect);
    void updateAiState();
    void startAiSession();
    void stopAiSession(bool forceShutdown = false);
    void requestAiStartup();
    void handleAiModelStartupFinished(bool ok, const QString &message, const QJsonObject &modelInfo);
    void handleAiModelShutdownFinished(bool ok, const QString &message);
    void resetAiStartupState();
    void onAiTimerTick();
    void onFrameCaptured(const QString &cameraId, const QImage &frame);

    QWidget *m_navigationWidget = nullptr;
    QWidget *m_navigationContent = nullptr;
    QStackedWidget *m_stackedWidget = nullptr;
    QLabel *m_companyAvatar = nullptr;
    QLabel *m_companyName = nullptr;
    QPushButton *m_weldingButton = nullptr;
    QPushButton *m_storageButton = nullptr;
    QPushButton *m_settingsButton = nullptr;
    QLabel *m_appVersion = nullptr;
    QToolButton *m_toggleNavigationButton = nullptr;
    QButtonGroup *m_navigationButtonGroup = nullptr;

    std::unique_ptr<CameraManager> m_cameraManager;
    std::unique_ptr<RecordingManager> m_recordingManager;
    std::unique_ptr<AiClient> m_aiClient;
    std::unique_ptr<PLCClient> m_plcClient;
    std::unique_ptr<LocalizationManager> m_localizationManager;

    WeldingPage *m_weldingPage = nullptr;
    StoragePage *m_storagePage = nullptr;
    SettingsPage *m_settingsPage = nullptr;

    bool m_cameraPermissionWarningShown = false;
    bool m_microphonePermissionWarningShown = false;
    bool m_camerasInitialized = false;
    bool m_navigationCollapsed = false;

    QMediaDevices *m_mediaDevices = nullptr;
    QHash<QString, QPointer<QCamera>> m_cameraControllers;
    QMap<QString, AnalysisConfig> m_analysisConfigs;
    QString m_activeAnalysisCameraId;
    QSet<QString> m_enabledAnalysisCameras;
    QStringList m_enabledAnalysisOrder;
    QHash<QString, QImage> m_latestFrames;
    QTimer *m_aiTimer = nullptr;
    struct AiStartupContext
    {
        QString cameraId;
        QString baseUrl;
        QString streamKey;
        QStringList weldTypes;
        QString refType;
        double refScale = 0.0;
    };
    static constexpr int kAiStartupMaxAttempts = 3;
    AiStartupContext m_aiStartupContext;
    int m_aiStartupAttemptCount = 0;
    bool m_aiStartupWaitingForShutdown = false;
    bool m_aiSessionActive = false;
    bool m_analysisPending = false;
    bool m_aiReady = false;
};
