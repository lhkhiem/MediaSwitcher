#include "common/logger/Logger.h"
#include "common/config/AppInfo.h"
#include "app/WorkspaceManager.h"
#include "engine/diagnostics/MediaDiagnostics.h"
#include "engine/obs/ObsContext.h"
#include "engine/obs/ObsPlaybackBackend.h"
#include "engine/obs/ObsThumbnailService.h"
#include "ui/ObsDualMediaTestWindow.h"
#include "ui/ObsMediaTestWindow.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QIcon>
#include <QMessageBox>
#include <QSysInfo>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <unordered_set>

extern "C" {
#include <util/base.h>
}

namespace {
void logQtMessage(QtMsgType type, const QMessageLogContext&, const QString& message) {
    const auto logger = Logger::getLogger();
    if (!logger) return;
    const auto level = type == QtFatalMsg || type == QtCriticalMsg ? spdlog::level::err :
        type == QtWarningMsg ? spdlog::level::warn :
        type == QtDebugMsg ? spdlog::level::debug : spdlog::level::info;
    logger->log(level, "Qt: {}", message.toUtf8().constData());
}

void logObsMessage(int level, const char* format, va_list arguments, void*) {
    const auto logger = Logger::getLogger();
    if (!logger) return;
    const auto severity = level <= LOG_ERROR ? spdlog::level::err :
        level <= LOG_WARNING ? spdlog::level::warn :
        level <= LOG_INFO ? spdlog::level::info : spdlog::level::debug;
    if (!logger->should_log(severity)) return;
    const auto message = QString::vasprintf(format, arguments).trimmed().toUtf8();
    logger->log(severity, "libobs: {}", message.constData());
}

int startupFailure(const QString& reason) {
    if (!QCoreApplication::arguments().contains(QStringLiteral("--startup-smoke-test"))) {
        QMessageBox::critical(nullptr, QStringLiteral("MediaSwitcher"),
            reason + QStringLiteral("\n\nNhật ký chẩn đoán: ") +
            QDir::current().absoluteFilePath(QStringLiteral("logs/mediaswitcher.log")));
    }
    return 1;
}

QString obsMediaTestPath(const QStringList& arguments) {
    const QString prefix = QStringLiteral("--obs-media-test=");
    for (const QString& argument : arguments) {
        if (argument.startsWith(prefix)) return argument.mid(prefix.size());
    }
    return {};
}

QString obsDualMediaTestPath(const QStringList& arguments) {
    const QString prefix = QStringLiteral("--obs-dual-media-test=");
    for (const QString& argument : arguments) {
        if (argument.startsWith(prefix)) return argument.mid(prefix.size());
    }
    return {};
}

QString obsPauseSmokeTestPath(const QStringList& arguments) {
    const QString prefix = QStringLiteral("--obs-pause-smoke-test=");
    for (const QString& argument : arguments) {
        if (argument.startsWith(prefix)) return argument.mid(prefix.size());
    }
    return {};
}

QString obsFpsSmokeTestPath(const QStringList& arguments) {
    const QString prefix = QStringLiteral("--obs-fps-smoke-test=");
    for (const QString& argument : arguments) {
        if (argument.startsWith(prefix)) return argument.mid(prefix.size());
    }
    return {};
}

QString obsThumbnailSmokeTestPath(const QStringList& arguments) {
    const QString prefix = QStringLiteral("--obs-thumbnail-smoke-test=");
    for (const QString& argument : arguments) {
        if (argument.startsWith(prefix)) return argument.mid(prefix.size());
    }
    return {};
}

QString obsCueOnlySmokeTestPath(const QStringList& arguments) {
    const QString prefix = QStringLiteral("--obs-cue-only-smoke-test=");
    for (const QString& argument : arguments) {
        if (argument.startsWith(prefix)) return argument.mid(prefix.size());
    }
    return {};
}

void waitForObsLifecycle(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
}

int main(int argc, char* argv[]) {
    Logger::init();
    LOG_INFO("Starting MediaSwitcher...");
    qInstallMessageHandler(logQtMessage);
    base_set_log_handler(logObsMessage, nullptr);

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(AppInfo::COMPANY);
    QCoreApplication::setApplicationName(AppInfo::NAME);
    QCoreApplication::setApplicationVersion(AppInfo::VERSION);
    LOG_INFO("Platform: {}; kernel={}; architecture={}; Qt={}",
             QSysInfo::prettyProductName().toStdString(), QSysInfo::kernelVersion().toStdString(),
             QSysInfo::currentCpuArchitecture().toStdString(), qVersion());

    WorkspaceManager workspaceManager;
    if (!workspaceManager.initialize()) {
        LOG_ERROR("Failed to initialize workspace.");
        return startupFailure(QStringLiteral("Không thể tạo thư mục làm việc. Kiểm tra quyền ghi của thư mục ứng dụng."));
    }

    // Exercise the installed dependency chain and GPU without opening the workspace UI.
    if (QCoreApplication::arguments().contains(QStringLiteral("--startup-smoke-test"))) {
        ObsContext obsContext;
        const bool passed = obsContext.initialize();
        LOG_INFO("Startup smoke test: {}", passed ? "PASS" : "FAIL");
        return passed ? 0 : 1;
    }

    const QString iconPath = QCoreApplication::applicationDirPath() + "/app_icon.ico";
    QIcon appIcon(iconPath);
    if (appIcon.isNull()) appIcon = QIcon(":/app_icon.ico");
    if (!appIcon.isNull()) app.setWindowIcon(appIcon);

    const QString mediaTestPath = obsMediaTestPath(QCoreApplication::arguments());
    const QString dualMediaTestPath = obsDualMediaTestPath(QCoreApplication::arguments());
    const QString pauseSmokeTestPath = obsPauseSmokeTestPath(QCoreApplication::arguments());
    const QString fpsSmokeTestPath = obsFpsSmokeTestPath(QCoreApplication::arguments());
    const QString thumbnailSmokeTestPath = obsThumbnailSmokeTestPath(QCoreApplication::arguments());
    const QString cueOnlySmokeTestPath = obsCueOnlySmokeTestPath(QCoreApplication::arguments());
    if (!fpsSmokeTestPath.isEmpty()) {
        LOG_INFO("Startup mode: OBS Project FPS smoke test");
        ObsContext obsContext;
        if (!obsContext.initialize()) {
            LOG_ERROR("OBS Project FPS smoke test could not initialize OBS.");
            return -1;
        }

        ObsPlaybackBackend preview(obsContext);
        preview.setAudioOutputEnabled(false);
        if (!preview.open(std::filesystem::path(fpsSmokeTestPath.toStdWString()), true)) {
            LOG_ERROR("OBS Project FPS smoke test could not open the media file.");
            return -1;
        }

        waitForObsLifecycle(250);
        const ObsVideoFrameRate original = obsContext.videoFrameRate();
        bool passed = true;
        for (const ObsVideoFrameRate frameRate : ObsContext::supportedVideoFrameRates()) {
            if (!obsContext.setVideoFrameRate(frameRate)) {
                passed = false;
                break;
            }
            waitForObsLifecycle(50);
        }
        if (!obsContext.setVideoFrameRate(original)) passed = false;
        LOG_INFO("OBS Project FPS smoke: tested {} preset(s) with active media; result={}",
                 ObsContext::supportedVideoFrameRates().size(), passed ? "PASS" : "FAIL");
        preview.close();
        return passed ? 0 : 1;
    }

    if (!dualMediaTestPath.isEmpty()) {
        LOG_INFO("Startup mode: OBS dual media test");
        LOG_INFO("OBS compiled: yes");

        ObsContext obsContext;
        if (!obsContext.initialize()) {
            LOG_ERROR("OBS dual media test requested but OBS initialization failed.");
            return -1;
        }
        LOG_INFO("OBS initialized: yes");

        MediaDiagnostics::instance().start();
        ObsDualMediaTestWindow testWindow(obsContext, std::filesystem::path(dualMediaTestPath.toStdWString()));
        testWindow.show();
        LOG_INFO("OBS dual media test running for '{}'.", dualMediaTestPath.toStdString());
        const int result = app.exec();
        MediaDiagnostics::instance().stop();
        LOG_INFO("OBS dual media test exiting with code {}.", result);
        return result;
    }

    if (!mediaTestPath.isEmpty()) {
        LOG_INFO("Startup mode: OBS media test");
        LOG_INFO("OBS compiled: yes");

        ObsContext obsContext;
        if (!obsContext.initialize()) {
            LOG_ERROR("OBS media test requested but OBS initialization failed.");
            return -1;
        }
        LOG_INFO("OBS initialized: yes");

        ObsMediaTestWindow testWindow(obsContext, std::filesystem::path(mediaTestPath.toStdWString()));
        testWindow.show();
        LOG_INFO("OBS media test running for '{}'.", mediaTestPath.toStdString());
        const int result = app.exec();
        LOG_INFO("OBS media test exiting with code {}.", result);
        return result;
    }

    if (!cueOnlySmokeTestPath.isEmpty()) {
        LOG_INFO("Startup mode: OBS cue-only workflow smoke test");
        ObsContext obsContext;
        if (!obsContext.initialize()) {
            LOG_ERROR("OBS cue-only smoke test could not initialize OBS.");
            return -1;
        }
        ObsDualMediaTestWindow testWindow(obsContext);
        testWindow.show();
        QApplication::processEvents();
        const bool passed = testWindow.runCueOnlySmokeTest(
            std::filesystem::path(cueOnlySmokeTestPath.toStdWString()));
        LOG_INFO("OBS cue-only workflow smoke: result={}", passed ? "PASS" : "FAIL");
        return passed ? 0 : 1;
    }

    if (!thumbnailSmokeTestPath.isEmpty()) {
        LOG_INFO("Startup mode: OBS thumbnail smoke test");
        ObsContext obsContext;
        if (!obsContext.initialize()) {
            LOG_ERROR("OBS thumbnail smoke test could not initialize OBS.");
            return -1;
        }

        bool passed = true;
        std::unordered_set<quint64> fastCompleted;
        std::unordered_set<quint64> posterCompleted;
        int64_t slowestFastThumbnailMs = 0;
        const auto thumbnailStartedAt = std::chrono::steady_clock::now();
        QEventLoop loop;
        const auto connection = QObject::connect(
            &ObsThumbnailService::instance(), &ObsThumbnailService::thumbnailReady, &loop,
            [&loop, &passed, &fastCompleted, &posterCompleted, &slowestFastThumbnailMs,
             thumbnailStartedAt](quint64 sourceId, const QImage& image, qint64 positionMs) {
                passed = passed && sourceId >= 1 && sourceId <= 3 && !image.isNull() &&
                    image.width() <= 1280 && image.height() <= 720 && positionMs >= 0;
                if (image.width() <= 320 && image.height() <= 180) {
                    fastCompleted.insert(sourceId);
                    slowestFastThumbnailMs = std::max<int64_t>(slowestFastThumbnailMs,
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - thumbnailStartedAt).count());
                } else {
                    posterCompleted.insert(sourceId);
                }
                if (posterCompleted.size() == 3) loop.quit();
            });
        QTimer::singleShot(6000, &loop, &QEventLoop::quit);
        const std::filesystem::path thumbnailPath(thumbnailSmokeTestPath.toStdWString());
        for (uint64_t sourceId = 1; sourceId <= 3; ++sourceId) {
            ObsThumbnailService::instance().request(sourceId, thumbnailPath, ObsCatalogSourceType::VideoFile);
        }
        loop.exec();
        ObsThumbnailService::instance().waitForDone();
        QObject::disconnect(connection);
        passed = passed && fastCompleted.size() == 3 && posterCompleted.size() == 3 &&
            slowestFastThumbnailMs <= 1000;
        LOG_INFO("OBS thumbnail smoke: slowest fast tile={} ms; fast={}/3 poster={}/3.",
                 slowestFastThumbnailMs, fastCompleted.size(), posterCompleted.size());
        LOG_INFO("OBS thumbnail smoke: result={}", passed ? "PASS" : "FAIL");
        return passed ? 0 : 1;
    }

