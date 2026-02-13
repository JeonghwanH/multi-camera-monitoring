# Multi-Camera Monitoring Application - Architecture

## Technology Stack

| Component | Technology |
|-----------|------------|
| **Language** | C++17 |
| **GUI Framework** | Qt 6 (Widgets) |
| **Video Backend** | FFmpeg + Qt Multimedia (for RTSP & device capture) |
| **Build System** | CMake |
| **Configuration** | JSON (via Qt's QJsonDocument) |

---

## Project Structure

```
multi-camera-monitoring/
├── CMakeLists.txt
├── config.json                    # Default configuration
├── src/
│   ├── main.cpp                   # Entry point
│   ├── core/
│   │   ├── Config.h/cpp           # Configuration management
│   │   ├── FrameBuffer.h/cpp      # Thread-safe circular buffer
│   │   └── VideoRecorder.h/cpp    # Chunk-based video recording
│   ├── capture/
│   │   ├── CaptureThread.h/cpp    # Base capture thread
│   │   ├── DeviceCapture.h/cpp    # Wired camera capture (V4L2/DirectShow)
│   │   └── RtspCapture.h/cpp      # RTSP stream capture
│   ├── widgets/
│   │   ├── MainWindow.h/cpp       # Main application window
│   │   ├── HomeScreen.h/cpp       # Initial screen (Settings/Streaming buttons)
│   │   ├── MonitoringScreen.h/cpp # Grid of camera slots
│   │   ├── SettingsScreen.h/cpp   # Configuration UI
│   │   ├── CameraSlot.h/cpp       # Individual camera slot widget
│   │   ├── ExpandedView.h/cpp     # Double-click expanded window
│   │   └── RtspInputDialog.h/cpp  # RTSP URL input dialog
│   └── utils/
│       └── DeviceDetector.h/cpp   # Camera device detection
└── resources/
    ├── icons/
    └── styles.qss                 # Qt stylesheet
```

---

## Class Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                           MainWindow                                 │
│  ┌─────────────┐    ┌──────────────────┐    ┌─────────────────┐    │
│  │ HomeScreen  │───▶│ MonitoringScreen │    │ SettingsScreen  │    │
│  │             │    │                  │    │                 │    │
│  │ [Streaming] │    │ ┌──────────────┐ │    │ - Grid Config   │    │
│  │ [Settings]  │    │ │ CameraSlot[] │ │    │ - Buffer Config │    │
│  └─────────────┘    │ └──────────────┘ │    │ - Record Config │    │
│                     └──────────────────┘    └─────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                          CameraSlot                                  │
│  ┌─────────────────┐   ┌─────────────────┐   ┌──────────────────┐  │
│  │  VideoDisplay   │   │  SourceSelector │   │   StatusLabel    │  │
│  │  (QLabel)       │   │  (ComboBox)     │   │   (Slot #/Info)  │  │
│  └────────┬────────┘   └─────────────────┘   └──────────────────┘  │
│           │                                                         │
│           ▼                                                         │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    CaptureThread                             │   │
│  │  ┌───────────────┐  ┌───────────────┐  ┌─────────────────┐  │   │
│  │  │ DeviceCapture │  │  RtspCapture  │  │  FrameBuffer    │  │   │
│  │  │ or            │  │  (FFmpeg)     │  │  (Circular)     │  │   │
│  │  └───────────────┘  └───────────────┘  └────────┬────────┘  │   │
│  └─────────────────────────────────────────────────│───────────┘   │
│                                                    │                │
│                                                    ▼                │
│                                           ┌─────────────────┐      │
│                                           │  VideoRecorder  │      │
│                                           │  (Chunk-based)  │      │
│                                           └─────────────────┘      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Data Flow

```
┌──────────────┐     ┌─────────────┐     ┌─────────────┐     ┌──────────────┐
│   Camera/    │────▶│  Capture    │────▶│   Frame     │────▶│   Display    │
│   RTSP       │     │  Thread     │     │   Buffer    │     │   (QLabel)   │
└──────────────┘     └──────┬──────┘     └─────────────┘     └──────────────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ VideoRecorder │
                    │ (Chunk Save)  │
                    └───────────────┘
```

---

## Screen Flow

```
┌─────────────────────┐
│     HomeScreen      │
│  ┌───────────────┐  │
│  │  STREAMING    │──────────▶  MonitoringScreen (8-slot grid)
│  └───────────────┘  │
│  ┌───────────────┐  │
│  │   SETTINGS    │──────────▶  SettingsScreen
│  └───────────────┘  │
└─────────────────────┘
```

