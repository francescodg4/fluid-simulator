#pragma once

#include "simulation/SimulationController.hpp"

#include <QWidget>

class QLabel;
class QToolButton;

namespace fluid::app {

class ValueField;
class TimelineRuler;

/**
 * Bottom timeline (Blender style): transport controls, iteration counter, start / end range and
 * a ruler that plots the drag / lift convergence under the playhead.
 */
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setRunning(bool running);
    void setProgress(std::uint64_t step, double physicalTime);
    void setHistory(const QVector<ForceSample>& history);
    std::uint64_t endStep() const;

signals:
    void playToggled(bool play);
    void stepRequested();
    void resetRequested();

private:
    QToolButton* m_play = nullptr;
    QLabel* m_counter = nullptr;
    QLabel* m_time = nullptr;
    ValueField* m_end = nullptr;
    TimelineRuler* m_ruler = nullptr;
    bool m_running = false;
};

} // namespace fluid::app
