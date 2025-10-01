#pragma once

#include "model/SceneDocument.hpp"

#include <QFrame>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTabWidget;

namespace fluid::app {

class ValueField;

/**
 * The in-viewport sidebar ("N panel"), modelled on the Fluid Flow add-on panel:
 * simulation type presets, vehicle placement, collision objects, solver and emitter settings
 * on the first tab; display toggles on the second.
 */
class FluidFlowPanel : public QFrame {
    Q_OBJECT
public:
    explicit FluidFlowPanel(SceneDocument* document, QWidget* parent = nullptr);

    void setDomainInfo(const DomainInfo& domain);

signals:
    void createSimulationRequested();

private:
    QWidget* buildSimulationTab();
    QWidget* buildViewTab();
    void syncFromDocument();
    void syncObjects();
    void updateTypeButtons();
    void applyFlow(const std::function<void(FlowSettings&)>& edit);
    void applyTracers(const std::function<void(TracerSettings&)>& edit);
    void applyView(const std::function<void(ViewSettings&)>& edit);
    void applyTunnel(const std::function<void(TunnelSettings&)>& edit);
    void applyPlacement(const std::function<void(ModelPlacement&)>& edit);

    SceneDocument* m_doc;
    bool m_syncing = false;
    QTabWidget* m_tabs = nullptr;

    // Simulation tab
    QList<QPushButton*> m_typeButtons;
    QLabel* m_domainLabel = nullptr;
    QLabel* m_emitterLabel = nullptr;
    QComboBox* m_forward = nullptr;
    ValueField* m_length = nullptr;
    ValueField* m_yaw = nullptr;
    QListWidget* m_objects = nullptr;
    ValueField* m_speed = nullptr;
    ValueField* m_reynolds = nullptr;
    ValueField* m_steps = nullptr;
    ValueField* m_resolution = nullptr;
    ValueField* m_smagorinsky = nullptr;
    ValueField* m_turbulence = nullptr;
    ValueField* m_lattice = nullptr;
    QCheckBox* m_rollingRoad = nullptr;
    ValueField* m_upstream = nullptr;
    ValueField* m_downstream = nullptr;
    ValueField* m_height = nullptr;
    ValueField* m_width = nullptr;
    ValueField* m_seedsX = nullptr;
    ValueField* m_seedsY = nullptr;
    ValueField* m_rakeWidth = nullptr;
    ValueField* m_rakeHeight = nullptr;
    ValueField* m_rakePosition = nullptr;
    ValueField* m_maxPoints = nullptr;
    ValueField* m_particleCount = nullptr;

    // View tab
    QList<QPushButton*> m_shadingButtons;
    QComboBox* m_field = nullptr;
    QCheckBox* m_showStreamlines = nullptr;
    ValueField* m_lineWidth = nullptr;
    ValueField* m_lineOpacity = nullptr;
    QCheckBox* m_animate = nullptr;
    QCheckBox* m_showParticles = nullptr;
    ValueField* m_particleSize = nullptr;
    QCheckBox* m_showSlice = nullptr;
    QComboBox* m_sliceAxis = nullptr;
    ValueField* m_slicePosition = nullptr;
    ValueField* m_sliceOpacity = nullptr;
    QCheckBox* m_showVolume = nullptr;
    ValueField* m_volumeDensity = nullptr;
    QCheckBox* m_showDomain = nullptr;
    QCheckBox* m_showFloor = nullptr;
    QCheckBox* m_showRake = nullptr;
    QComboBox* m_quality = nullptr;
};

} // namespace fluid::app
