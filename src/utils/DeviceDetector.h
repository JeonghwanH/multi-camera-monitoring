#ifndef DEVICEDETECTOR_H
#define DEVICEDETECTOR_H

#include <QObject>
#include <QTimer>
#include <QList>
#include <QString>
#include <QCameraDevice>

namespace MCM {

/**
 * @brief Information about a detected video device
 */
struct DeviceInfo {
    int index;           // Sequential index (0, 1, 2, ...) - our filtered numbering
    QString name;        // Human-readable device name
    QString deviceId;    // Qt device ID (used to retrieve actual QCameraDevice)
    bool available;      // Whether device is currently available
    
    bool operator==(const DeviceInfo& other) const {
        return index == other.index && deviceId == other.deviceId;
    }
};

/**
 * @brief Monitors for camera device connection/disconnection
 * 
 * Uses platform-specific APIs to detect available video devices.
 * On Linux, filters out metadata nodes and only includes actual video capture devices.
 * Can poll periodically to detect changes.
 */
class DeviceDetector : public QObject {
    Q_OBJECT

public:
    explicit DeviceDetector(QObject* parent = nullptr);
    ~DeviceDetector();

    /**
     * @brief Detect all available video devices (filtered, sequential indexing)
     * @return List of detected devices with sequential indices
     */
    QList<DeviceInfo> detectDevices();

    /**
     * @brief Start monitoring for device changes
     * @param intervalMs Polling interval in milliseconds
     */
    void startMonitoring(int intervalMs = 1000);

    /**
     * @brief Stop monitoring
     */
    void stopMonitoring();

    /**
     * @brief Check if a specific device index is available
     */
    bool isDeviceAvailable(int index) const;

    /**
     * @brief Get the last known device list
     */
    QList<DeviceInfo> lastKnownDevices() const { return m_lastKnownDevices; }

    /**
     * @brief Get device name by index
     */
    QString deviceName(int index) const;

    /**
     * @brief Get QCameraDevice by our filtered index
     * @param index Our sequential index (0, 1, 2, ...)
     * @return QCameraDevice or null device if not found
     */
    QCameraDevice cameraDeviceByIndex(int index) const;

    /**
     * @brief Get the total count of available (filtered) devices
     */
    int deviceCount() const { return m_lastKnownDevices.size(); }

    /**
     * @brief Get maximum number of devices to check
     */
    int maxDevicesToCheck() const { return m_maxDevicesToCheck; }

    /**
     * @brief Set maximum number of devices to check
     */
    void setMaxDevicesToCheck(int max) { m_maxDevicesToCheck = max; }

signals:
    /**
     * @brief Emitted when a new device is detected
     */
    void deviceAdded(int index, const QString& name);

    /**
     * @brief Emitted when a device is removed
     */
    void deviceRemoved(int index);

    /**
     * @brief Emitted when device list changes
     */
    void devicesChanged(const QList<MCM::DeviceInfo>& devices);

private slots:
    void pollDevices();

private:
    /**
     * @brief Check if a specific device index can be opened
     */
    bool checkDevice(int index, QString& outName);

    /**
     * @brief Check if a V4L2 device supports video capture (Linux only)
     * @param devicePath Path like "/dev/video0"
     * @return true if device supports VIDEO_CAPTURE
     */
    bool isVideoCaptureDevice(const QString& devicePath);

    QTimer* m_pollTimer;
    QList<DeviceInfo> m_lastKnownDevices;
    int m_maxDevicesToCheck{8};  // Check up to 8 devices (matches slot count)
};

} // namespace MCM

// Register metatype for signal/slot
Q_DECLARE_METATYPE(QList<MCM::DeviceInfo>)

#endif // DEVICEDETECTOR_H
