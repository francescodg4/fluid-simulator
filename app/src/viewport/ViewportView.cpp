#include "viewport/ViewportView.hpp"

#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QPainter>
#include <QWheelEvent>

#include <spdlog/spdlog.h>

#include <cmath>

namespace fluid::app {
namespace {

    constexpr qreal kMargin = 12.0;
    constexpr qreal kPanelWidth = 318.0;

    QMatrix4x4 toQMatrix(const Affine3f& a)
    {
        return QMatrix4x4(a.m[0][0], a.m[0][1], a.m[0][2], a.m[0][3],
            a.m[1][0], a.m[1][1], a.m[1][2], a.m[1][3],
            a.m[2][0], a.m[2][1], a.m[2][2], a.m[2][3],
            0.0f, 0.0f, 0.0f, 1.0f);
    }

    const char* fieldTitle(ScalarField f)
    {
        switch (f) {
        case ScalarField::Speed:
            return "Velocity magnitude";
        case ScalarField::Pressure:
            return "Pressure coefficient Cp";
        case ScalarField::Vorticity:
            return "Vorticity magnitude";
        }
        return "";
    }

} // namespace

ViewportView::ViewportView(SceneDocument* document, SimulationController* simulation, QWidget* parent)
    : QGraphicsView(parent)
    , m_doc(document)
    , m_simulation(simulation)
{
    m_gl = new QOpenGLWidget;
    setViewport(m_gl);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);

    auto* scene = new QGraphicsScene(this);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    setScene(scene);

    // --- overlays ---------------------------------------------------------------------------
    m_hud = new HudItem;
    scene->addItem(m_hud);

    m_toolShelf = new ToolShelfItem;
    scene->addItem(m_toolShelf);
    connect(m_toolShelf, &ToolShelfItem::toolChanged, this, &ViewportView::setTool);

    m_gizmo = new NavigationGizmoItem;
    scene->addItem(m_gizmo);
    connect(m_gizmo, &NavigationGizmoItem::orbitDragged, this, [this](const QPointF& d) {
        m_cameraAnimation.stop();
        m_camera.orbit(static_cast<float>(d.x()), static_cast<float>(d.y()));
        m_viewName = QStringLiteral("User Perspective");
    });
    connect(m_gizmo, &NavigationGizmoItem::axisClicked, this, [this](const QVector3D& axis) {
        if (axis.x() > 0.5f) {
            setViewPreset(ViewPreset::Back);
        } else if (axis.x() < -0.5f) {
            setViewPreset(ViewPreset::Front);
        } else if (axis.y() > 0.5f) {
            setViewPreset(ViewPreset::Top);
        } else if (axis.y() < -0.5f) {
            setViewPreset(ViewPreset::Bottom);
        } else if (axis.z() > 0.5f) {
            setViewPreset(ViewPreset::Right);
        } else {
            setViewPreset(ViewPreset::Left);
        }
    });

    auto addNavButton = [&](theme::Icon icon, const QString& tip) {
        auto* b = new IconButtonItem(icon, tip);
        scene->addItem(b);
        m_navButtons.push_back(b);
        return b;
    };
    connect(addNavButton(theme::Icon::Zoom, tr("Zoom in")), &IconButtonItem::clicked, this, [this] { m_camera.dolly(2.0f); });
    connect(addNavButton(theme::Icon::Frame, tr("Frame vehicle (Home)")), &IconButtonItem::clicked, this, [this] { frameVehicle(); });
    connect(addNavButton(theme::Icon::Camera, tr("Three-quarter view")), &IconButtonItem::clicked, this, [this] { setViewPreset(ViewPreset::ThreeQuarter); });
    m_orthoButton = addNavButton(theme::Icon::Perspective, tr("Toggle perspective / orthographic (Numpad 5)"));
    connect(m_orthoButton, &IconButtonItem::clicked, this, [this] { setOrthographic(!isOrthographic()); });
    connect(addNavButton(theme::Icon::Screenshot, tr("Save screenshot")), &IconButtonItem::clicked, this, &ViewportView::screenshotRequested);

    m_legend = new ColorLegendItem;
    scene->addItem(m_legend);
    m_forceCard = new ForceCardItem;
    scene->addItem(m_forceCard);
    m_probe = new ProbeItem;
    m_probe->hide();
    scene->addItem(m_probe);
    m_loading = new LoadingItem;
    m_loading->hide();
    scene->addItem(m_loading);

