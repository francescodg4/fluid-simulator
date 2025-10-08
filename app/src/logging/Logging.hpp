#pragma once

#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace fluid::app::logging {

namespace channel {
    inline constexpr const char* App = "app";
    inline constexpr const char* Io = "io";
    inline constexpr const char* Simulation = "simulation";
    inline constexpr const char* Render = "render";
    inline constexpr const char* Report = "report";
    inline constexpr const char* Qt = "qt";
} // namespace channel

struct Options {
    spdlog::level::level_enum level = spdlog::level::info;
    bool console = true;
    /** Rotating log file; empty disables file logging. */
    std::filesystem::path file;
};

/**
 * Every channel logger writes into one distribution sink, so sinks (console, rotating file and
 * UI sinks attached later) are shared by all subsystems and can be added or removed at runtime.
 * Qt's own messages are routed to the "qt" channel.
 */
void initialize(const Options& options);

/** Flushes and releases every logger. */
void shutdown();

/** Logger of a channel, created on first use and sharing the common sinks. */
std::shared_ptr<spdlog::logger> get(const std::string& channel);

void addSink(const spdlog::sink_ptr& sink);
void removeSink(const spdlog::sink_ptr& sink);

/** Minimum level captured by every channel. */
void setLevel(spdlog::level::level_enum level);
spdlog::level::level_enum level();

/** Active log file (empty when file logging is disabled or failed). */
std::filesystem::path logFile();

spdlog::level::level_enum parseLevel(std::string_view text, spdlog::level::level_enum fallback);

} // namespace fluid::app::logging
