#!/bin/sh
set -eu

EXTENSION_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(CDPATH= cd -- "$EXTENSION_DIR/../.." && pwd)"
ENGINE_SOURCE="${1:?Pass a separate Panda source checkout at commit 37b843540f0164ea0971fe4c3ec8abff02556b71}"
ENGINE_SOURCE="$(CDPATH= cd -- "$ENGINE_SOURCE" && pwd)"
EXPECTED_COMMIT=37b843540f0164ea0971fe4c3ec8abff02556b71
ACTUAL_COMMIT="$(git -C "$ENGINE_SOURCE" rev-parse HEAD)"
if [ "$ACTUAL_COMMIT" != "$EXPECTED_COMMIT" ]; then
    echo "Panda version mismatch: expected $EXPECTED_COMMIT, found $ACTUAL_COMMIT. Source not changed." >&2
    exit 1
fi

# No reset/checkout/staging, and no edits to an installed editor or SDK.
# Applying an already applied patch is a no-op; a conflicting patch is an error.
if patch -d "$ENGINE_SOURCE" -p1 -R --dry-run -s < "$EXTENSION_DIR/panda-cloth.patch" >/dev/null 2>&1; then
    echo 'Cloth extension is already applied.'
elif patch -d "$ENGINE_SOURCE" -p1 --dry-run -s < "$EXTENSION_DIR/panda-cloth.patch" >/dev/null 2>&1; then
    patch -d "$ENGINE_SOURCE" -p1 -s < "$EXTENSION_DIR/panda-cloth.patch"
else
    echo 'Cloth patch conflicts with this checkout. Preserve its changes and use a separate clean source tree.' >&2
    exit 1
fi

EDITOR_OUTPUT="$PROJECT_DIR/Scripts/SMASH LAB/bin/cloth-editor"
ENGINE_BUILD="${PANDA_CLOTH_BUILD_DIR:-$EDITOR_OUTPUT/cmake-build-engine}"
SLANG_COMPILER="${PANDA_SLANGC_EXECUTABLE:-/Applications/PandaEditor.app/Contents/Resources/Tools/slang/mac/aarch64/bin/slangc}"
if [ ! -x "$SLANG_COMPILER" ]; then
    echo 'Set PANDA_SLANGC_EXECUTABLE to the existing slangc executable.' >&2
    exit 1
fi

cmake -S "$ENGINE_SOURCE" -B "$ENGINE_BUILD" \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "-DBIN_DIRECTORY=$EDITOR_OUTPUT" "-DPANDA_SLANGC_EXECUTABLE=$SLANG_COMPILER"
cmake --build "$ENGINE_BUILD" --parallel 8 --target PandaEditor PandaSDKPackage
cmake --build "$ENGINE_BUILD" --parallel 8 --target PandaEditorEmbedSdk
echo "Ready for manual checking: $EDITOR_OUTPUT/PandaEditor.app"
