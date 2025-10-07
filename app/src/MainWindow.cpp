#include "MainWindow.hpp"

#include "logging/Logging.hpp"
#include "panels/FluidFlowPanel.hpp"
#include "panels/OutlinerPanel.hpp"
#include "panels/PropertiesPanel.hpp"
#include "ui/Theme.hpp"
#include "version.h"
#include "viewport/ViewportView.hpp"
#include "widgets/TimelineWidget.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace fluid::app {
namespace {
    std::shared_ptr<spdlog::logger> appLog() { return logging::get(logging::channel::App); }
}

MainWindow::MainWindow(const StartupOptions& options, QWidget* parent)
    : QMainWindow(parent)
    , m_options(options)
{
    setWindowTitle(QStringLiteral("Wind Tunnel %1 — Virtual Aerodynamics Lab").arg(QStringLiteral(APPLICATION_VERSION_STR)));
    setWindowIcon(theme::icon(theme::Icon::Wind, theme::kAccentBright));
    resize(1680, 980);

    // --- services & model ---------------------------------------------------------------------
    m_doc = new SceneDocument(this);
    m_simulation = new SimulationController(this);
    m_meshProvider = new MeshProvider(this);
    m_reports = new ReportService(this);
    if (m_options.resolution > 0) {
        TunnelSettings t = m_doc->tunnel();
        t.resolution = m_options.resolution;
        m_doc->setTunnel(t);
    }

    // --- layout ---------------------------------------------------------------------------
    setMenuWidget(buildTopBar());

    m_viewport = new ViewportView(m_doc, m_simulation);
    m_sidePanel = new FluidFlowPanel(m_doc);
    m_viewport->setSidePanel(m_sidePanel);

    auto* viewportArea = new QWidget;
    auto* va = new QVBoxLayout(viewportArea);
    va->setContentsMargins(0, 0, 0, 0);
    va->setSpacing(0);
    va->addWidget(buildViewportHeader());
    va->addWidget(m_viewport, 1);

    m_outliner = new OutlinerPanel(m_doc);
    m_properties = new PropertiesPanel(m_doc, m_simulation);
    auto* rightColumn = new QSplitter(Qt::Vertical);
    rightColumn->addWidget(m_outliner);
    rightColumn->addWidget(m_properties);
    rightColumn->setStretchFactor(0, 2);
    rightColumn->setStretchFactor(1, 5);
    rightColumn->setSizes({ 300, 640 });

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(viewportArea);
    split->addWidget(rightColumn);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({ 1330, 350 });

    m_timeline = new TimelineWidget;
    auto* central = new QWidget;
    auto* cv = new QVBoxLayout(central);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(2);
    cv->addWidget(split, 1);
    cv->addWidget(m_timeline);
    setCentralWidget(central);

    // --- status bar ---------------------------------------------------------------------------
    auto* hint = new QLabel(tr("LMB orbit · Shift/RMB pan · Wheel zoom · N sidebar · 1/3/7 views · Space run/pause"));
    statusBar()->addWidget(hint, 1);
    m_statusModel = new QLabel;
    m_statusGrid = new QLabel;
    m_statusPerf = new QLabel;
    m_statusGl = new QLabel;
    for (QLabel* l : { m_statusModel, m_statusGrid, m_statusPerf, m_statusGl }) {
        statusBar()->addPermanentWidget(l);
    }
    statusBar()->setSizeGripEnabled(false);

    // --- wiring ---------------------------------------------------------------------------------
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(250);
    connect(&m_rebuildTimer, &QTimer::timeout, this, [this] {
        if (!m_doc->mesh()) {
            return;
        }
        m_resumeAfterBuild = m_resumeAfterBuild || m_simulation->isRunning();
        appLog()->info("Rebuilding wind tunnel ({} collision objects, resolution {})", m_doc->collisionObjects().size(), m_doc->tunnel().resolution);
        m_simulation->setRunning(false);
        m_simulation->rebuild(m_doc->rebuildRequest());
    });
    connect(m_doc, &SceneDocument::geometryChanged, this, &MainWindow::requestRebuild);
    connect(m_doc, &SceneDocument::flowChanged, m_simulation, &SimulationController::setFlow);
    connect(m_doc, &SceneDocument::tracersChanged, m_simulation, &SimulationController::setTracers);
    connect(m_doc, &SceneDocument::viewChanged, this, &MainWindow::syncHeaderButtons);
    connect(m_doc, &SceneDocument::tracersChanged, this, &MainWindow::syncHeaderButtons);

    connect(m_meshProvider, &MeshProvider::started, this, [this](const QString& path) {
        m_viewport->setLoadingState(tr("Loading model"), QFileInfo(path).fileName(), 0.0);
    });
    connect(m_meshProvider, &MeshProvider::progress, this, [this](double p) {
        m_viewport->setLoadingState(tr("Loading model"), QFileInfo(m_options.modelPath).fileName(), p);
    });
    connect(m_meshProvider, &MeshProvider::loaded, this, [this](const MeshLoadResult& result) {
        statusBar()->showMessage(tr("Loaded %1 in %2 s").arg(QFileInfo(result.path).fileName()).arg(result.seconds, 0, 'f', 2), 5000);
        m_doc->setMesh(result.mesh, result.lod, result.path);
        updateStatus();
    });
    connect(m_meshProvider, &MeshProvider::failed, this, [this](const QString& message) {
        m_viewport->clearLoadingState();
        QMessageBox::warning(this, tr("Cannot load model"), message);
    });

    connect(m_simulation, &SimulationController::buildStarted, this, [this] {
        m_viewport->setLoadingState(tr("Building wind tunnel"), tr("Voxelizing collision objects and initialising the lattice…"), -1.0);
    });
    connect(m_simulation, &SimulationController::domainReady, this, [this](const DomainInfo& domain) {
        m_viewport->clearLoadingState();
        m_sidePanel->setDomainInfo(domain);
        updateStatus();
        if (m_resumeAfterBuild && m_options.autoRun) {
            m_simulation->setRunning(true);
        }
        m_resumeAfterBuild = false;
    });
    connect(m_simulation, &SimulationController::snapshotReady, this, [this](const SnapshotPtr& snapshot) {
        m_timeline->setProgress(snapshot->stats.step, snapshot->stats.physicalTime);
        if (m_simulation->isRunning() && snapshot->stats.step >= m_timeline->endStep()) {
            m_simulation->setRunning(false);
            appLog()->info("Reached the end iteration {} — solver paused", m_timeline->endStep());
            statusBar()->showMessage(tr("Reached the end iteration — solver paused"), 5000);
        }
    });
    connect(m_simulation, &SimulationController::historyChanged, this, [this] { m_timeline->setHistory(m_simulation->history()); });
    connect(m_simulation, &SimulationController::runningChanged, m_timeline, &TimelineWidget::setRunning);
    connect(m_simulation, &SimulationController::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });

    connect(m_timeline, &TimelineWidget::playToggled, this, [this](bool play) { m_simulation->setRunning(play); });
    connect(m_timeline, &TimelineWidget::stepRequested, m_simulation, &SimulationController::stepOnce);
    connect(m_timeline, &TimelineWidget::resetRequested, m_simulation, &SimulationController::resetFlow);

    connect(m_sidePanel, &FluidFlowPanel::createSimulationRequested, this, [this] {
        m_resumeAfterBuild = true;
        m_rebuildTimer.start(0);
    });
    connect(m_properties, &PropertiesPanel::exportReportRequested, this, [this] { exportReport(); });
    connect(m_properties, &PropertiesPanel::previewReportRequested, this, &MainWindow::previewReport);
    connect(m_reports, &ReportService::exported, this, [this](const QString& path) {
        statusBar()->showMessage(tr("Report written to %1").arg(path), 8000);
    });
    connect(m_reports, &ReportService::failed, this, [this](const QString& message) { QMessageBox::warning(this, tr("Report"), message); });

    connect(m_viewport, &ViewportView::rendererReady, this, [this](const QString& info) {
        m_statusGl->setText(info);
        m_statusGl->setToolTip(info);
    });
    connect(m_viewport, &ViewportView::framesPerSecondChanged, this, [this] { updateStatus(); });
    connect(m_viewport, &ViewportView::screenshotRequested, this, &MainWindow::saveScreenshot);

    auto* space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    space->setContext(Qt::ApplicationShortcut);
    connect(space, &QShortcut::activated, this, &MainWindow::togglePlay);

    applyWorkspace(m_options.workspace);
    syncHeaderButtons();
    updateStatus();

    if (!m_options.modelPath.isEmpty()) {
        QTimer::singleShot(0, this, [this] { openModel(m_options.modelPath); });
    }
    if (!m_options.screenshotPath.isEmpty() || !m_options.reportPath.isEmpty()) {
        QTimer::singleShot(static_cast<int>(m_options.automationDelay * 1000), this, &MainWindow::runAutomation);
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_simulation->setRunning(false);
    m_meshProvider->cancel();
    event->accept();
}

