#pragma once

#include <Bamboo/Base.hpp>
#include <Bamboo/Internal/MainThread.hpp>
#include <Panda/ScriptEngine/ABI/ScriptApi.hpp>

// The packaged Bamboo EntityAPI wraps component creation but omits the existing
// has/remove calls. These are real, exported PandaSDK function-pointer variables;
// their signatures come from the published host ABI. No engine library is
// linked into the game bundle, and no address lookup or memory offset is used.
// Validated against ABI 32 and 33. Version 33 adds application focus/pause
// callbacks; these two SDK-declared functions and exported variables are
// unchanged. We never index into a host table or assume its binary offsets.
static_assert(Panda::Scripting::SCRIPT_ABI_VERSION == 32 ||
              Panda::Scripting::SCRIPT_ABI_VERSION == 33,
              "Review SdkComponentAccess against the new PandaSDK host ABI.");

namespace Panda::ExternalCalls {

extern Scripting::EntityHasComponentFn entity_HasComponent;
extern Scripting::EntityRemoveComponentFn entity_RemoveComponent;

} // namespace Panda::ExternalCalls

namespace SmashLab::Components {

inline bool available() {
    return Panda::ExternalCalls::entity_HasComponent != nullptr &&
           Panda::ExternalCalls::entity_RemoveComponent != nullptr;
}

inline bool has(Bamboo::EntityHandle entity, const char *componentName) {
    if (!available() || !entity.isValid() || componentName == nullptr || componentName[0] == '\0' ||
        !Bamboo::Internal::requireMainThreadApi("SmashLab::Components::has")) {
        return false;
    }
    return Panda::ExternalCalls::entity_HasComponent(entity.id, componentName);
}

inline bool remove(Bamboo::EntityHandle entity, const char *componentName) {
    if (!available() || !entity.isValid() || componentName == nullptr || componentName[0] == '\0' ||
        !Bamboo::Internal::requireMainThreadApi("SmashLab::Components::remove")) {
        return false;
    }
    if (!Panda::ExternalCalls::entity_HasComponent(entity.id, componentName)) { return true; }
    Panda::ExternalCalls::entity_RemoveComponent(entity.id, componentName);
    return !Panda::ExternalCalls::entity_HasComponent(entity.id, componentName);
}

} // namespace SmashLab::Components
