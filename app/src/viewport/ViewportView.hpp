#pragma once

#include "model/SceneDocument.hpp"
#include "simulation/SimulationController.hpp"
#include "viewport/OrbitCamera.hpp"
#include "viewport/OverlayItems.hpp"
#include "viewport/SceneRenderer.hpp"

#include <QElapsedTimer>
#include <QGraphicsView>
#include <QTimer>
#include <QVariantAnimation>

class QGraphicsProxyWidget;
class QOpenGLWidget;

namespace fluid::app {

/**
 * The 3D viewport.
 * 
 * A QGraphicsView whose viewport is a QOpenGLWidget: the OpenGL scene is rendered natively in
 * drawBackground(), and interactive 2D items (orientation gizmo, tool shelf, HUD, legend, force
 * card, probe callouts) plus the embedded "N panel" (a QGraphicsProxyWidget) are composited on
 * top by the scene with QPainter.
 */
class ViewportView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ViewportView(SceneDocument* document, SimulationController* simulation, QWidget* parent = nullptr);
    ~ViewportView() override;

    /** Embeds @p panel as the floating sidebar on the right (toggled with N). */
    void setSidePanel(QWidget* panel);
    void setSidePanelVisible(bool visible);
    bool isSidePanelVisible() const;

    void setLoadingState(const QString& title, const QString& detail, double progress);
    void clearLoadingState();

    enum class ViewPreset {
        Front,
        Back,
        Left,
        Right,
        Top,
        Bottom,
        ThreeQuarter,
    };
    void setViewPreset(ViewPreset preset);
    void frameVehicle(bool animated = true);
    void setOrthographic(bool ortho);
    void setTool(ViewportTool tool);
    bool isOrthographic() const;

    /** Renders the scene (+ optionally the overlays) into an image of the current viewport size. */
    QImage capture(bool withOverlays);
    QString rendererInfo() const { return m_renderer.rendererInfo(); }
    double framesPerSecond() const { return m_fps; }

signals:
    void framesPerSecondChanged(double fps);
    void rendererReady(const QString& info);
    void screenshotRequested();

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void onSnapshot(const SnapshotPtr& snapshot);
    void onDomain(const DomainInfo& domain);
    void layoutOverlays();
    void updateLegend();
    void updateHud();
    void animateCamera(const OrbitCamera::State& target, int durationMs = 380);
    void probeAt(const QPoint& pos);
    SceneRenderer::Frame makeFrame() const;
    QMatrix4x4 projection() const;

    SceneDocument* m_doc;
    SimulationController* m_simulation;
    QOpenGLWidget* m_gl = nullptr;
    SceneRenderer m_renderer;
    OrbitCamera m_camera;
    QVariantAnimation m_cameraAnimation;
    OrbitCamera::State m_animFrom;
    OrbitCamera::State m_animTo;

    SnapshotPtr m_snapshot;
    DomainInfo m_domain;
    bool m_framedOnce = false;
    QString m_viewName = QStringLiteral("User Perspective");

    // Overlays
    NavigationGizmoItem* m_gizmo = nullptr;
    std::vector<IconButtonItem*> m_navButtons;
    IconButtonItem* m_orthoButton = nullptr;
    ToolShelfItem* m_toolShelf = nullptr;
    HudItem* m_hud = nullptr;
    ColorLegendItem* m_legend = nullptr;
    ForceCardItem* m_forceCard = nullptr;
    ProbeItem* m_probe = nullptr;
    LoadingItem* m_loading = nullptr;
    QGraphicsProxyWidget* m_panelProxy = nullptr;

    // Interaction
    Qt::MouseButton m_dragButton = Qt::NoButton;
    QPointF m_lastMouse;
    ViewportTool m_tool = ViewportTool::Select;

    // Frame loop
    QTimer m_frameTimer;
    QElapsedTimer m_clock;
    QElapsedTimer m_fpsClock;
    int m_frames = 0;
    double m_fps = 0.0;
};

} // namespace fluid::app
