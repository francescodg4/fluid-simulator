#include "panels/PropertiesPanel.hpp"

#include "ui/Theme.hpp"
#include "widgets/CollapsibleSection.hpp"
#include "widgets/TransferFunctionEditor.hpp"
#include "widgets/ValueField.hpp"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fluid::app {

/** Cd / Cl convergence plot. */
class ForceChartWidget : public QWidget {
public:
    explicit ForceChartWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(150);
    }
    void setHistory(const QVector<ForceSample>& history)
    {
        m_history = history;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(theme::kBorder, 1));
        p.setBrush(theme::kPanelAlt);
        p.drawRoundedRect(r, 6, 6);
        const QRectF plot = r.adjusted(36, 12, -10, -20);
        QFont f = font();
        f.setPointSizeF(7.2);
        p.setFont(f);
        if (m_history.size() < 2) {
            p.setPen(theme::kTextDim);
            p.drawText(r, Qt::AlignCenter, tr("Run the solver to see force convergence"));
            return;
        }
        double lo = 1e30, hi = -1e30;
        const int first = m_history.size() > 20 ? m_history.size() / 5 : 0;
        for (int i = first; i < m_history.size(); ++i) {
            lo = std::min({ lo, m_history[i].cd, m_history[i].cl });
            hi = std::max({ hi, m_history[i].cd, m_history[i].cl });
        }
        const double pad = std::max(0.05, (hi - lo) * 0.12);
        lo -= pad;
        hi += pad;
        const double s0 = static_cast<double>(m_history[first].step);
        const double s1 = std::max(s0 + 1, static_cast<double>(m_history.back().step));
        auto map = [&](double s, double v) {
            return QPointF(plot.left() + (s - s0) / (s1 - s0) * plot.width(), plot.bottom() - (v - lo) / (hi - lo) * plot.height());
        };
        for (int i = 0; i <= 4; ++i) {
            const double v = lo + (hi - lo) * i / 4.0;
            const QPointF a = map(s0, v);
            p.setPen(QPen(QColor(255, 255, 255, 16), 1));
            p.drawLine(a, QPointF(plot.right(), a.y()));
            p.setPen(theme::kTextDim);
            p.drawText(QRectF(r.left() + 2, a.y() - 7, 30, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'f', 2));
        }
        p.drawText(QRectF(plot.left(), plot.bottom() + 3, plot.width(), 14), Qt::AlignLeft, QString::number(s0, 'f', 0));
        p.drawText(QRectF(plot.left(), plot.bottom() + 3, plot.width(), 14), Qt::AlignRight, QString::number(s1, 'f', 0));
        p.drawText(QRectF(plot.left(), plot.bottom() + 3, plot.width(), 14), Qt::AlignHCenter, tr("iteration"));
        QPainterPath cd, cl;
        for (int i = first; i < m_history.size(); ++i) {
            const QPointF a = map(static_cast<double>(m_history[i].step), m_history[i].cd);
            const QPointF b = map(static_cast<double>(m_history[i].step), m_history[i].cl);
            i == first ? cd.moveTo(a) : cd.lineTo(a);
            i == first ? cl.moveTo(b) : cl.lineTo(b);
        }
        p.setClipRect(plot.adjusted(-2, -2, 2, 2));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0xf0, 0x9a, 0x4a), 1.4));
        p.drawPath(cl);
        p.setPen(QPen(theme::kAccentBright, 1.8));
        p.drawPath(cd);
        p.setClipping(false);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(theme::kAccentBright);
        p.drawText(QPointF(plot.right() - 62, plot.top() + 10), QStringLiteral("Cd"));
        p.setPen(QColor(0xf0, 0x9a, 0x4a));
        p.drawText(QPointF(plot.right() - 34, plot.top() + 10), QStringLiteral("Cl"));
    }

private:
    QVector<ForceSample> m_history;
};

namespace {

