#pragma once

#include "simulation/SimulationController.hpp"
#include "simulation/SimulationTypes.hpp"

#include <QFuture>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVector>

class QPainter;

namespace fluid::app {

/** Plain data gathered from the application for a report */
struct ReportData {
    QString title;
    QString author;
    QString notes;
    QString modelName;
    std::size_t triangles = 0;
    std::size_t vertices = 0;
    std::size_t objects = 0;
    QImage viewport;
    DomainInfo domain;
    SolverStats stats;
    FlowSettings flow;
    TunnelSettings tunnel;
    ModelPlacement placement;
    QVector<ForceSample> history;
    QString fieldName;
    QString fieldUnit;
    ScalarRange range;
    QVector<QColor> legend; ///< colours sampled along the transfer function
};

/** Centralised report generation (PDF). Rendering happens on a worker thread. */
class ReportService : public QObject {
    Q_OBJECT
public:
    explicit ReportService(QObject* parent = nullptr);

    /** Writes the report asynchronously; the future yields an empty string or an error message. */
    QFuture<QString> exportPdf(ReportData data, const QString& path);

    /** Paints the whole report page into @p painter (page rectangle in device units). */
    static void paint(QPainter& painter, const ReportData& data, const QRectF& page);

signals:
    void exported(const QString& path);
    void failed(const QString& message);
};

} // namespace fluid::app
