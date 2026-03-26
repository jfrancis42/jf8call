cmake_minimum_required(VERSION 3.16)

# ── Version ───────────────────────────────────────────────────────────────────
file(READ "${CMAKE_SOURCE_DIR}/VERSION" JF8CALL_FULL_VERSION)
string(STRIP "${JF8CALL_FULL_VERSION}" JF8CALL_FULL_VERSION)
string(REGEX REPLACE "[-+].*$" "" _JF8CALL_VERSION_NUMERIC "${JF8CALL_FULL_VERSION}")
project(jf8call VERSION ${_JF8CALL_VERSION_NUMERIC} LANGUAGES CXX)
file(WRITE "${CMAKE_BINARY_DIR}/jf8call_version.h"
    "#pragma once\n#define JF8CALL_VERSION_STR \"${JF8CALL_FULL_VERSION}\"\n")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Qt6 ───────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Widgets WebSockets Network)

# ── gfsk8modem ─────────────────────────────────────────────────────────────────
# Location of the gfsk8modem source tree.
# Pass -DGFSK8MODEM_DIR=<path> to cmake, or set the default below.
if(NOT DEFINED GFSK8MODEM_DIR)
    set(GFSK8MODEM_DIR "$ENV{HOME}/gfsk8-modem-clean")
endif()

if(NOT EXISTS "${GFSK8MODEM_DIR}/include/gfsk8modem.h")
    message(FATAL_ERROR
        "gfsk8modem not found at ${GFSK8MODEM_DIR}.\n"
        "Pass -DGFSK8MODEM_DIR=<path> to cmake.")
endif()

# Locate the pre-built libgfsk8modem.a.
# GFSK8MODEM_BUILD_DIR can be passed on the cmake command line to point at
# a cross-compile build directory (e.g. build-rpi, build-windows).
if(DEFINED GFSK8MODEM_BUILD_DIR)
    set(GFSK8MODEM_LIB "${GFSK8MODEM_BUILD_DIR}/libgfsk8modem.a")
else()
    set(GFSK8MODEM_LIB "${GFSK8MODEM_DIR}/build/libgfsk8modem.a")
    if(NOT EXISTS "${GFSK8MODEM_LIB}")
        set(GFSK8MODEM_LIB "${GFSK8MODEM_DIR}/libgfsk8modem.a")
    endif()
endif()
if(NOT EXISTS "${GFSK8MODEM_LIB}")
    message(FATAL_ERROR
        "libgfsk8modem.a not found. Build gfsk8modem first:\n"
        "  cd ${GFSK8MODEM_DIR} && cmake -B build && cmake --build build\n"
        "For cross-compile, pass -DGFSK8MODEM_BUILD_DIR=<path>.")
endif()

add_library(gfsk8modem STATIC IMPORTED)
set_target_properties(gfsk8modem PROPERTIES
    IMPORTED_LOCATION "${GFSK8MODEM_LIB}")
target_include_directories(gfsk8modem INTERFACE
    "${GFSK8MODEM_DIR}/include"
    "${GFSK8MODEM_DIR}/src")

# ── KissFFT (for waterfall) ───────────────────────────────────────────────────
find_package(PkgConfig QUIET)
# Skip pkg-config during cross-compilation: it would find the HOST architecture's
# kissfft and cause a link error against the wrong ABI.
if(PkgConfig_FOUND AND NOT CMAKE_CROSSCOMPILING)
    pkg_check_modules(KISSFFT QUIET kissfft-float)
endif()
if(NOT KISSFFT_FOUND)
    # Try standard name
    find_library(KISSFFT_LIB NAMES kissfft-float kissfft)
    find_path(KISSFFT_INCLUDE_DIR NAMES kissfft/kiss_fft.h kiss_fft.h)
    if(KISSFFT_LIB)
        set(KISSFFT_FOUND TRUE)
        set(KISSFFT_LIBRARIES "${KISSFFT_LIB}")
        if(KISSFFT_INCLUDE_DIR)
            set(KISSFFT_INCLUDE_DIRS "${KISSFFT_INCLUDE_DIR}")
        endif()
    endif()
