#pragma once

#include <QWidget>

class QLineEdit;

namespace fluid::app {

/**
 * Blender-style numeric field: label on the left, value on the right, optional fill bar.
 * Drag horizontally to scrub (Shift = fine, Ctrl = snap), click to type a value,
 * wheel + Ctrl to step. Supports logarithmic mapping for wide ranges (e.g. Reynolds numbers).
 */
class ValueField : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY valueChanged)
public:
    explicit ValueField(const QString& label, double minimum, double maximum, double value, int decimals = 2, QWidget* parent = nullptr);

    double value() const { return m_value; }
    void setValue(double value); ///< does not emit
    void setRange(double minimum, double maximum);
    void setDecimals(int decimals);
    void setSuffix(const QString& suffix);
    void setLogarithmic(bool log) { m_log = log; update(); }
    void setSliderVisible(bool visible) { m_slider = visible; update(); }
    void setStep(double step) { m_step = step; }
    void setLabel(const QString& label);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /** Emitted continuously while dragging and when an edit is committed. */
    void valueChanged(double value);
    /** Emitted once when the user finishes an interaction (mouse release / return). */
    void valueCommitted(double value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    double toUnit(double v) const;
    double fromUnit(double t) const;
    void applyValue(double v, bool commit);
    void beginEdit();
    void finishEdit(bool accept);
    QString text() const;

    QString m_label;
    QString m_suffix;
    double m_min;
    double m_max;
    double m_value;
    double m_step = 0.0;
    int m_decimals;
    bool m_log = false;
    bool m_slider = true;
    bool m_hover = false;
    bool m_dragging = false;
    bool m_pressed = false;
    QPoint m_pressPos;
    double m_pressUnit = 0.0;
    QLineEdit* m_editor = nullptr;
};

} // namespace fluid::app
