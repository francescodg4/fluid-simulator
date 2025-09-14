#pragma once

#include "fluid/Grid.hpp"
#include "fluid/Mesh.hpp"

#include <span>

namespace fluid {

/** Direction the vehicle's nose points to in the source model's coordinates (Y is up). */
enum class ForwardAxis {
    PosX,
    NegX,
    PosZ,
    NegZ,
};

struct ModelPlacement {
    ForwardAxis forward = ForwardAxis::NegZ;
    float lengthMeters = 4.8f; ///< the model is uniformly scaled to this overall length
    float yawDegrees = 0.0f; ///< rotation about the vertical axis (cross-wind angle)
};

/**
 * Builds the model -> world transform. In world space the wind blows along +X, Y is up,
 * the vehicle's nose is at x = 0, it rests on the ground plane y = 0 and is centred on z = 0.
 */
Affine3f computeModelToWorld(const TriangleMesh& mesh, std::span<const std::size_t> objects, const ModelPlacement& placement);

struct TunnelSettings {
    int resolution = 160; ///< cells along the flow direction
    float upstream = 0.8f; ///< inlet distance, in vehicle lengths
    float downstream = 2.2f; ///< outlet distance, in vehicle lengths
    float heightFactor = 2.6f; ///< tunnel height, in vehicle heights
    float widthFactor = 2.6f; ///< tunnel width, in vehicle widths
};

/** Fits a wind-tunnel grid around @p body (world space). The floor coincides with y = 0. */
GridSpec fitWindTunnel(const Aabb& body, const TunnelSettings& settings);

} // namespace fluid