endif()
if(NOT KISSFFT_FOUND)
    # Fall back to using the libkissfft.a already built by the gfsk8modem
    # build (in GFSK8MODEM_BUILD_DIR).  This is required for cross-compilation
    # where the target-arch system kissfft package is not installed on the host.
    # gfsk8modem always builds vendored kissfft as a static library target named
    # "kissfft", so libkissfft.a is always present in any gfsk8modem build dir.
    set(_kissfft_prebuilt "")
    if(DEFINED GFSK8MODEM_BUILD_DIR AND EXISTS "${GFSK8MODEM_BUILD_DIR}/libkissfft.a")
        set(_kissfft_prebuilt "${GFSK8MODEM_BUILD_DIR}/libkissfft.a")
    elseif(EXISTS "${GFSK8MODEM_DIR}/build/libkissfft.a")
        set(_kissfft_prebuilt "${GFSK8MODEM_DIR}/build/libkissfft.a")
    endif()

    if(_kissfft_prebuilt)
        add_library(kissfft_imported STATIC IMPORTED)
        set_target_properties(kissfft_imported PROPERTIES IMPORTED_LOCATION "${_kissfft_prebuilt}")
        set(KISSFFT_FOUND TRUE)
        set(KISSFFT_LIBRARIES kissfft_imported)
        set(KISSFFT_INCLUDE_DIRS "${GFSK8MODEM_DIR}/vendor")
        message(STATUS "KissFFT: using pre-built ${_kissfft_prebuilt}")
    else()
        message(FATAL_ERROR "KissFFT not found. Install libkissfft-dev, or build gfsk8modem first (libkissfft.a not found in GFSK8MODEM_BUILD_DIR).")
    endif()
endif()

# ── PortAudio ────────────────────────────────────────────────────────────────
# Skip pkg-config during cross-compilation: it would find the HOST architecture's
# portaudio and inject /usr/include, which breaks MinGW compilation.
if(PkgConfig_FOUND AND NOT CMAKE_CROSSCOMPILING)
    pkg_check_modules(PORTAUDIO QUIET portaudio-2.0)
endif()
if(NOT PORTAUDIO_FOUND)
    find_library(PORTAUDIO_LIB NAMES portaudio portaudio_x64)
    find_path(PORTAUDIO_INCLUDE_DIR NAMES portaudio.h)
    if(PORTAUDIO_LIB)
        set(PORTAUDIO_FOUND TRUE)
        set(PORTAUDIO_LIBRARIES "${PORTAUDIO_LIB}")
        if(PORTAUDIO_INCLUDE_DIR)
            set(PORTAUDIO_INCLUDE_DIRS "${PORTAUDIO_INCLUDE_DIR}")
        endif()
    endif()
endif()
if(NOT PORTAUDIO_FOUND)
    message(FATAL_ERROR "PortAudio not found. Install libportaudio-dev.\nFor Windows cross-compile: run build-windows-cross.sh --setup to build PortAudio.")
endif()

# ── Olivia modem (olivia-modem library) ──────────────────────────────────────
# Location of the olivia-modem source tree.
# Pass -DOLIVIA_MODEM_DIR=<path> or rely on the default ~/olivia-modem.
if(NOT DEFINED OLIVIA_MODEM_DIR)
    set(OLIVIA_MODEM_DIR "$ENV{HOME}/olivia-modem")
endif()

if(EXISTS "${OLIVIA_MODEM_DIR}/include/oliviamodem.h")
    # Determine the pre-built library path, mirroring gfsk8modem pattern
    if(DEFINED OLIVIA_MODEM_BUILD_DIR)
        set(OLIVIA_MODEM_LIB "${OLIVIA_MODEM_BUILD_DIR}/liboliviamodem.a")
    else()
        set(OLIVIA_MODEM_LIB "${OLIVIA_MODEM_DIR}/build/liboliviamodem.a")
        if(NOT EXISTS "${OLIVIA_MODEM_LIB}")
            set(OLIVIA_MODEM_LIB "${OLIVIA_MODEM_DIR}/liboliviamodem.a")
        endif()
    endif()

    if(EXISTS "${OLIVIA_MODEM_LIB}")
        add_library(oliviamodem STATIC IMPORTED)
        set_target_properties(oliviamodem PROPERTIES
            IMPORTED_LOCATION "${OLIVIA_MODEM_LIB}")
        target_include_directories(oliviamodem INTERFACE
            "${OLIVIA_MODEM_DIR}/include")
        set(OLIVIA_MODEM_FOUND TRUE)
        message(STATUS "Olivia modem found — Olivia mode enabled")
    else()
        message(STATUS "Olivia modem: library not built yet — run cmake --build in ${OLIVIA_MODEM_DIR}. Olivia disabled.")
    endif()
else()
    message(STATUS "Olivia modem not found at ${OLIVIA_MODEM_DIR} — Olivia disabled")
endif()

# ── libpsk (PSK31/63/FEC modem, optional) ────────────────────────────────────
# Location of the libpsk source tree (~/.../psk31).
# Pass -DPSK_MODEM_DIR=<path> or rely on the default ~/psk31.
if(NOT DEFINED PSK_MODEM_DIR)
    set(PSK_MODEM_DIR "$ENV{HOME}/psk31")
endif()

