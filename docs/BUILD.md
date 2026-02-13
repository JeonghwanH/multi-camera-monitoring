# Build Instructions

## Dependencies

### Required Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| Qt 6 | 6.5+ | GUI framework, threading, JSON |
| FFmpeg | 5.0+ | RTSP streaming, video encoding |
| OpenCV | 4.8+ | Video capture, image processing |

### Platform-Specific

| Platform | Additional Requirements |
|----------|------------------------|
| Linux | V4L2 headers, pkg-config |
| macOS | Xcode Command Line Tools |
| Windows | Visual Studio 2019+, vcpkg |

---

## Build Steps

### Linux (Ubuntu/Debian)

```bash
# Install dependencies
sudo apt update
sudo apt install -y \
    qt6-base-dev \
    qt6-multimedia-dev \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libopencv-dev \
    cmake \
    build-essential

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run
./multi-camera-monitor
```

### macOS

```bash
# Install dependencies via Homebrew
brew install qt@6 ffmpeg opencv cmake

# Build
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)
make -j$(sysctl -n hw.ncpu)

# Run
./multi-camera-monitor
```

### Windows

```powershell
# Using vcpkg
vcpkg install qt6:x64-windows ffmpeg:x64-windows opencv:x64-windows

# Build with CMake
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release

# Run
.\Release\multi-camera-monitor.exe
```

---

## CMake Configuration

```cmake
cmake_minimum_required(VERSION 3.16)
project(multi-camera-monitor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find packages
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)
find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale)

# Sources
set(SOURCES
    src/main.cpp
    src/core/Config.cpp
    src/core/FrameBuffer.cpp
    src/core/VideoRecorder.cpp
    src/capture/CaptureThread.cpp
    src/capture/DeviceCapture.cpp
    src/capture/RtspCapture.cpp
    src/widgets/MainWindow.cpp
    src/widgets/HomeScreen.cpp
    src/widgets/MonitoringScreen.cpp
    src/widgets/SettingsScreen.cpp
    src/widgets/CameraSlot.cpp
    src/widgets/ExpandedView.cpp
    src/widgets/RtspInputDialog.cpp
    src/utils/DeviceDetector.cpp
)

set(HEADERS
    src/core/Config.h
    src/core/FrameBuffer.h
    src/core/VideoRecorder.h
    src/capture/CaptureThread.h
    src/capture/DeviceCapture.h
    src/capture/RtspCapture.h
    src/widgets/MainWindow.h
    src/widgets/HomeScreen.h
    src/widgets/MonitoringScreen.h
    src/widgets/SettingsScreen.h
    src/widgets/CameraSlot.h
    src/widgets/ExpandedView.h
    src/widgets/RtspInputDialog.h
    src/utils/DeviceDetector.h
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    ${OpenCV_LIBS}
    ${FFMPEG_LIBRARIES}
)

# Copy config file to build directory
configure_file(${CMAKE_SOURCE_DIR}/config.json ${CMAKE_BINARY_DIR}/config.json COPYONLY)
```

---

## Troubleshooting

### Qt not found
```bash
# Set Qt path explicitly
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt6
```

### FFmpeg headers not found
```bash
# Install development packages
sudo apt install libavcodec-dev libavformat-dev libswscale-dev libavutil-dev
```

### OpenCV not found
```bash
# Set OpenCV path
cmake .. -DOpenCV_DIR=/path/to/opencv/build
```

### Camera permission denied (macOS)
- Go to System Preferences → Security & Privacy → Camera
- Allow the application to access the camera

### Camera permission denied (Linux)
```bash
# Add user to video group
sudo usermod -aG video $USER
# Logout and login again
```

