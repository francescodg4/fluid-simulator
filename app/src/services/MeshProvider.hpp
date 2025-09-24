#pragma once

#include "simulation/SimulationTypes.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QString>

namespace fluid::app {

struct MeshLoadResult {
    MeshPtr mesh;
    MeshPtr lod; ///< vertex-clustered render proxy
    QString path;
    QString error;
    double seconds = 0.0;
};

/**
 * Data-provider service: loads meshes on the global thread pool, reporting progress and
 * supporting cancellation through QPromise / QFuture.
 */
class MeshProvider : public QObject {
    Q_OBJECT
public:
    explicit MeshProvider(QObject* parent = nullptr);
    ~MeshProvider() override;

    void load(const QString& path);
    void cancel();
    bool isLoading() const { return m_watcher.isRunning(); }

signals:
    void started(const QString& path);
    void progress(double fraction);
    void loaded(const fluid::app::MeshLoadResult& result);
    void failed(const QString& message);

private:
    QFutureWatcher<MeshLoadResult> m_watcher;
};

} // namespace fluid::app

Q_DECLARE_METATYPE(fluid::app::MeshLoadResult)
