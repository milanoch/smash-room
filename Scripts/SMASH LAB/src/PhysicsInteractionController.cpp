#include "PhysicsInteractionController.hpp"

#include "CursorRay.hpp"
#include "OrbitCameraController.hpp"
#include "MenuNavigation.hpp"
#include "SdkComponentAccess.hpp"

#include <Bamboo/Components/Rigidbody3DComponentAPI.hpp>
#include <Bamboo/Components/TransformComponentAPI.hpp>
#include <Bamboo/EntityAPI.hpp>
#include <Bamboo/Input.hpp>
#include <Bamboo/Logger.hpp>
#include <Bamboo/Physics3DAPI.hpp>
#include <Bamboo/WorldAPI.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace {

constexpr size_t kExistingObjectCount = 25;
constexpr std::array<const char *, 86> kObjectTags = {
    "Cube_Red", "Cube_Blue", "Cube_Green", "Cube_Yellow", "Cube_Purple", "Crate_Wood", "Sphere_Small",
    "Sphere_Large", "Platform", "Cylinder_BoxCollider", "CoffeeTable", "Bowl_CoffeeTable",
    "CoffeeTable_Book_01", "CoffeeTable_Book_02", "Vase_CoffeeTable", "Sofa", "Sofa_BackFrame",
    "Sofa_Arm_Left", "Sofa_Arm_Right", "Sofa_SeatCushion_01", "Sofa_SeatCushion_02", "Sofa_BackCushion_01",
    "Sofa_BackCushion_02", "Sofa_ThrowPillow_Sage", "Sofa_ThrowPillow_Terracotta", "Bookshelf",
    "Bookshelf_Back", "Bookshelf_Side_Left", "Bookshelf_Side_Right", "Bookshelf_Shelf_02",
    "Bookshelf_Shelf_03", "Bookshelf_Shelf_04", "Bookshelf_Shelf_05", "Bookshelf_Shelf_06",
    "Bookshelf_Book_01", "Bookshelf_Book_02", "Bookshelf_Book_03", "Bookshelf_Book_04", "Bookshelf_Book_05",
    "Bookshelf_Book_06", "Bookshelf_Book_07", "Bookshelf_Book_08", "Bookshelf_Book_09", "Bookshelf_Book_10",
    "Bookshelf_Book_11", "Bookshelf_Book_12", "Bookshelf_Book_13", "Bookshelf_Book_14", "Bookshelf_Book_15",
    "Bookshelf_Book_16", "Bookshelf_Book_17", "Bookshelf_Book_18", "Bookshelf_Book_19", "Bookshelf_Book_20",
    "Bookshelf_Book_21", "Bookshelf_Book_22", "Bookshelf_Book_23", "Bookshelf_Book_24", "Bookshelf_Book_25",
    "Bookshelf_Book_26", "Bookshelf_Book_27", "Bookshelf_Book_28", "Bookshelf_Book_29", "Bookshelf_Book_30",
    "Bookshelf_Book_31", "Bookshelf_StorageBox_1", "Bookshelf_StorageBox_3", "Bookshelf_Vase_1",
    "Bookshelf_Vase_2", "Bookshelf_Decor_Ring", "TVStand", "TVStand_Book", "Vase_TVStand", "Plant_TVStand",
    "Television", "Television_Foot_L", "Television_Foot_R", "FloorLamp", "FloorLamp_Shade", "Plant_Large",
    "Painting_Back", "Painting_Left_01", "Painting_Left_02", "Curtain_Left", "Curtain_Right", "Rug_SoftWool"
};

bool isClothTag(std::string_view tag) {
    return tag == "Curtain_Left" || tag == "Curtain_Right" || tag == "Rug_SoftWool";
}

bool isSofaFrame(Bamboo::EntityHandle entity) {
    const char *name = Bamboo::EntityAPI::getName(entity);
    if (!name) { return false; }
    const std::string_view tag(name);
    return tag == "Sofa" || tag == "Sofa_BackFrame" || tag == "Sofa_Arm_Left" || tag == "Sofa_Arm_Right";
}

