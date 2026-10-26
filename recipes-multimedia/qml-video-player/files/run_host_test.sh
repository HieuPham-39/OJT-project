#!/bin/bash
# ==============================================================================
# Linux Host Test Runner: OJT Automotive Qt5/QML Video Player
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_host"

echo "=========================================================="
echo "  OJT Automotive QML Video Player - Linux Host Testbench  "
echo "=========================================================="

# 1. Generate sample videos for local testing
"${SCRIPT_DIR}/generate_sample_videos.sh"

# 2. Check for host Qt5 development packages and QML plugins
MISSING_PKGS=()
if ! pkg-config --exists Qt5Core Qt5Gui Qt5Quick Qt5Multimedia 2>/dev/null; then
    MISSING_PKGS+=("qtbase5-dev" "qtdeclarative5-dev" "qtquickcontrols2-5-dev" "qtmultimedia5-dev")
fi

if [ ! -d "/usr/lib/x86_64-linux-gnu/qt5/qml/QtQuick/Window.2" ] && [ ! -d "/usr/lib/qt5/qml/QtQuick/Window.2" ]; then
    MISSING_PKGS+=("qml-module-qtquick-window2")
fi

if [ ! -d "/usr/lib/x86_64-linux-gnu/qt5/qml/QtQuick/Layouts" ] && [ ! -d "/usr/lib/qt5/qml/QtQuick/Layouts" ]; then
    MISSING_PKGS+=("qml-module-qtquick-layouts")
fi

if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
    echo ""
    echo "[!] Missing required host Qt5 development or QML runtime packages: ${MISSING_PKGS[*]}"
    echo "    Please run the following command to install them:"
    echo "    sudo apt-get update && sudo apt-get install -y \\"
    echo "        qtbase5-dev qtdeclarative5-dev qtquickcontrols2-5-dev qtmultimedia5-dev \\"
    echo "        qml-module-qtquick2 qml-module-qtquick-controls2 qml-module-qtquick-window2 \\"
    echo "        qml-module-qtquick-layouts qml-module-qtmultimedia cmake g++"
    echo ""
    exit 1
fi

# 3. Configure and compile using CMake
echo ">> Configuring with CMake..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake "${SCRIPT_DIR}"

echo ">> Compiling ojt-qml-player..."
make -j$(nproc)

# 4. Launch QML Player with sample videos
echo ">> Launching Automotive QML Video Player..."
./ojt-qml-player --dir "${SCRIPT_DIR}/sample_videos"
