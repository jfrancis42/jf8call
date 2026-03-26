#!/usr/bin/env bash
# build-raspberrypi.sh — Cross-compile JF8Call for Raspberry Pi (aarch64)
#
# Prerequisites (Ubuntu/Debian x86_64 host):
#   ./build-raspberrypi.sh --setup   (one-time: installs arm64 packages)
#   ./build-raspberrypi.sh           (builds aarch64 binary)
#
# Output: build-rpi/jf8call (ELF aarch64)
# Deploy to Pi:
#   scp build-rpi/jf8call pi@raspberrypi:~
#   # On Pi: sudo apt-get install libqt6widgets6 libqt6network6 libhamlib4 libportaudio2

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GFSK8MODEM_DIR="${GFSK8MODEM_DIR:-$HOME/gfsk8-modem-clean}"
OLIVIA_MODEM_DIR="${OLIVIA_MODEM_DIR:-$HOME/olivia-modem}"
PSK_MODEM_DIR="${PSK_MODEM_DIR:-$HOME/psk31}"
BUILD_DIR="$SCRIPT_DIR/build-rpi"

if [[ "${1:-}" == "--setup" ]]; then
    echo "=== Setting up arm64 cross-compile environment ==="
    sudo dpkg --add-architecture arm64

    # Add ports.ubuntu.com for arm64 packages.
    # On Linux Mint, lsb_release -cs returns the Mint codename (e.g. "zena"),
    # not the Ubuntu base codename (e.g. "noble"). Use upstream-release if available.
    if [[ -f /etc/upstream-release/lsb-release ]]; then
        CODENAME=$(. /etc/upstream-release/lsb-release && echo "$DISTRIB_CODENAME")
    else
        CODENAME=$(lsb_release -cs 2>/dev/null || echo noble)
    fi
    if ! grep -qr "ports.ubuntu.com" /etc/apt/sources.list /etc/apt/sources.list.d/ 2>/dev/null; then
        echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports $CODENAME main restricted universe multiverse" \
            | sudo tee /etc/apt/sources.list.d/ubuntu-arm64-ports.list
        echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports ${CODENAME}-updates main restricted universe multiverse" \
            | sudo tee -a /etc/apt/sources.list.d/ubuntu-arm64-ports.list
    fi

    # archive.ubuntu.com does NOT serve arm64 packages; those live on ports.ubuntu.com.
    # Add an apt config snippet that restricts archive.ubuntu.com and security.ubuntu.com
    # to amd64 only, so apt stops generating 404 errors when fetching arm64 from them.
    cat <<'EOF' | sudo tee /etc/apt/sources.list.d/ubuntu-amd64-arch.conf > /dev/null
APT::Sources::With-Push "false";
EOF
    # Pin archive.ubuntu.com to amd64 only via preferences
    cat <<'EOF' | sudo tee /etc/apt/preferences.d/ubuntu-amd64-only > /dev/null
Package: *
Pin: release o=Ubuntu
Pin-Priority: 500
Explanation: Limit archive.ubuntu.com to amd64
EOF
    # Create an amd64-restricted override for the Ubuntu sources
    if [[ ! -f /etc/apt/sources.list.d/ubuntu-noble-amd64.list ]]; then
        cat <<'EOF' | sudo tee /etc/apt/sources.list.d/ubuntu-noble-amd64.list
deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble main restricted universe multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-updates main restricted universe multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-backports main restricted universe multiverse
deb [arch=amd64] http://security.ubuntu.com/ubuntu noble-security main restricted universe multiverse
EOF
        # Remove the architecture-unrestricted Ubuntu sources from Mint's list so we
        # don't have duplicates (but keep Mint's own packages.linuxmint.com entry).
        sudo sed -i 's|^deb http://archive.ubuntu.com|#deb http://archive.ubuntu.com|g' \
            /etc/apt/sources.list.d/official-package-repositories.list 2>/dev/null || true
        sudo sed -i 's|^deb http://security.ubuntu.com|#deb http://security.ubuntu.com|g' \
            /etc/apt/sources.list.d/official-package-repositories.list 2>/dev/null || true
    fi

    sudo apt-get update
    sudo apt-get install -y \
        gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
        cmake ninja-build \
        qt6-base-dev:arm64 \
        libqt6websockets6-dev:arm64 \
        libhamlib-dev:arm64 \
        portaudio19-dev:arm64
    echo "=== Setup complete. ==="
    exit 0
fi

# Build gfsk8modem for aarch64 first
GFSK8MODEM_RPI_BUILD="$GFSK8MODEM_DIR/build-rpi"
echo "=== Building gfsk8modem for aarch64 ==="
cmake -B "$GFSK8MODEM_RPI_BUILD" -S "$GFSK8MODEM_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchain-raspberrypi-aarch64.cmake" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$GFSK8MODEM_RPI_BUILD"

# Build olivia-modem for aarch64
OLIVIA_RPI_BUILD="$OLIVIA_MODEM_DIR/build-rpi"
echo "=== Building olivia-modem for aarch64 ==="
cmake -B "$OLIVIA_RPI_BUILD" -S "$OLIVIA_MODEM_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchain-raspberrypi-aarch64.cmake" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$OLIVIA_RPI_BUILD"

# Build libpsk for aarch64 (library only)
PSK_RPI_BUILD="$PSK_MODEM_DIR/build-rpi"
echo "=== Building libpsk for aarch64 ==="
cmake -B "$PSK_RPI_BUILD" -S "$PSK_MODEM_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchain-raspberrypi-aarch64.cmake" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$PSK_RPI_BUILD" --target psk

echo "=== Configuring JF8Call for aarch64 ==="
cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/toolchain-raspberrypi-aarch64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DGFSK8MODEM_DIR="$GFSK8MODEM_DIR" \
    -DGFSK8MODEM_BUILD_DIR="$GFSK8MODEM_RPI_BUILD" \
    -DOLIVIA_MODEM_DIR="$OLIVIA_MODEM_DIR" \
    -DOLIVIA_MODEM_BUILD_DIR="$OLIVIA_RPI_BUILD" \
    -DPSK_MODEM_DIR="$PSK_MODEM_DIR" \
    -DPSK_MODEM_BUILD_DIR="$PSK_RPI_BUILD"

echo "=== Building ==="
cmake --build "$BUILD_DIR"

VERSION="$(cat "$SCRIPT_DIR/VERSION" | tr -d '[:space:]')"
TOOL_OUTPUT="$SCRIPT_DIR/jf8-tool-${VERSION}-aarch64"
cp "$BUILD_DIR/jf8-tool" "$TOOL_OUTPUT"

echo "=== Done ==="
ls -lh "$BUILD_DIR/jf8call" "$BUILD_DIR/jf8-tool"
echo ""
echo "Artifacts:"
echo "  $SCRIPT_DIR/JF8Call-${VERSION}-aarch64  (main application)"
echo "  $TOOL_OUTPUT  (API tool)"
echo ""
echo "Deploy to Raspberry Pi:"
echo "  scp $BUILD_DIR/jf8call pi@raspberrypi:~"
echo "  scp $TOOL_OUTPUT pi@raspberrypi:~/jf8-tool"
echo "  ssh pi@raspberrypi 'sudo apt-get install -y libqt6widgets6 libhamlib4 libportaudio2 libkissfft-float131'"