bool isDynamic(Bamboo::EntityHandle entity) {
    if (!entity.isValid()) { return false; }
    const float mass = Bamboo::Rigidbody3DComponentAPI::getMass(entity);
    // Box3D static and kinematic bodies have zero mass; dynamic bodies have
    // positive mass. The SDK does not expose a Bamboo body-type getter.
    return std::isfinite(mass) && mass > 0.f;
}

bool hitDynamic(Bamboo::Vec3 origin, Bamboo::Vec3 direction, float distance, Bamboo::Physics3DAPI::RaycastHit &hit) {
    // Keep the closest solid hit: a wall blocks the ray instead of allowing
    // interaction with a dynamic object hidden behind it.
    return Bamboo::Physics3DAPI::raycast(origin, direction, distance, hit) && isDynamic(hit.entity);
}

} // namespace

void PhysicsInteractionController::start() {
    m_pendingImpacts.clear();
    m_initialStates = {};
    m_pendingImpacts.reserve(kMaxPendingImpacts);
    m_reportedMissingCamera = false;
    m_camera.reset();
    m_ready = false;
    if (!SmashLab::MenuNavigation::enterRoom(SmashLab::kTestRoomWorldAssetId)) {
        LOG_INFO("SMASH LAB: direct Play opens MainMenu before entering the test room.");
        return;
    }
    m_ready = true;

    // start() runs after physics initialization and before the first fixed step.
    // These are the authored poses, including the objects initially in the air.
    for (size_t i = 0; i < kObjectTags.size(); ++i) {
        const auto matches = Bamboo::WorldAPI::findAllByTag(kObjectTags[i]);
        if (matches.empty()) { continue; }
        if (matches.size() == 1 && isClothTag(kObjectTags[i]) &&
            !SmashLab::Components::has(matches.front(), "cloth3dComponent")) {
            // An older editor cannot simulate the new module component. Leave its original
            // static curtain mesh intact and avoid an empty, falling owner body.
            SmashLab::Components::remove(matches.front(), "rigidbody3dComponent");
            LOG_ERROR("%s needs the cloth-enabled PandaEditor; cloth interaction is unavailable in this editor.", kObjectTags[i]);
            continue;
        }
        if (matches.size() != 1 || !isDynamic(matches.front())) {
            LOG_ERROR("Impact Tool: expected one dynamic object named %s; interaction is disabled.", kObjectTags[i]);
            m_ready = false;
            continue;
        }
        const Bamboo::EntityHandle entity = matches.front();
        LOG_INFO("Impact Tool: %s mass %.3f", kObjectTags[i], Bamboo::Rigidbody3DComponentAPI::getMass(entity));
        m_initialStates[i] = {
            entity,
            Bamboo::TransformComponentAPI::getPosition(entity),
            Bamboo::TransformComponentAPI::getRotation(entity),
            Bamboo::TransformComponentAPI::getScale(entity)
        };
    }
    auto definition = SmashLab::glassCubeDefinition(Bamboo::MaterialHandle(26835769));
    if (m_ready && !Bamboo::WorldAPI::findAllByTag(definition.sourceTag).empty()) {
        // Existing editor-authored material ID, copied unchanged with its
        // sidecar to Object_Glass.mat. No published fields or guessed fieldIds.
        m_ready = m_destruction.initialize(std::move(definition));
    }
    if (m_ready) {
        m_ready = m_furnitureDestruction.initialize(SmashLab::livingRoomFurnitureDefinitions());
    }
}

