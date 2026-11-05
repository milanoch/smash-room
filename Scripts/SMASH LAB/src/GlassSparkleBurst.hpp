#pragma once

#include <Bamboo/Assets/MeshAPI.hpp>

#include <array>
#include <cstddef>

namespace SmashLab {

// Cosmetic CPU particles submitted through the existing glass MeshComponent.
// No physics bodies, event listeners, per-burst entities, or per-burst assets.
class GlassSparkleBurst {
public:
    // Bounded, reusable burst: 192 polygonal chips (96 larger chips are closed
    // thin prisms), 80 optical pinpoints, 48 glints and 192 dust motes. No bodies.
    static constexpr std::size_t kParticleCount = 512;
    static constexpr uint32_t kTextureWidth = 288;
    static constexpr uint32_t kTextureHeight = 256;

    bool initialize();
    Bamboo::TextureHandle texture() const { return m_texture; }
    void trigger(Bamboo::Vec3 point, Bamboo::Vec3 direction, Bamboo::Vec3 inheritedVelocity,
                 uint32_t seed = 0x51a55e31u, float severity = 1.f);
    void reset();
    void shutdown();

    // Call once per frame, before the owner sorts all glass/VFX triangles.
    void appendTo(Bamboo::MeshData &mesh, Bamboo::Vec3 cameraPosition,
                  Bamboo::Quat cameraRotation, float deltaTime,
                  float viewportHeight = 0.f, float verticalFovDegrees = 58.f);

    // Right-hand strip of the shared atlas: RGB white, alpha from 0 to 1.
    // PBR ignores vertex alpha, so glass face opacity is sampled here instead.
    static Bamboo::Vec2 glassUV(float alpha);

private:
    enum class ParticleKind { Point, Streak, TriangleChip, QuadChip, PentagonChip, Dust };

    struct Particle {
        Bamboo::Vec3 position;
        Bamboo::Vec3 velocity;
        float age = 0.f;
        float lifetime = 0.f;
        float size = 0.f;
        float thickness = 0.f;
        float drag = .4f;
        float stretch = 1.f;
        float angle = 0.f;
        float angularSpeed = 0.f;
        float brightness = 1.f;
        float twinklePhase = 0.f;
        std::array<Bamboo::Vec2, 5> outline{};
        Bamboo::Vec3 right;
        Bamboo::Vec3 up;
        Bamboo::Vec3 spinAxis;
        ParticleKind kind = ParticleKind::Point;
    };

    Bamboo::TextureHandle m_texture;
    std::array<Particle, kParticleCount> m_particles{};
    Bamboo::Vec3 m_flashPosition;
    float m_flashAge = 1.f;
    float m_flashStrength = 0.f;
    bool m_pendingBirth = false;
};

} // namespace SmashLab
