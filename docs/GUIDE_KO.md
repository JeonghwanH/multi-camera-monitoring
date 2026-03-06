# 멀티 카메라 모니터링 - 사용 가이드

## 개요

여러 대의 카메라를 동시에 모니터링하고 녹화할 수 있는 Qt 기반 데스크톱 애플리케이션입니다.

## 주요 기능

- **다중 카메라 지원**: 최대 8개 슬롯 동시 모니터링
- **다양한 소스**: USB 카메라, V4L2 캡처 카드, RTSP 스트림
- **연속 녹화**: 청크 단위 자동 파일 분할 녹화
- **하드웨어 인코딩**: GPU 가속 녹화 지원

## 설치 방법

### 자동 설치 (권장)

```bash
git clone <repository>
cd multi-camera-monitoring
./setup.sh
```

`setup.sh`가 자동으로:
- 필요한 패키지 설치
- Qt 빌드
- 환경 설정

### 수동 설치

```bash
# 의존성 설치 (Ubuntu)
sudo apt install qt6-base-dev qt6-multimedia-dev cmake build-essential

# 빌드
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## 실행 방법

```bash
# setup.sh로 설치한 경우
source qt_env.sh  # Qt 환경 로드
./run.sh

# 직접 실행
./build/multi-camera-monitor
```

## 사용 방법

### 1. 시작 화면

애플리케이션 시작 시 시작 화면이 표시됩니다.
- **Streaming** 버튼: 모니터링 화면으로 이동
- **Settings** 버튼: 설정 화면으로 이동

### 2. 모니터링 화면

#### 카메라 소스 선택

각 슬롯의 드롭다운 메뉴에서 소스를 선택합니다:

| 소스 타입 | 설명 |
|-----------|------|
| **None** | 스트리밍 없음 ("No Signal" 표시) |
| **Auto (Device N)** | 슬롯 번호와 매칭되는 자동 장치 |
| **카메라 이름 (/dev/videoN)** | 특정 카메라 장치 |
| **RTSP Stream...** | RTSP URL 입력 |

#### 스트리밍 시작

1. 각 슬롯에서 원하는 소스 선택
2. 상단의 **Play All** 버튼 클릭
3. 선택된 모든 소스가 동시에 재생 시작

#### 녹화

- 녹화는 설정에서 활성화된 경우 자동으로 시작됩니다
- 녹화 파일은 `recordings/slot_N/` 폴더에 저장됩니다
- 설정된 시간(기본 5분)마다 자동으로 새 파일 생성

### 3. 설정 화면

#### 그리드 설정
- **행(Rows)**: 그리드 행 수 (1-4)
- **열(Columns)**: 그리드 열 수 (1-4)

#### 녹화 설정
- **녹화 활성화**: 녹화 On/Off
- **청크 시간**: 파일 분할 간격 (초)
- **저장 경로**: 녹화 파일 저장 위치

## 설정 파일

`config.json` 파일로 설정을 저장/불러오기:

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
    },
    "slots": [
        {"type": 1, "source": "0"},
        {"type": 1, "source": "1"},
        {"type": 0, "source": ""},
        {"type": 0, "source": ""}
    ]
}
```

### 슬롯 타입

| 타입 값 | 의미 |
|---------|------|
| 0 | None |
| 1 | Auto |
| 2 | Wired (특정 장치) |
| 3 | RTSP |

## 녹화 파일 구조

```
recordings/
├── slot_0/
│   ├── 001_20260306_100000.mp4
│   ├── 002_20260306_100500.mp4
│   └── ...
├── slot_1/
│   └── ...
└── ...
```

파일명 형식: `{청크번호}_{날짜}_{시간}.mp4`

## 플랫폼별 참고사항

### macOS

- 카메라 권한 필요: 시스템 환경설정 → 보안 및 개인 정보 → 카메라
- 하드웨어 인코딩: VideoToolbox (H.264) 자동 사용

### Linux (Ubuntu)

- Qt 6.4+ 필요 (Ubuntu 24.04 기본 제공)
- FFmpeg 백엔드 사용

#### 하드웨어 인코딩

**NVIDIA GPU (NVENC):**
```bash
export QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=cuda
```

**Intel/AMD GPU (VA-API):**
```bash
# 드라이버 설치
sudo apt install intel-media-va-driver-non-free  # Intel
sudo apt install mesa-va-drivers                  # AMD

# 확인
vainfo | grep -i h264
```

### Windows

- Visual Studio 2019+ 또는 MinGW 필요
- Media Foundation 백엔드 사용

## 문제 해결

### 카메라가 감지되지 않음

```bash
# Linux에서 장치 확인
v4l2-ctl --list-devices

# 권한 확인
ls -la /dev/video*
```

### 화면이 표시되지 않음

1. 카메라가 다른 프로그램에서 사용 중인지 확인
2. 드롭다운에서 소스를 다시 선택
3. **Play All** 버튼 클릭

### 프레임 혼선 (여러 카메라 이미지가 섞임)

동일한 해상도/프레임레이트의 카메라 여러 대 사용 시 발생 가능:
- 720p 우선 설정 사용 (기본값)
- 각 카메라를 다른 해상도로 설정
- 프레임레이트 30fps 이하 권장

### 녹화가 시작되지 않음

1. 저장 경로의 쓰기 권한 확인
2. 디스크 공간 확인
3. 콘솔 로그에서 인코딩 오류 확인

## 단축키

| 단축키 | 기능 |
|--------|------|
| 더블클릭 | 슬롯 확대 보기 |
| ESC | 확대 보기 닫기 |

## 기술 지원

문제가 지속되면 콘솔 출력을 포함하여 이슈를 등록해 주세요.

```bash
# 디버그 모드 실행
./build/multi-camera-monitor 2>&1 | tee debug.log
```

