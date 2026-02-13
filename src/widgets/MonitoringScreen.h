#ifndef MONITORINGSCREEN_H
#define MONITORINGSCREEN_H

#include <QWidget>
#include <QGridLayout>
#include <QVector>
#include <QPushButton>

namespace MCM {

class CameraSlot;
class DeviceDetector;

/**
 * @brief Camera monitoring screen with grid of camera slots
 * 
 * Displays a configurable grid of CameraSlot widgets.
 * Each slot can display a different camera source.
 */
class MonitoringScreen : public QWidget {
    Q_OBJECT

public:
    explicit MonitoringScreen(DeviceDetector* detector, QWidget* parent = nullptr);
    ~MonitoringScreen();

    /**
     * @brief Start all camera streams
     */
    void startAllStreams();

    /**
     * @brief Stop all camera streams
     */
    void stopAllStreams();

    /**
     * @brief Rebuild the grid based on current configuration
     */
    void rebuildGrid();

signals:
    void backRequested();

private slots:
    void onSlotDoubleClicked(int slotIndex);
    void onDevicesChanged();

private:
    void setupUi();
    void createSlots();
    void clearSlots();

    QGridLayout* m_gridLayout;
    QVector<CameraSlot*> m_slots;
    QPushButton* m_backButton;
    DeviceDetector* m_deviceDetector;
    
    bool m_streaming{false};
};

} // namespace MCM

#endif // MONITORINGSCREEN_H

