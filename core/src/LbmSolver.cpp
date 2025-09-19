#include "fluid/LbmSolver.hpp"

#include "fluid/ThreadPool.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace fluid {
namespace {

    int findDirection(int x, int y, int z)
    {
        for (int q = 0; q < LbmSolver::Q; ++q) {
            if (LbmSolver::cx[q] == x && LbmSolver::cy[q] == y && LbmSolver::cz[q] == z) {
                return q;
            }
        }
        return 0;
    }

    constexpr float kMaxSpeed = 0.3f; // hard cap keeping the collision stable in transients

} // namespace

void LbmSolver::equilibrium(float rho, Vec3f u, float* feq)
{
    const float u2 = 1.5f * dot(u, u);
    for (int q = 0; q < Q; ++q) {
        const float cu = 3.0f * (static_cast<float>(cx[q]) * u.x + static_cast<float>(cy[q]) * u.y + static_cast<float>(cz[q]) * u.z);
        feq[q] = weight[q] * rho * (1.0f + cu + 0.5f * cu * cu - u2);
    }
}

LbmSolver::LbmSolver(const GridSpec& grid, std::vector<std::uint8_t> solid, ThreadPool& pool)
    : m_grid(grid)
    , m_solid(std::move(solid))
    , m_pool(pool)
    , m_cells(grid.cellCount())
{
    m_solid.resize(m_cells, 0);
    for (int q = 0; q < Q; ++q) {
        m_offset[q] = static_cast<std::ptrdiff_t>(cx[q])
            + static_cast<std::ptrdiff_t>(m_grid.nx) * (static_cast<std::ptrdiff_t>(cy[q]) + static_cast<std::ptrdiff_t>(m_grid.ny) * cz[q]);
        m_mirrorY[q] = findDirection(cx[q], -cy[q], cz[q]);
        m_mirrorZ[q] = findDirection(cx[q], cy[q], -cz[q]);
    }
    m_f[0].assign(m_cells * Q, 0.0f);
    m_f[1].assign(m_cells * Q, 0.0f);
    m_inletEq.assign(static_cast<std::size_t>(Q) * m_grid.ny * m_grid.nz, 0.0f);
    m_threadForce.resize(pool.size());
    classifyCells();
    setParameters(m_params);
    reset();
}

void LbmSolver::classifyCells()
{
    m_type.assign(m_cells, Interior);
    m_fluidCells = m_cells - static_cast<std::size_t>(std::count(m_solid.begin(), m_solid.end(), std::uint8_t { 1 }));
    m_floorBelt.assign(static_cast<std::size_t>(m_grid.nx) * m_grid.nz, 1);
    for (int k = 0; k < m_grid.nz; ++k) {
        for (int j = 0; j < m_grid.ny; ++j) {
            for (int i = 0; i < m_grid.nx; ++i) {
                const std::size_t n = m_grid.index(i, j, k);
                if (m_solid[n]) {
                    m_type[n] = Solid;
                    // Keep the belt still under (and one cell around) the footprint.
                    for (int dk = -1; dk <= 1; ++dk) {
                        for (int di = -1; di <= 1; ++di) {
                            if (m_grid.contains(i + di, 0, k + dk)) {
                                m_floorBelt[static_cast<std::size_t>(i + di) + static_cast<std::size_t>(m_grid.nx) * (k + dk)] = 0;
                            }
                        }
                    }
                    continue;
                }
                for (int q = 1; q < Q; ++q) {
                    const int si = i - cx[q], sj = j - cy[q], sk = k - cz[q];
                    if (!m_grid.contains(si, sj, sk) || m_solid[m_grid.index(si, sj, sk)]) {
                        m_type[n] = Boundary;
                        break;
                    }
                }
            }
        }
    }
}

