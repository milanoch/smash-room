#pragma once

#include <Bamboo/Bamboo.hpp>
#include <Bamboo/Script.hpp>

#include <memory>

// A Play-mode orbit camera for the open front of LivingRoom_Prototype.
// No published fields: the scene supplies the initial pose, and the orbit target
// and bounds stay local to this small prototype controller.
class OrbitCameraController final : public Bamboo::Script {
public:
    struct PointerSample {
        Bamboo::Vec2 position;
        Bamboo::Vec2 viewport;
    };

    // Matches cameraComponent.camera.fieldOfView in LivingRoom_Prototype.world.
    // This SDK exposes no Bamboo camera-projection getter. If the scene's FOV
    // changes, update this value with it; viewport dimensions are always live.
    static constexpr float kVerticalFovDegrees = 58.f;

    void start() override;
    void update(float deltaTime) override;
    void shutdown() override;

    bool getScenePointer(PointerSample &sample) const;
    bool takeImpactClick(PointerSample &sample);
    void clearImpactClicks();
    void showInteractionMarker(bool available);
    bool acceptsSceneKeyboard() const;

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
