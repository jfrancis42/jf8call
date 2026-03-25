#!/bin/sh
# autogen.sh — check for required tools before configure
set -e

echo "JF8Call build setup"
echo "==================="

# Check for cmake
if ! command -v cmake >/dev/null 2>&1; then
    echo "ERROR: cmake not found. Install with: sudo apt install cmake"
    exit 1
fi
echo "  cmake:       $(cmake --version | head -1)"

# Check for Qt6
if ! pkg-config --exists Qt6Core 2>/dev/null; then
    echo "WARNING: Qt6 not found via pkg-config (may still work if Qt6 is installed)"
fi

# Check for pkg-config
if ! command -v pkg-config >/dev/null 2>&1; then
    echo "WARNING: pkg-config not found — dependency detection may fail"
fi

# Check for portaudio
if pkg-config --exists portaudio-2.0 2>/dev/null; then
    echo "  portaudio:   $(pkg-config --modversion portaudio-2.0)"
else
    echo "WARNING: portaudio-2.0 not found. Install: sudo apt install libportaudio-dev"
fi

# Check for hamlib
if pkg-config --exists hamlib 2>/dev/null; then
    echo "  hamlib:      $(pkg-config --modversion hamlib)"
else
    echo "  hamlib:      not found (radio control will be disabled)"
fi

# Check for gfsk8-modem-clean
GFSK8MODEM_DIR="${GFSK8MODEM_DIR:-$HOME/gfsk8-modem-clean}"
if [ -f "$GFSK8MODEM_DIR/include/gfsk8modem.h" ]; then
    echo "  gfsk8-modem-clean: found at $GFSK8MODEM_DIR"
else
    echo "ERROR: gfsk8-modem-clean not found at $GFSK8MODEM_DIR"
    echo "       Set GFSK8MODEM_DIR environment variable or build gfsk8-modem-clean first:"
    echo "       cd ~/gfsk8-modem-clean && cmake -B build && cmake --build build -- -j2"
    exit 1
fi

echo ""
echo "Run ./configure to set build options, then make to build."
