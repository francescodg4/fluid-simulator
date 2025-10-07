#include "logging/Logging.hpp"

#include <QString>
#include <QtGlobal>

#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdio>
#include <mutex>

namespace fluid::app::logging {
namespace {

    constexpr const char* kPattern = "[%H:%M:%S.%e] [%^%-8l%$] [%-10n] %v";
    constexpr std::size_t kMaxFileSize = 5 * 1024 * 1024;
    constexpr std::size_t kMaxFiles = 3;

    struct State {
        std::mutex mutex;
        std::shared_ptr<spdlog::sinks::dist_sink_mt> sinks;
        spdlog::level::level_enum level = spdlog::level::info;
        std::filesystem::path file;
    };

    State& state()
    {
        static State s;
        return s;
    }

    void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
        const auto logger = get(channel::Qt);
        const std::string text = context.category && std::string_view(context.category) != "default"
            ? std::string(context.category) + ": " + message.toStdString()
            : message.toStdString();
        switch (type) {
        case QtDebugMsg:
            logger->debug(text);
            break;
        case QtInfoMsg:
            logger->info(text);
            break;
        case QtWarningMsg:
            logger->warn(text);
            break;
        case QtCriticalMsg:
            logger->error(text);
            break;
        case QtFatalMsg:
            logger->critical(text);
            logger->flush();
            break;
        }
    }

    std::shared_ptr<spdlog::logger> createLocked(State& s, const std::string& name)
    {
        if (auto existing = spdlog::get(name)) {
            return existing;
        }
        auto logger = std::make_shared<spdlog::logger>(name, s.sinks);
        logger->set_level(s.level);
        logger->flush_on(spdlog::level::warn);
        spdlog::register_logger(logger);
        return logger;
    }

} // namespace

void initialize(const Options& options)
{
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        s.level = options.level;
        s.sinks = std::make_shared<spdlog::sinks::dist_sink_mt>();

        if (options.console) {
            auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console->set_pattern(kPattern);
            s.sinks->add_sink(console);
        }
        if (!options.file.empty()) {
            try {
                std::filesystem::create_directories(options.file.parent_path());
                auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(options.file.string(), kMaxFileSize, kMaxFiles);
                file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%-8l] [%-10n] [thread %t] %v");
                s.sinks->add_sink(file);
                s.file = options.file;
            } catch (const std::exception& e) {
                s.file.clear();
                std::fprintf(stderr, "Cannot open log file %s: %s\n", options.file.string().c_str(), e.what());
            }
        }

        spdlog::drop_all();
        spdlog::set_default_logger(createLocked(s, channel::App));
    }
    qInstallMessageHandler(qtMessageHandler);
    get(channel::App)->debug("Logging initialised (level {}, file '{}')", spdlog::level::to_string_view(options.level), s.file.string());
}

void shutdown()
{
    qInstallMessageHandler(nullptr);
    spdlog::shutdown();
}

std::shared_ptr<spdlog::logger> get(const std::string& channel)
{
    if (auto existing = spdlog::get(channel)) {
        return existing;
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (!s.sinks) {
        return spdlog::default_logger(); // not initialised (e.g. unit tests)
    }
    return createLocked(s, channel);
}

void addSink(const spdlog::sink_ptr& sink)
{
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (s.sinks) {
        s.sinks->add_sink(sink);
    }
}

void removeSink(const spdlog::sink_ptr& sink)
{
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (s.sinks) {
        s.sinks->remove_sink(sink);
    }
}

void setLevel(spdlog::level::level_enum level)
{
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        s.level = level;
    }
    spdlog::set_level(level); // applies to every registered channel
}

spdlog::level::level_enum level()
{
    State& s = state();
    std::lock_guard lock(s.mutex);
    return s.level;
}

std::filesystem::path logFile()
{
    State& s = state();
    std::lock_guard lock(s.mutex);
    return s.file;
}

spdlog::level::level_enum parseLevel(std::string_view text, spdlog::level::level_enum fallback)
{
    const auto level = spdlog::level::from_str(std::string(text));
    // from_str returns "off" for unknown names; only accept it when explicitly requested.
    return level == spdlog::level::off && text != "off" ? fallback : level;
}

} // namespace fluid::app::logging
