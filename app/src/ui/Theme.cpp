#include "ui/Theme.hpp"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>

#include <cmath>

namespace fluid::app::theme {
namespace {

    QString css(const QColor& c) { return c.name(QColor::HexRgb); }

    QString styleSheet()
    {
        QString s = QStringLiteral(R"(
QMainWindow, QDialog { background: @window; }
QWidget { color: @text; font-size: 9pt; }
QToolTip { background: @panelAlt; color: @text; border: 1px solid @border; padding: 4px 6px; border-radius: 4px; }

QMenuBar { background: @header; border: none; padding: 1px 2px; }
QMenuBar::item { background: transparent; padding: 4px 9px; border-radius: 4px; }
QMenuBar::item:selected { background: @fieldHover; }
QMenu { background: @panelAlt; border: 1px solid @border; padding: 4px; border-radius: 6px; }
QMenu::item { padding: 5px 26px 5px 22px; border-radius: 4px; }
QMenu::item:selected { background: @selection; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
QMenu::icon { padding-left: 6px; }

QStatusBar { background: @header; color: @dim; border-top: 1px solid @border; }
QStatusBar QLabel { color: @dim; padding: 0 6px; }

QSplitter::handle { background: @window; }
QSplitter::handle:horizontal { width: 3px; }
QSplitter::handle:vertical { height: 3px; }
QSplitter::handle:hover { background: @accent; }

QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar::handle:vertical { background: @fieldHover; border-radius: 3px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: @fieldFill; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }
QScrollBar::handle:horizontal { background: @fieldHover; border-radius: 3px; min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QPushButton, QToolButton {
  background: @field; border: 1px solid @border; border-radius: 4px; padding: 4px 10px; color: @text;
}
QPushButton:hover, QToolButton:hover { background: @fieldHover; }
QPushButton:pressed, QToolButton:pressed { background: @selection; }
QPushButton:checked, QToolButton:checked { background: @accent; border-color: @accentBright; color: white; }
QPushButton:disabled, QToolButton:disabled { color: @dim; }
QToolButton[flat="true"] { background: transparent; border: none; padding: 3px; }
QToolButton[flat="true"]:hover { background: @fieldHover; }
QToolButton[flat="true"]:checked { background: @selection; }

QPushButton#primary { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #3f7fe8, stop:1 #2d63c4); border-color: #4c8ef5; color: white; font-weight: 600; }
QPushButton#primary:hover { background: #4a8cf5; }

QComboBox { background: @field; border: 1px solid @border; border-radius: 4px; padding: 3px 8px; min-height: 18px; }
QComboBox:hover { background: @fieldHover; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid @dim; margin-right: 6px; }
QComboBox QAbstractItemView { background: @panelAlt; border: 1px solid @border; selection-background-color: @selection; outline: none; padding: 2px; }

QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox {
  background: @field; border: 1px solid @border; border-radius: 4px; padding: 3px 6px; selection-background-color: @accent;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent; }

QCheckBox { spacing: 6px; }
QCheckBox::indicator { width: 13px; height: 13px; border-radius: 3px; border: 1px solid @border; background: @field; }
QCheckBox::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked { background: @accent; border-color: @accentBright; image: none; }

QTreeView, QListView, QTableView { background: @panel; alternate-background-color: @panelAlt; border: none; outline: none; }
QTreeView::item, QListView::item { padding: 2px 0; }
QTreeView::item:hover, QListView::item:hover { background: #1b2740; }
QTreeView::item:selected, QListView::item:selected { background: @selection; color: white; }
QHeaderView::section { background: @panelAlt; color: @dim; border: none; border-bottom: 1px solid @border; padding: 3px 6px; }

QTabWidget::pane { border: none; background: @panel; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; color: @dim; padding: 5px 12px; border: none; }
QTabBar::tab:hover { color: @text; }
QTabBar::tab:selected { color: white; background: @panelAlt; }
QTabBar#workspaceTabs::tab { padding: 4px 12px; margin: 3px 1px; border-radius: 4px; }
QTabBar#workspaceTabs::tab:selected { background: @field; border-bottom: 2px solid @accent; }
QTabWidget#sideTabs QTabBar::tab { padding: 10px 4px; margin: 1px 0; border-radius: 0; background: @header; }
QTabWidget#sideTabs QTabBar::tab:selected { background: @panelAlt; color: white; border-left: 2px solid @accent; }

QGroupBox { border: 1px solid @border; border-radius: 6px; margin-top: 12px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: @dim; }

QProgressBar { background: @field; border: none; border-radius: 3px; height: 6px; text-align: center; }
QProgressBar::chunk { background: @accent; border-radius: 3px; }

QFrame#panelCard { background: rgba(16, 23, 38, 222); border: 1px solid @border; border-radius: 8px; }
QFrame#headerBar { background: @header; border-bottom: 1px solid @border; }
QFrame#viewportHeader { background: rgba(14, 20, 33, 245); border-bottom: 1px solid @border; }
QLabel#dimLabel { color: @dim; }
QLabel#sectionLabel { color: @dim; font-weight: 600; }
)");
        const QList<QPair<QString, QColor>> tokens {
            { "@window", kWindow }, { "@panelAlt", kPanelAlt }, { "@panel", kPanel }, { "@header", kHeader },
            { "@fieldHover", kFieldHover }, { "@fieldFill", kFieldFill }, { "@field", kField }, { "@border", kBorder },
            { "@accentBright", kAccentBright }, { "@accent", kAccent }, { "@text", kText }, { "@dim", kTextDim },
            { "@selection", kSelection },
        };
        for (const auto& [token, color] : tokens) {
            s.replace(token, css(color));
        }
        return s;
    }

} // namespace

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    p.setColor(QPalette::Window, kWindow);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, kPanel);
    p.setColor(QPalette::AlternateBase, kPanelAlt);
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, kField);
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, kPanelAlt);
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::PlaceholderText, kTextDim);
    p.setColor(QPalette::Link, kAccentBright);
    p.setColor(QPalette::Disabled, QPalette::Text, kTextDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, kTextDim);
    p.setColor(QPalette::Disabled, QPalette::WindowText, kTextDim);
    app.setPalette(p);
    app.setStyleSheet(styleSheet());
}

