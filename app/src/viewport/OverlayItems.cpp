#include "viewport/OverlayItems.hpp"

#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>

namespace fluid::app {
namespace {

    QFont uiFont(qreal pt, int weight = QFont::Normal)
    {
        QFont f;
        f.setPointSizeF(pt);
        f.setWeight(static_cast<QFont::Weight>(weight));
        return f;
    }

    void shadowText(QPainter* p, const QPointF& pos, const QString& text, const QColor& color)
    {
        p->setPen(QColor(0, 0, 0, 170));
        p->drawText(pos + QPointF(1, 1), text);
        p->setPen(color);
        p->drawText(pos, text);
    }

} // namespace

// ---------------------------------------------------------------------------------------------
OverlayItem::OverlayItem(QGraphicsItem* parent)
    : QGraphicsObject(parent)
{
    setAcceptHoverEvents(true);
}

void OverlayItem::setSize(const QSizeF& size)
{
    if (size == m_size) {
        return;
    }
    prepareGeometryChange();
    m_size = size;
}

void OverlayItem::drawCard(QPainter* p, const QRectF& rect, qreal radius, qreal alpha)
{
    p->setRenderHint(QPainter::Antialiasing);
    QColor fill = theme::kPanel;
    fill.setAlphaF(alpha);
    p->setPen(QPen(QColor(0x2c, 0x3b, 0x58, 200), 1.0));
    p->setBrush(fill);
    p->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

// ---------------------------------------------------------------------------------------------
NavigationGizmoItem::NavigationGizmoItem(QGraphicsItem* parent)
    : OverlayItem(parent)
{
    setSize({ 104, 104 });
    setCursor(Qt::PointingHandCursor);
}

void NavigationGizmoItem::setViewMatrix(const QMatrix4x4& view)
{
    m_view = view;
    update();
}

std::vector<NavigationGizmoItem::Bubble> NavigationGizmoItem::bubbles() const
{
    const QPointF c = boundingRect().center();
    const qreal radius = 36.0;
    const struct {
        QVector3D axis;
        QColor color;
        const char* label;
    } axes[] = { { { 1, 0, 0 }, theme::kAxisX, "X" }, { { 0, 1, 0 }, theme::kAxisY, "Y" }, { { 0, 0, 1 }, theme::kAxisZ, "Z" } };

    std::vector<Bubble> out;
    for (const auto& a : axes) {
        for (const bool positive : { true, false }) {
            const QVector3D axis = positive ? a.axis : -a.axis;
            const QVector3D v = m_view.mapVector(axis);
            out.push_back({ axis, c + QPointF(v.x(), -v.y()) * radius, v.z(), a.color, positive ? QString::fromLatin1(a.label) : QString(), positive });
        }
    }
    // Painter's algorithm: far bubbles first (view space looks down -Z).
    std::sort(out.begin(), out.end(), [](const Bubble& a, const Bubble& b) { return a.depth < b.depth; });
    return out;
}

int NavigationGizmoItem::bubbleAt(const QPointF& pos) const
{
    const auto list = bubbles();
    for (int i = static_cast<int>(list.size()) - 1; i >= 0; --i) {
        if (QLineF(pos, list[static_cast<std::size_t>(i)].center).length() <= 10.0) {
            return i;
        }
    }
    return -1;
}

void NavigationGizmoItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = boundingRect();
    if (m_hover || m_dragging) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 22));
        p->drawEllipse(r.adjusted(4, 4, -4, -4));
    }
    const QPointF c = r.center();
    const auto list = bubbles();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Bubble& b = list[i];
        const bool hot = static_cast<int>(i) == m_hoverBubble;
        if (b.positive) {
            p->setPen(QPen(b.color, 2.2, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(c, b.center);
            p->setPen(hot ? QPen(Qt::white, 1.5) : Qt::NoPen);
            p->setBrush(b.color);
            p->drawEllipse(b.center, 9.0, 9.0);
            p->setFont(uiFont(7.5, QFont::Bold));
            p->setPen(QColor(10, 14, 22));
            p->drawText(QRectF(b.center.x() - 9, b.center.y() - 9, 18, 18), Qt::AlignCenter, b.label);
        } else {
            QColor fill = b.color.darker(260);
            fill.setAlpha(220);
            p->setPen(QPen(hot ? Qt::white : b.color.darker(130), 1.4));
            p->setBrush(fill);
            p->drawEllipse(b.center, 7.5, 7.5);
        }
    }
}

