#include "panels/OutlinerPanel.hpp"

#include "ui/Theme.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fluid::app {
namespace {
    constexpr int kNameColumn = 0;
    constexpr int kEyeColumn = 1;
    constexpr int kCollisionColumn = 2;
}

OutlinerPanel::OutlinerPanel(SceneDocument* document, QWidget* parent)
    : QWidget(parent)
    , m_doc(document)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("headerBar"));
    auto* h = new QHBoxLayout(header);
    h->setContentsMargins(8, 4, 8, 4);
    auto* icon = new QLabel(header);
    icon->setPixmap(theme::icon(theme::Icon::Scene, theme::kTextDim).pixmap(14, 14));
    h->addWidget(icon);
    m_filter = new QLineEdit(header);
    m_filter->setPlaceholderText(tr("Search objects…"));
    m_filter->setClearButtonEnabled(true);
    h->addWidget(m_filter, 1);
    layout->addWidget(header);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(true);
    m_tree->setIconSize(QSize(14, 14));
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(kEyeColumn, QHeaderView::Fixed);
    m_tree->header()->setSectionResizeMode(kCollisionColumn, QHeaderView::Fixed);
    m_tree->header()->resizeSection(kEyeColumn, 24);
    m_tree->header()->resizeSection(kCollisionColumn, 24);
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemClicked, this, &OutlinerPanel::onItemClicked);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!m_syncing && item && item->data(0, KindRole).toInt() == ObjectItem) {
            m_doc->setSelectedObject(item->data(0, IndexRole).toInt());
        }
    });
    connect(m_filter, &QLineEdit::textChanged, this, &OutlinerPanel::applyFilter);
    connect(m_doc, &SceneDocument::meshChanged, this, &OutlinerPanel::rebuild);
    connect(m_doc, &SceneDocument::objectsChanged, this, &OutlinerPanel::refreshStates);
    connect(m_doc, &SceneDocument::viewChanged, this, &OutlinerPanel::refreshStates);
    connect(m_doc, &SceneDocument::tracersChanged, this, &OutlinerPanel::refreshStates);
    connect(m_doc, &SceneDocument::selectionChanged, this, &OutlinerPanel::refreshStates);
    rebuild();
}

void OutlinerPanel::rebuild()
{
    m_syncing = true;
    m_tree->clear();
    auto* root = new QTreeWidgetItem(m_tree, { tr("Scene Collection") });
    root->setIcon(kNameColumn, theme::icon(theme::Icon::Scene, theme::kTextDim));

    auto* vehicle = new QTreeWidgetItem(root, { m_doc->modelName() });
    vehicle->setIcon(kNameColumn, theme::icon(theme::Icon::Collection, theme::kText));
    auto* helpers = new QTreeWidgetItem(root, { tr("Studio & Helpers") });
    helpers->setIcon(kNameColumn, theme::icon(theme::Icon::Collection, theme::kTextDim));

    const auto& objects = m_doc->objects();
    for (std::size_t i = 0; i < objects.size(); ++i) {
        auto* item = new QTreeWidgetItem(objects[i].helper ? helpers : vehicle, { objects[i].name });
        item->setData(0, KindRole, ObjectItem);
        item->setData(0, IndexRole, static_cast<int>(i));
        item->setIcon(kNameColumn, theme::icon(theme::Icon::Mesh, objects[i].helper ? theme::kTextDim : QColor(0xf0, 0xa0, 0x50)));
        item->setToolTip(kNameColumn, tr("%L1 triangles").arg(objects[i].triangles));
    }

    auto* tunnel = new QTreeWidgetItem(root, { tr("Wind Tunnel") });
    tunnel->setIcon(kNameColumn, theme::icon(theme::Icon::Collection, theme::kCyan));
    const std::pair<TunnelEntry, std::pair<QString, theme::Icon>> entries[] = {
        { Domain, { tr("Domain"), theme::Icon::Domain } },
        { Emitter, { tr("Inlet Emitter"), theme::Icon::Emitter } },
        { Streamlines, { tr("Streamlines"), theme::Icon::Streamlines } },
        { Particles, { tr("Smoke Particles"), theme::Icon::Particles } },
        { Slice, { tr("Slice Plane"), theme::Icon::Slice } },
        { Volume, { tr("Volume"), theme::Icon::Volume } },
        { Floor, { tr("Floor"), theme::Icon::Grid } },
    };
    for (const auto& [entry, spec] : entries) {
        auto* item = new QTreeWidgetItem(tunnel, { spec.first });
        item->setData(0, KindRole, TunnelItem);
        item->setData(0, IndexRole, static_cast<int>(entry));
        item->setIcon(kNameColumn, theme::icon(spec.second, theme::kCyan));
    }

    m_tree->expandItem(root);
    m_tree->expandItem(vehicle);
    m_tree->expandItem(tunnel);
    m_syncing = false;
    refreshStates();
    applyFilter(m_filter->text());
}