void MainWindow::openModel(const QString& path)
{
    m_options.modelPath = path;
    appLog()->info("Opening model {}", path.toStdString());
    m_resumeAfterBuild = true;
    m_meshProvider->load(path);
}

void MainWindow::requestRebuild()
{
    m_rebuildTimer.start();
}

void MainWindow::togglePlay()
{
    m_simulation->setRunning(!m_simulation->isRunning());
}

QWidget* MainWindow::buildTopBar()
{
    auto* bar = new QFrame;
    bar->setObjectName(QStringLiteral("headerBar"));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(6, 0, 10, 0);
    h->setSpacing(6);

    auto* logo = new QLabel;
    logo->setPixmap(theme::icon(theme::Icon::Wind, theme::kAccentBright).pixmap(20, 20));
    logo->setToolTip(tr("Wind Tunnel"));
    h->addWidget(logo);

    auto* menus = new QMenuBar;
    menus->setNativeMenuBar(false);
    buildMenus(menus);
    h->addWidget(menus);

    m_workspaces = new QTabBar;
    m_workspaces->setObjectName(QStringLiteral("workspaceTabs"));
    m_workspaces->setDrawBase(false);
    m_workspaces->setExpanding(false);
    for (const QString& name : { tr("Layout"), tr("Aerodynamics"), tr("Flow Structures"), tr("Smoke"), tr("Wind Tunnel") }) {
        m_workspaces->addTab(name);
    }
    m_workspaces->setTabToolTip(0, tr("Studio shading with animated streamlines"));
    m_workspaces->setTabToolTip(1, tr("Surface pressure and a pressure slice"));
    m_workspaces->setTabToolTip(2, tr("Volume-rendered vorticity"));
    m_workspaces->setTabToolTip(3, tr("Smoke particles"));
    m_workspaces->setTabToolTip(4, tr("Velocity slice with streamlines from the side"));
    connect(m_workspaces, &QTabBar::currentChanged, this, &MainWindow::applyWorkspace);
    h->addWidget(m_workspaces);
    h->addStretch(1);

    auto* scene = new QLabel(tr("Scene  ·  Wind Tunnel"));
    scene->setObjectName(QStringLiteral("dimLabel"));
    h->addWidget(scene);
    return bar;
}