void NavigationGizmoItem::hoverEnterEvent(QGraphicsSceneHoverEvent*)
{
    m_hover = true;
    update();
}

void NavigationGizmoItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    const int hot = bubbleAt(event->pos());
    if (hot != m_hoverBubble) {
        m_hoverBubble = hot;
        update();
    }
}

void NavigationGizmoItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    m_hover = false;
    m_hoverBubble = -1;
    update();
}

void NavigationGizmoItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    m_pressPos = m_lastPos = event->pos();
    m_dragging = false;
    event->accept();
}

void NavigationGizmoItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (!m_dragging && QLineF(event->pos(), m_pressPos).length() > 3.0) {
        m_dragging = true;
    }
    if (m_dragging) {
        emit orbitDragged(event->pos() - m_lastPos);
        m_lastPos = event->pos();
    }
}

void NavigationGizmoItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (!m_dragging) {
        const int index = bubbleAt(event->pos());
        if (index >= 0) {
            emit axisClicked(bubbles()[static_cast<std::size_t>(index)].axis);
        }
    }
    m_dragging = false;
    update();
}

// ---------------------------------------------------------------------------------------------
IconButtonItem::IconButtonItem(theme::Icon icon, const QString& tooltip, QGraphicsItem* parent)
    : OverlayItem(parent)
    , m_icon(icon)
{
    setSize({ 30, 30 });
    setToolTip(tooltip);
    setCursor(Qt::PointingHandCursor);
}

void IconButtonItem::setChecked(bool checked)
{
    if (m_checked == checked) {
        return;
    }
    m_checked = checked;
    update();
}

void IconButtonItem::setIcon(theme::Icon icon)
{
    m_icon = icon;
    update();
}

void IconButtonItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = boundingRect().adjusted(1, 1, -1, -1);
    QColor fill(255, 255, 255, 0);
    if (m_checked) {
        fill = theme::kAccent;
    } else if (m_pressed) {
        fill = theme::kSelection;
    } else if (m_hover) {
        fill = QColor(255, 255, 255, 36);
    } else if (m_round) {
        fill = QColor(20, 28, 44, 170);
    }
    p->setPen(Qt::NoPen);
    p->setBrush(fill);
    if (m_round) {
        p->drawEllipse(r);
    } else {
        p->drawRoundedRect(r, 5, 5);
    }
    theme::paintIcon(*p, m_icon, r.adjusted(7, 7, -7, -7), m_checked ? QColor(Qt::white) : theme::kText);
}

void IconButtonItem::hoverEnterEvent(QGraphicsSceneHoverEvent*)
{
    m_hover = true;
    update();
}

void IconButtonItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    m_hover = false;
    update();
}

void IconButtonItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    m_pressed = true;
    update();
    event->accept();
}

void IconButtonItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    m_pressed = false;
    if (boundingRect().contains(event->pos())) {
        if (m_checkable) {
            m_checked = !m_checked;
            emit toggled(m_checked);
        }
        emit clicked();
    }
    update();
}

