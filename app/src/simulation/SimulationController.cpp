#include "simulation/SimulationController.hpp"

#include "simulation/SimulationWorker.hpp"

#include <cmath>

namespace fluid::app {

float FlowSnapshot::valueAt(Vec3f world, int channel) const
{
    const Vec3f g = grid.toGrid(world);
    const int i = static_cast<int>(std::floor(g.x));
    const int j = static_cast<int>(std::floor(g.y));
    const int k = static_cast<int>(std::floor(g.z));
    if (!grid.contains(i, j, k) || volume.empty()) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return volume[grid.index(i, j, k) * kChannels + static_cast<std::size_t>(channel)];
}

SimulationController::SimulationController(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<fluid::app::DomainInfo>();
    qRegisterMetaType<fluid::app::SnapshotPtr>();
    qRegisterMetaType<fluid::app::FlowSettings>();
    qRegisterMetaType<fluid::app::TracerSettings>();

    m_thread.setObjectName(QStringLiteral("SimulationThread"));
    auto* worker = new SimulationWorker;
    worker->moveToThread(&m_thread);
    m_worker = worker;
    connect(&m_thread, &QThread::finished, worker, &QObject::deleteLater);

    connect(worker, &SimulationWorker::domainReady, this, [this](const DomainInfo& domain) {
        m_domain = domain;
        m_building = false;
        m_history.clear();
        emit domainReady(domain);
        emit historyChanged();
    });
    connect(worker, &SimulationWorker::snapshotReady, this, &SimulationController::onSnapshot);
    connect(worker, &SimulationWorker::runningChanged, this, [this](bool running) {
        m_running = running;
        emit runningChanged(running);
    });
    connect(worker, &SimulationWorker::statusMessage, this, &SimulationController::statusMessage);

    m_thread.start();
    QMetaObject::invokeMethod(worker, &SimulationWorker::initialize, Qt::QueuedConnection);
}

SimulationController::~SimulationController()
{
    m_thread.quit();
    m_thread.wait();
}

void SimulationController::rebuild(const RebuildRequest& request)
{
    m_building = true;
    emit buildStarted();
    QMetaObject::invokeMethod(m_worker, [w = m_worker, request] { w->rebuild(request); }, Qt::QueuedConnection);
}

void SimulationController::setFlow(const FlowSettings& flow)
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker, flow] { w->setFlow(flow); }, Qt::QueuedConnection);
}

void SimulationController::setTracers(const TracerSettings& tracers)
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker, tracers] { w->setTracers(tracers); }, Qt::QueuedConnection);
}

void SimulationController::setRunning(bool running)
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker, running] { w->setRunning(running); }, Qt::QueuedConnection);
}

void SimulationController::stepOnce()
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->stepOnce(); }, Qt::QueuedConnection);
}

void SimulationController::resetFlow()
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->resetFlow(); }, Qt::QueuedConnection);
}

void SimulationController::onSnapshot(const SnapshotPtr& snapshot)
{
    // Acknowledge first so the worker can prepare the next frame while we render this one.
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->snapshotConsumed(); }, Qt::QueuedConnection);
    if (!snapshot || snapshot->domainVersion != m_domain.version) {
        return; // stale frame from a previous domain
    }
    m_latest = snapshot;
    const SolverStats& s = snapshot->stats;
    if (!m_history.isEmpty() && s.step < m_history.back().step) {
        m_history.clear(); // the flow was reset
    }
    if (s.step > 0 && (m_history.isEmpty() || s.step > m_history.back().step) && std::isfinite(s.cd)) {
        m_history.push_back({ s.step, s.cd, s.cl });
        emit historyChanged();
    } else if (s.step == 0 && !m_history.isEmpty()) {
        m_history.clear();
        emit historyChanged();
    }
    emit snapshotReady(snapshot);
}

} // namespace fluid::app
