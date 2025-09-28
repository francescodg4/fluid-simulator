#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;
class QPainter;
class QRectF;

namespace fluid::app::theme {

// Blender-inspired dark UI with a blue identity.
inline const QColor kWindow { 0x0c, 0x11, 0x1c };
inline const QColor kPanel { 0x13, 0x1a, 0x2a };
inline const QColor kPanelAlt { 0x17, 0x20, 0x33 };
inline const QColor kHeader { 0x0a, 0x0f, 0x19 };
inline const QColor kField { 0x1d, 0x28, 0x3d };
inline const QColor kFieldHover { 0x25, 0x33, 0x4d };
inline const QColor kFieldFill { 0x2b, 0x4f, 0x8a };
inline const QColor kBorder { 0x24, 0x31, 0x4a };
inline const QColor kAccent { 0x3d, 0x8b, 0xff };
inline const QColor kAccentBright { 0x6c, 0xb2, 0xff };
inline const QColor kCyan { 0x38, 0xd0, 0xf0 };
inline const QColor kText { 0xd3, 0xdc, 0xec };
inline const QColor kTextDim { 0x7d, 0x8b, 0xa6 };
inline const QColor kSelection { 0x24, 0x47, 0x80 };
inline const QColor kWarning { 0xf5, 0xa6, 0x23 };
inline const QColor kAxisX { 0xf0, 0x4a, 0x5f };
inline const QColor kAxisY { 0x7c, 0xd6, 0x3a };
inline const QColor kAxisZ { 0x3f, 0x8c, 0xf5 };

/** Installs the Fusion style, a dark palette and the application style sheet. */
void apply(QApplication& app);

enum class Icon {
    Play,
    Pause,
    StepForward,
    SkipBack,
    Reset,
    Eye,
    EyeOff,
    Collision,
    Mesh,
    Collection,
    Scene,
    Camera,
    Grid,
    Wind,
    Streamlines,
    Particles,
    Slice,
    Volume,
    Report,
    Cursor,
    Orbit,
    Pan,
    Zoom,
    Probe,
    Frame,
    Ortho,
    Perspective,
    Screenshot,
    Folder,
    Plus,
    Chart,
    Palette,
    Emitter,
    Domain,
    Sphere,
};

/** Paints a vector glyph into @p rect (antialiased strokes, no image assets). */
void paintIcon(QPainter& painter, Icon icon, const QRectF& rect, const QColor& color);
QIcon icon(Icon icon, const QColor& color = kText);

} // namespace fluid::app::theme
