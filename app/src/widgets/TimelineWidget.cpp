#include "widgets/TimelineWidget.hpp"

#include "ui/Theme.hpp"
#include "widgets/ValueField.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace fluid::app {

class TimelineRuler : public QWidget {
public:
    explicit TimelineRuler(QWidget* parent)
        : QWidget(parent)
    {
        setMinimumHeight(44);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    std::uint64_t step = 0;
    std::uint64_t end = 20000;
    QVector<ForceSample> history;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = rect();
        p.fillRect(r, theme::kPanel);
        const QRectF track = r.adjusted(14, 20, -14, -4);
        auto xOf = [&](double s) { return track.left() + s / static_cast<double>(std::max<std::uint64_t>(1, end)) * track.width(); };

        // Header band with tick labels
        p.fillRect(QRectF(r.left(), r.top(), r.width(), 18), theme::kPanelAlt);
        const double niceSteps[] = { 100, 200, 250, 500, 1000, 2000, 2500, 5000, 10000, 20000, 50000 };
        double major = niceSteps[0];
        for (double s : niceSteps) {
            major = s;
            if (track.width() / (static_cast<double>(end) / s) > 70.0) {
                break;
            }
        }
        QFont f = font();
        f.setPointSizeF(7.5);
        p.setFont(f);
        for (double s = 0; s <= static_cast<double>(end) + 0.5; s += major / 5.0) {
            const double x = xOf(s);
            const bool isMajor = std::fmod(s, major) < 1e-6;
            p.setPen(QColor(255, 255, 255, isMajor ? 26 : 10));
            p.drawLine(QPointF(x, 18), QPointF(x, r.bottom()));
            if (isMajor) {
                p.setPen(theme::kTextDim);
                p.drawText(QRectF(x - 40, 1, 80, 16), Qt::AlignCenter, QString::number(s, 'f', 0));
            }
        }

        // Force coefficient curves
        if (history.size() > 1) {
            double lo = 1e30, hi = -1e30;
            const int first = history.size() > 20 ? history.size() / 5 : 0;
            for (int i = first; i < history.size(); ++i) {
                lo = std::min({ lo, history[i].cd, history[i].cl });
                hi = std::max({ hi, history[i].cd, history[i].cl });
            }
            const double pad = std::max(0.05, (hi - lo) * 0.15);
            lo -= pad;
            hi += pad;
            auto yOf = [&](double v) { return track.bottom() - (std::clamp(v, lo, hi) - lo) / (hi - lo) * track.height(); };
            QPainterPath cd, cl;
            for (int i = 0; i < history.size(); ++i) {
                const double x = xOf(static_cast<double>(history[i].step));
                i == 0 ? cd.moveTo(x, yOf(history[i].cd)) : cd.lineTo(x, yOf(history[i].cd));
                i == 0 ? cl.moveTo(x, yOf(history[i].cl)) : cl.lineTo(x, yOf(history[i].cl));
            }
            p.setClipRect(track);
            p.setPen(QPen(QColor(0xf0, 0x9a, 0x4a, 150), 1.2));
            p.drawPath(cl);
            p.setPen(QPen(QColor(0x6c, 0xb2, 0xff, 200), 1.4));
            p.drawPath(cd);
            p.setClipping(false);
        }

        // Region past the end
        const double xEnd = xOf(static_cast<double>(end));
        p.fillRect(QRectF(xEnd, 18, r.right() - xEnd, r.height() - 18), QColor(0, 0, 0, 70));

        // Playhead
        const double x = xOf(static_cast<double>(std::min(step, end)));
        p.setPen(QPen(theme::kAccent, 1.5));
        p.drawLine(QPointF(x, 16), QPointF(x, r.bottom()));
        const QString label = QString::number(step);
        const qreal w = QFontMetricsF(f).horizontalAdvance(label) + 12;
        p.setPen(Qt::NoPen);
        p.setBrush(theme::kAccent);
        p.drawRoundedRect(QRectF(x - w / 2, 1, w, 16), 4, 4);
        p.setPen(Qt::white);
        p.drawText(QRectF(x - w / 2, 1, w, 16), Qt::AlignCenter, label);
    }
};

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("headerBar"));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 3, 8, 3);
    h->setSpacing(4);

    auto makeButton = [&](theme::Icon icon, const QString& tip) {
        auto* b = new QToolButton(bar);
        b->setIcon(theme::icon(icon));
        b->setToolTip(tip);
        b->setProperty("flat", true);
        b->setIconSize(QSize(15, 15));
        h->addWidget(b);
        return b;
    };
    auto* playback = new QLabel(tr("Playback  ·  Solver"), bar);
    playback->setObjectName(QStringLiteral("dimLabel"));
    h->addWidget(playback);
    h->addSpacing(12);
    h->addStretch(1);
    connect(makeButton(theme::Icon::SkipBack, tr("Reset flow to free stream")), &QToolButton::clicked, this, &TimelineWidget::resetRequested);
    m_play = makeButton(theme::Icon::Play, tr("Run / pause the solver (Space)"));
    connect(m_play, &QToolButton::clicked, this, [this] { emit playToggled(!m_running); });
    connect(makeButton(theme::Icon::StepForward, tr("Advance one frame")), &QToolButton::clicked, this, &TimelineWidget::stepRequested);
    h->addSpacing(10);
    m_counter = new QLabel(QStringLiteral("0"), bar);
    m_counter->setMinimumWidth(70);
    m_counter->setAlignment(Qt::AlignCenter);
    m_counter->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px; padding: 2px 8px; font-weight: 600;").arg(theme::kField.name()));
    h->addWidget(m_counter);
    m_time = new QLabel(QStringLiteral("0.000 s"), bar);
    m_time->setObjectName(QStringLiteral("dimLabel"));
    m_time->setMinimumWidth(70);
    h->addWidget(m_time);
    h->addStretch(1);
    auto* start = new ValueField(tr("Start"), 0, 0, 0, 0, bar);
    start->setEnabled(false);
    start->setSliderVisible(false);
    start->setFixedWidth(110);
    h->addWidget(start);
    m_end = new ValueField(tr("End"), 500, 200000, 20000, 0, bar);
    m_end->setSliderVisible(false);
    m_end->setLogarithmic(true);
    m_end->setFixedWidth(130);
    m_end->setToolTip(tr("The solver pauses automatically at this iteration"));
    h->addWidget(m_end);
    layout->addWidget(bar);

    m_ruler = new TimelineRuler(this);
    layout->addWidget(m_ruler, 1);
    connect(m_end, &ValueField::valueChanged, this, [this](double v) {
        m_ruler->end = static_cast<std::uint64_t>(v);
        m_ruler->update();
    });
    setMinimumHeight(78);
}

void TimelineWidget::setRunning(bool running)
{
    m_running = running;
    m_play->setIcon(theme::icon(running ? theme::Icon::Pause : theme::Icon::Play));
}

void TimelineWidget::setProgress(std::uint64_t step, double physicalTime)
{
    m_counter->setText(QLocale().toString(static_cast<qulonglong>(step)));
    m_time->setText(QStringLiteral("%1 s").arg(physicalTime, 0, 'f', 3));
    m_ruler->step = step;
    m_ruler->update();
}

void TimelineWidget::setHistory(const QVector<ForceSample>& history)
{
    m_ruler->history = history;
    m_ruler->update();
}

std::uint64_t TimelineWidget::endStep() const
{
    return static_cast<std::uint64_t>(m_end->value());
}

} // namespace fluid::app
