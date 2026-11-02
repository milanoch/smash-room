#pragma once

#include <Bamboo/Assets/MeshAPI.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace SmashLab {

// An ordinary unit box rendered by Panda's existing lit material. Thin border
// quads are brighter and more opaque than the face centers; no new shader,
// texture, or renderer is required. All fragments share this runtime mesh.
inline Bamboo::MeshData makeEdgedBoxMesh() {
    struct Face {
        Bamboo::Vec3 normal;
        Bamboo::Vec3 u;
        Bamboo::Vec3 v;
    };
    const std::array<Face, 6> faces = {{
        {{1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 1.f, 0.f}},
        {{-1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}},
        {{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}},
        {{0.f, -1.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}},
        {{0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}},
        {{0.f, 0.f, -1.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}},
    }};
    constexpr std::array<float, 4> grid = {-.5f, -.46f, .46f, .5f};
    Bamboo::MeshData mesh;
    mesh.vertices.reserve(216);
    mesh.indices.reserve(324);
    for (const Face &face : faces) {
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                const bool middle = row == 1 && column == 1;
                const Bamboo::Vec4 color = middle ? Bamboo::Vec4(.75f, .88f, 1.f, .48f)
                                                  : Bamboo::Vec4(1.f, 1.f, 1.f, 1.f);
                const std::array<Bamboo::Vec2, 4> corners = {{
                    {grid[column], grid[row]},
                    {grid[column + 1], grid[row]},
                    {grid[column + 1], grid[row + 1]},
                    {grid[column], grid[row + 1]},
                }};
                const auto first = static_cast<uint32_t>(mesh.vertices.size());
                for (const Bamboo::Vec2 &corner : corners) {
                    Bamboo::Vertex vertex;
                    vertex.pos = {
                        face.normal.x * .5f + face.u.x * corner.x + face.v.x * corner.y,
                        face.normal.y * .5f + face.u.y * corner.x + face.v.y * corner.y,
                        face.normal.z * .5f + face.u.z * corner.x + face.v.z * corner.y,
                    };
                    vertex.uv = {corner.x + .5f, corner.y + .5f};
                    vertex.normal = face.normal;
                    vertex.color = color;
                    vertex.light = 1.f;
                    vertex.id = 0;
                    mesh.vertices.push_back(vertex);
                }
                mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first + 2, first + 3, first});
            }
        }
    }
    return mesh;
}

} // namespace SmashLab
