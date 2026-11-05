#pragma once

#include <Bamboo/Assets/MeshAPI.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace SmashLab {

// Match Panda's editor primitive: neutral white vertices and full coverage.
// Object_Glass alone supplies the tint and opacity in Edit and Play. A second
// alpha mask or brighter borders would change the runtime replacement's look.
// Decorative effects author their own colors and masks independently.
inline Bamboo::Vec4 glassSurfaceVertexColor(bool /*edge*/) {
    return {1.f, 1.f, 1.f, 1.f};
}

// Match Panda::PrimitiveMeshes::buildCube, including its face/index order.
// The intact Play replacement must have the same transparent surfaces as the
// editor primitive. Shards keep their independent prepared geometry below.
inline Bamboo::MeshData makeEdgedBoxMesh() {
    struct Face {
        Bamboo::Vec3 normal;
        Bamboo::Vec3 u;
        Bamboo::Vec3 v;
    };
    const std::array<Face, 6> faces = {{
        {{0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}},
        {{0.f, 0.f, -1.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}},
        {{1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 1.f, 0.f}},
        {{-1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}},
        {{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}},
        {{0.f, -1.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}},
    }};
    const std::array<Bamboo::Vec2, 4> corners = {{{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f}, {-.5f, .5f}}};
    Bamboo::MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);
    for (const Face &face : faces) {
        const auto first = static_cast<uint32_t>(mesh.vertices.size());
        for (const Bamboo::Vec2 &corner : corners) {
            Bamboo::Vertex vertex;
            vertex.pos = {
                face.normal.x * .5f + face.u.x * corner.x + face.v.x * corner.y,
                face.normal.y * .5f + face.u.y * corner.x + face.v.y * corner.y,
                face.normal.z * .5f + face.u.z * corner.x + face.v.z * corner.y,
            };
            vertex.uv = {corner.x + .5f, .5f - corner.y};
            vertex.normal = face.normal;
            vertex.color = glassSurfaceVertexColor(false);
            vertex.light = 1.f;
            vertex.id = 0;
            mesh.vertices.push_back(vertex);
        }
        mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first + 2, first + 3, first});
    }
    return mesh;
}

