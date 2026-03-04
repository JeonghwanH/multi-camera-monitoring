// Author: SeungJae Lee
// CameraManager interface: tracks camera metadata, visibility, and shared QCamera handles.

#pragma once

#include <QObject>
#include <QMap>
#include <QPointer>
#include <QString>
#include <QVector>

class QCamera;

class CameraManager : public QObject
{
    Q_OBJECT

public:
    struct CameraInfo
    {
        struct SettingState
        {
            QString name;
            bool enabled = false;
        };

        QString id;
        QString name;
        QString alias;
        QString slotId;
        double fps = 0.0;
        QString status;
        bool visible = true;
        QVector<SettingState> settings;
    };

    explicit CameraManager(QObject *parent = nullptr);

    QVector<CameraInfo> cameras() const;
    CameraInfo camera(const QString &id) const;
    bool hasCamera(const QString &id) const;
    bool isCameraVisible(const QString &id) const;
    QCamera *cameraHandle(const QString &id) const;

public slots:
    void addCamera(const CameraInfo &info);
    void removeCamera(const QString &id);
    void updateCamera(const CameraInfo &info);
    void setCameraVisibility(const QString &id, bool visible);
    void assignCameraHandle(const QString &id, QCamera *camera);

signals:
    void cameraAdded(const CameraManager::CameraInfo &info);
    void cameraRemoved(const QString &id);
    void cameraUpdated(const CameraManager::CameraInfo &info);
    void cameraVisibilityChanged(const QString &id, bool visible);
    void cameraHandleAssigned(const QString &id, QCamera *camera);

private:
    // Stored camera metadata keyed by id; handles keep ownership with Qt smart pointers.
    QMap<QString, CameraInfo> m_cameras;
    QMap<QString, QPointer<QCamera>> m_cameraHandles;
};
