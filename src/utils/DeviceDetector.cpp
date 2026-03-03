#include "DeviceDetector.h"
#include <QDebug>
#include <QMediaDevices>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstring>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#endif

namespace {
#ifdef Q_OS_LINUX
struct V4L2DeviceInfo {
    QString path;       // e.g., "/dev/video0"
    QString card;       // e.g., "AV.io SDI+"
    QString busInfo;    // e.g., "usb-0000:80:14.0-2"
    bool isCapture;
};

// Get V4L2 device info
V4L2DeviceInfo getV4L2DeviceInfo(const QString& devicePath) {
    V4L2DeviceInfo info;
    info.path = devicePath;
    info.isCapture = false;
    
    int fd = open(devicePath.toUtf8().constData(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        return info;
    }
    
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        info.card = QString::fromUtf8((const char*)cap.card);
        info.busInfo = QString::fromUtf8((const char*)cap.bus_info);
        
        __u32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        info.isCapture = (caps & V4L2_CAP_VIDEO_CAPTURE) || (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE);
        
        // Exclude metadata-only devices
        bool isMetaOnly = (caps & V4L2_CAP_META_CAPTURE) && !info.isCapture;
        if (isMetaOnly) {
            info.isCapture = false;
        }
    }
    
    close(fd);
    return info;
}

// Enumerate all /dev/video* devices and filter to capture-only
QList<V4L2DeviceInfo> enumerateV4L2CaptureDevices() {
    QList<V4L2DeviceInfo> devices;
    QSet<QString> seenBusInfo;  // Track unique physical devices by bus_info
    
    // Check /dev/video0 through /dev/video31
    for (int i = 0; i < 32; ++i) {
        QString path = QString("/dev/video%1").arg(i);
        if (!QFile::exists(path)) {
            continue;
        }
        
        V4L2DeviceInfo info = getV4L2DeviceInfo(path);
        
        qDebug() << "V4L2 Device:" << path 
                 << "card:" << info.card 
                 << "bus:" << info.busInfo 
                 << "capture:" << info.isCapture;
        
        if (info.isCapture) {
            // Only add the FIRST capture node for each physical device (by bus_info)
            if (!seenBusInfo.contains(info.busInfo)) {
                seenBusInfo.insert(info.busInfo);
                devices.append(info);
                qDebug() << "  -> INCLUDED as unique device";
            } else {
                qDebug() << "  -> SKIPPED (duplicate bus_info)";
            }
        } else {
            qDebug() << "  -> SKIPPED (not capture)";
        }
    }
    
    return devices;
}
#endif
} // anonymous namespace

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
    
#ifdef Q_OS_LINUX
    // On Linux, enumerate /dev/video* directly using V4L2 to:
    // 1. Filter out metadata nodes
    // 2. Get unique physical devices by bus_info
    // 3. Match to Qt devices for actual camera usage
    
    QList<V4L2DeviceInfo> v4l2Devices = enumerateV4L2CaptureDevices();
    QList<QCameraDevice> qtDevices = QMediaDevices::videoInputs();
    
    qDebug() << "DeviceDetector: Found" << v4l2Devices.size() << "unique V4L2 capture devices";
    qDebug() << "DeviceDetector: Qt reports" << qtDevices.size() << "devices";
    
    for (int i = 0; i < v4l2Devices.size(); ++i) {
        const V4L2DeviceInfo& v4l2Dev = v4l2Devices[i];
        
        DeviceInfo info;
        info.index = i;
        info.name = v4l2Dev.card;
        info.devicePath = v4l2Dev.path;
        info.busInfo = v4l2Dev.busInfo;
        info.available = true;
        
        // Find matching Qt device by looking for one with matching name
        // that hasn't been used yet
        for (const QCameraDevice& qtDev : qtDevices) {
            QString qtName = qtDev.description();
            // Qt names have "(V4L2)" suffix, V4L2 card names don't
            if (qtName.contains(v4l2Dev.card)) {
                info.deviceId = QString::fromUtf8(qtDev.id());
                qDebug() << "  Matched V4L2" << v4l2Dev.path << "to Qt device id:" << info.deviceId;
                break;
            }
        }
        
        // Include a bus info hint in the display name to differentiate same-model devices
        if (v4l2Devices.size() > 1) {
            // Check if there are other devices with the same card name
            int sameNameCount = 0;
            int sameNameIndex = 0;
            for (int j = 0; j < v4l2Devices.size(); ++j) {
                if (v4l2Devices[j].card == v4l2Dev.card) {
                    if (j < i) sameNameIndex++;
                    sameNameCount++;
                }
            }
            if (sameNameCount > 1) {
                // Add index to differentiate: "AV.io SDI+ #1", "AV.io SDI+ #2"
                info.name = QString("%1 #%2").arg(v4l2Dev.card).arg(sameNameIndex + 1);
            }
        }
        
        devices.append(info);
        qDebug() << "  [" << info.index << "]" << info.name 
                 << "path:" << info.devicePath << "bus:" << info.busInfo;
    }
    
