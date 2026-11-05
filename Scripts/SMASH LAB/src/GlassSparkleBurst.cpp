#include "GlassSparkleBurst.hpp"

#include <Bamboo/Assets/AssetManagerAPI.hpp>
#include <Bamboo/Assets/TextureAPI.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace SmashLab {
namespace {

constexpr float kFlashLifetime = .08f;
constexpr uint32_t kTileSize = 64;
constexpr uint32_t kFrames = 16;

glm::vec3 vector(Bamboo::Vec3 value) { return {value.x, value.y, value.z}; }
Bamboo::Vec3 vector(glm::vec3 value) { return {value.x, value.y, value.z}; }

float random01(uint32_t &seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return static_cast<float>(seed & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

glm::vec3 unit(glm::vec3 value, glm::vec3 fallback) {
    const float length = glm::length(value);
    return std::isfinite(length) && length > 1.e-5f ? value / length : fallback;
}

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void appendBillboard(Bamboo::MeshData &mesh, glm::vec3 center, glm::vec3 right, glm::vec3 up,
                     glm::vec3 normal, float width, float height, float phase, float brightness) {
    const uint32_t frame = std::min(kFrames - 1, static_cast<uint32_t>(std::clamp(phase, 0.f, 1.f) * (kFrames - 1)));
    const float tileX = static_cast<float>((frame % 4) * kTileSize);
    const float tileY = static_cast<float>((frame / 4) * kTileSize);
    // Half-texel inset isolates each frame from its neighbours under filtering.
    const float left = (tileX + .5f) / GlassSparkleBurst::kTextureWidth;
    const float rightUV = (tileX + kTileSize - .5f) / GlassSparkleBurst::kTextureWidth;
    const float bottom = (tileY + .5f) / GlassSparkleBurst::kTextureHeight;
    const float top = (tileY + kTileSize - .5f) / GlassSparkleBurst::kTextureHeight;
    const std::array<Bamboo::Vec2, 4> uv = {{{left, bottom}, {rightUV, bottom}, {rightUV, top}, {left, top}}};
    const std::array<glm::vec2, 4> corners = {{{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f}, {-.5f, .5f}}};
    const auto first = static_cast<uint32_t>(mesh.vertices.size());
    for (std::size_t i = 0; i < corners.size(); ++i) {
        Bamboo::Vertex vertex;
        vertex.pos = vector(center + right * (corners[i].x * width) + up * (corners[i].y * height));
        vertex.uv = uv[i];
        vertex.normal = vector(normal);
        vertex.color = {1.f, 1.f, 1.f, 1.f};
        // The existing PBR shader multiplies lit RGB by input_fragLight. This
        // makes a small glint bright without adding a light or shader pass.
        vertex.light = brightness;
        vertex.id = 0;
        mesh.vertices.push_back(vertex);
    }
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first + 2, first + 3, first});
}

// Small, genuinely polygonal visual chips complement the soft glints.
// They share the atlas's alpha ramp with the physical glass, but have no body.
void appendChip(Bamboo::MeshData &mesh, glm::vec3 center, glm::vec3 right, glm::vec3 up,
                float width, float height, float thickness, float phase, float brightness,
                const std::array<Bamboo::Vec2, 5> &corners, uint32_t count) {
    const glm::vec3 normal = glm::normalize(glm::cross(right, up));
    const float coverage = (1.f - phase) * (1.f - phase) * .92f;
    // A closed chip has entry and exit interfaces. Split the old single-sheet
    // coverage between them in the optically thin approximation, rather than
    // doubling its apparent opacity. Round down to the atlas ramp so even its
    // quantization cannot increase the pair's coverage. No material alpha is
    // assumed or modified; this is not a thickness/refraction shader model.
    const float alphaSteps = static_cast<float>(GlassSparkleBurst::kTextureHeight - 1);
    const float faceCoverage = thickness > 0.f ? std::floor(coverage * alphaSteps * .5f) / alphaSteps : coverage;
    const auto uv = GlassSparkleBurst::glassUV(faceCoverage);
    auto appendVertex = [&](glm::vec3 position, glm::vec3 faceNormal) {
        Bamboo::Vertex vertex{};
        vertex.pos = vector(position);
        vertex.uv = uv;
        vertex.normal = vector(faceNormal);
        // The shared material supplies the tint; only actual face orientation
        // changes lighting between the front, back and thin broken edges.
        vertex.color = {1.f, 1.f, 1.f, 1.f};
        vertex.light = brightness;
        mesh.vertices.push_back(vertex);
    };
    if (thickness > 0.f) {
        std::array<glm::vec3, 5> profile;
        for (uint32_t i = 0; i < count; ++i) {
            profile[i] = center + right * (corners[i].x * width) + up * (corners[i].y * height);
        }
        const glm::vec3 halfDepth = normal * (.5f * thickness);
        const auto front = static_cast<uint32_t>(mesh.vertices.size());
        for (uint32_t i = 0; i < count; ++i) { appendVertex(profile[i] + halfDepth, normal); }
        const auto back = static_cast<uint32_t>(mesh.vertices.size());
        for (uint32_t i = 0; i < count; ++i) { appendVertex(profile[i] - halfDepth, -normal); }
        for (uint32_t i = 1; i + 1 < count; ++i) {
            mesh.indices.insert(mesh.indices.end(), {front, front + i, front + i + 1,
                                                    back, back + i + 1, back + i});
        }
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t j = (i + 1) % count;
            const glm::vec3 sideNormal = glm::normalize(glm::cross(profile[j] - profile[i], normal));
            const auto side = static_cast<uint32_t>(mesh.vertices.size());
            appendVertex(profile[i] - halfDepth, sideNormal);
            appendVertex(profile[j] - halfDepth, sideNormal);
            appendVertex(profile[j] + halfDepth, sideNormal);
            appendVertex(profile[i] + halfDepth, sideNormal);
            mesh.indices.insert(mesh.indices.end(), {side, side + 1, side + 2, side, side + 2, side + 3});
        }
        return;
    }
    const auto first = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t i = 0; i < count; ++i) {
        appendVertex(center + right * (corners[i].x * width) + up * (corners[i].y * height), normal);
    }
    for (uint32_t i = 1; i + 1 < count; ++i) {
        mesh.indices.insert(mesh.indices.end(), {first, first + i, first + i + 1});
    }
}

} // namespace