// ---------------------------------------------------------------------------------------------
ToolShelfItem::ToolShelfItem(QGraphicsItem* parent)
    : OverlayItem(parent)
{
    const std::pair<ViewportTool, std::pair<theme::Icon, QString>> tools[] = {
        { ViewportTool::Select, { theme::Icon::Cursor, tr("Select / orbit (LMB drag orbits)") } },
        { ViewportTool::Orbit, { theme::Icon::Orbit, tr("Orbit view") } },
        { ViewportTool::Pan, { theme::Icon::Pan, tr("Pan view") } },
        { ViewportTool::Zoom, { theme::Icon::Zoom, tr("Zoom view") } },
        { ViewportTool::Probe, { theme::Icon::Probe, tr("Probe flow field: click on the vehicle or the slice") } },
    };
    qreal y = 6;
    for (const auto& [tool, spec] : tools) {
        auto* button = new IconButtonItem(spec.first, spec.second, this);
        button->setRound(false);
        button->setCheckable(true);
        button->setPos(5, y);
        y += 34;
        connect(button, &IconButtonItem::clicked, this, [this, t = tool] {
            setTool(t);
            emit toolChanged(t);
        });
        m_buttons.emplace_back(tool, button);
    }
    setSize({ 40, y + 2 });
    setTool(ViewportTool::Select);
}

void ToolShelfItem::setTool(ViewportTool tool)
{
    m_tool = tool;
    for (auto& [t, button] : m_buttons) {
        button->setChecked(t == tool);
    }
}

void ToolShelfItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    drawCard(p, boundingRect(), 7, 0.78);
}

// ---------------------------------------------------------------------------------------------
HudItem::HudItem(QGraphicsItem* parent)
    : OverlayItem(parent)
{
    setAcceptHoverEvents(false);
    setAcceptedMouseButtons(Qt::NoButton);
    setSize({ 520, 70 });
}

void HudItem::setLines(const QStringList& lines)
{
    if (lines == m_lines) {
        return;
    }
    m_lines = lines;
    update();
}

void HudItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    p->setRenderHint(QPainter::TextAntialiasing);
    qreal y = 14;
    for (int i = 0; i < m_lines.size(); ++i) {
        p->setFont(uiFont(i == 0 ? 9.5 : 8.5, i == 0 ? QFont::DemiBold : QFont::Normal));
        shadowText(p, QPointF(2, y), m_lines[i], i == 0 ? theme::kText : theme::kTextDim);
        y += i == 0 ? 17 : 15;
    }
}

// ---------------------------------------------------------------------------------------------
ColorLegendItem::ColorLegendItem(QGraphicsItem* parent)
    : OverlayItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setSize({ 250, 56 });
}

void ColorLegendItem::setLegend(const TransferFunction& tf, const ScalarRange& range, const QString& title, const QString& unit, double unitScale)
{
    m_colors.clear();
    for (int i = 0; i <= 32; ++i) {
        const Vec3f c = tf.color(static_cast<float>(i) / 32.0f);
        m_colors.push_back(QColor::fromRgbF(c.x, c.y, c.z));
    }
    m_range = range;
    m_title = title;
    m_unit = unit;
    m_unitScale = unitScale;
    update();
}

void ColorLegendItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    const QRectF r = boundingRect();
    drawCard(p, r, 8, 0.8);
    p->setFont(uiFont(8, QFont::DemiBold));
    p->setPen(theme::kText);
    p->drawText(QRectF(10, 5, r.width() - 20, 16), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    p->setFont(uiFont(7.5));
    p->setPen(theme::kTextDim);
    p->drawText(QRectF(10, 5, r.width() - 20, 16), Qt::AlignRight | Qt::AlignVCenter, m_unit);

    const QRectF bar(10, 24, r.width() - 20, 9);
    QLinearGradient g(bar.topLeft(), bar.topRight());
    for (int i = 0; i < m_colors.size(); ++i) {
        g.setColorAt(static_cast<double>(i) / (m_colors.size() - 1), m_colors[i]);
    }
    p->setPen(Qt::NoPen);
    p->setBrush(g);
    p->drawRoundedRect(bar, 3, 3);

    p->setFont(uiFont(7.2));
    for (int i = 0; i <= 4; ++i) {
        const double t = i / 4.0;
        const double v = (m_range.min + (m_range.max - m_range.min) * t) * m_unitScale;
        const qreal x = bar.left() + bar.width() * t;
        p->setPen(QColor(255, 255, 255, 90));
        p->drawLine(QPointF(x, bar.bottom() + 1), QPointF(x, bar.bottom() + 4));
        p->setPen(theme::kTextDim);
        const Qt::Alignment a = i == 0 ? Qt::AlignLeft : (i == 4 ? Qt::AlignRight : Qt::AlignHCenter);
        const QRectF label(i == 0 ? x : (i == 4 ? x - 60 : x - 30), bar.bottom() + 4, 60, 13);
        p->drawText(label, a | Qt::AlignVCenter, QString::number(v, 'f', std::abs(v) >= 100 ? 0 : (std::abs(v) >= 10 ? 1 : 2)));
    }
}