void LbmSolver::setParameters(const LbmParameters& parameters)
{
    m_params = parameters;
    m_params.inletVelocity = std::clamp(m_params.inletVelocity, 0.0f, 0.2f);
    m_params.viscosity = std::max(m_params.viscosity, 1e-6f);

    // Quadratic ramp of extra relaxation time towards the outlet absorbs the wake.
    m_tauSponge.assign(m_grid.nx, 0.0f);
    const int spongeCells = static_cast<int>(std::round(m_params.spongeFraction * static_cast<float>(m_grid.nx)));
    for (int s = 0; s < spongeCells; ++s) {
        const float t = static_cast<float>(s + 1) / static_cast<float>(spongeCells);
        m_tauSponge[m_grid.nx - spongeCells + s] = 0.8f * t * t;
    }
    updateInlet();
}

void LbmSolver::updateInlet()
{
    const float u0 = m_params.inletVelocity;
    const float amp = m_params.inletTurbulence * u0;
    const float phase = static_cast<float>(m_steps) * 0.011f;
    constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
    float feq[Q];
    for (int k = 0; k < m_grid.nz; ++k) {
        for (int j = 0; j < m_grid.ny; ++j) {
            Vec3f u { u0, 0.0f, 0.0f };
            if (amp > 0.0f) {
                const float fy = static_cast<float>(j) / static_cast<float>(m_grid.ny);
                const float fz = static_cast<float>(k) / static_cast<float>(m_grid.nz);
                u.x += amp * std::sin(twoPi * (phase * 0.7f + 2.0f * fy + 3.0f * fz));
                u.y += amp * std::sin(twoPi * (phase + 3.0f * fz));
                u.z += amp * std::cos(twoPi * (phase * 1.3f + 2.0f * fy));
            }
            equilibrium(1.0f, u, feq);
            const std::size_t base = static_cast<std::size_t>(j) + static_cast<std::size_t>(m_grid.ny) * k;
            for (int q = 0; q < Q; ++q) {
                m_inletEq[q * static_cast<std::size_t>(m_grid.ny) * m_grid.nz + base] = feq[q];
            }
        }
    }
}

void LbmSolver::reset()
{
    float feq[Q];
    equilibrium(1.0f, { m_params.inletVelocity, 0.0f, 0.0f }, feq);
    for (int b = 0; b < 2; ++b) {
        std::vector<float>& f = m_f[b];
        for (std::size_t n = 0; n < m_cells; ++n) {
            for (int q = 0; q < Q; ++q) {
                f[q * m_cells + n] = m_solid[n] ? 0.0f : feq[q];
            }
        }
    }
    m_steps = 0;
    m_force = {};
    m_diverged = false;
    updateInlet();
}

