#!/usr/bin/env bash
# build-windows-cross.sh — Cross-compile JF8Call for Windows 64-bit on Linux
#
# Prerequisites (Ubuntu/Debian):
#   sudo apt-get install mingw-w64 cmake ninja-build python3-pip
#   pip install aqtinstall
#   ./build-windows-cross.sh --setup   (one-time: downloads Qt6 + Hamlib)
#   ./build-windows-cross.sh           (builds jf8call.exe)
#
# PortAudio Windows: also set up by --setup (downloads prebuilt MinGW DLL).
#
# Output: build-windows/deploy/jf8call.exe + DLLs

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CROSS_DIR="$HOME/Qt-cross"
HAMLIB_WIN_DIR="$CROSS_DIR/hamlib-windows"
PORTAUDIO_WIN_DIR="$CROSS_DIR/portaudio-windows"
CODEC2_SRC_DIR="$CROSS_DIR/codec2-src"
CODEC2_WIN_BUILD="$CROSS_DIR/codec2-win-build"
CODEC2_WIN_DIR="$CROSS_DIR/codec2-windows"
GFSK8MODEM_DIR="${GFSK8MODEM_DIR:-$HOME/gfsk8-modem-clean}"
OLIVIA_MODEM_DIR="${OLIVIA_MODEM_DIR:-$HOME/olivia-modem}"
PSK_MODEM_DIR="${PSK_MODEM_DIR:-$HOME/psk31}"
# Qt version for Windows target must match the host (system) Qt version so that
# QT_HOST_PATH cross-compilation tool discovery works (same moc/rcc version).
QT_VERSION="6.9.2"
QT_WIN_DIR="$CROSS_DIR/windows/${QT_VERSION}/mingw_64"
HAMLIB_VERSION="4.5.5"
BUILD_DIR="$SCRIPT_DIR/build-windows"

