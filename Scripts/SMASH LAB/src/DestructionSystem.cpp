#include "DestructionSystem.hpp"

#include "DestructionGeometry.hpp"
#include "GlassRenderGeometry.hpp"
#include "SdkComponentAccess.hpp"

#include <Bamboo/Assets/AssetManagerAPI.hpp>
#include <Bamboo/Assets/MeshAPI.hpp>
#include <Bamboo/Assets/MaterialAPI.hpp>
#include <Bamboo/Assets/TextureAPI.hpp>
#include <Bamboo/Components/MeshComponentAPI.hpp>
#include <Bamboo/Components/Rigidbody3DComponentAPI.hpp>
#include <Bamboo/Components/TransformComponentAPI.hpp>
#include <Bamboo/EntityAPI.hpp>
#include <Bamboo/Logger.hpp>
#include <Bamboo/WorldAPI.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <chrono>
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

uint32_t nextRandom(uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float signedRandom(uint32_t &state) {
    return static_cast<float>(nextRandom(state) & 0x00ffffffu) / 8388608.f - 1.f;
}

} // namespace

bool DestructionSystem::initialize(BreakableDefinition definition) {
    if (m_initialized || !Components::available() || !definition.surfaceMaterial.isValid() ||
        !definition.sourceTag || !*definition.sourceTag ||
        definition.fragments.empty() || definition.fragments.size() > 24 ||
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
    m_intactGeometry = makeEdgedBoxMesh();
    m_fracture = {};
    m_damage = 0.f;
    m_randomState = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ m_source.id;
    if (m_randomState == 0) { m_randomState = 0x51a55e31u; }
    if (!m_sparkles.initialize()) { return false; }
    if (!createRoughnessMap()) {
        m_sparkles.shutdown();
        LOG_ERROR("Destruction: could not create the glass roughness map.");
        return false;
    }
    m_mesh = Bamboo::AssetManagerAPI::createMesh();
    if (!m_mesh.isValid()) {
        releaseRoughnessMap();
        m_sparkles.shutdown();
        LOG_ERROR("Destruction: could not create the glass visual batch.");
        return false;
    }
    // One nonphysical visual node, allocated once per Play, follows the source
    // or its prepared bodies. This allows glass + sparks to share triangle ordering.
    m_visualEntity = Bamboo::WorldAPI::createEntity("Glass_VisualBatch_Runtime");
    if (m_visualEntity.isValid()) {
        Bamboo::EntityAPI::addComponent(m_visualEntity, Bamboo::ComponentType::MESH_COMPONENT);
    }
    if (!m_visualEntity.isValid() || !Components::has(m_visualEntity, kMesh) ||
        !Components::remove(m_source, kPrimitive)) {
        if (m_visualEntity.isValid()) {
            Components::remove(m_visualEntity, kMesh);
            Bamboo::WorldAPI::destroyEntity(m_visualEntity);
            m_visualEntity = {};
        }
        Bamboo::AssetManagerAPI::deleteMesh(m_mesh);
        m_mesh = {};
        releaseRoughnessMap();
        m_sparkles.shutdown();
        LOG_ERROR("Destruction: could not replace the source's visual component.");
        return false;
    }
    Bamboo::TransformComponentAPI::setPosition(m_visualEntity, {});
    Bamboo::TransformComponentAPI::setRotation(m_visualEntity, {0.f, 0.f, 0.f, 1.f});
    Bamboo::TransformComponentAPI::setScale(m_visualEntity, {1.f, 1.f, 1.f});
    Bamboo::MeshComponentAPI::setMesh(m_visualEntity, m_mesh);
    Bamboo::MeshComponentAPI::setMaterial(m_visualEntity, m_definition.surfaceMaterial);
    // This material belongs only to the prototype glass. No other palette
    // material is changed. Restore its authored white texture before release.
    Bamboo::MaterialAPI::setTexture(m_definition.surfaceMaterial, "baseColorTexture", m_sparkles.texture());
    Bamboo::MaterialAPI::setTexture(m_definition.surfaceMaterial, "metallicRoughnessTexture", m_roughnessTexture);
    m_state = {};
    m_initialized = true;
    return true;
}

bool DestructionSystem::createRoughnessMap() {
    constexpr uint32_t width = GlassSparkleBurst::kTextureWidth;
    std::array<unsigned char, width * 4> pixels;
    pixels.fill(255);
    for (uint32_t x = 0; x < width; ++x) {
        // The material's .2375 roughness applies to physical glass only. Keep
        // the old effective .095 for sparkles/chips/cracks sharing this batch.
        const bool glassBand = x >= kGlassHighlightColumn - 4 && x <= kGlassHighlightColumn + 4;
        pixels[x * 4 + 1] = glassBand ? 255 : kEffectRoughnessGreen;
    }
    // The installed API creates a linear RGBA8 texture with one mip level.
    // No mip filtering can spread the glass band into the effects' UV regions.
    m_roughnessTexture = Bamboo::AssetManagerAPI::createTextureRGBA8(width, 1);
    if (!m_roughnessTexture.isValid()) { return false; }
    Bamboo::TextureAPI::updateRGBA8(m_roughnessTexture, pixels.data(), static_cast<uint32_t>(pixels.size()));
    return true;
}

void DestructionSystem::releaseRoughnessMap() {
    if (m_roughnessTexture.isValid()) {
        Bamboo::AssetManagerAPI::deleteTexture(m_roughnessTexture);
        m_roughnessTexture = {};
    }
}

bool DestructionSystem::isSource(Bamboo::EntityHandle entity) const {
    return m_initialized && entity.id == m_source.id;
}

void DestructionSystem::updateVisuals(float deltaTime, Bamboo::Vec3 cameraPosition, Bamboo::Quat cameraRotation,
                                    float viewportHeight, float verticalFovDegrees) {
    if (!m_initialized) { return; }
    m_visualGeometry.vertices.clear();
    m_visualGeometry.indices.clear();
    auto append = [&](Bamboo::EntityHandle entity, const Bamboo::MeshData &geometry, bool polishedGlass = true) {
        appendGlassGeometry(m_visualGeometry, geometry,
                            Bamboo::TransformComponentAPI::getPosition(entity),
                            Bamboo::TransformComponentAPI::getRotation(entity),
                            Bamboo::TransformComponentAPI::getScale(entity), cameraPosition, polishedGlass);
    };
    if (m_state.state() == DestructionState::Intact || m_state.state() == DestructionState::Cracked ||
        m_state.state() == DestructionState::Breaking) {
        append(m_source, m_intactGeometry);
        if (m_state.state() != DestructionState::Intact) {
            m_crackGeometry.vertices.clear();
            m_crackGeometry.indices.clear();
            float width = .0016f;
            if (std::isfinite(viewportHeight) && viewportHeight > 0.f &&
                std::isfinite(verticalFovDegrees) && verticalFovDegrees > 0.f && verticalFovDegrees < 179.f) {
                const float distance = glm::length(vector(cameraPosition) - vector(Bamboo::TransformComponentAPI::getPosition(m_source)));
                width = std::clamp(distance * std::tan(glm::radians(verticalFovDegrees) * .5f) / viewportHeight,
                                   .001f, .006f);
            }
            m_cracks.appendTo(m_crackGeometry, width);
            append(m_source, m_crackGeometry, false);
        }
    } else if (m_state.state() == DestructionState::Broken) {
        for (size_t i = 0; i < m_fragments.size(); ++i) { append(m_fragments[i].entity, m_fracture.meshes[i]); }
    }
    m_sparkles.appendTo(m_visualGeometry, cameraPosition, cameraRotation, deltaTime, viewportHeight, verticalFovDegrees);
    // Preserve the editor primitive's triangle order while the cube is whole.
    // Crack overlays and scattered shards still need their existing sorting.
    if (m_state.state() != DestructionState::Intact) {
        sortGlassTriangles(m_visualGeometry, cameraPosition, cameraRotation);
    }
    Bamboo::MeshAPI::update(m_mesh, m_visualGeometry);
}

bool DestructionSystem::impact(Bamboo::EntityHandle entity, Bamboo::Vec3 point, Bamboo::Vec3 direction, float strength) {
    if (!isSource(entity)) { return false; } // Ordinary items retain the existing Impact Tool.
    if (m_state.state() != DestructionState::Intact && m_state.state() != DestructionState::Cracked) { return true; }
    if (!finite(point) || !finite(direction) || glm::length(vector(direction)) < 1.e-5f ||
        !std::isfinite(strength) || strength < 0.f) { return false; }
    const auto savedRotation = Bamboo::TransformComponentAPI::getRotation(m_source);
    const glm::quat rotation = glm::normalize(glm::quat(savedRotation.w, savedRotation.x, savedRotation.y, savedRotation.z));
    const auto sourceScale = Bamboo::TransformComponentAPI::getScale(m_source);
    if (!positive(sourceScale)) { return false; }
    const glm::vec3 localPoint = glm::clamp((glm::conjugate(rotation) *
        (vector(point) - vector(Bamboo::TransformComponentAPI::getPosition(m_source)))) / vector(sourceScale),
        glm::vec3(-.5f), glm::vec3(.5f));
    const Bamboo::Vec3 localIncomingDirection = vector((glm::conjugate(rotation) * vector(direction)) / vector(sourceScale));
    const float mass = Bamboo::Rigidbody3DComponentAPI::getMass(m_source);
    const float impulseMagnitude = mass * strength * m_definition.inheritedImpactRatio;
    if (!std::isfinite(mass) || mass <= 0.f || !std::isfinite(impulseMagnitude)) { return false; }
    int face = 0;
    for (int axis = 1; axis < 3; ++axis) {
        const float error = std::abs(localPoint[axis]) - std::abs(localPoint[face]);
        if (error > 1.e-5f || (std::abs(error) <= 1.e-5f &&
            std::abs(vector(localIncomingDirection)[axis]) > std::abs(vector(localIncomingDirection)[face]))) { face = axis; }
    }
    glm::vec3 localNormal(0.f);
    localNormal[face] = localPoint[face] >= 0.f ? 1.f : -1.f;
    const float incidence = std::max(0.f, -glm::dot(unit(vector(direction)), rotation * localNormal));
    const float normalSpeed = strength * incidence / m_definition.strength;
    // The same impulse has a different point response at a face centre and
    // near an edge. Include the rotational term from Panda's actual world
    // inverse inertia instead of treating every normal hit as identical.
    const auto massData = Bamboo::Rigidbody3DComponentAPI::getMassData(m_source);
    if (!std::isfinite(massData.mass) || massData.mass <= 0.f || !finite(massData.center)) { return false; }
    glm::mat3 inverseInertia(0.f);
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            const float value = massData.inverseInertia[column * 3 + row];
            if (!std::isfinite(value)) { return false; }
            inverseInertia[column][row] = value;
        }
    }
    const glm::vec3 lever = glm::cross(vector(point) - vector(massData.center), rotation * localNormal);
    const float rotationalResponse = glm::dot(lever, inverseInertia * lever);
    if (!std::isfinite(rotationalResponse) || rotationalResponse < -1.e-5f) { return false; }
    const float pointResponse = 1.f + massData.mass * std::max(0.f, rotationalResponse);
    const float energy = .5f * normalSpeed * normalSpeed * pointResponse;
    if (!std::isfinite(energy)) { return false; }
    // Calibrated gameplay thresholds, not a claimed stress/contact solver.
    // Damage persists, weak hits retain the solid body and strong hits release it.
    constexpr float crackOnset = .12f;
    const uint32_t seed = nextRandom(m_randomState);
    if (energy >= crackOnset && m_fracture.meshes.empty()) {
        auto fracture = makeImpactGlassFracturePattern(m_definition.fragments, sourceScale, vector(localPoint), seed,
                                                      localIncomingDirection);
        if (fracture.fragments.size() != m_fragments.size() || fracture.meshes.size() != m_fragments.size() ||
            fracture.cracks.empty() || !std::isfinite(fracture.colliderVolumeFraction) || fracture.colliderVolumeFraction <= 0.f) {
            LOG_ERROR("Destruction: could not prepare a coherent fracture; keeping the intact cube.");
            return false;
        }
        m_fracture = std::move(fracture);
        m_cracks.prepare(m_fracture, sourceScale);
    }
    // Transfer the tool's retained impulse exactly once, at the actual hit.
    // Box3D supplies its off-centre angular response. The fragment transition
    // inherits this motion instead of adding the same tool push a second time.
    Bamboo::Rigidbody3DComponentAPI::applyLinearImpulseAtPoint(
        m_source, vector(unit(vector(direction)) * impulseMagnitude), point);
    if (energy < crackOnset) { return true; }
    const float previousDamage = m_damage;
    m_damage = std::clamp(m_damage + (energy - crackOnset) * .14f, 0.f, 1.f);
    const bool shatter = energy >= 4.f * (1.f - previousDamage * .5f) || m_damage >= 1.f;
    m_cracks.reveal(vector(localPoint), shatter ? 1.f : std::clamp(.08f + m_damage * .72f + energy * .035f, .1f, .85f));
    if (!shatter) { m_state.markCracked(); return true; }
    if (!m_state.beginBreak()) { return true; }
    m_pendingBreak = {vector(localPoint), direction, strength, seed, std::clamp(energy / 6.48f, .3f, 1.f)};
    m_breakElapsed = 0.f;
    // At most one ordinary fixed step: no .1-second decorative drawing delay.
    // Previously revealed cracks remain exact exterior edges of this cached plan.
    return true;
}

