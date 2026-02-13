#ifndef DEVICEDETECTOR_H
#define DEVICEDETECTOR_H

#include <QObject>
#include <QTimer>
#include <QList>
#include <QString>

namespace MCM {

/**
 * @brief Information about a detected video device
 */
struct DeviceInfo {
    int index;           // Device index (0, 1, 2, ...)
    QString name;        // Human-readable device name
    bool available;      // Whether device is currently available
    
    bool operator==(const DeviceInfo& other) const {
        return index == other.index && name == other.name;
    }
};

/**
 * @brief Monitors for camera device connection/disconnection
 * 
 * Uses platform-specific APIs to detect available video devices.
 * Can poll periodically to detect changes.
 */
class DeviceDetector : public QObject {
    Q_OBJECT

public:
    explicit DeviceDetector(QObject* parent = nullptr);
    ~DeviceDetector();

    /**
     * @brief Detect all available video devices
     * @return List of detected devices
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

    QTimer* m_pollTimer;
    QList<DeviceInfo> m_lastKnownDevices;
    int m_maxDevicesToCheck{8};  // Check up to 8 devices (matches slot count)
};

} // namespace MCM

// Register metatype for signal/slot
Q_DECLARE_METATYPE(QList<MCM::DeviceInfo>)

#endif // DEVICEDETECTOR_H

