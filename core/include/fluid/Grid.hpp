#pragma once

#include "fluid/Math.hpp"

#include <cstddef>
#include <cstdint>

namespace fluid {

/**
 * Regular, cell-centred Cartesian grid. Cell (i, j, k) covers
 * [origin + (i, j, k) * dx, origin + (i + 1, j + 1, k + 1) * dx].
 */
struct GridSpec {
    int nx = 0;
    int ny = 0;
    int nz = 0;
    Vec3f origin;
    float dx = 1.0f;

    constexpr std::size_t cellCount() const
    {
        return static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz);
    }
    constexpr std::size_t index(int i, int j, int k) const
    {
        return static_cast<std::size_t>(i) + static_cast<std::size_t>(nx) * (static_cast<std::size_t>(j) + static_cast<std::size_t>(ny) * static_cast<std::size_t>(k));
    }
    constexpr bool contains(int i, int j, int k) const
    {
        return i >= 0 && j >= 0 && k >= 0 && i < nx && j < ny && k < nz;
    }
    /** World position -> continuous grid coordinates (cell centres sit at i + 0.5). */
    constexpr Vec3f toGrid(Vec3f world) const { return (world - origin) / dx; }
    constexpr Vec3f toWorld(Vec3f grid) const { return origin + grid * dx; }
    constexpr Vec3f cellCenter(int i, int j, int k) const
    {
        return toWorld({ static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f, static_cast<float>(k) + 0.5f });
    }
    constexpr Vec3f extent() const { return Vec3f(static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz)) * dx; }
    constexpr Aabb worldBounds() const { return { origin, origin + extent() }; }
    constexpr bool operator==(const GridSpec&) const = default;
};

} // namespace fluid
