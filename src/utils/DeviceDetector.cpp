#include "DeviceDetector.h"
#include <QDebug>
#include <QMediaDevices>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstring>
#include <cstdlib>

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

// Count V4L2 capture-only devices (for verification)
int countV4L2CaptureDevices() {
    int count = 0;
    
    for (int i = 0; i < 32; ++i) {
        QString path = QString("/dev/video%1").arg(i);
        if (!QFile::exists(path)) continue;
        
        V4L2DeviceInfo info = getV4L2DeviceInfo(path);
        if (info.isCapture) {
            count++;
        }
    }
    
    return count;
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
    
    QList<QCameraDevice> qtDevices = QMediaDevices::videoInputs();
    int qtTotalCount = qtDevices.size();
    
    qDebug() << "DeviceDetector: Qt reports" << qtTotalCount << "total devices";
    
#ifdef Q_OS_LINUX
    // ========================================
    // Linux: Check backend type first
    // ========================================
    const char* backendEnv = std::getenv("QT_MEDIA_BACKEND");
    bool isFFmpegBackend = backendEnv && QString(backendEnv).toLower() == "ffmpeg";
    
    int captureCount;
    
    if (isFFmpegBackend) {
        // ========================================
        // FFmpeg backend: All Qt devices are captures
        // ========================================
        // FFmpeg only reports capture devices, not metadata
        // Device ID is the actual path (e.g., "/dev/video1")
        captureCount = qtTotalCount;
        qDebug() << "DeviceDetector: FFmpeg backend - using ALL" << captureCount << "Qt devices";
    } else {
        // ========================================
        // GStreamer/other backend: First half are captures
        // ========================================
        // GStreamer reports both capture and metadata devices
        // First half = captures, second half = metadata
        // Use V4L2 count for verification
        
        captureCount = qtTotalCount / 2;  // Default: first half are captures
        
        // V4L2 count for verification
        int v4l2Count = countV4L2CaptureDevices();
        
        qDebug() << "DeviceDetector: GStreamer backend - V4L2 captures:" << v4l2Count << ", Qt total:" << qtTotalCount;
        
        if (qtTotalCount / 2 == v4l2Count) {
            // Perfect match
            captureCount = v4l2Count;
            qDebug() << "DeviceDetector: ✓ MATCH - using" << captureCount << "capture devices";
        } else if (v4l2Count == qtTotalCount / 2 + 1) {
            // Qt missed one device
            captureCount = v4l2Count;
            qDebug() << "DeviceDetector: ⚠ Qt missed 1 device - using V4L2 count:" << captureCount;
        } else if (v4l2Count > qtTotalCount / 2 + 1) {
            // Qt still loading
            qDebug() << "DeviceDetector: ⏳ Qt still loading - using Qt count:" << captureCount;
        } else {
            qDebug() << "DeviceDetector: ? Unusual counts - using Qt first half:" << captureCount;
        }
    }
#else
    // ========================================
    // macOS/Windows: Use all Qt devices directly
    // ========================================
    // Qt only reports real cameras, no metadata devices
    int captureCount = qtTotalCount;
    qDebug() << "DeviceDetector: Non-Linux - using all" << captureCount << "Qt devices";
#endif
    
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
