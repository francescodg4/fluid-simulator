#include <fluid/Math.hpp>
#include <fluid/ObjLoader.hpp>
#include <fluid/SceneLayout.hpp>
#include <fluid/ThreadPool.hpp>
#include <fluid/Voxelizer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <numeric>
#include <string>

using namespace fluid;
using Catch::Approx;

namespace {

/** Axis-aligned box [lo, hi]^3 as OBJ text; @p withBottom = false leaves the -Y face open. */
std::string boxObj(float lo, float hi, bool withBottom, int indexOffset = 0)
{
    std::string s = "o Box\nusemtl Paint\n";
    for (int i = 0; i < 8; ++i) {
        s += "v " + std::to_string(i & 1 ? hi : lo) + " " + std::to_string(i & 2 ? hi : lo) + " " + std::to_string(i & 4 ? hi : lo) + "\n";
    }
    std::vector<std::array<int, 4>> faces { { 1, 3, 4, 2 }, { 5, 6, 8, 7 }, { 1, 5, 7, 3 }, { 2, 4, 8, 6 }, { 3, 7, 8, 4 } }; // -z, +z, -x, +x, +y
    if (withBottom) {
        faces.push_back({ 1, 2, 6, 5 });
    }
    for (const auto& f : faces) {
        s += "f";
        for (const int i : f) {
            s += " " + std::to_string(i + indexOffset);
        }
        s += "\n";
    }
    return s;
}

} // namespace

TEST_CASE("Affine transforms compose and rotate about +Y", "[math]")
{
    const Affine3f r = Affine3f::rotationY(radians(90.0f));
    const Vec3f p = r.transformPoint({ 0.0f, 0.0f, -1.0f });
    CHECK(p.x == Approx(-1.0f).margin(1e-6));
    CHECK(p.z == Approx(0.0f).margin(1e-6));

    const Affine3f m = Affine3f::translation({ 1, 2, 3 }) * Affine3f::scaling(2.0f);
    const Vec3f q = m.transformPoint({ 1, 1, 1 });
    CHECK(q == Vec3f(3, 4, 5));
    CHECK(m.transformVector({ 1, 0, 0 }) == Vec3f(2, 0, 0));
}

TEST_CASE("OBJ parser triangulates, de-duplicates and groups", "[obj]")
{
    const std::string obj = "# comment\n"
                            "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0.5 1.5 0\n"
                            "vn 0 0 1\n"
                            "o Quad\nusemtl Red\n"
                            "f 1//1 2//1 3//1 4//1\n"
                            "usemtl Blue\n"
                            "f -5//-1 -4//-1 -1//-1\n" // negative (relative) indices
                            "o Pentagon\n"
                            "f 1 2 3 5 4\n"; // no normals -> generated

    auto result = parseObj(obj);
    REQUIRE(result.has_value());
    const TriangleMesh& mesh = *result;

    REQUIRE(mesh.objects.size() == 2);
    CHECK(mesh.objects[0].name == "Quad");
    CHECK(mesh.objects[0].triangleCount() == 3);
    REQUIRE(mesh.objects[0].parts.size() == 2);
    CHECK(mesh.objects[0].parts[0].material == "Red");
    CHECK(mesh.objects[0].parts[0].indexCount == 6);
    CHECK(mesh.objects[0].parts[1].material == "Blue");
    CHECK(mesh.objects[1].triangleCount() == 3);
    CHECK(mesh.triangleCount() == 6);

    // (position, normal) pairs are shared: positions 1-5 with the shared normal + positions 1-5 without a normal.
    CHECK(mesh.vertexCount() == 10);
    for (const Vec3f& n : mesh.normals) {
        CHECK(length(n) == Approx(1.0f));
        CHECK(n.z == Approx(1.0f)); // the generated normals of the planar pentagon face +Z too
    }
    CHECK(mesh.objects[1].bounds.max.y == Approx(1.5f));
}

TEST_CASE("OBJ parser reports errors and honours cancellation", "[obj]")
{
    CHECK(parseObj("v 0 0 0\n").error().code == ObjLoadError::Code::Empty);
    CHECK(loadObj("/definitely/not/here.obj").error().code == ObjLoadError::Code::FileNotFound);

    ObjLoadOptions cancel;
    cancel.progress = [](float) { return false; };
    CHECK(parseObj(boxObj(0, 1, true), cancel).error().code == ObjLoadError::Code::Cancelled);
}

TEST_CASE("ThreadPool parallelFor covers every index exactly once", "[threads]")
{
    ThreadPool pool(4);
    for (int round = 0; round < 50; ++round) {
        std::vector<std::atomic<int>> hits(1000);
        pool.parallelFor(0, hits.size(), [&](std::size_t b, std::size_t e, unsigned worker) {
            REQUIRE(worker < pool.size());
            for (std::size_t i = b; i < e; ++i) {
                hits[i].fetch_add(1);
            }
        }, 7);
        for (auto& h : hits) {
            REQUIRE(h.load() == 1);
        }
    }
}

TEST_CASE("Model placement puts the nose at x=0 on the ground", "[layout]")
{
    auto mesh = parseObj(boxObj(-1.0f, 1.0f, true));
    REQUIRE(mesh);
    const std::vector<std::size_t> objects { 0 };
    ModelPlacement placement;
    placement.lengthMeters = 4.0f;
    const Affine3f toWorld = computeModelToWorld(*mesh, objects, placement);
    const Aabb box = mesh->bounds(objects, toWorld);
    CHECK(box.min.x == Approx(0.0f).margin(1e-5));
    CHECK(box.max.x == Approx(4.0f));
    CHECK(box.min.y == Approx(0.0f).margin(1e-5));
    CHECK(box.center().z == Approx(0.0f).margin(1e-5));

    const GridSpec grid = fitWindTunnel(box, TunnelSettings { 100, 1.0f, 2.0f, 2.0f, 3.0f });
    CHECK(grid.nx == 100);
    CHECK(grid.origin.x == Approx(-4.0f));
    CHECK(grid.origin.y == 0.0f);
    CHECK(grid.extent().x == Approx(16.0f));
    CHECK(grid.extent().y >= 8.0f - 1e-4f);
    CHECK(grid.extent().z >= 12.0f - 1e-4f);
}

TEST_CASE("Voxelizer fills closed and open-bottom boxes", "[voxel]")
{
    const std::vector<std::size_t> objects { 0 };
    GridSpec grid;
    grid.nx = grid.ny = grid.nz = 20;
    grid.origin = { 0, 0, 0 };
    grid.dx = 0.1f;

    for (bool closed : { true, false }) {
        auto mesh = parseObj(boxObj(0.52f, 1.48f, closed));
        REQUIRE(mesh);
        const VoxelGrid v = voxelize(*mesh, objects, {}, grid);
        // Cells 5..14 in each axis are touched by the box [0.52, 1.48].
        CHECK(v.solidCount == 10u * 10u * 10u);
        CHECK(v.solid[grid.index(10, 10, 10)] == 1);
        CHECK(v.solid[grid.index(2, 10, 10)] == 0);
        CHECK(v.frontalCells == 100u);
    }
}
