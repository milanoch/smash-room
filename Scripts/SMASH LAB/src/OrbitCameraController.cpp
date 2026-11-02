#include "OrbitCameraController.hpp"

#include <Bamboo/Components/TransformComponentAPI.hpp>
#include <Bamboo/Input.hpp>
#include <Bamboo/Logger.hpp>
#include <PandaUI/Runtime/Window.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr float kRadians = 0.017453292519943295f;
const Bamboo::Vec3 kTarget(0.f, 1.15f, 0.f);
constexpr float kMinYaw = -55.f * kRadians;
constexpr float kMaxYaw = 55.f * kRadians;
constexpr float kMinPitch = 12.f * kRadians;
constexpr float kMaxPitch = 65.f * kRadians;
constexpr float kMinDistance = 9.f;
constexpr float kMaxDistance = 20.f;
constexpr float kOrbitSensitivity = 0.22f * kRadians;
constexpr float kZoomSensitivity = 0.15f;
constexpr float kSmoothing = 12.f;

} // namespace

// The packaged Bamboo Input header declares getMouseScrollY(), but this SDK's
// library/host ABI does not implement it. Use Panda's existing game-UI event
// routing for wheel input instead; this surface has no visible UI or widgets.
struct OrbitCameraController::ScrollInput final : PandaUI::View {
    PandaUI::Window window;
    std::shared_ptr<PandaUI::View> previousRoot;
    float zoomSteps = 0.f;

    ScrollInput() {
        surface().setBackgroundColor(PandaUI::Color(0.f, 0.f, 0.f, 0.f));
        setFocusable(false);
    }

    bool scroll(PandaUI::ScrollEvent &event) override {
        if (event.isGestureEnded || event.isMomentum || !std::isfinite(event.delta.y) || event.delta.y == 0.f) {
            return false;
        }
        const float steps = event.isTrackpad ? event.delta.y / 40.f : event.delta.y;
        zoomSteps = std::clamp(zoomSteps + steps, -3.f, 3.f);
        event.isHandled = true;
        return true;
    }
};

void OrbitCameraController::start() {
    const Bamboo::Vec3 position = Bamboo::TransformComponentAPI::getPosition(getEntity());
    const float x = position.x - kTarget.x;
    const float y = position.y - kTarget.y;
    const float z = position.z - kTarget.z;
    const float length = std::sqrt(x * x + y * y + z * z);

    // Seed both the displayed and requested state from the saved camera pose:
    // entering Play never starts a smoothing flight from unrelated defaults.
    if (length > 0.001f) {
        m_yaw = std::clamp(std::atan2(x, z), kMinYaw, kMaxYaw);
        m_pitch = std::clamp(std::asin(std::clamp(y / length, -1.f, 1.f)), kMinPitch, kMaxPitch);
        m_distance = std::clamp(length, kMinDistance, kMaxDistance);
    } else {
        m_yaw = 12.f * kRadians;
        m_pitch = 25.f * kRadians;
        m_distance = 13.f;
    }
    m_targetYaw = m_yaw;
    m_targetPitch = m_pitch;
    m_targetDistance = m_distance;
    m_wasOrbiting = false;
    applyPose();

    const PandaUI::Window window = PandaUI::Window::main();
    if (window.isValid()) {
        m_scrollInput = std::make_shared<ScrollInput>();
        m_scrollInput->window = window;
        m_scrollInput->previousRoot = m_scrollInput->window.context().getRootView();
        m_scrollInput->window.setRootView(m_scrollInput);
    } else {
        LOG_WARN("OrbitCameraController: game UI window unavailable; wheel zoom cannot receive events.");
    }
}

void OrbitCameraController::update(float deltaTime) {
    const bool orbiting = Bamboo::Input::isMouseButtonPressed(Bamboo::MouseButton::RIGHT);
    if (orbiting && m_wasOrbiting) {
        const double dx = Bamboo::Input::getMouseDeltaX();
        const double dy = Bamboo::Input::getMouseDeltaY();
        // Discard a cursor warp/focus jump. The acquisition frame is also ignored,
        // because its delta can include motion before the right button was held.
        if (std::isfinite(dx) && std::isfinite(dy) && std::abs(dx) <= 250. && std::abs(dy) <= 250.) {
            m_targetYaw = std::clamp(m_targetYaw - static_cast<float>(dx) * kOrbitSensitivity, kMinYaw, kMaxYaw);
            m_targetPitch = std::clamp(m_targetPitch + static_cast<float>(dy) * kOrbitSensitivity, kMinPitch, kMaxPitch);
        }
    }
    m_wasOrbiting = orbiting;

    const float scroll = m_scrollInput ? std::exchange(m_scrollInput->zoomSteps, 0.f) : 0.f;
    if (scroll != 0.f) {
        m_targetDistance = std::clamp(
            m_targetDistance * std::exp(-scroll * kZoomSensitivity), kMinDistance, kMaxDistance
        );
    }

    // Mouse/scroll values are deltas, so they are not multiplied by frame time.
    // Only the exponential smoothing uses dt and stays stable across frame rates.
    const float step = std::clamp(deltaTime, 0.f, 0.1f);
    const float blend = 1.f - std::exp(-kSmoothing * step);
    m_yaw += (m_targetYaw - m_yaw) * blend;
    m_pitch += (m_targetPitch - m_pitch) * blend;
    m_distance += (m_targetDistance - m_distance) * blend;
    applyPose();
}

void OrbitCameraController::shutdown() {
    // Detach synchronously: no callback or script-owned View vtable may survive
    // unloading the script bundle. The editor's own UI context is separate.
    if (m_scrollInput && m_scrollInput->window.isValid()) {
        auto &context = m_scrollInput->window.context();
        if (context.getRootView().get() == m_scrollInput.get()) {
            context.setRootView(m_scrollInput->previousRoot);
        }
    }
    m_scrollInput.reset();
    m_wasOrbiting = false;
}

void OrbitCameraController::applyPose() {
    const float horizontal = m_distance * std::cos(m_pitch);
    Bamboo::TransformComponentAPI::setPosition(
        getEntity(),
        {kTarget.x + horizontal * std::sin(m_yaw), kTarget.y + m_distance * std::sin(m_pitch),
         kTarget.z + horizontal * std::cos(m_yaw)}
    );

    // qYaw * qPitch(-pitch): Panda's camera looks along local -Z with +Y up.
    // This points exactly at kTarget without roll or an Euler singularity.
    const float sinYaw = std::sin(m_yaw * 0.5f);
    const float cosYaw = std::cos(m_yaw * 0.5f);
    const float sinPitch = std::sin(m_pitch * 0.5f);
    const float cosPitch = std::cos(m_pitch * 0.5f);
    Bamboo::TransformComponentAPI::setRotation(
        getEntity(), {-cosYaw * sinPitch, sinYaw * cosPitch, sinYaw * sinPitch, cosYaw * cosPitch}
    );
}