void MainWindow::buildMenus(QMenuBar* bar)
{
    QMenu* file = bar->addMenu(tr("File"));
    file->addAction(theme::icon(theme::Icon::Folder), tr("Open Model…"), QKeySequence::Open, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open Model"), QFileInfo(m_options.modelPath).absolutePath(), tr("Wavefront OBJ (*.obj)"));
        if (!path.isEmpty()) {
            openModel(path);
        }
    });
    file->addSeparator();
    file->addAction(theme::icon(theme::Icon::Report), tr("Export PDF Report…"), QKeySequence(Qt::CTRL | Qt::Key_E), this, [this] { exportReport(); });
    file->addAction(theme::icon(theme::Icon::Screenshot), tr("Save Screenshot…"), QKeySequence(Qt::Key_F12), this, &MainWindow::saveScreenshot);
    file->addSeparator();
    file->addAction(tr("Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* sim = bar->addMenu(tr("Simulation"));
    sim->addAction(theme::icon(theme::Icon::Play), tr("Run / Pause"), this, &MainWindow::togglePlay);
    sim->addAction(theme::icon(theme::Icon::StepForward), tr("Step"), m_simulation, &SimulationController::stepOnce);
    sim->addAction(theme::icon(theme::Icon::Reset), tr("Reset Flow"), m_simulation, &SimulationController::resetFlow);
    sim->addSeparator();
    sim->addAction(theme::icon(theme::Icon::Plus), tr("Rebuild Wind Tunnel"), this, [this] {
        m_resumeAfterBuild = true;
        m_rebuildTimer.start(0);
    });

    QMenu* view = bar->addMenu(tr("View"));
    view->addAction(tr("Toggle Sidebar"), this, [this] { m_viewport->setSidePanelVisible(!m_viewport->isSidePanelVisible()); });
    view->addAction(theme::icon(theme::Icon::Frame), tr("Frame Vehicle"), this, [this] { m_viewport->frameVehicle(); });
    view->addSeparator();
    view->addAction(tr("Front"), this, [this] { m_viewport->setViewPreset(ViewportView::ViewPreset::Front); });
    view->addAction(tr("Right"), this, [this] { m_viewport->setViewPreset(ViewportView::ViewPreset::Right); });
    view->addAction(tr("Top"), this, [this] { m_viewport->setViewPreset(ViewportView::ViewPreset::Top); });
    view->addAction(tr("Three-Quarter"), this, [this] { m_viewport->setViewPreset(ViewportView::ViewPreset::ThreeQuarter); });
    view->addAction(tr("Toggle Orthographic"), this, [this] { m_viewport->setOrthographic(!m_viewport->isOrthographic()); });

    QMenu* help = bar->addMenu(tr("Help"));
    help->addAction(tr("About Wind Tunnel"), this, [this] {
        QMessageBox::about(this, tr("About Wind Tunnel"),
            tr("<h3>Wind Tunnel %1</h3>"
               "<p>Real-time virtual wind tunnel: D3Q19 lattice Boltzmann solver with a Smagorinsky LES model, "
               "running asynchronously next to a QGraphicsView / OpenGL 3.3 viewport.</p>"
               "<p>Visualisation: streamline ribbons, smoke particles, MPR slice, volume ray casting and surface "
               "pressure, all driven by an editable transfer function.</p>")
                .arg(QStringLiteral(APPLICATION_VERSION_STR)));
    });
    help->addAction(tr("About Qt"), qApp, &QApplication::aboutQt);
}

QWidget* MainWindow::buildViewportHeader()
{
    auto* header = new QFrame;
    header->setObjectName(QStringLiteral("viewportHeader"));
    auto* h = new QHBoxLayout(header);
    h->setContentsMargins(8, 3, 8, 3);
    h->setSpacing(4);

    auto* mode = new QComboBox;
    mode->addItems({ tr("Object Mode"), tr("Probe Mode") });
    mode->setToolTip(tr("Probe mode: click on the vehicle or on the slice to read the local flow"));
    h->addWidget(mode);
    h->addSpacing(6);

    auto* viewMenu = new QToolButton;
    viewMenu->setText(tr("View"));
    viewMenu->setPopupMode(QToolButton::InstantPopup);
    viewMenu->setProperty("flat", true);
    auto* menu = new QMenu(viewMenu);
    menu->addAction(tr("Front (1)"), this, [this] { m_viewport->setViewPreset(ViewportView::ViewPreset::Front); });
    menu->addAction(tr("Right (3)"), this, [this] { m_viewport->setViewPreset(ViewportView::ViewPreset::Right); });
    menu->addAction(tr("Top (7)"), this, [this] { m_viewport->setViewPreset(ViewportView::ViewPreset::Top); });
    menu->addAction(tr("Back (Ctrl+1)"), this, [this] { m_viewport->setViewPreset(ViewportView::ViewPreset::Back); });
    menu->addAction(tr("Three-Quarter (0)"), this, [this] { m_viewport->setViewPreset(ViewportView::ViewPreset::ThreeQuarter); });
    menu->addSeparator();
    menu->addAction(tr("Frame Vehicle (Home)"), this, [this] { m_viewport->frameVehicle(); });
    menu->addAction(tr("Sidebar (N)"), this, [this] { m_viewport->setSidePanelVisible(!m_viewport->isSidePanelVisible()); });
    viewMenu->setMenu(menu);
    h->addWidget(viewMenu);

    auto* simMenuButton = new QToolButton;
    simMenuButton->setText(tr("Simulation"));
    simMenuButton->setPopupMode(QToolButton::InstantPopup);
    simMenuButton->setProperty("flat", true);
    auto* simMenu = new QMenu(simMenuButton);
    simMenu->addAction(tr("Run / Pause (Space)"), this, &MainWindow::togglePlay);
    simMenu->addAction(tr("Reset Flow"), m_simulation, &SimulationController::resetFlow);
    simMenu->addAction(tr("Rebuild Wind Tunnel"), this, [this] {
        m_resumeAfterBuild = true;
        m_rebuildTimer.start(0);
    });
    simMenuButton->setMenu(simMenu);
    h->addWidget(simMenuButton);
    h->addStretch(1);

    auto toggle = [&](theme::Icon icon, const QString& tip) {
        auto* b = new QToolButton;
        b->setIcon(theme::icon(icon));
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setProperty("flat", true);
        b->setIconSize(QSize(16, 16));
        b->setFocusPolicy(Qt::NoFocus);
        h->addWidget(b);
        return b;
    };
    m_streamButton = toggle(theme::Icon::Streamlines, tr("Streamlines"));
    m_particleButton = toggle(theme::Icon::Particles, tr("Smoke particles"));
    m_sliceButton = toggle(theme::Icon::Slice, tr("Slice plane"));
    m_volumeButton = toggle(theme::Icon::Volume, tr("Volume rendering"));
    connect(m_streamButton, &QToolButton::clicked, this, [this](bool on) {
        TracerSettings t = m_doc->tracers();
        t.streamlines = on;
        m_doc->setTracers(t);
    });
    connect(m_particleButton, &QToolButton::clicked, this, [this](bool on) {
        TracerSettings t = m_doc->tracers();
        t.particles = on;
        m_doc->setTracers(t);
    });
    connect(m_sliceButton, &QToolButton::clicked, this, [this](bool on) {
        ViewSettings v = m_doc->view();
        v.showSlice = on;
        m_doc->setView(v);
    });
    connect(m_volumeButton, &QToolButton::clicked, this, [this](bool on) {
        ViewSettings v = m_doc->view();
        v.showVolume = on;
        m_doc->setView(v);
    });
    h->addSpacing(10);

    // Shading buttons (like Blender's viewport shading spheres).
    const std::pair<QColor, QString> shading[] = {
        { QColor(0xc8, 0x30, 0x38), tr("Studio shading (materials)") },
        { QColor(0x8f, 0xa3, 0xc4), tr("Clay shading") },
        { QColor(0x3d, 0xc8, 0xf0), tr("Surface field shading (transfer function)") },
    };
    for (int i = 0; i < 3; ++i) {
        auto* b = toggle(theme::Icon::Sphere, shading[i].second);
        b->setIcon(theme::icon(theme::Icon::Sphere, shading[i].first));
        m_shadingButtons.push_back(b);
        connect(b, &QToolButton::clicked, this, [this, i] {
            ViewSettings v = m_doc->view();
            v.shading = static_cast<SurfaceShading>(i);
            m_doc->setView(v);
            syncHeaderButtons();
        });
    }
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_viewport->setTool(i == 1 ? ViewportTool::Probe : ViewportTool::Select);
        statusBar()->showMessage(i == 1 ? tr("Probe mode: click the vehicle or the slice to read the local flow") : QString(), 5000);
    });
    return header;
}

