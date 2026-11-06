#include "FurnitureDestructionSystem.hpp"

#include <Bamboo/Components/Rigidbody3DComponentAPI.hpp>
#include <Bamboo/Components/TransformComponentAPI.hpp>
#include <Bamboo/Logger.hpp>
#include <Bamboo/WorldAPI.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace SmashLab {
namespace {

using Bamboo::EntityHandle;
using Bamboo::Vec3;

FurnitureDestructionSystem::Pose getPose(EntityHandle entity) {
    return {Bamboo::TransformComponentAPI::getPosition(entity),
            Bamboo::TransformComponentAPI::getRotation(entity),
            Bamboo::TransformComponentAPI::getScale(entity)};
}

void setPose(EntityHandle entity, const FurnitureDestructionSystem::Pose &pose) {
    const auto scale = Bamboo::TransformComponentAPI::getScale(entity);
    if (scale.x != pose.scale.x || scale.y != pose.scale.y || scale.z != pose.scale.z) {
        Bamboo::TransformComponentAPI::setScale(entity, pose.scale);
    }
    Bamboo::TransformComponentAPI::setPosition(entity, pose.position);
    Bamboo::TransformComponentAPI::setRotation(entity, pose.rotation);
}

glm::vec3 vector(Vec3 value) { return {value.x, value.y, value.z}; }
Vec3 vector(glm::vec3 value) { return {value.x, value.y, value.z}; }

bool worldPosition(EntityHandle handle, glm::vec3 &position) {
    auto *world = Panda::GameContext::getCurrentWorld();
    if (!world || handle.id == BAMBOO_INVALID_HANDLE) { return false; }
    auto entity = world->getById(Panda::UUID(handle.id));
    if (!entity.isValid()) { return false; }
    const glm::mat4 transform = world->getWorldSpaceTransformMatrix(entity);
    position = glm::vec3(transform[3]);
    return true;
}

bool readSourceCollider(EntityHandle handle, Panda::BoxCollider3DComponent &collider) {
    auto *world = Panda::GameContext::getCurrentWorld();
    if (!world || handle.id == BAMBOO_INVALID_HANDLE) { return false; }
    auto entity = world->getById(Panda::UUID(handle.id));
    if (!entity.isValid() || !entity.hasComponent<Panda::BoxCollider3DComponent>()) { return false; }
    collider = entity.getComponent<Panda::BoxCollider3DComponent>();
    std::fill(std::begin(collider.runtimeData), std::end(collider.runtimeData), 0);
    return true;
}

bool addSourceCollider(EntityHandle handle, Panda::BoxCollider3DComponent collider) {
    auto *world = Panda::GameContext::getCurrentWorld();
    if (!world || handle.id == BAMBOO_INVALID_HANDLE) { return false; }
    auto entity = world->getById(Panda::UUID(handle.id));
    if (!entity.isValid() || entity.hasComponent<Panda::BoxCollider3DComponent>()) { return false; }
    std::fill(std::begin(collider.runtimeData), std::end(collider.runtimeData), 0);
    entity.addComponent<Panda::BoxCollider3DComponent>(collider);
    return entity.hasComponent<Panda::BoxCollider3DComponent>();
}

bool removeSourceCollider(EntityHandle handle) {
    auto *world = Panda::GameContext::getCurrentWorld();
    if (!world || handle.id == BAMBOO_INVALID_HANDLE) { return false; }
    auto entity = world->getById(Panda::UUID(handle.id));
    if (!entity.isValid()) { return false; }
    if (entity.hasComponent<Panda::BoxCollider3DComponent>()) {
        entity.removeComponent<Panda::BoxCollider3DComponent>();
    }
    return !entity.hasComponent<Panda::BoxCollider3DComponent>();
}

bool readFragmentCollider(EntityHandle handle, Panda::MeshCollider3DComponent &collider) {
    auto *world = Panda::GameContext::getCurrentWorld();
    if (!world || handle.id == BAMBOO_INVALID_HANDLE) { return false; }
    auto entity = world->getById(Panda::UUID(handle.id));
    if (!entity.isValid() || !entity.hasComponent<Panda::MeshCollider3DComponent>()) { return false; }
    collider = entity.getComponent<Panda::MeshCollider3DComponent>();
    std::fill(std::begin(collider.runtimeData), std::end(collider.runtimeData), 0);
    return true;
}

bool addFragmentCollider(EntityHandle handle, Panda::MeshCollider3DComponent collider) {
    auto *world = Panda::GameContext::getCurrentWorld();
    if (!world || handle.id == BAMBOO_INVALID_HANDLE) { return false; }
    auto entity = world->getById(Panda::UUID(handle.id));
    if (!entity.isValid() || entity.hasComponent<Panda::MeshCollider3DComponent>()) { return false; }
    std::fill(std::begin(collider.runtimeData), std::end(collider.runtimeData), 0);
    entity.addComponent<Panda::MeshCollider3DComponent>(collider);
    return entity.hasComponent<Panda::MeshCollider3DComponent>();
}

bool removeFragmentCollider(EntityHandle handle) {
    auto *world = Panda::GameContext::getCurrentWorld();
    if (!world || handle.id == BAMBOO_INVALID_HANDLE) { return false; }
    auto entity = world->getById(Panda::UUID(handle.id));
    if (!entity.isValid()) { return false; }
    if (entity.hasComponent<Panda::MeshCollider3DComponent>()) {
        entity.removeComponent<Panda::MeshCollider3DComponent>();
    }
    return !entity.hasComponent<Panda::MeshCollider3DComponent>();
}

} // namespace

