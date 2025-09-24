#pragma once

#include "simulation/SimulationTypes.hpp"

#include <fluid/FlowField.hpp>
#include <fluid/LbmSolver.hpp>

#include <QElapsedTimer>
#include <QObject>

#include <memory>

class QTimer;

namespace fluid::app {

/** Lives on the simulation thread; owns the solver and produces FlowSnapshots. */
class SimulationWorker : public QObject {
    Q_OBJECT
public:
    explicit SimulationWorker(QObject* parent = nullptr);
    ~SimulationWorker() override;

    // All of these run on the worker thread (posted by SimulationController).
    void initialize();
    void rebuild(const RebuildRequest& request);
    void setFlow(const FlowSettings& flow);
    void setTracers(const TracerSettings& tracers);
    void setRunning(bool running);
    void stepOnce();
    void resetFlow();
    void snapshotConsumed();

signals:
    void domainReady(const fluid::app::DomainInfo& domain);
    void snapshotReady(const fluid::app::SnapshotPtr& snapshot);
    void runningChanged(bool running);
    void statusMessage(const QString& message);

private:
    void tick();
    void applyFlow();
    void configureTracers();
    void publish(bool forceStreamlines);
    std::shared_ptr<const StreamlineGeometry> buildStreamlines();
    LbmParameters latticeParameters() const;
    double vehicleLengthCells() const;

    QTimer* m_timer = nullptr;
    std::unique_ptr<LbmSolver> m_solver;
    FlowField m_field;
    ParticleSystem m_particles;
    std::vector<float> m_vorticity;
    DomainInfo m_domain;
    FlowSettings m_flow;
    TracerSettings m_tracers;
    std::shared_ptr<const StreamlineGeometry> m_streamlines;

    QElapsedTimer m_sinceSnapshot;
    QElapsedTimer m_sinceStreamlines;
    float m_pendingParticleSteps = 0.0f;
    bool m_running = false;
    bool m_inFlight = false;
    bool m_publishPending = false;
    std::uint64_t m_version = 0;
};

} // namespace fluid::app
