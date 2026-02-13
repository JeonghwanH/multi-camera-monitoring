#!/bin/bash

# Multi-Camera Monitoring Application - Setup Script
# This script installs dependencies and builds the project

set -e  # Exit on error

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

# Detect OS
detect_os() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        if [ -f /etc/debian_version ]; then
            echo "debian"
        elif [ -f /etc/redhat-release ]; then
            echo "redhat"
        else
            echo "linux"
        fi
    else
        echo "unknown"
    fi
}

# Create symlinks for Homebrew's modular Qt6 structure
# Homebrew splits Qt6 into separate packages (qtbase, qtmultimedia, qtsvg, etc.)
# CMake expects all modules in one directory, so we create symlinks
setup_qt6_symlinks() {
    print_status "Setting up Qt6 module symlinks for Homebrew's modular structure..."
    
    local qtbase_cmake="/opt/homebrew/opt/qtbase/lib/cmake"
    local qtmultimedia_cmake="/opt/homebrew/opt/qtmultimedia/lib/cmake"
    
    # Check if qtbase exists
    if [[ ! -d "$qtbase_cmake" ]]; then
        print_warning "qtbase cmake directory not found at $qtbase_cmake"
        return 1
    fi
    
    # Check if qtmultimedia exists
    if [[ ! -d "$qtmultimedia_cmake" ]]; then
        print_warning "qtmultimedia cmake directory not found at $qtmultimedia_cmake"
        return 1
    fi
    
    # List of Qt6 Multimedia components to symlink
    local multimedia_components=(
        "Qt6Multimedia"
        "Qt6MultimediaWidgets"
    )
    
    for component in "${multimedia_components[@]}"; do
        local source="$qtmultimedia_cmake/$component"
        local target="$qtbase_cmake/$component"
        
        if [[ -d "$source" ]]; then
            if [[ -L "$target" ]]; then
                print_success "Symlink already exists: $component"
            elif [[ -d "$target" ]]; then
                print_success "Directory already exists: $component"
            else
                print_status "Creating symlink: $component"
                ln -sf "$source" "$target"
                print_success "Created symlink: $component -> $source"
            fi
        else
            print_warning "Source not found: $source"
        fi
    done
    
    print_success "Qt6 symlinks configured"
}

# Install dependencies for macOS
install_macos_deps() {
    print_status "Checking Homebrew..."
    if ! command -v brew &> /dev/null; then
        print_warning "Homebrew not found. Installing..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi
    print_success "Homebrew is installed"

    print_status "Installing dependencies via Homebrew..."
    
    # Homebrew Qt6 modular structure:
    # qt@6 / qt      → meta-package (depends on components)
    # qtbase         → Core, Gui, Widgets, Network, Concurrent
    # qtmultimedia   → Multimedia, MultimediaWidgets
    
    DEPS=("qt@6" "qtmultimedia" "ffmpeg" "opencv" "cmake" "pkg-config")
    for dep in "${DEPS[@]}"; do
        if brew list "$dep" &>/dev/null; then
            print_success "$dep is already installed"
        else
            print_status "Installing $dep..."
            brew install "$dep"
        fi
    done
    
    # Setup symlinks for modular Qt6
    setup_qt6_symlinks
    
    print_success "Dependencies installed"
}

# Install dependencies for Debian/Ubuntu
install_debian_deps() {
    print_status "Updating package list..."
    sudo apt update

    print_status "Installing dependencies..."
    sudo apt install -y \
        qt6-base-dev \
        qt6-multimedia-dev \
        libqt6multimedia6 \
        libavcodec-dev \
        libavformat-dev \
        libavutil-dev \
        libswscale-dev \
        libopencv-dev \
        cmake \
        build-essential \
        pkg-config

    print_success "Dependencies installed"
}

# Install dependencies for RedHat/Fedora
install_redhat_deps() {
    print_status "Installing dependencies..."
    sudo dnf install -y \
        qt6-qtbase-devel \
        qt6-qtmultimedia-devel \
        ffmpeg-devel \
        opencv-devel \
        cmake \
        gcc-c++ \
        pkg-config

    print_success "Dependencies installed"
}

# Build the project
build_project() {
    print_status "Creating build directory..."
    rm -rf build
    mkdir -p build
    cd build

    print_status "Running CMake..."
    
    if [[ "$OS" == "macos" ]]; then
        # Use Homebrew's qt@6 path
        # After symlinks are set up, all Qt6 modules are accessible from qtbase
        QT6_DIR="/opt/homebrew/opt/qt@6/lib/cmake/Qt6"
        QT_PREFIX="/opt/homebrew/opt/qt@6"
        
        # Fallback: check qtbase if qt@6 doesn't exist
        if [[ ! -d "$QT6_DIR" ]]; then
            QT6_DIR="/opt/homebrew/opt/qtbase/lib/cmake/Qt6"
            QT_PREFIX="/opt/homebrew/opt/qtbase"
        fi
        
        if [[ ! -d "$QT6_DIR" ]]; then
            print_error "Qt6 CMake directory not found!"
            echo ""
            echo "Please ensure Qt6 is installed:"
            echo "  brew install qt@6 qtmultimedia"
            exit 1
        fi
        
        print_status "Qt6_DIR: $QT6_DIR"
        print_status "CMAKE_PREFIX_PATH: $QT_PREFIX"
        
        # Set PKG_CONFIG_PATH for FFmpeg
        export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/opt/homebrew/opt/ffmpeg/lib/pkgconfig:$PKG_CONFIG_PATH"
        print_status "PKG_CONFIG_PATH: $PKG_CONFIG_PATH"
        
        # Run CMake with Qt6 paths
        cmake .. \
            -DCMAKE_PREFIX_PATH="$QT_PREFIX;/opt/homebrew" \
            -DQt6_DIR="$QT6_DIR" \
            -DCMAKE_BUILD_TYPE=Release
    else
        cmake .. -DCMAKE_BUILD_TYPE=Release
    fi

    print_status "Building project..."
    NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    make -j"$NPROC"

    cd ..
    print_success "Build complete!"
}

# Main script
main() {
    echo ""
    echo "========================================"
    echo "  Multi-Camera Monitor - Setup Script"
    echo "========================================"
    echo ""

    # Get script directory
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    cd "$SCRIPT_DIR"

    # Detect OS
    OS=$(detect_os)
    print_status "Detected OS: $OS"

    # Install dependencies based on OS
    case $OS in
        macos)
            install_macos_deps
            ;;
        debian)
            install_debian_deps
            ;;
        redhat)
            install_redhat_deps
            ;;
        linux)
            print_warning "Unknown Linux distribution. Please install dependencies manually:"
            echo "  - Qt 6.5+ (with Multimedia module)"
            echo "  - FFmpeg development libraries"
            echo "  - OpenCV development libraries"
            echo "  - CMake 3.16+"
            echo "  - pkg-config"
            read -p "Press Enter to continue with build, or Ctrl+C to abort..."
            ;;
        *)
            print_error "Unsupported operating system: $OSTYPE"
            exit 1
            ;;
    esac

    echo ""

    # Build the project
    build_project

    echo ""
    echo "========================================"
    print_success "Setup complete!"
    echo "========================================"
    echo ""
    echo "To run the application:"
    echo "  ./run.sh"
    echo ""
    echo "Or manually:"
    echo "  ./build/multi-camera-monitor"
    echo ""
}

main "$@"
