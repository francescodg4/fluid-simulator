#include "widgets/TransferFunctionEditor.hpp"

#include "ui/Theme.hpp"

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace fluid::app {

// ---------------------------------------------------------------------------------------------
/** Paints the histogram, grid, colormap strip and range labels. */
class TransferBackgroundItem : public QGraphicsItem {
public:
    explicit TransferBackgroundItem(TransferFunctionEditor* editor)
        : m_editor(editor)
    {
        setZValue(-10);
        setAcceptedMouseButtons(Qt::NoButton);
    }

    void setGeometry(const QRectF& plot, const QRectF& full)
    {
        prepareGeometryChange();
        m_plot = plot;
        m_full = full;
    }
    void setHistogram(const QVector<float>& bins)
    {
        m_bins = bins;
        update();
    }
    void setLabels(const QString& lo, const QString& hi)
    {
        m_lo = lo;
        m_hi = hi;
        update();
    }
    void setColors(const TransferFunction& tf)
    {
        m_colors.clear();
        for (int i = 0; i <= 32; ++i) {
            const Vec3f c = tf.color(static_cast<float>(i) / 32.0f);
            m_colors.push_back(QColor::fromRgbF(c.x, c.y, c.z));
        }
        update();
    }

    QRectF boundingRect() const override { return m_full; }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(Qt::NoPen);
        p->setBrush(theme::kPanelAlt.darker(115));
        p->drawRoundedRect(m_full.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);

        // Histogram (log-scaled counts)
        if (!m_bins.isEmpty()) {
            float peak = 0.0f;
            for (float b : m_bins) {
                peak = std::max(peak, std::log1p(b));
            }
            const qreal w = m_plot.width() / m_bins.size();
            p->setBrush(QColor(0x5a, 0x74, 0xa6, 70));
            for (int i = 0; i < m_bins.size(); ++i) {
                const qreal h = peak > 0 ? m_plot.height() * std::log1p(m_bins[i]) / peak : 0.0;
                p->drawRect(QRectF(m_plot.left() + i * w, m_plot.bottom() - h, std::max<qreal>(1.0, w - 1), h));
            }
        }
        // Grid
        p->setPen(QPen(QColor(255, 255, 255, 16), 1));
        for (int i = 1; i < 4; ++i) {
            const qreal y = m_plot.top() + m_plot.height() * i / 4.0;
            const qreal x = m_plot.left() + m_plot.width() * i / 4.0;
            p->drawLine(QPointF(m_plot.left(), y), QPointF(m_plot.right(), y));
            p->drawLine(QPointF(x, m_plot.top()), QPointF(x, m_plot.bottom()));
        }
        // Colormap strip
        const QRectF strip(m_plot.left(), m_plot.bottom() + 5, m_plot.width(), 9);
        QLinearGradient g(strip.topLeft(), strip.topRight());
        for (int i = 0; i < m_colors.size(); ++i) {
            g.setColorAt(static_cast<double>(i) / (m_colors.size() - 1), m_colors[i]);
        }
        p->setPen(Qt::NoPen);
        p->setBrush(g);
        p->drawRoundedRect(strip, 3, 3);
        QFont f = p->font();
        f.setPointSizeF(7.2);
        p->setFont(f);
        p->setPen(theme::kTextDim);
        const QRectF labels(m_plot.left(), strip.bottom() + 1, m_plot.width(), 13);
        p->drawText(labels, Qt::AlignLeft | Qt::AlignVCenter, m_lo);
        p->drawText(labels, Qt::AlignRight | Qt::AlignVCenter, m_hi);
        p->drawText(labels, Qt::AlignHCenter | Qt::AlignVCenter, QObject::tr("opacity ↑   value →"));
    }

private:
    TransferFunctionEditor* m_editor;
    QRectF m_plot;
    QRectF m_full;
    QVector<float> m_bins;
    QVector<QColor> m_colors;
    QString m_lo;
    QString m_hi;
};

