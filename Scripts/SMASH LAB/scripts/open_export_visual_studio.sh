#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
TARGET="${1:-windows-debug}"
PROJECT_DIR="$ROOT/Export/build/$TARGET"

"$(dirname -- "$0")/configure_export.sh" "$TARGET"

SOLUTION_PATH=""
if [ -d "$PROJECT_DIR" ]; then
    SOLUTION_PATH="$(find "$PROJECT_DIR" -maxdepth 1 -name '*.sln' -print -quit)"
fi
if [ -z "$SOLUTION_PATH" ]; then
    echo "Visual Studio solution not found in: $PROJECT_DIR"
    exit 1
fi

if command -v cmd.exe >/dev/null 2>&1; then
    WINDOWS_PATH="$SOLUTION_PATH"
    if command -v cygpath >/dev/null 2>&1; then
        WINDOWS_PATH="$(cygpath -w "$SOLUTION_PATH")"
    fi
    cmd.exe /C start "" "$WINDOWS_PATH"
elif command -v open >/dev/null 2>&1; then
    open "$SOLUTION_PATH"
elif command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$SOLUTION_PATH"
else
    echo "Open Visual Studio solution manually: $SOLUTION_PATH"
fi
