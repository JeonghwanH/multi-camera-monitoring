/**
 * Test QT_FFMPEG_ENCODING_HW_DEVICE_TYPES environment variable
 * Tests if Qt 6.4.x respects the encoding hardware device type setting
 */

#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QTextEdit>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QMediaFormat>
#include <QVideoWidget>
#include <QTimer>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QGroupBox>
#include <QLibraryInfo>
#include <cstdlib>

class EnvTestWindow : public QMainWindow {
    Q_OBJECT
    
public:
    EnvTestWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("Qt FFmpeg Encoding Env Test");
        resize(900, 700);
        
        auto* central = new QWidget(this);
        setCentralWidget(central);
        auto* layout = new QVBoxLayout(central);
        
        // Info section
        auto* infoGroup = new QGroupBox("Environment Info");
        auto* infoLayout = new QVBoxLayout(infoGroup);
        
        QString info;
        info += QString("Qt Version: %1\n").arg(QT_VERSION_STR);
        info += QString("Qt Build: %1\n").arg(QLibraryInfo::build());
        info += QString("QT_MEDIA_BACKEND: %1\n").arg(qgetenv("QT_MEDIA_BACKEND").constData());
        info += QString("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES: %1\n").arg(qgetenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES").constData());
        info += QString("LIBVA_DRIVER_NAME: %1\n").arg(qgetenv("LIBVA_DRIVER_NAME").constData());
        
        auto* infoLabel = new QLabel(info);
        infoLabel->setFont(QFont("monospace"));
        infoLayout->addWidget(infoLabel);
        layout->addWidget(infoGroup);
        
        // Camera selector
        auto* cameraLayout = new QHBoxLayout();
        cameraLayout->addWidget(new QLabel("Camera:"));
        m_cameraSelector = new QComboBox();
        cameraLayout->addWidget(m_cameraSelector, 1);
        layout->addLayout(cameraLayout);
        
        // Video preview
        m_videoWidget = new QVideoWidget();
        m_videoWidget->setMinimumHeight(250);
        layout->addWidget(m_videoWidget);
        
        // Test buttons
        auto* testGroup = new QGroupBox("Encoding Tests (watch console for encoder used)");
        auto* testLayout = new QVBoxLayout(testGroup);
        
        auto* row1 = new QHBoxLayout();
        m_startCameraBtn = new QPushButton("1. Start Camera");
        m_testDefaultBtn = new QPushButton("2. Test H.264 (Default)");
        m_testDefaultBtn->setEnabled(false);
        row1->addWidget(m_startCameraBtn);
        row1->addWidget(m_testDefaultBtn);
        testLayout->addLayout(row1);
        
        auto* row2 = new QHBoxLayout();
        m_testCudaOnlyBtn = new QPushButton("3. Set cuda → Test H.264");
        m_testNoVaapiBtn = new QPushButton("4. Set dummy VAAPI → Test H.264");
        m_testCudaOnlyBtn->setEnabled(false);
        m_testNoVaapiBtn->setEnabled(false);
        row2->addWidget(m_testCudaOnlyBtn);
        row2->addWidget(m_testNoVaapiBtn);
        testLayout->addLayout(row2);
        
        layout->addWidget(testGroup);
        
        // Log
        m_log = new QTextEdit();
        m_log->setReadOnly(true);
        m_log->setFont(QFont("monospace", 9));
        layout->addWidget(m_log);
        
        // Populate cameras
        populateCameras();
        
        // Connections
        connect(m_startCameraBtn, &QPushButton::clicked, this, &EnvTestWindow::startCamera);
        connect(m_testDefaultBtn, &QPushButton::clicked, this, [this]() { testEncoding("Default (no env change)"); });
        connect(m_testCudaOnlyBtn, &QPushButton::clicked, this, &EnvTestWindow::testCudaOnly);
        connect(m_testNoVaapiBtn, &QPushButton::clicked, this, &EnvTestWindow::testNoVaapi);
        
        // Create output directory
        QDir().mkpath("encoding_env_test");
        
        log("=== Qt FFmpeg Encoding Environment Test ===");
        log("");
        log("This tests if QT_FFMPEG_ENCODING_HW_DEVICE_TYPES works in your Qt version.");
        log("");
        log("Instructions:");
        log("1. Click 'Start Camera'");
        log("2. Click 'Test H.264 (Default)' - watch console for encoder");
        log("3. Click 'Set cuda → Test' - should use NVENC if Qt supports it");
        log("4. Click 'Set dummy VAAPI → Test' - fallback method");
        log("");
        log("Look for these in console output:");
        log("  [h264_vaapi] = Intel VA-API (we want to skip this)");
        log("  [h264_nvenc] = NVIDIA NVENC (ideal)");
        log("  [libx264]    = CPU software (acceptable fallback)");
    }
    
    ~EnvTestWindow() {
        if (m_camera) m_camera->stop();
    }
    
private slots:
    void populateCameras() {
        m_cameraSelector->clear();
        auto cameras = QMediaDevices::videoInputs();
        for (int i = 0; i < cameras.size(); ++i) {
            m_cameraSelector->addItem(
                QString("[%1] %2").arg(i).arg(cameras[i].description()),
                i
            );
        }
    }
    