void PhysicsInteractionController::lateUpdate(float deltaTime) {
    // Resolve lazily so registration/start ordering cannot select an unstarted
    // peer. This script and OrbitCameraController are on the same camera entity.
    if (!m_camera) {
        m_camera = Bamboo::EntityAPI::getScript<OrbitCameraController>(getEntity());
        if (!m_camera && !m_reportedMissingCamera) {
            LOG_ERROR("Impact Tool: OrbitCameraController must be attached to the same camera.");
            m_reportedMissingCamera = true;
        }
    }
    if (!m_camera) { return; }

    if (!m_ready) {
        m_camera->clearImpactClicks();
        m_camera->showInteractionMarker(false);
        return;
    }

    const Bamboo::Vec3 origin = Bamboo::TransformComponentAPI::getPosition(getEntity());
    const Bamboo::Quat rotation = Bamboo::TransformComponentAPI::getRotation(getEntity());
    m_destruction.updateVisuals(deltaTime, origin, rotation,
                              m_camera->getSceneViewportSize().y, OrbitCameraController::kVerticalFovDegrees);
    auto rayFor = [&](const OrbitCameraController::PointerSample &sample, Bamboo::Vec3 &direction) {
        return SmashLab::makeCursorRay(
            sample.position, sample.viewport, origin, rotation, OrbitCameraController::kVerticalFovDegrees, direction
        );
    };

    // Hover is sampled after camera.update(), so its inverse view uses the
    // current orbit/zoom pose. Shared material handles are never modified.
    OrbitCameraController::PointerSample pointer;
    Bamboo::Vec3 direction;
    Bamboo::Physics3DAPI::RaycastHit hit;
    const bool impactSelected = m_camera->isImpactToolSelected();
    const bool hovering = impactSelected && m_camera->getScenePointer(pointer) && rayFor(pointer, direction)
        && hitDynamic(origin, direction, kRayDistance, hit);
    m_camera->showInteractionMarker(hovering);

    // pointerDown produces one request per physical click, rather than one per
    // held frame. Each request retains its own pointer position and viewport.
    while (m_camera->takeImpactClick(pointer)) {
        if (!impactSelected) { continue; }
        if (m_pendingImpacts.size() >= kMaxPendingImpacts) { continue; }
        if (rayFor(pointer, direction) && hitDynamic(origin, direction, kRayDistance, hit)) {
            m_pendingImpacts.push_back({origin, direction, pointer.impactStrength});
        }
    }
}

void PhysicsInteractionController::fixedUpdate(float stepTime) {
    if (!m_ready) { return; }
    if (!m_camera) { m_camera = Bamboo::EntityAPI::getScript<OrbitCameraController>(getEntity()); }
    // Input retains an edge until exactly one fixed step sees it, even across
    // frames without a physics step. The UI button shares the same reset path;
    // reset cancels older clicks before they can fire.
    const bool uiReset = m_camera && m_camera->consumeRoomResetRequest();
    const bool keyboardReset = m_camera && m_camera->acceptsSceneKeyboard() &&
        Bamboo::Input::isKeyJustPressed(Bamboo::Key::R);
    if (uiReset || keyboardReset) {
        m_pendingImpacts.clear();
        if (m_camera) {
            m_camera->clearImpactClicks();
            m_camera->showInteractionMarker(false);
        }
        m_ready = restoreRoom();
        return; // A reset always wins over any impact in this fixed step.
    }

    m_destruction.fixedUpdate(stepTime);

    // Consume first, even when no ray hits: later fixed steps must never repeat
    // the same click. Recast against exact solver poses at the physics boundary.
    auto impacts = std::exchange(m_pendingImpacts, {});
    for (const ImpactRay &ray : impacts) {
        Bamboo::Physics3DAPI::RaycastHit hit;
        if (!Bamboo::Physics3DAPI::raycast(ray.origin, ray.direction, kRayDistance, hit)) {
            LOG_INFO("Impact Tool: raycast returned no hit");
            continue;
        }
        LOG_INFO("Impact Tool: raycast entity=%u mass=%.3f", hit.entity.id,
                 Bamboo::Rigidbody3DComponentAPI::getMass(hit.entity));
        if (!isDynamic(hit.entity)) { continue; }
        if (m_destruction.impact(hit.entity, hit.point, ray.direction, ray.strength)) { continue; }
        if (m_furnitureDestruction.impact(hit.entity, hit.point, ray.direction, ray.strength)) { continue; }
        const float mass = Bamboo::Rigidbody3DComponentAPI::getMass(hit.entity);
        // One impulse budget for the whole heavy sofa assembly, regardless of
        // which frame piece was hit. Other objects retain their existing response.
        float impulseMass = isSofaFrame(hit.entity) ? 30.f : mass;
        // Keep the existing table/sofa response. New heavy props get a bounded
        // impulse rather than mass-scaled acceleration identical to a book's.
        for (size_t i = kExistingObjectCount; i < kObjectTags.size(); ++i) {
            if (m_initialStates[i].entity.id != hit.entity.id) { continue; }
            const std::string_view tag(kObjectTags[i]);
            if (tag == "Bookshelf" || tag == "Bookshelf_Back" ||
                tag.starts_with("Bookshelf_Side_") || tag.starts_with("Bookshelf_Shelf_")) {
                impulseMass = 8.f; // One budget for the joined bookshelf frame.
            } else if (tag == "TVStand") {
                impulseMass = 12.f;
            } else if (tag == "Curtain_Left" || tag == "Curtain_Right") {
                impulseMass = std::min(mass, 0.04f); // local push on light fabric cells
            } else if (tag == "Rug_SoftWool") {
                impulseMass = std::min(mass, 0.25f);
            } else {
                impulseMass = std::min(mass, 2.f);
            }
            break;
        }
        const float magnitude = impulseMass * ray.strength;
        if (!std::isfinite(magnitude)) { continue; }
        const Bamboo::Vec3 impulse(
            ray.direction.x * magnitude, ray.direction.y * magnitude, ray.direction.z * magnitude
        );
        Bamboo::Rigidbody3DComponentAPI::applyLinearImpulseAtPoint(hit.entity, impulse, hit.point);
    }
}

