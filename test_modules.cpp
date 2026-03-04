// Test using actual modules: DeviceDetector + QtCameraCapture + OptimizedVideoWidget
// Build: cd build && cmake .. && make test_modules
// Run: ./test_modules [device_index]
//
// This tests if camera playback works correctly through our module stack

#ifdef Q_OS_MACOS
#include <QtPlugin>
Q_IMPORT_PLUGIN(QDarwinCameraPermissionPlugin)
#endif

#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QDebug>

// Our modules
#include "src/utils/DeviceDetector.h"
#include "src/capture/QtCameraCapture.h"
#include "src/widgets/OptimizedVideoWidget.h"

using namespace MCM;

class TestWindow : public QMainWindow
{
    Q_OBJECT

public:
    TestWindow(QWidget* parent = nullptr) : QMainWindow(parent)
    {
        setWindowTitle("Module Test - DeviceDetector + QtCameraCapture");
        resize(900, 700);

        // Central widget
        QWidget* central = new QWidget(this);
        QVBoxLayout* layout = new QVBoxLayout(central);
        setCentralWidget(central);

        // Info label
        m_infoLabel = new QLabel("Select a device to test", this);
        m_infoLabel->setStyleSheet("font-size: 14px; padding: 10px;");
        layout->addWidget(m_infoLabel);

        // Device selector
        QHBoxLayout* selectorLayout = new QHBoxLayout();
        selectorLayout->addWidget(new QLabel("Device:"));
        m_deviceCombo = new QComboBox(this);
        selectorLayout->addWidget(m_deviceCombo, 1);
        
        m_playButton = new QPushButton("Play", this);
        selectorLayout->addWidget(m_playButton);
        
        m_stopButton = new QPushButton("Stop", this);
        selectorLayout->addWidget(m_stopButton);
        
        layout->addLayout(selectorLayout);

        // Video widget (our actual module)
        m_videoWidget = new OptimizedVideoWidget(this);
        m_videoWidget->setMinimumSize(640, 480);
        layout->addWidget(m_videoWidget, 1);

        // Status label
        m_statusLabel = new QLabel("Status: Idle", this);
        m_statusLabel->setStyleSheet("font-size: 12px; color: gray; padding: 5px;");
        layout->addWidget(m_statusLabel);

        // Create device detector
        m_deviceDetector = new DeviceDetector(this);
        
        // Create camera capture (slot ID 99 for test)
        m_cameraCapture = new QtCameraCapture(99, this);

        // Connect signals
        connect(m_playButton, &QPushButton::clicked, this, &TestWindow::onPlay);
        connect(m_stopButton, &QPushButton::clicked, this, &TestWindow::onStop);
        connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
                this, &TestWindow::onDeviceChanged);
        
        connect(m_cameraCapture, &QtCameraCapture::connectionEstablished, this, [this]() {
            m_statusLabel->setText("Status: Connected - Camera active");
            m_statusLabel->setStyleSheet("font-size: 12px; color: green; padding: 5px;");
        });
        
        connect(m_cameraCapture, &QtCameraCapture::connectionLost, this, [this]() {
            m_statusLabel->setText("Status: Disconnected");
            m_statusLabel->setStyleSheet("font-size: 12px; color: orange; padding: 5px;");
        });
        
        connect(m_cameraCapture, &QtCameraCapture::errorOccurred, this, [this](const QString& error) {
            m_statusLabel->setText("Status: Error - " + error);
            m_statusLabel->setStyleSheet("font-size: 12px; color: red; padding: 5px;");
        });

        // Populate devices
        populateDevices();
    }

    ~TestWindow()
    {
        if (m_cameraCapture) {
            m_cameraCapture->stop();
        }
    }