    // --- camera animation -------------------------------------------------------------------
    m_cameraAnimation.setStartValue(0.0);
    m_cameraAnimation.setEndValue(1.0);
    connect(&m_cameraAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_camera.setState(OrbitCamera::interpolate(m_animFrom, m_animTo, v.toFloat()));
    });

    // --- document & simulation --------------------------------------------------------------
    connect(m_doc, &SceneDocument::meshChanged, this, [this] {
        m_renderer.setMesh(m_doc->mesh(), m_doc->lodMesh());
        m_framedOnce = false;
    });
    connect(m_doc, &SceneDocument::transferFunctionChanged, this, [this] {
        m_renderer.setTransferFunction(m_doc->transferFunction());
        updateLegend();
    });
    connect(m_doc, &SceneDocument::viewChanged, this, [this] { updateLegend(); });
    connect(m_simulation, &SimulationController::snapshotReady, this, &ViewportView::onSnapshot);
    connect(m_simulation, &SimulationController::domainReady, this, &ViewportView::onDomain);
    m_renderer.setTransferFunction(m_doc->transferFunction());
    updateLegend();

    // --- frame loop -------------------------------------------------------------------------
    connect(&m_frameTimer, &QTimer::timeout, this, [this] {
        m_loading->advanceAnimation(m_clock.elapsed() * 1e-3);
        viewport()->update();
    });
    m_frameTimer.start(16);
    m_clock.start();
    m_fpsClock.start();
}

ViewportView::~ViewportView()
{
    if (m_renderer.isInitialized()) {
        m_gl->makeCurrent();
        m_renderer.destroy();
        m_gl->doneCurrent();
    }
}

void ViewportView::setSidePanel(QWidget* panel)
{
    m_panelProxy = scene()->addWidget(panel);
    m_panelProxy->setZValue(20);
    m_panelProxy->setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    layoutOverlays();
}

void ViewportView::setSidePanelVisible(bool visible)
{
    if (m_panelProxy) {
        m_panelProxy->setVisible(visible);
        layoutOverlays();
    }
}

bool ViewportView::isSidePanelVisible() const
{
    return m_panelProxy && m_panelProxy->isVisible();
}

void ViewportView::setLoadingState(const QString& title, const QString& detail, double progress)
{
    m_loading->setState(title, detail, progress);
    m_loading->show();
    layoutOverlays();
}

void ViewportView::clearLoadingState()
{
    m_loading->hide();
}

QMatrix4x4 ViewportView::projection() const
{
    const float aspect = static_cast<float>(std::max(1, viewport()->width())) / static_cast<float>(std::max(1, viewport()->height()));
    return m_camera.projectionMatrix(aspect);
}

SceneRenderer::Frame ViewportView::makeFrame() const
{
    SceneRenderer::Frame f;
    f.view = m_camera.viewMatrix();
    f.projection = projection();
    f.cameraPosition = m_camera.position();
    f.viewDirection = m_camera.forward();
    f.orthographic = m_camera.isOrthographic();
    f.pixelSize = viewport()->size() * viewport()->devicePixelRatioF();
    f.time = static_cast<float>(m_clock.elapsed() * 1e-3);
    f.settings = m_doc->view();
    f.domain = m_domain;
    f.tracers = m_doc->tracers();
    f.selectedObject = m_doc->selectedObject();
    const ViewportQuality quality = f.settings.quality == ViewportQuality::Auto
        ? (m_renderer.isSoftwareRasterizer() ? ViewportQuality::Performance : ViewportQuality::High)
        : f.settings.quality;
    f.useLod = quality == ViewportQuality::Performance;
    f.samples = quality == ViewportQuality::Performance ? 0 : 4;
    f.fxaa = quality == ViewportQuality::Performance;
    for (const SceneObject& o : m_doc->objects()) {
        f.objectVisible.push_back(o.visible ? 1 : 0);
    }
    return f;
}