    QScrollArea* scrollable(QWidget* content)
    {
        auto* area = new QScrollArea;
        area->setWidget(content);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        return area;
    }

    constexpr double kFieldLimits[kScalarFieldCount][2] = { { 0.0, 3.0 }, { -6.0, 3.0 }, { 0.0, 400.0 } };

} // namespace

PropertiesPanel::PropertiesPanel(SceneDocument* document, SimulationController* simulation, QWidget* parent)
    : QWidget(parent)
    , m_doc(document)
    , m_simulation(simulation)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("sideTabs"));
    m_tabs->setTabPosition(QTabWidget::West);
    m_tabs->setDocumentMode(true);
    m_tabs->tabBar()->setIconSize(QSize(18, 18));
    m_tabs->addTab(scrollable(buildVisualizationTab()), theme::icon(theme::Icon::Palette), QString());
    m_tabs->setTabToolTip(0, tr("Visualization: scalar field & transfer function"));
    m_tabs->addTab(scrollable(buildAnalysisTab()), theme::icon(theme::Icon::Chart), QString());
    m_tabs->setTabToolTip(1, tr("Aerodynamic analysis"));
    m_tabs->addTab(scrollable(buildReportTab()), theme::icon(theme::Icon::Report), QString());
    m_tabs->setTabToolTip(2, tr("PDF report"));
    layout->addWidget(m_tabs);

    connect(m_doc, &SceneDocument::viewChanged, this, &PropertiesPanel::syncFromDocument);
    connect(m_doc, &SceneDocument::transferFunctionChanged, this, &PropertiesPanel::syncFromDocument);
    connect(m_doc, &SceneDocument::meshChanged, this, [this] {
        m_title->setText(tr("Aerodynamic Analysis — %1").arg(m_doc->modelName()));
    });
    connect(m_simulation, &SimulationController::snapshotReady, this, &PropertiesPanel::onSnapshot);
    connect(m_simulation, &SimulationController::historyChanged, this, [this] { m_chart->setHistory(m_simulation->history()); });
    m_histogramClock.start();
    m_metricsClock.start();
    syncFromDocument();
}

QWidget* PropertiesPanel::buildVisualizationTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(6, 6, 8, 6);
    v->setSpacing(6);

    auto* field = new CollapsibleSection(tr("Scalar Field"));
    m_field = new QComboBox;
    m_field->addItems({ tr("Velocity magnitude"), tr("Pressure coefficient Cp"), tr("Vorticity magnitude") });
    connect(m_field, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_syncing) {
            return;
        }
        ViewSettings s = m_doc->view();
        s.field = static_cast<ScalarField>(i);
        m_doc->setView(s);
    });
    field->addRow(tr("Field"), m_field);
    m_min = new ValueField(tr("Min"), 0, 1, 0, 2);
    m_max = new ValueField(tr("Max"), 0, 1, 1, 2);
    m_min->setSliderVisible(false);
    m_max->setSliderVisible(false);
    auto setRange = [this](bool isMin, double value) {
        if (m_syncing) {
            return;
        }
        ViewSettings s = m_doc->view();
        ScalarRange& r = s.ranges[static_cast<int>(s.field)];
        (isMin ? r.min : r.max) = static_cast<float>(value);
        m_doc->setView(s);
    };
    connect(m_min, &ValueField::valueChanged, this, [setRange](double x) { setRange(true, x); });
    connect(m_max, &ValueField::valueChanged, this, [setRange](double x) { setRange(false, x); });
    field->addWidget(m_min);
    field->addWidget(m_max);
    auto* autoButton = new QPushButton(tr("Auto Range (1–99 %)"));
    connect(autoButton, &QPushButton::clicked, this, &PropertiesPanel::autoRange);
    field->addWidget(autoButton);
    v->addWidget(field);

    auto* tfSection = new CollapsibleSection(tr("Transfer Function"));
    m_colormap = new QComboBox;
    for (const auto& name : TransferFunction::colormapNames) {
        m_colormap->addItem(QString::fromLatin1(name.data(), static_cast<int>(name.size())));
    }
    connect(m_colormap, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_syncing) {
            return;
        }
        TransferFunction tf = m_doc->transferFunction();
        tf.setColormap(static_cast<Colormap>(i));
        m_doc->setTransferFunction(tf);
    });
    tfSection->addRow(tr("Colormap"), m_colormap);
    m_editor = new TransferFunctionEditor;
    m_editor->setMinimumHeight(150);
    connect(m_editor, &TransferFunctionEditor::transferFunctionChanged, this, [this](const TransferFunction& tf) {
        m_syncing = true;
        m_doc->setTransferFunction(tf);
        m_syncing = false;
    });
    tfSection->addWidget(m_editor);
    auto* presets = new QWidget;
    auto* h = new QHBoxLayout(presets);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(3);
    const QString names[] = { tr("Ramp"), tr("Peaks"), tr("Band"), tr("Flat") };
    for (int i = 0; i < 4; ++i) {
        auto* b = new QPushButton(names[i]);
        b->setToolTip(tr("Opacity preset"));
        connect(b, &QPushButton::clicked, this, [this, i] { applyOpacityPreset(i); });
        h->addWidget(b);
    }
    tfSection->addWidget(presets);
    auto* hint = new QLabel(tr("Opacity drives volume rendering. Drag points, double-click to add, right-click to remove."));
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("dimLabel"));
    tfSection->addWidget(hint);
    v->addWidget(tfSection);
    v->addStretch(1);
    return page;
}

