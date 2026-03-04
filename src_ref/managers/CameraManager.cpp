// Author: SeungJae Lee
// CameraManager: central registry for camera metadata, visibility state, and Qt camera handles.

#include "CameraManager.h"

#include <QCamera>
#include <QDebug>

CameraManager::CameraManager(QObject *parent)
    : QObject(parent)
{
}

QVector<CameraManager::CameraInfo> CameraManager::cameras() const
{
    return m_cameras.values().toVector();
}

CameraManager::CameraInfo CameraManager::camera(const QString &id) const
{
    return m_cameras.value(id);
}

bool CameraManager::hasCamera(const QString &id) const
{
    return m_cameras.contains(id);
}

bool CameraManager::isCameraVisible(const QString &id) const
{
    if (!m_cameras.contains(id))
        return false;
    return m_cameras.value(id).visible;
}

QCamera *CameraManager::cameraHandle(const QString &id) const
{
    return m_cameraHandles.value(id);
}

void CameraManager::addCamera(const CameraInfo &info)
{
    if (m_cameras.contains(info.id))
        return;

    m_cameras.insert(info.id, info);
    emit cameraAdded(info);
}

void CameraManager::removeCamera(const QString &id)
{
    if (!m_cameras.contains(id))
        return;

    m_cameras.remove(id);
    m_cameraHandles.remove(id);
    emit cameraRemoved(id);
}

void CameraManager::updateCamera(const CameraInfo &info)
{
    if (!m_cameras.contains(info.id))
        return;

    m_cameras[info.id] = info;
    emit cameraUpdated(info);
}

void CameraManager::setCameraVisibility(const QString &id, bool visible)
{
    if (!m_cameras.contains(id))
        return;

    auto cameraInfo = m_cameras.value(id);
    if (cameraInfo.visible == visible)
        return;

    cameraInfo.visible = visible;
    m_cameras[id] = cameraInfo;
    emit cameraVisibilityChanged(id, visible);
}

void CameraManager::assignCameraHandle(const QString &id, QCamera *camera)
{
    if (!m_cameras.contains(id))
        return;

    m_cameraHandles[id] = camera;
    qInfo() << "[CameraManager] camera handle assigned" << id << camera;
    // Notify preview layers so they can attach to the live QCamera instance.
    emit cameraHandleAssigned(id, camera);
}
