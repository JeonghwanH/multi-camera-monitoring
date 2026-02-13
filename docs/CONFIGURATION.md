# Configuration Schema

## Overview

Configuration is stored in `config.json` and managed by the `Config` singleton class.

---

## Default Configuration

```json
{
    "grid": {
        "maxSlots": 8,
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
    },
    "slots": [
        {"type": "auto", "source": "0"},
        {"type": "auto", "source": "1"},
        {"type": "auto", "source": "2"},
        {"type": "auto", "source": "3"},
        {"type": "auto", "source": "4"},
        {"type": "auto", "source": "5"},
        {"type": "auto", "source": "6"},
        {"type": "auto", "source": "7"}
    ]
}
```

---

## Configuration Sections

### Grid Configuration

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `maxSlots` | int | 8 | Maximum number of camera slots |
| `rows` | int | 2 | Number of rows in the grid |
| `columns` | int | 4 | Number of columns in the grid |

**Note:** `rows × columns` should equal `maxSlots`

---

### Buffer Configuration

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `frameCount` | int | 30 | Maximum frames to buffer per slot |
| `minMaintenance` | int | 10 | Minimum frames required before playback starts |

**Behavior:**
- If buffer drops below `minMaintenance`, playback pauses and shows "Buffering..."
- When buffer reaches `minMaintenance` again, playback resumes
- This ensures smooth playback without stuttering

---

### Recording Configuration

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | true | Enable/disable video recording |
| `chunkDurationSeconds` | int | 300 | Duration of each video chunk (5 minutes) |
| `outputDirectory` | string | "recordings" | Base directory for saved videos |
| `fps` | int | 30 | Frames per second for recording |
| `codec` | string | "mp4v" | Video codec (mp4v, h264, xvid) |

**Recording Path Structure:**
```
{outputDirectory}/
├── slot_0/
│   ├── 001_20260212_143052.mp4
│   ├── 002_20260212_143552.mp4
│   └── ...
├── slot_1/
│   ├── 001_20260212_143052.mp4
│   └── ...
└── ...
```

---

### Slot Configuration

Each slot has its own configuration entry in the `slots` array.

| Field | Type | Values | Description |
|-------|------|--------|-------------|
| `type` | string | "auto", "none", "wired", "rtsp" | Source type |
| `source` | string | device index or URL | Source identifier |

**Slot Types:**

| Type | Source Value | Behavior |
|------|--------------|----------|
| `auto` | Device index (e.g., "0") | **Default.** Slot number = Device index. Slot 0 → Device 0, Slot 1 → Device 1, etc. |
| `none` | null or empty | No streaming, slot is disabled |
| `wired` | Device index (e.g., "2") | Specific wired camera by index |
| `rtsp` | URL string | RTSP stream URL |

**Default Behavior:**
- On first launch, all slots are set to `"auto"` with `source` matching slot index
- Slot 0 automatically connects to Device 0
- Slot 1 automatically connects to Device 1
- And so on...
- User can change any slot to different source type via UI

---

## Example Configurations

### All Wired Cameras (Default)

```json
{
    "slots": [
        {"type": "auto", "source": "0"},
        {"type": "auto", "source": "1"},
        {"type": "auto", "source": "2"},
        {"type": "auto", "source": "3"},
        {"type": "auto", "source": "4"},
        {"type": "auto", "source": "5"},
        {"type": "auto", "source": "6"},
        {"type": "auto", "source": "7"}
    ]
}
```

### Mixed Sources

```json
{
    "slots": [
        {"type": "auto", "source": "0"},
        {"type": "wired", "source": "1"},
        {"type": "rtsp", "source": "rtsp://192.168.1.100:554/stream1"},
        {"type": "rtsp", "source": "rtsp://192.168.1.101:554/stream1"},
        {"type": "none", "source": null},
        {"type": "none", "source": null},
        {"type": "auto", "source": "6"},
        {"type": "wired", "source": "3"}
    ]
}
```

### 4-Slot Grid

```json
{
    "grid": {
        "maxSlots": 4,
        "rows": 2,
        "columns": 2
    },
    "slots": [
        {"type": "auto", "source": "0"},
        {"type": "auto", "source": "1"},
        {"type": "auto", "source": "2"},
        {"type": "auto", "source": "3"}
    ]
}
```

---

## Settings UI

The Settings screen allows users to modify:

1. **Grid Settings**
   - Max slots (1-16)
   - Rows and columns

2. **Buffer Settings**
   - Frame count (10-120)
   - Minimum maintenance (5-60)

3. **Recording Settings**
   - Enable/disable
   - Chunk duration (60-3600 seconds)
   - Output directory (folder picker)
   - FPS (15-60)
   - Codec selection

Changes are saved immediately to `config.json` and applied on next stream start.

