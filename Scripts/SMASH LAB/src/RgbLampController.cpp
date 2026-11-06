#include "RgbLampController.hpp"

#include <Bamboo/Logger.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>

#include <algorithm>
#include <cmath>

namespace {

// Bamboo ABI 33 exposes directional-light setters but no point-light setters.
// Use the public native SDK instead: the five required symbols are exported by
// the installed macOS editor. No engine library is linked into the game bundle,
// no memory offsets/private object layouts are used, and no renderer is replaced.
Panda::PointLightComponent *pointLight(Bamboo::EntityHandle handle) {
    auto *world = Panda::GameContext::getCurrentWorld();
    if (!world || !handle.isValid()) { return nullptr; }
    auto entity = world->getById(Panda::UUID(handle.id));
    if (!entity.isValid() || !entity.hasComponent<Panda::PointLightComponent>()) { return nullptr; }
    return &entity.getComponent<Panda::PointLightComponent>();
}

} // namespace

void RgbLampController::start() {
    m_elapsed = 0.f;
    m_ready = pointLight(getEntity()) != nullptr;
    if (!m_ready) {
        LOG_ERROR("RGB lamp: attach RgbLampController to an entity with Point Light.");
        return;
    }
    applyColor();
}

void RgbLampController::update(float deltaTime) {
    if (!m_ready || !std::isfinite(deltaTime) || deltaTime <= 0.f) { return; }
    // Framerate-independent motion, with no sudden color jump after a long stall.
    m_elapsed = std::fmod(m_elapsed + std::min(deltaTime, 0.1f), kCycleSeconds);
    applyColor();
}

void RgbLampController::applyColor() {
    auto *light = pointLight(getEntity());
    if (!light) { m_ready = false; return; }
    constexpr float tau = 6.28318530718f;
    const float phase = tau * m_elapsed / kCycleSeconds;
    const auto channel = [](float angle) { return 0.12f + 0.88f * (0.5f + 0.5f * std::cos(angle)); };
    light->color = Panda::Color(channel(phase - tau / 3.f), channel(phase + tau / 3.f), channel(phase), 1.f);
}