void MainWindow::syncHeaderButtons()
{
    const ViewSettings& v = m_doc->view();
    const TracerSettings& t = m_doc->tracers();
    for (int i = 0; i < m_shadingButtons.size(); ++i) {
        m_shadingButtons[i]->setChecked(static_cast<int>(v.shading) == i);
    }
    m_streamButton->setChecked(t.streamlines);
    m_particleButton->setChecked(t.particles);
    m_sliceButton->setChecked(v.showSlice);
    m_volumeButton->setChecked(v.showVolume);
}

void MainWindow::applyWorkspace(int index)
{
    if (m_workspaces && m_workspaces->currentIndex() != index) {
        m_workspaces->setCurrentIndex(index); // re-enters through currentChanged
        return;
    }
    ViewSettings v = m_doc->view();
    TracerSettings t = m_doc->tracers();
    TransferFunction tf = m_doc->transferFunction();
    switch (index) {
    case 0: // Layout
        v.shading = SurfaceShading::Studio;
        v.field = ScalarField::Speed;
        v.showSlice = false;
        v.showVolume = false;
        t.streamlines = true;
        t.particles = false;
        tf.setColormap(Colormap::Turbo);
        break;
    case 1: // Aerodynamics
        v.shading = SurfaceShading::ScalarField;
        v.field = ScalarField::Pressure;
        v.showSlice = true;
        v.sliceAxis = SliceAxis::Y;
        v.slicePosition = 0.12;
        v.showVolume = false;
        t.streamlines = false;
        t.particles = false;
        tf.setColormap(Colormap::CoolWarm);
        break;
    case 2: // Flow structures
        v.shading = SurfaceShading::Clay;
        v.field = ScalarField::Vorticity;
        v.showSlice = false;
        v.showVolume = true;
        t.streamlines = false;
        t.particles = false;
        tf.setColormap(Colormap::Inferno);
        tf.setOpacityPoints({ { 0.0f, 0.0f }, { 0.25f, 0.0f }, { 0.55f, 0.18f }, { 1.0f, 0.9f } });
        break;
    case 3: // Smoke
        v.shading = SurfaceShading::Studio;
        v.field = ScalarField::Speed;
        v.showSlice = false;
        v.showVolume = false;
        t.streamlines = false;
        t.particles = true;
        tf.setColormap(Colormap::Ice);
        break;
    case 4: // Wind tunnel (side slice)
        v.shading = SurfaceShading::Clay;
        v.field = ScalarField::Speed;
        v.showSlice = true;
        v.sliceAxis = SliceAxis::Z;
        v.slicePosition = 0.5;
        v.showVolume = false;
        t.streamlines = true;
        t.particles = false;
        tf.setColormap(Colormap::Turbo);
        break;
    default:
        return;
    }
    appLog()->debug("Workspace '{}' activated", m_workspaces ? m_workspaces->tabText(index).toStdString() : std::to_string(index));
    m_doc->setView(v);
    m_doc->setTracers(t);
    m_doc->setTransferFunction(tf);
    if (index == 4) {
        m_viewport->setViewPreset(ViewportView::ViewPreset::Right);
    } else if (index == 1) {
        m_viewport->setViewPreset(ViewportView::ViewPreset::ThreeQuarter);
    }
}