if [[ "${1:-}" == "--setup" ]]; then
    mkdir -p "$CROSS_DIR"
    pip install aqtinstall --break-system-packages --quiet

    if [[ ! -f "$QT_WIN_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
        echo "=== Installing Qt6 $QT_VERSION for Windows (MinGW 64-bit) ==="
        aqt install-qt --outputdir "$CROSS_DIR/windows" \
            windows desktop "$QT_VERSION" win64_mingw \
            --modules qtwebsockets
    else
        echo "=== Qt6 $QT_VERSION already installed, skipping ==="
    fi

    if [[ ! -d "$HAMLIB_WIN_DIR" ]]; then
        echo "=== Downloading Hamlib $HAMLIB_VERSION for Windows ==="
        cd "$CROSS_DIR"
        wget -q "https://github.com/Hamlib/Hamlib/releases/download/${HAMLIB_VERSION}/hamlib-w64-${HAMLIB_VERSION}.zip" \
            -O hamlib-windows.zip
        unzip -q hamlib-windows.zip -d hamlib-windows-tmp
        mv "hamlib-windows-tmp/hamlib-w64-${HAMLIB_VERSION}" hamlib-windows
        rm -rf hamlib-windows-tmp hamlib-windows.zip
    else
        echo "=== Hamlib already installed, skipping ==="
    fi

    if [[ ! -f "$PORTAUDIO_WIN_DIR/include/portaudio.h" ]]; then
        echo "=== Building PortAudio for Windows (from source) ==="
        cd "$CROSS_DIR"
        if [[ ! -d portaudio-src ]]; then
            wget -q "https://github.com/PortAudio/portaudio/archive/refs/tags/v19.7.0.tar.gz" \
                -O portaudio.tar.gz
            tar xf portaudio.tar.gz
            mv portaudio-19.7.0 portaudio-src
            rm portaudio.tar.gz
        fi
        mkdir -p "$PORTAUDIO_WIN_DIR"
        cmake -B portaudio-src/build-windows -S portaudio-src \
            -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchain-windows-mingw64.cmake" \
            -DCMAKE_INSTALL_PREFIX="$PORTAUDIO_WIN_DIR" \
            -DCMAKE_BUILD_TYPE=Release \
            -DPA_BUILD_SHARED_LIBS=ON \
            -DPA_USE_ASIO=OFF
        cmake --build portaudio-src/build-windows
        cmake --install portaudio-src/build-windows
    else
        echo "=== PortAudio already built, skipping ==="
    fi

    if [[ ! -f "$CODEC2_WIN_DIR/lib/libcodec2.a" ]]; then
        echo "=== Building Codec2 for Windows ==="
        if [[ ! -d "$CODEC2_SRC_DIR" ]]; then
            curl -fsSL "https://github.com/drowe67/codec2/archive/refs/tags/1.2.0.tar.gz" \
                -o /tmp/codec2.tar.gz
            tar xf /tmp/codec2.tar.gz -C "$CROSS_DIR"
            mv "$CROSS_DIR/codec2-1.2.0" "$CODEC2_SRC_DIR"
            rm /tmp/codec2.tar.gz
        fi
        cmake -B "$CODEC2_WIN_BUILD" -S "$CODEC2_SRC_DIR" \
            -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchain-windows-mingw64.cmake" \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DUNITTEST=OFF \
            -DINSTALL_EXAMPLES=OFF
        cmake --build "$CODEC2_WIN_BUILD" --target codec2
        mkdir -p "$CODEC2_WIN_DIR/lib" "$CODEC2_WIN_DIR/include/codec2"
        cp "$CODEC2_WIN_BUILD/src/libcodec2.a" "$CODEC2_WIN_DIR/lib/"
        cp "$CODEC2_SRC_DIR/src/codec2.h" \
           "$CODEC2_SRC_DIR/src/freedv_api.h" \
           "$CODEC2_SRC_DIR/src/modem_stats.h" \
           "$CODEC2_SRC_DIR/src/codec2_fdmdv.h" \
           "$CODEC2_SRC_DIR/src/codec2_cohpsk.h" \
           "$CODEC2_SRC_DIR/src/reliable_text.h" \
           "$CODEC2_SRC_DIR/src/comp.h" \
           "$CODEC2_WIN_DIR/include/codec2/"
    else
        echo "=== Codec2 already built, skipping ==="
    fi

    echo "=== Setup complete. ==="
    exit 0
fi

if [[ ! -f "$QT_WIN_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    echo "ERROR: Qt6 Windows not found. Run: $0 --setup"; exit 1
fi

HAMLIB_ARG=""
[[ -d "$HAMLIB_WIN_DIR" ]] && HAMLIB_ARG="-DHAMLIB_WINDOWS_DIR=$HAMLIB_WIN_DIR"

PORTAUDIO_ARG=""
if [[ -d "$PORTAUDIO_WIN_DIR" ]]; then
    # cmake-built PortAudio installs the import lib as libportaudio.dll.a
    _pa_lib="$PORTAUDIO_WIN_DIR/lib/libportaudio.dll.a"
    [[ ! -f "$_pa_lib" ]] && _pa_lib="$PORTAUDIO_WIN_DIR/lib/libportaudio.a"
    PORTAUDIO_ARG="-DPORTAUDIO_LIBRARIES=$_pa_lib \
                   -DPORTAUDIO_INCLUDE_DIRS=$PORTAUDIO_WIN_DIR/include \
                   -DPORTAUDIO_FOUND=TRUE"
fi

# Build gfsk8modem for Windows first
GFSK8MODEM_WIN_BUILD="$GFSK8MODEM_DIR/build-windows"
echo "=== Building gfsk8modem for Windows ==="
cmake -B "$GFSK8MODEM_WIN_BUILD" -S "$GFSK8MODEM_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchain-windows-mingw64.cmake" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$GFSK8MODEM_WIN_BUILD"
GFSK8MODEM_WIN_LIB="$GFSK8MODEM_WIN_BUILD/libgfsk8modem.a"

# Build olivia-modem for Windows
OLIVIA_WIN_BUILD="$OLIVIA_MODEM_DIR/build-windows"
echo "=== Building olivia-modem for Windows ==="
cmake -B "$OLIVIA_WIN_BUILD" -S "$OLIVIA_MODEM_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchain-windows-mingw64.cmake" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$OLIVIA_WIN_BUILD"

# Build Codec2 for Windows if not already done
if [[ ! -f "$CODEC2_WIN_DIR/lib/libcodec2.a" ]]; then
    echo "=== Building Codec2 for Windows ==="
    if [[ ! -d "$CODEC2_SRC_DIR" ]]; then
        curl -fsSL "https://github.com/drowe67/codec2/archive/refs/tags/1.2.0.tar.gz" \
            -o /tmp/codec2.tar.gz
        tar xf /tmp/codec2.tar.gz -C "$CROSS_DIR"
        mv "$CROSS_DIR/codec2-1.2.0" "$CODEC2_SRC_DIR"
        rm /tmp/codec2.tar.gz
    fi
    cmake -B "$CODEC2_WIN_BUILD" -S "$CODEC2_SRC_DIR" \
        -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchain-windows-mingw64.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DUNITTEST=OFF \
        -DINSTALL_EXAMPLES=OFF
    cmake --build "$CODEC2_WIN_BUILD" --target codec2
    mkdir -p "$CODEC2_WIN_DIR/lib" "$CODEC2_WIN_DIR/include/codec2"
    cp "$CODEC2_WIN_BUILD/src/libcodec2.a" "$CODEC2_WIN_DIR/lib/"
    cp "$CODEC2_SRC_DIR/src/codec2.h" \
       "$CODEC2_SRC_DIR/src/freedv_api.h" \
       "$CODEC2_SRC_DIR/src/modem_stats.h" \
       "$CODEC2_SRC_DIR/src/codec2_fdmdv.h" \
       "$CODEC2_SRC_DIR/src/codec2_cohpsk.h" \
       "$CODEC2_SRC_DIR/src/reliable_text.h" \
       "$CODEC2_WIN_DIR/include/codec2/"
fi

# Build libpsk for Windows (library only — apps use POSIX headers unavailable on Windows)
PSK_WIN_BUILD="$PSK_MODEM_DIR/build-windows"
echo "=== Building libpsk for Windows ==="
cmake -B "$PSK_WIN_BUILD" -S "$PSK_MODEM_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchain-windows-mingw64.cmake" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$PSK_WIN_BUILD" --target psk

echo "=== Configuring JF8Call for Windows ==="
cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchain-windows-mingw64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DGFSK8MODEM_DIR="$GFSK8MODEM_DIR" \
    -DGFSK8MODEM_BUILD_DIR="$GFSK8MODEM_WIN_BUILD" \
    -DOLIVIA_MODEM_DIR="$OLIVIA_MODEM_DIR" \
    -DOLIVIA_MODEM_BUILD_DIR="$OLIVIA_WIN_BUILD" \
    -DPSK_MODEM_DIR="$PSK_MODEM_DIR" \
    -DPSK_MODEM_BUILD_DIR="$PSK_WIN_BUILD" \
    -DQT_WINDOWS_DIR="$QT_WIN_DIR" \
    -DQT_HOST_PATH=/usr \
    -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake \
    -DCODEC2_DIR="$CODEC2_WIN_DIR" \
    $HAMLIB_ARG \
    $PORTAUDIO_ARG

echo "=== Building ==="
cmake --build "$BUILD_DIR"

echo "=== Deploying DLLs ==="
DEPLOY_DIR="$BUILD_DIR/deploy"
mkdir -p "$DEPLOY_DIR"
cp "$BUILD_DIR/jf8call.exe" "$DEPLOY_DIR/"

# Copy Qt DLLs
QT_BIN="$QT_WIN_DIR/bin"
for dll in Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6WebSockets; do
    cp "$QT_BIN/$dll.dll" "$DEPLOY_DIR/" 2>/dev/null || true
done
mkdir -p "$DEPLOY_DIR/platforms"
cp "$QT_WIN_DIR/plugins/platforms/qwindows.dll" "$DEPLOY_DIR/platforms/" 2>/dev/null || true

# Copy runtime DLLs
MINGW_BIN="/usr/x86_64-w64-mingw32/bin"
for dll in libstdc++-6 libwinpthread-1 libgcc_s_seh-1; do
    find /usr -name "$dll.dll" 2>/dev/null | head -1 | xargs -I{} cp {} "$DEPLOY_DIR/" 2>/dev/null || true
done

[[ -d "$HAMLIB_WIN_DIR" ]] && cp "$HAMLIB_WIN_DIR"/bin/*.dll "$DEPLOY_DIR/" 2>/dev/null || true
if [[ -d "$PORTAUDIO_WIN_DIR/bin" ]]; then
    find "$PORTAUDIO_WIN_DIR/bin" -name "libportaudio*.dll" \
        -exec cp {} "$DEPLOY_DIR/" \; 2>/dev/null || true
fi

echo "=== Done: $DEPLOY_DIR ==="
ls -lh "$DEPLOY_DIR/"