QWidget* PropertiesPanel::buildAnalysisTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(6, 6, 8, 6);
    v->setSpacing(6);

    auto* coeffs = new CollapsibleSection(tr("Aerodynamic Coefficients"));
    auto* gridWidget = new QWidget;
    auto* grid = new QGridLayout(gridWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(3);
    const QStringList keys { tr("Drag Cd"), tr("Mean Cd (last 25%)"), tr("Lift Cl"), tr("Side Cs"), tr("Drag force"), tr("Lift force"), tr("Drag power"),
        tr("Frontal area"), tr("Re (simulated)"), tr("Re (air)"), tr("Grid"), tr("Solid cells"), tr("Throughput"), tr("Simulated time") };
    for (int i = 0; i < keys.size(); ++i) {
        auto* name = new QLabel(keys[i]);
        name->setObjectName(QStringLiteral("dimLabel"));
        auto* value = new QLabel(QStringLiteral("—"));
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setStyleSheet(QStringLiteral("font-weight: 600;"));
        grid->addWidget(name, i, 0);
        grid->addWidget(value, i, 1);
        m_metrics.insert(keys[i], value);
    }
    coeffs->addWidget(gridWidget);
    v->addWidget(coeffs);

    auto* convergence = new CollapsibleSection(tr("Convergence"));
    m_chart = new ForceChartWidget;
    convergence->addWidget(m_chart);
    v->addWidget(convergence);
    v->addStretch(1);
    return page;
}

QWidget* PropertiesPanel::buildReportTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(6, 6, 8, 6);
    v->setSpacing(6);
    auto* section = new CollapsibleSection(tr("PDF Report"));
    m_title = new QLineEdit(tr("Aerodynamic Analysis"));
    section->addRow(tr("Title"), m_title);
    m_author = new QLineEdit(qEnvironmentVariable("USER", tr("Engineering")));
    section->addRow(tr("Author"), m_author);
    m_notes = new QPlainTextEdit;
    m_notes->setPlaceholderText(tr("Notes, assumptions, conclusions…"));
    m_notes->setFixedHeight(80);
    section->addWidget(m_notes);
    auto* previewButton = new QPushButton(theme::icon(theme::Icon::Report), tr("Update Preview"));
    connect(previewButton, &QPushButton::clicked, this, &PropertiesPanel::previewReportRequested);
    section->addWidget(previewButton);
    m_preview = new QLabel;
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(120);
    m_preview->setStyleSheet(QStringLiteral("background: %1; border-radius: 6px;").arg(theme::kPanelAlt.name()));
    section->addWidget(m_preview);
    auto* exportButton = new QPushButton(theme::icon(theme::Icon::Report, Qt::white), tr("Export PDF Report…"));
    exportButton->setObjectName(QStringLiteral("primary"));
    exportButton->setMinimumHeight(28);
    connect(exportButton, &QPushButton::clicked, this, &PropertiesPanel::exportReportRequested);
    section->addWidget(exportButton);
    v->addWidget(section);
    v->addStretch(1);
    return page;
}

