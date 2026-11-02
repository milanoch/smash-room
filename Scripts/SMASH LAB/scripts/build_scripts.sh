#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
CONFIG="${1:-debug}"
BUILD_DIR="$ROOT/Scripts/SMASH LAB/cmake-build-$CONFIG"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    "$(dirname -- "$0")/configure_scripts.sh" "$CONFIG"
fi

cmake --build "$BUILD_DIR" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)"
