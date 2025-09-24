#include "simulation/SimulationWorker.hpp"

#include <fluid/ThreadPool.hpp>
#include <fluid/Voxelizer.hpp>

#include <QElapsedTimer>
#include <QTimer>

#include <spdlog/spdlog.h>

#include <cmath>

namespace fluid::app {
namespace {

    constexpr double kAirDensity = 1.225; // kg/m^3
    constexpr double kAirViscosity = 1.5e-5; // m^2/s
    constexpr qint64 kSnapshotIntervalMs = 33;
    constexpr qint64 kStreamlineIntervalMs = 90;
    constexpr float kMaxParticleSubstep = 12.0f;

} // namespace

SimulationWorker::SimulationWorker(QObject* parent)
    : QObject(parent)
{
}

SimulationWorker::~SimulationWorker() = default;

void SimulationWorker::initialize()
{
    m_timer = new QTimer(this);
    m_timer->setInterval(0);
    connect(m_timer, &QTimer::timeout, this, &SimulationWorker::tick);
    m_sinceSnapshot.start();
    m_sinceStreamlines.start();
}

double SimulationWorker::vehicleLengthCells() const
{
    return m_domain.valid() ? std::max(1.0, static_cast<double>(m_domain.vehicleBounds.size().x / m_domain.grid.dx)) : 1.0;
}

LbmParameters SimulationWorker::latticeParameters() const
{
    LbmParameters p;
    p.inletVelocity = static_cast<float>(m_flow.latticeVelocity);
    // Re = U L / nu  ->  nu = U L / Re, in lattice units. The sub-grid model supplies the rest.
    p.viscosity = static_cast<float>(m_flow.latticeVelocity * vehicleLengthCells() / std::max(1.0, m_flow.reynolds));
    p.smagorinsky = static_cast<float>(m_flow.smagorinsky);
    p.movingFloor = m_flow.movingFloor;
    p.inletTurbulence = static_cast<float>(m_flow.turbulence);
    return p;
}

void SimulationWorker::rebuild(const RebuildRequest& request)
{
    if (!request.mesh || request.collisionObjects.empty()) {
        m_solver.reset();
        m_domain = {};
        emit statusMessage(tr("Nothing to simulate: no collision objects"));
        return;
    }
    QElapsedTimer timer;
    timer.start();
    emit statusMessage(tr("Voxelizing %1 collision objects…").arg(request.collisionObjects.size()));

    m_flow = request.flow;
    m_tracers = request.tracers;

    DomainInfo domain;
    domain.version = ++m_version;
    domain.vehicleBounds = request.mesh->bounds(request.collisionObjects, request.modelToWorld);
    domain.grid = fitWindTunnel(domain.vehicleBounds, request.tunnel);
    VoxelGrid voxels = voxelize(*request.mesh, request.collisionObjects, request.modelToWorld, domain.grid);
    domain.solidCells = voxels.solidCount;
    domain.frontalCells = voxels.frontalCells;
    domain.frontalArea = static_cast<double>(voxels.frontalCells) * domain.grid.dx * domain.grid.dx;
    domain.voxelizeSeconds = static_cast<double>(timer.nsecsElapsed()) * 1e-9;
    m_domain = domain;

    m_field.grid = domain.grid;
    m_field.solid = voxels.solid;
    m_solver = std::make_unique<LbmSolver>(domain.grid, std::move(voxels.solid), ThreadPool::shared());
    m_solver->setParameters(latticeParameters());
    m_solver->reset();
    m_solver->macroscopic(m_field.density, m_field.velocity);
    configureTracers();

    spdlog::info("Domain {}x{}x{} ({} cells, dx = {:.3f} m), {} solid, frontal area {:.2f} m^2, built in {:.2f} s",
        domain.grid.nx, domain.grid.ny, domain.grid.nz, domain.grid.cellCount(), domain.grid.dx, domain.solidCells, domain.frontalArea, domain.voxelizeSeconds);
    emit domainReady(domain);
    emit statusMessage(tr("Wind tunnel ready: %1 × %2 × %3 cells").arg(domain.grid.nx).arg(domain.grid.ny).arg(domain.grid.nz));
    publish(true);
}

void SimulationWorker::configureTracers()
{
    if (!m_domain.valid()) {
        return;
    }
    const Aabb& car = m_domain.vehicleBounds;
    const Vec3f size = car.size();
    const float x = car.min.x + static_cast<float>(m_tracers.rakePosition) * size.x;
    const float top = static_cast<float>(m_tracers.rakeHeight) * size.y;
    const float bottom = 0.04f * size.y;
    const Vec3f center { x, 0.5f * (top + bottom), car.center().z };
    const Vec3f halfU { 0.0f, 0.0f, 0.5f * static_cast<float>(m_tracers.rakeWidth) * size.z };
    const Vec3f halfV { 0.0f, 0.5f * (top - bottom), 0.0f };
    m_particles.configure(m_tracers.particles ? static_cast<std::size_t>(m_tracers.particleCount) : 0, center, halfU, halfV);
    m_streamlines.reset();
}

void SimulationWorker::setFlow(const FlowSettings& flow)
{
    m_flow = flow;
    applyFlow();
}

void SimulationWorker::applyFlow()
{
    if (m_solver) {
        m_solver->setParameters(latticeParameters());
    }
    if (!m_running) {
        publish(false);
    }
}

void SimulationWorker::setTracers(const TracerSettings& tracers)
{
    const bool reseed = tracers.particles != m_tracers.particles || tracers.particleCount != m_tracers.particleCount
        || tracers.rakePosition != m_tracers.rakePosition || tracers.rakeHeight != m_tracers.rakeHeight || tracers.rakeWidth != m_tracers.rakeWidth;
    m_tracers = tracers;
    if (reseed) {
        configureTracers();
    }
    m_streamlines.reset();
    publish(true);
}

void SimulationWorker::setRunning(bool running)
{
    if (running && !m_solver) {
        running = false;
    }
    if (m_running == running) {
        emit runningChanged(m_running);
        return;
    }
    m_running = running;
    if (m_running) {
        m_timer->start();
    } else {
        m_timer->stop();
        publish(true);
    }
    emit runningChanged(m_running);
}

void SimulationWorker::stepOnce()
{
    if (!m_solver) {
        return;
    }
    m_solver->step(m_flow.stepsPerFrame);
    m_pendingParticleSteps += static_cast<float>(m_flow.stepsPerFrame);
    publish(true);
}

void SimulationWorker::resetFlow()
{
    if (!m_solver) {
        return;
    }
    m_solver->reset();
    configureTracers();
    publish(true);
}

void SimulationWorker::snapshotConsumed()
{
    m_inFlight = false;
    if (m_publishPending) {
        publish(true);
    }
}

void SimulationWorker::tick()
{
    if (!m_running || !m_solver) {
        return;
    }
    m_solver->step(m_flow.stepsPerFrame);
    m_pendingParticleSteps += static_cast<float>(m_flow.stepsPerFrame);
    if (m_solver->diverged()) {
        emit statusMessage(tr("Solver diverged and was restarted — try a lower Reynolds number or a higher resolution"));
        spdlog::warn("LBM solver diverged at Re = {}", m_flow.reynolds);
    }
    if (!m_inFlight && m_sinceSnapshot.elapsed() >= kSnapshotIntervalMs) {
        publish(false);
    }
}

void SimulationWorker::publish(bool forceStreamlines)
{
    if (!m_solver) {
        return;
    }
    if (m_inFlight) {
        m_publishPending = true;
        return;
    }
    m_publishPending = false;
    m_sinceSnapshot.restart();

    const GridSpec& grid = m_domain.grid;
    const std::size_t cells = grid.cellCount();
    const float u0 = static_cast<float>(m_flow.latticeVelocity);
    const float lengthCells = static_cast<float>(vehicleLengthCells());

    m_solver->macroscopic(m_field.density, m_field.velocity);
    m_field.vorticityMagnitude(m_vorticity);

    auto snapshot = std::make_shared<FlowSnapshot>();
    snapshot->domainVersion = m_domain.version;
    snapshot->grid = grid;
    snapshot->volume.resize(cells * FlowSnapshot::kChannels);
    float* volume = snapshot->volume.data();

    const float invU = 1.0f / u0;
    const float cpScale = 1.0f / (3.0f * 0.5f * u0 * u0); // Cp = (p - p0) / q, p = rho / 3
    const float vortScale = lengthCells / u0;
    const auto& solid = m_field.solid;
    ThreadPool& pool = ThreadPool::shared();
    pool.parallelFor(0, cells, [&](std::size_t b, std::size_t e, unsigned) {
        for (std::size_t n = b; n < e; ++n) {
            float* v = volume + n * 4;
            if (solid[n]) {
                v[0] = v[1] = v[2] = 0.0f;
                v[3] = 1.0f;
                continue;
            }
            v[0] = length(m_field.velocity[n]) * invU;
            v[1] = (m_field.density[n] - 1.0f) * cpScale;
            v[2] = m_vorticity[n] * vortScale;
            v[3] = 0.0f;
        }
    }, 8192);

    // Extrapolate the fluid values into the first layer of solid cells so the surface of the
    // vehicle (which lies inside those cells) samples meaningful pressure / speed.
    const std::ptrdiff_t strides[3] = { 1, grid.nx, static_cast<std::ptrdiff_t>(grid.nx) * grid.ny };
    pool.parallelFor(0, static_cast<std::size_t>(grid.nz), [&](std::size_t b, std::size_t e, unsigned) {
        for (int k = static_cast<int>(b); k < static_cast<int>(e); ++k) {
            for (int j = 0; j < grid.ny; ++j) {
                for (int i = 0; i < grid.nx; ++i) {
                    const std::size_t n = grid.index(i, j, k);
                    if (!solid[n]) {
                        continue;
                    }
                    const int coord[3] = { i, j, k };
                    const int limit[3] = { grid.nx, grid.ny, grid.nz };
                    float sum[3] = { 0.0f, 0.0f, 0.0f };
                    int count = 0;
                    for (int axis = 0; axis < 3; ++axis) {
                        for (int dir = -1; dir <= 1; dir += 2) {
                            const int c = coord[axis] + dir;
                            if (c < 0 || c >= limit[axis]) {
                                continue;
                            }
                            const std::size_t m = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n) + dir * strides[axis]);
                            if (solid[m]) {
                                continue;
                            }
                            sum[0] += volume[m * 4 + 0];
                            sum[1] += volume[m * 4 + 1];
                            sum[2] += volume[m * 4 + 2];
                            ++count;
                        }
                    }
                    if (count > 0) {
                        volume[n * 4 + 0] = sum[0] / static_cast<float>(count);
                        volume[n * 4 + 1] = sum[1] / static_cast<float>(count);
                        volume[n * 4 + 2] = sum[2] / static_cast<float>(count);
                    }
                }
            }
        }
    }, 1);

    // Tracers
    if (m_tracers.streamlines && (forceStreamlines || !m_streamlines || m_sinceStreamlines.elapsed() >= kStreamlineIntervalMs)) {
        m_streamlines = buildStreamlines();
        m_sinceStreamlines.restart();
    }
    snapshot->streamlines = m_tracers.streamlines ? m_streamlines : nullptr;

    if (m_tracers.particles) {
        while (m_pendingParticleSteps > 0.0f) {
            const float sub = std::min(m_pendingParticleSteps, kMaxParticleSubstep);
            m_particles.advance(m_field, sub);
            m_pendingParticleSteps -= sub;
        }
        const auto& particles = m_particles.particles();
        snapshot->particles.reserve(particles.size() * 4);
        for (const auto& p : particles) {
            if (p.age < 0.0f) {
                continue;
            }
            snapshot->particles.push_back(p.position.x);
            snapshot->particles.push_back(p.position.y);
            snapshot->particles.push_back(p.position.z);
            snapshot->particles.push_back(length(m_field.sampleVelocity(p.position)) * invU);
        }
    }
    m_pendingParticleSteps = 0.0f;

    // Statistics
    SolverStats& stats = snapshot->stats;
    stats.step = m_solver->stepCount();
    const double dtPhysical = m_flow.latticeVelocity * grid.dx / m_flow.windSpeed;
    stats.physicalTime = static_cast<double>(stats.step) * dtPhysical;
    const double qArea = 0.5 * u0 * u0 * static_cast<double>(std::max<std::size_t>(1, m_domain.frontalCells));
    const Vec3f force = m_solver->force();
    stats.cd = force.x / qArea;
    stats.cl = force.y / qArea;
    stats.cs = force.z / qArea;
    const double qPhysical = 0.5 * kAirDensity * m_flow.windSpeed * m_flow.windSpeed * m_domain.frontalArea;
    stats.dragNewton = stats.cd * qPhysical;
    stats.liftNewton = stats.cl * qPhysical;
    stats.mlups = m_solver->mlups();
    stats.reynoldsAir = m_flow.windSpeed * m_domain.vehicleBounds.size().x / kAirViscosity;
    stats.diverged = m_solver->diverged();
    float maxSpeed = 0.0f;
    for (std::size_t n = 0; n < cells; ++n) {
        maxSpeed = std::max(maxSpeed, solid[n] ? 0.0f : volume[n * 4]);
    }
    stats.maxSpeed = maxSpeed;

    m_inFlight = true;
    emit snapshotReady(std::move(snapshot));
}

