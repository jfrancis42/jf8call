# CMake toolchain file for cross-compiling OTA to Raspberry Pi (aarch64 / arm64)
# Targets: Raspberry Pi 4/5 running 64-bit Raspberry Pi OS or Ubuntu arm64
#
# Prerequisites (one-time setup on Ubuntu host):
#   sudo dpkg --add-architecture arm64
#   # Add ports.ubuntu.com arm64 mirror (see cmake/setup-cross-arm64.sh)
#   sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#   sudo apt-get install qt6-base-dev:arm64 libqt6network6:arm64 libhamlib-dev:arm64
#
# Usage:
#   cmake -B build-rpi \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-raspberrypi-aarch64.cmake \
#         [-DCMAKE_BUILD_TYPE=Release]

set(CMAKE_SYSTEM_NAME    Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross-compilers installed via: sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Ubuntu multiarch does NOT use a traditional sysroot directory.
# Libraries live in /usr/lib/aarch64-linux-gnu/, headers in /usr/include/.
# Do NOT set CMAKE_SYSROOT here — it breaks linker library resolution.

set(CMAKE_FIND_ROOT_PATH
    /usr/lib/aarch64-linux-gnu
    /usr/include
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Qt6 host tools (moc, rcc, uic) run on the build machine.
# QT_HOST_PATH must be the Qt installation prefix so Qt can find
# lib/qt6/libexec/moc etc. On Ubuntu/Debian this is /usr.
set(QT_HOST_PATH           /usr)
set(QT_HOST_PATH_CMAKE_DIR /usr/lib/x86_64-linux-gnu/cmake)

# Point CMake at the arm64 Qt6 cmake configs
list(APPEND CMAKE_PREFIX_PATH /usr/lib/aarch64-linux-gnu/cmake)