if(EXISTS "${PSK_MODEM_DIR}/include/psk.h")
    if(DEFINED PSK_MODEM_BUILD_DIR)
        set(PSK_MODEM_LIB "${PSK_MODEM_BUILD_DIR}/libpsk.a")
    else()
        set(PSK_MODEM_LIB "${PSK_MODEM_DIR}/build/libpsk.a")
        if(NOT EXISTS "${PSK_MODEM_LIB}")
            set(PSK_MODEM_LIB "${PSK_MODEM_DIR}/libpsk.a")
        endif()
    endif()

    if(EXISTS "${PSK_MODEM_LIB}")
        add_library(pskmodem STATIC IMPORTED)
        set_target_properties(pskmodem PROPERTIES
            IMPORTED_LOCATION "${PSK_MODEM_LIB}")
        target_include_directories(pskmodem INTERFACE
            "${PSK_MODEM_DIR}/include")
        set(PSK_MODEM_FOUND TRUE)
        message(STATUS "libpsk found — PSK modem enabled")
    else()
        message(STATUS "libpsk: library not built yet — run cmake --build in ${PSK_MODEM_DIR}. PSK disabled.")
    endif()
else()
    message(STATUS "libpsk not found at ${PSK_MODEM_DIR} — PSK modem disabled")
endif()

