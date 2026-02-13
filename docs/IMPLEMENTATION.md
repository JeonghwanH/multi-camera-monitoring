# Implementation Details

## Key Features Implementation

### 1. Independent Slot Operation

Each slot runs its own `QThread` for capture, ensuring one slot's issues don't affect others.

```cpp
// Each CameraSlot owns its own:
- CaptureThread (QThread)   // Independent capture loop
- FrameBuffer               // Separate buffer
- VideoRecorder             // Separate recording

// No shared state between slots except Config (read-only during streaming)
```

**Thread Model:**
```
Main Thread (GUI)
    │
    ├── CameraSlot[0]
    │       └── CaptureThread[0] ──▶ FrameBuffer[0] ──▶ VideoRecorder[0]
    │
    ├── CameraSlot[1]
    │       └── CaptureThread[1] ──▶ FrameBuffer[1] ──▶ VideoRecorder[1]
    │
    └── ... (up to maxSlots)
```

---

### 2. Buffer Management

Lock-free-ish circular buffer with condition variables for producer-consumer synchronization.

```cpp
void FrameBuffer::push(const QImage& frame) {
    QMutexLocker locker(&m_mutex);
    
    if (m_buffer.size() >= m_maxSize) {
        m_buffer.pop_front();  // Drop oldest frame
    }
    m_buffer.push_back(frame);
    
    // Check if we crossed the maintenance threshold
    bool wasUnhealthy = (m_buffer.size() - 1) < m_minMaintenance;
    bool isHealthy = m_buffer.size() >= m_minMaintenance;
    
    if (wasUnhealthy && isHealthy) {
        emit bufferStateChanged(true);
    }
    
    m_notEmpty.wakeOne();
}

QImage FrameBuffer::pop() {
    QMutexLocker locker(&m_mutex);
    
    while (m_buffer.empty()) {
        m_notEmpty.wait(&m_mutex);
    }
    
    QImage frame = m_buffer.front();
    m_buffer.pop_front();
    
    // Check if we dropped below maintenance
    if (m_buffer.size() < m_minMaintenance) {
        emit bufferStateChanged(false);
    }
    
    return frame;
}
```

---

### 3. RTSP Handling with FFmpeg

Using FFmpeg's libavformat for robust RTSP decode.

```cpp
// RtspCapture.cpp
void RtspCapture::run() {
    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    
    // Open RTSP stream
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);  // Use TCP for reliability
    av_dict_set(&options, "stimeout", "5000000", 0);    // 5 second timeout
    
    if (avformat_open_input(&formatCtx, m_url.toStdString().c_str(), 
                            nullptr, &options) < 0) {
        emit errorOccurred("Failed to open RTSP stream");
        return;
    }
    
    // Find video stream and setup decoder...
    // Decode frames and push to buffer...
    
    while (m_running) {
        AVPacket packet;
        if (av_read_frame(formatCtx, &packet) >= 0) {
            // Decode and convert to QImage
            QImage frame = decodeFrame(&packet);
            if (!frame.isNull()) {
                m_buffer->push(frame);
                if (m_recorder && m_recorder->isRecording()) {
                    m_recorder->writeFrame(frame);
                }
            }
            av_packet_unref(&packet);
        }
    }
    
    // Cleanup...
}
```

---

### 4. Device Detection (Platform-Specific)

#### Linux (V4L2)
```cpp
QList<DeviceInfo> DeviceDetector::detectDevices_Linux() {
    QList<DeviceInfo> devices;
    QDir devDir("/dev");
    
    for (const QString& entry : devDir.entryList(QStringList() << "video*")) {
        int index = entry.mid(5).toInt();  // Extract number from "video0"
        
        // Try to open and get device name
        QString path = "/dev/" + entry;
        int fd = open(path.toStdString().c_str(), O_RDONLY);
        if (fd >= 0) {
            struct v4l2_capability cap;
            if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
                devices.append({
                    index,
                    QString::fromUtf8((char*)cap.card),
                    true
                });
            }
            close(fd);
        }
    }
    
    return devices;
}
```

#### macOS (AVFoundation)
```cpp
// Uses QMediaDevices from Qt Multimedia
QList<DeviceInfo> DeviceDetector::detectDevices_macOS() {
    QList<DeviceInfo> devices;
    int index = 0;
    
    for (const QCameraDevice& camera : QMediaDevices::videoInputs()) {
        devices.append({
            index++,
            camera.description(),
            true
        });
    }
    
    return devices;
}
```

