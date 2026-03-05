// Test Qt devices directly - select from QMediaDevices::videoInputs()
// Build: cd build && cmake .. && make test_qt_devices
// Run: ./test_qt_devices
//
// This tests Qt camera playback WITHOUT our V4L2 mapping

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
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QCameraDevice>

class TestQtDevices : public QMainWindow
{
    Q_OBJECT

public:
    TestQtDevices(QWidget* parent = nullptr) : QMainWindow(parent)
    {
        setWindowTitle("Test Qt Devices Directly");
        resize(900, 700);

        QWidget* central = new QWidget(this);
        QVBoxLayout* layout = new QVBoxLayout(central);
        setCentralWidget(central);

        // Info label
        m_infoLabel = new QLabel("Select a Qt device to test", this);
        m_infoLabel->setStyleSheet("font-size: 14px; padding: 10px; background: #f0f0f0;");
        layout->addWidget(m_infoLabel);

        // Device selector
        QHBoxLayout* selectorLayout = new QHBoxLayout();
        selectorLayout->addWidget(new QLabel("Qt Device:"));
        m_deviceCombo = new QComboBox(this);
        m_deviceCombo->setMinimumWidth(400);
        selectorLayout->addWidget(m_deviceCombo, 1);
        
        m_playButton = new QPushButton("Play", this);
        m_playButton->setStyleSheet("background: #4CAF50; color: white; padding: 8px 16px;");
        selectorLayout->addWidget(m_playButton);
        
        m_stopButton = new QPushButton("Stop", this);
        m_stopButton->setStyleSheet("background: #f44336; color: white; padding: 8px 16px;");
        selectorLayout->addWidget(m_stopButton);
        
        m_refreshButton = new QPushButton("Refresh", this);
        selectorLayout->addWidget(m_refreshButton);
        
        layout->addLayout(selectorLayout);

        // Video display
        m_graphicsView = new QGraphicsView(this);
        m_graphicsScene = new QGraphicsScene(this);
        m_graphicsView->setScene(m_graphicsScene);
        m_graphicsView->setBackgroundBrush(Qt::black);
        m_graphicsView->setMinimumSize(640, 480);
        layout->addWidget(m_graphicsView, 1);

        // Video item
        m_videoItem = new QGraphicsVideoItem();
        m_graphicsScene->addItem(m_videoItem);

        // Status label
        m_statusLabel = new QLabel("Status: Idle", this);
        m_statusLabel->setStyleSheet("font-size: 12px; color: gray; padding: 5px;");
        layout->addWidget(m_statusLabel);

        // Create capture session
        m_session = new QMediaCaptureSession(this);

        // Connect signals
        connect(m_playButton, &QPushButton::clicked, this, &TestQtDevices::onPlay);
        connect(m_stopButton, &QPushButton::clicked, this, &TestQtDevices::onStop);
        connect(m_refreshButton, &QPushButton::clicked, this, &TestQtDevices::populateDevices);
        connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
                this, &TestQtDevices::onDeviceChanged);

        // Populate devices
        populateDevices();
    }

    ~TestQtDevices()
    {
        onStop();
    }

