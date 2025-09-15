#pragma once

#include "fluid/Grid.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace fluid {

class ThreadPool;

/** Physical model parameters, all in lattice units (dx = dt = 1). */
struct LbmParameters {
    float inletVelocity = 0.08f; ///< free-stream speed, keep < ~0.15 (low Mach number)
    float viscosity = 0.002f; ///< kinematic viscosity; tau = 3 nu + 0.5
    float smagorinsky = 0.16f; ///< Smagorinsky LES constant (0 disables the sub-grid model)
    bool movingFloor = true; ///< rolling road: the floor moves with the free stream
    float inletTurbulence = 0.0f; ///< relative amplitude of inflow perturbations
    float spongeFraction = 0.12f; ///< fraction of the domain before the outlet with extra damping
};

/**
 * D3Q19 lattice Boltzmann solver for external aerodynamics in a wind tunnel:
 * BGK collision + Smagorinsky sub-grid model, fused pull streaming, equilibrium inlet (-X),
 * zero-gradient outlet (+X), no-slip/moving floor (-Y), free-slip ceiling and side walls,
 * half-way bounce-back on solid cells and drag/lift via momentum exchange.
 */
class LbmSolver {
public:
    static constexpr int Q = 19;
    static constexpr std::array<int, Q> cx { 0, 1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0 };
    static constexpr std::array<int, Q> cy { 0, 0, 0, 1, -1, 0, 0, 1, -1, -1, 1, 0, 0, 0, 0, 1, -1, 1, -1 };
    static constexpr std::array<int, Q> cz { 0, 0, 0, 0, 0, 1, -1, 0, 0, 0, 0, 1, -1, -1, 1, 1, -1, -1, 1 };
    static constexpr std::array<float, Q> weight {
        1.0f / 3.0f,
        1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f,
        1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f,
        1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f
    };
    static constexpr int opposite(int q) { return q == 0 ? 0 : (q % 2 == 1 ? q + 1 : q - 1); }

    /** Second-order equilibrium distribution. */
    static void equilibrium(float rho, Vec3f u, float* feq);

    LbmSolver(const GridSpec& grid, std::vector<std::uint8_t> solid, ThreadPool& pool);

    void setParameters(const LbmParameters& parameters);
    const LbmParameters& parameters() const { return m_params; }

    /** Re-initialises every fluid cell to the free-stream equilibrium. */
    void reset();
    /** Advances @p count time steps. */
    void step(int count = 1);

    const GridSpec& grid() const { return m_grid; }
    const std::vector<std::uint8_t>& solid() const { return m_solid; }
    std::uint64_t stepCount() const { return m_steps; }
    /** Force exerted by the fluid on the solid cells (lattice units), averaged over the last step() call. */
    Vec3f force() const { return m_force; }
    /** Million lattice-cell updates per second of the last step() call. */
    double mlups() const { return m_mlups; }
    /** True when the last step produced non-physical densities (the state was reset). */
    bool diverged() const { return m_diverged; }

    /** Density and velocity of every cell (solid cells: rho = 1, u = 0). */
    void macroscopic(std::vector<float>& rho, std::vector<Vec3f>& velocity) const;

private:
    enum CellType : std::uint8_t {
        Interior = 0, ///< fluid cell whose 18 neighbours are all fluid and inside the domain
        Boundary = 1, ///< fluid cell that needs the boundary-aware streaming path
        Solid = 2,
    };

    void classifyCells();
    void updateInlet();
    void streamCollide(unsigned worker, std::size_t rowBegin, std::size_t rowEnd);
    float pullBoundary(int q, int i, int j, int k, std::size_t n, const float* src, std::array<double, 3>& force) const;

    GridSpec m_grid;
    std::vector<std::uint8_t> m_solid;
    std::vector<std::uint8_t> m_type;
    ThreadPool& m_pool;
    LbmParameters m_params;

    std::size_t m_cells = 0;
    std::size_t m_fluidCells = 0;
    std::array<std::vector<float>, 2> m_f;
    int m_current = 0;
    std::array<std::ptrdiff_t, Q> m_offset {};
    std::array<int, Q> m_mirrorY {};
    std::array<int, Q> m_mirrorZ {};

    std::vector<float> m_inletEq; ///< Q * ny * nz equilibrium populations of the inflow plane
    std::vector<float> m_tauSponge; ///< extra relaxation time per x column
    std::vector<std::array<double, 3>> m_threadForce;
    std::atomic<bool> m_divergedFlag { false };

    std::uint64_t m_steps = 0;
    Vec3f m_force;
    double m_mlups = 0.0;
    bool m_diverged = false;
};

} // namespace fluid
