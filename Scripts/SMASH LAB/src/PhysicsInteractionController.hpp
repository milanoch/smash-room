#pragma once

#include <Bamboo/Bamboo.hpp>
#include <Bamboo/Script.hpp>

#include "DestructionSystem.hpp"

#include <array>
#include <memory>
#include <vector>

class OrbitCameraController;

// The prototype's impact tool and in-place room reset. Input is routed by the
// camera's game-viewport surface; only fixedUpdate changes physical state.
class PhysicsInteractionController final : public Bamboo::Script {
public:
    void start() override;
    void fixedUpdate(float stepTime) override;
    void lateUpdate(float deltaTime) override;
    void shutdown() override;

private:
    struct InitialState {
        Bamboo::EntityHandle entity;
        Bamboo::Vec3 position;
        Bamboo::Quat rotation;
        Bamboo::Vec3 scale;
    };

    struct ImpactRay {
        Bamboo::Vec3 origin;
        Bamboo::Vec3 direction;
        float strength;
    };

    static constexpr size_t kObjectCount = 10;
    static constexpr size_t kMaxPendingImpacts = 16;
    static constexpr float kRayDistance = 80.f;

    bool restoreRoom();

    std::array<InitialState, kObjectCount> m_initialStates;
    std::vector<ImpactRay> m_pendingImpacts;
    std::shared_ptr<OrbitCameraController> m_camera;
    SmashLab::DestructionSystem m_destruction;
    bool m_ready = false;
    bool m_reportedMissingCamera = false;
};

REGISTER_SCRIPT(PhysicsInteractionController)