void MainWindow::saveScreenshot()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Screenshot"), dir + QStringLiteral("/wind-tunnel.png"), tr("PNG image (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    const QImage image = m_viewport->capture(true);
    if (image.save(path)) {
        appLog()->info("Screenshot saved to {} ({}x{})", path.toStdString(), image.width(), image.height());
        statusBar()->showMessage(tr("Screenshot saved to %1").arg(path), 5000);
    } else {
        appLog()->error("Cannot save screenshot to {}", path.toStdString());
    }
}

ReportData MainWindow::collectReport()
{
    ReportData d;
    d.title = m_properties->reportTitle();
    d.author = m_properties->reportAuthor();
    d.notes = m_properties->reportNotes();
    d.modelName = m_doc->modelName();
    if (const auto& mesh = m_doc->mesh()) {
        d.triangles = mesh->triangleCount();
        d.vertices = mesh->vertexCount();
    }
    d.objects = m_doc->collisionObjects().size();
    d.viewport = m_viewport->capture(false);
    d.domain = m_simulation->domain();
    if (const auto& snap = m_simulation->latestSnapshot()) {
        d.stats = snap->stats;
    }
    d.flow = m_doc->flow();
    d.tunnel = m_doc->tunnel();
    d.placement = m_doc->placement();
    d.history = m_simulation->history();
    const ViewSettings& v = m_doc->view();
    const bool showingField = v.shading == SurfaceShading::ScalarField || v.showSlice || v.showVolume;
    const ScalarField field = showingField ? v.field : ScalarField::Speed;
    const QString names[] = { tr("Velocity magnitude / U∞"), tr("Pressure coefficient Cp"), tr("Vorticity magnitude"), };
    d.fieldName = names[static_cast<int>(field)];
    d.fieldUnit = field == ScalarField::Vorticity ? QStringLiteral("ω·L/U") : QString();
    d.range = v.ranges[static_cast<int>(field)];
    for (int i = 0; i <= 24; ++i) {
        const Vec3f c = m_doc->transferFunction().color(static_cast<float>(i) / 24.0f);
        d.legend.push_back(QColor::fromRgbF(c.x, c.y, c.z));
    }
    return d;
}