// ---------------------------------------------------------------------------------------------
class ControlPointItem : public QGraphicsEllipseItem {
public:
    ControlPointItem(TransferFunctionEditor* editor, bool endpoint)
        : QGraphicsEllipseItem(-5.5, -5.5, 11, 11)
        , m_editor(editor)
        , m_endpoint(endpoint)
    {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges | ItemIsFocusable);
        setAcceptHoverEvents(true);
        setCursor(endpoint ? Qt::SizeVerCursor : Qt::SizeAllCursor);
        setZValue(10);
        setPen(QPen(Qt::white, 1.5));
        setBrush(theme::kAccent);
    }

    bool isEndpoint() const { return m_endpoint; }
    void setFixedX(qreal x) { m_fixedX = x; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override
    {
        if (change == ItemPositionChange && scene()) {
            const QRectF plot = m_editor->plotRect();
            QPointF p = value.toPointF();
            p.setX(m_endpoint ? m_fixedX : std::clamp(p.x(), plot.left() + 1, plot.right() - 1));
            p.setY(std::clamp(p.y(), plot.top(), plot.bottom()));
            return p;
        }
        if (change == ItemPositionHasChanged && !m_editor->m_syncing) {
            m_editor->pointMoved();
        }
        if (change == ItemSelectedHasChanged) {
            setBrush(value.toBool() ? theme::kWarning : theme::kAccent);
        }
        return QGraphicsEllipseItem::itemChange(change, value);
    }

    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        setScale(1.25);
        const auto op = m_editor->fromScene(pos());
        setToolTip(QObject::tr("value %1 %, opacity %2").arg(qRound(op.t * 100)).arg(op.alpha, 0, 'f', 2));
        QGraphicsEllipseItem::hoverEnterEvent(event);
    }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        setScale(1.0);
        QGraphicsEllipseItem::hoverLeaveEvent(event);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
        QGraphicsEllipseItem::mouseReleaseEvent(event);
        m_editor->commit();
    }

private:
    TransferFunctionEditor* m_editor;
    bool m_endpoint;
    qreal m_fixedX = 0;
};

// ---------------------------------------------------------------------------------------------
TransferFunctionEditor::TransferFunctionEditor(QWidget* parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(Qt::transparent);
    setStyleSheet(QStringLiteral("background: transparent;"));
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setMinimumHeight(130);
    setToolTip(tr("Opacity transfer function — drag points, double-click to add, right-click to remove"));

    m_background = new TransferBackgroundItem(this);
    scene()->addItem(m_background);
    m_fill = scene()->addPath(QPainterPath(), Qt::NoPen);
    m_fill->setZValue(-5);
    m_curve = scene()->addPath(QPainterPath(), QPen(QColor(0xe8, 0xef, 0xff), 1.6));
    m_curve->setZValue(-4);
    setTransferFunction(m_tf);
}

TransferFunctionEditor::~TransferFunctionEditor() = default;

QRectF TransferFunctionEditor::plotRect() const
{
    const QRectF r(QPointF(0, 0), QSizeF(viewport()->size()));
    return r.adjusted(10, 10, -10, -34);
}

QPointF TransferFunctionEditor::toScene(const TransferFunction::OpacityPoint& p) const
{
    const QRectF plot = plotRect();
    return { plot.left() + p.t * plot.width(), plot.bottom() - p.alpha * plot.height() };
}

TransferFunction::OpacityPoint TransferFunctionEditor::fromScene(const QPointF& pos) const
{
    const QRectF plot = plotRect();
    return { static_cast<float>(std::clamp((pos.x() - plot.left()) / plot.width(), 0.0, 1.0)),
        static_cast<float>(std::clamp((plot.bottom() - pos.y()) / plot.height(), 0.0, 1.0)) };
}

void TransferFunctionEditor::setTransferFunction(const TransferFunction& tf)
{
    m_tf = tf;
    m_background->setColors(m_tf);
    rebuildPoints();
}

void TransferFunctionEditor::setHistogram(const QVector<float>& bins)
{
    m_background->setHistogram(bins);
}

void TransferFunctionEditor::setRangeLabels(const QString& minimum, const QString& maximum)
{
    m_background->setLabels(minimum, maximum);
}

