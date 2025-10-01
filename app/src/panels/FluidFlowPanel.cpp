#include "panels/FluidFlowPanel.hpp"

#include "ui/Theme.hpp"
#include "widgets/CollapsibleSection.hpp"
#include "widgets/ValueField.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace fluid::app {
namespace {

    struct TypePreset {
        const char* name;
        QColor swatch;
        double reynolds;
        double smagorinsky;
        double turbulence;
    };
    const TypePreset kTypes[] = {
        { QT_TRANSLATE_NOOP("FluidFlowPanel", "Laminar"), QColor(0x38, 0xd0, 0xf0), 1000.0, 0.10, 0.0 },
        { QT_TRANSLATE_NOOP("FluidFlowPanel", "Transition"), QColor(0x8b, 0x7c, 0xf6), 8000.0, 0.14, 0.01 },
        { QT_TRANSLATE_NOOP("FluidFlowPanel", "Turbulent"), QColor(0xf5, 0x9e, 0x3a), 40000.0, 0.17, 0.03 },
    };

    QIcon swatchIcon(const QColor& color)
    {
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, 0, 28);
        g.setColorAt(0, color.lighter(125));
        g.setColorAt(1, color.darker(130));
        p.setBrush(g);
        p.setPen(QPen(color.lighter(150), 1.5));
        p.drawRoundedRect(QRectF(3, 3, 22, 22), 5, 5);
        return QIcon(pm);
    }

    QScrollArea* scrollable(QWidget* content)
    {
        auto* area = new QScrollArea;
        area->setWidget(content);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        area->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
        return area;
    }

    QLabel* valueLabel(const QString& text)
    {
        auto* l = new QLabel(text);
        l->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2; border-radius: 4px; padding: 3px 8px;")
                             .arg(theme::kField.name(), theme::kBorder.name()));
        return l;
    }

} // namespace

FluidFlowPanel::FluidFlowPanel(SceneDocument* document, QWidget* parent)
    : QFrame(parent)
    , m_doc(document)
{
    setObjectName(QStringLiteral("panelCard"));
    setAttribute(Qt::WA_TranslucentBackground);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 2, 6);
    layout->setSpacing(4);

    auto* title = new QHBoxLayout;
    auto* icon = new QLabel;
    icon->setPixmap(theme::icon(theme::Icon::Wind, theme::kAccentBright).pixmap(16, 16));
    title->addWidget(icon);
    auto* label = new QLabel(tr("Fluid Flow"));
    label->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 10pt;"));
    title->addWidget(label, 1);
    auto* hint = new QLabel(QStringLiteral("N"));
    hint->setObjectName(QStringLiteral("dimLabel"));
    hint->setToolTip(tr("Press N in the viewport to hide this panel"));
    title->addWidget(hint);
    layout->addLayout(title);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("sideTabs"));
    m_tabs->setTabPosition(QTabWidget::East);
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(scrollable(buildSimulationTab()), tr("Fluid Flow"));
    m_tabs->addTab(scrollable(buildViewTab()), tr("View"));
    layout->addWidget(m_tabs, 1);

    connect(m_doc, &SceneDocument::flowChanged, this, &FluidFlowPanel::syncFromDocument);
    connect(m_doc, &SceneDocument::tracersChanged, this, &FluidFlowPanel::syncFromDocument);
    connect(m_doc, &SceneDocument::viewChanged, this, &FluidFlowPanel::syncFromDocument);
    connect(m_doc, &SceneDocument::geometryChanged, this, &FluidFlowPanel::syncFromDocument);
    connect(m_doc, &SceneDocument::objectsChanged, this, &FluidFlowPanel::syncObjects);
    connect(m_doc, &SceneDocument::selectionChanged, this, &FluidFlowPanel::syncObjects);
    syncFromDocument();
    syncObjects();
}

