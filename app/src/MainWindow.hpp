#pragma once

#include "model/SceneDocument.hpp"
#include "services/MeshProvider.hpp"
#include "services/ReportService.hpp"
#include "simulation/SimulationController.hpp"

#include <QMainWindow>
#include <QTimer>

class QLabel;
class QTabBar;
class QToolButton;

namespace fluid::app {

namespace logging {
    class LogModel;
}

class LogPanel;
class ViewportView;
class FluidFlowPanel;
class OutlinerPanel;
class PropertiesPanel;
class TimelineWidget;

struct StartupOptions {
    QString modelPath;
    int resolution = 0; ///< 0 = default
    bool autoRun = true;
    int workspace = 0;
    QString screenshotPath; ///< capture after @ref automationDelay seconds and quit
    QString reportPath; ///< export a PDF report after @ref automationDelay seconds and quit
    double automationDelay = 8.0;
    logging::LogModel* log = nullptr; ///< session log shown in the Info Log panel
};

/**
 * Application shell: Blender-like layout (top bar with workspaces, 3D viewport with its
 * floating sidebar, outliner + properties column, timeline, status bar) wiring the document,
 * the services and the asynchronous simulation together.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const StartupOptions& options, QWidget* parent = nullptr);
    ~MainWindow() override;

    void openModel(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* buildTopBar();
    QWidget* buildViewportHeader();
    void buildMenus(class QMenuBar* bar);
    void applyWorkspace(int index);
    void requestRebuild();
    void togglePlay();
    void saveScreenshot();
    ReportData collectReport();
    void exportReport(const QString& path = {});
    void previewReport();
    void updateStatus();
    void runAutomation();
    void syncHeaderButtons();

    StartupOptions m_options;
    SceneDocument* m_doc = nullptr;
    SimulationController* m_simulation = nullptr;
    MeshProvider* m_meshProvider = nullptr;
    ReportService* m_reports = nullptr;

    ViewportView* m_viewport = nullptr;
    FluidFlowPanel* m_sidePanel = nullptr;
    OutlinerPanel* m_outliner = nullptr;
    PropertiesPanel* m_properties = nullptr;
    TimelineWidget* m_timeline = nullptr;
    LogPanel* m_logPanel = nullptr;
    QTabBar* m_workspaces = nullptr;
    QList<QToolButton*> m_shadingButtons;
    QToolButton* m_streamButton = nullptr;
    QToolButton* m_particleButton = nullptr;
    QToolButton* m_sliceButton = nullptr;
    QToolButton* m_volumeButton = nullptr;
    QLabel* m_statusModel = nullptr;
    QLabel* m_statusGrid = nullptr;
    QLabel* m_statusPerf = nullptr;
    QLabel* m_statusGl = nullptr;

    QTimer m_rebuildTimer;
    bool m_resumeAfterBuild = true;
    bool m_automationDone = false;
};

} // namespace fluid::app
