#pragma once

#include <QAbstractListModel>
#include <QString>

#include <spdlog/sinks/base_sink.h>

#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace fluid::app::logging {

struct LogRecord {
    qint64 timestamp = 0; ///< milliseconds since the epoch
    spdlog::level::level_enum level = spdlog::level::info;
    QString channel;
    QString message;
    quint64 thread = 0;
};

class LogModelSink;

/**
 * Bounded list model of log records for views. Lives on the GUI thread; records arrive in
 * batches from LogModelSink, which may be fed from any thread.
 */
class LogModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        LevelRole = Qt::UserRole + 1,
        ChannelRole,
        TimestampRole,
        MessageRole,
    };

    explicit LogModel(int capacity = 5000, QObject* parent = nullptr);
    ~LogModel() override;

    /** Registers a sink with the logging facility so every channel feeds this model. */
    void attachToLogging();

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    const LogRecord& record(int row) const { return m_records[static_cast<std::size_t>(row)]; }
    void append(std::vector<LogRecord> records);
    void clear();
    int capacity() const { return m_capacity; }
    /** Number of retained records of a level. */
    int count(spdlog::level::level_enum level) const { return m_counts[static_cast<std::size_t>(level)]; }

    static QString format(const LogRecord& record);

signals:
    void countsChanged();

private:
    std::deque<LogRecord> m_records;
    int m_capacity;
    std::array<int, spdlog::level::n_levels> m_counts {};
    std::shared_ptr<LogModelSink> m_sink;
};

/**
 * spdlog sink that converts messages into LogRecords and hands them to a LogModel in batches
 * through the model's event loop (one queued call per batch, whatever the logging rate).
 */
class LogModelSink : public spdlog::sinks::base_sink<std::mutex>, public std::enable_shared_from_this<LogModelSink> {
public:
    explicit LogModelSink(LogModel* model);
    /** Called by the model before it is destroyed; later messages are dropped. */
    void detach();

protected:
    void sink_it_(const spdlog::details::log_msg& message) override;
    void flush_() override { }

private:
    void deliver();

    LogModel* m_model;
    std::vector<LogRecord> m_pending;
    bool m_scheduled = false;
};

} // namespace fluid::app::logging
