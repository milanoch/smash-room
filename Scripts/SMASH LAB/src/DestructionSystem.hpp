#pragma once

#include "DestructionData.hpp"

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
    bool restore();
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

    bool show(Bamboo::EntityHandle entity);
    bool clearFragments();
    bool failBreak();

    BreakableDefinition m_definition;
    Bamboo::EntityHandle m_source;
    Pose m_initial;
    std::vector<Fragment> m_fragments;
    Bamboo::MeshHandle m_mesh;
    DestructionStateMachine m_state;
    bool m_initialized = false;
};

} // namespace SmashLab