void ViewportView::drawBackground(QPainter* painter, const QRectF&)
{
    painter->beginNativePainting();
    if (!m_renderer.isInitialized()) {
        QString error;
        if (m_renderer.initialize(&error)) {
            m_renderer.setMesh(m_doc->mesh(), m_doc->lodMesh());
            m_renderer.setTransferFunction(m_doc->transferFunction());
            if (m_snapshot) {
                m_renderer.setSnapshot(m_snapshot);
            }
            connect(m_gl->context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
                m_gl->makeCurrent();
                m_renderer.destroy();
                m_gl->doneCurrent();
            });
            emit rendererReady(m_renderer.rendererInfo());
        } else {
            spdlog::critical("Renderer initialisation failed");
        }
    }
    m_renderer.setModelMatrix(toQMatrix(m_doc->modelToWorld()));
    static const bool profile = qEnvironmentVariableIsSet("WT_PROFILE");
    QElapsedTimer renderTimer;
    renderTimer.start();
    m_renderer.render(makeFrame(), m_gl->defaultFramebufferObject());
    if (profile) {
        m_gl->context()->functions()->glFinish();
        static double acc = 0;
        static int n = 0;
        acc += renderTimer.nsecsElapsed() * 1e-6;
        if (++n == 30) {
            spdlog::info("3D render: {:.1f} ms/frame", acc / n);
            acc = 0;
            n = 0;
        }
    }
    painter->endNativePainting();

    m_gizmo->setViewMatrix(m_camera.viewMatrix());
    updateHud();

    ++m_frames;
    if (m_fpsClock.elapsed() >= 1000) {
        m_fps = m_frames * 1000.0 / static_cast<double>(m_fpsClock.restart());
        m_frames = 0;
        emit framesPerSecondChanged(m_fps);
    }
}

void ViewportView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    scene()->setSceneRect(QRectF(QPointF(0, 0), QSizeF(viewport()->size())));
    layoutOverlays();
}

void ViewportView::layoutOverlays()
{
    const QSizeF s = viewport()->size();
    const bool panel = m_panelProxy && m_panelProxy->isVisible();
    const qreal right = panel ? s.width() - kPanelWidth - kMargin * 2 : s.width() - kMargin;

    if (m_panelProxy) {
        m_panelProxy->setPos(s.width() - kPanelWidth - kMargin, kMargin);
        m_panelProxy->resize(kPanelWidth, std::max(200.0, s.height() - 2 * kMargin));
    }
    m_hud->setPos(m_toolShelf->size().width() + kMargin * 2, kMargin - 2);
    m_toolShelf->setPos(kMargin, kMargin + 56);
    m_gizmo->setPos(right - m_gizmo->size().width(), kMargin);
    qreal y = kMargin + m_gizmo->size().height() + 6;
    for (IconButtonItem* b : m_navButtons) {
        b->setPos(right - m_gizmo->size().width() / 2 - b->size().width() / 2, y);
        y += b->size().height() + 6;
    }
    m_legend->setPos(kMargin, s.height() - m_legend->size().height() - kMargin);
    m_forceCard->setPos(kMargin, s.height() - m_legend->size().height() - m_forceCard->size().height() - kMargin - 8);
    m_loading->setPos((right + kMargin - m_loading->size().width()) / 2, (s.height() - m_loading->size().height()) / 2);
}

void ViewportView::updateLegend()
{
    const ViewSettings& v = m_doc->view();
    const bool showingField = v.shading == SurfaceShading::ScalarField || v.showSlice || v.showVolume;
    // Streamlines and particles are coloured by speed; other displays by the selected field.
    const ScalarField field = showingField ? v.field : ScalarField::Speed;
    const ScalarRange range = v.ranges[static_cast<int>(field)];
    QString unit;
    double scale = 1.0;
    switch (field) {
    case ScalarField::Speed:
        unit = QStringLiteral("m/s");
        scale = m_doc->flow().windSpeed;
        break;
    case ScalarField::Pressure:
        unit = QStringLiteral("—");
        break;
    case ScalarField::Vorticity:
        unit = QStringLiteral("ω·L/U");
        break;
    }
    m_legend->setLegend(m_doc->transferFunction(), range, tr(fieldTitle(field)), unit, scale);
}

