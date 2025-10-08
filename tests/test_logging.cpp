#include "logging/LogModel.hpp"
#include "logging/Logging.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

using namespace fluid::app::logging;

namespace {

QCoreApplication& application()
{
    static int argc = 1;
    static char name[] = "run-app-test";
    static char* argv[] = { name, nullptr };
    static QCoreApplication app(argc, argv);
    return app;
}

LogRecord record(spdlog::level::level_enum level, int i)
{
    LogRecord r;
    r.level = level;
    r.channel = QStringLiteral("test");
    r.message = QString::number(i);
    return r;
}

} // namespace

TEST_CASE("Log levels parse with a fallback", "[logging]")
{
    CHECK(parseLevel("debug", spdlog::level::info) == spdlog::level::debug);
    CHECK(parseLevel("warn", spdlog::level::info) == spdlog::level::warn);
    CHECK(parseLevel("off", spdlog::level::info) == spdlog::level::off);
    CHECK(parseLevel("nonsense", spdlog::level::info) == spdlog::level::info);
}

TEST_CASE("LogModel keeps the newest records within its capacity", "[logging]")
{
    LogModel model(16);
    std::vector<LogRecord> batch;
    for (int i = 0; i < 20; ++i) {
        batch.push_back(record(i % 2 ? spdlog::level::warn : spdlog::level::info, i));
    }
    model.append(std::move(batch));
    REQUIRE(model.rowCount() == 16);
    CHECK(model.record(0).message == QStringLiteral("4"));
    CHECK(model.record(15).message == QStringLiteral("19"));
    CHECK(model.count(spdlog::level::info) + model.count(spdlog::level::warn) == 16);
    CHECK(model.count(spdlog::level::warn) == 8);

    model.append({ record(spdlog::level::err, 20) });
    CHECK(model.rowCount() == 16);
    CHECK(model.count(spdlog::level::err) == 1);
    CHECK(model.data(model.index(15), LogModel::LevelRole).toInt() == static_cast<int>(spdlog::level::err));

    model.clear();
    CHECK(model.rowCount() == 0);
    CHECK(model.count(spdlog::level::warn) == 0);
}

TEST_CASE("Messages from worker threads reach the model in order", "[logging]")
{
    application();
    initialize(Options { spdlog::level::trace, false, {} });
    LogModel model(10000);
    model.attachToLogging();

    constexpr int kThreads = 4;
    constexpr int kMessages = 250;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t] {
            const auto logger = get("worker" + std::to_string(t));
            for (int i = 0; i < kMessages; ++i) {
                (i % 10 == 0) ? logger->warn("{}", i) : logger->debug("{}", i);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    QElapsedTimer timer;
    timer.start();
    while (model.rowCount() < kThreads * kMessages && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    REQUIRE(model.rowCount() == kThreads * kMessages);
    CHECK(model.count(spdlog::level::warn) == kThreads * kMessages / 10);

    // Records of each channel keep their emission order.
    std::vector<int> last(kThreads, -1);
    for (int row = 0; row < model.rowCount(); ++row) {
        const LogRecord& r = model.record(row);
        const int t = r.channel.back().digitValue();
        REQUIRE(r.message.toInt() > last[static_cast<std::size_t>(t)]);
        last[static_cast<std::size_t>(t)] = r.message.toInt();
    }
    shutdown();
}