---

### 5. Chunk Recording

Timer-based chunk rotation with proper file handling.

```cpp
void VideoRecorder::writeFrame(const QImage& frame) {
    if (!m_recording) return;
    
    // Check if need to rotate chunk
    if (m_framesInCurrentChunk >= m_maxFramesPerChunk) {
        rotateChunk();
    }
    
    // Convert QImage to cv::Mat and write
    cv::Mat mat = qImageToMat(frame);
    m_writer.write(mat);
    m_framesInCurrentChunk++;
}

void VideoRecorder::rotateChunk() {
    // Close current chunk
    if (m_writer.isOpened()) {
        m_writer.release();
    }
    
    // Start new chunk
    m_chunkNumber++;
    m_framesInCurrentChunk = 0;
    m_chunkStartTime = QDateTime::currentDateTime();
    
    QString filename = generateFilename();
    m_writer.open(
        filename.toStdString(),
        cv::VideoWriter::fourcc(m_codec[0], m_codec[1], m_codec[2], m_codec[3]),
        m_fps,
        cv::Size(m_frameWidth, m_frameHeight)
    );
}

QString VideoRecorder::generateFilename() const {
    // Format: {outputDir}/slot_{slotId}/{chunkNum}_{datetime}.mp4
    QString dir = QString("%1/slot_%2")
        .arg(m_outputDirectory)
        .arg(m_slotId);
    
    QDir().mkpath(dir);
    
    return QString("%1/%2_%3.mp4")
        .arg(dir)
        .arg(m_chunkNumber, 3, 10, QChar('0'))  // Zero-padded: 001, 002, ...
        .arg(m_chunkStartTime.toString("yyyyMMdd_HHmmss"));
}
```

---

### 6. Expanded View (Double-Click)

Opens new window with aspect-ratio preserved frame.

```cpp
void CameraSlot::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        ExpandedView* view = new ExpandedView(m_slotIndex);
        view->setAttribute(Qt::WA_DeleteOnClose);
        view->setWindowTitle(QString("Camera %1 - Expanded View").arg(m_slotIndex));
        view->resize(1280, 720);
        view->show();
        
        // Connect frame updates
        connect(this, &CameraSlot::frameUpdated, view, &ExpandedView::updateFrame);
    }
}

void ExpandedView::updateFrame(const QImage& frame) {
    m_currentFrame = frame;
    fitFrameToWindow();
}

void ExpandedView::fitFrameToWindow() {
    if (m_currentFrame.isNull()) return;
    
    // Scale while maintaining aspect ratio
    QSize labelSize = m_videoLabel->size();
    QImage scaled = m_currentFrame.scaled(
        labelSize,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    );
    
    m_videoLabel->setPixmap(QPixmap::fromImage(scaled));
}
```

---

### 7. Slot Number Overlay

Painted directly on the video display.

```cpp
void CameraSlot::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    
    QPainter painter(this);
    
    // Draw slot number in top-left corner
    QFont font = painter.font();
    font.setPointSize(16);
    font.setBold(true);
    painter.setFont(font);
    
    // Background for readability
    QString text = QString::number(m_slotIndex);
    QRect textRect = painter.fontMetrics().boundingRect(text);
    textRect.adjust(-5, -2, 5, 2);
    textRect.moveTopLeft(QPoint(10, 10));
    
    painter.fillRect(textRect, QColor(0, 0, 0, 150));
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, text);
}
```

---

## Error Handling

| Scenario | Handling |
|----------|----------|
| Device disconnected | Show "Disconnected" overlay, attempt reconnect every 2s |
| RTSP timeout | Show "Connection Lost", attempt reconnect every 5s |
| Buffer underrun | Show "Buffering...", wait for minMaintenance frames |
| Recording failure | Log error, continue streaming without recording |
| Invalid config | Fall back to defaults, show warning |

---

## Performance Considerations

1. **Frame Conversion**: Use hardware-accelerated color space conversion when available
2. **Memory**: Each slot uses ~50-100MB for buffer (30 frames × 1080p)
3. **CPU**: FFmpeg decoding is multi-threaded per stream
4. **Disk I/O**: Chunk-based recording reduces continuous write pressure
5. **UI Updates**: Frame display throttled to 30fps max to prevent GUI freeze

