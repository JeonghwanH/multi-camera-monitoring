#!/bin/bash

# Multi-Camera Monitoring Application - Debug Run Script
# Shows buffer size overlay on each slot

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Set debug mode environment variable
export MCM_DEBUG=1

# Find executable
EXECUTABLE=""
if [[ -f "build/multi-camera-monitor" ]]; then
    EXECUTABLE="build/multi-camera-monitor"
elif [[ -f "build/multi-camera-monitor.app/Contents/MacOS/multi-camera-monitor" ]]; then
    EXECUTABLE="build/multi-camera-monitor.app/Contents/MacOS/multi-camera-monitor"
fi

if [[ -z "$EXECUTABLE" ]]; then
    echo "Executable not found. Run ./setup.sh first."
    exit 1
fi

echo "========================================"
echo "  Multi-Camera Monitor - DEBUG MODE"
echo "========================================"
echo "Buffer size overlay: ENABLED"
echo ""

# Run from project root so config.json is found
exec "$EXECUTABLE" "$@"

