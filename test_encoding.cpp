/**
 * Test hardware encoding capabilities on Linux
 * Tests: VAAPI (AMD/Intel), NVENC (NVIDIA), Software fallback
 * Also tests direct FFmpeg encoding via QProcess
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
#include <QProcess>
#include <QGroupBox>

class EncodingTestWindow : public QMainWindow {
    Q_OBJECT
    
public:
    EncodingTestWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("Hardware Encoding Test");
        resize(800, 600);
        
        auto* central = new QWidget(this);
        setCentralWidget(central);
        auto* layout = new QVBoxLayout(central);
        
        // Camera selector
        auto* cameraLayout = new QHBoxLayout();
        cameraLayout->addWidget(new QLabel("Camera:"));
        m_cameraSelector = new QComboBox();
        cameraLayout->addWidget(m_cameraSelector, 1);
        layout->addLayout(cameraLayout);
        
        // Codec selector
        auto* codecLayout = new QHBoxLayout();
        codecLayout->addWidget(new QLabel("Codec:"));
        m_codecSelector = new QComboBox();
        m_codecSelector->addItem("H.264 (Hardware)", QVariant::fromValue(QMediaFormat::VideoCodec::H264));
        m_codecSelector->addItem("H.265/HEVC (Hardware)", QVariant::fromValue(QMediaFormat::VideoCodec::H265));
        m_codecSelector->addItem("VP8 (Software)", QVariant::fromValue(QMediaFormat::VideoCodec::VP8));
        m_codecSelector->addItem("VP9 (Hardware/Software)", QVariant::fromValue(QMediaFormat::VideoCodec::VP9));
        m_codecSelector->addItem("MPEG4 (Software)", QVariant::fromValue(QMediaFormat::VideoCodec::MPEG4));
        m_codecSelector->addItem("Unspecified (Auto)", QVariant::fromValue(QMediaFormat::VideoCodec::Unspecified));
        codecLayout->addWidget(m_codecSelector, 1);
        layout->addLayout(codecLayout);
        
        // Container selector
        auto* containerLayout = new QHBoxLayout();
        containerLayout->addWidget(new QLabel("Container:"));
        m_containerSelector = new QComboBox();
        m_containerSelector->addItem("MP4", QVariant::fromValue(QMediaFormat::MPEG4));
        m_containerSelector->addItem("MKV (Matroska)", QVariant::fromValue(QMediaFormat::Matroska));
        m_containerSelector->addItem("WebM", QVariant::fromValue(QMediaFormat::WebM));
        m_containerSelector->addItem("AVI", QVariant::fromValue(QMediaFormat::AVI));
        containerLayout->addWidget(m_containerSelector, 1);
        layout->addLayout(containerLayout);
        
        // Video preview
        m_videoWidget = new QVideoWidget();
        m_videoWidget->setMinimumHeight(300);
        layout->addWidget(m_videoWidget);
        
        // Qt Recording Buttons
        auto* qtButtonLayout = new QHBoxLayout();
        m_startCameraBtn = new QPushButton("Start Camera");
        m_recordBtn = new QPushButton("Record 5s Test (Qt)");
        m_recordBtn->setEnabled(false);
        m_testAllBtn = new QPushButton("Test All Qt Codecs");
        qtButtonLayout->addWidget(m_startCameraBtn);
        qtButtonLayout->addWidget(m_recordBtn);
        qtButtonLayout->addWidget(m_testAllBtn);
        layout->addLayout(qtButtonLayout);
        
        // FFmpeg Direct Encoding Buttons (GPU Hardware)
        auto* ffmpegGroup = new QGroupBox("Direct FFmpeg GPU Encoding (bypasses Qt)");
        auto* ffmpegLayout = new QHBoxLayout(ffmpegGroup);
        m_testNvencBtn = new QPushButton("Test NVENC (NVIDIA)");
        m_testVaapiBtn = new QPushButton("Test VA-API (Intel/AMD)");
        m_testQsvBtn = new QPushButton("Test QSV (Intel)");
        m_testSoftwareBtn = new QPushButton("Test libx264 (CPU)");
        ffmpegLayout->addWidget(m_testNvencBtn);
        ffmpegLayout->addWidget(m_testVaapiBtn);
        ffmpegLayout->addWidget(m_testQsvBtn);
        ffmpegLayout->addWidget(m_testSoftwareBtn);
        layout->addWidget(ffmpegGroup);
        
        // Log output
        m_log = new QTextEdit();
        m_log->setReadOnly(true);
        m_log->setMaximumHeight(200);
        layout->addWidget(m_log);
        
        // Populate cameras
        populateCameras();
        
        // Connections
        connect(m_startCameraBtn, &QPushButton::clicked, this, &EncodingTestWindow::startCamera);
        connect(m_recordBtn, &QPushButton::clicked, this, &EncodingTestWindow::recordTest);
        connect(m_testAllBtn, &QPushButton::clicked, this, &EncodingTestWindow::testAllCodecs);
        
        // FFmpeg direct encoding connections
        connect(m_testNvencBtn, &QPushButton::clicked, this, [this]() { testFFmpegEncoder("h264_nvenc", "NVENC"); });
        connect(m_testVaapiBtn, &QPushButton::clicked, this, [this]() { testFFmpegEncoder("h264_vaapi", "VA-API"); });
        connect(m_testQsvBtn, &QPushButton::clicked, this, [this]() { testFFmpegEncoder("h264_qsv", "Quick Sync"); });
        connect(m_testSoftwareBtn, &QPushButton::clicked, this, [this]() { testFFmpegEncoder("libx264", "Software/CPU"); });
        
        // Create output directory
        QDir().mkpath("encoding_test");
        
        log("=== Hardware Encoding Test ===");
        log("QT_MEDIA_BACKEND: " + QString(qgetenv("QT_MEDIA_BACKEND")));
        log("Qt Version: " + QString(qVersion()));
        log("");
        log("Select a camera and click 'Start Camera'");
        log("Then click 'Record 5s Test' to test selected codec");
        log("Or click 'Test All Codecs' to try all options");
    }
    
    ~EncodingTestWindow() {
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
        
        // Stop existing
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
                log("Camera started: " + m_camera->cameraDevice().description());
                m_recordBtn->setEnabled(true);
            }
        });
        
        connect(m_camera, &QCamera::errorOccurred, this, [this](QCamera::Error err, const QString& msg) {
            log("Camera ERROR: " + msg);
        });
        
        m_camera->start();
    }
    
    void recordTest() {
        if (!m_session || !m_camera || !m_camera->isActive()) {
            log("ERROR: Camera not active");
            return;
        }
        
        auto codec = m_codecSelector->currentData().value<QMediaFormat::VideoCodec>();
        auto container = m_containerSelector->currentData().value<QMediaFormat::FileFormat>();
        
        testCodec(codec, container, m_codecSelector->currentText());
    }
    
    void testCodec(QMediaFormat::VideoCodec codec, QMediaFormat::FileFormat container, const QString& name) {
        if (!m_session) return;
        
        // Stop any existing recorder
        if (m_recorder) {
            if (m_recorder->recorderState() == QMediaRecorder::RecordingState) {
                m_recorder->stop();
            }
            delete m_recorder;
            m_recorder = nullptr;
        }
        
        m_recorder = new QMediaRecorder(this);
        m_session->setRecorder(m_recorder);
        
        // Configure format
        QMediaFormat format;
        format.setFileFormat(container);
        format.setVideoCodec(codec);
        format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);  // No audio
        
        m_recorder->setMediaFormat(format);
        m_recorder->setQuality(QMediaRecorder::NormalQuality);
        m_recorder->setVideoFrameRate(0);  // Source rate
        
        // Generate filename
        QString ext;
        switch (container) {
            case QMediaFormat::MPEG4: ext = "mp4"; break;
            case QMediaFormat::Matroska: ext = "mkv"; break;
            case QMediaFormat::WebM: ext = "webm"; break;
            case QMediaFormat::AVI: ext = "avi"; break;
            default: ext = "mp4"; break;
        }
        
        QString safeName = name;
        safeName.replace(" ", "_");
        safeName.replace("/", "-");
        safeName.replace("(", "");
        safeName.replace(")", "");
        
        QString filename = QString("encoding_test/%1_%2.%3")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"))
            .arg(safeName)
            .arg(ext);
        
        m_recorder->setOutputLocation(QUrl::fromLocalFile(QDir::currentPath() + "/" + filename));
        
        connect(m_recorder, &QMediaRecorder::recorderStateChanged, this, [this, name](QMediaRecorder::RecorderState state) {
            if (state == QMediaRecorder::RecordingState) {
                log("✓ Recording STARTED: " + name);
            } else if (state == QMediaRecorder::StoppedState) {
                log("✓ Recording STOPPED: " + name);
            }
        });
        
        connect(m_recorder, &QMediaRecorder::errorOccurred, this, [this, name](QMediaRecorder::Error err, const QString& msg) {
            log("✗ FAILED [" + name + "]: " + msg);
            m_testResults[name] = false;
        });
        
        connect(m_recorder, &QMediaRecorder::actualLocationChanged, this, [this](const QUrl& url) {
            log("  Output: " + url.toLocalFile());
        });
        
        log("Testing: " + name + "...");
        m_recorder->record();
        
        // Stop after 5 seconds
        QTimer::singleShot(5000, this, [this, name]() {
            if (m_recorder && m_recorder->recorderState() == QMediaRecorder::RecordingState) {
                m_recorder->stop();
                m_testResults[name] = true;
                log("✓ SUCCESS: " + name);
                
                // Check file size
                QTimer::singleShot(500, this, [this]() {
                    if (m_recorder) {
                        QFileInfo info(m_recorder->actualLocation().toLocalFile());
                        if (info.exists()) {
                            log("  File size: " + QString::number(info.size() / 1024) + " KB");
                        }
                    }
                });
            }
        });
    }
    
    void testAllCodecs() {
        if (!m_session || !m_camera || !m_camera->isActive()) {
            log("ERROR: Start camera first!");
            return;
        }
        
        m_testResults.clear();
        m_testQueue.clear();
        
        // Queue all codec/container combinations
        m_testQueue.append({QMediaFormat::VideoCodec::H264, QMediaFormat::MPEG4, "H264_MP4"});
        m_testQueue.append({QMediaFormat::VideoCodec::H265, QMediaFormat::MPEG4, "H265_MP4"});
        m_testQueue.append({QMediaFormat::VideoCodec::H264, QMediaFormat::Matroska, "H264_MKV"});
        m_testQueue.append({QMediaFormat::VideoCodec::VP9, QMediaFormat::WebM, "VP9_WebM"});
        m_testQueue.append({QMediaFormat::VideoCodec::MPEG4, QMediaFormat::MPEG4, "MPEG4_MP4"});
        m_testQueue.append({QMediaFormat::VideoCodec::MPEG4, QMediaFormat::AVI, "MPEG4_AVI"});
        
        log("");
        log("=== Testing All Codecs (6 combinations) ===");
        log("Each test records for 5 seconds...");
        log("");
        
        runNextTest();
    }
    
    void runNextTest() {
        if (m_testQueue.isEmpty()) {
            // Print summary
            log("");
            log("=== TEST SUMMARY ===");
            for (auto it = m_testResults.begin(); it != m_testResults.end(); ++it) {
                log(QString("%1: %2").arg(it.key(), -20).arg(it.value() ? "✓ WORKS" : "✗ FAILED"));
            }
            return;
        }
        
        auto test = m_testQueue.takeFirst();
        testCodec(std::get<0>(test), std::get<1>(test), std::get<2>(test));
        
        // Schedule next test after 7 seconds (5s recording + 2s buffer)
        QTimer::singleShot(7000, this, &EncodingTestWindow::runNextTest);
    }
    
    void testFFmpegEncoder(const QString& encoder, const QString& name) {
        // Get selected camera's device path
        int idx = m_cameraSelector->currentData().toInt();
        auto cameras = QMediaDevices::videoInputs();
        
        if (idx < 0 || idx >= cameras.size()) {
            log("ERROR: Select a camera first");
            return;
        }
        
        QString deviceId = cameras[idx].id();
        QString devicePath;
        
        // Try to get device path
        if (deviceId.startsWith("/dev/")) {
            devicePath = deviceId;
        } else {
            // Assume it's /dev/videoN where N is the index or ID
            bool ok;
            int num = deviceId.toInt(&ok);
            if (ok) {
                devicePath = QString("/dev/video%1").arg(num);
            } else {
                // Default to video0
                devicePath = "/dev/video0";
            }
        }
        
        log("");
        log(QString("=== Testing FFmpeg %1 Encoder ===").arg(name));
        log("Device: " + devicePath);
        log("Encoder: " + encoder);
        
        QString outputFile = QString("encoding_test/ffmpeg_%1_%2.mp4")
            .arg(encoder)
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        
        QStringList args;
        args << "-y";  // Overwrite
        args << "-f" << "v4l2";
        args << "-i" << devicePath;
        args << "-t" << "3";  // 3 seconds
        
        // Add encoder-specific options
        if (encoder == "h264_vaapi") {
            args << "-vaapi_device" << "/dev/dri/renderD128";
            args << "-vf" << "format=nv12,hwupload";
        }
        
        args << "-c:v" << encoder;
        
        if (encoder == "libx264") {
            args << "-preset" << "ultrafast";  // Fast for testing
        }
        
        args << "-an";  // No audio
        args << outputFile;
        
        log("Command: ffmpeg " + args.join(" "));
        
        QProcess* process = new QProcess(this);
        
        connect(process, &QProcess::readyReadStandardError, this, [this, process]() {
            QString output = process->readAllStandardError();
            // Only log important lines
            for (const QString& line : output.split('\n')) {
                if (line.contains("error", Qt::CaseInsensitive) ||
                    line.contains("encoder", Qt::CaseInsensitive) ||
                    line.contains("Output #0")) {
                    log("  " + line.trimmed());
                }
            }
        });
        
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, process, name, outputFile](int exitCode, QProcess::ExitStatus status) {
            if (exitCode == 0) {
                QFileInfo info(outputFile);
                if (info.exists() && info.size() > 1000) {
                    log(QString("✓ SUCCESS: %1 - File: %2 (%3 KB)")
                        .arg(name)
                        .arg(outputFile)
                        .arg(info.size() / 1024));
                } else {
                    log(QString("✗ FAILED: %1 - Output file empty or missing").arg(name));
                }
            } else {
                log(QString("✗ FAILED: %1 - Exit code: %2").arg(name).arg(exitCode));
            }
            process->deleteLater();
        });
        
        process->start("ffmpeg", args);
        
        if (!process->waitForStarted(5000)) {
            log("ERROR: Failed to start ffmpeg process");
            process->deleteLater();
        }
    }
    
    void log(const QString& msg) {
        m_log->append(msg);
        qDebug() << msg;
    }
    
private:
    QComboBox* m_cameraSelector = nullptr;
    QComboBox* m_codecSelector = nullptr;
    QComboBox* m_containerSelector = nullptr;
    QVideoWidget* m_videoWidget = nullptr;
    QPushButton* m_startCameraBtn = nullptr;
    QPushButton* m_recordBtn = nullptr;
    QPushButton* m_testAllBtn = nullptr;
    QPushButton* m_testNvencBtn = nullptr;
    QPushButton* m_testVaapiBtn = nullptr;
    QPushButton* m_testQsvBtn = nullptr;
    QPushButton* m_testSoftwareBtn = nullptr;
    QTextEdit* m_log = nullptr;
    
    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_session = nullptr;
    QMediaRecorder* m_recorder = nullptr;
    
    QMap<QString, bool> m_testResults;
    QList<std::tuple<QMediaFormat::VideoCodec, QMediaFormat::FileFormat, QString>> m_testQueue;
};

int main(int argc, char *argv[]) {
    // Set FFmpeg backend if not set
#ifdef Q_OS_LINUX
    if (qgetenv("QT_MEDIA_BACKEND").isEmpty()) {
        qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    }
#endif
    
    QApplication app(argc, argv);
    
    EncodingTestWindow window;
    window.show();
    
    return app.exec();
}

#include "test_encoding.moc"

