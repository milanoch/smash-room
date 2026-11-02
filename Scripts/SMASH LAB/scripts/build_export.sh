#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
TARGET="${1:-macos-debug}"
BUILD_DIR="$ROOT/Export/build/$TARGET"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    "$(dirname -- "$0")/configure_export.sh" "$TARGET"
fi

case "$TARGET" in
    *-release) CONFIG=Release ;;
    *) CONFIG=Debug ;;
esac

cmake --build "$BUILD_DIR" --config "$CONFIG" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)"