void ViewportView::updateHud()
{
    QStringList lines;
    lines << QStringLiteral("%1%2").arg(m_viewName, m_camera.isOrthographic() ? QStringLiteral(" · Orthographic") : QString());
    QString object = m_doc->modelName();
    if (m_doc->selectedObject() >= 0 && m_doc->selectedObject() < static_cast<int>(m_doc->objects().size())) {
        object += QStringLiteral(" | ") + m_doc->objects()[static_cast<std::size_t>(m_doc->selectedObject())].name;
    }
    lines << QStringLiteral("(%1) Wind Tunnel | %2").arg(m_snapshot ? m_snapshot->stats.step : 0).arg(object);
    if (m_snapshot) {
        const SolverStats& s = m_snapshot->stats;
        lines << QStringLiteral("t = %1 s · %2 MLUPS · Re %3 · %4 fps")
                     .arg(s.physicalTime, 0, 'f', 3)
                     .arg(s.mlups, 0, 'f', 0)
                     .arg(m_doc->flow().reynolds, 0, 'f', 0)
                     .arg(m_fps, 0, 'f', 0);
    }
    m_hud->setLines(lines);
}

void ViewportView::onSnapshot(const SnapshotPtr& snapshot)
{
    m_snapshot = snapshot;
    m_renderer.setSnapshot(snapshot);
    m_forceCard->setData(snapshot->stats, m_simulation->history(), m_simulation->isRunning());
}

void ViewportView::onDomain(const DomainInfo& domain)
{
    m_domain = domain;
    if (!m_framedOnce) {
        m_framedOnce = true;
        OrbitCamera::State s = m_camera.framed(domain.vehicleBounds);
        s.yaw = -38.0f;
        s.pitch = 16.0f;
        s.distance *= 1.25f;
        m_camera.setState(s);
    }
}

void ViewportView::animateCamera(const OrbitCamera::State& target, int durationMs)
{
    m_cameraAnimation.stop();
    m_animFrom = m_camera.state();
    m_animTo = target;
    m_cameraAnimation.setDuration(durationMs);
    m_cameraAnimation.start();
}

void ViewportView::setViewPreset(ViewPreset preset)
{
    OrbitCamera::State s = m_camera.state();
    switch (preset) {
    case ViewPreset::Front:
        s.yaw = -90.0f;
        s.pitch = 0.0f;
        m_viewName = QStringLiteral("Front (upwind)");
        break;
    case ViewPreset::Back:
        s.yaw = 90.0f;
        s.pitch = 0.0f;
        m_viewName = QStringLiteral("Back (downwind)");
        break;
    case ViewPreset::Right:
        s.yaw = 0.0f;
        s.pitch = 0.0f;
        m_viewName = QStringLiteral("Right");
        break;
    case ViewPreset::Left:
        s.yaw = 180.0f;
        s.pitch = 0.0f;
        m_viewName = QStringLiteral("Left");
        break;
    case ViewPreset::Top:
        s.yaw = 0.0f;
        s.pitch = 89.5f;
        m_viewName = QStringLiteral("Top");
        break;
    case ViewPreset::Bottom:
        s.yaw = 0.0f;
        s.pitch = -89.5f;
        m_viewName = QStringLiteral("Bottom");
        break;
    case ViewPreset::ThreeQuarter:
        s = m_camera.framed(m_domain.valid() ? m_domain.vehicleBounds : m_doc->vehicleBounds());
        s.yaw = -38.0f;
        s.pitch = 16.0f;
        s.distance *= 1.25f;
        s.orthoBlend = m_camera.state().orthoBlend;
        m_viewName = QStringLiteral("User Perspective");
        break;
    }
    animateCamera(s);
}

void ViewportView::frameVehicle(bool animated)
{
    const Aabb box = m_domain.valid() ? m_domain.vehicleBounds : m_doc->vehicleBounds();
    OrbitCamera::State s = m_camera.framed(box);
    s.distance *= 1.1f;
    animated ? animateCamera(s) : m_camera.setState(s);
}

void ViewportView::setOrthographic(bool ortho)
{
    OrbitCamera::State s = m_camera.state();
    s.orthoBlend = ortho ? 1.0f : 0.0f;
    m_orthoButton->setIcon(ortho ? theme::Icon::Ortho : theme::Icon::Perspective);
    animateCamera(s, 260);
}

void ViewportView::setTool(ViewportTool tool)
{
    m_tool = tool;
    m_toolShelf->setTool(tool);
    viewport()->setCursor(tool == ViewportTool::Probe ? Qt::CrossCursor : Qt::ArrowCursor);
    if (tool != ViewportTool::Probe) {
        m_probe->hide();
    }
}

bool ViewportView::isOrthographic() const
{
    return m_camera.state().orthoBlend > 0.5f;
}

