#pragma once

#include "simulation/SimulationTypes.hpp"

#include <QObject>
#include <QPointer>
#include <QThread>
#include <QVector>

namespace fluid::app {

class SimulationWorker;

struct ForceSample {
    std::uint64_t step = 0;
    double cd = 0.0;
    double cl = 0.0;
};

/**
 * GUI-thread facade of the asynchronous solver pipeline.
 * 
 * The solver, voxeliser and tracer run on a dedicated worker thread. Commands are posted as
 * queued calls; results come back as immutable FlowSnapshot objects. Only one snapshot is in
 * flight at a time (the GUI acknowledges each one), so a slow renderer never makes the queue grow
 * while the solver keeps iterating at full speed.
 */
class SimulationController : public QObject {
    Q_OBJECT
public:
    explicit SimulationController(QObject* parent = nullptr);
    ~SimulationController() override;

    void rebuild(const RebuildRequest& request);
    void setFlow(const FlowSettings& flow);
    void setTracers(const TracerSettings& tracers);
    void setRunning(bool running);
    void stepOnce();
    void resetFlow();

    bool isRunning() const { return m_running; }
    bool isBuilding() const { return m_building; }
    const DomainInfo& domain() const { return m_domain; }
    const SnapshotPtr& latestSnapshot() const { return m_latest; }
    const QVector<ForceSample>& history() const { return m_history; }

signals:
    void buildStarted();
    void domainReady(const fluid::app::DomainInfo& domain);
    void snapshotReady(const fluid::app::SnapshotPtr& snapshot);
    void runningChanged(bool running);
    void historyChanged();
    void statusMessage(const QString& message);

private:
    void onSnapshot(const SnapshotPtr& snapshot);

    QThread m_thread;
    QPointer<SimulationWorker> m_worker;
    DomainInfo m_domain;
    SnapshotPtr m_latest;
    QVector<ForceSample> m_history;
    bool m_running = false;
    bool m_building = false;
};

} // namespace fluid::app
