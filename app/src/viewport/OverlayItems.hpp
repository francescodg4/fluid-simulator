#pragma once

#include "simulation/SimulationController.hpp"
#include "simulation/SimulationTypes.hpp"
#include "ui/Theme.hpp"

#include <fluid/TransferFunction.hpp>

#include <QGraphicsObject>
#include <QMatrix4x4>
#include <QVector3D>

namespace fluid::app {

/** Base for flat overlay items: no caching of transforms, pixel aligned. */
class OverlayItem : public QGraphicsObject {
    Q_OBJECT
public:
    explicit OverlayItem(QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override { return QRectF(QPointF(0, 0), m_size); }
    void setSize(const QSizeF& size);
    QSizeF size() const { return m_size; }

protected:
    static void drawCard(QPainter* p, const QRectF& rect, qreal radius = 8.0, qreal alpha = 0.82);

private:
    QSizeF m_size;
};

/**
 * Blender-style orientation gizmo: shows the world axes, click an axis to align the view to it,
 * drag to orbit.
 */
class NavigationGizmoItem : public OverlayItem {
    Q_OBJECT
public:
    explicit NavigationGizmoItem(QGraphicsItem* parent = nullptr);
    void setViewMatrix(const QMatrix4x4& view);
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    void axisClicked(const QVector3D& axis);
    void orbitDragged(const QPointF& delta);

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    struct Bubble {
        QVector3D axis;
        QPointF center;
        float depth;
        QColor color;
        QString label;
        bool positive;
    };
    std::vector<Bubble> bubbles() const;
    int bubbleAt(const QPointF& pos) const;

    QMatrix4x4 m_view;
    bool m_hover = false;
    int m_hoverBubble = -1;
    bool m_dragging = false;
    QPointF m_pressPos;
    QPointF m_lastPos;
};

/** Round (or rounded-square) icon button, optionally checkable. */
class IconButtonItem : public OverlayItem {
    Q_OBJECT
public:
    IconButtonItem(theme::Icon icon, const QString& tooltip, QGraphicsItem* parent = nullptr);
    void setCheckable(bool checkable) { m_checkable = checkable; }
    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }
    void setIcon(theme::Icon icon);
    void setRound(bool round) { m_round = round; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    void clicked();
    void toggled(bool checked);

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    theme::Icon m_icon;
    bool m_checkable = false;
    bool m_checked = false;
    bool m_hover = false;
    bool m_pressed = false;
    bool m_round = true;
};

enum class ViewportTool {
    Select,
    Orbit,
    Pan,
    Zoom,
    Probe,
};

/** Vertical tool shelf on the left edge of the viewport (Blender's T-panel). */
class ToolShelfItem : public OverlayItem {
    Q_OBJECT
public:
    explicit ToolShelfItem(QGraphicsItem* parent = nullptr);
    void setTool(ViewportTool tool);
    ViewportTool tool() const { return m_tool; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    void toolChanged(fluid::app::ViewportTool tool);

private:
    std::vector<std::pair<ViewportTool, IconButtonItem*>> m_buttons;
    ViewportTool m_tool = ViewportTool::Select;
};

/** Multi-line text overlay with a subtle shadow (view name, active object, solver status). */
class HudItem : public OverlayItem {
    Q_OBJECT
public:
    explicit HudItem(QGraphicsItem* parent = nullptr);
    void setLines(const QStringList& lines);
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    QStringList m_lines;
};

/** Colour legend of the current transfer function and scalar range. */
class ColorLegendItem : public OverlayItem {
    Q_OBJECT
public:
    explicit ColorLegendItem(QGraphicsItem* parent = nullptr);
    void setLegend(const TransferFunction& tf, const ScalarRange& range, const QString& title, const QString& unit, double unitScale);
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    QVector<QColor> m_colors;
    ScalarRange m_range;
    QString m_title;
    QString m_unit;
    double m_unitScale = 1.0;
};

/** Live aerodynamic coefficients with a convergence sparkline. */
class ForceCardItem : public OverlayItem {
    Q_OBJECT
public:
    explicit ForceCardItem(QGraphicsItem* parent = nullptr);
    void setData(const SolverStats& stats, const QVector<ForceSample>& history, bool running);
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    SolverStats m_stats;
    QVector<ForceSample> m_history;
    bool m_running = false;
};

/** Callout showing probed field values. */
class ProbeItem : public OverlayItem {
    Q_OBJECT
public:
    explicit ProbeItem(QGraphicsItem* parent = nullptr);
    void setProbe(const QPointF& anchor, const QStringList& lines);
    QRectF boundingRect() const override { return QRectF(QPointF(-20, 0), size() + QSizeF(20, 16)); }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    QStringList m_lines;
};

/** Centered progress card used while loading the model or building the domain. */
class LoadingItem : public OverlayItem {
    Q_OBJECT
public:
    explicit LoadingItem(QGraphicsItem* parent = nullptr);
    void setState(const QString& title, const QString& detail, double progress); ///< progress < 0: indeterminate
    void advanceAnimation(double seconds) { m_phase = seconds; update(); }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    QString m_title;
    QString m_detail;
    double m_progress = -1.0;
    double m_phase = 0.0;
};

} // namespace fluid::app