QImage ViewportView::capture(bool withOverlays)
{
    m_gl->makeCurrent();
    QImage frame = m_renderer.grabFrame();
    m_gl->doneCurrent();
    if (frame.isNull()) {
        return frame;
    }
    frame = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    frame.setDevicePixelRatio(viewport()->devicePixelRatioF());
    if (withOverlays) {
        QPainter p(&frame);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        const QRectF target(QPointF(0, 0), QSizeF(viewport()->size()));
        scene()->render(&p, target, scene()->sceneRect());
    } else {
        // Report capture: keep the informative overlays, hide the interactive chrome.
        const bool panel = m_panelProxy && m_panelProxy->isVisible();
        std::vector<QGraphicsItem*> hidden { m_toolShelf, m_gizmo, m_probe, m_loading };
        for (auto* b : m_navButtons) {
            hidden.push_back(b);
        }
        if (m_panelProxy) {
            hidden.push_back(m_panelProxy);
        }
        std::vector<bool> wasVisible;
        for (auto* item : hidden) {
            wasVisible.push_back(item->isVisible());
            item->hide();
        }
        QPainter p(&frame);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        scene()->render(&p, QRectF(QPointF(0, 0), QSizeF(viewport()->size())), scene()->sceneRect());
        p.end();
        for (std::size_t i = 0; i < hidden.size(); ++i) {
            hidden[i]->setVisible(wasVisible[i]);
        }
        if (m_panelProxy) {
            m_panelProxy->setVisible(panel);
        }
    }
    return frame;
}

void ViewportView::probeAt(const QPoint& pos)
{
    if (!m_snapshot) {
        return;
    }
    // Un-project the cursor into a world-space ray.
    const QMatrix4x4 inv = (projection() * m_camera.viewMatrix()).inverted();
    const float x = 2.0f * static_cast<float>(pos.x()) / static_cast<float>(viewport()->width()) - 1.0f;
    const float y = 1.0f - 2.0f * static_cast<float>(pos.y()) / static_cast<float>(viewport()->height());
    const QVector3D nearP = inv.map(QVector3D(x, y, -1.0f));
    const QVector3D farP = inv.map(QVector3D(x, y, 1.0f));
    const QVector3D dirQ = (farP - nearP).normalized();
    const Vec3f origin { nearP.x(), nearP.y(), nearP.z() };
    const Vec3f dir { dirQ.x(), dirQ.y(), dirQ.z() };

    const GridSpec& g = m_snapshot->grid;
    const Aabb box = g.worldBounds();
    float tEnter = 0.0f, tLeave = std::numeric_limits<float>::max();
    for (int a = 0; a < 3; ++a) {
        const float inv = 1.0f / (std::abs(dir[a]) > 1e-8f ? dir[a] : 1e-8f);
        float t0 = (box.min[a] - origin[a]) * inv;
        float t1 = (box.max[a] - origin[a]) * inv;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        tEnter = std::max(tEnter, t0);
        tLeave = std::min(tLeave, t1);
    }
    if (tLeave <= tEnter) {
        m_probe->hide();
        return;
    }

    // Candidate hit on the slice plane.
    const ViewSettings& v = m_doc->view();
    float tSlice = std::numeric_limits<float>::max();
    if (v.showSlice) {
        const int axis = static_cast<int>(v.sliceAxis);
        const float plane = box.min[axis] + static_cast<float>(v.slicePosition) * (box.max[axis] - box.min[axis]);
        if (std::abs(dir[axis]) > 1e-6f) {
            const float t = (plane - origin[axis]) / dir[axis];
            if (t > tEnter && t < tLeave) {
                tSlice = t;
            }
        }
    }
    // March to the first solid cell (the vehicle surface).
    float tHit = std::numeric_limits<float>::max();
    for (float t = tEnter; t < std::min(tLeave, tSlice); t += 0.4f * g.dx) {
        if (m_snapshot->valueAt(origin + dir * t, 3) > 0.5f) {
            tHit = t;
            break;
        }
    }
    const bool onSurface = tHit < tSlice;
    const float t = std::min(tHit, tSlice);
    if (t == std::numeric_limits<float>::max()) {
        m_probe->hide();
        return;
    }
    const Vec3f p = origin + dir * t;
    const double speed = m_snapshot->valueAt(p, 0);
    const double cp = m_snapshot->valueAt(p, 1);
    const double vort = m_snapshot->valueAt(p, 2);
    const QStringList lines {
        onSurface ? tr("Surface probe") : tr("Slice probe"),
        tr("Speed      %1 m/s  (%2 U∞)").arg(speed * m_doc->flow().windSpeed, 0, 'f', 1).arg(speed, 0, 'f', 2),
        tr("Cp          %1").arg(cp, 0, 'f', 3),
        tr("Vorticity  %1 ω·L/U").arg(vort, 0, 'f', 1),
        tr("Position   %1, %2, %3 m").arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2).arg(p.z, 0, 'f', 2),
    };
    m_probe->setProbe(QPointF(pos), lines);
    m_probe->show();
}

