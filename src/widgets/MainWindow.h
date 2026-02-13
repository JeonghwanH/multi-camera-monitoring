#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>

namespace MCM {

class HomeScreen;
class MonitoringScreen;
class SettingsScreen;
class DeviceDetector;

/**
 * @brief Main application window
 * 
 * Contains a stacked widget to switch between:
 * - HomeScreen (initial screen with navigation buttons)
 * - MonitoringScreen (camera grid)
 * - SettingsScreen (configuration)
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    /**
     * @brief Show the home screen
     */
    void showHomeScreen();

    /**
     * @brief Show the monitoring screen
     */
    void showMonitoringScreen();

    /**
     * @brief Show the settings screen
     */
    void showSettingsScreen();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void loadStyleSheet();

    QStackedWidget* m_stackedWidget;
    HomeScreen* m_homeScreen;
    MonitoringScreen* m_monitoringScreen;
    SettingsScreen* m_settingsScreen;
    DeviceDetector* m_deviceDetector;
};

} // namespace MCM

#endif // MAINWINDOW_H

