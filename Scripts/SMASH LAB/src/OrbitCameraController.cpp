#include "OrbitCameraController.hpp"
#include "SdkComponentAccess.hpp"

#include <Bamboo/Components/TransformComponentAPI.hpp>
#include <Bamboo/Input.hpp>
#include <Bamboo/Logger.hpp>
#include <Bamboo/Physics3DAPI.hpp>
#include <PandaUI/Runtime/Window.hpp>
#include <PandaUI/Runtime/ModalManager.hpp>
#include <PandaUI/Elements/Label.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <utility>

namespace {

constexpr float kRadians = 0.017453292519943295f;
const Bamboo::Vec3 kTarget(0.f, 1.15f, 0.f);
constexpr float kFullTurn = 360.f * kRadians;
constexpr float kMinPitch = -85.f * kRadians;
constexpr float kMaxPitch = 85.f * kRadians;
constexpr float kMinDistance = .75f;
constexpr float kMaxDistance = 30.f;
constexpr float kOrbitSensitivity = 0.44f * kRadians;
// Fern already converts precise macOS trackpad points to scroll units (/40).
// Do not scale them down again. Both trackpads and ordinary wheels turn the view;
// zoom has its dedicated Q/E keys and never shares this event stream.
constexpr float kScrollLookSensitivity = 4.f * kRadians;
constexpr float kMaxQueuedScrollLook = 8.f * kRadians;
constexpr float kMaxScrollLookSpeed = 150.f * kRadians;
constexpr float kScrollLookSmoothing = 20.f;
// Keep the default macOS natural-scroll convention. The SDK does not expose
// NSEvent::isDirectionInvertedFromDevice; use -1 for the opposite preference.
constexpr float kScrollDirection = 1.f;
constexpr float kSmoothing = 12.f;
constexpr float kMoveSpeed = 6.f;
constexpr float kMoveResponse = 14.f;
constexpr float kKeyboardRotationSpeed = 130.f * kRadians;
constexpr float kKeyboardZoomSpeed = 7.f;
constexpr float kCameraClearance = .16f;
constexpr float kCollisionSkin = .025f;
constexpr std::array<Bamboo::Key, 13> kCameraKeys = {
    Bamboo::Key::W, Bamboo::Key::S, Bamboo::Key::A, Bamboo::Key::D,
    Bamboo::Key::SPACE, Bamboo::Key::LEFT_SHIFT, Bamboo::Key::LEFT, Bamboo::Key::RIGHT,
    Bamboo::Key::UP, Bamboo::Key::DOWN, Bamboo::Key::Q, Bamboo::Key::E, Bamboo::Key::C
};

} // namespace

// The packaged Bamboo Input header declares getMouseScrollY(), but this SDK's
// library/host ABI does not implement it. Use Panda's existing game-UI event
// routing for wheel and pointer input instead. Its coordinates and dimensions
// already belong to the game viewport, including when docked in PandaEditor.
struct OrbitCameraController::ScrollInput final : PandaUI::View {
    PandaUI::Window window;
    std::shared_ptr<PandaUI::View> previousRoot;
    OrbitCameraController *controller = nullptr;
    PandaUI::Point orbitDelta;
    PandaUI::Point pointer;
    PandaUI::Size pointerViewport;
    bool hasPointer = false;
    bool shortcutModifiersDown = false;
    std::deque<PointerSample> clicks;
    std::shared_ptr<PandaUI::View> marker;
    std::shared_ptr<PandaUI::Label> impactLabel;
    std::array<bool, 3> powerKeyDown{};

    ScrollInput() {
        surface().setBackgroundColor(PandaUI::Color(0.f, 0.f, 0.f, 0.f));
        setFocusable(true);
    }

    template<typename Event>
    void rememberModifiers(const Event &event) {
        // These flags describe this event, unlike global held keys which can
        // retain an editor shortcut across Reload or a lost key-up event.
        shortcutModifiersDown = event.isSuperDown || event.isControlDown || event.isAltDown;
        if (shortcutModifiersDown && controller) { controller->clearKeyboardInput(); }
    }

