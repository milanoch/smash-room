#pragma once

#include <Bamboo/Base.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace SmashLab {

// pointer and viewport must use the same game-viewport-local UI coordinates,
// with the origin at the top left. The caller uses cameraPosition as ray origin.
// This is the inverse perspective/view transform for a camera looking along -Z:
// projection determines the camera-space ray, inverse view rotates it to world.
// No near/far depth convention or window/editor offset enters the direction.
// On invalid input, return false and leave direction unchanged.
inline bool makeCursorRay(
    Bamboo::Vec2 pointer,
    Bamboo::Vec2 viewport,
    Bamboo::Vec3 cameraPosition,
    Bamboo::Quat cameraRotation,
    float verticalFovDegrees,
    Bamboo::Vec3 &direction
) {
    if (!std::isfinite(pointer.x) || !std::isfinite(pointer.y) ||
        !std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
        viewport.x <= 0.f || viewport.y <= 0.f ||
        pointer.x < 0.f || pointer.y < 0.f || pointer.x >= viewport.x || pointer.y >= viewport.y ||
        !std::isfinite(cameraPosition.x) || !std::isfinite(cameraPosition.y) || !std::isfinite(cameraPosition.z) ||
        !std::isfinite(cameraRotation.x) || !std::isfinite(cameraRotation.y) ||
        !std::isfinite(cameraRotation.z) || !std::isfinite(cameraRotation.w) ||
        !std::isfinite(verticalFovDegrees) || verticalFovDegrees <= 0.f || verticalFovDegrees >= 179.f) {
        return false;
    }

    const glm::dquat rotation(cameraRotation.w, cameraRotation.x, cameraRotation.y, cameraRotation.z);
    const double rotationLengthSquared = glm::dot(rotation, rotation);
    if (rotationLengthSquared < 1.e-12) { return false; }

    const double ndcX = 2. * pointer.x / viewport.x - 1.;
    const double ndcY = 1. - 2. * pointer.y / viewport.y;
    const double aspect = static_cast<double>(viewport.x) / viewport.y;
    const double tanHalfFov = std::tan(glm::radians(static_cast<double>(verticalFovDegrees)) * .5);
    const glm::dvec3 cameraRay(ndcX * aspect * tanHalfFov, ndcY * tanHalfFov, -1.);
    const glm::dvec3 worldRay = glm::normalize(rotation) * glm::normalize(cameraRay);
    if (!std::isfinite(worldRay.x) || !std::isfinite(worldRay.y) || !std::isfinite(worldRay.z)) { return false; }

    direction = {static_cast<float>(worldRay.x), static_cast<float>(worldRay.y), static_cast<float>(worldRay.z)};
    return true;
}

} // namespace SmashLab