    void startCamera() {
        int idx = m_cameraSelector->currentData().toInt();
        auto cameras = QMediaDevices::videoInputs();
        
        if (idx < 0 || idx >= cameras.size()) {
            log("ERROR: Invalid camera index");
            return;
        }
        
        if (m_camera) {
            m_camera->stop();
            delete m_camera;
            m_camera = nullptr;
        }
        if (m_session) {
            delete m_session;
            m_session = nullptr;
        }
        
        m_session = new QMediaCaptureSession(this);
        m_camera = new QCamera(cameras[idx], this);
        
        m_session->setCamera(m_camera);
        m_session->setVideoOutput(m_videoWidget);
        
        connect(m_camera, &QCamera::activeChanged, this, [this](bool active) {
            if (active) {
                log("✓ Camera started: " + m_camera->cameraDevice().description());
                m_testDefaultBtn->setEnabled(true);
                m_testCudaOnlyBtn->setEnabled(true);
                m_testNoVaapiBtn->setEnabled(true);
            }
        });
        
        m_camera->start();
    }
    
    void testEncoding(const QString& testName) {
        if (!m_session || !m_camera || !m_camera->isActive()) {
            log("ERROR: Camera not active");
            return;
        }
        
        log("");
        log("=== " + testName + " ===");
        log("Current env:");
        log("  QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=" + QString(qgetenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES")));
        log("  LIBVA_DRIVER_NAME=" + QString(qgetenv("LIBVA_DRIVER_NAME")));
        
        // Stop existing recorder
        if (m_recorder) {
            if (m_recorder->recorderState() == QMediaRecorder::RecordingState) {
                m_recorder->stop();
            }
            delete m_recorder;
            m_recorder = nullptr;
        }
        
        m_recorder = new QMediaRecorder(this);
        m_session->setRecorder(m_recorder);
        
        // Configure for H.264
        QMediaFormat format;
        format.setFileFormat(QMediaFormat::MPEG4);
        format.setVideoCodec(QMediaFormat::VideoCodec::H264);
        format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
        
        m_recorder->setMediaFormat(format);
        m_recorder->setQuality(QMediaRecorder::NormalQuality);
        
        QString safeName = testName;
        safeName.replace(" ", "_").replace("(", "").replace(")", "").replace("→", "-");
        
        QString filename = QString("encoding_env_test/%1_%2.mp4")
            .arg(safeName)
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        
        m_recorder->setOutputLocation(QUrl::fromLocalFile(QDir::currentPath() + "/" + filename));
        
        connect(m_recorder, &QMediaRecorder::recorderStateChanged, this, [this, testName](QMediaRecorder::RecorderState state) {
            if (state == QMediaRecorder::RecordingState) {
                log("✓ Recording STARTED - check console for [encoder] used!");
            }
        });
        
        connect(m_recorder, &QMediaRecorder::errorOccurred, this, [this, testName](QMediaRecorder::Error err, const QString& msg) {
            log("✗ ERROR: " + msg);
        });
        
        log("Starting H.264 recording...");
        log(">>> WATCH CONSOLE OUTPUT for encoder selection! <<<");
        m_recorder->record();
        
        // Stop after 3 seconds
        QTimer::singleShot(3000, this, [this, filename, testName]() {
            if (m_recorder && m_recorder->recorderState() == QMediaRecorder::RecordingState) {
                m_recorder->stop();
                
                QTimer::singleShot(500, this, [this, filename, testName]() {
                    QFileInfo info(filename);
                    if (info.exists() && info.size() > 1000) {
                        log(QString("✓ SUCCESS: %1 KB").arg(info.size() / 1024));
                    } else {
                        log("✗ FAILED: File empty or missing");
                    }
                });
            }
        });
    }
    
    void testCudaOnly() {
        log("");
        log(">>> Setting QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=cuda");
        qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", "cuda");
        
        testEncoding("CUDA only (QT_FFMPEG env)");
    }
    
    void testNoVaapi() {
        log("");
        log(">>> Setting LIBVA_DRIVER_NAME=dummy (disables VA-API)");
        qputenv("LIBVA_DRIVER_NAME", "dummy");
        
        testEncoding("VAAPI disabled (LIBVA env)");
    }
    
    void log(const QString& msg) {
        m_log->append(msg);
        qDebug().noquote() << msg;
    }
    
private:
    QComboBox* m_cameraSelector = nullptr;
    QVideoWidget* m_videoWidget = nullptr;
    QPushButton* m_startCameraBtn = nullptr;
    QPushButton* m_testDefaultBtn = nullptr;
    QPushButton* m_testCudaOnlyBtn = nullptr;
    QPushButton* m_testNoVaapiBtn = nullptr;
    QTextEdit* m_log = nullptr;
    
    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_session = nullptr;
    QMediaRecorder* m_recorder = nullptr;
};

int main(int argc, char *argv[]) {
    // Set FFmpeg backend
#ifdef Q_OS_LINUX
    if (qgetenv("QT_MEDIA_BACKEND").isEmpty()) {
        qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    }
#endif
    
    // Enable Qt FFmpeg debug logging
    qputenv("QT_FFMPEG_DEBUG", "1");
    
    QApplication app(argc, argv);
    
    qDebug() << "==============================================";
    qDebug() << "Qt FFmpeg Encoding Environment Variable Test";
    qDebug() << "==============================================";
    qDebug() << "Qt Version:" << QT_VERSION_STR;
    qDebug() << "QT_MEDIA_BACKEND:" << qgetenv("QT_MEDIA_BACKEND");
    qDebug() << "";
    
    EnvTestWindow window;
    window.show();
    
    return app.exec();
}

#include "test_qt_encoding_env.moc"

