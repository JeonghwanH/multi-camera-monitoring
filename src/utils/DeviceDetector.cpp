#include "DeviceDetector.h"
#include <QDebug>
#include <QMediaDevices>

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
    
    QList<QCameraDevice> qtDevices = QMediaDevices::videoInputs();
    int qtTotalCount = qtDevices.size();
    
    qDebug() << "DeviceDetector: Qt reports" << qtTotalCount << "total devices";
    
    // ========================================
    // Use all Qt devices directly
    // ========================================
    // On Linux with FFmpeg backend (default): Qt reports only capture devices
    // On macOS/Windows: Qt reports only real cameras
    int captureCount = qtTotalCount;
    qDebug() << "DeviceDetector: Using all" << captureCount << "Qt devices";
    
    // Log all Qt devices
    qDebug() << "=== Qt Device List ===";
    for (int i = 0; i < qtTotalCount; ++i) {
        const QCameraDevice& dev = qtDevices[i];
        QString marker = (i < captureCount) ? "CAPTURE" : "metadata";
        qDebug() << "  Qt[" << i << "]" << dev.description() 
                 << "id:" << QString::fromUtf8(dev.id()) << "-" << marker;
    }
    
    // Build capture device list (first captureCount devices)
    QMap<QString, int> nameCount;  // For duplicate numbering
    
    qDebug() << "=== Capture Devices ===";
    for (int i = 0; i < qMin(captureCount, qtTotalCount); ++i) {
        const QCameraDevice& dev = qtDevices[i];
        
        DeviceInfo info;
        info.index = i;
        info.deviceId = QString::number(i);  // Store Qt array index
        info.available = true;
        
        // Build display name
        QString baseName = dev.description();
        // Remove " (V4L2)" suffix for cleaner display
        if (baseName.endsWith(" (V4L2)")) {
            baseName = baseName.left(baseName.length() - 7);
        }
        
        // Handle duplicate names
        nameCount[baseName]++;
        
        // Check total count of same name for numbering
        int totalSameName = 0;
        for (int j = 0; j < qMin(captureCount, qtTotalCount); ++j) {
            QString otherName = qtDevices[j].description();
            if (otherName.endsWith(" (V4L2)")) {
                otherName = otherName.left(otherName.length() - 7);
            }
            if (otherName == baseName) totalSameName++;
        }
        
        if (totalSameName > 1) {
            info.name = QString("%1 #%2").arg(baseName).arg(nameCount[baseName]);
        } else {
            info.name = baseName;
        }
        
        devices.append(info);
        qDebug() << "  [" << info.index << "]" << info.name << "-> Qt index:" << info.deviceId;
    }
    
    qDebug() << "DeviceDetector: Total" << devices.size() << "capture devices available";
    return devices;
}

QCameraDevice DeviceDetector::cameraDeviceByIndex(int index) const {
    // deviceId stores Qt array index directly - simple lookup
    for (const auto& info : m_lastKnownDevices) {
        if (info.index == index) {
            QList<QCameraDevice> qtDevices = QMediaDevices::videoInputs();
            
            bool ok;
            int qtIndex = info.deviceId.toInt(&ok);
            
            qDebug() << "DeviceDetector::cameraDeviceByIndex(" << index << ")"
                     << "-> Qt[" << qtIndex << "]";
            
            if (ok && qtIndex >= 0 && qtIndex < qtDevices.size()) {
                const QCameraDevice& device = qtDevices[qtIndex];
                qDebug() << "  Found:" << device.description();
                return device;
            }
            
            qWarning() << "  Qt index" << qtIndex << "out of range (have" << qtDevices.size() << ")";
            break;
        }
    }
    
    qWarning() << "DeviceDetector: No device found for filtered index" << index;
    return QCameraDevice();  // Return null device
}

bool DeviceDetector::checkDevice(int index, QString& outName) {
    for (const auto& device : m_lastKnownDevices) {
        if (device.index == index) {
            outName = device.name;
            return device.available;
        }
    }
    return false;
}

void DeviceDetector::startMonitoring(int intervalMs) {
    qDebug() << "DeviceDetector: Starting monitoring with" << intervalMs << "ms interval";
    
    // Delay initial detection to prevent main thread blocking
    // This allows Qt event loop to start and video surfaces to initialize
    QTimer::singleShot(100, this, [this, intervalMs]() {
        m_lastKnownDevices = detectDevices();
        emit devicesChanged(m_lastKnownDevices);
        
        qDebug() << "DeviceDetector: Initial detection complete -" 
                 << m_lastKnownDevices.size() << "capture devices";
        
        // Start periodic polling
        m_pollTimer->start(intervalMs);
    });
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
            // Match by device ID, not index (index may shift)
            if (known.deviceId == current.deviceId) {
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
            if (known.deviceId == current.deviceId) {
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