namespace GlassGeometry {

inline Bamboo::Vec3 subtract(Bamboo::Vec3 a, Bamboo::Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Bamboo::Vec3 cross(Bamboo::Vec3 a, Bamboo::Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// One convex, outward-wound face. Its inner fan and the surrounding border
// occupy the same plane with disjoint interiors, so there is no z-fighting.
inline void appendFace(Bamboo::MeshData &mesh, const std::vector<Bamboo::Vec3> &corners) {
    auto normal = cross(subtract(corners[1], corners[0]), subtract(corners[2], corners[0]));
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    normal = {normal.x / length, normal.y / length, normal.z / length};
    Bamboo::Vec3 center{};
    for (auto p : corners) {
        center.x += p.x / corners.size();
        center.y += p.y / corners.size();
        center.z += p.z / corners.size();
    }
    const auto first = static_cast<uint32_t>(mesh.vertices.size());
    auto vertex = [&](Bamboo::Vec3 p, Bamboo::Vec4 color) {
        Bamboo::Vertex result{};
        result.pos = p;
        result.normal = normal;
        result.color = color;
        result.light = 1.f;
        mesh.vertices.push_back(result);
    };
    vertex(center, glassSurfaceVertexColor(false));
    for (auto p : corners) {
        vertex({center.x + (p.x - center.x) * .94f,
                center.y + (p.y - center.y) * .94f,
                center.z + (p.z - center.z) * .94f}, glassSurfaceVertexColor(false));
    }
    for (auto p : corners) { vertex(p, glassSurfaceVertexColor(true)); }
    const auto count = static_cast<uint32_t>(corners.size());
    for (uint32_t i = 0; i < count; ++i) {
        const auto j = (i + 1) % count;
        const uint32_t innerA = first + 1 + i, innerB = first + 1 + j;
        const uint32_t outerA = first + 1 + count + i, outerB = first + 1 + count + j;
        mesh.indices.insert(mesh.indices.end(), {first, innerA, innerB,
                                                innerA, outerA, outerB,
                                                innerA, outerB, innerB});
    }
}

} // namespace GlassGeometry

// Closed convex shards, with their broad profile perpendicular to the shortest
// collider dimension. Small cells use triangular/irregular quadrilateral faces;
// larger cells use five to eight sides. Every profile stays inside its unit
// AABB. BoxCollider remains deliberately approximate at the clipped corners.
inline Bamboo::MeshData makeGlassShardMesh(Bamboo::Vec3 worldSize, std::size_t variant,
                                           std::size_t sideCount = 6) {
    const std::array<float, 3> size = {worldSize.x, worldSize.y, worldSize.z};
    std::array<std::size_t, 3> axes = {0, 1, 2};
    std::stable_sort(axes.begin(), axes.end(), [&](auto a, auto b) { return size[a] < size[b]; });
    const auto thin = axes[0], middle = axes[1], longest = axes[2];
    uint32_t seed = static_cast<uint32_t>(variant) | 1u;
    auto next = [&]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<float>(seed & 0x00ffffffu) / 16777216.f;
    };
    std::vector<Bamboo::Vec2> profile;
    if (sideCount == 3) {
        // Only the smallest, narrow cells use a half-cell triangular profile.
        profile = {{-.5f, -.5f}, {.5f, -.5f}, {-.16f + .32f * next(), .5f}};
    } else if (sideCount == 4) {
        const float a = std::min(.16f + .09f * next(), .04f / std::max(size[middle], 1.e-5f));
        const float b = std::min(.16f + .09f * next(), .04f / std::max(size[middle], 1.e-5f));
        profile = {{-.5f, -.5f + a}, {.5f, -.5f}, {.5f, .5f - b}, {-.5f, .5f}};
    } else {
        sideCount = std::clamp(sideCount, std::size_t{5}, std::size_t{8});
        const std::array<Bamboo::Vec2, 4> corners = {{{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f}, {-.5f, .5f}}};
        const std::size_t firstCut = variant % 4;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const std::size_t relative = (i + 4 - firstCut) % 4;
            const bool cut = sideCount == 6 ? relative % 2 == 0 : relative < sideCount - 4;
            if (!cut) { profile.push_back(corners[i]); continue; }
            const auto p = corners[i];
            const auto before = corners[(i + 3) % 4];
            const auto after = corners[(i + 1) % 4];
            const float horizontal = .3f + .18f * next();
            const float vertical = std::min(.16f + .09f * next(), .045f / std::max(size[middle], 1.e-5f));
            profile.push_back({p.x + (before.x - p.x) * horizontal,
                               p.y + (before.y - p.y) * vertical});
            profile.push_back({p.x + (after.x - p.x) * horizontal,
                               p.y + (after.y - p.y) * vertical});
        }
    }
    // A shallow sloping cap varies thickness across a shard instead of leaving
    // every face as a parallel extrusion. Stronger taper belongs only to the
    // tiny triangular/quad pieces, limiting both volume loss and box mismatch.
    const float taper = sideCount <= 4 ? 1.f : .24f;
    const float slopeU = (.06f + .12f * next()) * taper * (variant % 2 == 0 ? 1.f : -1.f);
    const float slopeV = (.04f + .08f * next()) * taper * ((variant / 2) % 2 == 0 ? 1.f : -1.f);
    float minimumProjection = 1.f;
    for (auto p : profile) { minimumProjection = std::min(minimumProjection, p.x * slopeU + p.y * slopeV); }
    auto point = [&](Bamboo::Vec2 p, float depth) {
        std::array<float, 3> coordinate{};
        coordinate[longest] = variant % 2 == 0 ? p.x : -p.x;
        coordinate[middle] = (variant / 2) % 2 == 0 ? p.y : -p.y;
        coordinate[thin] = depth > 0.f ? depth - (p.x * slopeU + p.y * slopeV - minimumProjection) : depth;
        return Bamboo::Vec3(coordinate[0], coordinate[1], coordinate[2]);
    };
    Bamboo::MeshData mesh;
    mesh.vertices.reserve(160);
    mesh.indices.reserve(432);
    auto face = [&](std::vector<Bamboo::Vec3> corners) {
        const auto n = GlassGeometry::cross(GlassGeometry::subtract(corners[1], corners[0]),
                                            GlassGeometry::subtract(corners[2], corners[0]));
        Bamboo::Vec3 center{};
        for (auto p : corners) { center.x += p.x; center.y += p.y; center.z += p.z; }
        // The origin is strictly inside every profile. This also handles
        // mirrored profiles and either parity of the selected axis permutation.
        if (n.x * center.x + n.y * center.y + n.z * center.z < 0.f) {
            for (std::size_t i = 0; i < corners.size() / 2; ++i) {
                std::swap(corners[i], corners[corners.size() - 1 - i]);
            }
        }
        GlassGeometry::appendFace(mesh, corners);
    };
    std::vector<Bamboo::Vec3> bottom, top;
    for (auto p : profile) { bottom.push_back(point(p, -.5f)); top.push_back(point(p, .5f)); }
    face(bottom);
    face(top);
    for (std::size_t i = 0; i < profile.size(); ++i) {
        const auto j = (i + 1) % profile.size();
        face({bottom[i], bottom[j], top[j], top[i]});
    }
    return mesh;
}

} // namespace SmashLab