std::vector<FurnitureDefinition> livingRoomFurnitureDefinitions() {
    return {
        {"CoffeeTable", 3.f,
         {"CoffeeTable_Leg_01", "CoffeeTable_Leg_02", "CoffeeTable_Leg_03", "CoffeeTable_Leg_04",
          "CoffeeTable_LowerShelf", "CoffeeTable_Tabletop"}},
        {"TVStand", 3.f,
         {"TVStand_Back", "TVStand_Bottom", "TVStand_Door_Left", "TVStand_Door_Right", "TVStand_Foot_01",
          "TVStand_Foot_02", "TVStand_Foot_03", "TVStand_Foot_04", "TVStand_Handle_L", "TVStand_Handle_R",
          "TVStand_MiddleShelf", "TVStand_Side_Left", "TVStand_Side_Right", "TVStand_Top"}}
    };
}

bool FurnitureDestructionSystem::initialize(std::vector<FurnitureDefinition> definitions) {
    m_assemblies.clear();
    auto *world = Panda::GameContext::getCurrentWorld();
    if (!world) {
        LOG_ERROR("Furniture destruction: no current Panda world.");
        return false;
    }
    std::unordered_set<uint32_t> usedIds;
    for (const FurnitureDefinition &definition : definitions) {
        if (!definition.sourceTag || !std::isfinite(definition.breakStrength) || definition.breakStrength <= 0.f ||
            definition.partTags.empty()) {
            LOG_ERROR("Furniture destruction: invalid breakable definition.");
            return false;
        }
        const auto sources = Bamboo::WorldAPI::findAllByTag(definition.sourceTag);
        if (sources.empty()) { continue; }
        if (sources.size() != 1 || Bamboo::Rigidbody3DComponentAPI::getMass(sources.front()) <= 0.f ||
            !usedIds.insert(sources.front().id).second) {
            LOG_ERROR("Furniture destruction: expected one intact Dynamic source named %s.", definition.sourceTag);
            return false;
        }
        Assembly assembly;
        assembly.source = sources.front();
        assembly.initial = getPose(assembly.source);
        assembly.breakStrength = definition.breakStrength;
        if (!readSourceCollider(assembly.source, assembly.sourceCollider)) {
            LOG_ERROR("Furniture destruction: %s needs its authored Box Collider 3D.", definition.sourceTag);
            return false;
        }
        for (const char *partTag : definition.partTags) {
            const auto parts = Bamboo::WorldAPI::findAllByTag(partTag);
            if (parts.size() != 1) {
                LOG_ERROR("Furniture destruction: expected one prepared part named %s.", partTag);
                return false;
            }
            auto owner = world->getById(Panda::UUID(parts.front().id));
            if (!owner.isValid()) { return false; }
            EntityHandle fragmentEntity;
            for (const Panda::UUID childId : owner.getChildEntities()) {
                auto child = world->getById(childId);
                if (!child.isValid() || !child.hasComponent<Panda::ImportedSubmeshComponent>()) { continue; }
                if (fragmentEntity.id != BAMBOO_INVALID_HANDLE) {
                    LOG_ERROR("Furniture destruction: part %s must have one imported mesh child.", partTag);
                    return false;
                }
                fragmentEntity = EntityHandle(static_cast<uint32_t>(child.getId()));
            }
            if (fragmentEntity.id == BAMBOO_INVALID_HANDLE ||
                Bamboo::Rigidbody3DComponentAPI::getMass(fragmentEntity) <= 0.f ||
                !usedIds.insert(fragmentEntity.id).second) {
                LOG_ERROR("Furniture destruction: %s needs a Dynamic mesh child with a convex collider.", partTag);
                return false;
            }
            Fragment fragment{fragmentEntity, getPose(fragmentEntity), {}};
            if (!readFragmentCollider(fragmentEntity, fragment.collider) || !fragment.collider.convex) {
                LOG_ERROR("Furniture destruction: %s must use a convex Mesh Collider 3D.", partTag);
                return false;
            }
            assembly.fragments.push_back(std::move(fragment));
        }
        m_assemblies.push_back(std::move(assembly));
    }

    // Physics starts before scripts. Remove the prepared fragment bodies now, before
    // the first fixed step; their authored settings remain in these snapshots.
    std::unordered_set<uint32_t> removed;
    for (Assembly &assembly : m_assemblies) {
        for (const Fragment &fragment : assembly.fragments) {
            if (!removeFragmentCollider(fragment.entity)) {
                for (const Assembly &other : m_assemblies) {
                    for (const Fragment &item : other.fragments) {
                        if (removed.contains(item.entity.id) && !addFragmentCollider(item.entity, item.collider)) {
                            LOG_ERROR("Furniture destruction: could not roll back fragment activation.");
                        }
                    }
                }
                m_assemblies.clear();
                LOG_ERROR("Furniture destruction: could not deactivate prepared fragments.");
                return false;
            }
            removed.insert(fragment.entity.id);
        }
    }
    return true;
}

