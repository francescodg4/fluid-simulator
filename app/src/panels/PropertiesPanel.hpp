#pragma once

#include "model/SceneDocument.hpp"
#include "simulation/SimulationController.hpp"

#include <QElapsedTimer>
#include <QMap>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;

namespace fluid::app {

class ValueField;
class TransferFunctionEditor;
class ForceChartWidget;

/**
 * Properties editor (right column, below the outliner) with icon tabs:
 * visualisation (scalar field, range, colormap, transfer function), analysis (aerodynamic
 * coefficients and convergence) and report (PDF export).
 */
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    PropertiesPanel(SceneDocument* document, SimulationController* simulation, QWidget* parent = nullptr);

    QString reportTitle() const;
    QString reportAuthor() const;
    QString reportNotes() const;
    void setReportPreview(const QImage& image);
    void showTab(int index);

signals:
    void exportReportRequested();
    void previewReportRequested();

private:
    QWidget* buildVisualizationTab();
    QWidget* buildAnalysisTab();
    QWidget* buildReportTab();
    void syncFromDocument();
    void onSnapshot(const SnapshotPtr& snapshot);
    void updateHistogram();
    void autoRange();
    void applyOpacityPreset(int preset);

    SceneDocument* m_doc;
    SimulationController* m_simulation;
    bool m_syncing = false;
    QTabWidget* m_tabs = nullptr;
    QElapsedTimer m_histogramClock;
    QElapsedTimer m_metricsClock;

    // Visualisation
    QComboBox* m_field = nullptr;
    QComboBox* m_colormap = nullptr;
    ValueField* m_min = nullptr;
    ValueField* m_max = nullptr;
    TransferFunctionEditor* m_editor = nullptr;

    // Analysis
    QMap<QString, QLabel*> m_metrics;
    ForceChartWidget* m_chart = nullptr;

    // Report
    QLineEdit* m_title = nullptr;
    QLineEdit* m_author = nullptr;
    QPlainTextEdit* m_notes = nullptr;
    QLabel* m_preview = nullptr;
};

} // namespace fluid::app