    if (!pauseSmokeTestPath.isEmpty()) {
        LOG_INFO("Startup mode: OBS pause smoke test");
        ObsContext obsContext;
        if (!obsContext.initialize()) {
            LOG_ERROR("OBS pause smoke test could not initialize OBS.");
            return -1;
        }

        ObsPlaybackBackend preview(obsContext);
        preview.setAudioOutputEnabled(false);
        if (!preview.open(std::filesystem::path(pauseSmokeTestPath.toStdWString()), true)) {
            LOG_ERROR("OBS pause smoke test could not open the media file.");
            return -1;
        }

        waitForObsLifecycle(250);
        preview.enforcePendingPause();
        waitForObsLifecycle(250);
        const auto firstState = preview.state();
        const auto firstPosition = preview.positionMs();
        waitForObsLifecycle(1000);
        const auto secondState = preview.state();
        const auto secondPosition = preview.positionMs();
        const bool passed = firstState == ObsPlaybackState::Paused && secondState == ObsPlaybackState::Paused &&
                            std::abs(secondPosition - firstPosition) <= 50;
        LOG_INFO("OBS pause smoke: state1={} position1={}ms state2={} position2={}ms result={}",
                 static_cast<int>(firstState), firstPosition, static_cast<int>(secondState), secondPosition,
                 passed ? "PASS" : "FAIL");
        preview.close();
        return passed ? 0 : 1;
    }

    {
        LOG_INFO("Startup mode: OBS application");
        LOG_INFO("OBS compiled: yes");
        ObsContext obsContext;
        if (!obsContext.initialize()) {
            LOG_ERROR("OBS application startup failed because OBS initialization failed.");
            return startupFailure(QStringLiteral("Không thể khởi tạo bộ máy phát media. Kiểm tra runtime, dữ liệu OBS và driver đồ họa Direct3D 11."));
        }
        LOG_INFO("OBS initialized: yes");

        MediaDiagnostics::instance().start();
        ObsDualMediaTestWindow mainWindow(obsContext);
        mainWindow.showNormal();
        mainWindow.raise();
        mainWindow.activateWindow();
        LOG_INFO("OBS application running with an empty Input Bank; visible={} windowState={} geometry={}x{}+{}+{}.",
                 mainWindow.isVisible(), static_cast<int>(mainWindow.windowState()),
                 mainWindow.width(), mainWindow.height(), mainWindow.x(), mainWindow.y());
        const int result = app.exec();
        MediaDiagnostics::instance().stop();
        LOG_INFO("OBS application exiting with code {}.", result);
        return result;
    }
}
