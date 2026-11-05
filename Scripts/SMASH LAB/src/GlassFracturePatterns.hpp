#pragma once

#include "DestructionData.hpp"
#include "DestructionGeometry.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace SmashLab {

// These are actual boundaries between the generated shards on the source
// cube's exterior, not an independently drawn decorative crack pattern.
struct GlassCrackSegment {
    Bamboo::Vec3 from, to, normal;
};

struct GlassFracturePattern {
    std::vector<FragmentData> fragments;
    std::vector<Bamboo::MeshData> meshes;
    std::vector<GlassCrackSegment> cracks;
    float colliderVolumeFraction = 0.f;
    float visualVolumeFraction = 0.f;
};

namespace GlassFracture {

using Point = glm::dvec3;
struct Face {
    std::vector<Point> vertices;
    Point normal;
    bool exterior = false;
};
using Cell = std::vector<Face>;

inline uint32_t mix(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
inline Point point(Bamboo::Vec3 p) { return {p.x, p.y, p.z}; }
inline Bamboo::Vec3 point(Point p) { return {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)}; }
inline bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

inline void clean(std::vector<Point> &vertices) {
    constexpr double epsilon2 = 1.e-20;
    std::vector<Point> result;
    for (Point p : vertices) {
        if (result.empty() || glm::dot(p - result.back(), p - result.back()) > epsilon2) { result.push_back(p); }
    }
    if (result.size() > 1 && glm::dot(result.front() - result.back(), result.front() - result.back()) < epsilon2) {
        result.pop_back();
    }
    bool changed = true;
    while (changed && result.size() >= 3) {
        changed = false;
        for (std::size_t i = 0; i < result.size(); ++i) {
            const Point a = result[i] - result[(i + result.size() - 1) % result.size()];
            const Point b = result[(i + 1) % result.size()] - result[i];
            const auto cross = glm::cross(a, b);
            if (glm::dot(cross, cross) < 1.e-22) {
                result.erase(result.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    vertices = std::move(result);
}

inline Cell cube() {
    Cell result;
    const std::array<Point, 6> normals = {{{0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}}};
    const std::array<Point, 6> us = {{{1,0,0}, {-1,0,0}, {0,0,-1}, {0,0,1}, {1,0,0}, {1,0,0}}};
    for (std::size_t i = 0; i < normals.size(); ++i) {
        const Point n = normals[i], u = us[i], v = glm::cross(n, u);
        result.push_back({{n*.5-u*.5-v*.5, n*.5+u*.5-v*.5, n*.5+u*.5+v*.5, n*.5-u*.5+v*.5}, n, true});
    }
    return result;
}

inline double volume(const Cell &cell) {
    double result = 0.;
    for (const Face &face : cell) {
        for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
            result += glm::dot(face.vertices[0], glm::cross(face.vertices[i], face.vertices[i + 1])) / 6.;
        }
    }
    return result;
}
inline Point center(const Cell &cell) {
    Point total(0.);
    double count = 0.;
    for (const Face &face : cell) { for (Point p : face.vertices) { total += p; count += 1.; } }
    return total / std::max(1., count);
}
inline std::pair<Point, Point> bounds(const Cell &cell) {
    Point low(1.), high(-1.);
    for (const Face &face : cell) { for (Point p : face.vertices) { low = glm::min(low, p); high = glm::max(high, p); } }
    return {low, high};
}

// Distance to the actual convex solid, not its AABB: an oblique shard's bounds
// can contain the impact even when the shard itself is far from that point.
// Project onto each face; outside its polygon, the closest point is on an edge.
inline double squaredDistanceToCell(const Cell &cell, Point query) {
    if (cell.empty()) { return std::numeric_limits<double>::infinity(); }
    bool inside = true;
    for (const Face &face : cell) {
        if (face.vertices.empty()) { continue; }
        if (glm::dot(face.normal, query-face.vertices[0]) > 1.e-11) { inside = false; break; }
    }
    if (inside) { return 0.; }
    double closest = std::numeric_limits<double>::infinity();
    for (const Face &face : cell) {
        if (face.vertices.size() < 3) { continue; }
        const double normalSquared = glm::dot(face.normal, face.normal);
        if (normalSquared <= 1.e-20) { continue; }
        const double offset = glm::dot(face.normal, query-face.vertices[0]) / normalSquared;
        const Point projected = query-face.normal*offset;
        bool inPolygon = true;
        for (std::size_t i = 0; i < face.vertices.size(); ++i) {
            const Point a = face.vertices[i], b = face.vertices[(i+1)%face.vertices.size()];
            if (glm::dot(glm::cross(b-a, projected-a), face.normal) < -1.e-11) { inPolygon = false; }
            const Point edge = b-a;
            const double lengthSquared = glm::dot(edge, edge);
            const double t = lengthSquared > 1.e-20 ? std::clamp(glm::dot(query-a, edge)/lengthSquared, 0., 1.) : 0.;
            const Point delta = query-(a+edge*t);
            closest = std::min(closest, glm::dot(delta,delta));
        }
        if (inPolygon) { closest = std::min(closest, offset*offset*normalSquared); }
    }
    return closest;
}

// Keep n.p <= distance. Clipping is limited to the current convex cube pieces;
// this is not an arbitrary mesh Boolean or a replacement physics solver.
inline Cell clipped(const Cell &cell, Point n, double distance) {
    Cell result;
    std::vector<Point> cap;
    for (const Face &face : cell) {
        std::vector<Point> vertices;
        for (std::size_t i = 0; i < face.vertices.size(); ++i) {
            const Point a = face.vertices[i], b = face.vertices[(i + 1) % face.vertices.size()];
            const double da = glm::dot(n, a) - distance, db = glm::dot(n, b) - distance;
            const bool insideA = da <= 1.e-11, insideB = db <= 1.e-11;
            if (insideA) { vertices.push_back(a); }
            if (insideA != insideB && std::abs(da - db) > 1.e-15) {
                const Point intersection = a + (b - a) * (da / (da - db));
                vertices.push_back(intersection);
                bool duplicate = false;
                for (Point p : cap) {
                    if (glm::dot(p - intersection, p - intersection) < 1.e-18) { duplicate = true; break; }
                }
                if (!duplicate) { cap.push_back(intersection); }
            }
        }
        clean(vertices);
        if (vertices.size() >= 3) { result.push_back({std::move(vertices), face.normal, face.exterior}); }
    }
    if (cap.size() >= 3) {
        Point c(0.);
        for (Point p : cap) { c += p / static_cast<double>(cap.size()); }
        const Point u = glm::normalize(glm::cross(std::abs(n.y) < .8 ? Point(0,1,0) : Point(1,0,0), n));
        const Point v = glm::cross(n, u);
        std::sort(cap.begin(), cap.end(), [&](Point a, Point b) {
            return std::atan2(glm::dot(a-c, v), glm::dot(a-c, u)) < std::atan2(glm::dot(b-c, v), glm::dot(b-c, u));
        });
        clean(cap);
        if (cap.size() >= 3) { result.push_back({std::move(cap), n, false}); }
    }
    return result;
}

inline void appendExteriorCuts(std::vector<GlassCrackSegment> &cracks, const Cell &cell, Point n, double distance) {
    for (const Face &face : cell) {
        if (!face.exterior) { continue; }
        std::vector<Point> intersections;
        auto append = [&](Point p) {
            for (Point old : intersections) { if (glm::dot(old-p, old-p) < 1.e-18) { return; } }
            intersections.push_back(p);
        };
        for (std::size_t i = 0; i < face.vertices.size(); ++i) {
            Point a = face.vertices[i], b = face.vertices[(i + 1) % face.vertices.size()];
            const double da = glm::dot(n, a)-distance, db = glm::dot(n, b)-distance;
            if (std::abs(da) < 1.e-10) { append(a); }
            if (da * db < 0.) { append(a + (b-a) * (da/(da-db))); }
        }
        if (intersections.size() < 2) { continue; }
        Point a = intersections[0], b = intersections[1];
        double best = glm::dot(a-b, a-b);
        for (Point p : intersections) { for (Point q : intersections) {
            const double d = glm::dot(p-q, p-q);
            if (d > best) { a = p; b = q; best = d; }
        } }
        if (best > 1.e-12) { cracks.push_back({point(a), point(b), point(face.normal)}); }
    }
}

// Several independently intersected planes can meet within a few microns.
// Weld that shared corner consistently across ALL faces before triangulation;
// otherwise dropping only its tiny cap would leave a microscopic open edge.
inline Cell canonicalized(const Cell &source) {
    std::vector<Point> points;
    Cell result;
    for (const Face &face : source) {
        std::vector<Point> vertices;
        for (Point p : face.vertices) {
            bool found = false;
            for (Point old : points) {
                if (glm::dot(old-p, old-p) < 4.e-12) { vertices.push_back(old); found = true; break; }
            }
            if (!found) { points.push_back(p); vertices.push_back(p); }
        }
        clean(vertices);
        if (vertices.size() < 3) { continue; }
        result.push_back({std::move(vertices),face.normal,face.exterior});
    }
    return result;
}

struct Box { Point center, halfSize; double volume = 0.; };

// Conservative contact proxies must stay close to their visible hull. Long
// acute splinters whose only safe box is a tiny remote core are rejected.
inline double contactGap(const Cell &cell, const Box &box) {
    double gap = 0.;
    for (const Face &face : cell) { for (Point p : face.vertices) {
        const Point nearest = glm::clamp(p, box.center-box.halfSize, box.center+box.halfSize);
        gap = std::max(gap, glm::length(p-nearest));
    } }
    return gap;
}

// Largest tested interior AABB. Every corner is inside every convex face, so
// neighboring collider boxes cannot initially intersect. This is conservative
// contact geometry, not the visible hull; density/mass setters are unavailable.
inline Box inscribedBox(const Cell &cell) {
    const auto [low, high] = bounds(cell);
    const Point average = center(cell);
    Box best{};
    auto candidate = [&](Point c, Point ratio) {
        double multiplier = 1.e20;
        for (const Face &face : cell) {
            const double slack = glm::dot(face.normal, face.vertices[0] - c);
            if (slack <= 1.e-9) { return; }
            multiplier = std::min(multiplier, slack / glm::dot(glm::abs(face.normal), ratio));
        }
        Point half = ratio * multiplier * .985;
        auto keep = [&]() {
            const double v = 8. * half.x * half.y * half.z;
            if (v > best.volume) { best = {c, half, v}; }
        };
        keep();
        // Coordinate ascent over box extents remains within all face planes.
        for (int pass = 0; pass < 4; ++pass) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                double maximum = 1.e20;
                for (const Face &face : cell) {
                    const Point a = glm::abs(face.normal);
                    if (a[axis] < 1.e-12) { continue; }
                    const double other = glm::dot(a, half) - a[axis] * half[axis];
                    const double slack = glm::dot(face.normal, face.vertices[0] - c);
                    maximum = std::min(maximum, (slack - other) / a[axis]);
                }
                half[axis] = std::max(1.e-9, maximum * .99);
            }
            keep();
        }
    };
    const Point size = high-low;
    for (double x : {.28, .5, .72}) { for (double y : {.28, .5, .72}) { for (double z : {.28, .5, .72}) {
        const Point c = average * .4 + (low + size * Point(x,y,z)) * .6;
        candidate(c, glm::max(size, Point(1.e-6)));
        candidate(c, Point(1.));
    } } }
    candidate(average, glm::max(size, Point(1.e-6)));
    return best;
}

inline Bamboo::MeshData mesh(const Cell &cell, const Box &box) {
    Bamboo::MeshData result;
    for (const Face &face : cell) {
        Point center(0.);
        for (Point p : face.vertices) { center += p / static_cast<double>(face.vertices.size()); }
        // A centre fan avoids skinny fan triangles along nearly collinear cut
        // edges. Flat per-triangle normals also respect the final welded mesh.
        for (std::size_t i = 0; i < face.vertices.size(); ++i) {
            const std::array<Point, 3> triangle = {center, face.vertices[i], face.vertices[(i+1)%face.vertices.size()]};
            const std::array<Bamboo::Vec3, 3> local = {point((triangle[0]-box.center)/(box.halfSize*2.)),
                                                       point((triangle[1]-box.center)/(box.halfSize*2.)),
                                                       point((triangle[2]-box.center)/(box.halfSize*2.))};
            Point normal = glm::cross(point(local[1])-point(local[0]),point(local[2])-point(local[0]));
            if (glm::dot(normal,normal) < 1.e-24) { return {}; }
            normal = glm::normalize(normal);
            const auto first = static_cast<uint32_t>(result.vertices.size());
            for (Bamboo::Vec3 p : local) {
                Bamboo::Vertex vertex{};
                vertex.pos = p;
                vertex.normal = point(normal);
                vertex.color = glassSurfaceVertexColor(false);
                vertex.light = 1.f;
                result.vertices.push_back(vertex);
            }
            result.indices.insert(result.indices.end(),{first,first+1,first+2});
        }
    }
    return result;
}

} // namespace GlassFracture

// Fresh oblique plane cuts exactly partition the authored cube. The recorded
// exterior cut segments are the SAME boundaries later visible on the shards.
inline GlassFracturePattern makeImpactGlassFracturePattern(const std::vector<FragmentData> &nodeBindings,
                                                          Bamboo::Vec3 sourceScale, Bamboo::Vec3 localHit,
                                                          uint32_t seed, Bamboo::Vec3 localIncomingDirection = {}) {
    using namespace GlassFracture;
    const Point scale = point(sourceScale), hit = glm::clamp(point(localHit), Point(-.5), Point(.5));
    const Point incoming = point(localIncomingDirection);
    if ((nodeBindings.size() < 10 || nodeBindings.size() > 24) || !finite(scale) || glm::any(glm::lessThanEqual(scale, Point(0.))) ||
        !finite(point(localHit)) || !finite(incoming)) { return {}; }
    std::size_t depth = 0;
    for (std::size_t axis = 1; axis < 3; ++axis) {
        const double error = std::abs(hit[axis]) - std::abs(hit[depth]);
        if (error > 1.e-5 || (std::abs(error) <= 1.e-5 && std::abs(incoming[axis]) > std::abs(incoming[depth]))) { depth = axis; }
    }
    Point normal(0.); normal[depth] = hit[depth] >= 0. ? 1. : -1.;
    Point u(0.); u[(depth+1)%3] = 1.;
    const Point v = glm::cross(normal, u);
    auto next = [&]() {
        seed = mix(seed + 0x9e3779b9u);
        return static_cast<double>(seed & 0xffffffu) / 16777216.;
    };
    std::vector<Cell> cells{cube()};
    std::vector<bool> blocked(1, false);
    GlassFracturePattern result;
    while (cells.size() < nodeBindings.size()) {
        const std::size_t cut = cells.size()-1;
        std::size_t selected = cells.size();
        double bestScore = -1.;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (blocked[i]) { continue; }
            const double cellVolume = volume(cells[i]);
            if (cellVolume < .003) { continue; }
            const double distance2 = squaredDistanceToCell(cells[i],hit);
            const double score = cellVolume * (1. + 5. * std::exp(-distance2 / .09)) * (.8 + .4 * next());
            if (score > bestScore) { selected = i; bestScore = score; }
        }
        if (selected == cells.size()) { return {}; }
        bool accepted = false;
        for (int attempt = 0; attempt < 120 && !accepted; ++attempt) {
            const Cell &cell = cells[selected];
            const Point c = center(cell);
            const double angle = next() * 6.283185307179586;
            Point n;
            const bool anchorToHit = cut == 0 && attempt < 64;
            if (anchorToHit) {
                // Start a crack at the contact rather than cutting a layer
                // behind the entry face. Large radial angles are tried first;
                // near-tangent-axis candidates retain safe box contact proxies.
                if (attempt < 32) {
                    n = glm::normalize(u * std::cos(angle) + v * std::sin(angle) + normal * ((next()-.5)*.35));
                } else {
                    n = u * ((next()-.5)*.22) + v * ((next()-.5)*.22) + normal * ((next()-.5)*.12);
                    n += attempt % 2 == 0 ? u : v;
                    n = glm::normalize(n);
                }
            } else if (cut == 0 && attempt < 76) {
                n = glm::normalize(normal + u * ((next()-.5)*.7) + v * ((next()-.5)*.7));
            } else if (attempt < 50) {
                n = glm::normalize(u * std::cos(angle) + v * std::sin(angle) + normal * ((next()-.5)*1.2));
            } else {
                // A bounded conditioning fallback still uses oblique cuts,
                // but avoids arbitrarily acute cells when the hull is large.
                n = Point((next()-.5)*.22, (next()-.5)*.22, (next()-.5)*.22);
                n[static_cast<std::size_t>(next()*3.) % 3] = 1.;
                n = glm::normalize(n);
            }
            double minimum = 1.e20, maximum = -1.e20;
            for (const Face &face : cell) { for (Point p : face.vertices) {
                const double projection = glm::dot(n,p);
                minimum = std::min(minimum, projection); maximum = std::max(maximum, projection);
            } }
            const Point focus = cut == 0 ? hit - normal * (.10 + .10 * next()) : glm::mix(c, hit, .15 + .25 * next());
            const double fraction = std::clamp((glm::dot(n,focus)-minimum) / (maximum-minimum) + (next()-.5)*.12, .18, .82);
            // Exact edge/corner contacts may not permit a well-conditioned
            // anchored cut. After the bounded attempts, retain the safe fallback.
            const double d = anchorToHit ? glm::dot(n,hit) : minimum + (maximum-minimum)*fraction;
            Cell a = clipped(cell,n,d), b = clipped(cell,-n,-d);
            const double va = volume(a), vb = volume(b), oldVolume = volume(cell);
            if (va < std::max(.0008, oldVolume*.09) || vb < std::max(.0008, oldVolume*.09) ||
                std::abs(va+vb-oldVolume) > 1.e-8) { continue; }
            const Box boxA = inscribedBox(a), boxB = inscribedBox(b);
            if (boxA.volume < .00008 || boxB.volume < .00008 ||
                std::min({boxA.halfSize.x, boxA.halfSize.y, boxA.halfSize.z}) < .004 || std::min({boxB.halfSize.x, boxB.halfSize.y, boxB.halfSize.z}) < .004) { continue; }
            const auto quality = [](const Cell &child, const Box &box, double childVolume) {
                const double ratio = childVolume > .015 ? .30 : .22;
                return box.volume >= childVolume*ratio && contactGap(child,box) <= .08;
            };
            if (!quality(a,boxA,va) || !quality(b,boxB,vb)) { continue; }
            appendExteriorCuts(result.cracks,cell,n,d);
            cells[selected] = std::move(a);
            cells.push_back(std::move(b));
            blocked[selected] = false;
            blocked.push_back(false);
            accepted = true;
        }
        if (!accepted) { blocked[selected] = true; }
    }
    // Closest exterior boundaries are revealed first for low-energy cracks.
    std::stable_sort(result.cracks.begin(),result.cracks.end(),[&](const GlassCrackSegment &a,const GlassCrackSegment &b) {
        const Point ca = (point(a.from)+point(a.to))*.5-hit, cb = (point(b.from)+point(b.to))*.5-hit;
        return glm::dot(ca,ca) < glm::dot(cb,cb);
    });
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const Cell cell = canonicalized(cells[i]);
        const Box box = inscribedBox(cell);
        if (!finite(box.center) || !finite(box.halfSize) || box.volume <= 0.) { return {}; }
        result.fragments.push_back({nodeBindings[i].tag,point(box.center),point(box.halfSize*2.)});
        auto generated = mesh(cell,box);
        if (generated.indices.empty()) { return {}; }
        result.meshes.push_back(std::move(generated));
        result.colliderVolumeFraction += static_cast<float>(box.volume);
        result.visualVolumeFraction += static_cast<float>(volume(cell));
    }
    return result;
}

} // namespace SmashLab
