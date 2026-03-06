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

# Install Qt 6.7+ using aqtinstall (for VA-API hardware encoding support)
# Ubuntu's apt only provides Qt 6.4 which lacks QT_FFMPEG_ENCODING_HW_DEVICE_TYPES
# NOTE: This function is ONLY for Debian/Ubuntu Linux
install_qt_aqt() {
    local QT_INSTALL_DIR="$HOME/Qt"
    local VENV_DIR="$HOME/.aqt_venv"
    # Try these versions in order (6.7+ required for VA-API encoding)
    local QT_VERSIONS=("6.8.0" "6.7.3" "6.7.2" "6.7.1" "6.7.0")
    local QT_VERSION=""
    local qt_path=""
    
    # Check if any Qt 6.7+ is already installed
    for ver in "${QT_VERSIONS[@]}"; do
        if [[ -d "$QT_INSTALL_DIR/$ver/gcc_64" ]]; then
            print_success "Qt $ver already installed at $QT_INSTALL_DIR/$ver/gcc_64"
            return 0
        fi
    done
    
    print_status "Installing Qt 6.7+ using aqtinstall..."
    print_status "This is required for VA-API hardware encoding support"
    
    # Install python3-venv if not available
    if ! python3 -m venv --help &> /dev/null; then
        print_status "Installing python3-venv..."
        sudo apt install -y python3-venv
    fi
    
    # Create virtual environment for aqtinstall
    if [[ ! -d "$VENV_DIR" ]]; then
        print_status "Creating Python virtual environment at $VENV_DIR..."
        python3 -m venv "$VENV_DIR"
    fi
    
    # Activate venv and install aqtinstall
    print_status "Installing aqtinstall in virtual environment..."
    source "$VENV_DIR/bin/activate"
    pip install --upgrade pip
    pip install aqtinstall
    
    # List available Qt versions (for debugging)
    print_status "Available Qt versions:"
    aqt list-qt linux desktop 2>/dev/null | grep "6\.[789]" | head -10 || echo "  (could not list versions)"
    
    # Try each version until one succeeds
    local install_success=false
    for ver in "${QT_VERSIONS[@]}"; do
        print_status "Trying Qt $ver..."
        
        if aqt install-qt linux desktop "$ver" gcc_64 -O "$QT_INSTALL_DIR" 2>&1; then
            QT_VERSION="$ver"
            qt_path="$QT_INSTALL_DIR/$ver/gcc_64"
            install_success=true
            print_success "Qt $ver installed successfully!"
            break
        else
            print_warning "Qt $ver not available, trying next version..."
        fi
    done
    
    # Deactivate venv
    deactivate
    
    if [[ "$install_success" == "true" ]] && [[ -d "$qt_path" ]]; then
        print_success "Qt $QT_VERSION installed at $qt_path"
        
        # Create environment setup script
        cat > "$SCRIPT_DIR/qt_env.sh" << EOF
# Qt $QT_VERSION environment setup (auto-generated by setup.sh)
export Qt6_DIR="$qt_path"
export CMAKE_PREFIX_PATH="$qt_path:\$CMAKE_PREFIX_PATH"
export PATH="$qt_path/bin:\$PATH"
export LD_LIBRARY_PATH="$qt_path/lib:\$LD_LIBRARY_PATH"
export QT_MEDIA_BACKEND=ffmpeg
export QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=vaapi
EOF
        print_success "Created qt_env.sh for environment setup"
    else
        print_error "Qt installation failed"
        return 1
    fi
}

# Install dependencies for Debian/Ubuntu
install_debian_deps() {
    print_status "Updating package list..."
    sudo apt update

    print_status "Installing build dependencies..."
    sudo apt install -y \
        libavcodec-dev \
        libavformat-dev \
        libavutil-dev \
        libswscale-dev \
        libopencv-dev \
        cmake \
        build-essential \
        pkg-config \
        python3-pip \
        libgl1-mesa-dev \
        libxkbcommon-dev \
        libxcb-cursor0

    print_success "Build dependencies installed"
    
    # Install Intel VA-API drivers for hardware video encoding
    print_status "Installing Intel VA-API drivers for hardware encoding..."
    sudo apt install -y \
        intel-media-va-driver-non-free \
        vainfo \
        || print_warning "VA-API drivers not available (optional - software encoding will be used)"
    
    # Check VA-API support
    if command -v vainfo &> /dev/null; then
        print_status "Checking VA-API support..."
        if vainfo 2>&1 | grep -q "VAEntrypointEncSlice"; then
            print_success "VA-API hardware encoding is supported"
        else
            print_warning "VA-API detected but encoding may not be supported"
        fi
    fi
    
    # Install Qt 6.7+ using aqtinstall (required for VA-API encoding)
    install_qt_aqt
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
            
    elif [[ "$OS" == "debian" ]]; then
        # Use aqtinstall Qt 6.7+ for VA-API encoding support (Ubuntu only)
        # Search for any installed Qt 6.7+ version
        local qt_path=""
        local qt_version=""
        for ver in 6.8.0 6.7.3 6.7.2 6.7.1 6.7.0; do
            if [[ -d "$HOME/Qt/$ver/gcc_64" ]]; then
                qt_path="$HOME/Qt/$ver/gcc_64"
                qt_version="$ver"
                break
            fi
        done
        
        if [[ -n "$qt_path" ]]; then
            print_status "Using Qt $qt_version from $qt_path"
            print_status "VA-API hardware encoding will be available"
            
            cmake .. \
                -DCMAKE_PREFIX_PATH="$qt_path" \
                -DCMAKE_BUILD_TYPE=Release
        else
            print_warning "Qt 6.7+ not found, using system Qt (VA-API encoding may not work)"
            cmake .. -DCMAKE_BUILD_TYPE=Release
        fi
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
    
    if [[ "$OS" == "debian" ]] && [[ -f "$SCRIPT_DIR/qt_env.sh" ]]; then
        echo "Qt 6.7+ installed with VA-API encoding support!"
        echo ""
        echo "To run the application:"
        echo "  source qt_env.sh && ./run.sh"
        echo ""
        echo "Or add to your shell profile:"
        echo "  echo 'source $SCRIPT_DIR/qt_env.sh' >> ~/.bashrc"
        echo ""
    else
        echo "To run the application:"
        echo "  ./run.sh"
        echo ""
    fi
    
    echo "Or manually:"
    echo "  ./build/multi-camera-monitor"
    echo ""
}

main "$@"
