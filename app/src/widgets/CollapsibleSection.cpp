#include "widgets/CollapsibleSection.hpp"

#include "ui/Theme.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace fluid::app {
namespace {
    constexpr int kHeaderHeight = 26;
}

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QWidget(parent)
    , m_title(title)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, kHeaderHeight, 0, 4);
    outer->setSpacing(0);
    m_content = new QWidget(this);
    m_body = new QVBoxLayout(m_content);
    m_body->setContentsMargins(8, 4, 8, 6);
    m_body->setSpacing(4);
    outer->addWidget(m_content);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
}

void CollapsibleSection::addRow(const QString& label, QWidget* field)
{
    auto* row = new QWidget(m_content);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    auto* l = new QLabel(label, row);
    l->setObjectName(QStringLiteral("dimLabel"));
    l->setFixedWidth(70);
    h->addWidget(l);
    h->addWidget(field, 1);
    m_body->addWidget(row);
}

void CollapsibleSection::addWidget(QWidget* widget)
{
    m_body->addWidget(widget);
}

void CollapsibleSection::setExpanded(bool expanded)
{
    if (m_expanded == expanded) {
        return;
    }
    m_expanded = expanded;
    m_content->setVisible(expanded);
    updateGeometry();
    update();
    emit expandedChanged(expanded);
}

void CollapsibleSection::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF header(1, 1, width() - 2, kHeaderHeight - 2);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p.fillPath(path, QColor(0x16, 0x1f, 0x31, 235));
    p.setPen(QPen(theme::kBorder, 1));
    p.drawPath(path);

    // Disclosure arrow
    p.setPen(Qt::NoPen);
    p.setBrush(theme::kTextDim);
    const QPointF c(header.left() + 12, header.center().y());
    QPolygonF arrow = m_expanded ? QPolygonF { c + QPointF(-4, -2), c + QPointF(4, -2), c + QPointF(0, 3) }
                                 : QPolygonF { c + QPointF(-2, -4), c + QPointF(3, 0), c + QPointF(-2, 4) };
    p.drawPolygon(arrow);

    QFont f = font();
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(theme::kText);
    p.drawText(header.adjusted(24, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, m_title);

    // Drag-handle dots like Blender's panel headers
    p.setBrush(QColor(255, 255, 255, 40));
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            p.drawEllipse(QPointF(header.right() - 14 + j * 4, header.center().y() - 4 + i * 4), 1.1, 1.1);
        }
    }
}

void CollapsibleSection::mousePressEvent(QMouseEvent* event)
{
    if (event->position().y() < kHeaderHeight) {
        setExpanded(!m_expanded);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

} // namespace fluid::app
