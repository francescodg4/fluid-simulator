#pragma once

#include <fluid/Grid.hpp>
#include <fluid/Mesh.hpp>
#include <fluid/SceneLayout.hpp>

#include <QMetaType>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace fluid::app {

/** Physical inflow and solver settings as exposed in the UI (SI units where applicable). */
struct FlowSettings {
    double windSpeed = 30.0; ///< free-stream velocity [m/s]
    double reynolds = 20000.0; ///< simulated Reynolds number (based on vehicle length)
    double smagorinsky = 0.16;
    double latticeVelocity = 0.08; ///< lattice Mach number control
    double turbulence = 0.02; ///< inflow perturbation amplitude
    bool movingFloor = true;
    int stepsPerFrame = 8;

    bool operator==(const FlowSettings&) const = default;
};

/** Streamline rake / particle emitter configuration. */
struct TracerSettings {
    int seedsAcross = 22; ///< seeds across the vehicle width
    int seedsVertical = 9; ///< seeds along the vehicle height
    double rakeWidth = 0.95; ///< x vehicle width
    double rakeHeight = 1.08; ///< x vehicle height
    double rakePosition = -0.2; ///< x vehicle length, relative to the nose (negative = upstream)
    int maxPoints = 420;
    int particleCount = 40000;
    bool streamlines = true;
    bool particles = false;

    bool operator==(const TracerSettings&) const = default;
};

enum class ScalarField {
    Speed = 0,
    Pressure = 1,
    Vorticity = 2,
};
inline constexpr int kScalarFieldCount = 3;

struct ScalarRange {
    float min = 0.0f;
    float max = 1.0f;
    bool operator==(const ScalarRange&) const = default;
};

/** Everything the worker needs to (re)build the voxel domain. */
struct RebuildRequest {
    std::shared_ptr<const TriangleMesh> mesh;
    std::vector<std::size_t> collisionObjects;
    Affine3f modelToWorld;
    TunnelSettings tunnel;
    FlowSettings flow;
    TracerSettings tracers;
};

struct DomainInfo {
    std::uint64_t version = 0;
    GridSpec grid;
    Aabb vehicleBounds; ///< world space
    std::size_t solidCells = 0;
    std::size_t frontalCells = 0;
    double frontalArea = 0.0; ///< [m^2]
    double voxelizeSeconds = 0.0;
    bool valid() const { return grid.cellCount() > 0; }
};

struct SolverStats {
    std::uint64_t step = 0;
    double physicalTime = 0.0; ///< [s]
    double cd = 0.0;
    double cl = 0.0;
    double cs = 0.0; ///< side force coefficient
    double dragNewton = 0.0;
    double liftNewton = 0.0;
    double mlups = 0.0;
    double reynoldsAir = 0.0; ///< the real-world Reynolds number at this speed
    double maxSpeed = 0.0; ///< normalised by the free stream
    bool diverged = false;
};

/** Ready-to-upload ribbon geometry: per vertex {pos.xyz, prev.xyz, next.xyz, side, scalar, arc}. */
struct StreamlineGeometry {
    static constexpr int kFloatsPerVertex = 12;
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t lineCount = 0;
};

/** Immutable frame of simulation output handed from the worker thread to the GUI. */
struct FlowSnapshot {
    static constexpr int kChannels = 4; ///< speed / U, Cp, vorticity * L / U, solid
    std::uint64_t domainVersion = 0;
    GridSpec grid;
    std::vector<float> volume;
    std::shared_ptr<const StreamlineGeometry> streamlines;
    std::vector<float> particles; ///< xyz + normalised speed
    SolverStats stats;

    /** Nearest-cell lookup of one channel at a world position (NaN outside the grid). */
    float valueAt(Vec3f world, int channel) const;
};

using SnapshotPtr = std::shared_ptr<const FlowSnapshot>;
using MeshPtr = std::shared_ptr<const TriangleMesh>;

} // namespace fluid::app

Q_DECLARE_METATYPE(fluid::app::FlowSettings)
Q_DECLARE_METATYPE(fluid::app::TracerSettings)
Q_DECLARE_METATYPE(fluid::app::DomainInfo)
Q_DECLARE_METATYPE(fluid::app::SnapshotPtr)
Q_DECLARE_METATYPE(fluid::app::MeshPtr)
