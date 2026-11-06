# SMASH LAB — Scandinavian Kitchen

A complete, game-ready kitchen matching the beige Scandinavian living room. The room is modeled at real-world scale with light oak cabinetry, quartz worktops, a recessed stainless sink, integrated oven and induction hob, refrigerator, tall pantry, four-person dining set, tableware, small appliances, open shelves, plants and warm pendant fixtures.

## Open the scene

Import `SMASH_LAB_Kitchen.gltf` into a glTF 2.0-compatible engine or DCC. Keep `SMASH_LAB_Kitchen.bin` and the `textures` folder beside the `.gltf` file with their existing relative paths.

- Format: glTF 2.0 separate export; no GLB container
- Geometry: 24,662 triangles across 310 independently named mesh nodes
- Room: exactly 8 × 6 m floor, 3 m walls
- Coordinates: standard glTF Y-up, metres; the open front faces +Z
- Architecture: three walls and floor, no ceiling and no front wall
- Materials: core metallic-roughness PBR with external PNG base-color and normal textures
- Extensions: none
- Validation: the actual exported file passed cold re-import, external dependency, UV, normal, hierarchy and scale checks

## Selection and physics preparation

Furniture and appliance assemblies retain separate named roots. Chairs, plates, cups, bowls, cutlery, utensils, small appliances, plants and decorative objects are not merged into the architecture.

Cabinet doors are children of nodes ending in `_Pivot`. Their extras describe `motion: hinge_z` and the local joint axis. Drawers use nodes ending in `_Slide`, with `motion: slide_y`, local `-Y` travel and a `travel_m` value. The oven has an `Oven_Door_Pivot` with a local X hinge. These are authoring metadata and transforms for later engine-specific physics setup; no physics bodies or destruction simulation are embedded in glTF.

The tabletop, table legs, cabinet doors, drawer fronts, refrigerator parts, oven door, cookware and other important components remain separate. The sink opening and recessed bowl are actual geometry. Backsplash tiles and fine surface detail use textures rather than tiny tile meshes.

## Lighting and preview

`Preview.png` is a Cycles render of the complete scene. Pendant shades and warm emissive diffusers are included in the glTF. Preview-only cameras and area lights are intentionally excluded; add engine lights as needed for your runtime.

All geometry and texture assets in this package were created procedurally for this scene or reused from the matching SMASH LAB living-room material set. No third-party model assets are included.
