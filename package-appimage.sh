#!/usr/bin/env bash
# package-appimage.sh — Build Linux AppImages for JF8Call and jf8-tool
#
# Prerequisites:
#   cmake ninja-build (build deps)
#   linuxdeploy + linuxdeploy-plugin-qt in /usr/local/bin/ (or current dir)
#   Build the app first: ./configure && make
#
# Output:
#   JF8Call-<version>-x86_64.AppImage
#   jf8-tool-<version>-x86_64.AppImage

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
VERSION="$(cat "$SCRIPT_DIR/VERSION" | tr -d '[:space:]')"
OUTPUT_JF8CALL="$SCRIPT_DIR/JF8Call-${VERSION}-x86_64.AppImage"
OUTPUT_TOOL="$SCRIPT_DIR/jf8-tool-${VERSION}-x86_64.AppImage"

# Build first if needed
if [[ ! -f "$BUILD_DIR/jf8call" ]] || [[ ! -f "$BUILD_DIR/jf8-tool" ]]; then
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

export QMAKE="$(which qmake6 || which qmake)"

# ── JF8Call AppImage ──────────────────────────────────────────────────────────

echo "=== Building JF8Call AppImage ==="
APPDIR="$SCRIPT_DIR/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$BUILD_DIR/jf8call" "$APPDIR/usr/bin/"
cp "$SCRIPT_DIR/jf8call.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/jf8call.png"

# Pre-seed the offscreen platform plugin so linuxdeploy resolves its dependencies.
# QT_QPA_PLATFORM=offscreen is set by main.cpp for --headless and --text modes.
QT6_PLATFORMS="$(dirname "$(find /usr -name 'libqxcb.so' -path '*/qt6/*' 2>/dev/null | head -1)")"
if [[ -f "$QT6_PLATFORMS/libqoffscreen.so" ]]; then
    mkdir -p "$APPDIR/usr/plugins/platforms"
    cp "$QT6_PLATFORMS/libqoffscreen.so" "$APPDIR/usr/plugins/platforms/"
fi

cat > "$APPDIR/usr/share/applications/jf8call.desktop" << 'EOF'
[Desktop Entry]
Name=JF8Call
Exec=jf8call
Icon=jf8call
Type=Application
Categories=HamRadio;Network;
Comment=JS8Call-compatible HF digital mode application
EOF

OUTPUT="$OUTPUT_JF8CALL" linuxdeploy \
    --appdir "$APPDIR" \
    --plugin qt \
    --exclude-library 'libtinfo.so*' \
    --output appimage

echo "=== Done: $OUTPUT_JF8CALL ==="

# ── jf8-tool AppImage ─────────────────────────────────────────────────────────

echo "=== Building jf8-tool AppImage ==="
APPDIR_TOOL="$SCRIPT_DIR/AppDir-tool"
rm -rf "$APPDIR_TOOL"
mkdir -p "$APPDIR_TOOL/usr/bin" "$APPDIR_TOOL/usr/share/applications" "$APPDIR_TOOL/usr/share/icons/hicolor/256x256/apps"

cp "$BUILD_DIR/jf8-tool" "$APPDIR_TOOL/usr/bin/"
cp "$SCRIPT_DIR/jf8call.png" "$APPDIR_TOOL/usr/share/icons/hicolor/256x256/apps/jf8-tool.png"

cat > "$APPDIR_TOOL/usr/share/applications/jf8-tool.desktop" << 'EOF'
[Desktop Entry]
Name=jf8-tool
Exec=jf8-tool
Icon=jf8-tool
Type=Application
Terminal=true
Categories=HamRadio;Network;
Comment=JF8Call WebSocket API command-line tool
EOF

OUTPUT="$OUTPUT_TOOL" linuxdeploy \
    --appdir "$APPDIR_TOOL" \
    --plugin qt \
    --exclude-library 'libtinfo.so*' \
    --output appimage

echo "=== Done: $OUTPUT_TOOL ==="