    bool cameraKey(PandaUI::KeyEvent &event, bool down) {
        rememberModifiers(event);
        if (!controller) { return false; }
        const std::array<PandaUI::Key, 3> powerKeys = {
            PandaUI::Key::Key1, PandaUI::Key::Key2, PandaUI::Key::Key3
        };
        for (size_t i = 0; i < powerKeys.size(); ++i) {
            if (event.key != powerKeys[i]) { continue; }
            const bool wasDown = powerKeyDown[i];
            if (!down) { powerKeyDown[i] = false; }
            if (event.isHandled || !controller->acceptsSceneKeyboard()) { return false; }
            powerKeyDown[i] = down;
            if (down && !wasDown) {
                constexpr std::array<float, 3> strengths = {.8f, 1.8f, 3.6f};
                constexpr std::array<const char *, 3> names = {"Weak", "Medium", "Strong"};
                controller->m_impactStrength = strengths[i];
                updateImpactLabel();
                LOG_INFO("Impact Tool: %s (delta-v %.1f m/s); 1/2/3 selects power", names[i], strengths[i]);
            }
            event.isHandled = true;
            return true;
        }
        for (size_t i = 0; i < kCameraKeys.size(); ++i) {
            if (static_cast<int>(event.key) != static_cast<int>(kCameraKeys[i])) { continue; }
            const bool wasDown = controller->m_keyDown[i];
            // Always accept a release into our state, even if UI has taken focus.
            if (!down) { controller->m_keyDown[i] = false; }
            if (event.isHandled || !controller->acceptsSceneKeyboard()) { return false; }
            controller->m_keyDown[i] = down;
            if (down && !wasDown && event.key == PandaUI::Key::C) {
                controller->m_resetViewRequested = true;
            }
            event.isHandled = true;
            return true;
        }
        return false; // R still belongs to the existing room reset.
    }

    bool keyDown(PandaUI::KeyEvent &event) override { return cameraKey(event, true); }
    bool keyUp(PandaUI::KeyEvent &event) override { return cameraKey(event, false); }

    void updateImpactLabel() {
        if (!impactLabel || !controller) { return; }
        const char *name = controller->m_impactStrength < 1.f ? "Weak (1)" :
                           controller->m_impactStrength < 3.f ? "Medium (2)" : "Strong (3)";
        impactLabel->setText(std::string("Impact: ") + name + "\n1 Weak   2 Medium   3 Strong   R Reset");
    }

    void cancelCameraInput() {
        shortcutModifiersDown = false;
        powerKeyDown.fill(false);
        orbitDelta = {};
        clicks.clear();
        if (controller) {
            controller->clearKeyboardInput();
            controller->m_pendingScrollLook = {};
            controller->m_moveVelocity = {};
        }
    }

    void focusChanged(bool focused) override {
        if (!focused) { cancelCameraInput(); }
    }

    void interactionCancelled() override { cancelCameraInput(); }

    bool scenePoint(PandaUI::Point point) {
        if (!window.isValid()) { return false; }
        auto &context = window.context();
        const auto size = context.getViewportSize();
        return std::isfinite(point.x) && std::isfinite(point.y) &&
               point.x >= 0.f && point.y >= 0.f && point.x < size.width && point.y < size.height &&
               context.getRootView().get() == this && !context.modals().isPresenting() &&
               hitTest(point) == this;
    }

    void rememberPointer(PandaUI::Point point) {
        pointer = point;
        hasPointer = scenePoint(point);
        if (hasPointer) { pointerViewport = window.context().getViewportSize(); }
        if (!hasPointer && marker) { marker->setHidden(true); }
    }

    bool pointerMove(PandaUI::PointerEvent &event) override {
        rememberModifiers(event);
        rememberPointer(event.position);
        return false;
    }

    bool pointerDown(PandaUI::PointerEvent &event) override {
        rememberModifiers(event);
        rememberPointer(event.position);
        if (hasPointer && !shortcutModifiersDown) { window.context().setFocusedView(this); }
        if (!hasPointer || shortcutModifiersDown || event.button != PandaUI::PointerButton::Left ||
            Bamboo::Input::isMouseButtonPressed(Bamboo::MouseButton::RIGHT)) {
            return false;
        }
        const auto size = window.context().getViewportSize();
        // A native down event means one press, never one impulse per held frame.
        if (clicks.size() < 16) {
            clicks.push_back({{pointer.x, pointer.y}, {size.width, size.height}, controller->currentImpactStrength()});
        }
        event.isHandled = true;
        return true;
    }

