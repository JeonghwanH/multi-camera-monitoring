#include "DeviceDetector.h"
#include <QDebug>
#include <QMediaDevices>
#include <QCameraDevice>
#include <algorithm>

namespace MCM {

DeviceDetector::DeviceDetector(QObject* parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
{
    qRegisterMetaType<QList<MCM::DeviceInfo>>("QList<MCM::DeviceInfo>");
    connect(m_pollTimer, &QTimer::timeout, this, &DeviceDetector::pollDevices);
}

DeviceDetector::~DeviceDetector() {
    stopMonitoring();
}

QList<DeviceInfo> DeviceDetector::detectDevices() {
    QList<DeviceInfo> devices;
    
    // Use Qt's QMediaDevices (GPU-friendly, no OpenCV)
    QList<QCameraDevice> availableDevices = QMediaDevices::videoInputs();
    
    for (int i = 0; i < availableDevices.size(); ++i) {
        const QCameraDevice& device = availableDevices.at(i);
        DeviceInfo info;
        info.index = i;  // Index in QMediaDevices list
        info.name = device.description();
        info.available = true;
        devices.append(info);
    }
    
    // Sort by index
    std::sort(devices.begin(), devices.end(), [](const DeviceInfo& a, const DeviceInfo& b) {
        return a.index < b.index;
    });
    
    return devices;
}

bool DeviceDetector::checkDevice(int index, QString& outName) {
    QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    
    if (index >= 0 && index < devices.size()) {
        outName = devices.at(index).description();
        return true;
    }
    
    return false;
}

void DeviceDetector::startMonitoring(int intervalMs) {
    // Do initial detection
    m_lastKnownDevices = detectDevices();
    emit devicesChanged(m_lastKnownDevices);
    
    // Start periodic polling (as backup, QMediaDevices::videoInputsChanged is primary)
    m_pollTimer->start(intervalMs);
    
    qDebug() << "DeviceDetector: Started monitoring with" << intervalMs << "ms interval";
    qDebug() << "DeviceDetector: Found" << m_lastKnownDevices.size() << "devices";
}

void DeviceDetector::stopMonitoring() {
    m_pollTimer->stop();
}

void DeviceDetector::pollDevices() {
    QList<DeviceInfo> currentDevices = detectDevices();
    
    // Check for added devices
    for (const auto& current : currentDevices) {
        bool found = false;
        for (const auto& known : m_lastKnownDevices) {
            if (known.index == current.index && known.name == current.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            qDebug() << "DeviceDetector: Device added -" << current.index << current.name;
            emit deviceAdded(current.index, current.name);
        }
    }
    
    // Check for removed devices
    for (const auto& known : m_lastKnownDevices) {
        bool found = false;
        for (const auto& current : currentDevices) {
            if (known.index == current.index && known.name == current.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            qDebug() << "DeviceDetector: Device removed -" << known.index << known.name;
            emit deviceRemoved(known.index);
        }
    }
    
    // Update if changed
    if (currentDevices != m_lastKnownDevices) {
        m_lastKnownDevices = currentDevices;
        emit devicesChanged(m_lastKnownDevices);
    }
}

bool DeviceDetector::isDeviceAvailable(int index) const {
    for (const auto& device : m_lastKnownDevices) {
        if (device.index == index) {
            return device.available;
        }
    }
    return false;
}

QString DeviceDetector::deviceName(int index) const {
    for (const auto& device : m_lastKnownDevices) {
        if (device.index == index) {
            return device.name;
        }
    }
    return QString();
}

} // namespace MCM

