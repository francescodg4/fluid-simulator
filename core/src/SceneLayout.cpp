#include "fluid/SceneLayout.hpp"

#include <cmath>
#include <numbers>

namespace fluid {

Affine3f computeModelToWorld(const TriangleMesh& mesh, std::span<const std::size_t> objects, const ModelPlacement& placement)
{
    constexpr float halfPi = std::numbers::pi_v<float> * 0.5f;
    float axisAngle = 0.0f;
    switch (placement.forward) {
    case ForwardAxis::NegX:
        axisAngle = 0.0f;
        break;
    case ForwardAxis::PosX:
        axisAngle = 2.0f * halfPi;
        break;
    case ForwardAxis::NegZ:
        axisAngle = halfPi;
        break;
    case ForwardAxis::PosZ:
        axisAngle = -halfPi;
        break;
    }
    // Nose towards -X so that the wind (+X) hits it head on.
    const Affine3f orient = Affine3f::rotationY(axisAngle);
    const Aabb oriented = mesh.bounds(objects, orient);
    if (!oriented.valid()) {
        return orient;
    }
    const float length = std::max(oriented.size().x, 1e-6f);
    const float scale = placement.lengthMeters / length;
    const Affine3f normalize = Affine3f::translation({ -oriented.min.x * scale, -oriented.min.y * scale, -oriented.center().z * scale })
        * Affine3f::scaling(scale) * orient;

    const Vec3f pivot { placement.lengthMeters * 0.5f, 0.0f, 0.0f };
    const Affine3f yaw = Affine3f::translation(pivot) * Affine3f::rotationY(radians(placement.yawDegrees)) * Affine3f::translation(-pivot);
    return yaw * normalize;
}

GridSpec fitWindTunnel(const Aabb& body, const TunnelSettings& settings)
{
    GridSpec grid;
    if (!body.valid() || settings.resolution < 8) {
        return grid;
    }
    const Vec3f size = body.size();
    const float length = std::max(size.x, 1e-3f);
    const float x0 = body.min.x - settings.upstream * length;
    const float x1 = body.max.x + settings.downstream * length;
    grid.nx = settings.resolution;
    grid.dx = (x1 - x0) / static_cast<float>(grid.nx);

    const float height = std::max(body.max.y, 1e-3f) * settings.heightFactor;
    const float width = std::max(size.z, 1e-3f) * settings.widthFactor;
    grid.ny = std::max(8, static_cast<int>(std::ceil(height / grid.dx)));
    grid.nz = std::max(8, static_cast<int>(std::ceil(width / grid.dx)));
    grid.origin = { x0, 0.0f, body.center().z - 0.5f * static_cast<float>(grid.nz) * grid.dx };
    return grid;
}

} // namespace fluid
