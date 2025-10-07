#include "services/MeshProvider.hpp"

#include "model/SceneDocument.hpp"

#include <fluid/MeshSimplify.hpp>
#include <fluid/ObjLoader.hpp>

#include <QElapsedTimer>
#include <QPromise>
#include <QtConcurrent/QtConcurrentRun>

#include "logging/Logging.hpp"

namespace fluid::app {
namespace {
    std::shared_ptr<spdlog::logger> ioLog() { return logging::get(logging::channel::Io); }
}

MeshProvider::MeshProvider(QObject* parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<MeshLoadResult>::progressValueChanged, this, [this](int value) {
        emit progress(value / 1000.0);
    });
    connect(&m_watcher, &QFutureWatcher<MeshLoadResult>::finished, this, [this] {
        if (m_watcher.isCanceled() || m_watcher.future().resultCount() == 0) {
            ioLog()->warn("Model loading cancelled");
            emit failed(tr("Loading cancelled"));
            return;
        }
        const MeshLoadResult result = m_watcher.result();
        if (!result.mesh) {
            ioLog()->error("Model loading failed: {}", result.error.toStdString());
            emit failed(result.error);
            return;
        }
        ioLog()->info("Loaded '{}' in {:.2f} s: {} vertices, {} triangles, {} objects (LOD: {} triangles)",
            result.path.toStdString(), result.seconds, result.mesh->vertexCount(), result.mesh->triangleCount(), result.mesh->objects.size(),
            result.lod ? result.lod->triangleCount() : result.mesh->triangleCount());
        emit loaded(result);
    });
}

MeshProvider::~MeshProvider()
{
    cancel();
    m_watcher.waitForFinished();
}

void MeshProvider::load(const QString& path)
{
    cancel();
    m_watcher.waitForFinished();
    ioLog()->debug("Loading {} on the thread pool", path.toStdString());
    emit started(path);

    m_watcher.setFuture(QtConcurrent::run([path](QPromise<MeshLoadResult>& promise) {
        promise.setProgressRange(0, 1000);
        QElapsedTimer timer;
        timer.start();

        ObjLoadOptions options;
        options.progress = [&promise](float fraction) {
            promise.setProgressValue(static_cast<int>(fraction * 1000.0f));
            return !promise.isCanceled();
        };
        auto mesh = loadObj(path.toStdString(), options);

        MeshLoadResult result;
        result.path = path;
        result.seconds = static_cast<double>(timer.nsecsElapsed()) * 1e-9;
        if (mesh) {
            // LOD cell size relative to the vehicle (helpers such as a studio backdrop would skew it).
            Aabb vehicle;
            for (const MeshObject& o : mesh->objects) {
                if (!isHelperObject(o)) {
                    vehicle.extend(o.bounds);
                }
            }
            const float cell = vehicle.valid() ? 0.0015f * length(vehicle.size()) : 0.0f;
            result.lod = cell > 0.0f ? std::make_shared<const TriangleMesh>(simplifyByClustering(*mesh, cell)) : nullptr;
            result.mesh = std::make_shared<const TriangleMesh>(std::move(*mesh));
        } else if (mesh.error().code == ObjLoadError::Code::Cancelled) {
            return;
        } else {
            result.error = QString::fromStdString(mesh.error().message);
        }
        promise.addResult(std::move(result));
    }));
}

void MeshProvider::cancel()
{
    if (m_watcher.isRunning()) {
        m_watcher.cancel();
    }
}

} // namespace fluid::app
