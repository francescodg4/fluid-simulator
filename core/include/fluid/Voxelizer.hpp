#pragma once

#include "fluid/Grid.hpp"
#include "fluid/Mesh.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace fluid {

struct VoxelizeOptions {
    /**
     * A non-surface cell is solid when at least this many of the six axis-aligned rays
     * leaving it hit the surface. 6 = strict enclosure; 5 tolerates one open side, which makes
     * game-style meshes (no underbody, open wheel arches) voxelize as solid bodies.
     */
    int enclosureVotes = 5;
};

struct VoxelGrid {
    GridSpec grid;
    std::vector<std::uint8_t> solid; ///< 1 = solid, 0 = fluid; indexed with GridSpec::index
    std::size_t solidCount = 0;
    std::size_t surfaceCount = 0;
    std::size_t frontalCells = 0; ///< projected solid area on the Y-Z plane, in cells
};

/**
 * Rasterizes the triangles of @p objects (transformed by @p toWorld) into @p grid and fills
 * the enclosed interior with a six-direction ray vote (robust to non-watertight meshes).
 */
VoxelGrid voxelize(const TriangleMesh& mesh,
    std::span<const std::size_t> objects,
    const Affine3f& toWorld,
    const GridSpec& grid,
    const VoxelizeOptions& options = {});

} // namespace fluid