bool GlassSparkleBurst::initialize() {
    if (m_texture.isValid()) { return true; }
    std::vector<unsigned char> pixels(kTextureWidth * kTextureHeight * 4, 255);
    for (uint32_t y = 0; y < kTextureHeight; ++y) {
        for (uint32_t x = 0; x < kTextureWidth; ++x) {
            float alpha;
            if (x >= 256) {
                alpha = static_cast<float>(y) / 255.f;
            } else {
                const uint32_t frame = (y / kTileSize) * 4 + x / kTileSize;
                const float px = (static_cast<float>(x % kTileSize) + .5f) / kTileSize * 2.f - 1.f;
                const float py = (static_cast<float>(y % kTileSize) + .5f) / kTileSize * 2.f - 1.f;
                const float radius2 = px * px + py * py;
                const float core = std::exp(-radius2 * 7.f) * .7f;
                const float streaks = (std::exp(-(px * px * 140.f + py * py * 12.f)) +
                                       std::exp(-(py * py * 140.f + px * px * 12.f))) * .18f;
                const float border = std::clamp((.96f - std::sqrt(radius2)) * 8.f, 0.f, 1.f);
                const float fade = 1.f - static_cast<float>(frame) / (kFrames - 1);
                alpha = std::clamp((core + streaks) * border, 0.f, 1.f) * fade * fade;
            }
            pixels[(y * kTextureWidth + x) * 4 + 3] = static_cast<unsigned char>(std::lround(alpha * 255.f));
        }
    }
    m_texture = Bamboo::AssetManagerAPI::createTextureRGBA8(kTextureWidth, kTextureHeight);
    if (!m_texture.isValid()) { return false; }
    Bamboo::TextureAPI::updateRGBA8(m_texture, pixels.data(), static_cast<uint32_t>(pixels.size()));
    reset();
    return true;
}