QWidget* FluidFlowPanel::buildSimulationTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 6, 0);
    v->setSpacing(6);

    // --- Simulation type ---------------------------------------------------------------------
    auto* type = new CollapsibleSection(tr("Simulation Type"));
    auto* typeRow = new QWidget;
    auto* typeLayout = new QHBoxLayout(typeRow);
    typeLayout->setContentsMargins(0, 0, 0, 0);
    typeLayout->setSpacing(3);
    auto* group = new QButtonGroup(this);
    for (const TypePreset& preset : kTypes) {
        auto* b = new QPushButton(swatchIcon(preset.swatch), tr(preset.name));
        b->setCheckable(true);
        b->setIconSize(QSize(12, 12));
        b->setMinimumHeight(30);
        b->setMinimumWidth(40);
        b->setStyleSheet(QStringLiteral("padding: 4px 3px;"));
        b->setToolTip(tr("Re = %L1, Smagorinsky %2").arg(preset.reynolds, 0, 'f', 0).arg(preset.smagorinsky));
        group->addButton(b);
        typeLayout->addWidget(b);
        m_typeButtons.push_back(b);
        connect(b, &QPushButton::clicked, this, [this, preset] {
            applyFlow([&](FlowSettings& f) {
                f.reynolds = preset.reynolds;
                f.smagorinsky = preset.smagorinsky;
                f.turbulence = preset.turbulence;
            });
        });
    }
    group->setExclusive(false);
    type->addWidget(typeRow);
    auto* create = new QPushButton(theme::icon(theme::Icon::Plus, Qt::white), tr("Create New Simulation"));
    create->setObjectName(QStringLiteral("primary"));
    create->setMinimumHeight(28);
    create->setToolTip(tr("Re-voxelize the collision objects, reset the flow and start the solver"));
    connect(create, &QPushButton::clicked, this, &FluidFlowPanel::createSimulationRequested);
    type->addWidget(create);
    m_domainLabel = valueLabel(tr("Wind Tunnel"));
    type->addRow(tr("Domain:"), m_domainLabel);
    m_emitterLabel = valueLabel(tr("Inlet"));
    type->addRow(tr("Emitter:"), m_emitterLabel);
    v->addWidget(type);

    // --- Vehicle ---------------------------------------------------------------------------
    auto* vehicle = new CollapsibleSection(tr("Vehicle Placement"));
    m_forward = new QComboBox;
    m_forward->addItems({ tr("+X forward"), tr("−X forward"), tr("+Z forward"), tr("−Z forward") });
    connect(m_forward, &QComboBox::currentIndexChanged, this, [this](int i) {
        applyPlacement([i](ModelPlacement& p) { p.forward = static_cast<ForwardAxis>(i); });
    });
    vehicle->addRow(tr("Nose axis"), m_forward);
    m_length = new ValueField(tr("Length"), 0.5, 20.0, 4.8, 2);
    m_length->setSuffix(QStringLiteral("m"));
    connect(m_length, &ValueField::valueCommitted, this, [this](double v) { applyPlacement([v](ModelPlacement& p) { p.lengthMeters = static_cast<float>(v); }); });
    vehicle->addWidget(m_length);
    m_yaw = new ValueField(tr("Yaw (cross-wind)"), -30.0, 30.0, 0.0, 1);
    m_yaw->setSuffix(QStringLiteral("°"));
    connect(m_yaw, &ValueField::valueCommitted, this, [this](double v) { applyPlacement([v](ModelPlacement& p) { p.yawDegrees = static_cast<float>(v); }); });
    vehicle->addWidget(m_yaw);
    v->addWidget(vehicle);

    // --- Collision objects -----------------------------------------------------------------
    auto* collision = new CollapsibleSection(tr("Collision Objects"));
    collision->addRow(tr("Collection:"), valueLabel(tr("Vehicle")));
    m_objects = new QListWidget;
    m_objects->setFixedHeight(118);
    m_objects->setToolTip(tr("Checked objects are voxelized as solid obstacles"));
    connect(m_objects, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!m_syncing) {
            m_doc->setObjectCollision(static_cast<std::size_t>(item->data(Qt::UserRole).toInt()), item->checkState() == Qt::Checked);
        }
    });
    connect(m_objects, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!m_syncing && item) {
            m_doc->setSelectedObject(item->data(Qt::UserRole).toInt());
        }
    });
    collision->addWidget(m_objects);
    auto* addCollision = new QPushButton(theme::icon(theme::Icon::Collision), tr("Toggle Collision on Active Object"));
    connect(addCollision, &QPushButton::clicked, this, [this] {
        const int sel = m_doc->selectedObject();
        if (sel >= 0 && sel < static_cast<int>(m_doc->objects().size())) {
            m_doc->setObjectCollision(static_cast<std::size_t>(sel), !m_doc->objects()[static_cast<std::size_t>(sel)].collision);
        }
    });
    collision->addWidget(addCollision);
    v->addWidget(collision);

    // --- Simulation settings -----------------------------------------------------------------
    auto* sim = new CollapsibleSection(tr("Simulation Settings"));
    m_speed = new ValueField(tr("Wind Speed"), 2.0, 90.0, 30.0, 1);
    m_speed->setSuffix(QStringLiteral("m/s"));
    connect(m_speed, &ValueField::valueChanged, this, [this](double v) { applyFlow([v](FlowSettings& f) { f.windSpeed = v; }); });
    sim->addWidget(m_speed);
    m_reynolds = new ValueField(tr("Reynolds"), 200.0, 200000.0, 20000.0, 0);
    m_reynolds->setLogarithmic(true);
    m_reynolds->setToolTip(tr("Simulated Reynolds number based on vehicle length (the sub-grid model covers the rest)"));
    connect(m_reynolds, &ValueField::valueChanged, this, [this](double v) { applyFlow([v](FlowSettings& f) { f.reynolds = v; }); });
    sim->addWidget(m_reynolds);
    m_steps = new ValueField(tr("Solver Iterations"), 1, 48, 8, 0);
    m_steps->setToolTip(tr("Lattice-Boltzmann steps per displayed frame"));
    connect(m_steps, &ValueField::valueChanged, this, [this](double v) { applyFlow([v](FlowSettings& f) { f.stepsPerFrame = static_cast<int>(v); }); });
    sim->addWidget(m_steps);
    m_resolution = new ValueField(tr("Resolution"), 64, 352, 160, 0);
    m_resolution->setStep(8);
    m_resolution->setSuffix(tr("cells"));
    m_resolution->setToolTip(tr("Cells along the tunnel; memory and time grow with the cube"));
    connect(m_resolution, &ValueField::valueCommitted, this, [this](double v) { applyTunnel([v](TunnelSettings& t) { t.resolution = static_cast<int>(v); }); });
    sim->addWidget(m_resolution);
    m_smagorinsky = new ValueField(tr("Smagorinsky Cs"), 0.0, 0.3, 0.16, 3);
    connect(m_smagorinsky, &ValueField::valueChanged, this, [this](double v) { applyFlow([v](FlowSettings& f) { f.smagorinsky = v; }); });
    sim->addWidget(m_smagorinsky);
    m_turbulence = new ValueField(tr("Inlet Turbulence"), 0.0, 0.1, 0.02, 3);
    connect(m_turbulence, &ValueField::valueChanged, this, [this](double v) { applyFlow([v](FlowSettings& f) { f.turbulence = v; }); });
    sim->addWidget(m_turbulence);
    m_lattice = new ValueField(tr("Lattice Velocity"), 0.03, 0.12, 0.08, 3);
    m_lattice->setToolTip(tr("Free-stream speed in lattice units (Mach number control): higher is faster but less accurate"));
    connect(m_lattice, &ValueField::valueChanged, this, [this](double v) { applyFlow([v](FlowSettings& f) { f.latticeVelocity = v; }); });
    sim->addWidget(m_lattice);
    m_rollingRoad = new QCheckBox(tr("Rolling road (moving floor)"));
    connect(m_rollingRoad, &QCheckBox::toggled, this, [this](bool on) { applyFlow([on](FlowSettings& f) { f.movingFloor = on; }); });
    sim->addWidget(m_rollingRoad);
    auto* tunnelLabel = new QLabel(tr("Tunnel size (× vehicle)"));
    tunnelLabel->setObjectName(QStringLiteral("sectionLabel"));
    sim->addWidget(tunnelLabel);
    m_upstream = new ValueField(tr("Upstream"), 0.3, 2.0, 0.8, 2);
    m_downstream = new ValueField(tr("Downstream"), 1.0, 5.0, 2.2, 2);
    m_height = new ValueField(tr("Size Y (height)"), 1.5, 5.0, 2.6, 2);
    m_width = new ValueField(tr("Size X (width)"), 1.5, 5.0, 2.6, 2);
    connect(m_upstream, &ValueField::valueCommitted, this, [this](double v) { applyTunnel([v](TunnelSettings& t) { t.upstream = static_cast<float>(v); }); });
    connect(m_downstream, &ValueField::valueCommitted, this, [this](double v) { applyTunnel([v](TunnelSettings& t) { t.downstream = static_cast<float>(v); }); });
    connect(m_height, &ValueField::valueCommitted, this, [this](double v) { applyTunnel([v](TunnelSettings& t) { t.heightFactor = static_cast<float>(v); }); });
    connect(m_width, &ValueField::valueCommitted, this, [this](double v) { applyTunnel([v](TunnelSettings& t) { t.widthFactor = static_cast<float>(v); }); });
    for (auto* f : { m_upstream, m_downstream, m_height, m_width }) {
        sim->addWidget(f);
    }
    v->addWidget(sim);

    // --- Emitter (streamline rake) -----------------------------------------------------------
    auto* emitter = new CollapsibleSection(tr("Emitter Object"));
    m_seedsX = new ValueField(tr("Vertices X"), 2, 64, 26, 0);
    m_seedsY = new ValueField(tr("Vertices Y"), 2, 40, 12, 0);
    m_rakeWidth = new ValueField(tr("Size X"), 0.2, 2.5, 1.1, 2);
    m_rakeHeight = new ValueField(tr("Size Y"), 0.2, 2.5, 1.15, 2);
    m_rakePosition = new ValueField(tr("Position"), -0.7, 0.4, -0.2, 2);
    m_maxPoints = new ValueField(tr("Max Length"), 50, 1200, 420, 0);
    m_particleCount = new ValueField(tr("Particles"), 1000, 200000, 40000, 0);
    m_particleCount->setLogarithmic(true);
    m_rakeWidth->setSuffix(QStringLiteral("× W"));
    m_rakeHeight->setSuffix(QStringLiteral("× H"));
    m_rakePosition->setSuffix(QStringLiteral("× L"));
    connect(m_seedsX, &ValueField::valueCommitted, this, [this](double v) { applyTracers([v](TracerSettings& t) { t.seedsAcross = static_cast<int>(v); }); });
    connect(m_seedsY, &ValueField::valueCommitted, this, [this](double v) { applyTracers([v](TracerSettings& t) { t.seedsVertical = static_cast<int>(v); }); });
    connect(m_rakeWidth, &ValueField::valueCommitted, this, [this](double v) { applyTracers([v](TracerSettings& t) { t.rakeWidth = v; }); });
    connect(m_rakeHeight, &ValueField::valueCommitted, this, [this](double v) { applyTracers([v](TracerSettings& t) { t.rakeHeight = v; }); });
    connect(m_rakePosition, &ValueField::valueCommitted, this, [this](double v) { applyTracers([v](TracerSettings& t) { t.rakePosition = v; }); });
    connect(m_maxPoints, &ValueField::valueCommitted, this, [this](double v) { applyTracers([v](TracerSettings& t) { t.maxPoints = static_cast<int>(v); }); });
    connect(m_particleCount, &ValueField::valueCommitted, this, [this](double v) { applyTracers([v](TracerSettings& t) { t.particleCount = static_cast<int>(v); }); });
    for (auto* f : { m_seedsX, m_seedsY, m_rakeWidth, m_rakeHeight, m_rakePosition, m_maxPoints, m_particleCount }) {
        emitter->addWidget(f);
    }
    v->addWidget(emitter);
    v->addStretch(1);
    return page;
}