private slots:
    void populateDevices()
    {
        qDebug() << "\n========== POPULATING QT DEVICES ==========";
        
        m_devices = QMediaDevices::videoInputs();
        
        qDebug() << "QMediaDevices::videoInputs() returned" << m_devices.size() << "devices:";
        
        m_deviceCombo->clear();
        for (int i = 0; i < m_devices.size(); ++i) {
            const QCameraDevice& dev = m_devices[i];
            QString displayText = QString("Qt[%1] id:\"%2\" - %3")
                .arg(i)
                .arg(QString::fromUtf8(dev.id()))
                .arg(dev.description());
            m_deviceCombo->addItem(displayText);
            
            qDebug() << "  Qt[" << i << "] id:" << QString::fromUtf8(dev.id())
                     << "name:" << dev.description()
                     << "pos:" << static_cast<int>(dev.position());
        }
        
        if (!m_devices.isEmpty()) {
            onDeviceChanged(0);
        }
        
        qDebug() << "========== POPULATE DONE ==========\n";
    }

    void onDeviceChanged(int index)
    {
        if (index < 0 || index >= m_devices.size()) return;
        
        const QCameraDevice& dev = m_devices[index];
        QString info = QString(
            "Qt Array Index: %1\n"
            "Device ID: \"%2\"\n"
            "Description: %3\n"
            "Position: %4"
        ).arg(index)
         .arg(QString::fromUtf8(dev.id()))
         .arg(dev.description())
         .arg(static_cast<int>(dev.position()));
        
        m_infoLabel->setText(info);
    }

    void onPlay()
    {
        int index = m_deviceCombo->currentIndex();
        if (index < 0 || index >= m_devices.size()) return;

        qDebug() << "\n========== PLAY CLICKED ==========";
        qDebug() << "Selected Qt index:" << index;
        
        const QCameraDevice& device = m_devices[index];
        qDebug() << "QCameraDevice:";
        qDebug() << "  id:" << QString::fromUtf8(device.id());
        qDebug() << "  description:" << device.description();
        qDebug() << "  isNull:" << device.isNull();

        // Stop existing camera
        onStop();

        // Create new camera
        m_camera = new QCamera(device, this);
        
        connect(m_camera, &QCamera::activeChanged, this, [this](bool active) {
            qDebug() << "Camera activeChanged:" << active;
            if (active) {
                m_statusLabel->setText("Status: Playing");
                m_statusLabel->setStyleSheet("font-size: 12px; color: green; padding: 5px;");
            }
        });
        
        connect(m_camera, &QCamera::errorOccurred, this, 
            [this](QCamera::Error error, const QString& errorString) {
            qDebug() << "Camera error:" << error << errorString;
            m_statusLabel->setText("Status: Error - " + errorString);
            m_statusLabel->setStyleSheet("font-size: 12px; color: red; padding: 5px;");
        });

        // Connect to session
        m_session->setCamera(m_camera);
        m_session->setVideoOutput(m_videoItem);

        // Configure format
        auto formats = device.videoFormats();
        if (!formats.isEmpty()) {
            // Find 720p or similar
            for (const auto& fmt : formats) {
                if (fmt.resolution().height() == 720) {
                    m_camera->setCameraFormat(fmt);
                    qDebug() << "Set format:" << fmt.resolution() << "@" << fmt.maxFrameRate();
                    break;
                }
            }
        }

        m_statusLabel->setText("Status: Starting...");
        m_camera->start();
        
        qDebug() << "========== PLAY DONE ==========\n";
    }

    void onStop()
    {
        qDebug() << "\n========== STOP ==========";
        
        if (m_camera) {
            m_camera->stop();
            m_session->setCamera(nullptr);
            delete m_camera;
            m_camera = nullptr;
        }
        
        m_statusLabel->setText("Status: Stopped");
        m_statusLabel->setStyleSheet("font-size: 12px; color: gray; padding: 5px;");
        
        qDebug() << "========== STOP DONE ==========\n";
    }

private:
    QLabel* m_infoLabel;
    QLabel* m_statusLabel;
    QComboBox* m_deviceCombo;
    QPushButton* m_playButton;
    QPushButton* m_stopButton;
    QPushButton* m_refreshButton;
    
    QGraphicsView* m_graphicsView;
    QGraphicsScene* m_graphicsScene;
    QGraphicsVideoItem* m_videoItem;
    
    QList<QCameraDevice> m_devices;
    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_session = nullptr;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    qDebug() << "======================================";
    qDebug() << "Test Qt Devices Directly";
    qDebug() << "(No V4L2 mapping - raw Qt devices)";
    qDebug() << "======================================";

    TestQtDevices window;
    window.show();

    return app.exec();
}

#include "test_qt_devices.moc"

