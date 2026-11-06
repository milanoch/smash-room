#pragma once

#include <Bamboo/Script.hpp>
#include <Bamboo/Bamboo.hpp>

// Animates only the native point light on this entity, while Bamboo runs Play.
class RgbLampController final : public Bamboo::Script {
public:
    void start() override;
    void update(float deltaTime) override;

private:
    void applyColor();

    static constexpr float kCycleSeconds = 18.f;
    float m_elapsed = 0.f;
    bool m_ready = false;
};

REGISTER_SCRIPT(RgbLampController)
