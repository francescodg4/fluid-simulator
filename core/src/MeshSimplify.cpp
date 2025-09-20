#include "fluid/MeshSimplify.hpp"

#include <cmath>
#include <unordered_map>

namespace fluid {
namespace {

    int normalClass(Vec3f n)
    {
        const float ax = std::abs(n.x), ay = std::abs(n.y), az = std::abs(n.z);
        if (ax >= ay && ax >= az) {
            return n.x >= 0.0f ? 0 : 1;
        }
        if (ay >= az) {
            return n.y >= 0.0f ? 2 : 3;
        }
        return n.z >= 0.0f ? 4 : 5;
    }

} // namespace

TriangleMesh simplifyByClustering(const TriangleMesh& mesh, float cellSize)
{
    TriangleMesh out;
    if (cellSize <= 0.0f || mesh.indices.empty()) {
        return mesh;
    }
    Aabb box;
    for (const Vec3f& p : mesh.positions) {
        box.extend(p);
    }
    const float inv = 1.0f / cellSize;

    struct Cluster {
        Vec3f position;
        Vec3f normal;
        int count = 0;
    };
    std::vector<Cluster> clusters;
    std::unordered_map<std::uint64_t, std::uint32_t> lookup;
    lookup.reserve(mesh.vertexCount() / 2);
    std::vector<std::uint32_t> remap(mesh.vertexCount());

    for (const MeshObject& object : mesh.objects) {
        MeshObject o;
        o.name = object.name;
        o.firstIndex = static_cast<std::uint32_t>(out.indices.size());
        for (const SubMesh& part : object.parts) {
            SubMesh p;
            p.material = part.material;
            p.firstIndex = static_cast<std::uint32_t>(out.indices.size());
            // Vertices are clustered per part so materials never bleed into each other.
            lookup.clear();
            const std::uint32_t end = part.firstIndex + part.indexCount;
            for (std::uint32_t i = part.firstIndex; i < end; ++i) {
                const std::uint32_t v = mesh.indices[i];
                const Vec3f g = (mesh.positions[v] - box.min) * inv;
                const auto ix = static_cast<std::uint64_t>(g.x);
                const auto iy = static_cast<std::uint64_t>(g.y);
                const auto iz = static_cast<std::uint64_t>(g.z);
                const std::uint64_t key = ((ix * 73856093ULL) ^ (iy * 19349663ULL << 20) ^ (iz * 83492791ULL << 40)) * 8 + static_cast<std::uint64_t>(normalClass(mesh.normals[v]));
                auto [it, inserted] = lookup.try_emplace(key, static_cast<std::uint32_t>(clusters.size()));
                if (inserted) {
                    clusters.push_back({});
                }
                Cluster& c = clusters[it->second];
                // Each source vertex contributes once per part, no matter how many triangles use it.
                if (remap[v] != it->second + 1) {
                    c.position += mesh.positions[v];
                    c.normal += mesh.normals[v];
                    ++c.count;
                    remap[v] = it->second + 1;
                }
            }
            for (std::uint32_t i = part.firstIndex; i + 2 < end; i += 3) {
                const std::uint32_t a = remap[mesh.indices[i]] - 1;
                const std::uint32_t b = remap[mesh.indices[i + 1]] - 1;
                const std::uint32_t c = remap[mesh.indices[i + 2]] - 1;
                if (a != b && b != c && a != c) {
                    out.indices.insert(out.indices.end(), { a, b, c });
                }
            }
            for (std::uint32_t i = part.firstIndex; i < end; ++i) {
                remap[mesh.indices[i]] = 0;
            }
            p.indexCount = static_cast<std::uint32_t>(out.indices.size()) - p.firstIndex;
            if (p.indexCount > 0) {
                o.parts.push_back(std::move(p));
            }
        }
        o.indexCount = static_cast<std::uint32_t>(out.indices.size()) - o.firstIndex;
        out.objects.push_back(std::move(o)); // keep every object so indices stay aligned
    }

    out.positions.reserve(clusters.size());
    out.normals.reserve(clusters.size());
    for (const Cluster& c : clusters) {
        out.positions.push_back(c.position / static_cast<float>(std::max(1, c.count)));
        out.normals.push_back(normalized(c.normal));
    }
    out.updateObjectBounds();
    return out;
}

} // namespace fluid
