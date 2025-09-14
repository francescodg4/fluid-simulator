#pragma once

#include "fluid/Math.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fluid {

/** A contiguous run of triangles sharing one material. */
struct SubMesh {
    std::string material;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};

/** A named object ("o" statement in OBJ). Its triangles occupy [firstIndex, firstIndex + indexCount). */
struct MeshObject {
    std::string name;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::vector<SubMesh> parts;
    Aabb bounds;

    std::size_t triangleCount() const { return indexCount / 3; }
};

/** Indexed triangle mesh with per-vertex normals, organised into objects. */
struct TriangleMesh {
    std::vector<Vec3f> positions;
    std::vector<Vec3f> normals;
    std::vector<std::uint32_t> indices;
    std::vector<MeshObject> objects;

    std::size_t vertexCount() const { return positions.size(); }
    std::size_t triangleCount() const { return indices.size() / 3; }

    /** Bounds of the given objects after applying @p transform. */
    Aabb bounds(std::span<const std::size_t> objectIndices, const Affine3f& transform = {}) const;

    /** Invokes fn(a, b, c) for every triangle of the given objects. */
    template <typename Fn>
    void forEachTriangle(std::span<const std::size_t> objectIndices, Fn&& fn) const
    {
        for (const std::size_t oi : objectIndices) {
            const MeshObject& o = objects[oi];
            const std::uint32_t end = o.firstIndex + o.indexCount;
            for (std::uint32_t i = o.firstIndex; i + 2 < end; i += 3) {
                fn(positions[indices[i]], positions[indices[i + 1]], positions[indices[i + 2]]);
            }
        }
    }

    /** Recomputes MeshObject::bounds from the referenced vertices. */
    void updateObjectBounds();
};

} // namespace fluid