    bool pointerUp(PandaUI::PointerEvent &event) override {
        rememberModifiers(event);
        return false;
    }

    void pointerLeave(PandaUI::PointerEvent &) override {
        hasPointer = false;
        orbitDelta = {};
        if (controller) { controller->m_pendingScrollLook = {}; }
        if (marker) { marker->setHidden(true); }
        setCursor(PandaUI::Cursor::Arrow);
    }

    bool scroll(PandaUI::ScrollEvent &event) override {
        // Check at event time as well as update time: focus or modifiers may
        // change between the two. Unhandled UI-edge scrolls must not orbit.
        if (event.isHandled || !controller || !controller->acceptsSceneKeyboard()) { return false; }
        rememberPointer(event.position);
        if (!hasPointer || event.isGestureEnded || event.isMomentum ||
            !std::isfinite(event.delta.x) || !std::isfinite(event.delta.y) ||
            (event.delta.x == 0.f && event.delta.y == 0.f)) {
            return false;
        }
        // UI coordinates: right/down gestures follow the existing RMB signs.
        // Accumulate both axes before the frame update, bounded together so a
        // diagonal gesture or a large event burst cannot cause a sharp jump.
        orbitDelta.x -= event.delta.x * kScrollDirection * kScrollLookSensitivity;
        orbitDelta.y += event.delta.y * kScrollDirection * kScrollLookSensitivity;
        const float length = std::hypot(orbitDelta.x, orbitDelta.y);
        if (length > kMaxQueuedScrollLook) {
            const float scale = kMaxQueuedScrollLook / length;
            orbitDelta.x *= scale;
            orbitDelta.y *= scale;
        }
        event.isHandled = true;
        return true;
    }
};

