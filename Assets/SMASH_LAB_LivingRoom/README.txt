SMASH LAB — SCANDINAVIAN LIVING ROOM
===================================

ENTRY FILE
Open SMASH_LAB_LivingRoom.gltf in a glTF 2.0 importer.
Keep SMASH_LAB_LivingRoom.bin and the textures folder beside it with their relative paths unchanged.

SCENE
• Real-world scale in meters; standard glTF Y-up coordinates.
• Room floor: 8 × 6 m. Wall height: 3 m.
• Open front faces glTF +Z. Three walls, floor, and no ceiling mesh.
• 16,836 exported triangles, 202 independently selectable mesh nodes, 28 materials.
• 13 external PNG textures: 512 × 512, except the oak floor at 1024 × 1024.
• Core metallic-roughness PBR with UV-mapped base-color and normal textures.
• Only the standard KHR_lights_punctual extension is used for the sun and warm lamp lights.

HIERARCHY
The SMASH_LAB_LivingRoom root preserves named assemblies and parts, including:
Architecture, Window, Sofa, CoffeeTable, TVStand, Television, Bookshelf,
FloorLamp, CeilingLight, Rug_SoftWool, plants, paintings, books, and vases.
Tabletops, legs, shelves, TV housing/screen, cushions, lamp parts, and other
important components are separate mesh nodes. Walls and floor are separate
from furniture under Architecture.

PHYSICS NOTES
No rigid bodies, collision shapes, or destruction logic are baked into the file.
Assembly roots and architectural meshes include descriptive custom properties
in glTF extras to support later physics setup in Panda Engine. Apply collisions
and destruction to the individual named nodes as needed.

VALIDATION
The delivered glTF was cold-reimported successfully. The export has the stated
triangle count, all expected external files, UVs on every mesh, applied unit
object scales, and an 8 × 6 m floor after coordinate conversion.
Preview.png is a render of the completed scene; the 3D scene files are included.

AUTHORING NOTE
The pendant canopy remains at ceiling height although no ceiling mesh is present,
matching the requested three-wall open-front sandbox layout.
