#pragma once

#include <Bamboo/Bamboo.hpp>
#include <Bamboo/Script.hpp>

#include <memory>

// MainMenu.world only. No published fields or guessed manifest field IDs.
// Room navigation uses Bamboo's deferred WorldAPI::load; no custom level loader.
class GameMenuController final : public Bamboo::Script {
public:
    void start() override;
    void update(float deltaTime) override;
    void shutdown() override;

private:
    struct State;
    std::shared_ptr<State> m_state;
};

REGISTER_SCRIPT(GameMenuController)
