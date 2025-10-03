#include "services/ReportService.hpp"

#include "version.h"

#include <QDateTime>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace fluid::app {
namespace {

    // Report palette.
    const QColor kInk(0x1a, 0x23, 0x33);
    const QColor kMuted(0x5b, 0x6b, 0x84);
    const QColor kAccent(0x2f, 0x7d, 0xf6);
    const QColor kBand(0x0f, 0x1a, 0x2e);
    const QColor kRule(0xd5, 0xdd, 0xea);
    const QColor kPanel(0xf3, 0xf6, 0xfb);

    constexpr double kPageW = 595.0; // A4 in points
    constexpr double kPageH = 842.0;
    constexpr double kMargin = 40.0;

    // The painter is scaled so that one unit is one point; point sizes are resolved against the
    // device DPI first, so compensate for it to keep text in page units.
    thread_local double gDeviceDpi = 72.0;

    QFont font(double pt, int weight = QFont::Normal)
    {
        QFont f(QStringLiteral("Sans Serif"));
        f.setPointSizeF(pt * 72.0 / gDeviceDpi);
        f.setWeight(static_cast<QFont::Weight>(weight));
        return f;
    }

    void sectionTitle(QPainter& p, const QString& text, double x, double y, double w)
    {
        p.setFont(font(10.5, QFont::DemiBold));
        p.setPen(kInk);
        p.drawText(QRectF(x, y, w, 16), Qt::AlignLeft | Qt::AlignVCenter, text);
        p.setPen(QPen(kAccent, 1.2));
        p.drawLine(QPointF(x, y + 18), QPointF(x + 28, y + 18));
    }

    double table(QPainter& p, const QList<QPair<QString, QString>>& rows, double x, double y, double w)
    {
        const double rowH = 16.0;
        for (int i = 0; i < rows.size(); ++i) {
            const QRectF r(x, y + i * rowH, w, rowH);
            if (i % 2 == 0) {
                p.fillRect(r, kPanel);
            }
            p.setFont(font(8.2));
            p.setPen(kMuted);
            p.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter, rows[i].first);
            p.setFont(font(8.2, QFont::DemiBold));
            p.setPen(kInk);
            p.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignRight | Qt::AlignVCenter, rows[i].second);
        }
        return y + rows.size() * rowH;
    }

    void chart(QPainter& p, const QVector<ForceSample>& history, const QRectF& area)
    {
        p.fillRect(area, kPanel);
        const QRectF plot = area.adjusted(34, 10, -10, -22);
        p.setFont(font(7));
        if (history.size() < 2) {
            p.setPen(kMuted);
            p.drawText(area, Qt::AlignCenter, QObject::tr("Not enough samples yet"));
            return;
        }
        // Skip the start-up transient so the converged values set the scale.
        const QVector<ForceSample> tail = history.size() > 20 ? history.mid(history.size() / 5) : history;
        double lo = 0.0, hi = 0.0;
        for (const ForceSample& s : tail) {
            lo = std::min({ lo, s.cd, s.cl });
            hi = std::max({ hi, s.cd, s.cl });
        }
        const double pad = std::max(0.05, (hi - lo) * 0.1);
        lo -= pad;
        hi += pad;
        const double s0 = static_cast<double>(tail.front().step);
        const double s1 = std::max(s0 + 1.0, static_cast<double>(tail.back().step));
        auto map = [&](double step, double v) {
            return QPointF(plot.left() + (step - s0) / (s1 - s0) * plot.width(), plot.bottom() - (v - lo) / (hi - lo) * plot.height());
        };

        p.setPen(QPen(kRule, 0.6));
        for (int i = 0; i <= 4; ++i) {
            const double v = lo + (hi - lo) * i / 4.0;
            const QPointF a = map(s0, v);
            p.drawLine(a, QPointF(plot.right(), a.y()));
            p.setPen(kMuted);
            p.drawText(QRectF(area.left(), a.y() - 6, 30, 12), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'f', 2));
            p.setPen(QPen(kRule, 0.6));
        }
        p.setPen(kMuted);
        p.drawText(QRectF(plot.left(), plot.bottom() + 4, plot.width(), 12), Qt::AlignLeft, QString::number(s0, 'f', 0));
        p.drawText(QRectF(plot.left(), plot.bottom() + 4, plot.width(), 12), Qt::AlignHCenter, QObject::tr("solver iteration"));
        p.drawText(QRectF(plot.left(), plot.bottom() + 4, plot.width(), 12), Qt::AlignRight, QString::number(s1, 'f', 0));

        QPainterPath cd, cl;
        for (int i = 0; i < tail.size(); ++i) {
            const QPointF a = map(static_cast<double>(tail[i].step), tail[i].cd);
            const QPointF b = map(static_cast<double>(tail[i].step), tail[i].cl);
            i == 0 ? cd.moveTo(a) : cd.lineTo(a);
            i == 0 ? cl.moveTo(b) : cl.lineTo(b);
        }
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kAccent, 1.4));
        p.drawPath(cd);
        p.setPen(QPen(QColor(0xe0, 0x7a, 0x2f), 1.4));
        p.drawPath(cl);

        p.setFont(font(7.5, QFont::DemiBold));
        p.setPen(kAccent);
        p.drawText(QPointF(plot.right() - 70, plot.top() + 10), QStringLiteral("Cd"));
        p.setPen(QColor(0xe0, 0x7a, 0x2f));
        p.drawText(QPointF(plot.right() - 40, plot.top() + 10), QStringLiteral("Cl"));
    }

} // namespace

