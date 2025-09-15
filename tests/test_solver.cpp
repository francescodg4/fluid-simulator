#include <fluid/LbmSolver.hpp>
#include <fluid/ThreadPool.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fluid;
using Catch::Approx;

namespace {

GridSpec channel(int nx, int ny, int nz)
{
    GridSpec g;
    g.nx = nx;
    g.ny = ny;
    g.nz = nz;
    g.dx = 1.0f;
    return g;
}

} // namespace

TEST_CASE("D3Q19 lattice is symmetric and equilibrium recovers moments", "[lbm]")
{
    float wsum = 0.0f;
    for (int q = 0; q < LbmSolver::Q; ++q) {
        wsum += LbmSolver::weight[q];
        const int o = LbmSolver::opposite(q);
        CHECK(LbmSolver::cx[o] == -LbmSolver::cx[q]);
        CHECK(LbmSolver::cy[o] == -LbmSolver::cy[q]);
        CHECK(LbmSolver::cz[o] == -LbmSolver::cz[q]);
    }
    CHECK(wsum == Approx(1.0f));

    float feq[LbmSolver::Q];
    const Vec3f u { 0.05f, -0.02f, 0.03f };
    LbmSolver::equilibrium(1.1f, u, feq);
    float rho = 0.0f;
    Vec3f j;
    for (int q = 0; q < LbmSolver::Q; ++q) {
        rho += feq[q];
        j += Vec3f(static_cast<float>(LbmSolver::cx[q]), static_cast<float>(LbmSolver::cy[q]), static_cast<float>(LbmSolver::cz[q])) * feq[q];
    }
    CHECK(rho == Approx(1.1f));
    CHECK(j.x == Approx(1.1f * u.x));
    CHECK(j.y == Approx(1.1f * u.y));
    CHECK(j.z == Approx(1.1f * u.z));
}

TEST_CASE("Uniform free stream is a steady solution of the empty tunnel", "[lbm]")
{
    ThreadPool pool(4);
    const GridSpec grid = channel(24, 10, 12);
    LbmSolver solver(grid, std::vector<std::uint8_t>(grid.cellCount(), 0), pool);
    LbmParameters p;
    p.inletVelocity = 0.06f;
    p.movingFloor = true; // floor moves with the flow -> no boundary layer
    solver.setParameters(p);
    solver.reset();
    solver.step(200);
    REQUIRE_FALSE(solver.diverged());

    std::vector<float> rho;
    std::vector<Vec3f> u;
    solver.macroscopic(rho, u);
    for (std::size_t n = 0; n < grid.cellCount(); ++n) {
        REQUIRE(rho[n] == Approx(1.0f).margin(1e-3));
        REQUIRE(u[n].x == Approx(0.06f).margin(1e-3));
        REQUIRE(u[n].y == Approx(0.0f).margin(1e-3));
        REQUIRE(u[n].z == Approx(0.0f).margin(1e-3));
    }
    CHECK(length(solver.force()) == Approx(0.0f).margin(1e-9));
    CHECK(solver.stepCount() == 200u);
}

TEST_CASE("A bluff body produces drag and a stagnation region", "[lbm]")
{
    ThreadPool pool(4);
    const GridSpec grid = channel(48, 16, 16);
    std::vector<std::uint8_t> solid(grid.cellCount(), 0);
    for (int k = 6; k < 10; ++k) {
        for (int j = 0; j < 5; ++j) {
            for (int i = 14; i < 18; ++i) {
                solid[grid.index(i, j, k)] = 1;
            }
        }
    }
    LbmSolver solver(grid, solid, pool);
    LbmParameters p;
    p.inletVelocity = 0.08f;
    p.viscosity = 0.02f;
    solver.setParameters(p);
    solver.reset();
    solver.step(600);
    REQUIRE_FALSE(solver.diverged());

    CHECK(solver.force().x > 0.0f); // drag acts downstream
    CHECK(std::abs(solver.force().z) < 0.1f * solver.force().x); // symmetric body: no side force

    std::vector<float> rho;
    std::vector<Vec3f> u;
    solver.macroscopic(rho, u);
    const std::size_t front = grid.index(13, 2, 8);
    const std::size_t far = grid.index(4, 12, 2);
    CHECK(u[front].x < 0.5f * u[far].x); // decelerated ahead of the body
    CHECK(rho[front] > rho[far]); // stagnation pressure
    CHECK(solver.mlups() > 0.0);
}