float LbmSolver::pullBoundary(int q, int i, int j, int k, std::size_t n, const float* src, std::array<double, 3>& force) const
{
    int si = i - cx[q];
    int sj = j - cy[q];
    int sk = k - cz[q];
    int qq = q;

    if (si < 0) {
        const std::size_t plane = static_cast<std::size_t>(m_grid.ny) * m_grid.nz;
        return m_inletEq[q * plane + static_cast<std::size_t>(j) + static_cast<std::size_t>(m_grid.ny) * k];
    }
    if (si >= m_grid.nx) {
        // Pressure outlet: equilibrium at the reference density with the cell's own velocity
        // (taken from its post-collision populations, collision conserves momentum).
        float rho = 0.0f;
        Vec3f j3;
        for (int p = 0; p < Q; ++p) {
            const float v = src[p * m_cells + n];
            rho += v;
            j3 += Vec3f(static_cast<float>(cx[p]), static_cast<float>(cy[p]), static_cast<float>(cz[p])) * v;
        }
        const Vec3f u = rho > 0.0f ? j3 / rho : Vec3f {};
        const float cu = 3.0f * (static_cast<float>(cx[q]) * u.x + static_cast<float>(cy[q]) * u.y + static_cast<float>(cz[q]) * u.z);
        return weight[q] * (1.0f + cu + 0.5f * cu * cu - 1.5f * dot(u, u));
    }
    if (sj >= m_grid.ny) {
        sj = j; // free-slip ceiling: specular reflection
        qq = m_mirrorY[qq];
    }
    if (sj < 0) {
        // Floor: half-way bounce-back, optionally moving with the free stream (rolling road).
        // The belt only runs outside the vehicle footprint: the wheels are not spinning, and a belt
        // moving under a stationary contact patch would pump fluid out of the enclosed pockets.
        const bool belt = m_params.movingFloor && m_floorBelt[static_cast<std::size_t>(i) + static_cast<std::size_t>(m_grid.nx) * k];
        const float uw = belt ? m_params.inletVelocity : 0.0f;
        return src[opposite(q) * m_cells + n] + 6.0f * weight[q] * static_cast<float>(cx[q]) * uw;
    }
    if (sk < 0 || sk >= m_grid.nz) {
        sk = k; // free-slip side walls
        qq = m_mirrorZ[qq];
    }
    const std::size_t s = m_grid.index(si, sj, sk);
    if (m_solid[s]) {
        const float fo = src[opposite(q) * m_cells + n];
        // Momentum exchange: the population heading into the wall is reflected back. The rest
        // state (w_q, i.e. rho = 1) is subtracted so that only gauge pressure contributes: surfaces
        // in contact with the floor would otherwise feel the ambient pressure from one side only.
        const double exchange = 2.0 * (static_cast<double>(fo) - weight[q]);
        force[0] -= cx[q] * exchange;
        force[1] -= cy[q] * exchange;
        force[2] -= cz[q] * exchange;
        return fo;
    }
    return src[qq * m_cells + s];
}

void LbmSolver::streamCollide(unsigned worker, std::size_t rowBegin, std::size_t rowEnd)
{
    const float* src = m_f[m_current].data();
    float* dst = m_f[1 - m_current].data();
    const std::size_t N = m_cells;
    const float tau0 = 3.0f * m_params.viscosity + 0.5f;
    const float smagorinskyFactor = 18.0f * std::numbers::sqrt2_v<float> * m_params.smagorinsky * m_params.smagorinsky;
    std::array<double, 3>& force = m_threadForce[worker];

    float f[Q];
    float feq[Q];
    for (std::size_t row = rowBegin; row < rowEnd; ++row) {
        const int j = static_cast<int>(row % static_cast<std::size_t>(m_grid.ny));
        const int k = static_cast<int>(row / static_cast<std::size_t>(m_grid.ny));
        const std::size_t rowStart = m_grid.index(0, j, k);
        for (int i = 0; i < m_grid.nx; ++i) {
            const std::size_t n = rowStart + static_cast<std::size_t>(i);
            const std::uint8_t type = m_type[n];
            if (type == Solid) {
                continue;
            }
            if (type == Interior) {
                for (int q = 0; q < Q; ++q) {
                    f[q] = src[q * N + n - m_offset[q]];
                }
            } else {
                for (int q = 0; q < Q; ++q) {
                    f[q] = q == 0 ? src[n] : pullBoundary(q, i, j, k, n, src, force);
                }
            }

            float rho = 0.0f, jx = 0.0f, jy = 0.0f, jz = 0.0f;
            for (int q = 0; q < Q; ++q) {
                rho += f[q];
                jx += static_cast<float>(cx[q]) * f[q];
                jy += static_cast<float>(cy[q]) * f[q];
                jz += static_cast<float>(cz[q]) * f[q];
            }
            if (!(rho > 0.05f && rho < 20.0f)) { // also catches NaN
                m_divergedFlag.store(true, std::memory_order_relaxed);
                rho = 1.0f;
                jx = jy = jz = 0.0f;
            }
            const float invRho = 1.0f / rho;
            Vec3f u { jx * invRho, jy * invRho, jz * invRho };
            const float speed2 = dot(u, u);
            if (speed2 > kMaxSpeed * kMaxSpeed) {
                u *= kMaxSpeed / std::sqrt(speed2);
            }
            equilibrium(rho, u, feq);

            // Non-equilibrium momentum flux -> local strain rate -> eddy viscosity.
            float pxx = 0.0f, pyy = 0.0f, pzz = 0.0f, pxy = 0.0f, pxz = 0.0f, pyz = 0.0f;
            for (int q = 1; q < Q; ++q) {
                const float d = f[q] - feq[q];
                const float ex = static_cast<float>(cx[q]);
                const float ey = static_cast<float>(cy[q]);
                const float ez = static_cast<float>(cz[q]);
                pxx += ex * ex * d;
                pyy += ey * ey * d;
                pzz += ez * ez * d;
                pxy += ex * ey * d;
                pxz += ex * ez * d;
                pyz += ey * ez * d;
            }
            const float pi2 = pxx * pxx + pyy * pyy + pzz * pzz + 2.0f * (pxy * pxy + pxz * pxz + pyz * pyz);
            const float tau = tau0 + m_tauSponge[i];
            const float tauEff = 0.5f * (tau + std::sqrt(tau * tau + smagorinskyFactor * std::sqrt(pi2) * invRho));
            const float omega = 1.0f / tauEff;

            for (int q = 0; q < Q; ++q) {
                dst[q * N + n] = f[q] + omega * (feq[q] - f[q]);
            }
        }
    }
}

