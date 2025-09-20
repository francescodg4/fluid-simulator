#include "fluid/FlowField.hpp"

#include "fluid/ThreadPool.hpp"

#include <cmath>

namespace fluid {

bool FlowField::insideDomain(Vec3f world) const
{
    const Vec3f g = grid.toGrid(world);
    return g.x >= 0.0f && g.y >= 0.0f && g.z >= 0.0f && g.x < static_cast<float>(grid.nx) && g.y < static_cast<float>(grid.ny) && g.z < static_cast<float>(grid.nz);
}

bool FlowField::isSolidAt(Vec3f world) const
{
    const Vec3f g = grid.toGrid(world);
    const int i = static_cast<int>(std::floor(g.x));
    const int j = static_cast<int>(std::floor(g.y));
    const int k = static_cast<int>(std::floor(g.z));
    return grid.contains(i, j, k) && !solid.empty() && solid[grid.index(i, j, k)];
}

Vec3f FlowField::sampleVelocity(Vec3f world) const
{
    // Cell centres are at i + 0.5 in grid coordinates.
    const Vec3f g = grid.toGrid(world) - Vec3f(0.5f, 0.5f, 0.5f);
    const float fx = std::floor(g.x), fy = std::floor(g.y), fz = std::floor(g.z);
    const int i0 = static_cast<int>(fx), j0 = static_cast<int>(fy), k0 = static_cast<int>(fz);
    const float tx = g.x - fx, ty = g.y - fy, tz = g.z - fz;

    auto at = [&](int i, int j, int k) {
        i = std::clamp(i, 0, grid.nx - 1);
        j = std::clamp(j, 0, grid.ny - 1);
        k = std::clamp(k, 0, grid.nz - 1);
        return velocity[grid.index(i, j, k)];
    };
    const Vec3f c00 = lerp(at(i0, j0, k0), at(i0 + 1, j0, k0), tx);
    const Vec3f c10 = lerp(at(i0, j0 + 1, k0), at(i0 + 1, j0 + 1, k0), tx);
    const Vec3f c01 = lerp(at(i0, j0, k0 + 1), at(i0 + 1, j0, k0 + 1), tx);
    const Vec3f c11 = lerp(at(i0, j0 + 1, k0 + 1), at(i0 + 1, j0 + 1, k0 + 1), tx);
    return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
}

void FlowField::vorticityMagnitude(std::vector<float>& out) const
{
    out.assign(grid.cellCount(), 0.0f);
    const std::size_t sx = 1;
    const std::size_t sy = static_cast<std::size_t>(grid.nx);
    const std::size_t sz = static_cast<std::size_t>(grid.nx) * grid.ny;
    ThreadPool::shared().parallelFor(0, static_cast<std::size_t>(grid.nz), [&](std::size_t b, std::size_t e, unsigned) {
        for (int k = static_cast<int>(b); k < static_cast<int>(e); ++k) {
            for (int j = 0; j < grid.ny; ++j) {
                for (int i = 0; i < grid.nx; ++i) {
                    const std::size_t n = grid.index(i, j, k);
                    if (solid[n]) {
                        continue;
                    }
                    // One-sided differences at the domain boundary.
                    const std::size_t xm = i > 0 ? n - sx : n, xp = i + 1 < grid.nx ? n + sx : n;
                    const std::size_t ym = j > 0 ? n - sy : n, yp = j + 1 < grid.ny ? n + sy : n;
                    const std::size_t zm = k > 0 ? n - sz : n, zp = k + 1 < grid.nz ? n + sz : n;
                    const float hx = 1.0f / static_cast<float>(std::max<std::size_t>(1, (xp - xm) / sx));
                    const float hy = 1.0f / static_cast<float>(std::max<std::size_t>(1, (yp - ym) / sy));
                    const float hz = 1.0f / static_cast<float>(std::max<std::size_t>(1, (zp - zm) / sz));
                    const Vec3f dudx = (velocity[xp] - velocity[xm]) * hx;
                    const Vec3f dudy = (velocity[yp] - velocity[ym]) * hy;
                    const Vec3f dudz = (velocity[zp] - velocity[zm]) * hz;
                    const Vec3f curl { dudy.z - dudz.y, dudz.x - dudx.z, dudx.y - dudy.x };
                    out[n] = length(curl);
                }
            }
        }
    }, 2);
}

std::vector<Vec3f> makeRakeSeeds(Vec3f center, Vec3f halfU, Vec3f halfV, int countU, int countV)
{
    std::vector<Vec3f> seeds;
    seeds.reserve(static_cast<std::size_t>(std::max(0, countU * countV)));
    for (int v = 0; v < countV; ++v) {
        const float tv = countV > 1 ? 2.0f * static_cast<float>(v) / static_cast<float>(countV - 1) - 1.0f : 0.0f;
        for (int u = 0; u < countU; ++u) {
            const float tu = countU > 1 ? 2.0f * static_cast<float>(u) / static_cast<float>(countU - 1) - 1.0f : 0.0f;
            seeds.push_back(center + halfU * tu + halfV * tv);
        }
    }
    return seeds;
}

std::vector<Streamline> traceStreamlines(const FlowField& field, std::span<const Vec3f> seeds, const StreamlineOptions& options)
{
    std::vector<Streamline> lines(seeds.size());
    const float h = options.stepCells * field.grid.dx;

    auto direction = [&](Vec3f p, bool& ok) {
        const Vec3f u = field.sampleVelocity(p);
        const float s = length(u);
        ok = s > options.minSpeed;
        return ok ? u / s : Vec3f {};
    };

    ThreadPool::shared().parallelFor(0, seeds.size(), [&](std::size_t b, std::size_t e, unsigned) {
        for (std::size_t li = b; li < e; ++li) {
            Streamline& line = lines[li];
            line.reserve(static_cast<std::size_t>(options.maxPoints));
            Vec3f p = seeds[li];
            float arc = 0.0f;
            for (int n = 0; n < options.maxPoints; ++n) {
                if (!field.insideDomain(p) || field.isSolidAt(p)) {
                    break;
                }
                line.push_back({ p, length(field.sampleVelocity(p)), arc });
                bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
                const Vec3f k1 = direction(p, ok1);
                const Vec3f k2 = direction(p + k1 * (0.5f * h), ok2);
                const Vec3f k3 = direction(p + k2 * (0.5f * h), ok3);
                const Vec3f k4 = direction(p + k3 * h, ok4);
                if (!(ok1 && ok2 && ok3 && ok4)) {
                    break;
                }
                p += (k1 + k2 * 2.0f + k3 * 2.0f + k4) * (h / 6.0f);
                arc += h;
            }
            if (line.size() < 2) {
                line.clear();
            }
        }
    }, 4);
    return lines;
}

void ParticleSystem::configure(std::size_t count, Vec3f emitterCenter, Vec3f halfU, Vec3f halfV)
{
    m_center = emitterCenter;
    m_halfU = halfU;
    m_halfV = halfV;
    m_particles.resize(count);
    for (Particle& p : m_particles) {
        respawn(p, true);
    }
}

void ParticleSystem::respawn(Particle& p, bool randomAge)
{
    std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
    std::uniform_real_distribution<float> ages(0.0f, 1.0f);
    p.position = m_center + m_halfU * uni(m_rng) + m_halfV * uni(m_rng);
    p.age = randomAge ? -ages(m_rng) * 600.0f : 0.0f; // negative age = waiting to be emitted
}

void ParticleSystem::advance(const FlowField& field, float steps)
{
    const float scale = steps * field.grid.dx; // lattice velocity * steps -> world displacement
    for (Particle& p : m_particles) {
        p.age += steps;
        if (p.age < 0.0f) {
            continue;
        }
        const Vec3f k1 = field.sampleVelocity(p.position);
        const Vec3f k2 = field.sampleVelocity(p.position + k1 * (0.5f * scale));
        p.position += k2 * scale;
        if (!field.insideDomain(p.position) || field.isSolidAt(p.position) || p.age > 6000.0f) {
            respawn(p, false);
        }
    }
}

} // namespace fluid