bool PhysicsInteractionController::restoreRoom() {
    // Validate the whole snapshot before making changes, so an unsupported
    // deleted/retyped object cannot leave a partially restored room.
    for (const InitialState &state : m_initialStates) {
        if (state.entity.id == 0) { continue; }
        if (!m_destruction.isSource(state.entity) && !m_furnitureDestruction.isSource(state.entity) &&
            !isDynamic(state.entity)) {
            LOG_ERROR("Impact Tool: an initial dynamic object is unavailable; reset was cancelled.");
            return false;
        }
    }
    // Remove all fragment bodies before restoring the source collider. Its
    // entity/handle is retained, so there are no deferred deletes or ID swaps.
    if (!m_destruction.restore() || !m_furnitureDestruction.restore()) { return false; }
    for (const InitialState &state : m_initialStates) {
        if (state.entity.id == 0 || m_destruction.isSource(state.entity) ||
            m_furnitureDestruction.isSource(state.entity)) {
            continue;
        }
        // Rebuilding an unchanged body would destroy the sofa's Box3D joints.
        // Pose setters teleport in place, clear velocity and keep those joints.
        const auto scale = Bamboo::TransformComponentAPI::getScale(state.entity);
        if (scale.x != state.scale.x || scale.y != state.scale.y || scale.z != state.scale.z) {
            Bamboo::TransformComponentAPI::setScale(state.entity, state.scale);
        }
        Bamboo::TransformComponentAPI::setPosition(state.entity, state.position);
        Bamboo::TransformComponentAPI::setRotation(state.entity, state.rotation);
        Bamboo::Rigidbody3DComponentAPI::setLinearVelocity(state.entity, {0.f, 0.f, 0.f});
        Bamboo::Rigidbody3DComponentAPI::setAngularVelocity(state.entity, {0.f, 0.f, 0.f});
    }
    // There is no Bamboo force-clear API. This tool only applies instantaneous
    // impulses; its queued requests are already discarded above.
    return true;
}

void PhysicsInteractionController::shutdown() {
    m_pendingImpacts.clear();
    m_ready = false;
    m_destruction.shutdown();
    m_furnitureDestruction.shutdown();
    if (m_camera) {
        m_camera->clearImpactClicks();
        m_camera->showInteractionMarker(false);
    }
    m_camera.reset();
}