private slots:
    void onPlay()
    {
        int index = m_deviceCombo->currentIndex();
        if (index < 0) return;

        qDebug() << "\n========== PLAY CLICKED ==========";
        qDebug() << "Selected combo index:" << index;
        
        // Get device info from our stored list
        if (index >= m_devices.size()) {
            m_statusLabel->setText("Status: Invalid device index");
            return;
        }

        const DeviceInfo& device = m_devices[index];
        qDebug() << "DeviceInfo:";
        qDebug() << "  index:" << device.index;
        qDebug() << "  name:" << device.name;
        qDebug() << "  deviceId:" << device.deviceId;
        qDebug() << "  devicePath:" << device.devicePath;
        qDebug() << "  available:" << device.available;

        // Stop any existing stream
        m_cameraCapture->stop();
        
        // Reset video widget for clean state
        m_videoWidget->resetVideoItem();
        
        // Set video output FIRST
        QGraphicsVideoItem* videoItem = m_videoWidget->videoItem();
        qDebug() << "VideoItem:" << videoItem;
        m_cameraCapture->setVideoOutput(videoItem);

        // Now set device and start
        // Use cameraDeviceByIndex which uses our V4L2->Qt mapping
        qDebug() << "\nCalling cameraDeviceByIndex(" << device.index << ")...";
        QCameraDevice qtDevice = m_deviceDetector->cameraDeviceByIndex(device.index);
        qDebug() << "Got QCameraDevice:";
        qDebug() << "  id:" << QString::fromUtf8(qtDevice.id());
        qDebug() << "  description:" << qtDevice.description();
        qDebug() << "  isNull:" << qtDevice.isNull();

        if (qtDevice.isNull()) {
            m_statusLabel->setText("Status: Failed to get Qt device");
            return;
        }

        m_statusLabel->setText("Status: Starting camera...");
        
        // Set device index (this will use our mapping internally)
        m_cameraCapture->setDeviceIndex(device.index);
        m_cameraCapture->start();
        
        qDebug() << "========== PLAY DONE ==========\n";
    }

    void onStop()
    {
        qDebug() << "\n========== STOP CLICKED ==========";
        m_cameraCapture->stop();
        m_videoWidget->clear();
        m_statusLabel->setText("Status: Stopped");
        m_statusLabel->setStyleSheet("font-size: 12px; color: gray; padding: 5px;");
        qDebug() << "========== STOP DONE ==========\n";
    }

    void onDeviceChanged(int index)
    {
        if (index < 0) return;
        
        if (index < m_devices.size()) {
            const DeviceInfo& dev = m_devices[index];
            QString info = QString("Device %1: %2\nPath: %3\nQt ID: %4")
                .arg(dev.index)
                .arg(dev.name)
                .arg(dev.devicePath)
                .arg(dev.deviceId);
            m_infoLabel->setText(info);
        }
    }

private:
    void populateDevices()
    {
        qDebug() << "\n========== POPULATING DEVICES ==========";
        
        // Start monitoring to populate lastKnownDevices
        m_deviceDetector->startMonitoring(5000);
        
        // Also get devices directly for immediate use
        m_devices = m_deviceDetector->detectDevices();
        
        qDebug() << "DeviceDetector returned" << m_devices.size() << "devices:";
        
        m_deviceCombo->clear();
        for (const DeviceInfo& dev : m_devices) {
            QString displayText = QString("[%1] %2").arg(dev.index).arg(dev.name);
            m_deviceCombo->addItem(displayText);
            
            qDebug() << "  [" << dev.index << "]" << dev.name 
                     << "| path:" << dev.devicePath 
                     << "| qtId:" << dev.deviceId;
        }
        
        if (!m_devices.isEmpty()) {
            onDeviceChanged(0);
        }
        
        qDebug() << "========== POPULATE DONE ==========\n";
    }
    
    QList<DeviceInfo> m_devices;  // Store devices locally

    QLabel* m_infoLabel;
    QLabel* m_statusLabel;
    QComboBox* m_deviceCombo;
    QPushButton* m_playButton;
    QPushButton* m_stopButton;
    OptimizedVideoWidget* m_videoWidget;
    DeviceDetector* m_deviceDetector;
    QtCameraCapture* m_cameraCapture;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    qDebug() << "======================================";
    qDebug() << "Module Test: DeviceDetector + QtCameraCapture";
    qDebug() << "======================================";

    TestWindow window;
    window.show();

    return app.exec();
}

#include "test_modules.moc"

