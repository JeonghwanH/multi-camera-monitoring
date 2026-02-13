#include "DeviceCapture.h"
#include <QDebug>
#include <QThread>
#include <cstring>

namespace MCM {

DeviceCapture::DeviceCapture(int slotId, QObject* parent)
    : CaptureThread(slotId, parent)
    , m_deviceIndex(-1)
{
}

DeviceCapture::~DeviceCapture() {
    stopCapture();
    closeDevice();
}

void DeviceCapture::setDeviceIndex(int index) {
    m_deviceIndex = index;
    m_source = QString::number(index);
    m_sourceType = SourceType::Wired;
}

bool DeviceCapture::openDevice() {
    if (m_deviceIndex < 0) {
        return false;
    }
    
    closeDevice();
    
#ifdef __APPLE__
    // Use AVFoundation on macOS
    m_capture.open(m_deviceIndex, cv::CAP_AVFOUNDATION);
#elif defined(_WIN32)
    // Use DirectShow on Windows
    m_capture.open(m_deviceIndex, cv::CAP_DSHOW);
#else
    // Use V4L2 on Linux
    m_capture.open(m_deviceIndex, cv::CAP_V4L2);
#endif
    
    if (!m_capture.isOpened()) {
        // Try default backend
        m_capture.open(m_deviceIndex);
    }
    
    if (m_capture.isOpened()) {
        // Set capture properties for better performance
        m_capture.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
        m_capture.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        m_capture.set(cv::CAP_PROP_FPS, 30);
        m_capture.set(cv::CAP_PROP_BUFFERSIZE, 1);  // Minimize latency
        
        qDebug() << "DeviceCapture: Opened device" << m_deviceIndex 
                 << "for slot" << m_slotId;
        return true;
    }
    
    return false;
}

void DeviceCapture::closeDevice() {
    if (m_capture.isOpened()) {
        m_capture.release();
    }
}

void DeviceCapture::run() {
    m_running = true;
    m_connected = false;
    int failedAttempts = 0;
    const int maxFailedAttempts = 2;  // Stop rapid retries after 2 failures
    
    qDebug() << "DeviceCapture: Starting capture for slot" << m_slotId 
             << "device" << m_deviceIndex;
    
    while (m_running) {
        // Try to connect if not connected
        if (!m_connected) {
            if (openDevice()) {
                m_connected = true;
                failedAttempts = 0;
                emit connectionEstablished();
            } else {
                failedAttempts++;
                
                if (failedAttempts >= maxFailedAttempts) {
                    // Device doesn't exist, stop trying
                    qDebug() << "DeviceCapture: Device" << m_deviceIndex 
                             << "not available after" << maxFailedAttempts << "attempts, stopping";
                    emit errorOccurred(QString("Device %1 not available").arg(m_deviceIndex));
                    
                    // Wait longer between retry cycles for non-existent devices
                    // Use short sleep intervals to respond quickly to stop signal
                    int waitCount = 0;
                    while (m_running && waitCount < 100) {  // 100 * 100ms = 10 seconds
                        QThread::msleep(100);
                        waitCount++;
                    }
                    if (!m_running) break;
                    
                    if (openDevice()) {
                        m_connected = true;
                        failedAttempts = 0;
                        emit connectionEstablished();
                    }
                    continue;
                }
                
                // Wait before retry (check m_running frequently)
                for (int i = 0; i < 20 && m_running; ++i) {  // 20 * 100ms = 2 seconds
                    QThread::msleep(100);
                }
                continue;
            }
        }
        
        // Capture frame (reuse buffer)
        bool success = m_capture.read(m_frameBuffer);
        
        if (!success || m_frameBuffer.empty()) {
            // Lost connection
            if (m_connected) {
                m_connected = false;
                emit connectionLost();
                closeDevice();
            }
            QThread::msleep(100);
            continue;
        }
        
        // Convert and process frame (with buffer reuse)
        QImage qframe = matToQImage(m_frameBuffer);
        if (!qframe.isNull()) {
            processFrame(qframe);
        }
        
        // Small delay to prevent CPU overload
        QThread::msleep(1);
    }
    
    closeDevice();
    m_connected = false;
    
    qDebug() << "DeviceCapture: Stopped capture for slot" << m_slotId;
}

QImage DeviceCapture::matToQImage(const cv::Mat& mat) {
    if (mat.empty()) {
        return QImage();
    }
    
    int w = mat.cols;
    int h = mat.rows;
    
    switch (mat.type()) {
        case CV_8UC1: {
            // Grayscale - reuse buffer
            if (m_qimageBuffer.isNull() || 
                m_qimageBuffer.width() != w || 
                m_qimageBuffer.height() != h ||
                m_qimageBuffer.format() != QImage::Format_Grayscale8) {
                m_qimageBuffer = QImage(w, h, QImage::Format_Grayscale8);
            }
            memcpy(m_qimageBuffer.bits(), mat.data, w * h);
            return m_qimageBuffer.copy();  // Need copy for thread safety
        }
        case CV_8UC3: {
            // BGR -> RGB (reuse conversion buffer)
            cv::cvtColor(mat, m_rgbBuffer, cv::COLOR_BGR2RGB);
            
            // Reuse QImage buffer
            if (m_qimageBuffer.isNull() || 
                m_qimageBuffer.width() != w || 
                m_qimageBuffer.height() != h ||
                m_qimageBuffer.format() != QImage::Format_RGB888) {
                m_qimageBuffer = QImage(w, h, QImage::Format_RGB888);
            }
            
            // Copy row by row (handles stride differences)
            for (int y = 0; y < h; ++y) {
                memcpy(m_qimageBuffer.scanLine(y), m_rgbBuffer.ptr(y), w * 3);
            }
            return m_qimageBuffer.copy();  // Need copy for thread safety
        }
        case CV_8UC4: {
            // BGRA -> RGBA (reuse conversion buffer)
            cv::cvtColor(mat, m_rgbBuffer, cv::COLOR_BGRA2RGBA);
            
            // Reuse QImage buffer
            if (m_qimageBuffer.isNull() || 
                m_qimageBuffer.width() != w || 
                m_qimageBuffer.height() != h ||
                m_qimageBuffer.format() != QImage::Format_RGBA8888) {
                m_qimageBuffer = QImage(w, h, QImage::Format_RGBA8888);
            }
            
            // Copy row by row
            for (int y = 0; y < h; ++y) {
                memcpy(m_qimageBuffer.scanLine(y), m_rgbBuffer.ptr(y), w * 4);
            }
            return m_qimageBuffer.copy();  // Need copy for thread safety
        }
        default:
            qWarning() << "DeviceCapture: Unsupported mat type:" << mat.type();
            return QImage();
    }
}

} // namespace MCM