QWidget* FluidFlowPanel::buildViewTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 6, 0);
    v->setSpacing(6);

    auto* shading = new CollapsibleSection(tr("Surface Shading"));
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(3);
    const QString names[] = { tr("Studio"), tr("Clay"), tr("Field") };
    for (int i = 0; i < 3; ++i) {
        auto* b = new QPushButton(names[i]);
        b->setCheckable(true);
        h->addWidget(b);
        m_shadingButtons.push_back(b);
        connect(b, &QPushButton::clicked, this, [this, i] { applyView([i](ViewSettings& s) { s.shading = static_cast<SurfaceShading>(i); }); });
    }
    shading->addWidget(row);
    m_field = new QComboBox;
    m_field->addItems({ tr("Velocity magnitude"), tr("Pressure coefficient"), tr("Vorticity") });
    connect(m_field, &QComboBox::currentIndexChanged, this, [this](int i) { applyView([i](ViewSettings& s) { s.field = static_cast<ScalarField>(i); }); });
    shading->addRow(tr("Scalar field"), m_field);
    v->addWidget(shading);

    auto* streamlines = new CollapsibleSection(tr("Streamlines"));
    m_showStreamlines = new QCheckBox(tr("Show streamlines"));
    connect(m_showStreamlines, &QCheckBox::toggled, this, [this](bool on) { applyTracers([on](TracerSettings& t) { t.streamlines = on; }); });
    m_lineWidth = new ValueField(tr("Width"), 0.5, 8.0, 2.2, 1);
    m_lineWidth->setSuffix(QStringLiteral("px"));
    connect(m_lineWidth, &ValueField::valueChanged, this, [this](double x) { applyView([x](ViewSettings& s) { s.streamlineWidth = x; }); });
    m_lineOpacity = new ValueField(tr("Opacity"), 0.05, 1.0, 0.85, 2);
    connect(m_lineOpacity, &ValueField::valueChanged, this, [this](double x) { applyView([x](ViewSettings& s) { s.streamlineOpacity = x; }); });
    m_animate = new QCheckBox(tr("Animated flow pulses"));
    connect(m_animate, &QCheckBox::toggled, this, [this](bool on) { applyView([on](ViewSettings& s) { s.animateStreamlines = on; }); });
    for (QWidget* w : std::initializer_list<QWidget*> { m_showStreamlines, m_lineWidth, m_lineOpacity, m_animate }) {
        streamlines->addWidget(w);
    }
    v->addWidget(streamlines);

    auto* particles = new CollapsibleSection(tr("Particles"));
    m_showParticles = new QCheckBox(tr("Show smoke particles"));
    connect(m_showParticles, &QCheckBox::toggled, this, [this](bool on) { applyTracers([on](TracerSettings& t) { t.particles = on; }); });
    m_particleSize = new ValueField(tr("Size"), 0.5, 8.0, 2.5, 1);
    m_particleSize->setSuffix(QStringLiteral("px"));
    connect(m_particleSize, &ValueField::valueChanged, this, [this](double x) { applyView([x](ViewSettings& s) { s.particleSize = x; }); });
    particles->addWidget(m_showParticles);
    particles->addWidget(m_particleSize);
    v->addWidget(particles);

    auto* slice = new CollapsibleSection(tr("Slice Plane (MPR)"));
    m_showSlice = new QCheckBox(tr("Show slice"));
    connect(m_showSlice, &QCheckBox::toggled, this, [this](bool on) { applyView([on](ViewSettings& s) { s.showSlice = on; }); });
    m_sliceAxis = new QComboBox;
    m_sliceAxis->addItems({ tr("Cross-section (X)"), tr("Horizontal (Y)"), tr("Longitudinal (Z)") });
    connect(m_sliceAxis, &QComboBox::currentIndexChanged, this, [this](int i) { applyView([i](ViewSettings& s) { s.sliceAxis = static_cast<SliceAxis>(i); }); });
    m_slicePosition = new ValueField(tr("Position"), 0.0, 1.0, 0.5, 3);
    connect(m_slicePosition, &ValueField::valueChanged, this, [this](double x) { applyView([x](ViewSettings& s) { s.slicePosition = x; }); });
    m_sliceOpacity = new ValueField(tr("Opacity"), 0.1, 1.0, 0.9, 2);
    connect(m_sliceOpacity, &ValueField::valueChanged, this, [this](double x) { applyView([x](ViewSettings& s) { s.sliceOpacity = x; }); });
    slice->addWidget(m_showSlice);
    slice->addRow(tr("Axis"), m_sliceAxis);
    slice->addWidget(m_slicePosition);
    slice->addWidget(m_sliceOpacity);
    v->addWidget(slice);

    auto* volume = new CollapsibleSection(tr("Volume Rendering"));
    m_showVolume = new QCheckBox(tr("Ray-cast the selected field"));
    connect(m_showVolume, &QCheckBox::toggled, this, [this](bool on) { applyView([on](ViewSettings& s) { s.showVolume = on; }); });
    m_volumeDensity = new ValueField(tr("Density"), 0.05, 10.0, 1.5, 2);
    m_volumeDensity->setLogarithmic(true);
    connect(m_volumeDensity, &ValueField::valueChanged, this, [this](double x) { applyView([x](ViewSettings& s) { s.volumeDensity = x; }); });
    volume->addWidget(m_showVolume);
    volume->addWidget(m_volumeDensity);
    v->addWidget(volume);

    auto* overlays = new CollapsibleSection(tr("Overlays"));
    m_showDomain = new QCheckBox(tr("Wind tunnel bounds"));
    m_showFloor = new QCheckBox(tr("Floor grid"));
    m_showRake = new QCheckBox(tr("Emitter rake"));
    connect(m_showDomain, &QCheckBox::toggled, this, [this](bool on) { applyView([on](ViewSettings& s) { s.showDomain = on; }); });
    connect(m_showFloor, &QCheckBox::toggled, this, [this](bool on) { applyView([on](ViewSettings& s) { s.showFloor = on; }); });
    connect(m_showRake, &QCheckBox::toggled, this, [this](bool on) { applyView([on](ViewSettings& s) { s.showRake = on; }); });
    overlays->addWidget(m_showDomain);
    overlays->addWidget(m_showFloor);
    overlays->addWidget(m_showRake);
    m_quality = new QComboBox;
    m_quality->addItems({ tr("Auto"), tr("High (full mesh, MSAA)"), tr("Performance (LOD, FXAA)") });
    m_quality->setToolTip(tr("Auto selects Performance on software OpenGL rasterizers"));
    connect(m_quality, &QComboBox::currentIndexChanged, this, [this](int i) { applyView([i](ViewSettings& s) { s.quality = static_cast<ViewportQuality>(i); }); });
    overlays->addRow(tr("Quality"), m_quality);
    v->addWidget(overlays);
    v->addStretch(1);
    return page;
}