void ViewportView::mousePressEvent(QMouseEvent* event)
{
    if (itemAt(event->pos())) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    setFocus();
    if (event->button() == Qt::LeftButton && m_tool == ViewportTool::Probe) {
        probeAt(event->pos());
        event->accept();
        return;
    }
    m_dragButton = event->button();
    m_lastMouse = event->position();
    m_cameraAnimation.stop();
    event->accept();
}

void ViewportView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragButton == Qt::NoButton) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    const QPointF d = event->position() - m_lastMouse;
    m_lastMouse = event->position();
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool ctrl = event->modifiers() & Qt::ControlModifier;

    enum class Op { Orbit, Pan, Zoom } op = Op::Orbit;
    if (m_dragButton == Qt::RightButton) {
        op = Op::Pan;
    } else if (m_dragButton == Qt::MiddleButton) {
        op = shift ? Op::Pan : (ctrl ? Op::Zoom : Op::Orbit);
    } else if (m_dragButton == Qt::LeftButton) {
        op = shift || m_tool == ViewportTool::Pan ? Op::Pan : (ctrl || m_tool == ViewportTool::Zoom ? Op::Zoom : Op::Orbit);
    }
    switch (op) {
    case Op::Orbit:
        m_camera.orbit(static_cast<float>(d.x()), static_cast<float>(d.y()));
        m_viewName = QStringLiteral("User Perspective");
        break;
    case Op::Pan:
        m_camera.pan(static_cast<float>(d.x()), static_cast<float>(d.y()), static_cast<float>(viewport()->height()));
        break;
    case Op::Zoom:
        m_camera.dolly(static_cast<float>(-d.y()) * 0.05f);
        break;
    }
    event->accept();
}

void ViewportView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragButton != Qt::NoButton && event->button() == m_dragButton) {
        m_dragButton = Qt::NoButton;
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ViewportView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (itemAt(event->pos())) {
        QGraphicsView::mouseDoubleClickEvent(event);
        return;
    }
    frameVehicle();
}

void ViewportView::wheelEvent(QWheelEvent* event)
{
    if (itemAt(event->position().toPoint())) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    m_cameraAnimation.stop();
    m_camera.dolly(static_cast<float>(event->angleDelta().y()) / 120.0f);
    event->accept();
}

void ViewportView::keyPressEvent(QKeyEvent* event)
{
    // Keys go to the embedded panel first when one of its widgets has focus.
    if (scene()->focusItem()) {
        QGraphicsView::keyPressEvent(event);
        if (event->isAccepted()) {
            return;
        }
    }
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    switch (event->key()) {
    case Qt::Key_N:
        setSidePanelVisible(!isSidePanelVisible());
        break;
    case Qt::Key_Home:
    case Qt::Key_F:
        frameVehicle();
        break;
    case Qt::Key_1:
        setViewPreset(ctrl ? ViewPreset::Back : ViewPreset::Front);
        break;
    case Qt::Key_3:
        setViewPreset(ctrl ? ViewPreset::Left : ViewPreset::Right);
        break;
    case Qt::Key_7:
        setViewPreset(ctrl ? ViewPreset::Bottom : ViewPreset::Top);
        break;
    case Qt::Key_0:
    case Qt::Key_9:
        setViewPreset(ViewPreset::ThreeQuarter);
        break;
    case Qt::Key_5:
        setOrthographic(!isOrthographic());
        break;
    case Qt::Key_Escape:
        m_probe->hide();
        break;
    default:
        QGraphicsView::keyPressEvent(event);
        return;
    }
    event->accept();
}

} // namespace fluid::app