bool FurnitureDestructionSystem::isSource(EntityHandle entity) const {
    for (const Assembly &assembly : m_assemblies) {
        if (assembly.source.id == entity.id) { return true; }
    }
    return false;
}

bool FurnitureDestructionSystem::impact(EntityHandle entity, Bamboo::Vec3 point, Bamboo::Vec3 direction,
                                        float strength) {
    for (Assembly &assembly : m_assemblies) {
        if (assembly.source.id != entity.id) { continue; }
        if (assembly.broken || !std::isfinite(strength) || strength < assembly.breakStrength) { return false; }
        const glm::vec3 incoming = vector(direction);
        const float length = glm::length(incoming);
        const float mass = Bamboo::Rigidbody3DComponentAPI::getMass(assembly.source);
        if (!std::isfinite(length) || length < 1.e-5f || !std::isfinite(mass) || mass <= 0.f) { return false; }
        const glm::vec3 impulse = incoming / length * (mass * strength);
        Bamboo::Rigidbody3DComponentAPI::applyLinearImpulseAtPoint(assembly.source, vector(impulse), point);
        if (!breakAssembly(assembly)) {
            LOG_ERROR("Furniture destruction: activation failed; the intact body was restored.");
        }
        return true;
    }
    return false; // Ordinary items and already-free fragments use Impact Tool.
}

bool FurnitureDestructionSystem::breakAssembly(Assembly &assembly) {
    if (assembly.broken) { return false; }
    const auto massData = Bamboo::Rigidbody3DComponentAPI::getMassData(assembly.source);
    if (massData.mass <= 0.f) { return false; }
    const glm::vec3 sourceCenter = vector(massData.center);
    const glm::vec3 linear = vector(Bamboo::Rigidbody3DComponentAPI::getLinearVelocity(assembly.source));
    const glm::vec3 angular = vector(Bamboo::Rigidbody3DComponentAPI::getAngularVelocity(assembly.source));
    std::vector<glm::vec3> positions;
    positions.reserve(assembly.fragments.size());
    for (const Fragment &fragment : assembly.fragments) {
        glm::vec3 position;
        if (!worldPosition(fragment.entity, position)) { return false; }
        positions.push_back(position);
    }
    if (!removeSourceCollider(assembly.source)) { return false; }

    for (size_t i = 0; i < assembly.fragments.size(); ++i) {
        Fragment &fragment = assembly.fragments[i];
        if (!addFragmentCollider(fragment.entity, fragment.collider) ||
            Bamboo::Rigidbody3DComponentAPI::getMass(fragment.entity) <= 0.f) {
            restoreAssembly(assembly);
            return false;
        }
        const glm::vec3 inherited = linear + glm::cross(angular, positions[i] - sourceCenter);
        Bamboo::Rigidbody3DComponentAPI::setLinearVelocity(fragment.entity, vector(inherited));
        Bamboo::Rigidbody3DComponentAPI::setAngularVelocity(fragment.entity, vector(angular));
    }
    assembly.broken = true;
    return true;
}

bool FurnitureDestructionSystem::restoreAssembly(Assembly &assembly) {
    for (const Fragment &fragment : assembly.fragments) {
        if (!removeFragmentCollider(fragment.entity)) {
            LOG_ERROR("Furniture destruction: Reset could not remove a fragment body.");
            return false;
        }
    }
    for (const Fragment &fragment : assembly.fragments) { setPose(fragment.entity, fragment.initial); }
    setPose(assembly.source, assembly.initial);
    auto *world = Panda::GameContext::getCurrentWorld();
    if (!world) { return false; }
    auto source = world->getById(Panda::UUID(assembly.source.id));
    if (!source.isValid()) { return false; }
    if (!source.hasComponent<Panda::BoxCollider3DComponent>() &&
        !addSourceCollider(assembly.source, assembly.sourceCollider)) {
        LOG_ERROR("Furniture destruction: Reset could not restore the intact body.");
        return false;
    }
    if (Bamboo::Rigidbody3DComponentAPI::getMass(assembly.source) <= 0.f) { return false; }
    Bamboo::Rigidbody3DComponentAPI::setLinearVelocity(assembly.source, {});
    Bamboo::Rigidbody3DComponentAPI::setAngularVelocity(assembly.source, {});
    assembly.broken = false;
    return true;
}

bool FurnitureDestructionSystem::restore() {
    for (Assembly &assembly : m_assemblies) {
        if (!restoreAssembly(assembly)) { return false; }
    }
    return true;
}

void FurnitureDestructionSystem::shutdown() {
    for (Assembly &assembly : m_assemblies) { restoreAssembly(assembly); }
    m_assemblies.clear();
}

} // namespace SmashLab
