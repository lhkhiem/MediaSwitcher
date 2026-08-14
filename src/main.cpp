#include "common/logger/Logger.h"
#include "common/config/AppInfo.h"
#include "app/WorkspaceManager.h"
#include "ui/MainWindow.h"
#include "engine/audio/AudioEngine.h"
#ifdef MEDIASWITCHER_ENABLE_OBS
#include "engine/obs/ObsContext.h"
#include "engine/obs/ObsPlaybackBackend.h"
#include "ui/ObsDualMediaTestWindow.h"
#include "ui/ObsMediaTestWindow.h"
#endif

#include <QApplication>
#include <QEventLoop>
#include <QIcon>
#include <QTimer>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#define IDI_APPICON 101
#endif

#ifdef MEDIASWITCHER_ENABLE_OBS
namespace {
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

void waitForObsLifecycle(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
}
#endif

int main(int argc, char* argv[]) {
    Logger::init();
    LOG_INFO("Starting MediaSwitcher...");

    WorkspaceManager workspaceManager;
    if (!workspaceManager.initialize()) {
        LOG_ERROR("Failed to initialize workspace.");
        return -1;
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(AppInfo::COMPANY);
    QCoreApplication::setApplicationName(AppInfo::NAME);
    QCoreApplication::setApplicationVersion(AppInfo::VERSION);

    const QString iconPath = QCoreApplication::applicationDirPath() + "/app_icon.ico";
    QIcon appIcon(iconPath);
    if (appIcon.isNull()) appIcon = QIcon(":/app_icon.ico");
    if (!appIcon.isNull()) app.setWindowIcon(appIcon);

#ifdef MEDIASWITCHER_ENABLE_OBS
    const QString mediaTestPath = obsMediaTestPath(QCoreApplication::arguments());
    const QString dualMediaTestPath = obsDualMediaTestPath(QCoreApplication::arguments());
    const QString pauseSmokeTestPath = obsPauseSmokeTestPath(QCoreApplication::arguments());
    const QString fpsSmokeTestPath = obsFpsSmokeTestPath(QCoreApplication::arguments());
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

        ObsDualMediaTestWindow testWindow(obsContext, std::filesystem::path(dualMediaTestPath.toStdWString()));
        testWindow.show();
        LOG_INFO("OBS dual media test running for '{}'.", dualMediaTestPath.toStdString());
        const int result = app.exec();
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
            return -1;
        }
        LOG_INFO("OBS initialized: yes");

        ObsDualMediaTestWindow mainWindow(obsContext);
        mainWindow.show();
        LOG_INFO("OBS application running with an empty Input Bank.");
        const int result = app.exec();
        LOG_INFO("OBS application exiting with code {}.", result);
        return result;
    }
#endif

    if (!AudioEngine::instance().initialize()) {
        LOG_ERROR("Failed to initialize AudioEngine (XAudio2). Audio will be silent.");
    }

#ifndef MEDIASWITCHER_ENABLE_OBS
    LOG_INFO("Startup mode: Legacy");
    LOG_INFO("OBS compiled: no");
    LOG_INFO("OBS initialized: no");
#endif

    int result = 0;
    {
        MainWindow mainWindow;
        mainWindow.show();

#ifdef _WIN32
        const auto hwnd = reinterpret_cast<HWND>(mainWindow.winId());
        const auto setIcon = [hwnd](int metric, WPARAM size) {
            HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, metric, metric, LR_SHARED));
            if (icon) SendMessageW(hwnd, WM_SETICON, size, reinterpret_cast<LPARAM>(icon));
        };
        setIcon(GetSystemMetrics(SM_CXICON), ICON_BIG);
        setIcon(GetSystemMetrics(SM_CXSMICON), ICON_SMALL);
#endif

        LOG_INFO("MediaSwitcher running.");
        result = app.exec();
    }

    AudioEngine::instance().shutdown();
    LOG_INFO("MediaSwitcher exiting with code {}", result);
    return result;
}
