#include "DeviceDetector.h"
#include <QDebug>
#include <QMediaDevices>
#include <algorithm>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#endif

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

bool DeviceDetector::isVideoCaptureDevice(const QString& devicePath) {
#ifdef Q_OS_LINUX
    // On Linux, check V4L2 capabilities to filter out metadata nodes
    int fd = open(devicePath.toUtf8().constData(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        qDebug() << "DeviceDetector: Cannot open" << devicePath << "for capability check";
        return false;
    }
    
    struct v4l2_capability cap;
    bool isCapture = false;
    
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        // Check for video capture capability (single-planar or multi-planar)
        isCapture = (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) ||
                   (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE);
        
        qDebug() << "DeviceDetector: Device" << devicePath
                 << "card:" << (const char*)cap.card
                 << "caps:" << Qt::hex << cap.device_caps
                 << "VIDEO_CAPTURE:" << isCapture;
    } else {
        qWarning() << "DeviceDetector: VIDIOC_QUERYCAP failed for" << devicePath;
    }
    
    close(fd);
    return isCapture;
#else
    // On non-Linux platforms, assume all devices are capture devices
    Q_UNUSED(devicePath);
    return true;
#endif
}

QList<DeviceInfo> DeviceDetector::detectDevices() {
    QList<DeviceInfo> devices;
    
    // Use Qt's QMediaDevices (GPU-friendly, no OpenCV)
    QList<QCameraDevice> availableDevices = QMediaDevices::videoInputs();
    
    qDebug() << "DeviceDetector: Qt found" << availableDevices.size() << "raw devices";
    
    int filteredIndex = 0;  // Our sequential index
    
    for (int i = 0; i < availableDevices.size(); ++i) {
        const QCameraDevice& device = availableDevices.at(i);
        QString deviceId = QString::fromUtf8(device.id());
        QString deviceName = device.description();
        
        qDebug() << "  Raw device[" << i << "]:" << deviceName << "id:" << deviceId;
        
#ifdef Q_OS_LINUX
        // On Linux, the device ID is typically the device path like "/dev/video0"
        // Filter to only include actual video capture devices
        if (!isVideoCaptureDevice(deviceId)) {
            qDebug() << "    -> SKIPPED (not a capture device)";
            continue;
        }
        qDebug() << "    -> INCLUDED as filtered index" << filteredIndex;
#endif
        
        DeviceInfo info;
        info.index = filteredIndex;  // Sequential index after filtering
        info.name = deviceName;
        info.deviceId = deviceId;
        info.available = true;
        devices.append(info);
        
        filteredIndex++;
    }
    
    qDebug() << "DeviceDetector: Filtered to" << devices.size() << "capture devices";
    for (const auto& dev : devices) {
        qDebug() << "  [" << dev.index << "]" << dev.name << "(id:" << dev.deviceId << ")";
    }
    
    return devices;
}

QCameraDevice DeviceDetector::cameraDeviceByIndex(int index) const {
    // Find the device info with this index
    for (const auto& info : m_lastKnownDevices) {
        if (info.index == index) {
            // Find the actual QCameraDevice with matching ID
            QList<QCameraDevice> allDevices = QMediaDevices::videoInputs();
            for (const auto& device : allDevices) {
                if (QString::fromUtf8(device.id()) == info.deviceId) {
                    return device;
                }
            }
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
    // Do initial detection
    m_lastKnownDevices = detectDevices();
    emit devicesChanged(m_lastKnownDevices);
    
    // Start periodic polling (as backup, QMediaDevices::videoInputsChanged is primary)
    m_pollTimer->start(intervalMs);
    
    qDebug() << "DeviceDetector: Started monitoring with" << intervalMs << "ms interval";
    qDebug() << "DeviceDetector: Found" << m_lastKnownDevices.size() << "capture devices";
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
