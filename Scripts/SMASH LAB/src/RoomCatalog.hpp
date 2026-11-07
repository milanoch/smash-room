#pragma once

#include <array>
#include <cstdint>

namespace SmashLab {

// Real IDs from the editor-generated sidecars; never create .pmeta files by hand.
inline constexpr uint32_t kMainMenuWorldAssetId = 3557474705u;
inline constexpr uint32_t kTestRoomWorldAssetId = 1673465096u;

enum class RoomAvailability { InDevelopment, Ready };

struct RoomDefinition {
    const char *key;
    const char *name;
    const char *nameRu;
    const char *description;
    const char *descriptionRu;
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
// prototype is exposed separately as a test room, launched only by a player action.
inline const std::array<RoomDefinition, 2> kRooms = {{
    {"kitchen", "Kitchen", "Кухня", "A new space for small objects and big chain reactions.",
     "Новое пространство для небольших предметов и больших цепных реакций.",
     "Assets/Kitchen.world", 0u, 0u, RoomAvailability::InDevelopment},
    {"livingroom_test", "Test Room", "Тестовая комната",
     "Development playground for physics, furniture and experimental systems.",
     "Полигон для проверки физики, мебели и экспериментальных систем.",
     "Assets/LivingRoom_Prototype.world", 4054835356u, kTestRoomWorldAssetId, RoomAvailability::Ready}
}};

} // namespace SmashLab
