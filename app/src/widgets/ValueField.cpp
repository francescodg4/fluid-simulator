#include "widgets/ValueField.hpp"

#include "ui/Theme.hpp"

#include <QDoubleValidator>
#include <QKeyEvent>
#include <QLocale>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>

namespace fluid::app {

ValueField::ValueField(const QString& label, double minimum, double maximum, double value, int decimals, QWidget* parent)
    : QWidget(parent)
    , m_label(label)
    , m_min(minimum)
    , m_max(maximum)
    , m_value(std::clamp(value, minimum, maximum))
    , m_decimals(decimals)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeHorCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover);
}

QSize ValueField::sizeHint() const
{
    return { 180, 22 };
}

QSize ValueField::minimumSizeHint() const
{
    return { 80, 22 };
}

void ValueField::setValue(double value)
{
    value = std::clamp(value, m_min, m_max);
    if (value == m_value) {
        return;
    }
    m_value = value;
    update();
}

void ValueField::setRange(double minimum, double maximum)
{
    m_min = minimum;
    m_max = maximum;
    m_value = std::clamp(m_value, m_min, m_max);
    update();
}

void ValueField::setDecimals(int decimals)
{
    m_decimals = decimals;
    update();
}

void ValueField::setSuffix(const QString& suffix)
{
    m_suffix = suffix;
    update();
}

void ValueField::setLabel(const QString& label)
{
    m_label = label;
    update();
}

double ValueField::toUnit(double v) const
{
    if (m_max <= m_min) {
        return 0.0;
    }
    if (m_log && m_min > 0.0) {
        return std::log(v / m_min) / std::log(m_max / m_min);
    }
    return (v - m_min) / (m_max - m_min);
}

double ValueField::fromUnit(double t) const
{
    t = std::clamp(t, 0.0, 1.0);
    if (m_log && m_min > 0.0) {
        return m_min * std::pow(m_max / m_min, t);
    }
    return m_min + t * (m_max - m_min);
}

QString ValueField::text() const
{
    QString s = QLocale().toString(m_value, 'f', m_decimals);
    if (!m_suffix.isEmpty()) {
        s += QLatin1Char(' ') + m_suffix;
    }
    return s;
}

void ValueField::applyValue(double v, bool commit)
{
    if (m_decimals == 0) {
        v = std::round(v);
    }
    if (m_step > 0.0) {
        v = std::round(v / m_step) * m_step;
    }
    v = std::clamp(v, m_min, m_max);
    const bool changed = v != m_value;
    m_value = v;
    update();
    if (changed) {
        emit valueChanged(m_value);
    }
    if (commit) {
        emit valueCommitted(m_value);
    }
}

void ValueField::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath shape;
    shape.addRoundedRect(r, 4, 4);

    p.fillPath(shape, m_hover || m_dragging ? theme::kFieldHover : theme::kField);
    if (m_slider && isEnabled()) {
        const double t = std::clamp(toUnit(m_value), 0.0, 1.0);
        p.save();
        p.setClipPath(shape);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0, theme::kFieldFill.lighter(115));
        g.setColorAt(1, theme::kFieldFill);
        p.fillRect(QRectF(r.left(), r.top(), r.width() * t, r.height()), g);
        p.restore();
    }
    p.setPen(QPen(hasFocus() ? theme::kAccent : theme::kBorder, 1));
    p.drawPath(shape);

    if (m_editor) {
        return;
    }
    const QRectF textRect = r.adjusted(8, 0, -8, 0);
    p.setPen(isEnabled() ? theme::kText : theme::kTextDim);
    QFont f = font();
    p.setFont(f);
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_label);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(isEnabled() ? QColor(Qt::white) : theme::kTextDim);
    p.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, text());
}

void ValueField::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_editor) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    m_dragging = false;
    m_pressPos = event->pos();
    m_pressUnit = toUnit(m_value);
}

void ValueField::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_pressed) {
        return;
    }
    const int dx = event->pos().x() - m_pressPos.x();
    if (!m_dragging && std::abs(dx) > 3) {
        m_dragging = true;
    }
    if (!m_dragging) {
        return;
    }
    double sensitivity = 1.0 / std::max(60, width());
    if (event->modifiers() & Qt::ShiftModifier) {
        sensitivity *= 0.1;
    }
    double v = fromUnit(m_pressUnit + dx * sensitivity);
    if (event->modifiers() & Qt::ControlModifier) {
        const double snap = m_decimals == 0 ? 10.0 : std::pow(10.0, 1 - m_decimals) * 5.0;
        v = std::round(v / snap) * snap;
    }
    applyValue(v, false);
}

void ValueField::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_pressed || event->button() != Qt::LeftButton) {
        return;
    }
    m_pressed = false;
    if (m_dragging) {
        m_dragging = false;
        emit valueCommitted(m_value);
        update();
    } else {
        beginEdit();
    }
}

void ValueField::wheelEvent(QWheelEvent* event)
{
    if (!(event->modifiers() & Qt::ControlModifier)) {
        event->ignore(); // let the surrounding scroll area scroll
        return;
    }
    const double steps = event->angleDelta().y() / 120.0;
    applyValue(fromUnit(toUnit(m_value) + steps * 0.02), true);
    event->accept();
}

void ValueField::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        beginEdit();
        break;
    case Qt::Key_Left:
    case Qt::Key_Down:
        applyValue(fromUnit(toUnit(m_value) - 0.01), true);
        break;
    case Qt::Key_Right:
    case Qt::Key_Up:
        applyValue(fromUnit(toUnit(m_value) + 0.01), true);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void ValueField::enterEvent(QEnterEvent*)
{
    m_hover = true;
    update();
}

void ValueField::leaveEvent(QEvent*)
{
    m_hover = false;
    update();
}

void ValueField::beginEdit()
{
    if (m_editor) {
        return;
    }
    m_editor = new QLineEdit(this);
    m_editor->setFrame(false);
    m_editor->setAlignment(Qt::AlignCenter);
    m_editor->setStyleSheet(QStringLiteral("QLineEdit { background: %1; border: 1px solid %2; border-radius: 4px; }")
                                .arg(theme::kPanelAlt.name(), theme::kAccent.name()));
    m_editor->setText(QString::number(m_value, 'f', m_decimals));
    m_editor->setGeometry(rect());
    auto* validator = new QDoubleValidator(m_min, m_max, std::max(m_decimals, 6), m_editor);
    validator->setNotation(QDoubleValidator::StandardNotation);
    validator->setLocale(QLocale::c());
    m_editor->setValidator(validator);
    m_editor->selectAll();
    m_editor->show();
    m_editor->setFocus();
    connect(m_editor, &QLineEdit::editingFinished, this, [this] { finishEdit(true); });
    update();
}

void ValueField::finishEdit(bool accept)
{
    if (!m_editor) {
        return;
    }
    QLineEdit* editor = m_editor;
    m_editor = nullptr;
    bool ok = false;
    const double v = editor->text().toDouble(&ok);
    editor->hide();
    editor->deleteLater();
    if (accept && ok) {
        applyValue(v, true);
    }
    update();
}

} // namespace fluid::app
