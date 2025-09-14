#include "fluid/Voxelizer.hpp"

#include "fluid/ThreadPool.hpp"

#include <cmath>

namespace fluid {
namespace {

    void rasterizeSurface(const TriangleMesh& mesh,
        std::span<const std::size_t> objects,
        const Affine3f& toWorld,
        const GridSpec& grid,
        std::vector<std::uint8_t>& surface)
    {
        auto mark = [&](Vec3f g) {
            const int i = static_cast<int>(std::floor(g.x));
            const int j = static_cast<int>(std::floor(g.y));
            const int k = static_cast<int>(std::floor(g.z));
            if (grid.contains(i, j, k)) {
                surface[grid.index(i, j, k)] = 1;
            }
        };

        mesh.forEachTriangle(objects, [&](Vec3f pa, Vec3f pb, Vec3f pc) {
            const Vec3f a = grid.toGrid(toWorld.transformPoint(pa));
            const Vec3f b = grid.toGrid(toWorld.transformPoint(pb));
            const Vec3f c = grid.toGrid(toWorld.transformPoint(pc));
            const float longest = std::max({ length(b - a), length(c - a), length(c - b) });
            // Sample the triangle at <= half-cell spacing so its footprint is gap free.
            const int n = std::max(1, static_cast<int>(std::ceil(longest * 2.0f)));
            if (n == 1) {
                mark(a);
                mark(b);
                mark(c);
                mark((a + b + c) / 3.0f);
                return;
            }
            const Vec3f ab = (b - a) / static_cast<float>(n);
            const Vec3f ac = (c - a) / static_cast<float>(n);
            for (int u = 0; u <= n; ++u) {
                for (int v = 0; u + v <= n; ++v) {
                    mark(a + ab * static_cast<float>(u) + ac * static_cast<float>(v));
                }
            }
        });
    }

} // namespace

VoxelGrid voxelize(const TriangleMesh& mesh,
    std::span<const std::size_t> objects,
    const Affine3f& toWorld,
    const GridSpec& grid,
    const VoxelizeOptions& options)
{
    VoxelGrid result;
    result.grid = grid;
    const std::size_t cells = grid.cellCount();
    std::vector<std::uint8_t> surface(cells, 0);
    rasterizeSurface(mesh, objects, toWorld, grid, surface);

    // For every cell count along how many of the 6 axis directions a surface cell exists.
    std::vector<std::uint8_t> votes(cells, 0);
    ThreadPool& pool = ThreadPool::shared();

    // Rays along X: one line per (j, k).
    pool.parallelFor(0, static_cast<std::size_t>(grid.ny) * grid.nz, [&](std::size_t b, std::size_t e, unsigned) {
        for (std::size_t line = b; line < e; ++line) {
            const int j = static_cast<int>(line % grid.ny);
            const int k = static_cast<int>(line / grid.ny);
            bool seen = false;
            for (int i = 0; i < grid.nx; ++i) {
                const std::size_t n = grid.index(i, j, k);
                votes[n] += seen;
                seen = seen || surface[n];
            }
            seen = false;
            for (int i = grid.nx - 1; i >= 0; --i) {
                const std::size_t n = grid.index(i, j, k);
                votes[n] += seen;
                seen = seen || surface[n];
            }
        }
    }, 16);

    // Rays along Y: one line per (i, k).
    pool.parallelFor(0, static_cast<std::size_t>(grid.nx) * grid.nz, [&](std::size_t b, std::size_t e, unsigned) {
        for (std::size_t line = b; line < e; ++line) {
            const int i = static_cast<int>(line % grid.nx);
            const int k = static_cast<int>(line / grid.nx);
            bool seen = false;
            for (int j = 0; j < grid.ny; ++j) {
                const std::size_t n = grid.index(i, j, k);
                votes[n] += seen;
                seen = seen || surface[n];
            }
            seen = false;
            for (int j = grid.ny - 1; j >= 0; --j) {
                const std::size_t n = grid.index(i, j, k);
                votes[n] += seen;
                seen = seen || surface[n];
            }
        }
    }, 16);

    // Rays along Z: one line per (i, j).
    pool.parallelFor(0, static_cast<std::size_t>(grid.nx) * grid.ny, [&](std::size_t b, std::size_t e, unsigned) {
        for (std::size_t line = b; line < e; ++line) {
            const int i = static_cast<int>(line % grid.nx);
            const int j = static_cast<int>(line / grid.nx);
            bool seen = false;
            for (int k = 0; k < grid.nz; ++k) {
                const std::size_t n = grid.index(i, j, k);
                votes[n] += seen;
                seen = seen || surface[n];
            }
            seen = false;
            for (int k = grid.nz - 1; k >= 0; --k) {
                const std::size_t n = grid.index(i, j, k);
                votes[n] += seen;
                seen = seen || surface[n];
            }
        }
    }, 16);

    result.solid.assign(cells, 0);
    for (std::size_t n = 0; n < cells; ++n) {
        const bool solid = surface[n] || votes[n] >= options.enclosureVotes;
        result.solid[n] = solid;
        result.solidCount += solid;
        result.surfaceCount += surface[n];
    }

    for (int k = 0; k < grid.nz; ++k) {
        for (int j = 0; j < grid.ny; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                if (result.solid[grid.index(i, j, k)]) {
                    ++result.frontalCells;
                    break;
                }
            }
        }
    }
    return result;
}

} // namespace fluid