// ---------------------------------------------------------------------------------------------
ForceCardItem::ForceCardItem(QGraphicsItem* parent)
    : OverlayItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setSize({ 250, 92 });
}

void ForceCardItem::setData(const SolverStats& stats, const QVector<ForceSample>& history, bool running)
{
    m_stats = stats;
    m_history = history;
    m_running = running;
    update();
}

void ForceCardItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    const QRectF r = boundingRect();
    drawCard(p, r, 8, 0.8);

    auto metric = [&](qreal x, const QString& label, const QString& value, const QColor& color) {
        p->setFont(uiFont(7.2, QFont::DemiBold));
        p->setPen(theme::kTextDim);
        p->drawText(QPointF(x, 17), label);
        p->setFont(uiFont(12.5, QFont::DemiBold));
        p->setPen(color);
        p->drawText(QPointF(x, 36), value);
    };
    metric(10, QStringLiteral("DRAG Cd"), QString::number(m_stats.cd, 'f', 3), theme::kAccentBright);
    metric(92, QStringLiteral("LIFT Cl"), QString::number(m_stats.cl, 'f', 3), QColor(0xf0, 0x9a, 0x4a));
    metric(172, QStringLiteral("DRAG"), QStringLiteral("%1 N").arg(m_stats.dragNewton, 0, 'f', 0), theme::kText);

    // Status dot
    p->setPen(Qt::NoPen);
    p->setBrush(m_running ? QColor(0x4a, 0xde, 0x80) : theme::kTextDim);
    p->drawEllipse(QPointF(r.width() - 10, 10), 3.5, 3.5);

    const QRectF plot(10, 46, r.width() - 20, r.height() - 54);
    p->setPen(QPen(QColor(255, 255, 255, 18), 1));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(plot, 3, 3);
    if (m_history.size() < 2) {
        p->setFont(uiFont(7.2));
        p->setPen(theme::kTextDim);
        p->drawText(plot, Qt::AlignCenter, tr("waiting for samples…"));
        return;
    }
    // Show the last 60% of the history so the start-up transient does not dominate the scale.
    const int first = m_history.size() > 20 ? m_history.size() * 2 / 5 : 0;
    double lo = 1e30, hi = -1e30;
    for (int i = first; i < m_history.size(); ++i) {
        lo = std::min({ lo, m_history[i].cd, m_history[i].cl });
        hi = std::max({ hi, m_history[i].cd, m_history[i].cl });
    }
    const double pad = std::max(0.02, (hi - lo) * 0.12);
    lo -= pad;
    hi += pad;
    const double s0 = static_cast<double>(m_history[first].step);
    const double s1 = std::max(s0 + 1.0, static_cast<double>(m_history.back().step));
    auto map = [&](double step, double v) {
        return QPointF(plot.left() + (step - s0) / (s1 - s0) * plot.width(), plot.bottom() - (v - lo) / (hi - lo) * plot.height());
    };
    QPainterPath cd, cl;
    for (int i = first; i < m_history.size(); ++i) {
        const QPointF a = map(static_cast<double>(m_history[i].step), m_history[i].cd);
        const QPointF b = map(static_cast<double>(m_history[i].step), m_history[i].cl);
        i == first ? cd.moveTo(a) : cd.lineTo(a);
        i == first ? cl.moveTo(b) : cl.lineTo(b);
    }
    p->setClipRect(plot);
    p->setPen(QPen(QColor(0xf0, 0x9a, 0x4a, 200), 1.3));
    p->drawPath(cl);
    p->setPen(QPen(theme::kAccentBright, 1.6));
    p->drawPath(cd);
}

