#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
TARGET="${1:-macos-debug}"
: "${PANDA_SDK_DIR:?Set PANDA_SDK_DIR to <PandaSDK>/lib/cmake/PandaSDK (or configure from the PandaEditor CMake panel)}"

EXPORT_DIR="$ROOT/Export"
BUILD_DIR="$EXPORT_DIR/build/$TARGET"

# The generator and the platform flags of the target (the CMake presets used to carry this).
case "$TARGET" in
    macos-*)
        set -- -G Xcode ;;
    ios-device-*)
        set -- -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
            -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 ;;
    ios-*)
        set -- -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
            -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 ;;
    windows-*)
        set -- -G "Visual Studio 17 2022" -A x64 ;;
    linux-debug)
        set -- -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug ;;
    linux-release)
        set -- -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ;;
    *)
        echo "Unknown export target: $TARGET"; exit 1 ;;
esac

cmake -S "$EXPORT_DIR" -B "$BUILD_DIR" "$@" -DPandaSDK_DIR="$PANDA_SDK_DIR"