#else
    // On non-Linux platforms, use Qt's enumeration directly
    QList<QCameraDevice> availableDevices = QMediaDevices::videoInputs();
    
    qDebug() << "DeviceDetector: Qt found" << availableDevices.size() << "devices";
    
    for (int i = 0; i < availableDevices.size(); ++i) {
        const QCameraDevice& device = availableDevices.at(i);
        
        DeviceInfo info;
        info.index = i;
        info.name = device.description();
        info.deviceId = QString::fromUtf8(device.id());
        info.available = true;
        devices.append(info);
        
        qDebug() << "  [" << info.index << "]" << info.name << "(id:" << info.deviceId << ")";
    }
#endif
    
    qDebug() << "DeviceDetector: Total" << devices.size() << "capture devices";
    return devices;
}

QCameraDevice DeviceDetector::cameraDeviceByIndex(int index) const {
    // Find the device info with this index
    for (const auto& info : m_lastKnownDevices) {
        if (info.index == index) {
            QList<QCameraDevice> allDevices = QMediaDevices::videoInputs();
            
#ifdef Q_OS_LINUX
            // On Linux, we need to find the Qt device that matches our V4L2 device
            // Strategy: Find Qt device with matching name AND that points to the same
            // physical device (by trying to match path from V4L2 bus info)
            
            qDebug() << "DeviceDetector::cameraDeviceByIndex(" << index << ")"
                     << "Looking for:" << info.name << "path:" << info.devicePath 
                     << "bus:" << info.busInfo;
            
            // First try: exact deviceId match (if we stored it)
            if (!info.deviceId.isEmpty()) {
                for (const auto& device : allDevices) {
                    if (QString::fromUtf8(device.id()) == info.deviceId) {
                        qDebug() << "  Found by deviceId:" << info.deviceId;
                        return device;
                    }
                }
            }
            
            // Fallback: match by card name and take the Nth one that matches
            // This works because Qt lists devices in the same order as V4L2
            QString targetCard = info.name;
            // Remove our added "#N" suffix if present
            int hashIndex = targetCard.lastIndexOf(" #");
            if (hashIndex > 0) {
                targetCard = targetCard.left(hashIndex);
            }
            
            int targetInstance = 0;
            // Count how many devices with same card name come before this one
            for (const auto& other : m_lastKnownDevices) {
                if (other.index < index) {
                    QString otherCard = other.name;
                    int otherHash = otherCard.lastIndexOf(" #");
                    if (otherHash > 0) {
                        otherCard = otherCard.left(otherHash);
                    }
                    if (otherCard == targetCard) {
                        targetInstance++;
                    }
                }
            }
            
            qDebug() << "  Looking for card:" << targetCard << "instance:" << targetInstance;
            
            int foundInstance = 0;
            for (const auto& device : allDevices) {
                QString qtName = device.description();
                if (qtName.contains(targetCard)) {
                    if (foundInstance == targetInstance) {
                        qDebug() << "  Found Qt device:" << qtName << "id:" << QString::fromUtf8(device.id());
                        return device;
                    }
                    foundInstance++;
                }
            }
#else
            // On non-Linux, simple ID match
            for (const auto& device : allDevices) {
                if (QString::fromUtf8(device.id()) == info.deviceId) {
                    return device;
                }
            }
#endif
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
