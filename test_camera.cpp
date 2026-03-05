// Minimal QCamera + QMediaCaptureSession test
// Build: cd build && cmake .. && make test_camera
// Run: ./test_camera [device_index]
//   On Linux: device_index maps V4L2 capture devices to Qt devices

#ifdef Q_OS_MACOS
#include <QtPlugin>
Q_IMPORT_PLUGIN(QDarwinCameraPermissionPlugin)
#endif

#include <QApplication>
#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QDebug>
#include <QDir>

#ifdef Q_OS_LINUX
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

struct V4L2CaptureDevice {
    QString path;       // /dev/videoN
    QString card;       // Device name
    QString busInfo;    // USB bus info
    int qtDeviceId;     // Mapped Qt device ID
};

// Enumerate V4L2 capture-only devices and map to Qt
QList<V4L2CaptureDevice> enumerateV4L2CaptureDevices()
{
    QList<V4L2CaptureDevice> devices;
    
    QDir devDir("/dev");
    QStringList videoDevices = devDir.entryList(QStringList() << "video*", QDir::System);
    
    // Sort by device number
    std::sort(videoDevices.begin(), videoDevices.end(), [](const QString& a, const QString& b) {
        return a.mid(5).toInt() < b.mid(5).toInt();
    });
    
    qDebug() << "\n=== V4L2 Device Scan ===";
    
    for (const QString& devName : videoDevices) {
        QString devPath = "/dev/" + devName;
        
        int fd = open(devPath.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            qDebug() << "  " << devPath << "- cannot open";
            continue;
        }
        
        struct v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
            close(fd);
            qDebug() << "  " << devPath << "- cannot query caps";
            continue;
        }
        
        bool isCapture = (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
        bool isMeta = (cap.device_caps & V4L2_CAP_META_CAPTURE) != 0;
        
        qDebug() << "  " << devPath 
                 << "card:" << reinterpret_cast<const char*>(cap.card)
                 << "| capture:" << isCapture << "meta:" << isMeta;
        
        if (isCapture && !isMeta) {
            V4L2CaptureDevice dev;
            dev.path = devPath;
            dev.card = QString::fromLocal8Bit(reinterpret_cast<const char*>(cap.card));
            dev.busInfo = QString::fromLocal8Bit(reinterpret_cast<const char*>(cap.bus_info));
            
            // Map V4L2 to Qt: Qt ID = V4L2 device number / 2
            // V4L2 interleaves capture/meta, Qt separates them
            int v4l2Num = devName.mid(5).toInt();
            dev.qtDeviceId = v4l2Num / 2;
            
            devices.append(dev);
        }
        
        close(fd);
    }
    
    return devices;
}
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // List available Qt cameras
    QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    qDebug() << "\n=== Qt Device List ===";
    qDebug() << "Qt cameras:" << cameras.size();
    for (int i = 0; i < cameras.size(); ++i) {
        qDebug() << "  Qt[" << i << "] id:" << QString::fromUtf8(cameras[i].id()) 
                 << "name:" << cameras[i].description()
                 << "pos:" << static_cast<int>(cameras[i].position());
    }

    if (cameras.isEmpty()) {
        qDebug() << "No cameras found!";
        return 1;
    }

    // Determine which camera to use
    int selectedQtIndex = 0;
    
#ifdef Q_OS_LINUX
    // On Linux, use V4L2 enumeration and mapping
    QList<V4L2CaptureDevice> v4l2Devices = enumerateV4L2CaptureDevices();
    
    qDebug() << "\n=== V4L2 Capture Devices (filtered) ===";
    for (int i = 0; i < v4l2Devices.size(); ++i) {
        const V4L2CaptureDevice& dev = v4l2Devices[i];
        qDebug() << "  [" << i << "]" << dev.path << "->" << dev.card 
                 << "| Qt ID:" << dev.qtDeviceId;
    }
    
    // Parse command line argument for device selection
    int v4l2Index = 0;
    if (argc > 1) {
        v4l2Index = QString(argv[1]).toInt();
    }
    
    if (v4l2Index >= 0 && v4l2Index < v4l2Devices.size()) {
        selectedQtIndex = v4l2Devices[v4l2Index].qtDeviceId;
        qDebug() << "\n>>> Selected V4L2[" << v4l2Index << "]:" << v4l2Devices[v4l2Index].path
                 << "-> Qt ID:" << selectedQtIndex;
    } else {
        qDebug() << "\nUsage: ./test_camera [v4l2_index]";
        qDebug() << "  v4l2_index: 0 to" << (v4l2Devices.size() - 1);
        selectedQtIndex = v4l2Devices.isEmpty() ? 0 : v4l2Devices[0].qtDeviceId;
    }
#else
    // On other platforms, use command line argument directly as Qt index
    if (argc > 1) {
        selectedQtIndex = QString(argv[1]).toInt();
    }
    qDebug() << "\n>>> Selected Qt camera index:" << selectedQtIndex;
#endif

    if (selectedQtIndex < 0 || selectedQtIndex >= cameras.size()) {
        qDebug() << "Invalid camera index:" << selectedQtIndex;
        return 1;
    }

    // Create window
    QMainWindow window;
    window.setWindowTitle("QCamera Test");
    window.resize(800, 600);

    // Create graphics view (like ZoomableVideoView in src_ref)
    QGraphicsView* view = new QGraphicsView(&window);
    QGraphicsScene* scene = new QGraphicsScene(view);
    view->setScene(scene);
    view->setBackgroundBrush(Qt::black);
    view->setFrameStyle(QFrame::NoFrame);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Create video item (like src_ref)
    QGraphicsVideoItem* videoItem = new QGraphicsVideoItem();
    scene->addItem(videoItem);

    window.setCentralWidget(view);

    // Create camera with selected device
    qDebug() << "\nCreating camera with Qt device:" << cameras[selectedQtIndex].description();
    QCamera* camera = new QCamera(cameras[selectedQtIndex]);

    // Create capture session (this replaces FrameBuffer)
    QMediaCaptureSession* captureSession = new QMediaCaptureSession();

    // Connect: Camera -> Session -> VideoItem
    captureSession->setCamera(camera);
    captureSession->setVideoOutput(videoItem);

    // Log camera state changes
    QObject::connect(camera, &QCamera::activeChanged, [](bool active) {
        qDebug() << "Camera active:" << active;
    });

    QObject::connect(camera, &QCamera::errorOccurred, 
        [](QCamera::Error error, const QString &errorString) {
        qDebug() << "Camera error:" << error << errorString;
    });

    // Start camera
    qDebug() << "Starting camera...";
    camera->start();

    window.show();

    // Fit video to view when it gets native size
    QObject::connect(videoItem, &QGraphicsVideoItem::nativeSizeChanged, 
        [view, videoItem](const QSizeF &size) {
        qDebug() << "Video native size:" << size;
        if (size.isValid()) {
            videoItem->setSize(size);
            view->fitInView(videoItem, Qt::KeepAspectRatio);
        }
    });

    return app.exec();
}