Bamboo::Vec2 GlassSparkleBurst::glassUV(float alpha) {
    if (!std::isfinite(alpha)) { alpha = 0.f; }
    const float row = std::round(std::clamp(alpha, 0.f, 1.f) * 255.f);
    return {272.5f / kTextureWidth, (row + .5f) / kTextureHeight};
}

void GlassSparkleBurst::trigger(Bamboo::Vec3 point, Bamboo::Vec3 direction, Bamboo::Vec3 inheritedVelocity,
                               uint32_t seed, float severity) {
    if (!m_texture.isValid()) { return; }
    reset();
    if (!finite(vector(point)) || !finite(vector(direction)) || !finite(vector(inheritedVelocity)) ||
        !std::isfinite(severity) || severity <= 0.f) {
        reset();
        return;
    }
    severity = std::clamp(severity, 0.f, 1.f);
    const glm::vec3 axis = unit(vector(direction), {0.f, 0.f, -1.f});
    const glm::vec3 side = unit(glm::cross(axis, glm::vec3(0.f, 1.f, 0.f)), {1.f, 0.f, 0.f});
    const glm::vec3 up = unit(glm::cross(side, axis), {0.f, 1.f, 0.f});
    if (seed == 0) { seed = 0x51a55e31u; }
    for (std::size_t i = 0; i < m_particles.size(); ++i) {
        Particle &particle = m_particles[i];
        // Sample the entire mixture even for a moderate hit, rather than
        // truncating the array and accidentally dropping all dust or chips.
        if (random01(seed) > .2f + .8f * severity) { continue; }
        particle.kind = i < 80 ? ParticleKind::Point : i < 128 ? ParticleKind::Streak :
                        i < 224 ? ParticleKind::TriangleChip : i < 288 ? ParticleKind::QuadChip :
                        i < 320 ? ParticleKind::PentagonChip : ParticleKind::Dust;
        const bool dust = particle.kind == ParticleKind::Dust;
        const bool chip = i >= 128 && !dust;
        const float angle = random01(seed) * 6.28318530718f;
        const float sizeSample = random01(seed);
        // Overlapping size bands bridge the previous gap between large solver
        // shards and subpixel flecks. Actual chip geometry stays world sized.
        const auto sizeBand = chip ? (i - 128) % 4 : 0;
        particle.size = chip ? (sizeBand == 0 ? .012f + sizeSample * sizeSample * .043f :
                               sizeBand == 3 ? .0008f + sizeSample * .0047f :
                               .004f + sizeSample * sizeSample * .022f) :
                        dust ? .00025f + sizeSample * .0018f :
                        particle.kind == ParticleKind::Streak ? .0015f + sizeSample * .0025f :
                        .001f + sizeSample * .004f;
        if (chip) {
            const float physicalThickness = std::clamp(particle.size * .035f, .00008f, .001f);
            // The two larger bands have explicit volume. Submillimetre flecks
            // keep their cheaper polygon without adding extra geometry.
            particle.thickness = sizeBand < 2 ? physicalThickness : 0.f;
            // Area/mass rises as glass gets thinner: tiny chips lose speed
            // sooner than broad heavy chips. This remains cosmetic drag.
            particle.drag = std::clamp(.4f * (.0006f / physicalThickness), .25f, 3.f);
        }
        const float sizeResponse = chip ? std::clamp(std::sqrt(.009f / particle.size), .65f, 1.8f) : 1.f;
        const float speedResponse = .4f + .6f * severity;
        const float spread = (.22f + random01(seed) * (dust ? 1.1f : 1.5f)) * sizeResponse * speedResponse;
        const glm::vec3 radial = side * std::cos(angle) + up * std::sin(angle);
        // A compact burst originates at the entry surface and slightly along
        // the incoming direction, rather than spawning a halo in front of it.
        particle.position = vector(vector(point) + axis * (random01(seed) * .006f) +
                                   radial * (random01(seed) * (dust ? .018f : .012f)));
        // Chips inherit the moving impact point before aerodynamic damping.
        particle.velocity = vector(vector(inheritedVelocity) * (chip ? 1.f : .3f) +
                                   axis * ((.4f + random01(seed) * (dust ? 1.4f : 1.8f)) * sizeResponse * speedResponse) + radial * spread);
        particle.age = 0.f;
        particle.lifetime = chip ? .35f + random01(seed) * .7f : dust ? .22f + random01(seed) * .45f :
                            .12f + random01(seed) * .35f;
        particle.stretch = particle.kind == ParticleKind::Streak ? 3.2f + random01(seed) * 1.8f :
                           chip ? .8f + random01(seed) * 2.4f : 1.f;
        if (chip) { particle.stretch = std::min(particle.stretch, .09f / particle.size); }
        particle.angle = angle;
        particle.angularSpeed = (random01(seed) * 2.f - 1.f) * (chip ? 10.f : 2.5f);
        particle.brightness = chip ? 2.f + random01(seed) * 2.5f : dust ? 1.4f + random01(seed) * 1.2f :
                              4.f + random01(seed) * 4.f;
        particle.twinklePhase = random01(seed) * 6.28318530718f;
        if (chip) {
            const uint32_t count = particle.kind == ParticleKind::TriangleChip ? 3u :
                                   particle.kind == ParticleKind::QuadChip ? 4u : 5u;
            // Ordered points on a circle remain convex. An affine shear and
            // unequal arc spacing vary the actual silhouette, not just its spin.
            const float shear = (random01(seed) * 2.f - 1.f) * .3f;
            const float phase = random01(seed) * 6.28318530718f;
            for (uint32_t corner = 0; corner < count; ++corner) {
                const float theta = phase + 6.28318530718f * static_cast<float>(corner) / count +
                                    (random01(seed) * 2.f - 1.f) * .22f;
                const float y = std::sin(theta) * .5f;
                particle.outline[corner] = {std::cos(theta) * .5f + shear * y, y};
            }
            const glm::quat tilt = glm::angleAxis((random01(seed) * 2.f - 1.f) * 1.2f, radial);
            particle.right = vector(tilt * (side * std::cos(angle) + up * std::sin(angle)));
            particle.up = vector(tilt * (up * std::cos(angle) - side * std::sin(angle)));
            particle.spinAxis = vector(unit(glm::vec3(random01(seed) * 2.f - 1.f,
                                                      random01(seed) * 2.f - 1.f,
                                                      random01(seed) * 2.f - 1.f), axis));
        }
    }
    m_flashPosition = vector(vector(point) - axis * .025f);
    m_flashAge = 0.f;
    m_flashStrength = severity;
    m_pendingBirth = true;
}

