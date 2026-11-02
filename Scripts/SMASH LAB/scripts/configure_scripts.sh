#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
CONFIG="${1:-debug}"
: "${PANDA_SDK_DIR:?Set PANDA_SDK_DIR to <PandaSDK>/lib/cmake/PandaSDK (or build from the PandaEditor CMake panel)}"

case "$CONFIG" in
    debug) BUILD_TYPE=Debug ;;
    release) BUILD_TYPE=RelWithDebInfo ;;
    *) echo "Unknown config: $CONFIG (use debug|release)"; exit 1 ;;
esac

SRC="$ROOT/Scripts/SMASH LAB"
cmake -S "$SRC" -B "$SRC/cmake-build-$CONFIG" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DPandaSDK_DIR="$PANDA_SDK_DIR" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
