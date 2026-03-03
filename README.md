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

- Qt 6.5+
- CMake 3.16+
- C++17 compiler

### Platform-Specific

**macOS:**
- Xcode Command Line Tools
- Camera permission required (see [MACOS_CAMERA_PERMISSION.md](docs/MACOS_CAMERA_PERMISSION.md))

**Linux (Ubuntu):**
- GStreamer plugins for Qt Multimedia
- v4l2 utilities (optional, for debugging)

**Windows:**
- Visual Studio 2019+ or MinGW

## License

MIT License
