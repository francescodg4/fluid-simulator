#pragma once

#include "logging/LogModel.hpp"

#include <QSortFilterProxyModel>
#include <QWidget>

#include <array>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QToolButton;

namespace fluid::app {

/** Filters log records by level (a set of enabled levels) and by free text (message or channel). */
class LogFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit LogFilterProxy(QObject* parent = nullptr);
    void setLevelEnabled(spdlog::level::level_enum level, bool enabled);
    bool isLevelEnabled(spdlog::level::level_enum level) const { return m_enabled[static_cast<std::size_t>(level)]; }
    void setText(const QString& text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    std::array<bool, spdlog::level::n_levels> m_enabled;
    QString m_text;
};

/**
 * The session log with coloured levels, per-level filter toggles
 * with counts, text search, capture verbosity, auto-scroll, copy and clear.
 */
class LogPanel : public QWidget {
    Q_OBJECT
public:
    explicit LogPanel(logging::LogModel* model, QWidget* parent = nullptr);

    /** Colour used for a level's text and badge. */
    static QColor levelColor(spdlog::level::level_enum level);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct LevelFilter {
        QString name;
        std::vector<spdlog::level::level_enum> levels; ///< e.g. "Error" covers error + critical
        QToolButton* button = nullptr;
    };

    void updateCounts();
    void copySelection();

    logging::LogModel* m_model;
    LogFilterProxy* m_proxy = nullptr;
    QListView* m_view = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_verbosity = nullptr;
    QToolButton* m_autoScroll = nullptr;
    QLabel* m_summary = nullptr;
    std::vector<LevelFilter> m_filters;
};

} // namespace fluid::app
