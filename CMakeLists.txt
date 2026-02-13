cmake_minimum_required(VERSION 3.16)
project(multi-camera-monitor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)

# Find OpenCV
find_package(OpenCV REQUIRED)

# Find FFmpeg via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)

# On macOS with Homebrew, we need to explicitly add library directories
if(APPLE)
    # Add Homebrew library paths
    link_directories(/opt/homebrew/lib)
    link_directories(/usr/local/lib)
    
    # Also add from pkg-config
    link_directories(${AVCODEC_LIBRARY_DIRS})
    link_directories(${AVFORMAT_LIBRARY_DIRS})
    link_directories(${AVUTIL_LIBRARY_DIRS})
    link_directories(${SWSCALE_LIBRARY_DIRS})
endif()

# Collect all source files
set(CORE_SOURCES
    src/core/Config.cpp
    src/core/FrameBuffer.cpp
    src/core/VideoRecorder.cpp
)

set(CORE_HEADERS
    src/core/Config.h
    src/core/FrameBuffer.h
    src/core/VideoRecorder.h
)

set(CAPTURE_SOURCES
    src/capture/CaptureThread.cpp
    src/capture/DeviceCapture.cpp
    src/capture/RtspCapture.cpp
)

set(CAPTURE_HEADERS
    src/capture/CaptureThread.h
    src/capture/DeviceCapture.h
    src/capture/RtspCapture.h
)

set(WIDGETS_SOURCES
    src/widgets/MainWindow.cpp
    src/widgets/HomeScreen.cpp
    src/widgets/MonitoringScreen.cpp
    src/widgets/SettingsScreen.cpp
    src/widgets/CameraSlot.cpp
    src/widgets/ExpandedView.cpp
    src/widgets/RtspInputDialog.cpp
    src/widgets/VideoWidget.cpp
)

set(WIDGETS_HEADERS
    src/widgets/MainWindow.h
    src/widgets/HomeScreen.h
    src/widgets/MonitoringScreen.h
    src/widgets/SettingsScreen.h
    src/widgets/CameraSlot.h
    src/widgets/ExpandedView.h
    src/widgets/RtspInputDialog.h
    src/widgets/VideoWidget.h
)

set(UTILS_SOURCES
    src/utils/DeviceDetector.cpp
)

set(UTILS_HEADERS
    src/utils/DeviceDetector.h
)

set(ALL_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${CAPTURE_SOURCES}
    ${WIDGETS_SOURCES}
    ${UTILS_SOURCES}
)

set(ALL_HEADERS
    ${CORE_HEADERS}
    ${CAPTURE_HEADERS}
    ${WIDGETS_HEADERS}
    ${UTILS_HEADERS}
)

# Resources
set(RESOURCES
    resources/resources.qrc
)

# Create executable
add_executable(${PROJECT_NAME}
    ${ALL_SOURCES}
    ${ALL_HEADERS}
    ${RESOURCES}
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
)

# Add library directories for the target (modern CMake way)
target_link_directories(${PROJECT_NAME} PRIVATE
    ${AVCODEC_LIBRARY_DIRS}
    ${AVFORMAT_LIBRARY_DIRS}
    ${AVUTIL_LIBRARY_DIRS}
    ${SWSCALE_LIBRARY_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    ${OpenCV_LIBS}
    ${AVCODEC_LINK_LIBRARIES}
    ${AVFORMAT_LINK_LIBRARIES}
    ${AVUTIL_LINK_LIBRARIES}
    ${SWSCALE_LINK_LIBRARIES}
)

# Copy config file to build directory
configure_file(
    ${CMAKE_SOURCE_DIR}/config.json
    ${CMAKE_BINARY_DIR}/config.json
    COPYONLY
)

# Copy resources
file(COPY ${CMAKE_SOURCE_DIR}/resources/styles.qss DESTINATION ${CMAKE_BINARY_DIR}/resources/)

# Platform-specific settings
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.monitoring.multicamera
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    )
endif()

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
