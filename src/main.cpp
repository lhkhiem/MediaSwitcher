#include "common/logger/Logger.h"
#include "common/config/AppInfo.h"
#include "app/WorkspaceManager.h"
#include "ui/MainWindow.h"
#include "engine/audio/AudioEngine.h"
#ifdef MEDIASWITCHER_ENABLE_OBS
#include "engine/obs/ObsContext.h"
#include "ui/ObsDualMediaTestWindow.h"
#include "ui/ObsMediaTestWindow.h"
#endif

#include <QApplication>
#include <QIcon>
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

    LOG_INFO("Startup mode: Legacy");
    LOG_INFO("OBS compiled: yes");
    LOG_INFO("OBS initialized: no");
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
