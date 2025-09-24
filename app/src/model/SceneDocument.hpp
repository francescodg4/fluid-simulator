#pragma once

#include "simulation/SimulationTypes.hpp"

#include <fluid/TransferFunction.hpp>

#include <QObject>
#include <QString>

#include <array>

namespace fluid::app {

enum class SurfaceShading {
    Studio = 0, ///< material colours
    Clay = 1,
    ScalarField = 2, ///< transfer function applied to the selected field on the surface
};

enum class ViewportQuality {
    Auto = 0, ///< High on GPUs, Performance on software rasterizers
    High = 1, ///< full-resolution mesh, 4x MSAA
    Performance = 2, ///< level-of-detail mesh, FXAA
};

enum class SliceAxis {
    X = 0,
    Y = 1,
    Z = 2,
};

/** Display settings shared by the viewport, the legend and the report. */
struct ViewSettings {
    SurfaceShading shading = SurfaceShading::Studio;
    ScalarField field = ScalarField::Speed;
    std::array<ScalarRange, kScalarFieldCount> ranges { ScalarRange { 0.0f, 1.3f }, ScalarRange { -1.6f, 1.0f }, ScalarRange { 0.0f, 22.0f } };
    bool showSlice = false;
    SliceAxis sliceAxis = SliceAxis::Z;
    double slicePosition = 0.5; ///< normalised along the slice axis
    double sliceOpacity = 0.9;
    bool showVolume = false;
    double volumeDensity = 3.0;
    bool showDomain = true;
    bool showFloor = true;
    bool showRake = true;
    double streamlineWidth = 1.6; ///< [px]
    double streamlineOpacity = 0.7;
    bool animateStreamlines = true;
    double particleSize = 2.5; ///< [px]
    ViewportQuality quality = ViewportQuality::Auto;

    const ScalarRange& range() const { return ranges[static_cast<int>(field)]; }
    bool operator==(const ViewSettings&) const = default;
};

/** Per-object state of the loaded vehicle. */
struct SceneObject {
    QString name;
    bool visible = true;
    bool collision = true;
    bool helper = false; ///< studio props & boolean cutters, hidden by default
    std::size_t triangles = 0;
};

/** Studio props (lights, backdrop) and boolean-cutter helpers exported from DCC tools. */
bool isHelperObject(const MeshObject& object);

/**
 * The application's document: the loaded model plus all user-editable settings.
 * Views observe it through signals, services read from it; it is the single source of truth.
 */
class SceneDocument : public QObject {
    Q_OBJECT
public:
    explicit SceneDocument(QObject* parent = nullptr);

    // Model
    void setMesh(MeshPtr mesh, MeshPtr lod, const QString& path);
    const MeshPtr& mesh() const { return m_mesh; }
    /** Simplified render proxy of mesh() (same objects and parts). */
    const MeshPtr& lodMesh() const { return m_lod; }
    const QString& modelPath() const { return m_modelPath; }
    QString modelName() const;
    const std::vector<SceneObject>& objects() const { return m_objects; }
    void setObjectVisible(std::size_t index, bool visible);
    void setObjectCollision(std::size_t index, bool collision);
    std::vector<std::size_t> collisionObjects() const;
    std::vector<std::size_t> visibleObjects() const;
    int selectedObject() const { return m_selected; }
    void setSelectedObject(int index);

    // Placement & domain (changing these requires re-voxelisation)
    const ModelPlacement& placement() const { return m_placement; }
    void setPlacement(const ModelPlacement& placement);
    const TunnelSettings& tunnel() const { return m_tunnel; }
    void setTunnel(const TunnelSettings& tunnel);
    const Affine3f& modelToWorld() const { return m_modelToWorld; }
    Aabb vehicleBounds() const;

    // Solver & tracers (applied live)
    const FlowSettings& flow() const { return m_flow; }
    void setFlow(const FlowSettings& flow);
    const TracerSettings& tracers() const { return m_tracers; }
    void setTracers(const TracerSettings& tracers);

    // Visualisation
    const ViewSettings& view() const { return m_view; }
    void setView(const ViewSettings& view);
    const TransferFunction& transferFunction() const { return m_transfer; }
    void setTransferFunction(const TransferFunction& tf);

    RebuildRequest rebuildRequest() const;

signals:
    void meshChanged();
    void objectsChanged();
    void selectionChanged(int index);
    void geometryChanged(); ///< placement, tunnel or collision set changed
    void flowChanged(const fluid::app::FlowSettings& flow);
    void tracersChanged(const fluid::app::TracerSettings& tracers);
    void viewChanged(const fluid::app::ViewSettings& view);
    void transferFunctionChanged();

private:
    void updateTransform();

    MeshPtr m_mesh;
    MeshPtr m_lod;
    QString m_modelPath;
    std::vector<SceneObject> m_objects;
    int m_selected = -1;
    ModelPlacement m_placement;
    TunnelSettings m_tunnel;
    Affine3f m_modelToWorld;
    FlowSettings m_flow;
    TracerSettings m_tracers;
    ViewSettings m_view;
    TransferFunction m_transfer;
};

} // namespace fluid::app
