#ifndef DEVICECAPTURE_H
#define DEVICECAPTURE_H

#include "CaptureThread.h"
#include <opencv2/opencv.hpp>

namespace MCM {

/**
 * @brief Capture thread for wired cameras (USB, built-in, etc.)
 * 
 * Uses OpenCV's VideoCapture for device access.
 * Supports automatic reconnection on device disconnect.
 */
class DeviceCapture : public CaptureThread {
    Q_OBJECT

public:
    explicit DeviceCapture(int slotId, QObject* parent = nullptr);
    ~DeviceCapture() override;

    /**
     * @brief Set the device index to capture from
     */
    void setDeviceIndex(int index);

    /**
     * @brief Get the current device index
     */
    int deviceIndex() const { return m_deviceIndex; }

protected:
    void run() override;

private:
    /**
     * @brief Try to open the device
     * @return true if successful
     */
    bool openDevice();

    /**
     * @brief Close the device
     */
    void closeDevice();

    /**
     * @brief Convert cv::Mat to QImage
     */
    QImage matToQImage(const cv::Mat& mat);

    int m_deviceIndex;
    cv::VideoCapture m_capture;
    
    // Reusable buffers (avoid allocation per frame)
    cv::Mat m_frameBuffer;
    cv::Mat m_rgbBuffer;
    QImage m_qimageBuffer;
    
    // Reconnection settings
    static constexpr int RECONNECT_DELAY_MS = 2000;
    static constexpr int FRAME_TIMEOUT_MS = 5000;
};

} // namespace MCM

#endif // DEVICECAPTURE_H