// ---------------------------------------------------------------------------------------------
ProbeItem::ProbeItem(QGraphicsItem* parent)
    : OverlayItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(50);
}

void ProbeItem::setProbe(const QPointF& anchor, const QStringList& lines)
{
    m_lines = lines;
    QFontMetricsF fm(uiFont(8.2));
    qreal w = 0;
    for (const QString& l : lines) {
        w = std::max(w, fm.horizontalAdvance(l));
    }
    prepareGeometryChange();
    setSize({ w + 24, 12 + 16.0 * lines.size() + 10 });
    setPos(anchor + QPointF(14, -size().height() - 10));
    update();
}

void ProbeItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    const QRectF r(QPointF(0, 0), size());
    drawCard(p, r, 7, 0.92);
    // Leader line towards the anchor (bottom-left, outside the card).
    p->setPen(QPen(theme::kWarning, 1.4));
    p->drawLine(QPointF(0, r.height()), QPointF(-14, r.height() + 10));
    p->setBrush(theme::kWarning);
    p->drawEllipse(QPointF(-14, r.height() + 10), 3, 3);
    qreal y = 20;
    for (int i = 0; i < m_lines.size(); ++i) {
        p->setFont(uiFont(8.2, i == 0 ? QFont::DemiBold : QFont::Normal));
        p->setPen(i == 0 ? theme::kWarning : theme::kText);
        p->drawText(QPointF(12, y), m_lines[i]);
        y += 16;
    }
}

// ---------------------------------------------------------------------------------------------
LoadingItem::LoadingItem(QGraphicsItem* parent)
    : OverlayItem(parent)
{
    setSize({ 340, 104 });
    setZValue(100);
}

void LoadingItem::setState(const QString& title, const QString& detail, double progress)
{
    m_title = title;
    m_detail = detail;
    m_progress = progress;
    update();
}

void LoadingItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    const QRectF r = boundingRect();
    drawCard(p, r, 10, 0.94);
    theme::paintIcon(*p, theme::Icon::Wind, QRectF(18, 20, 30, 30), theme::kAccentBright);
    p->setFont(uiFont(10.5, QFont::DemiBold));
    p->setPen(theme::kText);
    p->drawText(QRectF(60, 18, r.width() - 76, 18), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    p->setFont(uiFont(8.2));
    p->setPen(theme::kTextDim);
    p->drawText(QRectF(60, 38, r.width() - 76, 16), Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(p->font()).elidedText(m_detail, Qt::ElideMiddle, static_cast<int>(r.width() - 76)));

    const QRectF track(18, 70, r.width() - 36, 6);
    p->setPen(Qt::NoPen);
    p->setBrush(theme::kField);
    p->drawRoundedRect(track, 3, 3);
    QLinearGradient g(track.topLeft(), track.topRight());
    g.setColorAt(0, theme::kAccent);
    g.setColorAt(1, theme::kCyan);
    p->setBrush(g);
    if (m_progress >= 0.0) {
        p->drawRoundedRect(QRectF(track.left(), track.top(), track.width() * std::clamp(m_progress, 0.0, 1.0), track.height()), 3, 3);
        p->setFont(uiFont(7.5));
        p->setPen(theme::kTextDim);
        p->drawText(QRectF(track.left(), track.bottom() + 4, track.width(), 14), Qt::AlignRight, QStringLiteral("%1%").arg(qRound(m_progress * 100)));
    } else {
        const double w = track.width() * 0.3;
        const double x = track.left() + (track.width() - w) * (0.5 + 0.5 * std::sin(m_phase * 3.0));
        p->drawRoundedRect(QRectF(x, track.top(), w, track.height()), 3, 3);
    }
}

} // namespace fluid::app
