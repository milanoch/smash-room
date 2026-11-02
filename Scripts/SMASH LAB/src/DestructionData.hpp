#pragma once

#include <Bamboo/Base.hpp>

#include <vector>

namespace SmashLab {

enum class DestructionMaterial { Glass };

struct FragmentData {
    const char *tag;
    // Relative to the intact unit cube, before the source transform is applied.
    Bamboo::Vec3 center;
    Bamboo::Vec3 size;
};

struct FragmentPhysics {
    // These match the BoxCollider3D defaults exposed by this SDK's addComponent.
    // A different set needs a corresponding engine-supported configuration path.
    float density = 1.f;
    float friction = .5f;
    float restitution = 0.f;
};

struct BreakableDefinition {
    const char *sourceTag = nullptr;
    DestructionMaterial material = DestructionMaterial::Glass;
    Bamboo::MaterialHandle surfaceMaterial;
    float strength = 1.f;
    float inheritedImpactRatio = .65f;
    float radialSpeed = .65f;
    float maxScatterSpeed = 1.4f;
    FragmentPhysics physics;
    std::vector<FragmentData> fragments;
};

inline BreakableDefinition glassCubeDefinition(Bamboo::MaterialHandle material) {
    BreakableDefinition result;
    result.sourceTag = "Cube_Purple";
    result.surfaceMaterial = material;
    // Ten authored cells tile the unit cube, with each cell shrunk about its
    // center by .97 to avoid initial interpenetration. Their volume totals
    // .912673 of the source; equal density keeps mass approximately conserved.
    result.fragments = {
        {"Glass_Fragment_00", {-.405f, -.29f, -.23f}, {.1843f, .4074f, .5238f}},
        {"Glass_Fragment_01", {-.215f, -.29f, -.23f}, {.1843f, .4074f, .5238f}},
        {"Glass_Fragment_02", {-.31f, -.29f, .27f}, {.3686f, .4074f, .4462f}},
        {"Glass_Fragment_03", {-.31f, .21f, -.23f}, {.3686f, .5626f, .5238f}},
        {"Glass_Fragment_04", {-.31f, .21f, .27f}, {.3686f, .5626f, .4462f}},
        {"Glass_Fragment_05", {.19f, -.29f, -.23f}, {.6014f, .4074f, .5238f}},
        {"Glass_Fragment_06", {.19f, -.29f, .27f}, {.6014f, .4074f, .4462f}},
        {"Glass_Fragment_07", {.19f, .21f, -.23f}, {.6014f, .5626f, .5238f}},
        {"Glass_Fragment_08", {.19f, .007f, .27f}, {.6014f, .16878f, .4462f}},
        {"Glass_Fragment_09", {.19f, .297f, .27f}, {.6014f, .39382f, .4462f}},
    };
    return result;
}

enum class DestructionState { Intact, Breaking, Broken, Resetting };

class DestructionStateMachine {
public:
    DestructionState state() const { return m_state; }
    bool needsRestore() const { return m_needsRestore; }

    bool beginBreak() {
        if (m_state != DestructionState::Intact) { return false; }
        m_state = DestructionState::Breaking;
        m_needsRestore = true;
        return true;
    }

    bool finishBreak() {
        if (m_state != DestructionState::Breaking) { return false; }
        m_state = DestructionState::Broken;
        return true;
    }

    bool beginReset() {
        if (m_state == DestructionState::Resetting) { return false; }
        m_state = DestructionState::Resetting;
        m_needsRestore = true;
        return true;
    }

    bool finishReset() {
        if (m_state != DestructionState::Resetting) { return false; }
        m_state = DestructionState::Intact;
        m_needsRestore = false;
        return true;
    }

private:
    DestructionState m_state = DestructionState::Intact;
    bool m_needsRestore = false;
};

} // namespace SmashLab
