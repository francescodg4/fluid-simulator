#include "MainWindow.hpp"
#include "logging/Logging.hpp"
#include "ui/Theme.hpp"
#include "version.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QStandardPaths>
#include <QSurfaceFormat>

#include <argparse/argparse.hpp>

#include <iostream>

namespace {

/** Creates a throw-away context to find out whether OpenGL runs on a CPU rasterizer. */
bool probeSoftwareRasterizer()
{
    QOffscreenSurface surface;
    surface.create();
    QOpenGLContext context;
    if (!context.create() || !context.makeCurrent(&surface)) {
        return false;
    }
    const QString renderer = QString::fromLatin1(reinterpret_cast<const char*>(context.functions()->glGetString(GL_RENDERER))).toLower();
    context.doneCurrent();
    return renderer.contains("llvmpipe") || renderer.contains("softpipe") || renderer.contains("swrast") || renderer.contains("swiftshader");
}

} // namespace

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("windtunnel", APPLICATION_VERSION_STR);
    program.add_description("Real-time virtual wind tunnel (lattice Boltzmann) with a Qt 6 / OpenGL viewport.");
    program.add_argument("--model").help("Wavefront OBJ model to load").default_value(std::string(WINDTUNNEL_DEFAULT_MODEL));
    program.add_argument("--resolution").help("cells along the tunnel (64 - 352)").default_value(0).scan<'i', int>();
    program.add_argument("--workspace").help("0 Layout, 1 Aerodynamics, 2 Flow Structures, 3 Smoke, 4 Wind Tunnel").default_value(0).scan<'i', int>();
    program.add_argument("--paused").help("do not start the solver automatically").default_value(false).implicit_value(true);
    program.add_argument("--screenshot").help("save a window screenshot after --delay seconds and quit").default_value(std::string());
    program.add_argument("--report").help("export a PDF report after --delay seconds and quit").default_value(std::string());
    program.add_argument("--delay").help("seconds before --screenshot / --report").default_value(8.0).scan<'g', double>();
    program.add_argument("--verbose").help("shortcut for --log-level debug").default_value(false).implicit_value(true);
    program.add_argument("--log-level").help("trace, debug, info, warn, error, critical or off").default_value(std::string("info"));
    program.add_argument("--log-file").help("rotating log file (default: application data folder; 'none' disables it)").default_value(std::string());

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << '\n' << program;
        return EXIT_FAILURE;
    }
    // OpenGL 3.3 core for the viewport; multisampling for smooth QPainter overlays.
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Wind Tunnel"));
    QApplication::setOrganizationName(QStringLiteral("WindTunnel"));
    QApplication::setApplicationVersion(QStringLiteral(APPLICATION_VERSION_STR));

    namespace logging = fluid::app::logging;
    logging::Options logOptions;
    logOptions.level = program.get<bool>("--verbose") ? spdlog::level::debug
                                                      : logging::parseLevel(program.get<std::string>("--log-level"), spdlog::level::info);
    const std::string logFile = program.get<std::string>("--log-file");
    if (logFile.empty()) {
        logOptions.file = (QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs/windtunnel.log")).toStdString();
    } else if (logFile != "none") {
        logOptions.file = logFile;
    }
    logging::initialize(logOptions);
    const auto log = logging::get(logging::channel::App);
    log->info("Wind Tunnel {} starting (Qt {}, log level {})", APPLICATION_VERSION_STR, qVersion(), spdlog::level::to_string_view(logOptions.level));
    if (!logging::logFile().empty()) {
        log->info("Writing log file {}", logging::logFile().string());
    }

    fluid::app::theme::apply(app);
    if (probeSoftwareRasterizer()) {
        // Multisampling the whole widget is prohibitively slow on CPU rasterizers; the scene
        // renderer switches to FXAA and the level-of-detail mesh on its own.
        logging::get(logging::channel::Render)->warn("Software OpenGL rasterizer detected: using performance rendering settings");
        format.setSamples(0);
        QSurfaceFormat::setDefaultFormat(format);
    }

    fluid::app::StartupOptions options;
    options.modelPath = QString::fromStdString(program.get<std::string>("--model"));
    options.resolution = program.get<int>("--resolution");
    options.workspace = program.get<int>("--workspace");
    options.autoRun = !program.get<bool>("--paused");
    options.screenshotPath = QString::fromStdString(program.get<std::string>("--screenshot"));
    options.reportPath = QString::fromStdString(program.get<std::string>("--report"));
    options.automationDelay = program.get<double>("--delay");
    if (!QFileInfo::exists(options.modelPath)) {
        logging::get(logging::channel::Io)->warn("Model '{}' not found — use File > Open Model", options.modelPath.toStdString());
        options.modelPath.clear();
    }

    int status = 0;
    {
        fluid::app::MainWindow window(options);
        window.show();
        status = app.exec();
    }
    log->info("Shutting down (exit code {})", status);
    logging::shutdown();
    return status;
}