void DestructionSystem::fixedUpdate(float stepTime) {
    if (!m_initialized || m_state.state() != DestructionState::Breaking ||
        !std::isfinite(stepTime) || stepTime <= 0.f) { return; }
    m_breakElapsed = std::min(m_breakDuration, m_breakElapsed + stepTime);
    if (m_breakElapsed >= m_breakDuration) { activateFragments(); }
}

bool DestructionSystem::activateFragments() {
    if (m_state.state() != DestructionState::Breaking) { return false; }

    const glm::vec3 position = vector(Bamboo::TransformComponentAPI::getPosition(m_source));
    const auto savedRotation = Bamboo::TransformComponentAPI::getRotation(m_source);
    const glm::quat rotation = glm::normalize(glm::quat(savedRotation.w, savedRotation.x, savedRotation.y, savedRotation.z));
    const glm::vec3 scale = vector(Bamboo::TransformComponentAPI::getScale(m_source));
    const glm::vec3 velocity = vector(Bamboo::Rigidbody3DComponentAPI::getLinearVelocity(m_source));
    const glm::vec3 angular = vector(Bamboo::Rigidbody3DComponentAPI::getAngularVelocity(m_source));
    const float sourceMass = Bamboo::Rigidbody3DComponentAPI::getMass(m_source);
    const glm::vec3 impactDirection = unit(vector(m_pendingBreak.direction), {0.f, 0.f, -1.f});
    // Follow the hit location on the moving source instead of leaving a stale
    // world-space hit floating behind it during the crack phase.
    const glm::vec3 hitPoint = position + rotation * (vector(m_pendingBreak.localPoint) * scale);
    uint32_t scatterSeed = m_pendingBreak.seed ^ 0xa8f31e49u;
    if (scatterSeed == 0) { scatterSeed = 1; }

    // These hooks synchronously remove the solver body before any new body is
    // created. There is never an intact collider under the physical fragments.
    if (!Components::remove(m_source, kBox)) { return failBreak(); }
    if (Bamboo::Rigidbody3DComponentAPI::getMass(m_source) != 0.f) { return failBreak(); }

    std::vector<glm::vec3> centers;
    std::vector<glm::vec3> scatter;
    std::vector<glm::vec3> spin;
    std::vector<glm::mat3> inertias;
    std::vector<float> masses;
    centers.reserve(m_fragments.size());
    scatter.reserve(m_fragments.size());
    spin.reserve(m_fragments.size());
    inertias.reserve(m_fragments.size());
    masses.reserve(m_fragments.size());
    glm::vec3 weightedScatter(0.f);
    float totalMass = 0.f;
    for (size_t i = 0; i < m_fragments.size(); ++i) {
        const auto fragment = m_fragments[i].entity;
        const auto &data = m_fracture.fragments[i];
        const glm::vec3 center = position + rotation * (vector(data.center) * scale);
        Bamboo::TransformComponentAPI::setScale(fragment, vector(vector(data.size) * scale));
        Bamboo::TransformComponentAPI::setPosition(fragment, vector(center));
        Bamboo::TransformComponentAPI::setRotation(fragment, savedRotation);
        // The authored Dynamic component remains; adding a collider creates a
        // real Box3D dynamic body with the SDK's documented default parameters.
        Bamboo::EntityAPI::addComponent(fragment, kBox);
        const float mass = Bamboo::Rigidbody3DComponentAPI::getMass(fragment);
        if (!std::isfinite(mass) || mass <= 0.f) { return failBreak(); }
        const float relativeVolume = data.size.x * data.size.y * data.size.z * static_cast<float>(m_fragments.size());
        const float sizeResponse = glm::clamp(std::pow(relativeVolume, -.16f), .8f, 1.4f);
        const glm::vec3 jitter{signedRandom(scatterSeed), signedRandom(scatterSeed), signedRandom(scatterSeed)};
        const float proximity = .2f + .8f * std::exp(-glm::length(center - hitPoint) /
                                                     std::max(.05f, glm::length(scale) * .3f));
        const float strengthResponse = std::min(m_pendingBreak.strength / 2.5f, 2.f);
        const glm::vec3 outward = (unit(center - hitPoint) * m_definition.radialSpeed + jitter * .1f) *
                                  sizeResponse * proximity * strengthResponse;
        const glm::vec3 randomAxis{signedRandom(scatterSeed), signedRandom(scatterSeed), signedRandom(scatterSeed)};
        spin.push_back(unit(glm::cross(impactDirection, center - hitPoint) + randomAxis * .12f, {1.f, 0.f, 0.f}) *
                       ((.4f + std::abs(signedRandom(scatterSeed)) * .8f) * sizeResponse * proximity * strengthResponse));
        // Inertia of the actual default box used by these prepared bodies,
        // rotated to world space. No guessed engine layouts or mass setters.
        const glm::vec3 dimensions = vector(data.size) * scale;
        glm::mat3 inertia(0.f);
        inertia[0][0] = mass * (dimensions.y * dimensions.y + dimensions.z * dimensions.z) / 12.f;
        inertia[1][1] = mass * (dimensions.x * dimensions.x + dimensions.z * dimensions.z) / 12.f;
        inertia[2][2] = mass * (dimensions.x * dimensions.x + dimensions.y * dimensions.y) / 12.f;
        const glm::mat3 basis = glm::mat3_cast(rotation);
        inertias.push_back(basis * inertia * glm::transpose(basis));
        centers.push_back(center);
        scatter.push_back(outward);
        masses.push_back(mass);
        weightedScatter += outward * mass;
        totalMass += mass;
    }
    // Convex visuals use conservative inscribed boxes because this SDK exposes
    // no convex-hull collider or density setter. Validate the real expected box
    // mass, and do not pretend that their reduced mass equals the visual volume.
    const float expectedMass = sourceMass * m_fracture.colliderVolumeFraction;
    if (!std::isfinite(totalMass) || sourceMass <= 0.f || !std::isfinite(expectedMass) || expectedMass <= 0.f ||
        std::abs(totalMass - expectedMass) > std::max(1.e-6f, expectedMass * .01f)) {
        LOG_ERROR("Destruction: fragment proxy mass is invalid; restoring the intact cube.");
        return failBreak();
    }
    // The impact already changed the source. Extra separation must not create
    // a second net push or torque: project out its rigid translation/rotation.
    const glm::vec3 meanScatter = weightedScatter / totalMass;
    glm::vec3 massCenter(0.f);
    for (size_t i = 0; i < centers.size(); ++i) { massCenter += centers[i] * masses[i]; }
    massCenter /= totalMass;
    glm::mat3 aggregateInertia(0.f);
    glm::vec3 extraAngularMomentum(0.f);
    for (size_t i = 0; i < scatter.size(); ++i) {
        scatter[i] -= meanScatter;
        const glm::vec3 offset = centers[i] - massCenter;
        aggregateInertia += inertias[i] + masses[i] *
            (glm::mat3(glm::dot(offset, offset)) - glm::outerProduct(offset, offset));
        extraAngularMomentum += glm::cross(offset, scatter[i] * masses[i]) + inertias[i] * spin[i];
    }
    const float inertiaDeterminant = glm::determinant(aggregateInertia);
    if (!std::isfinite(inertiaDeterminant) || inertiaDeterminant <= 0.f) { return failBreak(); }
    const glm::vec3 rigidSpin = glm::inverse(aggregateInertia) * extraAngularMomentum;
    if (!finite(vector(rigidSpin))) { return failBreak(); }
    float maxScatter = 0.f;
    float maxSpin = 0.f;
    float separationEnergy = 0.f;
    for (size_t i = 0; i < scatter.size(); ++i) {
        scatter[i] -= glm::cross(rigidSpin, centers[i] - massCenter);
        spin[i] -= rigidSpin;
        maxScatter = std::max(maxScatter, glm::length(scatter[i]));
        maxSpin = std::max(maxSpin, glm::length(spin[i]));
        separationEnergy += .5f * (masses[i] * glm::dot(scatter[i], scatter[i]) +
                                   glm::dot(spin[i], inertias[i] * spin[i]));
    }
    // A bounded artistic stress-release allowance, not measured contact energy.
    // Uniform scaling preserves the zero net extra impulse and torque above.
    const float retainedSpeed = m_pendingBreak.strength * m_definition.inheritedImpactRatio;
    const float energyBudget = .08f * .5f * totalMass * retainedSpeed * retainedSpeed;
    if (!std::isfinite(separationEnergy) || !std::isfinite(energyBudget)) { return failBreak(); }
    float scatterScale = maxScatter > m_definition.maxScatterSpeed ? m_definition.maxScatterSpeed / maxScatter : 1.f;
    if (maxSpin > 4.f) { scatterScale = std::min(scatterScale, 4.f / maxSpin); }
    if (separationEnergy > energyBudget && separationEnergy > 0.f) {
        scatterScale = std::min(scatterScale, std::sqrt(energyBudget / separationEnergy));
    }
    for (size_t i = 0; i < m_fragments.size(); ++i) {
        const auto fragment = m_fragments[i].entity;
        const glm::vec3 offset = centers[i] - position;
        Bamboo::Rigidbody3DComponentAPI::setLinearVelocity(fragment, vector(velocity + glm::cross(angular, offset)));
        Bamboo::Rigidbody3DComponentAPI::applyLinearImpulse(fragment, vector(scatter[i] * (scatterScale * masses[i])));
        Bamboo::Rigidbody3DComponentAPI::setAngularVelocity(fragment, vector(angular + spin[i] * scatterScale));
    }
    m_state.finishBreak();
    m_cracks.reset();
    m_sparkles.trigger(vector(hitPoint), m_pendingBreak.direction,
                       vector(velocity + glm::cross(angular, hitPoint - position)), m_pendingBreak.seed, m_pendingBreak.severity);
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
    if (!m_initialized) { return true; }
    if (!m_state.beginReset()) { return false; }
    m_cracks.reset();
    m_sparkles.reset();
    m_pendingBreak = {};
    m_fracture = {};
    m_damage = 0.f;
    m_breakElapsed = 0.f;
    if (!clearFragments()) {
        LOG_ERROR("Destruction: reset stopped because a fragment body could not be removed.");
        return false;
    }
    Bamboo::TransformComponentAPI::setScale(m_source, m_initial.scale);
    Bamboo::TransformComponentAPI::setPosition(m_source, m_initial.position);
    Bamboo::TransformComponentAPI::setRotation(m_source, m_initial.rotation);
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
        Bamboo::MaterialAPI::setTexture(m_definition.surfaceMaterial, "baseColorTexture", Bamboo::TextureHandle(5));
        Bamboo::MaterialAPI::setTexture(m_definition.surfaceMaterial, "metallicRoughnessTexture", Bamboo::TextureHandle(5));
    }
    if (m_visualEntity.isValid()) {
        Components::remove(m_visualEntity, kMesh);
        // Remove the asset reference synchronously, then defer only the empty
        // entity, following WorldAPI's documented callback-safe destruction.
        Bamboo::WorldAPI::destroyEntity(m_visualEntity);
        m_visualEntity = {};
    }
    if (m_mesh.isValid()) {
        Bamboo::AssetManagerAPI::deleteMesh(m_mesh);
        m_mesh = {};
    }
    m_sparkles.shutdown();
    releaseRoughnessMap();
    m_cracks.reset();
    m_intactGeometry = {};
    m_fracture = {};
    m_visualGeometry = {};
    m_crackGeometry = {};
    m_pendingBreak = {};
    m_damage = 0.f;
    m_breakElapsed = 0.f;
    m_fragments.clear();
    m_initialized = false;
    m_source = {};
    m_state = {};
}

} // namespace SmashLab
