# Core Components Detail

## 1. Config (Singleton)

Manages all configuration via JSON file.

```cpp
class Config {
    // Grid settings
    struct GridConfig {
        int maxSlots;      // Maximum number of camera slots (default: 8)
        int rows;          // Grid rows (default: 2)
        int columns;       // Grid columns (default: 4)
    };

    // Buffer settings
    struct BufferConfig {
        int frameCount;      // Max frames in buffer (default: 30)
        int minMaintenance;  // Min frames before playback starts (default: 10)
    };

    // Recording settings
    struct RecordingConfig {
        bool enabled;              // Enable/disable recording
        int chunkDurationSeconds;  // Chunk duration (default: 300 = 5 min)
        QString outputDirectory;   // Save location
        int fps;                   // Recording FPS
        QString codec;             // Video codec (mp4v, h264, etc.)
    };

    // Per-slot settings
    struct SlotConfig {
        QString type;    // "auto", "none", "wired", "rtsp"
        QString source;  // Device index (for wired) or URL (for rtsp)
    };
};
```

---

## 2. FrameBuffer (Thread-safe Circular Buffer)

Per-slot frame buffer with maintenance threshold for smooth playback.

```cpp
class FrameBuffer : public QObject {
    Q_OBJECT

public:
    void push(const QImage& frame);     // Producer (capture thread)
    QImage pop();                        // Consumer (display thread)
    int size() const;                    // Current frame count
    bool isBelowMaintenance() const;     // Check if should pause display
    bool isEmpty() const;
    void clear();

signals:
    void bufferStateChanged(bool isHealthy);  // Emitted when crossing threshold

private:
    std::deque<QImage> m_buffer;
    mutable QMutex m_mutex;
    QWaitCondition m_notEmpty;
    int m_maxSize;           // From config: buffer.frameCount
    int m_minMaintenance;    // From config: buffer.minMaintenance
};
```

**Behavior:**
- When buffer size < `minMaintenance`: Display shows "Buffering..." and waits
- When buffer size >= `minMaintenance`: Normal playback resumes
- When buffer is full: Oldest frame is dropped (circular behavior)

---

## 3. CaptureThread (QThread)

Independent thread per slot for video capture.

```cpp
class CaptureThread : public QThread {
    Q_OBJECT

public:
    void setSource(SourceType type, const QString& source);
    void stopCapture();

signals:
    void frameReady(const QImage& frame);
    void connectionLost();
    void connectionRestored();
    void errorOccurred(const QString& message);

protected:
    void run() override;

private:
    SourceType m_sourceType;  // WIRED or RTSP
    QString m_source;         // Device index or URL
    std::atomic<bool> m_running;
    FrameBuffer* m_buffer;
    VideoRecorder* m_recorder;
};
```

---

## 4. VideoRecorder

Chunk-based recording per slot for memory-efficient storage.

```cpp
class VideoRecorder : public QObject {
    Q_OBJECT

public:
    void startRecording(int slotId);
    void stopRecording();
    void writeFrame(const QImage& frame);

private:
    void rotateChunk();      // Start new chunk file
    QString generateFilename() const;

    // File path format: {outputDir}/{slotId}/{chunkNum}_{startDateTime}.mp4
    // Example: recordings/0/001_20260212_143052.mp4

    int m_slotId;
    int m_chunkNumber;
    int m_framesInCurrentChunk;
    int m_maxFramesPerChunk;  // Calculated from chunkDurationSeconds * fps
    QDateTime m_chunkStartTime;
    cv::VideoWriter m_writer;
};
```

**Chunk Management:**
- New chunk created when frame count exceeds `chunkDurationSeconds * fps`
- Each chunk is a complete, playable video file
- Filename includes chunk number and start timestamp

---

## 5. CameraSlot (QWidget)

Individual camera display widget with source selection.

```cpp
class CameraSlot : public QWidget {
    Q_OBJECT

public:
    explicit CameraSlot(int slotIndex, QWidget* parent = nullptr);
    
    void setSource(SourceType type, const QString& source);
    int slotIndex() const;

signals:
    void doubleClicked(int slotIndex);
    void sourceChanged(int slotIndex, SourceType type, const QString& source);

protected:
    void paintEvent(QPaintEvent* event) override;      // Draw slot number overlay
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private slots:
    void onSourceSelectorChanged(int index);
    void onFrameReady(const QImage& frame);
    void onBufferStateChanged(bool isHealthy);

private:
    int m_slotIndex;
    QLabel* m_videoDisplay;
    QComboBox* m_sourceSelector;  // None, Auto, Wired 0-7, RTSP
    QLabel* m_statusLabel;
    CaptureThread* m_captureThread;
    FrameBuffer* m_buffer;
    
    bool m_isBuffering;
    QImage m_currentFrame;
};
```

**Source Selector Options:**
1. `None` - No streaming
2. `Auto` - Uses device index matching slot number (Slot 0 → Device 0)
3. `Wired 0` through `Wired 7` - Specific device index
4. `RTSP` - Opens URL input dialog

---

## 6. DeviceDetector

Monitors for camera connection/disconnection events.

```cpp
class DeviceDetector : public QObject {
    Q_OBJECT

public:
    struct DeviceInfo {
        int index;
        QString name;
        bool available;
    };

    QList<DeviceInfo> detectDevices();
    void startMonitoring(int intervalMs = 1000);
    void stopMonitoring();

signals:
    void deviceAdded(int index, const QString& name);
    void deviceRemoved(int index);
    void devicesChanged(const QList<DeviceInfo>& devices);

private:
    QTimer* m_pollTimer;
    QList<DeviceInfo> m_lastKnownDevices;
    
    // Platform-specific implementation:
    // - Linux: V4L2 (/dev/video*)
    // - Windows: DirectShow
    // - macOS: AVFoundation
};
```

---

## 7. ExpandedView (QMainWindow)

Full-screen or large window view for a single camera.

```cpp
class ExpandedView : public QMainWindow {
    Q_OBJECT

public:
    explicit ExpandedView(int slotIndex, QWidget* parent = nullptr);
    void updateFrame(const QImage& frame);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;  // ESC to close

private:
    int m_slotIndex;
    QLabel* m_videoLabel;
    QImage m_currentFrame;
    
    void fitFrameToWindow();  // Maintain aspect ratio
};
```

---

## 8. RtspInputDialog (QDialog)

Dialog for entering RTSP URL.

```cpp
class RtspInputDialog : public QDialog {
    Q_OBJECT

public:
    explicit RtspInputDialog(QWidget* parent = nullptr);
    QString getUrl() const;

private:
    QLineEdit* m_urlInput;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
    
    // Placeholder: rtsp://username:password@ip:port/path
};
```

