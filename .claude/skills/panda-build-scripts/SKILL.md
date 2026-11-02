---
name: panda-build-scripts
description: Building and reloading the game's C++ scripts (Scripts/). Use after editing script code.
---

# Building scripts

Scripts are C++ in `Scripts/SMASH LAB/src/`; they are built into a dynamic library that
the editor loads.

## Workflow

1. Edit the code (the script skeleton and fields — PandaSDK `docs/scripting.md`:
   the `Script` class, `PANDA_FIELDS_*`, `REGISTER_SCRIPT`).
2. Build from a terminal:
   `Scripts/SMASH LAB/scripts/configure_scripts.sh debug` (once),
   then `Scripts/SMASH LAB/scripts/build_scripts.sh debug`.
   Fix compilation errors yourself, using the output.
3. Ask the user to press Reload (Cmd/Ctrl+R) — the editor picks up the fresh library (by mtime).
   The user is also the one who runs and checks the game.

After the scripts are loaded the editor updates `.Panda/scripts-manifest.json` — take the fields
for binding in a world from there (panda-script-fields).
