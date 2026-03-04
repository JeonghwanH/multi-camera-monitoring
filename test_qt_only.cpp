// Test Qt-only device list with V4L2 count verification
// Build: cd build && cmake .. && make test_qt_only
// Run: ./test_qt_only
//
// Strategy:
// - Use Qt devices directly (first half = captures)
// - V4L2 count as verification
// - If Qt == V4L2: use Qt first half
// - If V4L2 is 1 more: Qt missed one, use V4L2 count for split
// - If V4L2 is 2+ more: still loading, use Qt count

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
#include <QDir>
#include <QTimer>

#ifdef Q_OS_LINUX
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

// Count V4L2 capture-only devices
int countV4L2CaptureDevices()
{
    int captureCount = 0;
    
    QDir devDir("/dev");
    QStringList videoDevices = devDir.entryList(QStringList() << "video*", QDir::System);
    
    for (const QString& devName : videoDevices) {
        QString devPath = "/dev/" + devName;
        
        int fd = open(devPath.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        
        struct v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            bool isCapture = (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
            bool isMeta = (cap.device_caps & V4L2_CAP_META_CAPTURE) != 0;
            
            if (isCapture && !isMeta) {
                captureCount++;
            }
        }
        close(fd);
    }
    
    return captureCount;
}
#else
int countV4L2CaptureDevices() { return 0; }  // Not available on non-Linux
#endif

class TestQtOnly : public QMainWindow
{
    Q_OBJECT

public:
    TestQtOnly(QWidget* parent = nullptr) : QMainWindow(parent)
    {
        setWindowTitle("Qt-Only Device List (with V4L2 verification)");
        resize(950, 750);

        QWidget* central = new QWidget(this);
        QVBoxLayout* layout = new QVBoxLayout(central);
        setCentralWidget(central);

        // Count info
        m_countLabel = new QLabel(this);
        m_countLabel->setStyleSheet("font-size: 13px; padding: 10px; background: #e8f5e9; border: 1px solid #4CAF50;");
        layout->addWidget(m_countLabel);

        // Device selector
        QHBoxLayout* selectorLayout = new QHBoxLayout();
        selectorLayout->addWidget(new QLabel("Capture Device:"));
        m_deviceCombo = new QComboBox(this);
        m_deviceCombo->setMinimumWidth(450);
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

        // Device info
        m_infoLabel = new QLabel("Select a device", this);
        m_infoLabel->setStyleSheet("font-size: 12px; padding: 8px; background: #f5f5f5;");
        layout->addWidget(m_infoLabel);

        // Video display
        m_graphicsView = new QGraphicsView(this);
        m_graphicsScene = new QGraphicsScene(this);
        m_graphicsView->setScene(m_graphicsScene);
        m_graphicsView->setBackgroundBrush(Qt::black);
        m_graphicsView->setMinimumSize(640, 480);
        layout->addWidget(m_graphicsView, 1);

        m_videoItem = new QGraphicsVideoItem();
        m_graphicsScene->addItem(m_videoItem);

        // Status
        m_statusLabel = new QLabel("Status: Idle", this);
        m_statusLabel->setStyleSheet("font-size: 12px; color: gray; padding: 5px;");
        layout->addWidget(m_statusLabel);

        // Session
        m_session = new QMediaCaptureSession(this);

        // Connections
        connect(m_playButton, &QPushButton::clicked, this, &TestQtOnly::onPlay);
        connect(m_stopButton, &QPushButton::clicked, this, &TestQtOnly::onStop);
        connect(m_refreshButton, &QPushButton::clicked, this, &TestQtOnly::populateDevices);
        connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
                this, &TestQtOnly::onDeviceChanged);

        // Initial populate
        populateDevices();
    }

    ~TestQtOnly() { onStop(); }

private slots:
    void populateDevices()
    {
        qDebug() << "\n========== DEVICE ENUMERATION ==========";
        
        // Get counts
        int v4l2Count = countV4L2CaptureDevices();
        m_allQtDevices = QMediaDevices::videoInputs();
        int qtTotalCount = m_allQtDevices.size();
        
        qDebug() << "V4L2 capture devices:" << v4l2Count;
        qDebug() << "Qt total devices:" << qtTotalCount;
        
        // Determine capture count
        int captureCount;
        QString countStatus;
        
        if (v4l2Count == 0) {
            // Non-Linux or no V4L2
            captureCount = qtTotalCount / 2;
            countStatus = QString("Qt-only mode: using first %1 of %2 devices")
                .arg(captureCount).arg(qtTotalCount);
        } else if (qtTotalCount / 2 == v4l2Count) {
            // Match! Qt has captures + metadata, V4L2 has captures only
            captureCount = v4l2Count;
            countStatus = QString("✓ MATCH: V4L2=%1 captures, Qt=%2 total (first half = captures)")
                .arg(v4l2Count).arg(qtTotalCount);
        } else if (v4l2Count == qtTotalCount / 2 + 1) {
            // Qt missed one device
            captureCount = v4l2Count;
            countStatus = QString("⚠ Qt missed 1 device: V4L2=%1, Qt=%2 (using V4L2 count)")
                .arg(v4l2Count).arg(qtTotalCount / 2);
        } else if (v4l2Count > qtTotalCount / 2 + 1) {
            // Qt still loading
            captureCount = qtTotalCount / 2;
            countStatus = QString("⏳ Qt still loading: V4L2=%1, Qt=%2 (using Qt count)")
                .arg(v4l2Count).arg(qtTotalCount / 2);
        } else {
            // V4L2 less than Qt - unusual, use Qt
            captureCount = qtTotalCount / 2;
            countStatus = QString("? Unusual: V4L2=%1 < Qt=%2 (using Qt count)")
                .arg(v4l2Count).arg(qtTotalCount / 2);
        }
        
        m_captureCount = captureCount;
        m_countLabel->setText(countStatus);
        
        qDebug() << "Using capture count:" << captureCount;
        qDebug() << "";
        
        // Build capture-only list
        m_captureDevices.clear();
        m_deviceCombo->clear();
        
        // Track names for numbering duplicates
        QMap<QString, int> nameCount;
        
        qDebug() << "=== CAPTURE DEVICES (first" << captureCount << ") ===";
        for (int i = 0; i < qMin(captureCount, m_allQtDevices.size()); ++i) {
            const QCameraDevice& dev = m_allQtDevices[i];
            m_captureDevices.append(dev);
            
            // Build display name with duplicate numbering
            QString baseName = dev.description();
            // Remove " (V4L2)" suffix for cleaner display
            if (baseName.endsWith(" (V4L2)")) {
                baseName = baseName.left(baseName.length() - 7);
            }
            
            nameCount[baseName]++;
            QString displayName;
            if (nameCount[baseName] > 1) {
                displayName = QString("%1 #%2").arg(baseName).arg(nameCount[baseName]);
            } else {
                displayName = baseName;
            }
            
            // Check if there will be more with same name
            int totalSameName = 0;
            for (int j = 0; j < qMin(captureCount, m_allQtDevices.size()); ++j) {
                QString otherName = m_allQtDevices[j].description();
                if (otherName.endsWith(" (V4L2)")) {
                    otherName = otherName.left(otherName.length() - 7);
                }
                if (otherName == baseName) totalSameName++;
            }
            if (totalSameName > 1 && nameCount[baseName] == 1) {
                displayName = QString("%1 #1").arg(baseName);
            }
            
            m_deviceCombo->addItem(QString("[%1] %2").arg(i).arg(displayName));
            
            qDebug() << "  [" << i << "]" << displayName 
                     << "| Qt id:" << QString::fromUtf8(dev.id());
        }
        
        qDebug() << "";
        qDebug() << "=== METADATA DEVICES (remaining) ===";
        for (int i = captureCount; i < m_allQtDevices.size(); ++i) {
            const QCameraDevice& dev = m_allQtDevices[i];
            qDebug() << "  [" << i << "]" << dev.description() 
                     << "| Qt id:" << QString::fromUtf8(dev.id()) << "(SKIPPED)";
        }
        
        if (!m_captureDevices.isEmpty()) {
            onDeviceChanged(0);
        }
        
        qDebug() << "========== ENUMERATION DONE ==========\n";
    }

    void onDeviceChanged(int index)
    {
        if (index < 0 || index >= m_captureDevices.size()) return;
        
        const QCameraDevice& dev = m_captureDevices[index];
        QString info = QString(
            "Index: %1 | Qt ID: \"%2\" | Description: %3"
        ).arg(index)
         .arg(QString::fromUtf8(dev.id()))
         .arg(dev.description());
        
        m_infoLabel->setText(info);
    }

    void onPlay()
    {
        int index = m_deviceCombo->currentIndex();
        if (index < 0 || index >= m_captureDevices.size()) return;

        qDebug() << "\n========== PLAY ==========";
        qDebug() << "Selected capture index:" << index;
        
        const QCameraDevice& device = m_captureDevices[index];
        qDebug() << "Qt device:" << device.description() << "id:" << QString::fromUtf8(device.id());

        onStop();

        m_camera = new QCamera(device, this);
        
        connect(m_camera, &QCamera::activeChanged, this, [this](bool active) {
            qDebug() << "Camera active:" << active;
            if (active) {
                m_statusLabel->setText("Status: Playing ✓");
                m_statusLabel->setStyleSheet("font-size: 12px; color: green; padding: 5px;");
            }
        });
        
        connect(m_camera, &QCamera::errorOccurred, this, 
            [this](QCamera::Error error, const QString& errorString) {
            qDebug() << "Camera error:" << error << errorString;
            m_statusLabel->setText("Status: Error - " + errorString);
            m_statusLabel->setStyleSheet("font-size: 12px; color: red; padding: 5px;");
        });

        m_session->setCamera(m_camera);
        m_session->setVideoOutput(m_videoItem);

        // Set 720p if available
        for (const auto& fmt : device.videoFormats()) {
            if (fmt.resolution().height() == 720) {
                m_camera->setCameraFormat(fmt);
                break;
            }
        }

        m_statusLabel->setText("Status: Starting...");
        m_camera->start();
        
        qDebug() << "========== PLAY DONE ==========\n";
    }

    void onStop()
    {
        if (m_camera) {
            m_camera->stop();
            m_session->setCamera(nullptr);
            delete m_camera;
            m_camera = nullptr;
        }
        m_statusLabel->setText("Status: Stopped");
        m_statusLabel->setStyleSheet("font-size: 12px; color: gray; padding: 5px;");
    }

private:
    QLabel* m_countLabel;
    QLabel* m_infoLabel;
    QLabel* m_statusLabel;
    QComboBox* m_deviceCombo;
    QPushButton* m_playButton;
    QPushButton* m_stopButton;
    QPushButton* m_refreshButton;
    
    QGraphicsView* m_graphicsView;
    QGraphicsScene* m_graphicsScene;
    QGraphicsVideoItem* m_videoItem;
    
    QList<QCameraDevice> m_allQtDevices;
    QList<QCameraDevice> m_captureDevices;
    int m_captureCount = 0;
    
    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_session = nullptr;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    qDebug() << "======================================";
    qDebug() << "Qt-Only Device List Test";
    qDebug() << "First half of Qt devices = captures";
    qDebug() << "V4L2 count for verification";
    qDebug() << "======================================";

    TestQtOnly window;
    window.show();

    return app.exec();
}

#include "test_qt_only.moc"

