#include "fluid/Mesh.hpp"

namespace fluid {

Aabb TriangleMesh::bounds(std::span<const std::size_t> objectIndices, const Affine3f& transform) const
{
    Aabb box;
    forEachTriangle(objectIndices, [&](Vec3f a, Vec3f b, Vec3f c) {
        box.extend(transform.transformPoint(a));
        box.extend(transform.transformPoint(b));
        box.extend(transform.transformPoint(c));
    });
    return box;
}

void TriangleMesh::updateObjectBounds()
{
    for (MeshObject& o : objects) {
        o.bounds = {};
        for (std::uint32_t i = o.firstIndex; i < o.firstIndex + o.indexCount; ++i) {
            o.bounds.extend(positions[indices[i]]);
        }
    }
}

} // namespace fluid