void OrbitCameraController::start() {
    m_active = false;
    m_hasAppliedPose = false;
    m_reportedExternalMotion = false;
    m_diagnosticElapsed = 0.f;
    m_diagnosticSnapshots = 0;
    m_impactStrength = 1.8f;
    if (!ensureNonPhysicalCamera()) { return; }
    clearKeyboardInput();
    const Bamboo::Vec3 position = Bamboo::TransformComponentAPI::getPosition(getEntity());
    const float x = position.x - kTarget.x;
    const float y = position.y - kTarget.y;
    const float z = position.z - kTarget.z;
    const float length = std::sqrt(x * x + y * y + z * z);

    // Seed both the displayed and requested state from the saved camera pose:
    // entering Play never starts a smoothing flight from unrelated defaults.
    if (length > 0.001f) {
        m_yaw = std::atan2(x, z);
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
    m_initialYaw = m_yaw;
    m_initialPitch = m_pitch;
    m_initialDistance = m_distance;
    m_pivot = kTarget;
    m_moveVelocity = {};
    m_pendingScrollLook = {};
    m_wasOrbiting = false;
    applyPose(false);

    const PandaUI::Window window = PandaUI::Window::main();
    if (window.isValid()) {
        m_scrollInput = std::make_shared<ScrollInput>();
        m_scrollInput->controller = this;
        m_scrollInput->window = window;
        m_scrollInput->previousRoot = m_scrollInput->window.context().getRootView();
        m_scrollInput->marker = std::make_shared<PandaUI::View>();
        auto &marker = m_scrollInput->marker;
        marker->layoutSetAbsolute();
        marker->layoutSetSize({14.f, 14.f});
        marker->setUserInteractionEnabled(false);
        marker->setHidden(true);
        marker->surface().setBackgroundColor(PandaUI::Color(0.f, 0.f, 0.f, 0.f));
        marker->surface().setBorderColor(PandaUI::Color(.25f, 1.f, .55f, .95f));
        marker->surface().setBorderWidth(2.f);
        marker->surface().setCornerRadius(7.f);
        m_scrollInput->addSubview(marker);
        // A noninteractive HUD shares the existing game root. It never takes
        // keyboard focus or blocks picking, orbiting or scrolling underneath.
        m_scrollInput->impactLabel = std::make_shared<PandaUI::Label>();
        auto &impactLabel = m_scrollInput->impactLabel;
        impactLabel->setUserInteractionEnabled(false);
        impactLabel->layoutSetAbsolute();
        impactLabel->layoutSetOrigin({16.f, 14.f});
        impactLabel->layoutSetSize({390.f, 44.f});
        impactLabel->setFont(PandaUI::Font(14.f));
        impactLabel->setNumberOfLines(2);
        impactLabel->setTextColor(PandaUI::Color(.9f, .96f, 1.f, 1.f));
        impactLabel->setTextOutline(PandaUI::TextOutline(1.f, PandaUI::Color(0.f, 0.f, 0.f, .8f)));
        m_scrollInput->updateImpactLabel();
        m_scrollInput->addSubview(impactLabel);
        m_scrollInput->window.setRootView(m_scrollInput);
        // Key events start at the focused view; merely installing a root does
        // not route keys to it. This is the game's context, not the editor UI.
        m_scrollInput->window.context().setFocusedView(m_scrollInput.get());
    } else {
        LOG_WARN("OrbitCameraController: game UI window unavailable; scene input is disabled.");
    }
    m_active = true;
    LOG_INFO("Camera input v4: entity=%u y=%.3f; game UI=%d; event-based keyboard and trackpad",
             getEntity().id, position.y, m_scrollInput != nullptr);
}

void OrbitCameraController::update(float deltaTime) {
    if (!m_active) { return; }
    const auto observed = Bamboo::TransformComponentAPI::getPosition(getEntity());
    const float externalDistance = std::hypot(observed.x - m_lastAppliedPosition.x,
                                            observed.y - m_lastAppliedPosition.y,
                                            observed.z - m_lastAppliedPosition.z);
    if (m_hasAppliedPose && externalDistance > .001f) {
        if (!m_reportedExternalMotion) {
            LOG_WARN("Camera guard v3: external transform movement entity=%u deltaY=%.4f; restoring controller pose",
                     getEntity().id, observed.y - m_lastAppliedPosition.y);
            m_reportedExternalMotion = true;
        }
        if (!ensureNonPhysicalCamera()) { m_active = false; return; }
        Bamboo::TransformComponentAPI::setPosition(getEntity(), m_lastAppliedPosition);
    }
    // Three startup samples distinguish a moving camera from moving scenery,
    // stale key input and an unexpected physics write without per-frame spam.
    m_diagnosticElapsed += std::isfinite(deltaTime) ? std::clamp(deltaTime, 0.f, .1f) : 0.f;
    if (m_diagnosticSnapshots < 3 && m_diagnosticElapsed >= 1.f) {
        m_diagnosticElapsed -= 1.f;
        ++m_diagnosticSnapshots;
        LOG_INFO("Camera guard v3 check: observedY=%.3f ownedY=%.3f pitch=%.1f Shift=%d W=%d S=%d",
                 observed.y, m_lastAppliedPosition.y, m_pitch / kRadians,
                 Bamboo::Input::isKeyPressed(Bamboo::Key::LEFT_SHIFT),
                 Bamboo::Input::isKeyPressed(Bamboo::Key::W), Bamboo::Input::isKeyPressed(Bamboo::Key::S));
    }
    if (!acceptsSceneKeyboard()) {
        // Do not coast or retain old orbit/zoom requests behind focused UI,
        // modals or editor command shortcuts. No pointer is needed for keys.
        m_moveVelocity = {};
        m_pendingScrollLook = {};
        m_targetYaw = m_yaw;
        m_targetPitch = m_pitch;
        m_targetDistance = m_distance;
        m_wasOrbiting = false;
        clearKeyboardInput();
        if (m_scrollInput) { m_scrollInput->orbitDelta = {}; }
        return;
    }
    if (m_resetViewRequested) {
        resetView();
        return;
    }
    const float step = std::isfinite(deltaTime) ? std::clamp(deltaTime, 0.f, .1f) : 0.f;
    auto axis = [this](Bamboo::Key positive, Bamboo::Key negative) {
        return static_cast<float>(cameraKeyPressed(positive)) - static_cast<float>(cameraKeyPressed(negative));
    };
    m_targetYaw += axis(Bamboo::Key::LEFT, Bamboo::Key::RIGHT) * kKeyboardRotationSpeed * step;
    m_targetPitch -= axis(Bamboo::Key::UP, Bamboo::Key::DOWN) * kKeyboardRotationSpeed * step;
    m_targetDistance = std::clamp(m_targetDistance + axis(Bamboo::Key::E, Bamboo::Key::Q) *
                                 kKeyboardZoomSpeed * step, kMinDistance, kMaxDistance);

    const bool orbiting = m_scrollInput && m_scrollInput->hasPointer &&
        m_scrollInput->scenePoint(m_scrollInput->pointer) &&
        Bamboo::Input::isMouseButtonPressed(Bamboo::MouseButton::RIGHT);
    if (orbiting && m_wasOrbiting) {
        const double dx = Bamboo::Input::getMouseDeltaX();
        const double dy = Bamboo::Input::getMouseDeltaY();
        // Discard a cursor warp/focus jump. The acquisition frame is also ignored,
        // because its delta can include motion before the right button was held.
        if (std::isfinite(dx) && std::isfinite(dy) && std::abs(dx) <= 250. && std::abs(dy) <= 250.) {
            m_targetYaw -= static_cast<float>(dx) * kOrbitSensitivity;
            m_targetPitch += static_cast<float>(dy) * kOrbitSensitivity;
        }
    }
    m_wasOrbiting = orbiting;

    const auto scroll = m_scrollInput ? std::exchange(m_scrollInput->orbitDelta, PandaUI::Point{}) : PandaUI::Point{};
    // Keep look input separate from orbital requests. A fresh reverse gesture
    // immediately cancels that axis's old tail instead of adding a backlog.
    if (scroll.x * m_pendingScrollLook.x < 0.f) { m_pendingScrollLook.x = 0.f; }
    if (scroll.y * m_pendingScrollLook.y < 0.f) { m_pendingScrollLook.y = 0.f; }
    m_pendingScrollLook.x += scroll.x;
    m_pendingScrollLook.y += scroll.y;
    const float pendingLength = std::hypot(m_pendingScrollLook.x, m_pendingScrollLook.y);
    if (pendingLength > kMaxQueuedScrollLook) {
        const float scale = kMaxQueuedScrollLook / pendingLength;
        m_pendingScrollLook.x *= scale;
        m_pendingScrollLook.y *= scale;
    }
    m_targetPitch = std::clamp(m_targetPitch, kMinPitch, kMaxPitch);

    // Translate the pivot along the displayed camera axes. Moving both pivot
    // and eye preserves the view direction and never pulls back to kTarget.
    const float forward = axis(Bamboo::Key::W, Bamboo::Key::S);
    const float right = axis(Bamboo::Key::D, Bamboo::Key::A);
    const float vertical = axis(Bamboo::Key::SPACE, Bamboo::Key::LEFT_SHIFT);
    const float sinYaw = std::sin(m_yaw), cosYaw = std::cos(m_yaw);
    const float cosPitch = std::cos(m_pitch);
    Bamboo::Vec3 wantedVelocity{
        -sinYaw * cosPitch * forward + cosYaw * right,
        -std::sin(m_pitch) * forward + vertical,
        -cosYaw * cosPitch * forward - sinYaw * right
    };
    const float inputLength = std::sqrt(wantedVelocity.x * wantedVelocity.x + wantedVelocity.y * wantedVelocity.y +
                                        wantedVelocity.z * wantedVelocity.z);
    const float speed = kMoveSpeed / std::max(1.f, inputLength);
    wantedVelocity = {wantedVelocity.x * speed, wantedVelocity.y * speed, wantedVelocity.z * speed};
    const float velocityBlend = 1.f - std::exp(-kMoveResponse * step);
    // Exact integral of exponential velocity smoothing: constant held input
    // covers the same distance at different frame rates, including its coast.
    m_pivot.x += wantedVelocity.x * step + (m_moveVelocity.x - wantedVelocity.x) * velocityBlend / kMoveResponse;
    m_pivot.y += wantedVelocity.y * step + (m_moveVelocity.y - wantedVelocity.y) * velocityBlend / kMoveResponse;
    m_pivot.z += wantedVelocity.z * step + (m_moveVelocity.z - wantedVelocity.z) * velocityBlend / kMoveResponse;
    m_moveVelocity.x += (wantedVelocity.x - m_moveVelocity.x) * velocityBlend;
    m_moveVelocity.y += (wantedVelocity.y - m_moveVelocity.y) * velocityBlend;
    m_moveVelocity.z += (wantedVelocity.z - m_moveVelocity.z) * velocityBlend;
    if (m_moveVelocity.x * m_moveVelocity.x + m_moveVelocity.y * m_moveVelocity.y +
        m_moveVelocity.z * m_moveVelocity.z < 1.e-6f) { m_moveVelocity = {}; }

    // Pointer deltas need no dt multiplier. Keyboard rates and all smoothing do.
    const float blend = 1.f - std::exp(-kSmoothing * step);
    const float yawDifference = std::remainder(m_targetYaw - m_yaw, kFullTurn);
    m_yaw = std::remainder(m_yaw + yawDifference * blend, kFullTurn);
    m_targetYaw = m_yaw + yawDifference * (1.f - blend);
    m_pitch += (m_targetPitch - m_pitch) * blend;
    m_distance += (m_targetDistance - m_distance) * blend;
    applyScrollLook(step);
    applyPose();
}

bool OrbitCameraController::ensureNonPhysicalCamera() {
    if (!SmashLab::Components::available() || !SmashLab::Components::has(getEntity(), "cameraComponent")) {
        LOG_ERROR("Camera guard v3: entity=%u must be a Camera; component API unavailable or wrong script attachment",
                  getEntity().id);
        return false;
    }
    // The controller owns this entity's transform. An accidentally added body
    // in an unsaved editor world must not make the view fall under gravity.
    for (const char *body : {"rigidbody3dComponent", "rigidbody2dComponent"}) {
        if (!SmashLab::Components::has(getEntity(), body)) { continue; }
        if (!SmashLab::Components::remove(getEntity(), body)) {
            LOG_ERROR("Camera guard v3: could not detach %s from camera entity=%u", body, getEntity().id);
            return false;
        }
        LOG_WARN("Camera guard v3: detached %s from camera entity=%u; gravity cannot move this camera", body, getEntity().id);
    }
    return true;
}

void OrbitCameraController::clearKeyboardInput() {
    // Start empty and accept only keys delivered to the game UI in this Play
    // session. Stale native Shift/Cmd/W states cannot move or disable the camera.
    m_keyDown.fill(false);
    m_resetViewRequested = false;
}

bool OrbitCameraController::cameraKeyPressed(Bamboo::Key key) const {
    for (size_t i = 0; i < kCameraKeys.size(); ++i) {
        if (kCameraKeys[i] == key) { return m_keyDown[i]; }
    }
    return false;
}

void OrbitCameraController::applyScrollLook(float stepTime) {
    const float length = std::hypot(m_pendingScrollLook.x, m_pendingScrollLook.y);
    if (length < .001f * kRadians) { m_pendingScrollLook = {}; return; }
    if (stepTime <= 0.f) { return; }
    const float fraction = std::min(1.f - std::exp(-kScrollLookSmoothing * stepTime),
                                    kMaxScrollLookSpeed * stepTime / length);
    const float yawChange = m_pendingScrollLook.x * fraction;
    const float requestedPitchChange = m_pendingScrollLook.y * fraction;
    const float newPitch = std::clamp(m_pitch + requestedPitchChange, kMinPitch, kMaxPitch);
    const float pitchChange = newPitch - m_pitch;
    const float oldYaw = m_yaw;
    const float oldPitch = m_pitch;
    m_yaw += yawChange;
    m_pitch = newPitch;
    // Shift both displayed and requested angles: the orbit smoother must not
    // pull the view back after the gesture, or orbit during its look tail.
    m_targetYaw += yawChange;
    m_targetPitch = std::clamp(m_targetPitch + pitchChange, kMinPitch, kMaxPitch);
    m_pendingScrollLook.x -= yawChange;
    m_pendingScrollLook.y -= requestedPitchChange;
    if ((m_pitch >= kMaxPitch && m_pendingScrollLook.y > 0.f) ||
        (m_pitch <= kMinPitch && m_pendingScrollLook.y < 0.f)) { m_pendingScrollLook.y = 0.f; }

    // Orbit position = pivot + offset. Counteract only the scroll rotation's
    // offset change, at the current distance, so the eye stays in place while
    // looking around. Keyboard translation, Q/E and RMB still move normally.
    const float oldHorizontal = m_distance * std::cos(oldPitch);
    const float newHorizontal = m_distance * std::cos(m_pitch);
    m_pivot.x += oldHorizontal * std::sin(oldYaw) - newHorizontal * std::sin(m_yaw);
    m_pivot.y += m_distance * (std::sin(oldPitch) - std::sin(m_pitch));
    m_pivot.z += oldHorizontal * std::cos(oldYaw) - newHorizontal * std::cos(m_yaw);
}

void OrbitCameraController::shutdown() {
    m_active = false;
    m_moveVelocity = {};
    m_pendingScrollLook = {};
    m_hasAppliedPose = false;
    clearKeyboardInput();
    if (m_scrollInput) {
        m_scrollInput->controller = nullptr;
        m_scrollInput->orbitDelta = {};
    }
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

bool OrbitCameraController::getScenePointer(PointerSample &sample) const {
    if (!m_scrollInput || !m_scrollInput->hasPointer ||
        !m_scrollInput->scenePoint(m_scrollInput->pointer) ||
        Bamboo::Input::isMouseButtonPressed(Bamboo::MouseButton::RIGHT)) {
        return false;
    }
    const auto size = m_scrollInput->window.context().getViewportSize();
    if (size.width != m_scrollInput->pointerViewport.width || size.height != m_scrollInput->pointerViewport.height) {
        return false; // Wait for a new viewport-local event after a resize.
    }
    sample = {{m_scrollInput->pointer.x, m_scrollInput->pointer.y}, {size.width, size.height}, m_impactStrength};
    return true;
}

Bamboo::Vec2 OrbitCameraController::getSceneViewportSize() const {
    if (!m_scrollInput || !m_scrollInput->window.isValid()) { return {}; }
    const auto size = m_scrollInput->window.context().getViewportSize();
    if (!std::isfinite(size.width) || !std::isfinite(size.height) || size.width <= 0.f || size.height <= 0.f) {
        return {};
    }
    return {size.width, size.height};
}

bool OrbitCameraController::takeImpactClick(PointerSample &sample) {
    if (!m_scrollInput || !m_scrollInput->window.isValid()) { return false; }
    if (Bamboo::Input::isMouseButtonPressed(Bamboo::MouseButton::RIGHT)) {
        clearImpactClicks();
        return false;
    }
    const auto size = m_scrollInput->window.context().getViewportSize();
    while (!m_scrollInput->clicks.empty()) {
        sample = m_scrollInput->clicks.front();
        m_scrollInput->clicks.pop_front();
        if (sample.viewport.x == size.width && sample.viewport.y == size.height &&
            m_scrollInput->scenePoint({sample.position.x, sample.position.y})) {
            return true;
        }
    }
    return false;
}

void OrbitCameraController::clearImpactClicks() {
    if (m_scrollInput) { m_scrollInput->clicks.clear(); }
}

void OrbitCameraController::showInteractionMarker(bool available) {
    if (!m_scrollInput || !m_scrollInput->marker) { return; }
    PointerSample pointer;
    available = available && getScenePointer(pointer);
    if (available) { m_scrollInput->marker->layoutSetOrigin({pointer.position.x - 7.f, pointer.position.y - 7.f}); }
    m_scrollInput->marker->setHidden(!available);
    m_scrollInput->setCursor(available ? PandaUI::Cursor::PointingHand : PandaUI::Cursor::Arrow);
}

bool OrbitCameraController::acceptsSceneKeyboard() const {
    if (!m_active || !m_scrollInput || !m_scrollInput->window.isValid()) { return false; }
    if (m_scrollInput->shortcutModifiersDown) { return false; }
    auto &context = m_scrollInput->window.context();
    auto *focus = context.getFocusedView();
    return context.getRootView().get() == m_scrollInput.get() && !context.modals().isPresenting() &&
           (!focus || focus == m_scrollInput.get());
}

void OrbitCameraController::resetView() {
    const bool resetKeyHeld = m_keyDown.back();
    clearKeyboardInput();
    // Key-repeat while C is held must not generate a second reset request.
    m_keyDown.back() = resetKeyHeld;
    m_pivot = kTarget;
    m_moveVelocity = {};
    m_pendingScrollLook = {};
    m_yaw = m_targetYaw = m_initialYaw;
    m_pitch = m_targetPitch = m_initialPitch;
    m_distance = m_targetDistance = m_initialDistance;
    m_wasOrbiting = false;
    if (m_scrollInput) { m_scrollInput->orbitDelta = {}; }
    // Return directly to the known initial pose rather than flying through a
    // wall or letting a collision prevent C from completing its reset.
    applyPose(false);
}

void OrbitCameraController::applyPose(bool constrainMotion) {
    const float horizontal = m_distance * std::cos(m_pitch);
    const Bamboo::Vec3 offset{horizontal * std::sin(m_yaw), m_distance * std::sin(m_pitch),
                              horizontal * std::cos(m_yaw)};
    Bamboo::Vec3 position{m_pivot.x + offset.x, m_pivot.y + offset.y, m_pivot.z + offset.z};
    if (constrainMotion) {
        // Use the last controller pose, not a physics/foreign-script write as
        // a new orbit origin. Otherwise a drift can be fed back into the pivot.
        const auto previous = m_hasAppliedPose ? m_lastAppliedPosition : Bamboo::TransformComponentAPI::getPosition(getEntity());
        const Bamboo::Vec3 movement{position.x - previous.x, position.y - previous.y, position.z - previous.z};
        const float length = std::sqrt(movement.x * movement.x + movement.y * movement.y + movement.z * movement.z);
        if (std::isfinite(length) && length > 1.e-5f) {
            const Bamboo::Vec3 direction{movement.x / length, movement.y / length, movement.z / length};
            float allowed = length;
            // Seven parallel probes give a small clearance around the eye,
            // using existing solid-shape queries. This approximates a sweep;
            // the SDK exposes no sphere cast or camera collider controller.
            const std::array<Bamboo::Vec3, 7> probes = {{{0.f, 0.f, 0.f},
                {kCameraClearance, 0.f, 0.f}, {-kCameraClearance, 0.f, 0.f},
                {0.f, kCameraClearance, 0.f}, {0.f, -kCameraClearance, 0.f},
                {0.f, 0.f, kCameraClearance}, {0.f, 0.f, -kCameraClearance}}};
            for (auto probe : probes) {
                Bamboo::Physics3DAPI::RaycastHit hit;
                if (Bamboo::Physics3DAPI::raycast({previous.x + probe.x, previous.y + probe.y, previous.z + probe.z},
                    direction, length + kCollisionSkin, hit, getEntity()) && std::isfinite(hit.distance)) {
                    allowed = std::min(allowed, std::max(0.f, hit.distance - kCollisionSkin));
                }
            }
            if (allowed < length) {
                position = {previous.x + direction.x * allowed, previous.y + direction.y * allowed,
                            previous.z + direction.z * allowed};
                m_pivot = {position.x - offset.x, position.y - offset.y, position.z - offset.z};
                m_moveVelocity = {};
                // Discard blocked requests so releasing a key cannot later
                // resume a hidden move or orbit towards the old pivot.
                m_targetYaw = m_yaw;
                m_targetPitch = m_pitch;
                m_targetDistance = m_distance;
            }
        }
    }
    Bamboo::TransformComponentAPI::setPosition(getEntity(), position);
    m_lastAppliedPosition = position;
    m_hasAppliedPose = true;

    // qYaw * qPitch(-pitch): Panda's camera looks along local -Z with +Y up.
    // This points at the current movable pivot without roll or a pole flip.
    const float sinYaw = std::sin(m_yaw * 0.5f);
    const float cosYaw = std::cos(m_yaw * 0.5f);
    const float sinPitch = std::sin(m_pitch * 0.5f);
    const float cosPitch = std::cos(m_pitch * 0.5f);
    Bamboo::TransformComponentAPI::setRotation(
        getEntity(), {-cosYaw * sinPitch, sinYaw * cosPitch, sinYaw * sinPitch, cosYaw * cosPitch}
    );
}
