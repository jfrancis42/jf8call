#!/usr/bin/env bash
# package-appimage.sh — Build a Linux AppImage for JF8Call
#
# Prerequisites:
#   cmake ninja-build (build deps)
#   linuxdeploy + linuxdeploy-plugin-qt in /usr/local/bin/ (or current dir)
#   Build the app first: ./configure && make
#
# Output: JF8Call-<version>-x86_64.AppImage

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
VERSION="$(cat "$SCRIPT_DIR/VERSION" | tr -d '[:space:]')"
OUTPUT="$SCRIPT_DIR/JF8Call-${VERSION}-x86_64.AppImage"

# Build first if needed
if [[ ! -f "$BUILD_DIR/jf8call" ]]; then
    echo "Binary not found — running configure + make first..."
    cd "$SCRIPT_DIR"
    ./configure
    make
fi

# Download linuxdeploy if missing
for tool in linuxdeploy linuxdeploy-plugin-qt; do
    if ! command -v "$tool" >/dev/null 2>&1 && [[ ! -f "/usr/local/bin/$tool" ]]; then
        echo "=== Downloading $tool ==="
        URL="https://github.com/linuxdeploy/${tool}/releases/download/continuous/${tool}-x86_64.AppImage"
        sudo wget -q "$URL" -O "/usr/local/bin/$tool"
        sudo chmod +x "/usr/local/bin/$tool"
    fi
done

# Create AppDir
APPDIR="$SCRIPT_DIR/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$BUILD_DIR/jf8call" "$APPDIR/usr/bin/"
cp "$SCRIPT_DIR/jf8call.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/jf8call.png"

cat > "$APPDIR/usr/share/applications/jf8call.desktop" << 'EOF'
[Desktop Entry]
Name=JF8Call
Exec=jf8call
Icon=jf8call
Type=Application
Categories=HamRadio;Network;
Comment=JS8Call-compatible HF digital mode application
EOF

export QMAKE="$(which qmake6 || which qmake)"
export OUTPUT="$OUTPUT"

linuxdeploy \
    --appdir "$APPDIR" \
    --plugin qt \
    --output appimage

echo "=== Done: $OUTPUT ==="