void MainWindow::exportReport(const QString& target)
{
    QString path = target;
    if (path.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        path = QFileDialog::getSaveFileName(this, tr("Export Report"), dir + QStringLiteral("/%1-aero-report.pdf").arg(m_doc->modelName()), tr("PDF (*.pdf)"));
        if (path.isEmpty()) {
            return;
        }
    }
    statusBar()->showMessage(tr("Writing report…"));
    m_reports->exportPdf(collectReport(), path);
}

void MainWindow::previewReport()
{
    const ReportData data = collectReport();
    QImage page(595 * 2, 842 * 2, QImage::Format_ARGB32_Premultiplied);
    page.fill(Qt::white);
    QPainter p(&page);
    ReportService::paint(p, data, QRectF(0, 0, page.width(), page.height()));
    p.end();
    m_properties->setReportPreview(page);
}

void MainWindow::updateStatus()
{
    if (const auto& mesh = m_doc->mesh()) {
        std::size_t visible = 0;
        for (const auto& o : m_doc->objects()) {
            visible += o.visible;
        }
        m_statusModel->setText(tr("%1 | Verts %L2 | Tris %L3 | Objects %4/%5")
                                   .arg(m_doc->modelName())
                                   .arg(mesh->vertexCount())
                                   .arg(mesh->triangleCount())
                                   .arg(visible)
                                   .arg(m_doc->objects().size()));
    }
    const DomainInfo& d = m_simulation->domain();
    if (d.valid()) {
        m_statusGrid->setText(tr("Grid %1×%2×%3 (%L4 cells)").arg(d.grid.nx).arg(d.grid.ny).arg(d.grid.nz).arg(d.grid.cellCount()));
    }
    const double mlups = m_simulation->latestSnapshot() ? m_simulation->latestSnapshot()->stats.mlups : 0.0;
    m_statusPerf->setText(tr("%1 MLUPS | %2 fps").arg(mlups, 0, 'f', 0).arg(m_viewport->framesPerSecond(), 0, 'f', 0));
}

void MainWindow::runAutomation()
{
    if (m_automationDone) {
        return;
    }
    if (m_simulation->isBuilding() || !m_simulation->latestSnapshot()) {
        QTimer::singleShot(1000, this, &MainWindow::runAutomation); // not ready yet
        return;
    }
    m_automationDone = true;
    if (!m_options.screenshotPath.isEmpty()) {
        const QImage shot = grab().toImage();
        // The GL viewport is composited separately: paste our own capture over it.
        QImage full = shot.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        {
            QPainter p(&full);
            const QPoint offset = m_viewport->mapTo(this, QPoint(0, 0));
            p.drawImage(QRect(offset, m_viewport->size()), m_viewport->capture(true));
        }
        full.save(m_options.screenshotPath);
        appLog()->info("Screenshot written to {}", m_options.screenshotPath.toStdString());
    }
    if (!m_options.reportPath.isEmpty()) {
        m_reports->exportPdf(collectReport(), m_options.reportPath).waitForFinished();
        QCoreApplication::processEvents();
    }
    QTimer::singleShot(200, qApp, &QApplication::quit);
}

} // namespace fluid::app
