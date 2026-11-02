---
name: panda-assets
description: Adding assets (textures, glTF meshes, worlds) and editing .mat materials in a Panda project. Use when working with content.
---

# Assets and materials

## Adding an asset

Put the file into `Assets/` — the registry is maintained by the EDITOR: it registers new files
when the project is opened or rescanned and assigns ids, keeping each id in a `<file>.pmeta`
sidecar next to the asset file. Do not edit the sidecars by hand. If the id of a new file is
needed right away and the editor has not rescanned yet, ask the user (the registry is updated and
the id appears in the file's `.pmeta`).

- Textures: `.png/.jpg`. sRGB (color) by default; DATA maps (normal, roughness, noise) need sRGB
  switched off — ask the user to toggle it in the texture inspector.
- Meshes: `.gltf` (+ `.bin` and textures next to it). For every glTF material the editor creates
  `<name>.mat` next to the mesh; meshes of one folder share materials by name.
- Worlds: `.world` (see panda-world-editing).

## Editing a material (.mat)

JSON `{"inputs": [{"name","type","value"}]}`; `type`: 0 float, 1 uint, 2 vec4,
3 texture (value — asset id, 0 = empty), 4 color (value — {x,y,z,w}).

- Edit the values freely; the SET and the ORDER of the fields are the shader's contract, do not
  change them.
- Create a new material in the editor (choosing a shader fills in the field template).
- The material's shader is stored in the registry (the `.pmeta` sidecar), not in the .mat.

After the edits, ask the user to press Reload (Cmd/Ctrl+R): the editor rereads the changed
files. More details: PandaSDK `docs/assets.md`.
