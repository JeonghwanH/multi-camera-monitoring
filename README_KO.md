# 멀티 카메라 모니터링 애플리케이션

다중 카메라 소스를 모니터링하고 버퍼링된 재생과 청크 기반 녹화를 지원하는 네이티브 C++ Qt 애플리케이션입니다.

## 주요 기능

- **멀티 슬롯 그리드 디스플레이**: 설정 가능한 그리드 레이아웃 (기본: 2×4, 8슬롯)
- **다양한 소스 타입 지원**: 유선 카메라 (USB/V4L2) 및 RTSP 스트림
- **독립적인 슬롯 운영**: 각 카메라가 별도의 스레드에서 실행
- **버퍼링 재생**: 설정 가능한 버퍼 크기로 부드러운 재생
- **청크 기반 녹화**: 메모리 효율적인 비디오 저장 (설정 가능한 청크 시간)
- **자동 감지**: 유선 카메라 자동 감지 및 연결
- **확대 보기**: 슬롯 더블 클릭으로 큰 창에서 보기
- **유연한 설정**: UI와 JSON을 통한 모든 설정 가능
- **크로스 플랫폼**: macOS, Linux (Ubuntu), Windows 지원

## 스크린샷

```
┌──────────────────────────────────────────────────────────────┐
│                     멀티 카메라 모니터                         │
├──────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌──────┐ │
│  │ 0           │  │ 1           │  │ 2           │  │ 3    │ │
│  │   카메라    │  │   카메라    │  │   카메라    │  │ 신호 │ │
│  │    영상     │  │    영상     │  │    영상     │  │ 없음 │ │
│  │  [자동 ▼]   │  │  [자동 ▼]   │  │ [RTSP ▼]   │  │[없음]│ │
│  └─────────────┘  └─────────────┘  └─────────────┘  └──────┘ │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌──────┐ │
│  │ 4           │  │ 5           │  │ 6           │  │ 7    │ │
│  │   카메라    │  │  버퍼링...  │  │   카메라    │  │ 신호 │ │
│  │    영상     │  │             │  │    영상     │  │ 없음 │ │
│  │  [자동 ▼]   │  │  [자동 ▼]   │  │ [유선 2▼]  │  │[없음]│ │
│  └─────────────┘  └─────────────┘  └─────────────┘  └──────┘ │
└──────────────────────────────────────────────────────────────┘
```

## 빠른 시작

```bash
# 클론 및 빌드
git clone <repository>
cd multi-camera-monitoring
mkdir build && cd build
cmake ..
make -j$(nproc)

# 실행
./multi-camera-monitor
```

## 문서

| 문서 | 설명 |
|------|------|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | 시스템 아키텍처 및 프로젝트 구조 |
| [COMPONENTS.md](docs/COMPONENTS.md) | 상세 컴포넌트 명세 |
| [CONFIGURATION.md](docs/CONFIGURATION.md) | 설정 스키마 및 옵션 |
| [IMPLEMENTATION.md](docs/IMPLEMENTATION.md) | 구현 세부사항 및 코드 예제 |
| [BUILD.md](docs/BUILD.md) | 모든 플랫폼 빌드 가이드 |
| [MACOS_CAMERA_PERMISSION.md](docs/MACOS_CAMERA_PERMISSION.md) | macOS 카메라 권한 설정 |

## 소스 선택

각 슬롯은 다양한 소스 타입으로 설정할 수 있습니다:

| 소스 타입 | 설명 |
|-----------|------|
| **None (없음)** | 스트리밍 없음 ("신호 없음" 표시) |
| **Auto (자동)** | 슬롯 번호와 일치하는 장치 인덱스에 자동 연결 |
| **Wired [0-7] (유선)** | 특정 USB/V4L2 장치 인덱스 |
| **RTSP** | 사용자 지정 RTSP 스트림 URL |

## 설정

`config.json` 파일을 편집하거나 설정 화면을 사용하세요:

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

### 설정 항목 설명

| 항목 | 설명 |
|------|------|
| `grid.rows` | 그리드 행 수 |
| `grid.columns` | 그리드 열 수 |
| `buffer.frameCount` | 버퍼에 유지할 프레임 수 |
| `buffer.minMaintenance` | 최소 유지 프레임 수 |
| `recording.enabled` | 녹화 활성화 여부 |
| `recording.chunkDurationSeconds` | 청크당 녹화 시간 (초) |
| `recording.outputDirectory` | 녹화 파일 저장 경로 |
| `recording.fps` | 녹화 프레임 레이트 |
| `recording.codec` | 비디오 코덱 |

## 녹화 출력

비디오는 슬롯별로 청크 단위로 저장됩니다:
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

파일명 형식: `{청크번호}_{날짜}_{시간}.mp4`

## 사용법

### 기본 조작

1. **애플리케이션 실행**: `./multi-camera-monitor` 또는 `./run.sh`
2. **소스 변경**: 각 슬롯 하단의 드롭다운 메뉴에서 소스 선택
3. **확대 보기**: 슬롯을 더블 클릭하여 큰 창에서 보기
4. **설정 변경**: 설정 화면에서 그리드, 버퍼, 녹화 옵션 변경

### RTSP 스트림 연결

1. 소스 드롭다운에서 "RTSP" 선택
2. RTSP URL 입력 (예: `rtsp://192.168.1.100:554/stream`)
3. 연결 확인

### 디버그 모드 실행

```bash
./run_debug.sh
```

디버그 모드에서는 각 슬롯에 FPS가 표시됩니다.

## 시스템 요구사항

- Qt 6.5+
- CMake 3.16+
- C++17 컴파일러

### 플랫폼별 요구사항

**macOS:**
- Xcode Command Line Tools
- 카메라 권한 필요 ([MACOS_CAMERA_PERMISSION.md](docs/MACOS_CAMERA_PERMISSION.md) 참조)

**Linux (Ubuntu):**
- GStreamer 플러그인 (Qt Multimedia용)
  ```bash
  sudo apt install gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad
  ```
- v4l2 유틸리티 (디버깅용, 선택사항)
  ```bash
  sudo apt install v4l-utils
  ```

**Windows:**
- Visual Studio 2019+ 또는 MinGW

## 문제 해결

### 카메라가 인식되지 않음

**macOS:**
- 시스템 환경설정 → 보안 및 개인 정보 보호 → 카메라에서 권한 확인
- 터미널에서 권한 재설정: `tccutil reset Camera`

**Linux:**
- 장치 확인: `v4l2-ctl --list-devices`
- 권한 확인: `ls -la /dev/video*`
- 사용자를 video 그룹에 추가: `sudo usermod -aG video $USER`

### "신호 없음" 표시

- 카메라가 연결되어 있는지 확인
- 다른 애플리케이션에서 카메라를 사용 중인지 확인
- Auto 모드에서 슬롯 번호와 일치하는 장치가 있는지 확인

### 녹화 파일이 생성되지 않음

- `recordings` 디렉토리 쓰기 권한 확인
- 설정에서 녹화가 활성화되어 있는지 확인
- 디스크 공간 확인

## 라이선스

MIT License

