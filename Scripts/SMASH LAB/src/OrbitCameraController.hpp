#pragma once

#include <Bamboo/Bamboo.hpp>
#include <Bamboo/Script.hpp>
#include <Bamboo/Key.hpp>

#include <memory>
#include <array>

// A Play-mode orbit camera with a movable pivot and keyboard navigation.
// No published fields: the scene supplies the initial pose, and the orbit target
// and bounds stay local to this small prototype controller.
class OrbitCameraController final : public Bamboo::Script {
public:
    struct PointerSample {
        Bamboo::Vec2 position;
        Bamboo::Vec2 viewport;
        // Snapshot at pointerDown: later power changes cannot alter this hit.
        float impactStrength = 1.8f;
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
    void selectImpactTool();
    bool isImpactToolSelected() const;
    void requestRoomReset();
    bool consumeRoomResetRequest();
    float currentImpactStrength() const { return m_impactStrength; }
    Bamboo::Vec2 getSceneViewportSize() const;

private:
    struct ScrollInput;
    void applyPose(bool constrainMotion = true);
    void applyScrollLook(float stepTime);
    void resetView();
    bool ensureNonPhysicalCamera();
    void clearKeyboardInput();
    bool cameraKeyPressed(Bamboo::Key key) const;

    std::shared_ptr<ScrollInput> m_scrollInput;

    float m_yaw = 0.f;
    float m_pitch = 0.f;
    float m_distance = 13.f;
    float m_targetYaw = 0.f;
    float m_targetPitch = 0.f;
    float m_targetDistance = 13.f;
    Bamboo::Vec3 m_pivot;
    Bamboo::Vec3 m_moveVelocity;
    Bamboo::Vec2 m_pendingScrollLook{};
    Bamboo::Vec3 m_lastAppliedPosition{};
    std::array<bool, 13> m_keyDown{};
    float m_impactStrength = 1.8f;
    bool m_resetViewRequested = false;
    bool m_hasAppliedPose = false;
    bool m_reportedExternalMotion = false;
    float m_diagnosticElapsed = 0.f;
    int m_diagnosticSnapshots = 0;
    float m_initialYaw = 0.f;
    float m_initialPitch = 0.f;
    float m_initialDistance = 13.f;
    bool m_wasOrbiting = false;
    bool m_active = false;
    bool m_impactToolSelected = true;
    bool m_roomResetRequested = false;
};

REGISTER_SCRIPT(OrbitCameraController)