void FluidFlowPanel::setDomainInfo(const DomainInfo& domain)
{
    const GridSpec& g = domain.grid;
    m_domainLabel->setText(tr("Wind Tunnel  %1×%2×%3").arg(g.nx).arg(g.ny).arg(g.nz));
    m_domainLabel->setToolTip(tr("%L1 cells, dx = %2 mm, %L3 solid").arg(g.cellCount()).arg(g.dx * 1000.0, 0, 'f', 0).arg(domain.solidCells));
}

void FluidFlowPanel::applyFlow(const std::function<void(FlowSettings&)>& edit)
{
    if (m_syncing) {
        return;
    }
    FlowSettings f = m_doc->flow();
    edit(f);
    m_doc->setFlow(f);
}

void FluidFlowPanel::applyTracers(const std::function<void(TracerSettings&)>& edit)
{
    if (m_syncing) {
        return;
    }
    TracerSettings t = m_doc->tracers();
    edit(t);
    m_doc->setTracers(t);
}

void FluidFlowPanel::applyView(const std::function<void(ViewSettings&)>& edit)
{
    if (m_syncing) {
        return;
    }
    ViewSettings v = m_doc->view();
    edit(v);
    m_doc->setView(v);
}

void FluidFlowPanel::applyTunnel(const std::function<void(TunnelSettings&)>& edit)
{
    if (m_syncing) {
        return;
    }
    TunnelSettings t = m_doc->tunnel();
    edit(t);
    m_doc->setTunnel(t);
}

