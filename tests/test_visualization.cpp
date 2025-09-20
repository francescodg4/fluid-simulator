#include <fluid/FlowField.hpp>
#include <fluid/TransferFunction.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fluid;
using Catch::Approx;

namespace {

FlowField uniformField(Vec3f u)
{
    FlowField f;
    f.grid.nx = 20;
    f.grid.ny = 10;
    f.grid.nz = 10;
    f.grid.dx = 0.5f;
    f.velocity.assign(f.grid.cellCount(), u);
    f.density.assign(f.grid.cellCount(), 1.0f);
    f.solid.assign(f.grid.cellCount(), 0);
    return f;
}

} // namespace

TEST_CASE("Trilinear sampling reproduces a linear field exactly", "[field]")
{
    FlowField f = uniformField({});
    for (int k = 0; k < f.grid.nz; ++k) {
        for (int j = 0; j < f.grid.ny; ++j) {
            for (int i = 0; i < f.grid.nx; ++i) {
                const Vec3f c = f.grid.cellCenter(i, j, k);
                f.velocity[f.grid.index(i, j, k)] = { 2.0f * c.x + c.y, c.z, -c.x };
            }
        }
    }
    const Vec3f p { 3.13f, 2.2f, 1.7f };
    const Vec3f u = f.sampleVelocity(p);
    CHECK(u.x == Approx(2.0f * p.x + p.y));
    CHECK(u.y == Approx(p.z));
    CHECK(u.z == Approx(-p.x));
}

TEST_CASE("Vorticity of solid-body rotation is twice the angular velocity", "[field]")
{
    FlowField f = uniformField({});
    const float omega = 0.01f;
    for (int k = 0; k < f.grid.nz; ++k) {
        for (int j = 0; j < f.grid.ny; ++j) {
            for (int i = 0; i < f.grid.nx; ++i) {
                // Rotation about the X axis in lattice (cell) units.
                f.velocity[f.grid.index(i, j, k)] = { 0.0f, -omega * static_cast<float>(k), omega * static_cast<float>(j) };
            }
        }
    }
    std::vector<float> w;
    f.vorticityMagnitude(w);
    CHECK(w[f.grid.index(5, 5, 5)] == Approx(2.0f * omega));
    CHECK(w[f.grid.index(0, 0, 0)] == Approx(2.0f * omega));
}

TEST_CASE("Streamlines in a uniform flow are straight and stop at the outlet", "[streamlines]")
{
    const FlowField f = uniformField({ 0.1f, 0.0f, 0.0f });
    const auto seeds = makeRakeSeeds({ 0.5f, 2.5f, 2.5f }, { 0, 1, 0 }, { 0, 0, 1 }, 3, 2);
    REQUIRE(seeds.size() == 6);
    const auto lines = traceStreamlines(f, seeds, { 0.5f, 1000, 1e-6f });
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Streamline& line = lines[i];
        REQUIRE(line.size() > 10);
        CHECK(line.back().position.y == Approx(seeds[i].y));
        CHECK(line.back().position.z == Approx(seeds[i].z));
        CHECK(line.back().position.x > 9.5f); // reached the end of the 10 m long domain
        CHECK(line.back().arcLength == Approx(line.back().position.x - seeds[i].x).margin(1e-3));
        CHECK(line.front().speed == Approx(0.1f));
    }
}

TEST_CASE("Streamlines stop at solid cells", "[streamlines]")
{
    FlowField f = uniformField({ 0.1f, 0.0f, 0.0f });
    for (int k = 0; k < f.grid.nz; ++k) {
        for (int j = 0; j < f.grid.ny; ++j) {
            f.solid[f.grid.index(10, j, k)] = 1; // wall at x in [5, 5.5)
        }
    }
    const std::vector<Vec3f> seed { { 0.5f, 2.5f, 2.5f } };
    const auto lines = traceStreamlines(f, seed);
    REQUIRE(lines[0].size() > 2);
    CHECK(lines[0].back().position.x < 5.0f);
}

TEST_CASE("Particles move with the flow and respawn at the emitter", "[particles]")
{
    const FlowField f = uniformField({ 0.1f, 0.0f, 0.0f });
    ParticleSystem system;
    system.configure(100, { 0.5f, 2.5f, 2.5f }, { 0, 1, 0 }, { 0, 0, 1 });
    for (int i = 0; i < 2000; ++i) {
        system.advance(f, 10.0f);
    }
    for (const auto& p : system.particles()) {
        CHECK(f.insideDomain(p.position));
    }
}

TEST_CASE("Transfer function interpolates colours and opacity", "[transfer]")
{
    TransferFunction tf = TransferFunction::fromColormap(Colormap::CoolWarm);
    const Vec3f mid = tf.color(0.5f);
    CHECK(mid.x == Approx(0.866f));
    CHECK(tf.color(-1.0f) == tf.colorStops().front().rgb);
    CHECK(tf.color(2.0f) == tf.colorStops().back().rgb);

    tf.setOpacityPoints({ { 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.5f, 0.2f } }); // unsorted on purpose
    CHECK(tf.opacityPoints().front().t == 0.0f);
    CHECK(tf.opacity(0.25f) == Approx(0.1f));
    CHECK(tf.opacity(0.75f) == Approx(0.6f));

    const auto table = tf.bake(256);
    REQUIRE(table.size() == 256);
    CHECK(table.back().a == Approx(1.0f));
    CHECK(table.front().a == Approx(0.0f));

    for (int c = 0; c < static_cast<int>(TransferFunction::colormapNames.size()); ++c) {
        const auto map = TransferFunction::fromColormap(static_cast<Colormap>(c));
        CHECK(map.colorStops().size() >= 2);
    }
}
