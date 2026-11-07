SMASH LAB — ENCLOSED APARTMENT KITCHEN
====================================

CONTENTS
- SMASH_LAB_Kitchen.gltf: glTF 2.0 scene, separate JSON representation
- SMASH_LAB_Kitchen.bin: geometry and buffer data
- textures/: nine external PBR texture images
- Object_Manifest.json: mesh names, parents, dimensions, triangle counts, UV and physics metadata
- Preview.png: rendered view of the exported scene
- CREDITS.txt: reused asset information

IMPORT
Import SMASH_LAB_Kitchen.gltf into your DCC tool or game engine. Keep the .bin file and textures folder beside it. No Draco or Meshopt decoder is required.

SCENE CONTRACT
- Real-world units: metres
- glTF coordinate system: +Y up
- Blender source coordinate system: +Z up
- Interior kitchen size: approximately 7.2 x 5.6 m, with a 2.85 m ceiling
- Fully enclosed architecture, including the front wall and ceiling
- Hinged entrance door with a short modeled entrance vestibule behind it
- 394 separate mesh nodes; 173,072 evaluated triangles; 55 PBR materials
- Nine external textures; every exported mesh has UVs
- Static scene; no animation, camera, render-light, collider, or rigid-body setup is exported

HIERARCHY
Room_Architecture
  Floor, Ceiling, walls, window, door, trim and Corridor_Visible
Kitchen_Furniture
  Cabinets, independent doors/drawers, appliances, table, chairs, shelves and decor
Loose_Props
  Individual dishes, cups, utensils, food, cookware, containers, appliances, towels and plants
Lighting_Fixtures
  Pendant and recessed fixture geometry

PHYSICS PREPARATION
Mesh extras include smash_lab_group, movable, suggested_mass_kg and constraint. Suggested masses are initial authoring values only. Door and drawer constraint fields identify likely hinge/slide behavior. Cabinet door origins are placed at hinge edges; the main entrance door has a dedicated hinge root. Add engine-specific colliders, rigid bodies, joints and destruction rules in SMASH LAB.

VALIDATION
The package was re-imported into a clean Blender scene to verify mesh count, hierarchy, UVs, external dependencies and chair orientation. Khronos glTF Validator result: zero errors and zero warnings.
