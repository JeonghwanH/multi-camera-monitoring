/**
 * Test hardware encoding capabilities on Linux
 * Tests: VAAPI (AMD/Intel), NVENC (NVIDIA), Software fallback
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
        
        // Buttons
        auto* buttonLayout = new QHBoxLayout();
        m_startCameraBtn = new QPushButton("Start Camera");
        m_recordBtn = new QPushButton("Record 5s Test");
        m_recordBtn->setEnabled(false);
        m_testAllBtn = new QPushButton("Test All Codecs");
        buttonLayout->addWidget(m_startCameraBtn);
        buttonLayout->addWidget(m_recordBtn);
        buttonLayout->addWidget(m_testAllBtn);
        layout->addLayout(buttonLayout);
        
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

