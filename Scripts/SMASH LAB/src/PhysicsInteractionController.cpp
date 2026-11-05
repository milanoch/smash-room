#include "PhysicsInteractionController.hpp"

#include "CursorRay.hpp"
#include "OrbitCameraController.hpp"

#include <Bamboo/Components/Rigidbody3DComponentAPI.hpp>
#include <Bamboo/Components/TransformComponentAPI.hpp>
#include <Bamboo/EntityAPI.hpp>
#include <Bamboo/Input.hpp>
#include <Bamboo/Logger.hpp>
#include <Bamboo/Physics3DAPI.hpp>
#include <Bamboo/WorldAPI.hpp>

#include <cmath>
#include <utility>

namespace {

constexpr std::array<const char *, 10> kObjectTags = {
    "Cube_Red", "Cube_Blue", "Cube_Green", "Cube_Yellow", "Cube_Purple",
    "Crate_Wood", "Sphere_Small", "Sphere_Large", "Platform", "Cylinder_BoxCollider"
};

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
    m_pendingImpacts.reserve(kMaxPendingImpacts);
    m_reportedMissingCamera = false;
    m_camera.reset();
    m_ready = true;

    // start() runs after physics initialization and before the first fixed step.
    // These are the authored poses, including the objects initially in the air.
    for (size_t i = 0; i < kObjectTags.size(); ++i) {
        const auto matches = Bamboo::WorldAPI::findAllByTag(kObjectTags[i]);
        if (matches.size() != 1 || !isDynamic(matches.front())) {
            LOG_ERROR("Impact Tool: expected one dynamic object named %s; interaction is disabled.", kObjectTags[i]);
            m_ready = false;
            continue;
        }
        const Bamboo::EntityHandle entity = matches.front();
        m_initialStates[i] = {
            entity,
            Bamboo::TransformComponentAPI::getPosition(entity),
            Bamboo::TransformComponentAPI::getRotation(entity),
            Bamboo::TransformComponentAPI::getScale(entity)
        };
    }
    if (m_ready) {
        // Existing editor-authored material ID, copied unchanged with its
        // sidecar to Object_Glass.mat. No published fields or guessed fieldIds.
        m_ready = m_destruction.initialize(SmashLab::glassCubeDefinition(Bamboo::MaterialHandle(26835769)));
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
    const bool hovering = m_camera->getScenePointer(pointer) && rayFor(pointer, direction)
        && hitDynamic(origin, direction, kRayDistance, hit);
    m_camera->showInteractionMarker(hovering);

    // pointerDown produces one request per physical click, rather than one per
    // held frame. Each request retains its own pointer position and viewport.
    while (m_camera->takeImpactClick(pointer)) {
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
    // frames without a physics step. Read R here so reset cancels older clicks
    // before they can fire; do not also read it in the frame callbacks.
    if (m_camera && m_camera->acceptsSceneKeyboard() && Bamboo::Input::isKeyJustPressed(Bamboo::Key::R)) {
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
        if (!hitDynamic(ray.origin, ray.direction, kRayDistance, hit)) { continue; }
        if (m_destruction.impact(hit.entity, hit.point, ray.direction, ray.strength)) { continue; }
        const float mass = Bamboo::Rigidbody3DComponentAPI::getMass(hit.entity);
        const float magnitude = mass * ray.strength;
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
        if (!m_destruction.isSource(state.entity) && !isDynamic(state.entity)) {
            LOG_ERROR("Impact Tool: an initial dynamic object is unavailable; reset was cancelled.");
            return false;
        }
    }
    // Remove all fragment bodies before restoring the source collider. Its
    // entity/handle is retained, so there are no deferred deletes or ID swaps.
    if (!m_destruction.restore()) { return false; }
    for (const InitialState &state : m_initialStates) {
        if (m_destruction.isSource(state.entity)) { continue; }
        // Scale changes rebuild the body/shapes in the existing Physics module.
        // Pose setters teleport, clear velocity and reset interpolation history.
        // This scene has no joints; no entity or input handler is recreated.
        Bamboo::TransformComponentAPI::setScale(state.entity, state.scale);
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
    if (m_camera) {
        m_camera->clearImpactClicks();
        m_camera->showInteractionMarker(false);
    }
    m_camera.reset();
}
