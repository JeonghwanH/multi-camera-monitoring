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
    
    // Log all Qt devices for reference
    qDebug() << "=== Qt Device List ===";
    for (int q = 0; q < qtDevices.size(); ++q) {
        qDebug() << "  Qt[" << q << "] id:" << QString::fromUtf8(qtDevices[q].id()) 
                 << "name:" << qtDevices[q].description()
                 << "position:" << static_cast<int>(qtDevices[q].position());
    }
    qDebug() << "=== V4L2 to Qt Matching ===";
    
    // Log all V4L2 devices for reference
    qDebug() << "V4L2 devices to match:";
    for (int v = 0; v < v4l2Devices.size(); ++v) {
        qDebug() << "  V4L2[" << v << "] path:" << v4l2Devices[v].path 
                 << "card:" << v4l2Devices[v].card << "bus:" << v4l2Devices[v].busInfo;
    }
    
    for (int i = 0; i < v4l2Devices.size(); ++i) {
        const V4L2DeviceInfo& v4l2Dev = v4l2Devices[i];
        
        DeviceInfo info;
        info.index = i;
        info.name = v4l2Dev.card;
        info.devicePath = v4l2Dev.path;
        info.busInfo = v4l2Dev.busInfo;
        info.available = true;
        
        // Match by Qt ARRAY INDEX (not ID - IDs can have gaps!)
        // Qt array index is always sequential: 0, 1, 2, 3...
        // Qt device ID can have gaps: "0", "1", "2", "4"... (missing "3")
        //
        // V4L2 and Qt have DIFFERENT ordering:
        // V4L2 (interleaved):  0c, 0m, 1c, 1m, 2c, 2m  -> /dev/video0,1,2,3,4,5
        // Qt (separated):      0c, 1c, 2c, 0m, 1m, 2m  -> index 0,1,2,3,4,5
        //
        // Formula: Qt array index = V4L2 device number / 2
        
        qDebug() << "";
        qDebug() << "--- Matching V4L2[" << i << "]:" << v4l2Dev.path << "---";
        
        int targetQtIndex = -1;
        if (v4l2Dev.path.startsWith("/dev/video")) {
            bool ok;
            int v4l2DeviceNum = v4l2Dev.path.mid(10).toInt(&ok);
            if (ok) {
                targetQtIndex = v4l2DeviceNum / 2;  // Map to Qt array index
            }
        }
        qDebug() << "  V4L2 path:" << v4l2Dev.path << "-> Target Qt index:" << targetQtIndex;
        
        // Log all Qt devices for debugging
        for (int q = 0; q < qtDevices.size(); ++q) {
            const QCameraDevice& qtDev = qtDevices[q];
            QString qtId = QString::fromUtf8(qtDev.id());
            QString qtName = qtDev.description();
            int position = static_cast<int>(qtDev.position());
            bool isTarget = (q == targetQtIndex);
            qDebug() << "    Qt[" << q << "] id:" << qtId << "pos:" << position << "name:" << qtName
                     << (isTarget ? "<-- TARGET" : "");
        }
        
        // Use Qt array index directly (store as string for compatibility)
        if (targetQtIndex >= 0 && targetQtIndex < qtDevices.size()) {
            info.deviceId = QString::number(targetQtIndex);  // Store Qt INDEX, not ID
            qDebug() << "  >>> SELECTED: V4L2" << v4l2Dev.path << "-> Qt index:" << targetQtIndex
                     << "(" << qtDevices[targetQtIndex].description() << ")";
        } else {
            qWarning() << "  >>> WARNING: Qt index" << targetQtIndex << "out of range for" << v4l2Dev.path;
        }
        
        // Build display name with device path
        // Extract base name from card: "ProductName: Description" -> "ProductName"
        QString baseName = v4l2Dev.card;
        int colonIdx = baseName.indexOf(':');
        if (colonIdx > 0) {
            baseName = baseName.left(colonIdx).trimmed();
        }
        
        // Check if there are other devices with the same base name
        int sameNameCount = 0;
        int sameNameIndex = 0;
        for (int j = 0; j < v4l2Devices.size(); ++j) {
            QString otherBaseName = v4l2Devices[j].card;
            int otherColon = otherBaseName.indexOf(':');
            if (otherColon > 0) {
                otherBaseName = otherBaseName.left(otherColon).trimmed();
            }
            if (otherBaseName == baseName) {
                if (j < i) sameNameIndex++;
                sameNameCount++;
            }
        }
        
        QString displayName = baseName;
        if (sameNameCount > 1) {
            // Add index to differentiate: "AV.io SDI+ #1"
            displayName = QString("%1 #%2").arg(baseName).arg(sameNameIndex + 1);
        }
        
        // Always append the device path: "AV.io SDI+ #1 (/dev/video0)"
        info.name = QString("%1 (%2)").arg(displayName).arg(v4l2Dev.path);
        
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
            qDebug() << "DeviceDetector::cameraDeviceByIndex(" << index << ")"
                     << "Qt array index:" << info.deviceId  // deviceId now stores Qt INDEX
                     << "path:" << info.devicePath;
            
            QList<QCameraDevice> allDevices = QMediaDevices::videoInputs();
            
            // deviceId stores Qt ARRAY INDEX (not device ID)
            // Use it directly to get the device from the array
            if (!info.deviceId.isEmpty()) {
                bool ok;
                int qtIndex = info.deviceId.toInt(&ok);
                if (ok && qtIndex >= 0 && qtIndex < allDevices.size()) {
                    const QCameraDevice& device = allDevices[qtIndex];
                    qDebug() << "  Found Qt device at index" << qtIndex << ":"
                             << device.description() << "id:" << QString::fromUtf8(device.id());
                    return device;
                }
                qDebug() << "  WARNING: Qt index" << info.deviceId << "invalid or out of range";
            }
            
            // Fallback: try matching by name
            QString cardName = info.name;
            // Remove path suffix "(/dev/videoX)"
            int parenIndex = cardName.lastIndexOf(" (");
            if (parenIndex > 0) {
                cardName = cardName.left(parenIndex);
            }
            // Remove "#N" suffix
            int hashIndex = cardName.lastIndexOf(" #");
            if (hashIndex > 0) {
                cardName = cardName.left(hashIndex);
            }
            
            qDebug() << "  Fallback: looking for card name:" << cardName;
            for (const auto& device : allDevices) {
                if (device.description().contains(cardName)) {
                    qDebug() << "  Found by name fallback:" << device.description();
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