void GlassSparkleBurst::appendTo(Bamboo::MeshData &mesh, Bamboo::Vec3 cameraPosition,
                                Bamboo::Quat cameraRotation, float deltaTime,
                                float viewportHeight, float verticalFovDegrees) {
    if (!m_texture.isValid()) { return; }
    // A long frame expires short effects immediately instead of simulating a
    // separate catch-up loop. Cosmetic gravity never enters the Box3D world.
    // trigger runs at the fixed boundary, while this is a frame callback. Do
    // not age a new burst by the entire preceding frame before its first draw.
    const float elapsed = m_pendingBirth ? 0.f : std::isfinite(deltaTime) ? std::max(deltaTime, 0.f) : 0.f;
    glm::quat rotation(cameraRotation.w, cameraRotation.x, cameraRotation.y, cameraRotation.z);
    const float rotationLength = glm::length(rotation);
    if (!std::isfinite(rotationLength) || rotationLength < 1.e-5f) { return; }
    rotation /= rotationLength;
    m_pendingBirth = false;
    const glm::vec3 right = rotation * glm::vec3(1.f, 0.f, 0.f);
    const glm::vec3 up = rotation * glm::vec3(0.f, 1.f, 0.f);
    const glm::vec3 normal = rotation * glm::vec3(0.f, 0.f, 1.f);
    // A geometric millimetre chip must not turn into a centimetre chip just
    // because the camera moves away. Only optical glints and dust scattering
    // get a bounded footprint so a real viewport can actually resolve them.
    const bool pixelFootprint = finite(vector(cameraPosition)) && std::isfinite(viewportHeight) && viewportHeight > 0.f &&
                                std::isfinite(verticalFovDegrees) && verticalFovDegrees > 1.f && verticalFovDegrees < 179.f;
    const float pixelScale = pixelFootprint ? 2.f * std::tan(glm::radians(verticalFovDegrees) * .5f) / viewportHeight : 0.f;
    for (Particle &particle : m_particles) {
        particle.age += elapsed;
        if (particle.age >= particle.lifetime || particle.lifetime <= 0.f) { continue; }
        glm::vec3 velocity = vector(particle.velocity);
        glm::vec3 position = vector(particle.position);
        const bool chip = particle.kind == ParticleKind::TriangleChip ||
                          particle.kind == ParticleKind::QuadChip || particle.kind == ParticleKind::PentagonChip;
        const bool dust = particle.kind == ParticleKind::Dust;
        const glm::vec3 gravity(0.f, chip ? -9.81f : dust ? -.8f : -2.f, 0.f);
        // Closed-form linear drag keeps this cosmetic flight independent of
        // frame subdivision. Dust loses speed quickly; larger chips keep inertia.
        const float drag = chip ? particle.drag : dust ? 3.8f : 1.6f;
        const float decay = std::exp(-drag * elapsed);
        const glm::vec3 terminal = gravity / drag;
        position += terminal * elapsed + (velocity - terminal) * ((1.f - decay) / drag);
        velocity = terminal + (velocity - terminal) * decay;
        particle.position = vector(position);
        particle.velocity = vector(velocity);
        const float phase = particle.age / particle.lifetime;
        // Solid glass disappears through coverage, not by shrinking in flight.
        const float size = chip ? particle.size : particle.size * (1.f - phase * .55f);
        const float angle = particle.angle + particle.angularSpeed * particle.age;
        const glm::vec3 rotatedRight = right * std::cos(angle) + up * std::sin(angle);
        const glm::vec3 rotatedUp = up * std::cos(angle) - right * std::sin(angle);
        const float twinkle = .72f + .28f * std::sin(particle.twinklePhase + particle.age * 28.f);
        if (chip) {
            // World-space tumbling: changing the camera doesn't rotate a chip.
            const glm::quat spin = glm::angleAxis(particle.angularSpeed * particle.age, vector(particle.spinAxis));
            const uint32_t count = particle.kind == ParticleKind::TriangleChip ? 3u :
                                   particle.kind == ParticleKind::QuadChip ? 4u : 5u;
            appendChip(mesh, position, spin * vector(particle.right), spin * vector(particle.up),
                       size, size * particle.stretch, particle.thickness, phase, particle.brightness * twinkle,
                       particle.outline, count);
        } else {
            // The old late-frame mask multiplied coverage down to invisibility
            // through the glass material's alpha. A moderate soft mask plus
            // fast fading reveals scattering, without an opaque smoke cloud.
            const float maskPhase = dust ? .32f + phase * .68f : phase;
            float opticalSize = size;
            if (pixelFootprint) {
                const float worldPerPixel = glm::length(position - vector(cameraPosition)) * pixelScale;
                const float minimumPixels = dust ? 1.8f : particle.kind == ParticleKind::Streak ? 1.1f : 1.7f;
                const float cap = dust ? .04f : .065f;
                opticalSize = std::max(size, std::min(cap, worldPerPixel * minimumPixels * std::sqrt(1.f - phase)));
            }
            appendBillboard(mesh, position, rotatedRight, rotatedUp, normal, opticalSize,
                            std::min(.12f, opticalSize * particle.stretch),
                            maskPhase, particle.brightness * twinkle);
        }
    }
    m_flashAge += elapsed;
    if (m_flashAge < kFlashLifetime) {
        const float phase = m_flashAge / kFlashLifetime;
        const float size = (.025f + .16f * phase) * (.5f + .5f * m_flashStrength);
        appendBillboard(mesh, vector(m_flashPosition), right, up, normal, size, size, phase, 5.f + 2.f * m_flashStrength);
    }
}

void GlassSparkleBurst::reset() {
    for (Particle &particle : m_particles) { particle = {}; }
    m_flashAge = kFlashLifetime;
    m_flashStrength = 0.f;
    m_pendingBirth = false;
}

void GlassSparkleBurst::shutdown() {
    reset();
    if (m_texture.isValid()) {
        Bamboo::AssetManagerAPI::deleteTexture(m_texture);
        m_texture = {};
    }
}

} // namespace SmashLab