QString PropertiesPanel::reportTitle() const
{
    return m_title->text();
}

QString PropertiesPanel::reportAuthor() const
{
    return m_author->text();
}

QString PropertiesPanel::reportNotes() const
{
    return m_notes->toPlainText();
}

void PropertiesPanel::setReportPreview(const QImage& image)
{
    const int w = std::max(120, m_preview->width() - 12);
    m_preview->setPixmap(QPixmap::fromImage(image.scaledToWidth(w, Qt::SmoothTransformation)));
}

void PropertiesPanel::showTab(int index)
{
    m_tabs->setCurrentIndex(index);
}

void PropertiesPanel::syncFromDocument()
{
    if (m_syncing) {
        return;
    }
    m_syncing = true;
    const ViewSettings& v = m_doc->view();
    const int f = static_cast<int>(v.field);
    m_field->setCurrentIndex(f);
    const int decimals = v.field == ScalarField::Vorticity ? 1 : 2;
    for (ValueField* vf : { m_min, m_max }) {
        vf->setRange(kFieldLimits[f][0], kFieldLimits[f][1]);
        vf->setDecimals(decimals);
    }
    m_min->setValue(v.range().min);
    m_max->setValue(v.range().max);
    m_colormap->setCurrentIndex(static_cast<int>(m_doc->transferFunction().colormap()));
    m_editor->setTransferFunction(m_doc->transferFunction());
    m_editor->setRangeLabels(QString::number(v.range().min, 'f', decimals), QString::number(v.range().max, 'f', decimals));
    m_syncing = false;
    updateHistogram();
}

void PropertiesPanel::applyOpacityPreset(int preset)
{
    TransferFunction tf = m_doc->transferFunction();
    switch (preset) {
    case 0:
        tf.setOpacityPoints({ { 0.0f, 0.0f }, { 1.0f, 0.8f } });
        break;
    case 1:
        tf.setOpacityPoints({ { 0.0f, 0.0f }, { 0.55f, 0.0f }, { 0.8f, 0.3f }, { 1.0f, 0.95f } });
        break;
    case 2:
        tf.setOpacityPoints({ { 0.0f, 0.0f }, { 0.3f, 0.0f }, { 0.5f, 0.6f }, { 0.7f, 0.0f }, { 1.0f, 0.0f } });
        break;
    default:
        tf.setOpacityPoints({ { 0.0f, 0.15f }, { 1.0f, 0.15f } });
        break;
    }
    m_doc->setTransferFunction(tf);
}

void PropertiesPanel::autoRange()
{
    const SnapshotPtr& snap = m_simulation->latestSnapshot();
    if (!snap) {
        return;
    }
    const int channel = static_cast<int>(m_doc->view().field);
    std::vector<float> values;
    values.reserve(snap->volume.size() / 4);
    for (std::size_t n = 0; n < snap->volume.size() / 4; ++n) {
        if (snap->volume[n * 4 + 3] < 0.5f) {
            values.push_back(snap->volume[n * 4 + static_cast<std::size_t>(channel)]);
        }
    }
    if (values.size() < 10) {
        return;
    }
    auto percentile = [&](double q) {
        auto it = values.begin() + static_cast<std::ptrdiff_t>(q * static_cast<double>(values.size() - 1));
        std::nth_element(values.begin(), it, values.end());
        return *it;
    };
    ViewSettings s = m_doc->view();
    ScalarRange& r = s.ranges[channel];
    r.min = percentile(0.01);
    r.max = percentile(0.99);
    if (s.field == ScalarField::Vorticity || s.field == ScalarField::Speed) {
        r.min = std::max(0.0f, r.min);
    }
    if (r.max <= r.min) {
        r.max = r.min + 1e-3f;
    }
    m_doc->setView(s);
}

