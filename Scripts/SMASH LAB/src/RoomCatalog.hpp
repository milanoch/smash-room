#pragma once

#include <array>
#include <cstdint>

namespace SmashLab {

enum class RoomAvailability { InDevelopment, Ready };

struct RoomDefinition {
    const char *key;
    const char *name;
    const char *description;
    const char *worldPath; // authoring reference; the SDK loads the asset ID below
    uint32_t previewAssetId;
    uint32_t worldAssetId;
    RoomAvailability availability;

    bool playable() const {
        return availability == RoomAvailability::Ready && worldAssetId != 0;
    }
};

// Add entries here without changing the menu widgets. When a room is ready,
// register its OWN .world in PandaEditor, copy its .pmeta ID and set Ready.
// Kitchen stays unavailable until explicitly connected. The living-room
// prototype is a development sandbox and deliberately is not in this catalog.
inline const std::array<RoomDefinition, 1> kRooms = {{
    {"kitchen", "Kitchen", "A new space for small objects and big chain reactions.",
     "Assets/Kitchen.world", 4017666450u, 0u, RoomAvailability::InDevelopment}
}};

} // namespace SmashLab
