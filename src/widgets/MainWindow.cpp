#include "MainWindow.h"
#include "HomeScreen.h"
#include "MonitoringScreen.h"
#include "SettingsScreen.h"
#include "utils/DeviceDetector.h"
#include "core/Config.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QDebug>
#include <QMessageBox>

namespace MCM {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_stackedWidget(new QStackedWidget(this))
    , m_homeScreen(nullptr)
    , m_monitoringScreen(nullptr)
    , m_settingsScreen(nullptr)
    , m_deviceDetector(new DeviceDetector(this))
{
    setupUi();
    loadStyleSheet();
    
    // Start device monitoring (poll every 5 seconds)
    m_deviceDetector->startMonitoring(5000);
}

MainWindow::~MainWindow() {
    m_deviceDetector->stopMonitoring();
}

void MainWindow::setupUi() {
    setWindowTitle("Multi-Camera Monitor");
    setMinimumSize(1280, 720);
    resize(1600, 900);
    
    // Create screens
    m_homeScreen = new HomeScreen(this);
    m_monitoringScreen = new MonitoringScreen(m_deviceDetector, this);
    m_settingsScreen = new SettingsScreen(this);
    
    // Add to stacked widget
    m_stackedWidget->addWidget(m_homeScreen);
    m_stackedWidget->addWidget(m_monitoringScreen);
    m_stackedWidget->addWidget(m_settingsScreen);
    
    setCentralWidget(m_stackedWidget);
    
    // Connect signals
    connect(m_homeScreen, &HomeScreen::streamingClicked, this, &MainWindow::showMonitoringScreen);
    connect(m_homeScreen, &HomeScreen::settingsClicked, this, &MainWindow::showSettingsScreen);
    
    connect(m_monitoringScreen, &MonitoringScreen::backRequested, this, &MainWindow::showHomeScreen);
    connect(m_settingsScreen, &SettingsScreen::backRequested, this, &MainWindow::showHomeScreen);
    
    // Start with home screen
    showHomeScreen();
}

void MainWindow::loadStyleSheet() {
    QFile styleFile(":/styles/styles.qss");
    if (!styleFile.exists()) {
        styleFile.setFileName("resources/styles.qss");
    }
    
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QString style = QString::fromUtf8(styleFile.readAll());
        qApp->setStyleSheet(style);
        styleFile.close();
    } else {
        qWarning() << "Could not load stylesheet";
    }
}

void MainWindow::showHomeScreen() {
    m_monitoringScreen->stopAllStreams();
    m_stackedWidget->setCurrentWidget(m_homeScreen);
}

void MainWindow::showMonitoringScreen() {
    m_stackedWidget->setCurrentWidget(m_monitoringScreen);
    m_monitoringScreen->startAllStreams();
}

void MainWindow::showSettingsScreen() {
    m_monitoringScreen->stopAllStreams();
    m_stackedWidget->setCurrentWidget(m_settingsScreen);
    m_settingsScreen->loadCurrentSettings();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Stop all streams before closing
    m_monitoringScreen->stopAllStreams();
    
    // Save configuration
    Config::instance().save();
    
    event->accept();
}

} // namespace MCM