void paintIcon(QPainter& painter, Icon icon, const QRectF& rect, const QColor& color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // Design grid: 24 x 24 units.
    const double s = std::min(rect.width(), rect.height()) / 24.0;
    painter.translate(rect.center().x() - 12 * s, rect.center().y() - 12 * s);
    painter.scale(s, s);
    QPen pen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    auto fill = [&] {
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
    };

    switch (icon) {
    case Icon::Play: {
        fill();
        QPolygonF tri { { 7, 5 }, { 19, 12 }, { 7, 19 } };
        painter.drawPolygon(tri);
        break;
    }
    case Icon::Pause:
        fill();
        painter.drawRoundedRect(QRectF(6, 5, 4, 14), 1, 1);
        painter.drawRoundedRect(QRectF(14, 5, 4, 14), 1, 1);
        break;
    case Icon::StepForward: {
        fill();
        painter.drawPolygon(QPolygonF { { 5, 5 }, { 15, 12 }, { 5, 19 } });
        painter.drawRoundedRect(QRectF(16, 5, 3, 14), 1, 1);
        break;
    }
    case Icon::SkipBack: {
        fill();
        painter.drawPolygon(QPolygonF { { 19, 5 }, { 9, 12 }, { 19, 19 } });
        painter.drawRoundedRect(QRectF(5, 5, 3, 14), 1, 1);
        break;
    }
    case Icon::Reset: {
        QPainterPath arc;
        arc.arcMoveTo(QRectF(5, 5, 14, 14), 110);
        arc.arcTo(QRectF(5, 5, 14, 14), 110, 290);
        painter.drawPath(arc);
        fill();
        painter.drawPolygon(QPolygonF { { 7.5, 2.5 }, { 11.5, 6.5 }, { 6, 8.5 } });
        break;
    }
    case Icon::Eye:
    case Icon::EyeOff: {
        QPainterPath eye;
        eye.moveTo(3, 12);
        eye.quadTo(12, 3, 21, 12);
        eye.quadTo(12, 21, 3, 12);
        painter.drawPath(eye);
        painter.drawEllipse(QPointF(12, 12), 3, 3);
        if (icon == Icon::EyeOff) {
            painter.drawLine(QPointF(4, 20), QPointF(20, 4));
        }
        break;
    }
    case Icon::Collision: {
        QPainterPath shield;
        shield.moveTo(12, 3);
        shield.lineTo(19, 6);
        shield.quadTo(19, 16, 12, 21);
        shield.quadTo(5, 16, 5, 6);
        shield.closeSubpath();
        painter.drawPath(shield);
        break;
    }
    case Icon::Mesh: {
        QPolygonF tri { { 12, 3 }, { 21, 19 }, { 3, 19 } };
        painter.drawPolygon(tri);
        painter.drawLine(QPointF(12, 3), QPointF(12, 19));
        painter.drawLine(QPointF(7.5, 11), QPointF(16.5, 11));
        break;
    }
    case Icon::Collection: {
        painter.drawRoundedRect(QRectF(3, 7, 18, 13), 2, 2);
        painter.drawLine(QPointF(3, 7), QPointF(6, 4));
        painter.drawLine(QPointF(6, 4), QPointF(11, 4));
        painter.drawLine(QPointF(11, 4), QPointF(13, 7));
        break;
    }
    case Icon::Scene:
        painter.drawEllipse(QRectF(4, 4, 16, 16));
        painter.drawLine(QPointF(4, 12), QPointF(20, 12));
        painter.drawEllipse(QRectF(9, 4, 6, 16));
        break;
    case Icon::Camera:
        painter.drawRoundedRect(QRectF(3, 7, 13, 10), 2, 2);
        painter.drawPolygon(QPolygonF { { 16, 10 }, { 21, 7 }, { 21, 17 }, { 16, 14 } });
        break;
    case Icon::Grid:
        for (int i = 0; i < 4; ++i) {
            painter.drawLine(QPointF(4 + i * 16.0 / 3, 4), QPointF(4 + i * 16.0 / 3, 20));
            painter.drawLine(QPointF(4, 4 + i * 16.0 / 3), QPointF(20, 4 + i * 16.0 / 3));
        }
        break;
    case Icon::Wind: {
        QPainterPath p;
        p.moveTo(3, 9);
        p.lineTo(15, 9);
        p.cubicTo(19, 9, 19, 4, 15.5, 4.5);
        p.moveTo(3, 13);
        p.lineTo(19, 13);
        p.cubicTo(23, 13, 22, 19, 18, 18);
        p.moveTo(3, 17);
        p.lineTo(11, 17);
        painter.drawPath(p);
        break;
    }
    case Icon::Streamlines: {
        QPainterPath p;
        for (int i = 0; i < 3; ++i) {
            const double y = 7 + i * 5;
            p.moveTo(3, y);
            p.cubicTo(9, y - 4, 14, y + 4, 21, y);
        }
        painter.drawPath(p);
        break;
    }
    case Icon::Particles:
        fill();
        for (const QPointF& c : { QPointF(6, 7), QPointF(12, 5), QPointF(17, 9), QPointF(8, 13), QPointF(14, 14), QPointF(19, 17), QPointF(6, 19), QPointF(11, 19) }) {
            painter.drawEllipse(c, 1.7, 1.7);
        }
        break;
    case Icon::Slice: {
        QPolygonF plane { { 4, 8 }, { 14, 4 }, { 20, 16 }, { 10, 20 } };
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 70));
        painter.drawPolygon(plane);
        break;
    }
    case Icon::Volume: {
        painter.drawPolygon(QPolygonF { { 12, 3 }, { 20, 7.5 }, { 12, 12 }, { 4, 7.5 } });
        painter.drawPolyline(QPolygonF { { 4, 7.5 }, { 4, 16.5 }, { 12, 21 }, { 20, 16.5 }, { 20, 7.5 } });
        painter.drawLine(QPointF(12, 12), QPointF(12, 21));
        break;
    }
    case Icon::Report:
        painter.drawRoundedRect(QRectF(5, 3, 14, 18), 2, 2);
        painter.drawLine(QPointF(8, 8), QPointF(16, 8));
        painter.drawLine(QPointF(8, 12), QPointF(16, 12));
        painter.drawLine(QPointF(8, 16), QPointF(13, 16));
        break;
    case Icon::Cursor: {
        fill();
        painter.drawPolygon(QPolygonF { { 6, 3 }, { 18, 13 }, { 12.5, 13.5 }, { 15.5, 20 }, { 13, 21 }, { 10, 14.5 }, { 6, 18 } });
        break;
    }
    case Icon::Orbit:
        painter.drawEllipse(QRectF(3, 8, 18, 8));
        painter.drawEllipse(QPointF(12, 12), 3, 3);
        painter.drawLine(QPointF(12, 3), QPointF(12, 6));
        painter.drawLine(QPointF(12, 18), QPointF(12, 21));
        break;
    case Icon::Pan: {
        painter.drawLine(QPointF(12, 3), QPointF(12, 21));
        painter.drawLine(QPointF(3, 12), QPointF(21, 12));
        fill();
        painter.drawPolygon(QPolygonF { { 12, 2 }, { 9.5, 5.5 }, { 14.5, 5.5 } });
        painter.drawPolygon(QPolygonF { { 12, 22 }, { 9.5, 18.5 }, { 14.5, 18.5 } });
        painter.drawPolygon(QPolygonF { { 2, 12 }, { 5.5, 9.5 }, { 5.5, 14.5 } });
        painter.drawPolygon(QPolygonF { { 22, 12 }, { 18.5, 9.5 }, { 18.5, 14.5 } });
        break;
    }
    case Icon::Zoom:
        painter.drawEllipse(QRectF(4, 4, 12, 12));
        painter.drawLine(QPointF(14.5, 14.5), QPointF(20, 20));
        painter.drawLine(QPointF(7, 10), QPointF(13, 10));
        painter.drawLine(QPointF(10, 7), QPointF(10, 13));
        break;
    case Icon::Probe:
        painter.drawEllipse(QRectF(5, 5, 14, 14));
        painter.drawLine(QPointF(12, 2), QPointF(12, 8));
        painter.drawLine(QPointF(12, 16), QPointF(12, 22));
        painter.drawLine(QPointF(2, 12), QPointF(8, 12));
        painter.drawLine(QPointF(16, 12), QPointF(22, 12));
        break;
    case Icon::Frame:
        for (const auto& corner : { QPolygonF { { 4, 9 }, { 4, 4 }, { 9, 4 } }, QPolygonF { { 15, 4 }, { 20, 4 }, { 20, 9 } },
                 QPolygonF { { 20, 15 }, { 20, 20 }, { 15, 20 } }, QPolygonF { { 9, 20 }, { 4, 20 }, { 4, 15 } } }) {
            painter.drawPolyline(corner);
        }
        painter.drawRect(QRectF(9, 9, 6, 6));
        break;
    case Icon::Ortho:
        painter.drawRect(QRectF(5, 5, 14, 14));
        painter.drawLine(QPointF(5, 12), QPointF(19, 12));
        painter.drawLine(QPointF(12, 5), QPointF(12, 19));
        break;
    case Icon::Perspective:
        painter.drawPolygon(QPolygonF { { 8, 6 }, { 16, 6 }, { 21, 19 }, { 3, 19 } });
        painter.drawLine(QPointF(12, 6), QPointF(12, 19));
        painter.drawLine(QPointF(5.5, 12.5), QPointF(18.5, 12.5));
        break;
    case Icon::Screenshot:
        painter.drawRoundedRect(QRectF(3, 7, 18, 13), 2, 2);
        painter.drawEllipse(QPointF(12, 13.5), 3.5, 3.5);
        painter.drawLine(QPointF(8, 7), QPointF(9.5, 4));
        painter.drawLine(QPointF(9.5, 4), QPointF(14.5, 4));
        painter.drawLine(QPointF(14.5, 4), QPointF(16, 7));
        break;
    case Icon::Folder: {
        QPainterPath p;
        p.moveTo(3, 18);
        p.lineTo(3, 6);
        p.lineTo(9, 6);
        p.lineTo(11, 8);
        p.lineTo(21, 8);
        p.lineTo(21, 18);
        p.closeSubpath();
        painter.drawPath(p);
        break;
    }
    case Icon::Plus:
        painter.drawLine(QPointF(12, 5), QPointF(12, 19));
        painter.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Icon::Chart:
        painter.drawPolyline(QPolygonF { { 3, 3 }, { 3, 21 }, { 21, 21 } });
        painter.drawPolyline(QPolygonF { { 6, 16 }, { 10, 10 }, { 14, 13 }, { 20, 5 } });
        break;
    case Icon::Palette: {
        QLinearGradient g(3, 0, 21, 0);
        g.setColorAt(0.0, QColor(0x30, 0x40, 0xd0));
        g.setColorAt(0.35, QColor(0x20, 0xc0, 0xe0));
        g.setColorAt(0.65, QColor(0xf0, 0xe0, 0x30));
        g.setColorAt(1.0, QColor(0xe0, 0x30, 0x30));
        painter.setBrush(g);
        painter.drawRoundedRect(QRectF(3, 8, 18, 8), 2, 2);
        break;
    }
    case Icon::Emitter:
        painter.drawRect(QRectF(4, 5, 4, 14));
        painter.drawLine(QPointF(11, 8), QPointF(20, 8));
        painter.drawLine(QPointF(11, 12), QPointF(20, 12));
        painter.drawLine(QPointF(11, 16), QPointF(20, 16));
        break;
    case Icon::Domain:
        painter.drawRect(QRectF(3, 7, 14, 12));
        painter.drawPolyline(QPolygonF { { 3, 7 }, { 7, 3 }, { 21, 3 }, { 21, 15 }, { 17, 19 } });
        painter.drawLine(QPointF(17, 7), QPointF(21, 3));
        break;
    case Icon::Sphere: {
        QRadialGradient g(QPointF(9, 9), 12);
        g.setColorAt(0, color.lighter(160));
        g.setColorAt(1, color.darker(160));
        painter.setBrush(g);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(4, 4, 16, 16));
        break;
    }
    }
    painter.restore();
}

QIcon icon(Icon glyph, const QColor& color)
{
    QIcon result;
    for (const int size : { 16, 24, 32, 48 }) {
        for (const bool active : { false, true }) {
            QPixmap pm(size, size);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            paintIcon(p, glyph, QRectF(0, 0, size, size), active ? QColor(Qt::white) : color);
            p.end();
            result.addPixmap(pm, active ? QIcon::Active : QIcon::Normal);
            if (!active) {
                result.addPixmap(pm, QIcon::Normal, QIcon::Off);
            } else {
                result.addPixmap(pm, QIcon::Selected);
            }
        }
    }
    return result;
}

} // namespace fluid::app::theme
