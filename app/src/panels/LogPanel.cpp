#include "panels/LogPanel.hpp"

#include "logging/Logging.hpp"
#include "ui/Theme.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace fluid::app {
namespace {

    using Level = spdlog::level::level_enum;

    const char* badgeText(Level level)
    {
        switch (level) {
        case spdlog::level::trace:
            return "TRC";
        case spdlog::level::debug:
            return "DBG";
        case spdlog::level::info:
            return "INF";
        case spdlog::level::warn:
            return "WRN";
        case spdlog::level::err:
            return "ERR";
        case spdlog::level::critical:
            return "CRT";
        default:
            return "";
        }
    }

    QIcon dotIcon(const QColor& color)
    {
        QPixmap pm(12, 12);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QRectF(2, 2, 8, 8));
        return QIcon(pm);
    }

    /** Paints one record: time, level badge, channel and the message coloured by level. */
    class LogDelegate : public QStyledItemDelegate {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;

        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
        {
            return { option.rect.width(), QFontMetrics(option.font).height() + 5 };
        }

        void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            p->save();
            const QRect r = option.rect;
            const auto level = static_cast<Level>(index.data(logging::LogModel::LevelRole).toInt());
            if (option.state & QStyle::State_Selected) {
                p->fillRect(r, theme::kSelection);
            } else if (level >= spdlog::level::err) {
                p->fillRect(r, QColor(0xff, 0x5c, 0x6c, 28));
            } else if (level == spdlog::level::warn) {
                p->fillRect(r, QColor(0xf5, 0xa6, 0x23, 18));
            } else if (index.row() % 2) {
                p->fillRect(r, QColor(255, 255, 255, 6));
            }

            const QFontMetrics fm(option.font);
            p->setFont(option.font);
            int x = r.left() + 8;
            const QString time = QDateTime::fromMSecsSinceEpoch(index.data(logging::LogModel::TimestampRole).toLongLong()).toString(QStringLiteral("HH:mm:ss.zzz"));
            p->setPen(theme::kTextDim);
            p->drawText(QRect(x, r.top(), fm.horizontalAdvance(time) + 4, r.height()), Qt::AlignVCenter, time);
            x += fm.horizontalAdvance(QStringLiteral("00:00:00.000")) + 10;

            // Level badge
            const QColor color = LogPanel::levelColor(level);
            const QRectF badge(x, r.top() + 2, fm.horizontalAdvance(QStringLiteral("WRN")) + 10, r.height() - 4);
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(level == spdlog::level::critical ? color : QColor(color.red(), color.green(), color.blue(), 45));
            p->drawRoundedRect(badge, 3, 3);
            p->setPen(level == spdlog::level::critical ? QColor(Qt::white) : color);
            p->drawText(badge, Qt::AlignCenter, QString::fromLatin1(badgeText(level)));
            x = static_cast<int>(badge.right()) + 8;

            const QString channel = index.data(logging::LogModel::ChannelRole).toString();
            p->setPen(QColor(0x5f, 0xc8, 0xe8));
            p->drawText(QRect(x, r.top(), 90, r.height()), Qt::AlignVCenter, fm.elidedText(channel, Qt::ElideRight, 86));
            x += 90;

            p->setPen(level == spdlog::level::info ? theme::kText : color);
            const QString message = index.data(logging::LogModel::MessageRole).toString().section(QLatin1Char('\n'), 0, 0);
            p->drawText(QRect(x, r.top(), r.right() - x - 6, r.height()), Qt::AlignVCenter, fm.elidedText(message, Qt::ElideRight, r.right() - x - 6));
            p->restore();
        }
    };

} // namespace

// ---------------------------------------------------------------------------------------------
LogFilterProxy::LogFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    m_enabled.fill(true);
}

void LogFilterProxy::setLevelEnabled(spdlog::level::level_enum level, bool enabled)
{
    m_enabled[static_cast<std::size_t>(level)] = enabled;
    invalidateFilter();
}

void LogFilterProxy::setText(const QString& text)
{
    m_text = text;
    invalidateFilter();
}

bool LogFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    const int level = index.data(logging::LogModel::LevelRole).toInt();
    if (level < 0 || level >= spdlog::level::n_levels || !m_enabled[static_cast<std::size_t>(level)]) {
        return false;
    }
    if (m_text.isEmpty()) {
        return true;
    }
    return index.data(logging::LogModel::MessageRole).toString().contains(m_text, Qt::CaseInsensitive)
        || index.data(logging::LogModel::ChannelRole).toString().contains(m_text, Qt::CaseInsensitive);
}

// ---------------------------------------------------------------------------------------------
QColor LogPanel::levelColor(spdlog::level::level_enum level)
{
    switch (level) {
    case spdlog::level::trace:
        return { 0x6b, 0x7a, 0x93 };
    case spdlog::level::debug:
        return { 0x9a, 0x8c, 0xf0 };
    case spdlog::level::info:
        return theme::kAccentBright;
    case spdlog::level::warn:
        return theme::kWarning;
    case spdlog::level::err:
        return { 0xff, 0x5c, 0x6c };
    case spdlog::level::critical:
        return { 0xd6, 0x28, 0x3b };
    default:
        return theme::kTextDim;
    }
}

