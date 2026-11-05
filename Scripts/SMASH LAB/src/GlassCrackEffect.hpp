#pragma once

#include "GlassFracturePatterns.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace SmashLab {

// Cracks are the exterior traces of the SAME planes that cut the eventual
// fragments. No independent decorative spiderweb or slow drawing animation.
class GlassCrackEffect {
public:
    void prepare(const GlassFracturePattern &pattern, Bamboo::Vec3 sourceScale) {
        reset();
        m_scale = {sourceScale.x, sourceScale.y, sourceScale.z};
        m_segments = pattern.cracks;
        m_revealed.assign(m_segments.size(), false);
    }

    void reveal(Bamboo::Vec3 localHit, float coverage) {
        const glm::vec3 hit(localHit.x, localHit.y, localHit.z);
        if (!std::isfinite(coverage) || m_segments.empty()) { return; }
        std::vector<size_t> order(m_segments.size());
        std::iota(order.begin(), order.end(), size_t{0});
        auto distance = [&](size_t i) {
            const auto &s = m_segments[i];
            const glm::vec3 a(s.from.x, s.from.y, s.from.z), b(s.to.x, s.to.y, s.to.z);
            const glm::vec3 delta = (b - a) * m_scale;
            const glm::vec3 toHit = (hit - a) * m_scale;
            const float t = glm::clamp(glm::dot(toHit, delta) / std::max(glm::dot(delta, delta), 1.e-12f), 0.f, 1.f);
            const glm::vec3 d = toHit - delta * t;
            return glm::dot(d, d);
        };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return distance(a) < distance(b); });
        const size_t count = std::min(order.size(), std::max(size_t{1},
            static_cast<size_t>(std::ceil(std::clamp(coverage, 0.f, 1.f) * order.size()))));
        for (size_t i = 0; i < count; ++i) { m_revealed[order[i]] = true; }
    }

    void appendTo(Bamboo::MeshData &mesh, float physicalWidth = .0016f) const {
        for (size_t i = 0; i < m_segments.size(); ++i) {
            if (!m_revealed[i]) { continue; }
            const auto &s = m_segments[i];
            appendStrip(mesh, {s.from.x, s.from.y, s.from.z}, {s.to.x, s.to.y, s.to.z},
                        {s.normal.x, s.normal.y, s.normal.z}, std::clamp(physicalWidth, .0008f, .006f));
        }
    }

    void reset() { m_segments.clear(); m_revealed.clear(); }
    size_t segmentCount() const { return m_segments.size(); }
    size_t revealedCount() const { return static_cast<size_t>(std::count(m_revealed.begin(), m_revealed.end(), true)); }

private:
    void appendStrip(Bamboo::MeshData &mesh, glm::vec3 from, glm::vec3 to, glm::vec3 normal, float width) const {
        const glm::vec3 direction = (to - from) * m_scale;
        const float length = glm::length(direction);
        if (!std::isfinite(length) || length < 1.e-7f) { return; }
        const glm::vec3 offset = glm::normalize(glm::cross(normal, direction / length)) * (.5f * width) / m_scale;
        std::array<glm::vec3, 4> corners = {from - offset, to - offset, to + offset, from + offset};
        for (auto &p : corners) {
            for (int axis = 0; axis < 3; ++axis) {
                p[axis] = std::abs(normal[axis]) > .5f ? normal[axis] * .50035f : glm::clamp(p[axis], -.5f, .5f);
            }
        }
        const bool a = glm::dot(glm::cross(corners[1] - corners[0], corners[2] - corners[0]), normal) > 1.e-13f;
        const bool b = glm::dot(glm::cross(corners[2] - corners[0], corners[3] - corners[0]), normal) > 1.e-13f;
        if (!a && !b) { return; }
        const auto first = static_cast<uint32_t>(mesh.vertices.size());
        for (auto p : corners) {
            Bamboo::Vertex v{};
            v.pos = {p.x, p.y, p.z};
            v.normal = {normal.x, normal.y, normal.z};
            v.color = {1.f, 1.f, 1.f, .85f};
            v.light = 2.2f;
            mesh.vertices.push_back(v);
        }
        if (a) { mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2}); }
        if (b) { mesh.indices.insert(mesh.indices.end(), {first, first + 2, first + 3}); }
    }

    std::vector<GlassCrackSegment> m_segments;
    std::vector<bool> m_revealed;
    glm::vec3 m_scale{1.f};
};

} // namespace SmashLab
