#include "DestructionSystem.hpp"

#include "DestructionGeometry.hpp"
#include "SdkComponentAccess.hpp"

#include <Bamboo/Assets/AssetManagerAPI.hpp>
#include <Bamboo/Assets/MeshAPI.hpp>
#include <Bamboo/Components/MeshComponentAPI.hpp>
#include <Bamboo/Components/Rigidbody3DComponentAPI.hpp>
#include <Bamboo/Components/TransformComponentAPI.hpp>
#include <Bamboo/EntityAPI.hpp>
#include <Bamboo/Logger.hpp>
#include <Bamboo/WorldAPI.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace SmashLab {
namespace {

constexpr const char *kBox = "boxCollider3dComponent";
constexpr const char *kMesh = "runtimeMeshComponent";
constexpr const char *kPrimitive = "primitiveMeshComponent";
constexpr const char *kBody = "rigidbody3dComponent";

glm::vec3 vector(Bamboo::Vec3 value) { return {value.x, value.y, value.z}; }
Bamboo::Vec3 vector(glm::vec3 value) { return {value.x, value.y, value.z}; }

glm::vec3 unit(glm::vec3 value, glm::vec3 fallback = {0.f, 1.f, 0.f}) {
    const float length = glm::length(value);
    return std::isfinite(length) && length > 1.e-5f ? value / length : fallback;
}

bool positive(Bamboo::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           value.x > 0.f && value.y > 0.f && value.z > 0.f;
}

bool finite(Bamboo::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

bool DestructionSystem::initialize(BreakableDefinition definition) {
    if (m_initialized || !Components::available() || !definition.surfaceMaterial.isValid() ||
        !definition.sourceTag || !*definition.sourceTag ||
        definition.fragments.empty() || definition.fragments.size() > 12 ||
        !std::isfinite(definition.strength) || definition.strength <= 0.f ||
        !std::isfinite(definition.inheritedImpactRatio) || definition.inheritedImpactRatio < 0.f ||
        !std::isfinite(definition.radialSpeed) || definition.radialSpeed < 0.f ||
        !std::isfinite(definition.maxScatterSpeed) || definition.maxScatterSpeed <= 0.f ||
        definition.physics.density != 1.f || definition.physics.friction != .5f ||
        definition.physics.restitution != 0.f) {
        LOG_ERROR("Destruction: component ABI unavailable or unsupported prepared-fragment configuration.");
        return false;
    }
    const auto sources = Bamboo::WorldAPI::findAllByTag(definition.sourceTag);
    if (sources.size() != 1 || Bamboo::Rigidbody3DComponentAPI::getMass(sources.front()) <= 0.f ||
        !Components::has(sources.front(), kBox) || !Components::has(sources.front(), kPrimitive) ||
        Components::has(sources.front(), "sphereCollider3dComponent") ||
        Components::has(sources.front(), "capsuleCollider3dComponent")) {
        LOG_ERROR("Destruction: expected one intact Dynamic box with a primitive mesh.");
        return false;
    }
    m_source = sources.front();
    m_initial = {Bamboo::TransformComponentAPI::getPosition(m_source),
                 Bamboo::TransformComponentAPI::getRotation(m_source),
                 Bamboo::TransformComponentAPI::getScale(m_source)};
    if (!positive(m_initial.scale)) { return false; }
    std::unordered_set<uint32_t> ids{m_source.id};
    m_fragments.clear();
    for (const FragmentData &data : definition.fragments) {
        if (!data.tag || !*data.tag || !finite(data.center) || !positive(data.size)) { return false; }
        const glm::vec3 extent = glm::abs(vector(data.center)) + vector(data.size) * .5f;
        if (glm::any(glm::greaterThan(extent, glm::vec3(.50001f)))) {
            LOG_ERROR("Destruction: a prepared fragment extends beyond the intact box.");
            return false;
        }
        const auto matches = Bamboo::WorldAPI::findAllByTag(data.tag);
        if (matches.size() != 1 || !ids.insert(matches.front().id).second || !positive(data.size) ||
            !Components::has(matches.front(), kBody) || Components::has(matches.front(), kBox) ||
            Components::has(matches.front(), "sphereCollider3dComponent") ||
            Components::has(matches.front(), "capsuleCollider3dComponent") ||
            Components::has(matches.front(), kMesh) || Components::has(matches.front(), kPrimitive)) {
            LOG_ERROR("Destruction: prepared fragment %s is missing or already active.", data.tag);
            m_fragments.clear();
            return false;
        }
        const auto entity = matches.front();
        m_fragments.push_back({entity, {Bamboo::TransformComponentAPI::getPosition(entity),
                                      Bamboo::TransformComponentAPI::getRotation(entity),
                                      Bamboo::TransformComponentAPI::getScale(entity)}});
    }
    for (size_t i = 0; i < definition.fragments.size(); ++i) {
        for (size_t j = i + 1; j < definition.fragments.size(); ++j) {
            const auto &a = definition.fragments[i];
            const auto &b = definition.fragments[j];
            const glm::vec3 overlap = (vector(a.size) + vector(b.size)) * .5f -
                                      glm::abs(vector(a.center) - vector(b.center));
            if (glm::all(glm::greaterThan(overlap, glm::vec3(1.e-5f)))) {
                LOG_ERROR("Destruction: prepared fragment colliders overlap.");
                return false;
            }
        }
    }
    m_definition = std::move(definition);
    m_mesh = Bamboo::AssetManagerAPI::createMesh();
    if (!m_mesh.isValid()) {
        LOG_ERROR("Destruction: could not create the shared box mesh.");
        return false;
    }
    Bamboo::MeshAPI::update(m_mesh, makeEdgedBoxMesh());
    // Only the Play-world copy changes. The original .world keeps its primitive
    // for editing. One runtime mesh provides face borders to source and shards.
    if (!show(m_source) || !Components::remove(m_source, kPrimitive)) {
        Components::remove(m_source, kMesh);
        Bamboo::AssetManagerAPI::deleteMesh(m_mesh);
        m_mesh = {};
        LOG_ERROR("Destruction: could not replace the source's visual component.");
        return false;
    }
    m_state = {};
    m_initialized = true;
    return true;
}

bool DestructionSystem::isSource(Bamboo::EntityHandle entity) const {
    return m_initialized && entity.id == m_source.id;
}

bool DestructionSystem::show(Bamboo::EntityHandle entity) {
    if (!Components::has(entity, kMesh)) {
        Bamboo::EntityAPI::addComponent(entity, Bamboo::ComponentType::MESH_COMPONENT);
    }
    if (!Components::has(entity, kMesh)) { return false; }
    Bamboo::MeshComponentAPI::setMesh(entity, m_mesh);
    Bamboo::MeshComponentAPI::setMaterial(entity, m_definition.surfaceMaterial);
    return true;
}

bool DestructionSystem::impact(Bamboo::EntityHandle entity, Bamboo::Vec3 point, Bamboo::Vec3 direction, float strength) {
    if (!isSource(entity)) { return false; } // Ordinary items retain the existing Impact Tool.
    if (m_state.state() != DestructionState::Intact) { return true; }
    if (!finite(point) || !finite(direction) || glm::length(vector(direction)) < 1.e-5f ||
        !std::isfinite(strength) || strength < m_definition.strength) { return false; }
    if (!m_state.beginBreak()) { return true; }

    const glm::vec3 position = vector(Bamboo::TransformComponentAPI::getPosition(m_source));
    const auto savedRotation = Bamboo::TransformComponentAPI::getRotation(m_source);
    const glm::quat rotation = glm::normalize(glm::quat(savedRotation.w, savedRotation.x, savedRotation.y, savedRotation.z));
    const glm::vec3 scale = vector(Bamboo::TransformComponentAPI::getScale(m_source));
    const glm::vec3 velocity = vector(Bamboo::Rigidbody3DComponentAPI::getLinearVelocity(m_source));
    const glm::vec3 angular = vector(Bamboo::Rigidbody3DComponentAPI::getAngularVelocity(m_source));
    const float sourceMass = Bamboo::Rigidbody3DComponentAPI::getMass(m_source);
    const glm::vec3 impactDirection = unit(vector(direction), {0.f, 0.f, -1.f});

    // These hooks synchronously remove the solver body before any new body is
    // created. There is never an intact collider under the physical fragments.
    if (!Components::remove(m_source, kBox) || !Components::remove(m_source, kMesh)) { return failBreak(); }
    if (Bamboo::Rigidbody3DComponentAPI::getMass(m_source) != 0.f) { return failBreak(); }

    std::vector<glm::vec3> centers;
    std::vector<glm::vec3> scatter;
    std::vector<float> masses;
    centers.reserve(m_fragments.size());
    scatter.reserve(m_fragments.size());
    masses.reserve(m_fragments.size());
    glm::vec3 weightedScatter(0.f);
    float totalMass = 0.f;
    for (size_t i = 0; i < m_fragments.size(); ++i) {
        const auto fragment = m_fragments[i].entity;
        const auto &data = m_definition.fragments[i];
        const glm::vec3 center = position + rotation * (vector(data.center) * scale);
        Bamboo::TransformComponentAPI::setScale(fragment, vector(vector(data.size) * scale));
        Bamboo::TransformComponentAPI::setPosition(fragment, vector(center));
        Bamboo::TransformComponentAPI::setRotation(fragment, savedRotation);
        if (!show(fragment)) { return failBreak(); }
        // The authored Dynamic component remains; adding a collider creates a
        // real Box3D dynamic body with the SDK's documented default parameters.
        Bamboo::EntityAPI::addComponent(fragment, kBox);
        const float mass = Bamboo::Rigidbody3DComponentAPI::getMass(fragment);
        if (!std::isfinite(mass) || mass <= 0.f) { return failBreak(); }
        const glm::vec3 outward = unit(center - vector(point)) * m_definition.radialSpeed +
                                  unit(center - position) * .25f;
        centers.push_back(center);
        scatter.push_back(outward);
        masses.push_back(mass);
        weightedScatter += outward * mass;
        totalMass += mass;
    }
    if (!std::isfinite(totalMass) || sourceMass <= 0.f || totalMass < sourceMass * .8f || totalMass > sourceMass * 1.2f) {
        LOG_ERROR("Destruction: fragment mass does not match the source; restoring the intact cube.");
        return failBreak();
    }
    // Remove the mass-weighted radial drift, keeping the overall push directed
    // into the room. Bound scatter as a group, without changing its zero mean.
    const glm::vec3 meanScatter = weightedScatter / totalMass;
    float maxScatter = 0.f;
    for (auto &value : scatter) { value -= meanScatter; maxScatter = std::max(maxScatter, glm::length(value)); }
    const float scatterScale = maxScatter > m_definition.maxScatterSpeed ? m_definition.maxScatterSpeed / maxScatter : 1.f;
    for (size_t i = 0; i < m_fragments.size(); ++i) {
        const auto fragment = m_fragments[i].entity;
        const glm::vec3 offset = centers[i] - position;
        Bamboo::Rigidbody3DComponentAPI::setLinearVelocity(fragment, vector(velocity + glm::cross(angular, offset)));
        const glm::vec3 extraVelocity = impactDirection * (strength * m_definition.inheritedImpactRatio) + scatter[i] * scatterScale;
        Bamboo::Rigidbody3DComponentAPI::applyLinearImpulse(fragment, vector(extraVelocity * masses[i]));
        const float alternating = i % 2 == 0 ? .8f : -.8f;
        const glm::vec3 spin = unit(glm::cross(impactDirection, unit(offset)), {1.f, 0.f, 0.f}) * alternating;
        Bamboo::Rigidbody3DComponentAPI::setAngularVelocity(fragment, vector(angular + spin));
    }
    m_state.finishBreak();
    return true;
}

bool DestructionSystem::failBreak() {
    LOG_ERROR("Destruction: fragment activation failed; rolling back to the intact object.");
    if (!restore()) { LOG_ERROR("Destruction: rollback failed; stop Play and inspect the scene configuration."); }
    return true; // Consume this hit; do not also apply an impulse to a missing body.
}

bool DestructionSystem::clearFragments() {
    bool ok = true;
    for (const Fragment &fragment : m_fragments) {
        // Component removal follows World/Physics lifecycle hooks immediately;
        // no deferred entity destruction can overlap the restored source body.
        const bool bodyRemoved = Components::remove(fragment.entity, kBox);
        const bool meshRemoved = Components::remove(fragment.entity, kMesh);
        ok = bodyRemoved && meshRemoved && ok;
        if (bodyRemoved) {
            Bamboo::TransformComponentAPI::setScale(fragment.entity, fragment.initial.scale);
            Bamboo::TransformComponentAPI::setPosition(fragment.entity, fragment.initial.position);
            Bamboo::TransformComponentAPI::setRotation(fragment.entity, fragment.initial.rotation);
        }
    }
    return ok;
}

bool DestructionSystem::restore() {
    if (!m_initialized || !m_state.beginReset()) { return false; }
    if (!clearFragments()) {
        LOG_ERROR("Destruction: reset stopped because a fragment body could not be removed.");
        return false;
    }
    Bamboo::TransformComponentAPI::setScale(m_source, m_initial.scale);
    Bamboo::TransformComponentAPI::setPosition(m_source, m_initial.position);
    Bamboo::TransformComponentAPI::setRotation(m_source, m_initial.rotation);
    if (!show(m_source)) { return false; }
    if (!Components::has(m_source, kBox)) { Bamboo::EntityAPI::addComponent(m_source, kBox); }
    if (Bamboo::Rigidbody3DComponentAPI::getMass(m_source) <= 0.f) {
        LOG_ERROR("Destruction: intact collider could not be restored.");
        return false;
    }
    Bamboo::Rigidbody3DComponentAPI::setLinearVelocity(m_source, {});
    Bamboo::Rigidbody3DComponentAPI::setAngularVelocity(m_source, {});
    return m_state.finishReset();
}

void DestructionSystem::shutdown() {
    if (m_initialized) {
        clearFragments();
        Components::remove(m_source, kMesh);
    }
    if (m_mesh.isValid()) {
        Bamboo::AssetManagerAPI::deleteMesh(m_mesh);
        m_mesh = {};
    }
    m_fragments.clear();
    m_initialized = false;
    m_source = {};
    m_state = {};
}

} // namespace SmashLab
