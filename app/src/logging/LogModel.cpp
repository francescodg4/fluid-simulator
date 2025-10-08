#include "logging/LogModel.hpp"

#include "logging/Logging.hpp"

#include <QDateTime>
#include <QMetaObject>

#include <chrono>

namespace fluid::app::logging {

LogModel::LogModel(int capacity, QObject* parent)
    : QAbstractListModel(parent)
    , m_capacity(std::max(16, capacity))
{
}

LogModel::~LogModel()
{
    if (m_sink) {
        m_sink->detach();
        removeSink(m_sink);
    }
}

void LogModel::attachToLogging()
{
    if (m_sink) {
        return;
    }
    m_sink = std::make_shared<LogModelSink>(this);
    addSink(m_sink);
}

int LogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_records.size());
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const LogRecord& r = record(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return format(r);
    case LevelRole:
        return static_cast<int>(r.level);
    case ChannelRole:
        return r.channel;
    case TimestampRole:
        return r.timestamp;
    case MessageRole:
        return r.message;
    default:
        return {};
    }
}

QString LogModel::format(const LogRecord& record)
{
    const auto level = spdlog::level::to_string_view(record.level);
    return QStringLiteral("%1 [%2] [%3] %4")
        .arg(QDateTime::fromMSecsSinceEpoch(record.timestamp).toString(QStringLiteral("HH:mm:ss.zzz")),
            QString::fromLatin1(level.data(), static_cast<int>(level.size())), record.channel, record.message);
}

void LogModel::append(std::vector<LogRecord> records)
{
    if (records.empty()) {
        return;
    }
    // Keep only what fits, then evict the oldest rows to make room.
    if (records.size() > static_cast<std::size_t>(m_capacity)) {
        records.erase(records.begin(), records.end() - m_capacity);
    }
    const std::size_t overflow = m_records.size() + records.size() > static_cast<std::size_t>(m_capacity)
        ? m_records.size() + records.size() - static_cast<std::size_t>(m_capacity)
        : 0;
    if (overflow > 0) {
        beginRemoveRows({}, 0, static_cast<int>(overflow) - 1);
        for (std::size_t i = 0; i < overflow; ++i) {
            --m_counts[static_cast<std::size_t>(m_records.front().level)];
            m_records.pop_front();
        }
        endRemoveRows();
    }
    const int first = static_cast<int>(m_records.size());
    beginInsertRows({}, first, first + static_cast<int>(records.size()) - 1);
    for (LogRecord& r : records) {
        ++m_counts[static_cast<std::size_t>(r.level)];
        m_records.push_back(std::move(r));
    }
    endInsertRows();
    emit countsChanged();
}

void LogModel::clear()
{
    beginResetModel();
    m_records.clear();
    m_counts.fill(0);
    endResetModel();
    emit countsChanged();
}

// ---------------------------------------------------------------------------------------------
LogModelSink::LogModelSink(LogModel* model)
    : m_model(model)
{
}

void LogModelSink::detach()
{
    std::lock_guard lock(mutex_);
    m_model = nullptr;
    m_pending.clear();
}

void LogModelSink::sink_it_(const spdlog::details::log_msg& message)
{
    if (!m_model) {
        return;
    }
    LogRecord r;
    r.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(message.time.time_since_epoch()).count();
    r.level = message.level;
    r.channel = QString::fromUtf8(message.logger_name.data(), static_cast<qsizetype>(message.logger_name.size()));
    r.message = QString::fromUtf8(message.payload.data(), static_cast<qsizetype>(message.payload.size()));
    r.thread = message.thread_id;
    m_pending.push_back(std::move(r));

    if (!m_scheduled) {
        m_scheduled = true;
        // Posting only enqueues an event, so this is safe from any thread, including the GUI one.
        QMetaObject::invokeMethod(m_model, [weak = weak_from_this()] {
            if (auto self = weak.lock()) {
                self->deliver();
            }
        }, Qt::QueuedConnection);
    }
}

void LogModelSink::deliver()
{
    std::vector<LogRecord> batch;
    LogModel* model = nullptr;
    {
        std::lock_guard lock(mutex_);
        batch.swap(m_pending);
        m_scheduled = false;
        model = m_model;
    }
    if (model) {
        model->append(std::move(batch));
    }
}

} // namespace fluid::app::logging
