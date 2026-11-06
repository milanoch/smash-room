SMASH LAB — Modern Two-Seat Sofa
================================

OPEN / IMPORT
Import SMASH_LAB_Modern_Sofa.gltf with a glTF 2.0 importer.
Keep SMASH_LAB_Modern_Sofa.bin and the textures folder beside the .gltf file.
All texture paths are relative. No GLB, Draco, Meshopt, custom shaders, or extensions are used.

ASSET SPECIFICATION
- Dimensions: 2.0 m wide × 0.9 m deep × 0.85 m high
- Geometry: 912 triangles; one mesh with two material primitives
- Materials: beige fabric and walnut wood; standard glTF metallic-roughness PBR
- Textures: 512 × 512 PNG base colour, tangent-space normal, and packed ORM for each material
- UVs: one UV set; exported tangents and normals
- Origin: bottom centre
- glTF axes: +Y up, +Z front
- Scene contents: sofa only; no floor, room, cameras, lights, animation, skin, or morph targets
- Validation: Khronos glTF Validator — 0 errors, 0 warnings, 0 infos

ORM CHANNELS
R = occlusion (white / unused baked occlusion)
G = roughness
B = metallic (black / non-metallic)

FILES
SMASH_LAB_Modern_Sofa.gltf  — glTF 2.0 scene and material definitions
SMASH_LAB_Modern_Sofa.bin   — geometry, normals, tangents, UVs, and indices
textures/                  — all required external PNG textures
model_info.json             — geometry and texture specifications
validation.json             — final validator summary
SMASH_LAB_Modern_Sofa_Preview.png — rendered preview; not required by the model

Designed as a lightweight game prop for SMASH LAB. Use your engine's normal-map and sRGB texture import defaults; ORM/normal textures should be treated as linear data.
