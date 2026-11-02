---
name: panda-script-fields
description: Binding field values of C++ scripts to world entities (scriptListComponent, fieldId, types). Use when you need to attach a script to an entity or set its parameters.
---

# Script fields in a world

A script is attached to an entity in `scriptListComponent`; field values are set in the same
place.

## The source of truth is the manifest

`.Panda/scripts-manifest.json` is written by the editor on every script load: class names,
fields, their `fieldId` and types. **Always take `fieldId` from there**, do not compute it. If
the file is missing, ask the user to build/reload the scripts.

## Format in .world

```json
"scriptListComponent": {
    "scripts": [
        {
            "name": "MoveScript",
            "scriptFields": [
                { "name": "speed", "fieldId": 123456789, "type": 1, "value": 3.5 }
            ]
        }
    ]
}
```

- `name` — the name of the C++ class (`REGISTER_SCRIPT`).
- `type` as a number: 0 integer, 1 float, 2 entity, 3 texture, 4 material, 5 world
  (the manifest has the same types as words).
- `value`: integer/float — a number; entity — the id of an entity of THIS world; texture/
  material/world — the asset id from the asset's `.pmeta` sidecar (`<file>.pmeta`, key `id`).
- List EVERY field of the script: a field that is not listed is written as zero when Play
  starts — the initializer in the C++ class is not used.
- A script without parameters: `{ "name": "MyScript", "scriptFields": [] }`.

After the edit, ask the user to press Reload (Cmd/Ctrl+R).