void TransferFunctionEditor::rebuildPoints()
{
    m_syncing = true;
    for (ControlPointItem* p : m_points) {
        scene()->removeItem(p);
        delete p;
    }
    m_points.clear();
    const auto& points = m_tf.opacityPoints();
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const bool endpoint = i == 0 || i == static_cast<int>(points.size()) - 1;
        auto* item = new ControlPointItem(this, endpoint);
        const QPointF pos = toScene(points[static_cast<std::size_t>(i)]);
        item->setFixedX(pos.x());
        scene()->addItem(item);
        item->setPos(pos);
        m_points.push_back(item);
    }
    m_syncing = false;
    updateCurve();
}

void TransferFunctionEditor::pointMoved()
{
    std::vector<TransferFunction::OpacityPoint> points;
    for (ControlPointItem* p : m_points) {
        points.push_back(fromScene(p->pos()));
    }
    m_tf.setOpacityPoints(std::move(points));
    updateCurve();
    emit transferFunctionChanged(m_tf);
}

void TransferFunctionEditor::commit()
{
    // Re-sort the item list to match the (sorted) model after a drag crossed another point.
    std::sort(m_points.begin(), m_points.end(), [](ControlPointItem* a, ControlPointItem* b) { return a->pos().x() < b->pos().x(); });
    emit transferFunctionChanged(m_tf);
}

void TransferFunctionEditor::updateCurve()
{
    const QRectF plot = plotRect();
    QPainterPath curve;
    const auto& points = m_tf.opacityPoints();
    for (std::size_t i = 0; i < points.size(); ++i) {
        const QPointF p = toScene(points[i]);
        i == 0 ? curve.moveTo(p) : curve.lineTo(p);
    }
    m_curve->setPath(curve);

    QPainterPath fill = curve;
    if (!points.empty()) {
        fill.lineTo(toScene({ points.back().t, 0.0f }));
        fill.lineTo(toScene({ points.front().t, 0.0f }));
        fill.closeSubpath();
    }
    QLinearGradient g(plot.topLeft(), plot.topRight());
    for (int i = 0; i <= 16; ++i) {
        const float t = static_cast<float>(i) / 16.0f;
        const Vec3f c = m_tf.color(t);
        g.setColorAt(t, QColor::fromRgbF(c.x, c.y, c.z, 0.35f + 0.4f * m_tf.opacity(t)));
    }
    m_fill->setBrush(g);
    m_fill->setPath(fill);
}

void TransferFunctionEditor::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    const QRectF full(QPointF(0, 0), QSizeF(viewport()->size()));
    scene()->setSceneRect(full);
    m_background->setGeometry(plotRect(), full);
    rebuildPoints();
}

void TransferFunctionEditor::mouseDoubleClickEvent(QMouseEvent* event)
{
    const QPointF pos = mapToScene(event->pos());
    if (!plotRect().contains(pos) || dynamic_cast<ControlPointItem*>(itemAt(event->pos()))) {
        QGraphicsView::mouseDoubleClickEvent(event);
        return;
    }
    auto points = m_tf.opacityPoints();
    points.push_back(fromScene(pos));
    m_tf.setOpacityPoints(std::move(points));
    rebuildPoints();
    emit transferFunctionChanged(m_tf);
}

void TransferFunctionEditor::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        if (auto* item = dynamic_cast<ControlPointItem*>(itemAt(event->pos())); item && !item->isEndpoint()) {
            const int index = static_cast<int>(m_points.indexOf(item));
            auto points = m_tf.opacityPoints();
            if (index >= 0 && index < static_cast<int>(points.size())) {
                points.erase(points.begin() + index);
                m_tf.setOpacityPoints(std::move(points));
                rebuildPoints();
                emit transferFunctionChanged(m_tf);
            }
        }
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void TransferFunctionEditor::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        auto points = m_tf.opacityPoints();
        for (int i = static_cast<int>(m_points.size()) - 1; i >= 0; --i) {
            if (m_points[i]->isSelected() && !m_points[i]->isEndpoint()) {
                points.erase(points.begin() + i);
            }
        }
        m_tf.setOpacityPoints(std::move(points));
        rebuildPoints();
        emit transferFunctionChanged(m_tf);
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

} // namespace fluid::app
