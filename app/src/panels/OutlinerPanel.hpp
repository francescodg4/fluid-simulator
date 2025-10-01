#pragma once

#include "model/SceneDocument.hpp"

#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace fluid::app {

/**
 * Scene outliner: vehicle objects grouped in collections plus the wind-tunnel helpers, with
 * visibility (eye) and collision (shield) toggles, selection sync and a name filter.
 */
class OutlinerPanel : public QWidget {
    Q_OBJECT
public:
    explicit OutlinerPanel(SceneDocument* document, QWidget* parent = nullptr);

private:
    enum Role {
        KindRole = Qt::UserRole + 1,
        IndexRole,
    };
    enum Kind {
        ObjectItem = 1,
        TunnelItem = 2,
    };
    enum TunnelEntry {
        Domain,
        Emitter,
        Streamlines,
        Particles,
        Slice,
        Volume,
        Floor,
    };

    void rebuild();
    void refreshStates();
    void onItemClicked(QTreeWidgetItem* item, int column);
    bool tunnelVisible(int entry) const;
    void toggleTunnel(int entry);
    void applyFilter(const QString& text);

    SceneDocument* m_doc;
    QTreeWidget* m_tree = nullptr;
    QLineEdit* m_filter = nullptr;
    bool m_syncing = false;
};

} // namespace fluid::app
