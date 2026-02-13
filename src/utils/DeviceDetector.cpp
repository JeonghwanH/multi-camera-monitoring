#include "DeviceDetector.h"
#include <QDebug>
#include <opencv2/opencv.hpp>

#ifdef __linux__
#include <dirent.h>
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

QList<DeviceInfo> DeviceDetector::detectDevices() {
    QList<DeviceInfo> devices;
    
#ifdef __linux__
    // Linux: Check /dev/video* devices
    DIR* dir = opendir("/dev");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            QString name = QString::fromUtf8(entry->d_name);
            if (name.startsWith("video")) {
                bool ok;
                int index = name.mid(5).toInt(&ok);
                if (ok) {
                    QString devicePath = "/dev/" + name;
                    int fd = open(devicePath.toUtf8().constData(), O_RDONLY);
                    if (fd >= 0) {
                        struct v4l2_capability cap;
                        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
                            // Check if it's a video capture device
                            if (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) {
                                DeviceInfo info;
                                info.index = index;
                                info.name = QString::fromUtf8(reinterpret_cast<char*>(cap.card));
                                info.available = true;
                                devices.append(info);
                            }
                        }
                        close(fd);
                    }
                }
            }
        }
        closedir(dir);
    }
    
    // Sort by index
    std::sort(devices.begin(), devices.end(), [](const DeviceInfo& a, const DeviceInfo& b) {
        return a.index < b.index;
    });
    
#else
    // macOS/Windows: Use OpenCV to probe devices
    // Scan detected range + 1 to allow detecting one new device
    // Stop after 2 consecutive failures
    int consecutiveFailures = 0;
    const int maxConsecutiveFailures = 2;
    int highestFoundIndex = -1;
    
    for (int i = 0; i < m_maxDevicesToCheck && consecutiveFailures < maxConsecutiveFailures; ++i) {
        QString deviceName;
        if (checkDevice(i, deviceName)) {
            DeviceInfo info;
            info.index = i;
            info.name = deviceName.isEmpty() ? QString("Camera %1").arg(i) : deviceName;
            info.available = true;
            devices.append(info);
            consecutiveFailures = 0;  // Reset on success
            highestFoundIndex = i;
        } else {
            consecutiveFailures++;
        }
    }
    
    // Always try one more index beyond the highest found (to detect new devices)
    if (highestFoundIndex >= 0 && consecutiveFailures >= maxConsecutiveFailures) {
        int nextIndex = highestFoundIndex + 1;
        if (nextIndex < m_maxDevicesToCheck) {
            QString deviceName;
            if (checkDevice(nextIndex, deviceName)) {
                DeviceInfo info;
                info.index = nextIndex;
                info.name = deviceName.isEmpty() ? QString("Camera %1").arg(nextIndex) : deviceName;
                info.available = true;
                devices.append(info);
            }
        }
    }
#endif
    
    return devices;
}

bool DeviceDetector::checkDevice(int index, QString& outName) {
    // Suppress OpenCV warnings during device probing
    cv::VideoCapture cap;
    
#ifdef __APPLE__
    // On macOS, only try AVFoundation - don't fall back to other backends
    // This prevents the "out of bound" warnings
    cap.open(index, cv::CAP_AVFOUNDATION);
    
    if (cap.isOpened()) {
        outName = QString("Camera %1").arg(index);
        cap.release();
        return true;
    }
    return false;
#elif defined(_WIN32)
    cap.open(index, cv::CAP_DSHOW);
#else
    cap.open(index, cv::CAP_V4L2);
#endif
    
    if (!cap.isOpened()) {
        // Try default backend (not on macOS)
        cap.open(index);
    }
    
    if (cap.isOpened()) {
        outName = QString("Camera %1").arg(index);
        cap.release();
        return true;
    }
    
    return false;
}

void DeviceDetector::startMonitoring(int intervalMs) {
    // Do initial detection
    m_lastKnownDevices = detectDevices();
    emit devicesChanged(m_lastKnownDevices);
    
    // Start periodic polling
    m_pollTimer->start(intervalMs);
    
    qDebug() << "DeviceDetector: Started monitoring with" << intervalMs << "ms interval";
    qDebug() << "DeviceDetector: Found" << m_lastKnownDevices.size() << "devices";
}

void DeviceDetector::stopMonitoring() {
    m_pollTimer->stop();
}

void DeviceDetector::pollDevices() {
    // Find the highest device index currently known
    int highestKnownIndex = -1;
    for (const auto& device : m_lastKnownDevices) {
        if (device.index > highestKnownIndex) {
            highestKnownIndex = device.index;
        }
    }
    
    // Only scan the NEXT device index (e.g., if device 0,1 exist, only check device 2)
    int nextIndexToCheck = highestKnownIndex + 1;
    
    if (nextIndexToCheck < m_maxDevicesToCheck) {
        QString deviceName;
        if (checkDevice(nextIndexToCheck, deviceName)) {
            // New device found!
            DeviceInfo info;
            info.index = nextIndexToCheck;
            info.name = deviceName.isEmpty() ? QString("Camera %1").arg(nextIndexToCheck) : deviceName;
            info.available = true;
            m_lastKnownDevices.append(info);
            
            qDebug() << "DeviceDetector: Device added -" << nextIndexToCheck << info.name;
            emit deviceAdded(nextIndexToCheck, info.name);
            emit devicesChanged(m_lastKnownDevices);
        }
    }
    
    // Also verify existing devices still exist (check for disconnection)
    QList<int> removedIndices;
    for (const auto& known : m_lastKnownDevices) {
        QString name;
        if (!checkDevice(known.index, name)) {
            removedIndices.append(known.index);
        }
    }
    
    // Remove disconnected devices
    for (int idx : removedIndices) {
        for (int i = 0; i < m_lastKnownDevices.size(); ++i) {
            if (m_lastKnownDevices[i].index == idx) {
                QString removedName = m_lastKnownDevices[i].name;
                m_lastKnownDevices.removeAt(i);
                qDebug() << "DeviceDetector: Device removed -" << idx << removedName;
                emit deviceRemoved(idx);
                break;
            }
        }
    }
    
    if (!removedIndices.isEmpty()) {
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

