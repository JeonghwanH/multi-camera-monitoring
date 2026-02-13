/**
 * Multi-Camera Monitoring Application
 * 
 * A native C++ Qt application for monitoring multiple camera sources
 * with buffered playback and chunk-based recording.
 * 
 * Features:
 * - Configurable grid layout for camera slots
 * - Support for wired cameras and RTSP streams
 * - Independent capture threads per slot
 * - Buffered playback with maintenance threshold
 * - Chunk-based video recording
 * - Auto-detection of camera devices
 * - Expanded view on double-click
 */

// Import static Qt plugins for macOS camera permission
#ifdef Q_OS_DARWIN
#include <QtPlugin>
Q_IMPORT_PLUGIN(QDarwinCameraPermissionPlugin)
#endif

#include <QApplication>
#include <QDir>
#include <QDebug>

#include "widgets/MainWindow.h"
#include "core/Config.h"

int main(int argc, char *argv[]) {
    // Set application attributes before creating QApplication
    QApplication::setApplicationName("Multi-Camera Monitor");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("MCM");
    
    // Enable high DPI scaling
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    
    QApplication app(argc, argv);
    
    // Set application-wide font
    QFont appFont = app.font();
    appFont.setFamily("Segoe UI, SF Pro Display, -apple-system, sans-serif");
    app.setFont(appFont);
    
    // Load configuration
    QString configPath = "config.json";
    if (!QFile::exists(configPath)) {
        // Try in application directory
        configPath = QApplication::applicationDirPath() + "/config.json";
    }
    
    MCM::Config& config = MCM::Config::instance();
    if (!config.load(configPath)) {
        qWarning() << "Using default configuration";
    }
    
    // Ensure recordings directory exists
    QString recordingsDir = config.recording().outputDirectory;
    if (!QDir(recordingsDir).exists()) {
        QDir().mkpath(recordingsDir);
        qDebug() << "Created recordings directory:" << recordingsDir;
    }
    
    // Create and show main window
    MCM::MainWindow mainWindow;
    mainWindow.show();
    
    qDebug() << "Multi-Camera Monitor started";
    qDebug() << "Grid:" << config.grid().maxSlots << "slots ("
             << config.grid().rows << "x" << config.grid().columns << ")";
    qDebug() << "Buffer:" << config.buffer().frameCount << "frames, min"
             << config.buffer().minMaintenance;
    qDebug() << "Recording:" << (config.recording().enabled ? "enabled" : "disabled")
             << "- Chunk:" << config.recording().chunkDurationSeconds << "s";
    
    return app.exec();
}