std::shared_ptr<const StreamlineGeometry> SimulationWorker::buildStreamlines()
{
    const Aabb& car = m_domain.vehicleBounds;
    const Vec3f size = car.size();
    const float x = car.min.x + static_cast<float>(m_tracers.rakePosition) * size.x;
    const float top = static_cast<float>(m_tracers.rakeHeight) * size.y;
    const float bottom = 0.04f * size.y;
    const auto seeds = makeRakeSeeds({ x, 0.5f * (top + bottom), car.center().z },
        { 0.0f, 0.0f, 0.5f * static_cast<float>(m_tracers.rakeWidth) * size.z },
        { 0.0f, 0.5f * (top - bottom), 0.0f },
        m_tracers.seedsAcross,
        m_tracers.seedsVertical);

    StreamlineOptions options;
    options.maxPoints = m_tracers.maxPoints;
    const auto lines = traceStreamlines(m_field, seeds, options);

    auto geometry = std::make_shared<StreamlineGeometry>();
    std::size_t points = 0;
    for (const auto& line : lines) {
        points += line.size();
    }
    geometry->vertices.reserve(points * 2 * StreamlineGeometry::kFloatsPerVertex);
    geometry->indices.reserve(points * 6);
    const float invU = 1.0f / static_cast<float>(m_flow.latticeVelocity);

    for (const auto& line : lines) {
        if (line.size() < 2) {
            continue;
        }
        const auto base = static_cast<std::uint32_t>(geometry->vertices.size() / StreamlineGeometry::kFloatsPerVertex);
        for (std::size_t i = 0; i < line.size(); ++i) {
            const Vec3f p = line[i].position;
            const Vec3f prev = line[i == 0 ? 0 : i - 1].position;
            const Vec3f next = line[std::min(i + 1, line.size() - 1)].position;
            for (const float side : { -1.0f, 1.0f }) {
                geometry->vertices.insert(geometry->vertices.end(),
                    { p.x, p.y, p.z, prev.x, prev.y, prev.z, next.x, next.y, next.z, side, line[i].speed * invU, line[i].arcLength });
            }
            if (i + 1 < line.size()) {
                const auto a = base + static_cast<std::uint32_t>(2 * i);
                geometry->indices.insert(geometry->indices.end(), { a, a + 1, a + 3, a, a + 3, a + 2 });
            }
        }
        ++geometry->lineCount;
    }
    return geometry;
}

} // namespace fluid::app
