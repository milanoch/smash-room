#pragma once

#include "RoomCatalog.hpp"

#include <Bamboo/ApplicationAPI.hpp>
#include <Bamboo/WorldAPI.hpp>

#include <utility>

namespace SmashLab::MenuNavigation {

namespace Detail {
struct RoomLaunch {
    uint32_t worldAssetId = 0;
    float frameTime = -1.f;
};
inline RoomLaunch pendingLaunch;
} // namespace Detail

inline void menuStarted() {
    Detail::pendingLaunch = {};
}

inline void openRoom(const RoomDefinition &room) {
    if (!room.playable()) { return; }
    // The native editor/runtime executes the queued world load after callbacks in
    // THIS frame. Application time is constant throughout the frame (SDK contract).
    // A failed/cancelled request must not let a later Stop -> Play skip the menu.
    Detail::pendingLaunch = {room.worldAssetId, Bamboo::ApplicationAPI::getTime()};
    Bamboo::WorldAPI::load(Bamboo::WorldHandle(room.worldAssetId));
}

// Called at the room's start, before gameplay initialization. A direct editor Play
// goes to the separate menu world; an explicit menu selection enters the room once.
inline bool enterRoom(uint32_t worldAssetId) {
    const auto launch = std::exchange(Detail::pendingLaunch, Detail::RoomLaunch{});
    if (launch.worldAssetId == worldAssetId && launch.frameTime == Bamboo::ApplicationAPI::getTime()) {
        return true;
    }
    Bamboo::WorldAPI::load(Bamboo::WorldHandle(kMainMenuWorldAssetId));
    return false;
}

} // namespace SmashLab::MenuNavigation
