# macOS Camera Permission Setup for Qt6 Multimedia

## Problem

When using `QCamera` on macOS with Qt6 installed via Homebrew, you may see:

```
qt.permissions: Could not find permission plugin for QCameraPermission. 
Please make sure you have included the required usage description in your Info.plist
Access to camera not granted
```

## Root Cause

Homebrew Qt6 builds permission plugins as **static libraries** (`.a` files), not dynamic plugins. They must be:
1. Explicitly linked at build time
2. Imported via `Q_IMPORT_PLUGIN` macro

## Solution

### 1. Create Info.plist with Camera Permission

Create `macos/Info.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <!-- ... other keys ... -->
    <key>NSCameraUsageDescription</key>
    <string>This app needs camera access for video capture.</string>
    <key>NSMicrophoneUsageDescription</key>
    <string>This app may need microphone access for recording.</string>
</dict>
</plist>
```

### 2. Update CMakeLists.txt

```cmake
# Find Qt permission plugins (static on Homebrew macOS)
if(APPLE)
    find_library(QT_DARWIN_CAMERA_PERMISSION_PLUGIN
        NAMES qdarwincamerapermission
        PATHS /opt/homebrew/opt/qt/share/qt/plugins/permissions
              /opt/homebrew/Cellar/qtbase/6.10.1/share/qt/plugins/permissions
        NO_DEFAULT_PATH
    )
endif()

# Link libraries (add permission plugin and AVFoundation for macOS)
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    # ... other libs ...
    $<$<PLATFORM_ID:Darwin>:${QT_DARWIN_CAMERA_PERMISSION_PLUGIN}>
    $<$<PLATFORM_ID:Darwin>:-framework\ AVFoundation>
)

# Use custom Info.plist
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/macos/Info.plist
    )
endif()
```

### 3. Import Plugin in main.cpp

```cpp
#ifdef Q_OS_DARWIN
#include <QtPlugin>
Q_IMPORT_PLUGIN(QDarwinCameraPermissionPlugin)
#endif
```

## Plugin Location

Homebrew Qt6 permission plugins are located at:
```
/opt/homebrew/opt/qt/share/qt/plugins/permissions/
├── libqdarwinbluetoothpermission.a
├── libqdarwincalendarpermission.a
├── libqdarwincamerapermission.a      <-- Camera
├── libqdarwincontactspermission.a
├── libqdarwinlocationpermission.a
└── libqdarwinmicrophonepermission.a  <-- Microphone (if needed)
```

## Verification

Build and run `test_camera` to verify the setup works:

```bash
cd build && cmake .. && make test_camera
./test_camera.app/Contents/MacOS/test_camera
```

Expected output:
```
Available cameras: 1
   0 : "MacBook Pro Camera"
Starting camera...
Camera active: true
Video native size: QSizeF(1552, 1552)
```

## Notes

- The app must be a macOS bundle (`.app`) for permissions to work
- Plain executables without Info.plist will fail permission checks
- This applies to Homebrew Qt6; other Qt distributions may handle plugins differently