# ── Codec2 / FreeDV (optional) ───────────────────────────────────────────────
# Native Linux/RPi: link against system libcodec2 via pkg-config.
# Windows cross-compile: pass -DCODEC2_DIR=<path-to-prebuilt> to point at a
#   directory containing include/codec2/ headers and lib/libcodec2.a.
if(DEFINED CODEC2_DIR)
    find_library(CODEC2_LIB NAMES codec2
        PATHS "${CODEC2_DIR}/lib" "${CODEC2_DIR}"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_path(CODEC2_INCLUDE_DIR NAMES codec2/freedv_api.h
        PATHS "${CODEC2_DIR}/include"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(CODEC2_LIB AND CODEC2_INCLUDE_DIR)
        set(CODEC2_FOUND TRUE)
        set(CODEC2_LIBRARIES "${CODEC2_LIB}")
        set(CODEC2_INCLUDE_DIRS "${CODEC2_INCLUDE_DIR}")
    endif()
elseif(PkgConfig_FOUND AND NOT CMAKE_CROSSCOMPILING)
    pkg_check_modules(CODEC2 QUIET codec2)
endif()
if(NOT CODEC2_FOUND)
    find_library(CODEC2_LIB NAMES codec2)
    find_path(CODEC2_INCLUDE_DIR NAMES codec2/freedv_api.h)
    if(CODEC2_LIB AND CODEC2_INCLUDE_DIR)
        set(CODEC2_FOUND TRUE)
        set(CODEC2_LIBRARIES "${CODEC2_LIB}")
        set(CODEC2_INCLUDE_DIRS "${CODEC2_INCLUDE_DIR}")
    endif()
endif()

# ── Hamlib (optional) ────────────────────────────────────────────────────────
if(PkgConfig_FOUND AND NOT WIN32)
    pkg_check_modules(HAMLIB QUIET hamlib)
endif()
if(NOT HAMLIB_FOUND AND DEFINED HAMLIB_WINDOWS_DIR)
    find_library(HAMLIB_LIB NAMES hamlib hamlib-4
        PATHS "${HAMLIB_WINDOWS_DIR}/lib/gcc" "${HAMLIB_WINDOWS_DIR}/lib"
        NO_DEFAULT_PATH)
    if(HAMLIB_LIB)
        set(HAMLIB_FOUND TRUE)
        set(HAMLIB_LIBRARIES "${HAMLIB_LIB}")
        set(HAMLIB_INCLUDE_DIRS "${HAMLIB_WINDOWS_DIR}/include")
    endif()
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/buildinfo.cpp
    src/config.cpp
    src/checksum.cpp
    src/gridcache.cpp
    src/messageinbox.cpp
    src/hamlibcontroller.cpp
    src/gfsk8_modem.cpp
    src/js8message.cpp
    src/messagemodel.cpp
    src/waterfallwidget.cpp
    src/audioinput.cpp
    src/audiooutput.cpp
    src/periodclock.cpp
    src/updatechecker.cpp
    src/pskreporter.cpp
    src/mainwindow.cpp
    src/wsserver.cpp
    resources.qrc
)

if(CODEC2_FOUND)
    list(APPEND SOURCES src/codec2_modem.cpp)
    message(STATUS "Codec2 found — DATAC modem enabled")
else()
    message(STATUS "Codec2 not found — DATAC modem disabled (install libcodec2-dev to enable)")
endif()

if(OLIVIA_MODEM_FOUND)
    list(APPEND SOURCES src/olivia_modem.cpp)
endif()

if(PSK_MODEM_FOUND)
    list(APPEND SOURCES src/psk_modem.cpp)
endif()

set(HEADERS
    src/buildinfo.h
    src/config.h
    src/checksum.h
    src/gridcache.h
    src/messageinbox.h
    src/hamlibcontroller.h
    src/js8message.h
    src/messagemodel.h
    src/waterfallwidget.h
    src/audioinput.h
    src/audiooutput.h
    src/periodclock.h
    src/updatechecker.h
    src/pskreporter.h
    src/mainwindow.h
    src/wsserver.h
)

if(WIN32)
    add_executable(jf8call WIN32 ${SOURCES} ${HEADERS})
else()
    add_executable(jf8call ${SOURCES} ${HEADERS})
endif()

target_include_directories(jf8call PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${GFSK8MODEM_DIR}/include"
    "${GFSK8MODEM_DIR}/src"
    ${KISSFFT_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
)

target_link_libraries(jf8call PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::WebSockets
    Qt6::Network
    gfsk8modem
    ${KISSFFT_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
)

if(HAMLIB_FOUND)
    message(STATUS "Hamlib found — radio control enabled")
    target_compile_definitions(jf8call PRIVATE HAVE_HAMLIB)
    target_include_directories(jf8call PRIVATE ${HAMLIB_INCLUDE_DIRS})
    target_link_libraries(jf8call PRIVATE ${HAMLIB_LIBRARIES})
    target_link_options(jf8call PRIVATE ${HAMLIB_LDFLAGS_OTHER})
else()
    message(STATUS "Hamlib not found — radio control disabled")
endif()

if(CODEC2_FOUND)
    target_compile_definitions(jf8call PRIVATE HAVE_CODEC2)
    target_include_directories(jf8call PRIVATE ${CODEC2_INCLUDE_DIRS})
    target_link_libraries(jf8call PRIVATE ${CODEC2_LIBRARIES})
    if(CODEC2_LDFLAGS_OTHER)
        target_link_options(jf8call PRIVATE ${CODEC2_LDFLAGS_OTHER})
    endif()
endif()

if(OLIVIA_MODEM_FOUND)
    target_compile_definitions(jf8call PRIVATE HAVE_OLIVIA)
    target_link_libraries(jf8call PRIVATE oliviamodem m)
endif()

if(PSK_MODEM_FOUND)
    target_compile_definitions(jf8call PRIVATE HAVE_PSK_MODEM)
    target_include_directories(jf8call PRIVATE "${PSK_MODEM_DIR}/include")
    target_link_libraries(jf8call PRIVATE pskmodem m)
endif()

# ── Build timestamp ───────────────────────────────────────────────────────────
# Generates build_timestamp.h (into the binary dir) before any source is
# compiled.  Only buildinfo.cpp includes it, so only that one file recompiles.
set(BUILD_TIMESTAMP_H "${CMAKE_BINARY_DIR}/build_timestamp.h")
set(BUILD_STAMP_FILE  "${CMAKE_BINARY_DIR}/jf8call_build_time.stamp")

# JF8CALL_BUILD_TIME_OVERRIDE: when set (by the release script), all platform
# builds share the same timestamp so the server sees a consistent value.
if(DEFINED JF8CALL_BUILD_TIME_OVERRIDE AND NOT "${JF8CALL_BUILD_TIME_OVERRIDE}" STREQUAL "")
    set(_FIXED_TIME_ARG "-DFIXED_TIME=${JF8CALL_BUILD_TIME_OVERRIDE}")
else()
    set(_FIXED_TIME_ARG "")
endif()

add_custom_target(jf8call_build_timestamp
    COMMAND ${CMAKE_COMMAND}
        "-DHEADER_FILE=${BUILD_TIMESTAMP_H}"
        "-DSTAMP_FILE=${BUILD_STAMP_FILE}"
        ${_FIXED_TIME_ARG}
        -P "${CMAKE_SOURCE_DIR}/cmake/write_build_time.cmake"
    BYPRODUCTS "${BUILD_TIMESTAMP_H}" "${BUILD_STAMP_FILE}"
    COMMENT "Recording build timestamp"
)
add_dependencies(jf8call jf8call_build_timestamp)

# On successful native dev build: push the stamp file directly to the server.
# The release script uses JF8CALL_SUPPRESS_DEPLOY=ON and handles deployment
# (only after all three platforms succeed) so that ~/ordo/jf8call-build-time is
# kept as the last *release* timestamp, not the last dev build timestamp.
option(JF8CALL_SUPPRESS_DEPLOY "Skip POST_BUILD deploy to ordo (used by build-release.sh)" OFF)
if(NOT CMAKE_CROSSCOMPILING AND NOT JF8CALL_SUPPRESS_DEPLOY)
    add_custom_command(TARGET jf8call POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DSTAMP_FILE=${BUILD_STAMP_FILE}"
            -P "${CMAKE_SOURCE_DIR}/cmake/deploy_stamp.cmake"
        COMMENT "Deploying build time to ordo"
    )
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS jf8call RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
