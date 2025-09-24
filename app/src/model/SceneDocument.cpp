#include "model/SceneDocument.hpp"

#include <QFileInfo>

namespace fluid::app {

bool isHelperObject(const MeshObject& object)
{
    // Cutters carry no material; props are recognised by name.
    const QString name = QString::fromStdString(object.name).toLower();
    if (name.startsWith("alights") || name.startsWith("light") || name.startsWith("background") || name.startsWith("camera")) {
        return true;
    }
    return std::all_of(object.parts.begin(), object.parts.end(), [](const SubMesh& p) { return p.material == "None" || p.material.empty(); });
}

SceneDocument::SceneDocument(QObject* parent)
    : QObject(parent)
{
    m_transfer.setOpacityPoints({ { 0.0f, 0.0f }, { 0.45f, 0.03f }, { 0.75f, 0.35f }, { 1.0f, 0.85f } });
}

void SceneDocument::setMesh(MeshPtr mesh, MeshPtr lod, const QString& path)
{
    m_mesh = std::move(mesh);
    m_lod = lod ? std::move(lod) : m_mesh;
    m_modelPath = path;
    m_objects.clear();
    m_selected = -1;
    if (m_mesh) {
        m_objects.reserve(m_mesh->objects.size());
        for (const MeshObject& o : m_mesh->objects) {
            SceneObject so;
            so.name = QString::fromStdString(o.name);
            so.helper = isHelperObject(o);
            so.visible = !so.helper;
            so.collision = !so.helper;
            so.triangles = o.triangleCount();
            m_objects.push_back(so);
        }
    }
    updateTransform();
    emit meshChanged();
    emit objectsChanged();
    emit geometryChanged();
}

QString SceneDocument::modelName() const
{
    return m_modelPath.isEmpty() ? QStringLiteral("No model") : QFileInfo(m_modelPath).completeBaseName();
}

void SceneDocument::setObjectVisible(std::size_t index, bool visible)
{
    if (index >= m_objects.size() || m_objects[index].visible == visible) {
        return;
    }
    m_objects[index].visible = visible;
    emit objectsChanged();
}

void SceneDocument::setObjectCollision(std::size_t index, bool collision)
{
    if (index >= m_objects.size() || m_objects[index].collision == collision) {
        return;
    }
    m_objects[index].collision = collision;
    updateTransform();
    emit objectsChanged();
    emit geometryChanged();
}

std::vector<std::size_t> SceneDocument::collisionObjects() const
{
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < m_objects.size(); ++i) {
        if (m_objects[i].collision) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<std::size_t> SceneDocument::visibleObjects() const
{
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < m_objects.size(); ++i) {
        if (m_objects[i].visible) {
            result.push_back(i);
        }
    }
    return result;
}

void SceneDocument::setSelectedObject(int index)
{
    if (index == m_selected) {
        return;
    }
    m_selected = index;
    emit selectionChanged(index);
}

void SceneDocument::setPlacement(const ModelPlacement& placement)
{
    if (placement.forward == m_placement.forward && placement.lengthMeters == m_placement.lengthMeters && placement.yawDegrees == m_placement.yawDegrees) {
        return;
    }
    m_placement = placement;
    updateTransform();
    emit geometryChanged();
}

void SceneDocument::setTunnel(const TunnelSettings& tunnel)
{
    if (tunnel.resolution == m_tunnel.resolution && tunnel.upstream == m_tunnel.upstream && tunnel.downstream == m_tunnel.downstream
        && tunnel.heightFactor == m_tunnel.heightFactor && tunnel.widthFactor == m_tunnel.widthFactor) {
        return;
    }
    m_tunnel = tunnel;
    emit geometryChanged();
}

Aabb SceneDocument::vehicleBounds() const
{
    if (!m_mesh) {
        return {};
    }
    const auto objects = collisionObjects();
    return m_mesh->bounds(objects, m_modelToWorld);
}

void SceneDocument::setFlow(const FlowSettings& flow)
{
    if (flow == m_flow) {
        return;
    }
    m_flow = flow;
    emit flowChanged(m_flow);
}

void SceneDocument::setTracers(const TracerSettings& tracers)
{
    if (tracers == m_tracers) {
        return;
    }
    m_tracers = tracers;
    emit tracersChanged(m_tracers);
}

void SceneDocument::setView(const ViewSettings& view)
{
    if (view == m_view) {
        return;
    }
    m_view = view;
    emit viewChanged(m_view);
}

void SceneDocument::setTransferFunction(const TransferFunction& tf)
{
    m_transfer = tf;
    emit transferFunctionChanged();
}

RebuildRequest SceneDocument::rebuildRequest() const
{
    RebuildRequest request;
    request.mesh = m_mesh;
    request.collisionObjects = collisionObjects();
    request.modelToWorld = m_modelToWorld;
    request.tunnel = m_tunnel;
    request.flow = m_flow;
    request.tracers = m_tracers;
    return request;
}

void SceneDocument::updateTransform()
{
    if (!m_mesh) {
        m_modelToWorld = {};
        return;
    }
    auto objects = collisionObjects();
    if (objects.empty()) {
        objects = visibleObjects();
    }
    m_modelToWorld = computeModelToWorld(*m_mesh, objects, m_placement);
}

} // namespace fluid::app
