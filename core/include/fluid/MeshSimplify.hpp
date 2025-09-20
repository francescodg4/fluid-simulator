#pragma once

#include "fluid/Mesh.hpp"

namespace fluid {

/// Level-of-detail generation by vertex clustering (Rossignac & Borrel).
///
/// Vertices are snapped to a uniform grid of @p cellSize; vertices sharing a cell *and* a normal
/// direction class (the dominant signed axis) merge into their average, so creases survive while
/// dense detail (bolts, grilles) collapses. Triangles that degenerate are dropped. The object /
/// material structure of the input is preserved, so the result is a drop-in render proxy.
TriangleMesh simplifyByClustering(const TriangleMesh& mesh, float cellSize);

} // namespace fluid
