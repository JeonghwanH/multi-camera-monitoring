# Multi-Camera Monitoring Application

A native C++ Qt application for monitoring multiple camera sources with buffered playback and chunk-based recording.

## Features

- **Multi-Slot Grid Display**: Configurable grid layout (default: 8 slots in 2×4)
- **Multiple Source Types**: Wired cameras (USB/V4L2) and RTSP streams
- **Independent Slot Operation**: Each camera runs in its own thread
- **Buffered Playback**: Smooth playback with configurable buffer size
- **Chunk-Based Recording**: Memory-efficient video saving with configurable chunk duration
- **Auto-Detection**: Automatically detects and connects to wired cameras
- **Expanded View**: Double-click any slot for a larger window view
- **Flexible Configuration**: All settings configurable via UI and JSON
- **Cross-Platform**: Supports macOS, Linux (Ubuntu), and Windows

## Screenshots

```
┌──────────────────────────────────────────────────────────────┐
│                     Multi-Camera Monitor                      │
├──────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌──────┐ │
│  │ 0           │  │ 1           │  │ 2           │  │ 3    │ │
│  │   Camera    │  │   Camera    │  │   Camera    │  │  No  │ │
│  │    Feed     │  │    Feed     │  │    Feed     │  │Signal│ │
│  │  [Auto ▼]   │  │  [Auto ▼]   │  │ [RTSP ▼]    │  │[None]│ │
│  └─────────────┘  └─────────────┘  └─────────────┘  └──────┘ │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌──────┐ │
│  │ 4           │  │ 5           │  │ 6           │  │ 7    │ │
│  │   Camera    │  │ Buffering...│  │   Camera    │  │  No  │ │
│  │    Feed     │  │             │  │    Feed     │  │Signal│ │
│  │  [Auto ▼]   │  │  [Auto ▼]   │  │  [Wired 2▼] │  │[None]│ │
│  └─────────────┘  └─────────────┘  └─────────────┘  └──────┘ │
└──────────────────────────────────────────────────────────────┘
```

## Quick Start

```bash
# Clone and build
git clone <repository>
cd multi-camera-monitoring
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run
./multi-camera-monitor
```

## Documentation

| Document | Description |
|----------|-------------|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | System architecture and project structure |
| [COMPONENTS.md](docs/COMPONENTS.md) | Detailed component specifications |
| [CONFIGURATION.md](docs/CONFIGURATION.md) | Configuration schema and options |
| [IMPLEMENTATION.md](docs/IMPLEMENTATION.md) | Implementation details and code examples |
| [BUILD.md](docs/BUILD.md) | Build instructions for all platforms |
| [MACOS_CAMERA_PERMISSION.md](docs/MACOS_CAMERA_PERMISSION.md) | macOS camera permission setup |

## Source Selection

Each slot can be configured with different source types:

| Source Type | Description |
|-------------|-------------|
| **None** | No streaming (displays "No Signal") |
| **Auto** | Automatically pairs with device index matching slot number |
| **Wired [0-7]** | Specific USB/V4L2 device index |
| **RTSP** | Custom RTSP stream URL |

## Configuration

Edit `config.json` or use the Settings screen:

```json
{
    "grid": {
        "rows": 2,
        "columns": 4
    },
    "buffer": {
        "frameCount": 30,
        "minMaintenance": 10
    },
    "recording": {
        "enabled": true,
        "chunkDurationSeconds": 300,
        "outputDirectory": "recordings",
        "fps": 30,
        "codec": "mp4v"
    }
}
```

## Recording Output

Videos are saved in chunks per slot:
```
recordings/
├── slot_0/
│   ├── 001_20260212_143052.mp4
│   ├── 002_20260212_143552.mp4
│   └── ...
├── slot_1/
│   └── ...
└── ...
```

## Requirements

- Qt 6.5+ (Qt 6.7+ recommended for hardware encoding control)
- CMake 3.16+
- C++17 compiler

### Platform-Specific

**macOS:**
- Xcode Command Line Tools
- Camera permission required (see [MACOS_CAMERA_PERMISSION.md](docs/MACOS_CAMERA_PERMISSION.md))

**Linux (Ubuntu):**
- FFmpeg backend for Qt Multimedia (set automatically)
- v4l2 utilities (optional, for debugging)
- For hardware encoding: see [Hardware Encoding on Linux](#hardware-encoding-on-linux)

**Windows:**
- Visual Studio 2019+ or MinGW

## Hardware Encoding on Linux

### Qt Version Requirements

| Qt Version | Hardware Encoding Support |
|------------|--------------------------|
| 6.4.x (Ubuntu default) | Limited - `QT_FFMPEG_ENCODING_HW_DEVICE_TYPES` ignored |
| 6.6+ | Basic hardware encoder selection |
| 6.7+ | Full control via `QT_FFMPEG_ENCODING_HW_DEVICE_TYPES` |

To use hardware encoding control, install Qt 6.7+ via `aqtinstall`:

```bash
# setup.sh handles this automatically, or manually:
pip install aqtinstall
aqt install-qt linux desktop 6.8.0 linux_gcc_64 -m qtmultimedia -O ~/Qt
```

### Supported Hardware Encoders

| Encoder | Environment Variable | GPU |
|---------|---------------------|-----|
| NVENC | `QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=cuda` | NVIDIA |
| VA-API | `QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=vaapi` | Intel/AMD |
| QSV | `QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=qsv` | Intel Quick Sync |

### NVENC (NVIDIA) Setup

NVENC typically works out of the box with NVIDIA drivers:

```bash
# Verify NVENC support
ffmpeg -encoders | grep nvenc

# Test
QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=cuda ./build/test_qt_encoding_env
```

### VA-API (Intel/AMD) Limitations

VA-API H.264 encoding has a **pixel format limitation**:

- VA-API H.264 only accepts **NV12** input format
- Many cameras output **YUYV422** format
- Qt/FFmpeg doesn't automatically convert pixel formats for VA-API

**Symptom:**
```
Input surface format is yuyv422.
No usable encoding profile found.
```

**Direct FFmpeg workaround** (requires manual format conversion):
```bash
# Fails (no conversion)
ffmpeg -f v4l2 -i /dev/video0 -c:v h264_vaapi -vaapi_device /dev/dri/renderD128 out.mp4

# Works (with format conversion filter)
ffmpeg -f v4l2 -i /dev/video0 -vaapi_device /dev/dri/renderD128 \
       -vf 'format=nv12,hwupload' -c:v h264_vaapi out.mp4
```

**Note:** This limitation doesn't affect NVENC, which handles format conversion automatically.

### VA-API Driver Setup

If you want to use VA-API (and your camera outputs NV12):

```bash
# Install VA-API drivers
sudo apt install vainfo intel-media-va-driver-non-free  # Intel
sudo apt install vainfo mesa-va-drivers                  # AMD

# Verify H.264 encoding support
vainfo | grep -i "h264.*enc"
# Should show: VAProfileH264Main : VAEntrypointEncSlice
```

### Recommended Configuration

For most setups with NVIDIA GPU, use NVENC:

```bash
export QT_MEDIA_BACKEND=ffmpeg
export QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=cuda
```

The `setup.sh` script configures this automatically in `qt_env.sh`.

## License

MIT License
