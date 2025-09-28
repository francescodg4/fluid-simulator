#pragma once

#include <QWidget>

class QFormLayout;
class QVBoxLayout;

namespace fluid::app {

/** Blender-like panel section: a clickable header with a disclosure arrow and a body. */
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    /** Body layout (vertical) to add rows to. */
    QVBoxLayout* body() const { return m_body; }
    /** Adds a "Label: widget" row like in Blender's property panels. */
    void addRow(const QString& label, QWidget* field);
    void addWidget(QWidget* widget);

    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }

signals:
    void expandedChanged(bool expanded);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QString m_title;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_body = nullptr;
    bool m_expanded = true;
};

} // namespace fluid::app