bool OutlinerPanel::tunnelVisible(int entry) const
{
    const ViewSettings& v = m_doc->view();
    const TracerSettings& t = m_doc->tracers();
    switch (entry) {
    case Domain:
        return v.showDomain;
    case Emitter:
        return v.showRake;
    case Streamlines:
        return t.streamlines;
    case Particles:
        return t.particles;
    case Slice:
        return v.showSlice;
    case Volume:
        return v.showVolume;
    case Floor:
        return v.showFloor;
    }
    return false;
}

void OutlinerPanel::toggleTunnel(int entry)
{
    ViewSettings v = m_doc->view();
    TracerSettings t = m_doc->tracers();
    switch (entry) {
    case Domain:
        v.showDomain = !v.showDomain;
        break;
    case Emitter:
        v.showRake = !v.showRake;
        break;
    case Streamlines:
        t.streamlines = !t.streamlines;
        break;
    case Particles:
        t.particles = !t.particles;
        break;
    case Slice:
        v.showSlice = !v.showSlice;
        break;
    case Volume:
        v.showVolume = !v.showVolume;
        break;
    case Floor:
        v.showFloor = !v.showFloor;
        break;
    }
    m_doc->setView(v);
    m_doc->setTracers(t);
}

void OutlinerPanel::refreshStates()
{
    m_syncing = true;
    const QIcon eye = theme::icon(theme::Icon::Eye, theme::kText);
    const QIcon eyeOff = theme::icon(theme::Icon::EyeOff, QColor(0x4a, 0x55, 0x6a));
    const QIcon shield = theme::icon(theme::Icon::Collision, theme::kAccentBright);
    const QIcon shieldOff = theme::icon(theme::Icon::Collision, QColor(0x3a, 0x44, 0x58));
    const auto& objects = m_doc->objects();
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        const int kind = item->data(0, KindRole).toInt();
        const int index = item->data(0, IndexRole).toInt();
        if (kind == ObjectItem && index < static_cast<int>(objects.size())) {
            const SceneObject& o = objects[static_cast<std::size_t>(index)];
            item->setIcon(kEyeColumn, o.visible ? eye : eyeOff);
            item->setIcon(kCollisionColumn, o.collision ? shield : shieldOff);
            item->setToolTip(kCollisionColumn, o.collision ? tr("Collision: on") : tr("Collision: off"));
            item->setForeground(kNameColumn, o.visible ? theme::kText : theme::kTextDim);
            if (index == m_doc->selectedObject()) {
                m_tree->setCurrentItem(item);
            }
        } else if (kind == TunnelItem) {
            item->setIcon(kEyeColumn, tunnelVisible(index) ? eye : eyeOff);
        }
    }
    m_syncing = false;
}

void OutlinerPanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    const int kind = item->data(0, KindRole).toInt();
    const int index = item->data(0, IndexRole).toInt();
    if (kind == ObjectItem && column == kEyeColumn) {
        m_doc->setObjectVisible(static_cast<std::size_t>(index), !m_doc->objects()[static_cast<std::size_t>(index)].visible);
    } else if (kind == ObjectItem && column == kCollisionColumn) {
        m_doc->setObjectCollision(static_cast<std::size_t>(index), !m_doc->objects()[static_cast<std::size_t>(index)].collision);
    } else if (kind == TunnelItem && column == kEyeColumn) {
        toggleTunnel(index);
    }
}

void OutlinerPanel::applyFilter(const QString& text)
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        if (item->data(0, KindRole).toInt() == ObjectItem) {
            item->setHidden(!text.isEmpty() && !item->text(kNameColumn).contains(text, Qt::CaseInsensitive));
        }
    }
}

} // namespace fluid::app