ReportService::ReportService(QObject* parent)
    : QObject(parent)
{
}

void ReportService::paint(QPainter& p, const ReportData& d, const QRectF& page)
{
    p.save();
    gDeviceDpi = p.device() ? p.device()->logicalDpiY() : 72.0;
    p.translate(page.topLeft());
    p.scale(page.width() / kPageW, page.height() / kPageH);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(QRectF(0, 0, kPageW, kPageH), Qt::white);

    // Header band
    p.fillRect(QRectF(0, 0, kPageW, 78), kBand);
    p.fillRect(QRectF(0, 78, kPageW, 3), kAccent);
    p.setPen(Qt::white);
    p.setFont(font(18, QFont::Bold));
    p.drawText(QRectF(kMargin, 16, 380, 28), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(d.title, Qt::ElideRight, 380));
    p.setFont(font(8.5));
    p.setPen(QColor(0xa9, 0xc4, 0xee));
    p.drawText(QRectF(kMargin, 44, 360, 16), Qt::AlignLeft | Qt::AlignVCenter,
        QObject::tr("Virtual wind tunnel · D3Q19 lattice Boltzmann with Smagorinsky LES"));
    p.drawText(QRectF(kPageW - kMargin - 200, 20, 200, 16), Qt::AlignRight | Qt::AlignVCenter,
        QDateTime::currentDateTime().toString(QStringLiteral("dd MMM yyyy · HH:mm")));
    p.drawText(QRectF(kPageW - kMargin - 200, 38, 200, 16), Qt::AlignRight | Qt::AlignVCenter, d.author);

    double y = 96;
    // Viewport capture
    const double imgW = kPageW - 2 * kMargin;
    double imgH = 250;
    if (!d.viewport.isNull()) {
        imgH = std::min(270.0, imgW * d.viewport.height() / std::max(1, d.viewport.width()));
        QPainterPath clip;
        clip.addRoundedRect(QRectF(kMargin, y, imgW, imgH), 6, 6);
        p.save();
        p.setClipPath(clip);
        // Crop (never stretch) the capture to the frame's aspect ratio.
        QRectF source(QPointF(0, 0), QSizeF(d.viewport.size()));
        const double targetAspect = imgW / imgH;
        if (source.width() / source.height() < targetAspect) {
            const double h = source.width() / targetAspect;
            source = QRectF(0, (source.height() - h) / 2, source.width(), h);
        }
        p.drawImage(QRectF(kMargin, y, imgW, imgH), d.viewport, source);
        p.restore();
    } else {
        p.fillRect(QRectF(kMargin, y, imgW, imgH), kPanel);
    }
    y += imgH + 8;

    // Legend
    if (!d.legend.isEmpty()) {
        QLinearGradient g(kMargin, 0, kMargin + 220, 0);
        for (int i = 0; i < d.legend.size(); ++i) {
            g.setColorAt(static_cast<double>(i) / (d.legend.size() - 1), d.legend[i]);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(QRectF(kMargin, y, 220, 8), 3, 3);
        p.setFont(font(7.5));
        p.setPen(kMuted);
        p.drawText(QRectF(kMargin, y + 10, 220, 12), Qt::AlignLeft, QString::number(d.range.min, 'f', 2));
        p.drawText(QRectF(kMargin, y + 10, 220, 12), Qt::AlignRight, QString::number(d.range.max, 'f', 2));
        p.drawText(QRectF(kMargin + 232, y - 2, 280, 12), Qt::AlignLeft | Qt::AlignVCenter, d.fieldName + (d.fieldUnit.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(d.fieldUnit)));
    }
    y += 34;

    // Configuration & results
    const double colW = (kPageW - 2 * kMargin - 20) / 2;
    const GridSpec& g = d.domain.grid;
    sectionTitle(p, QObject::tr("Configuration"), kMargin, y, colW);
    sectionTitle(p, QObject::tr("Results"), kMargin + colW + 20, y, colW);
    y += 26;
    const double yConfig = table(p,
        {
            { QObject::tr("Model"), d.modelName },
            { QObject::tr("Geometry"), QObject::tr("%L1 triangles, %2 objects").arg(d.triangles).arg(d.objects) },
            { QObject::tr("Vehicle length / yaw"), QStringLiteral("%1 m / %2°").arg(d.placement.lengthMeters, 0, 'f', 2).arg(d.placement.yawDegrees, 0, 'f', 1) },
            { QObject::tr("Wind speed"), QStringLiteral("%1 m/s (%2 km/h)").arg(d.flow.windSpeed, 0, 'f', 1).arg(d.flow.windSpeed * 3.6, 0, 'f', 0) },
            { QObject::tr("Reynolds (simulated / air)"), QStringLiteral("%L1 / %2").arg(d.flow.reynolds, 0, 'f', 0).arg(d.stats.reynoldsAir, 0, 'e', 2) },
            { QObject::tr("Grid"), QStringLiteral("%1 × %2 × %3 (%L4 cells)").arg(g.nx).arg(g.ny).arg(g.nz).arg(g.cellCount()) },
            { QObject::tr("Cell size"), QStringLiteral("%1 mm").arg(g.dx * 1000.0, 0, 'f', 0) },
            { QObject::tr("Frontal area"), QStringLiteral("%1 m²").arg(d.domain.frontalArea, 0, 'f', 2) },
            { QObject::tr("Smagorinsky constant"), QString::number(d.flow.smagorinsky, 'f', 2) },
            { QObject::tr("Rolling road"), d.flow.movingFloor ? QObject::tr("on") : QObject::tr("off") },
        },
        kMargin, y, colW);
    const double yResults = table(p,
        {
            { QObject::tr("Drag coefficient Cd"), QString::number(d.stats.cd, 'f', 3) },
            { QObject::tr("Lift coefficient Cl"), QString::number(d.stats.cl, 'f', 3) },
            { QObject::tr("Side force coefficient Cs"), QString::number(d.stats.cs, 'f', 3) },
            { QObject::tr("Drag force"), QStringLiteral("%L1 N").arg(d.stats.dragNewton, 0, 'f', 0) },
            { QObject::tr("Lift force"), QStringLiteral("%L1 N").arg(d.stats.liftNewton, 0, 'f', 0) },
            { QObject::tr("Drag power"), QStringLiteral("%1 kW").arg(d.stats.dragNewton * d.flow.windSpeed / 1000.0, 0, 'f', 1) },
            { QObject::tr("Simulated time"), QStringLiteral("%1 s").arg(d.stats.physicalTime, 0, 'f', 3) },
            { QObject::tr("Iterations"), QStringLiteral("%L1").arg(d.stats.step) },
            { QObject::tr("Throughput"), QStringLiteral("%1 MLUPS").arg(d.stats.mlups, 0, 'f', 0) },
            { QObject::tr("Peak velocity"), QStringLiteral("%1 × U∞").arg(d.stats.maxSpeed, 0, 'f', 2) },
        },
        kMargin + colW + 20, y, colW);
    y = std::max(yConfig, yResults) + 18;

    sectionTitle(p, QObject::tr("Force coefficient convergence"), kMargin, y, imgW);
    y += 26;
    chart(p, d.history, QRectF(kMargin, y, imgW, 120));
    y += 134;

    if (!d.notes.trimmed().isEmpty()) {
        sectionTitle(p, QObject::tr("Notes"), kMargin, y, imgW);
        y += 24;
        p.setFont(font(8.5));
        p.setPen(kInk);
        p.drawText(QRectF(kMargin, y, imgW, kPageH - y - 50), Qt::TextWordWrap, d.notes);
    }

    // Footer
    p.setPen(QPen(kRule, 0.6));
    p.drawLine(QPointF(kMargin, kPageH - 34), QPointF(kPageW - kMargin, kPageH - 34));
    p.setFont(font(7));
    p.setPen(kMuted);
    p.drawText(QRectF(kMargin, kPageH - 30, imgW, 14), Qt::AlignLeft | Qt::AlignVCenter,
        QObject::tr("Wind Tunnel %1 — results are indicative; lattice resolution limits boundary-layer accuracy.").arg(QStringLiteral(APPLICATION_VERSION_STR)));
    p.drawText(QRectF(kMargin, kPageH - 30, imgW, 14), Qt::AlignRight | Qt::AlignVCenter, QObject::tr("Page 1 of 1"));
    p.restore();
}

QFuture<QString> ReportService::exportPdf(ReportData data, const QString& path)
{
    QFuture<QString> future = QtConcurrent::run([data = std::move(data), path]() -> QString {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setResolution(300);
        writer.setTitle(data.title);
        writer.setCreator(QStringLiteral("Wind Tunnel %1").arg(QStringLiteral(APPLICATION_VERSION_STR)));
        QPainter painter;
        if (!painter.begin(&writer)) {
            return QObject::tr("Cannot write %1").arg(path);
        }
        paint(painter, data, QRectF(0, 0, writer.width(), writer.height()));
        painter.end();
        return {};
    });
    future.then(this, [this, path](const QString& error) {
        error.isEmpty() ? emit exported(path) : emit failed(error);
    });
    return future;
}

} // namespace fluid::app
