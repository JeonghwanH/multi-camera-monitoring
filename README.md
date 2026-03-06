# Multi-Camera Monitoring Application

A native C++ Qt application for monitoring multiple camera sources with real-time display and chunk-based recording.

## Features

- **Multi-Slot Grid Display**: Configurable grid layout (default: 8 slots in 2×4)
- **Multiple Source Types**: USB cameras, V4L2 devices, and RTSP streams
- **Chunk-Based Recording**: Continuous video recording with automatic file rotation
- **Hardware Encoding**: GPU-accelerated recording (VideoToolbox/NVENC/VA-API)
- **Auto-Detection**: Automatic camera detection and listing
- **Cross-Platform**: macOS, Linux (Ubuntu), Windows

## Quick Start

### Using setup.sh (Recommended)

```bash
git clone <repository>
cd multi-camera-monitoring
./setup.sh
```

### Manual Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./multi-camera-monitor
```

## Documentation

| Document | Description |
|----------|-------------|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | System architecture and project structure |
| [COMPONENTS.md](docs/COMPONENTS.md) | Component specifications |
| [CONFIGURATION.md](docs/CONFIGURATION.md) | Configuration options |
| [BUILD.md](docs/BUILD.md) | Build instructions for all platforms |
| [GUIDE_KO.md](docs/GUIDE_KO.md) | 한국어 사용 가이드 |

## Source Selection

| Source Type | Description |
|-------------|-------------|
| **None** | No streaming (displays "No Signal") |
| **Auto** | Auto-pairs with device matching slot number |
| **Wired [0-7]** | Specific USB/V4L2 device |
| **RTSP** | RTSP stream URL |

## Configuration

Edit `config.json`:

```json
{
    "grid": {
        "rows": 2,
        "columns": 4
    },
    "recording": {
        "enabled": true,
        "chunkDurationSeconds": 300,
        "outputDirectory": "recordings"
    }
}
```

## Recording

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

- Qt 6.4+ (Qt 6.7+ recommended for full hardware encoding control)
- CMake 3.16+
- C++17 compiler

### Platform-Specific

| Platform | Backend | Hardware Encoder |
|----------|---------|------------------|
| **macOS** | AVFoundation | VideoToolbox (H.264) |
| **Linux** | FFmpeg | NVENC / VA-API / Software |
| **Windows** | Windows Media | Media Foundation |

### Linux Hardware Encoding

The application automatically tries hardware encoding with fallback:
1. **H.264 Hardware** (NVENC or VA-API)
2. **MPEG4 Software** (fallback)

For NVIDIA GPUs:
```bash
export QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=cuda
```

For Intel/AMD (VA-API):
```bash
sudo apt install intel-media-va-driver-non-free  # Intel
sudo apt install mesa-va-drivers                  # AMD
```

**Note:** VA-API requires NV12 pixel format. Some cameras output YUYV which may cause encoding to fall back to software.

## Known Limitations

### Multiple Identical Cameras on Linux

When running multiple cameras with identical resolution/format/framerate in a single process, frame interference may occur due to FFmpeg's internal buffer pool. Workarounds:
- Use cameras with different resolutions
- Reduce frame rate (prefer 30fps over 60fps)
- Use separate USB controllers for each camera group

## License

MIT License