LogPanel::LogPanel(logging::LogModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("headerBar"));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 3, 8, 3);
    h->setSpacing(4);
    auto* icon = new QLabel(bar);
    icon->setPixmap(theme::icon(theme::Icon::Report, theme::kTextDim).pixmap(14, 14));
    h->addWidget(icon);
    auto* title = new QLabel(tr("Info Log"), bar);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    h->addWidget(title);
    h->addSpacing(10);

    m_proxy = new LogFilterProxy(this);
    m_proxy->setSourceModel(m_model);

    m_filters = {
        { tr("Trace"), { spdlog::level::trace }, nullptr },
        { tr("Debug"), { spdlog::level::debug }, nullptr },
        { tr("Info"), { spdlog::level::info }, nullptr },
        { tr("Warning"), { spdlog::level::warn }, nullptr },
        { tr("Error"), { spdlog::level::err, spdlog::level::critical }, nullptr },
    };
    for (LevelFilter& f : m_filters) {
        auto* b = new QToolButton(bar);
        b->setCheckable(true);
        b->setChecked(true);
        b->setIcon(dotIcon(levelColor(f.levels.front())));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setProperty("flat", true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tr("Show %1 messages").arg(f.name.toLower()));
        connect(b, &QToolButton::toggled, this, [this, levels = f.levels](bool on) {
            for (const Level level : levels) {
                m_proxy->setLevelEnabled(level, on);
            }
        });
        h->addWidget(b);
        f.button = b;
    }
    h->addSpacing(8);

    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText(tr("Filter messages or channels…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(260);
    connect(m_search, &QLineEdit::textChanged, m_proxy, &LogFilterProxy::setText);
    h->addWidget(m_search, 1);
    h->addStretch(1);

    m_summary = new QLabel(bar);
    m_summary->setObjectName(QStringLiteral("dimLabel"));
    h->addWidget(m_summary);

    auto* verbosityLabel = new QLabel(tr("Capture"), bar);
    verbosityLabel->setObjectName(QStringLiteral("dimLabel"));
    h->addWidget(verbosityLabel);
    m_verbosity = new QComboBox(bar);
    m_verbosity->addItems({ tr("Trace"), tr("Debug"), tr("Info"), tr("Warning"), tr("Error") });
    m_verbosity->setToolTip(tr("Minimum level recorded by every channel (console, file and this log)"));
    m_verbosity->setCurrentIndex(std::min(static_cast<int>(logging::level()), 4));
    connect(m_verbosity, &QComboBox::currentIndexChanged, this, [](int i) {
        const auto level = static_cast<Level>(i);
        logging::setLevel(level);
        logging::get(logging::channel::App)->info("Log capture level set to {}", spdlog::level::to_string_view(level));
    });
    h->addWidget(m_verbosity);

    auto tool = [&](theme::Icon glyph, const QString& tip) {
        auto* b = new QToolButton(bar);
        b->setIcon(theme::icon(glyph));
        b->setToolTip(tip);
        b->setProperty("flat", true);
        b->setFocusPolicy(Qt::NoFocus);
        h->addWidget(b);
        return b;
    };
    m_autoScroll = tool(theme::Icon::StepForward, tr("Auto-scroll to the newest message"));
    m_autoScroll->setCheckable(true);
    m_autoScroll->setChecked(true);
    connect(tool(theme::Icon::Report, tr("Copy selected messages (Ctrl+C)")), &QToolButton::clicked, this, &LogPanel::copySelection);
    connect(tool(theme::Icon::Folder, tr("Open the log file")), &QToolButton::clicked, this, [] {
        const auto file = logging::logFile();
        if (!file.empty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(file.string())));
        }
    });
    connect(tool(theme::Icon::Reset, tr("Clear the log")), &QToolButton::clicked, m_model, &logging::LogModel::clear);
    layout->addWidget(bar);

    m_view = new QListView(this);
    m_view->setModel(m_proxy);
    m_view->setItemDelegate(new LogDelegate(m_view));
    m_view->setUniformItemSizes(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(8.5);
    m_view->setFont(mono);
    m_view->setStyleSheet(QStringLiteral("QListView { background: %1; }").arg(theme::kHeader.name()));
    layout->addWidget(m_view, 1);

    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, [this] {
        if (m_autoScroll->isChecked()) {
            m_view->scrollToBottom();
        }
    });
    // Scrolling up pauses auto-scroll; returning to the bottom resumes it.
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        auto* bar = m_view->verticalScrollBar();
        if (bar->isSliderDown()) {
            m_autoScroll->setChecked(value >= bar->maximum() - 2);
        }
    });
    connect(m_model, &logging::LogModel::countsChanged, this, &LogPanel::updateCounts);
    updateCounts();
    m_view->scrollToBottom();
    setMinimumHeight(90);
}

void LogPanel::updateCounts()
{
    for (LevelFilter& f : m_filters) {
        int count = 0;
        for (const Level level : f.levels) {
            count += m_model->count(level);
        }
        f.button->setText(QStringLiteral("%1 %2").arg(f.name).arg(count));
    }
    m_summary->setText(tr("%L1 / %L2").arg(m_model->rowCount()).arg(m_model->capacity()));
}

void LogPanel::copySelection()
{
    QModelIndexList rows = m_view->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex& a, const QModelIndex& b) { return a.row() < b.row(); });
    QStringList lines;
    for (const QModelIndex& index : rows) {
        lines << m_model->format(m_model->record(m_proxy->mapToSource(index).row()));
    }
    if (!lines.isEmpty()) {
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    }
}

void LogPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Copy)) {
        copySelection();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace fluid::app
