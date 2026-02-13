#!/bin/bash

# Multi-Camera Monitoring Application - Run Script
# This script runs the built application

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${BLUE}[*]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Executable paths to check
EXECUTABLE=""
if [[ -f "build/multi-camera-monitor" ]]; then
    EXECUTABLE="build/multi-camera-monitor"
elif [[ -f "build/multi-camera-monitor.app/Contents/MacOS/multi-camera-monitor" ]]; then
    EXECUTABLE="build/multi-camera-monitor.app/Contents/MacOS/multi-camera-monitor"
elif [[ -f "build/Release/multi-camera-monitor" ]]; then
    EXECUTABLE="build/Release/multi-camera-monitor"
elif [[ -f "build/Release/multi-camera-monitor.exe" ]]; then
    EXECUTABLE="build/Release/multi-camera-monitor.exe"
fi

# Check if executable exists
if [[ -z "$EXECUTABLE" ]]; then
    print_error "Executable not found!"
    echo ""
    echo "Please run setup.sh first to build the project:"
    echo "  ./setup.sh"
    echo ""
    exit 1
fi

# Ensure config.json exists in build directory
if [[ ! -f "build/config.json" ]]; then
    if [[ -f "config.json" ]]; then
        print_status "Copying config.json to build directory..."
        cp config.json build/
    fi
fi

# Ensure recordings directory exists
mkdir -p recordings

# Set library paths for macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    # Add Homebrew library paths
    if command -v brew &> /dev/null; then
        QT_PATH=$(brew --prefix qt@6 2>/dev/null || echo "")
        FFMPEG_PATH=$(brew --prefix ffmpeg 2>/dev/null || echo "")
        OPENCV_PATH=$(brew --prefix opencv 2>/dev/null || echo "")
        
        if [[ -n "$QT_PATH" ]]; then
            export DYLD_FRAMEWORK_PATH="$QT_PATH/lib:$DYLD_FRAMEWORK_PATH"
        fi
        if [[ -n "$FFMPEG_PATH" ]]; then
            export DYLD_LIBRARY_PATH="$FFMPEG_PATH/lib:$DYLD_LIBRARY_PATH"
        fi
        if [[ -n "$OPENCV_PATH" ]]; then
            export DYLD_LIBRARY_PATH="$OPENCV_PATH/lib:$DYLD_LIBRARY_PATH"
        fi
    fi
fi

# Set library paths for Linux
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    export LD_LIBRARY_PATH="/usr/local/lib:$LD_LIBRARY_PATH"
fi

echo ""
echo "========================================"
echo "  Multi-Camera Monitor"
echo "========================================"
echo ""
print_status "Starting application..."
print_status "Executable: $EXECUTABLE"
echo ""

# Run the application
cd build
exec "../$EXECUTABLE" "$@"

