#pragma once

#include "DestructionData.hpp"
#include "GlassCrackEffect.hpp"
#include "GlassFracturePatterns.hpp"
#include "GlassSparkleBurst.hpp"

#include <vector>

namespace SmashLab {

// A small game-layer owner, driven by PhysicsInteractionController's fixed step.
// Prepared nodes retain their authored Dynamic Rigidbody component. An inactive
// node has neither a collider/body nor a renderer; it is not a parked body.
class DestructionSystem {
public:
    bool initialize(BreakableDefinition definition);
    bool isSource(Bamboo::EntityHandle entity) const;
    bool impact(Bamboo::EntityHandle entity, Bamboo::Vec3 point, Bamboo::Vec3 direction, float strength);
    void fixedUpdate(float stepTime);
    bool restore();
    // Visuals use interpolated poses after the orbit camera update; physics is
    // still changed only at the existing fixed-step boundary.
    void updateVisuals(float deltaTime, Bamboo::Vec3 cameraPosition, Bamboo::Quat cameraRotation,
                       float viewportHeight = 0.f, float verticalFovDegrees = 58.f);
    void shutdown();

    DestructionState state() const { return m_state.state(); }
    bool needsRestore() const { return m_state.needsRestore(); }

private:
    struct Pose {
        Bamboo::Vec3 position;
        Bamboo::Quat rotation;
        Bamboo::Vec3 scale;
    };
    struct Fragment {
        Bamboo::EntityHandle entity;
        Pose initial;
    };
    struct PendingBreak {
        Bamboo::Vec3 localPoint;
        Bamboo::Vec3 direction;
        float strength = 0.f;
        uint32_t seed = 1;
        float severity = 1.f;
    };

    bool clearFragments();
    bool failBreak();
    bool activateFragments();
    bool createRoughnessMap();
    void releaseRoughnessMap();

    BreakableDefinition m_definition;
    Bamboo::EntityHandle m_source;
    Pose m_initial;
    std::vector<Fragment> m_fragments;
    Bamboo::MeshHandle m_mesh;
    Bamboo::TextureHandle m_roughnessTexture;
    Bamboo::EntityHandle m_visualEntity;
    Bamboo::MeshData m_intactGeometry;
    // Only the current hit's CPU geometry is retained. The GPU mesh and the
    // prepared solver nodes are still reused across every break/reset.
    GlassFracturePattern m_fracture;
    Bamboo::MeshData m_visualGeometry;
    Bamboo::MeshData m_crackGeometry;
    GlassCrackEffect m_cracks;
    GlassSparkleBurst m_sparkles;
    PendingBreak m_pendingBreak;
    uint32_t m_randomState = 1;
    float m_breakElapsed = 0.f;
    float m_breakDuration = .004f;
    float m_damage = 0.f;
    DestructionStateMachine m_state;
    bool m_initialized = false;
};

} // namespace SmashLab
