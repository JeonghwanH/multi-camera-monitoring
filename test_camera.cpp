// Minimal QCamera + QMediaCaptureSession test
// Build: cd build && cmake .. && make test_camera
// Run: ./test_camera

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

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // List available cameras
    QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    qDebug() << "Available cameras:" << cameras.size();
    for (int i = 0; i < cameras.size(); ++i) {
        qDebug() << "  " << i << ":" << cameras[i].description();
    }

    if (cameras.isEmpty()) {
        qDebug() << "No cameras found!";
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

    // Create camera (following src_ref pattern exactly)
    QCamera* camera = new QCamera(cameras.first());

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

