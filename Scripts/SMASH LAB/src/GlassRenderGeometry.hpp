#pragma once

#include "GlassSparkleBurst.hpp"

#include <Bamboo/Assets/MeshAPI.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace SmashLab {

// A spare column in the atlas's white alpha-ramp strip. Moving only physical
// glass here leaves its base color/alpha unchanged while the existing PBR
// metallic-roughness sampler selects its own roughness. Effects keep their UVs.
inline constexpr uint32_t kGlassHighlightColumn = 262;
inline constexpr unsigned char kEffectRoughnessGreen = 102; // .2375 * (102/255) == .095

// CPU preparation for a single ordinary Panda RuntimeMesh. The installed Metal
// backend couples depth writes to depth testing, including alpha-blended meshes.
// Keep this small group of transparent triangles ordered within one submission;
// opaque scene geometry still provides normal depth occlusion. This is an
// approximate triangle sort, not order-independent transparency for arbitrary
// intersecting transparent surfaces.
inline void appendGlassGeometry(Bamboo::MeshData &target, const Bamboo::MeshData &local,
                                Bamboo::Vec3 position, Bamboo::Quat orientation, Bamboo::Vec3 scale,
                                Bamboo::Vec3 cameraPosition, bool polishedGlass = false) {
    const glm::vec3 translation(position.x, position.y, position.z);
    const glm::vec3 dimensions(scale.x, scale.y, scale.z);
    const glm::vec3 eye(cameraPosition.x, cameraPosition.y, cameraPosition.z);
    const glm::quat rotation = glm::normalize(glm::quat(orientation.w, orientation.x, orientation.y, orientation.z));
    const auto first = static_cast<uint32_t>(target.vertices.size());
    for (auto vertex : local.vertices) {
        const glm::vec3 world = translation + rotation * (glm::vec3(vertex.pos.x, vertex.pos.y, vertex.pos.z) * dimensions);
        const glm::vec3 normal = glm::normalize(rotation * (glm::vec3(vertex.normal.x, vertex.normal.y, vertex.normal.z) / dimensions));
        const glm::vec3 toEye = eye - world;
        const float eyeDistance = glm::length(toEye);
        const float cosine = eyeDistance > 1.e-5f ? std::abs(glm::dot(normal, toEye / eyeDistance)) : 1.f;
        const float rim = std::pow(1.f - glm::clamp(cosine, 0.f, 1.f), 5.f);
        // PBR ignores vertex alpha, but honors texture alpha. Remap the authored
        // edge mask + subtle viewing-angle rim into the shared atlas alpha ramp.
        vertex.uv = GlassSparkleBurst::glassUV(glm::clamp(vertex.color.w + rim * .3f, 0.f, 1.f));
        if (polishedGlass) {
            vertex.uv.x = (static_cast<float>(kGlassHighlightColumn) + .5f) / GlassSparkleBurst::kTextureWidth;
        }
        vertex.pos = {world.x, world.y, world.z};
        vertex.normal = {normal.x, normal.y, normal.z};
        vertex.color.w = 1.f;
        target.vertices.push_back(vertex);
    }
    for (auto index : local.indices) { target.indices.push_back(first + index); }
}

inline void sortGlassTriangles(Bamboo::MeshData &mesh, Bamboo::Vec3 cameraPosition, Bamboo::Quat orientation) {
    struct Triangle { std::array<uint32_t, 3> indices; float depth; };
    const glm::vec3 eye(cameraPosition.x, cameraPosition.y, cameraPosition.z);
    const glm::quat rotation = glm::normalize(glm::quat(orientation.w, orientation.x, orientation.y, orientation.z));
    const glm::vec3 forward = rotation * glm::vec3(0.f, 0.f, -1.f);
    std::vector<Triangle> triangles;
    triangles.reserve(mesh.indices.size() / 3);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        glm::vec3 centroid(0.f);
        for (size_t j = 0; j < 3; ++j) {
            const auto &p = mesh.vertices[mesh.indices[i + j]].pos;
            centroid += glm::vec3(p.x, p.y, p.z);
        }
        triangles.push_back({{mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]},
                             glm::dot(centroid / 3.f - eye, forward)});
    }
    std::stable_sort(triangles.begin(), triangles.end(), [](const Triangle &a, const Triangle &b) { return a.depth > b.depth; });
    size_t index = 0;
    for (const auto &triangle : triangles) {
        for (auto vertex : triangle.indices) { mesh.indices[index++] = vertex; }
    }
}

} // namespace SmashLab
