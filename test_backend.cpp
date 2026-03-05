// Test Qt Multimedia backend device enumeration
// Compare FFmpeg vs GStreamer behavior
//
// Build: cd build && cmake .. && make test_backend
// Run:
//   ./test_backend                           # Default backend
//   QT_MEDIA_BACKEND=ffmpeg ./test_backend   # Force FFmpeg
//   QT_MEDIA_BACKEND=gstreamer ./test_backend # Force GStreamer

// Use QApplication like main app (not QCoreApplication)
#include <QApplication>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QDebug>
#include <QLibraryInfo>
#include <QDir>
#include <cstdlib>

// Include GUI components like main app
#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QMediaCaptureSession>
#include <QCamera>

#ifdef Q_OS_LINUX
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <QFile>
#include <cstring>

struct V4L2Info {
    QString path;
    QString name;
    bool isCapture;
    bool isMeta;
};

QList<V4L2Info> enumerateV4L2Devices() {
    QList<V4L2Info> devices;
    
    for (int i = 0; i < 32; ++i) {
        QString path = QString("/dev/video%1").arg(i);
        if (!QFile::exists(path)) continue;
        
        int fd = open(path.toUtf8().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        
        struct v4l2_capability cap;
        memset(&cap, 0, sizeof(cap));
        
        V4L2Info info;
        info.path = path;
        info.isCapture = false;
        info.isMeta = false;
        
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            info.name = QString::fromUtf8((const char*)cap.card);
            __u32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
            info.isCapture = (caps & V4L2_CAP_VIDEO_CAPTURE) || (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE);
            info.isMeta = (caps & V4L2_CAP_META_CAPTURE) != 0;
        }
        
        close(fd);
        devices.append(info);
    }
    
    return devices;
}
#endif

void runDeviceTest() {
    qDebug() << "=============================================";
    qDebug() << "  Qt Multimedia Backend Device Test";
    qDebug() << "  (Using QApplication like main app)";
    qDebug() << "=============================================";
    qDebug() << "";
    
    // Backend info
    const char* backendEnv = std::getenv("QT_MEDIA_BACKEND");
    qDebug() << "QT_MEDIA_BACKEND env:" << (backendEnv ? backendEnv : "(not set)");
    qDebug() << "Qt Version:" << QT_VERSION_STR;
    qDebug() << "Qt Build:" << QLibraryInfo::build();
    qDebug() << "";
    
#ifdef Q_OS_LINUX
    // V4L2 enumeration
    qDebug() << "=== V4L2 Direct Enumeration ===";
    QList<V4L2Info> v4l2Devices = enumerateV4L2Devices();
    int v4l2CaptureCount = 0;
    int v4l2MetaCount = 0;
    
    for (const auto& dev : v4l2Devices) {
        QString type;
        if (dev.isCapture && !dev.isMeta) {
            type = "CAPTURE";
            v4l2CaptureCount++;
        } else if (dev.isMeta) {
            type = "META";
            v4l2MetaCount++;
        } else {
            type = "OTHER";
        }
        qDebug() << "  " << dev.path << "-" << dev.name << "-" << type;
    }
    qDebug() << "";
    qDebug() << "V4L2 Summary: " << v4l2Devices.size() << "total," 
             << v4l2CaptureCount << "capture," << v4l2MetaCount << "meta";
    qDebug() << "";
#endif
    
    // Qt enumeration
    qDebug() << "=== Qt QMediaDevices::videoInputs() ===";
    QList<QCameraDevice> qtDevices = QMediaDevices::videoInputs();
    
    for (int i = 0; i < qtDevices.size(); ++i) {
        const QCameraDevice& dev = qtDevices[i];
        qDebug() << "  [" << i << "] id:" << QString::fromUtf8(dev.id())
                 << "- name:" << dev.description();
    }
    qDebug() << "";
    qDebug() << "Qt Summary:" << qtDevices.size() << "devices reported";
    qDebug() << "";
    
#ifdef Q_OS_LINUX
    // Analysis
    qDebug() << "=== Analysis ===";
    qDebug() << "V4L2 capture devices:" << v4l2CaptureCount;
    qDebug() << "Qt reported devices: " << qtDevices.size();
    
    if (qtDevices.size() == v4l2CaptureCount) {
        qDebug() << "";
        qDebug() << "✓ Qt count MATCHES V4L2 capture count";
        qDebug() << "  -> Backend reports ONLY capture devices (FFmpeg-style)";
        qDebug() << "  -> Use ALL Qt devices as captures";
    } else if (qtDevices.size() == v4l2CaptureCount * 2) {
        qDebug() << "";
        qDebug() << "✓ Qt count is 2x V4L2 capture count";
        qDebug() << "  -> Backend reports capture + metadata (GStreamer-style)";
        qDebug() << "  -> Use FIRST HALF of Qt devices as captures";
    } else if (qtDevices.size() == v4l2CaptureCount + v4l2MetaCount) {
        qDebug() << "";
        qDebug() << "✓ Qt count matches V4L2 capture + meta";
        qDebug() << "  -> Backend reports all V4L2 devices";
        qDebug() << "  -> Need to filter by capability";
    } else {
        qDebug() << "";
        qDebug() << "? Unexpected count relationship";
        qDebug() << "  -> Manual investigation needed";
    }
#endif
    
    qDebug() << "";
    qDebug() << "=============================================";
    qDebug() << "  Test Complete";
    qDebug() << "=============================================";
}

int main(int argc, char *argv[])
{
    // Same setup as main app
    QApplication::setApplicationName("Backend Test");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("MCM");
    
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    
    QApplication app(argc, argv);
    
    // Create GUI components like main app does
    // This might trigger different plugin loading
    qDebug() << "Creating GUI components (like main app)...";
    
    QMainWindow window;
    QGraphicsView* view = new QGraphicsView(&window);
    QGraphicsScene* scene = new QGraphicsScene(view);
    view->setScene(scene);
    
    // Create video items like main app
    QGraphicsVideoItem* videoItem = new QGraphicsVideoItem();
    scene->addItem(videoItem);
    
    // Create capture session like main app
    QMediaCaptureSession* session = new QMediaCaptureSession(&window);
    
    qDebug() << "GUI components created.";
    qDebug() << "";
    
    // Now run device test
    runDeviceTest();
    
    // Clean up
    delete session;
    
    return 0;
}