void FluidFlowPanel::applyPlacement(const std::function<void(ModelPlacement&)>& edit)
{
    if (m_syncing) {
        return;
    }
    ModelPlacement p = m_doc->placement();
    edit(p);
    m_doc->setPlacement(p);
}

void FluidFlowPanel::updateTypeButtons()
{
    const double re = m_doc->flow().reynolds;
    for (int i = 0; i < m_typeButtons.size(); ++i) {
        m_typeButtons[i]->setChecked(std::abs(kTypes[i].reynolds - re) < 1.0);
    }
}

void FluidFlowPanel::syncFromDocument()
{
    m_syncing = true;
    const FlowSettings& f = m_doc->flow();
    const TracerSettings& t = m_doc->tracers();
    const ViewSettings& v = m_doc->view();
    const TunnelSettings& tun = m_doc->tunnel();
    const ModelPlacement& p = m_doc->placement();

    updateTypeButtons();
    m_emitterLabel->setText(tr("Inlet  ·  %1 m/s").arg(f.windSpeed, 0, 'f', 1));
    m_forward->setCurrentIndex(static_cast<int>(p.forward));
    m_length->setValue(p.lengthMeters);
    m_yaw->setValue(p.yawDegrees);
    m_speed->setValue(f.windSpeed);
    m_reynolds->setValue(f.reynolds);
    m_steps->setValue(f.stepsPerFrame);
    m_resolution->setValue(tun.resolution);
    m_smagorinsky->setValue(f.smagorinsky);
    m_turbulence->setValue(f.turbulence);
    m_lattice->setValue(f.latticeVelocity);
    m_rollingRoad->setChecked(f.movingFloor);
    m_upstream->setValue(tun.upstream);
    m_downstream->setValue(tun.downstream);
    m_height->setValue(tun.heightFactor);
    m_width->setValue(tun.widthFactor);
    m_seedsX->setValue(t.seedsAcross);
    m_seedsY->setValue(t.seedsVertical);
    m_rakeWidth->setValue(t.rakeWidth);
    m_rakeHeight->setValue(t.rakeHeight);
    m_rakePosition->setValue(t.rakePosition);
    m_maxPoints->setValue(t.maxPoints);
    m_particleCount->setValue(t.particleCount);

    for (int i = 0; i < m_shadingButtons.size(); ++i) {
        m_shadingButtons[i]->setChecked(static_cast<int>(v.shading) == i);
    }
    m_field->setCurrentIndex(static_cast<int>(v.field));
    m_showStreamlines->setChecked(t.streamlines);
    m_lineWidth->setValue(v.streamlineWidth);
    m_lineOpacity->setValue(v.streamlineOpacity);
    m_animate->setChecked(v.animateStreamlines);
    m_showParticles->setChecked(t.particles);
    m_particleSize->setValue(v.particleSize);
    m_showSlice->setChecked(v.showSlice);
    m_sliceAxis->setCurrentIndex(static_cast<int>(v.sliceAxis));
    m_slicePosition->setValue(v.slicePosition);
    m_sliceOpacity->setValue(v.sliceOpacity);
    m_showVolume->setChecked(v.showVolume);
    m_volumeDensity->setValue(v.volumeDensity);
    m_showDomain->setChecked(v.showDomain);
    m_showFloor->setChecked(v.showFloor);
    m_showRake->setChecked(v.showRake);
    m_quality->setCurrentIndex(static_cast<int>(v.quality));
    m_syncing = false;
}

void FluidFlowPanel::syncObjects()
{
    m_syncing = true;
    m_objects->clear();
    const auto& objects = m_doc->objects();
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const SceneObject& o = objects[i];
        auto* item = new QListWidgetItem(theme::icon(theme::Icon::Mesh, o.helper ? theme::kTextDim : theme::kAccentBright), o.name);
        item->setData(Qt::UserRole, static_cast<int>(i));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(o.collision ? Qt::Checked : Qt::Unchecked);
        if (o.helper) {
            item->setForeground(theme::kTextDim);
            item->setToolTip(tr("Helper object (studio prop or boolean cutter)"));
        }
        m_objects->addItem(item);
        if (static_cast<int>(i) == m_doc->selectedObject()) {
            m_objects->setCurrentItem(item);
        }
    }
    m_syncing = false;
}

} // namespace fluid::app
