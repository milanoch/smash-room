#pragma once

#include <Bamboo/Bamboo.hpp>
#include <Bamboo/Script.hpp>

#include <memory>

// A Play-mode orbit camera for the open front of LivingRoom_Prototype.
// No published fields: the scene supplies the initial pose, and the orbit target
// and bounds stay local to this small prototype controller.
class OrbitCameraController final : public Bamboo::Script {
public:
    void start() override;
    void update(float deltaTime) override;
    void shutdown() override;

private:
    struct ScrollInput;
    void applyPose();

    std::shared_ptr<ScrollInput> m_scrollInput;

    float m_yaw = 0.f;
    float m_pitch = 0.f;
    float m_distance = 13.f;
    float m_targetYaw = 0.f;
    float m_targetPitch = 0.f;
    float m_targetDistance = 13.f;
    bool m_wasOrbiting = false;
};

REGISTER_SCRIPT(OrbitCameraController)