void PropertiesPanel::updateHistogram()
{
    const SnapshotPtr& snap = m_simulation->latestSnapshot();
    if (!snap) {
        return;
    }
    const ViewSettings& v = m_doc->view();
    const int channel = static_cast<int>(v.field);
    const ScalarRange r = v.range();
    QVector<float> bins(64, 0.0f);
    const float scale = static_cast<float>(bins.size()) / std::max(1e-6f, r.max - r.min);
    for (std::size_t n = 0; n < snap->volume.size() / 4; ++n) {
        if (snap->volume[n * 4 + 3] > 0.5f) {
            continue;
        }
        const int b = static_cast<int>((snap->volume[n * 4 + static_cast<std::size_t>(channel)] - r.min) * scale);
        if (b >= 0 && b < bins.size()) {
            bins[b] += 1.0f;
        }
    }
    m_editor->setHistogram(bins);
}

void PropertiesPanel::onSnapshot(const SnapshotPtr& snapshot)
{
    if (m_histogramClock.elapsed() > 500) {
        m_histogramClock.restart();
        updateHistogram();
    }
    if (m_metricsClock.elapsed() < 200) {
        return;
    }
    m_metricsClock.restart();
    const SolverStats& s = snapshot->stats;
    const DomainInfo& d = m_simulation->domain();
    const auto& history = m_simulation->history();
    double meanCd = s.cd;
    if (history.size() > 8) {
        const int from = history.size() * 3 / 4;
        double sum = 0.0;
        for (int i = from; i < history.size(); ++i) {
            sum += history[i].cd;
        }
        meanCd = sum / (history.size() - from);
    }
    auto set = [this](const QString& key, const QString& value) {
        if (auto* l = m_metrics.value(key)) {
            l->setText(value);
        }
    };
    set(tr("Drag Cd"), QString::number(s.cd, 'f', 3));
    set(tr("Mean Cd (last 25%)"), QString::number(meanCd, 'f', 3));
    set(tr("Lift Cl"), QString::number(s.cl, 'f', 3));
    set(tr("Side Cs"), QString::number(s.cs, 'f', 3));
    set(tr("Drag force"), QStringLiteral("%L1 N").arg(s.dragNewton, 0, 'f', 0));
    set(tr("Lift force"), QStringLiteral("%L1 N").arg(s.liftNewton, 0, 'f', 0));
    set(tr("Drag power"), QStringLiteral("%1 kW").arg(s.dragNewton * m_doc->flow().windSpeed / 1000.0, 0, 'f', 1));
    set(tr("Frontal area"), QStringLiteral("%1 m²").arg(d.frontalArea, 0, 'f', 2));
    set(tr("Re (simulated)"), QStringLiteral("%L1").arg(m_doc->flow().reynolds, 0, 'f', 0));
    set(tr("Re (air)"), QString::number(s.reynoldsAir, 'e', 2));
    set(tr("Grid"), QStringLiteral("%1×%2×%3").arg(d.grid.nx).arg(d.grid.ny).arg(d.grid.nz));
    set(tr("Solid cells"), QStringLiteral("%L1").arg(d.solidCells));
    set(tr("Throughput"), QStringLiteral("%1 MLUPS").arg(s.mlups, 0, 'f', 0));
    set(tr("Simulated time"), QStringLiteral("%1 s").arg(s.physicalTime, 0, 'f', 3));
}

} // namespace fluid::app
