#pragma once

#include <Bamboo/Base.hpp>
#include <Panda/Physics/PhysicsComponents.hpp>

#include <vector>

namespace SmashLab {

struct FurnitureDefinition {
    const char *sourceTag;
    float breakStrength;
    std::vector<const char *> partTags;
};

std::vector<FurnitureDefinition> livingRoomFurnitureDefinitions();

// Uses the furniture's existing imported parts as prepared fragments. The intact
// source owns the only active collider; a strong impact removes it and activates
// the authored Dynamic bodies/convex Mesh Colliders of its visible parts.
class FurnitureDestructionSystem {
public:
    bool initialize(std::vector<FurnitureDefinition> definitions);
    bool isSource(Bamboo::EntityHandle entity) const;
    bool impact(Bamboo::EntityHandle entity, Bamboo::Vec3 point, Bamboo::Vec3 direction, float strength);
    bool restore();
    void shutdown();

    struct Pose {
        Bamboo::Vec3 position;
        Bamboo::Quat rotation;
        Bamboo::Vec3 scale;
    };

private:
    struct Fragment {
        Bamboo::EntityHandle entity;
        Pose initial;
        Panda::MeshCollider3DComponent collider;
    };
    struct Assembly {
        Bamboo::EntityHandle source;
        Pose initial;
        Panda::BoxCollider3DComponent sourceCollider;
        float breakStrength = 3.f;
        std::vector<Fragment> fragments;
        bool broken = false;
    };

    bool restoreAssembly(Assembly &assembly);
    bool breakAssembly(Assembly &assembly);

    std::vector<Assembly> m_assemblies;
};

} // namespace SmashLab