void LbmSolver::step(int count)
{
    const auto start = std::chrono::steady_clock::now();
    const std::size_t rows = static_cast<std::size_t>(m_grid.ny) * m_grid.nz;
    Vec3f forceSum;
    m_diverged = false;

    for (int s = 0; s < count; ++s) {
        if (m_params.inletTurbulence > 0.0f) {
            updateInlet();
        }
        for (auto& f : m_threadForce) {
            f = { 0.0, 0.0, 0.0 };
        }
        m_pool.parallelFor(0, rows, [this](std::size_t b, std::size_t e, unsigned w) { streamCollide(w, b, e); }, 4);
        m_current = 1 - m_current;
        ++m_steps;

        std::array<double, 3> total { 0.0, 0.0, 0.0 };
        for (const auto& f : m_threadForce) {
            total[0] += f[0];
            total[1] += f[1];
            total[2] += f[2];
        }
        forceSum += Vec3f(static_cast<float>(total[0]), static_cast<float>(total[1]), static_cast<float>(total[2]));

        if (m_divergedFlag.exchange(false)) {
            m_diverged = true;
            reset();
            m_diverged = true;
            return;
        }
    }

    m_force = count > 0 ? forceSum / static_cast<float>(count) : Vec3f {};
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    m_mlups = seconds > 0.0 ? static_cast<double>(m_fluidCells) * count / seconds * 1e-6 : 0.0;
}

void LbmSolver::macroscopic(std::vector<float>& rho, std::vector<Vec3f>& velocity) const
{
    rho.resize(m_cells);
    velocity.resize(m_cells);
    const float* f = m_f[m_current].data();
    const std::size_t N = m_cells;
    m_pool.parallelFor(0, m_cells, [&](std::size_t b, std::size_t e, unsigned) {
        for (std::size_t n = b; n < e; ++n) {
            if (m_solid[n]) {
                rho[n] = 1.0f;
                velocity[n] = {};
                continue;
            }
            float r = 0.0f, jx = 0.0f, jy = 0.0f, jz = 0.0f;
            for (int q = 0; q < Q; ++q) {
                const float v = f[q * N + n];
                r += v;
                jx += static_cast<float>(cx[q]) * v;
                jy += static_cast<float>(cy[q]) * v;
                jz += static_cast<float>(cz[q]) * v;
            }
            rho[n] = r;
            velocity[n] = r > 0.0f ? Vec3f(jx, jy, jz) / r : Vec3f {};
        }
    }, 4096);
}

} // namespace fluid
