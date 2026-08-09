#include "common/logger/Logger.h"
#include "common/config/AppInfo.h"
#include "app/WorkspaceManager.h"
#include "ui/MainWindow.h"
#include "engine/audio/AudioEngine.h"

#include <QApplication>
#include <QIcon>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#define IDI_APPICON 101   // Must match resource.rc
#endif

int main(int argc, char *argv[]) {
    // 1. Initialize Logger
    Logger::init();
    LOG_INFO("Starting MediaSwitcher...");

    // 2. Initialize Workspace
    WorkspaceManager workspaceManager;
    if (!workspaceManager.initialize()) {
        LOG_ERROR("Failed to initialize workspace.");
        return -1;
    }

    // 3. Initialize Qt Application
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(AppInfo::COMPANY);
    QCoreApplication::setApplicationName(AppInfo::NAME);
    QCoreApplication::setApplicationVersion(AppInfo::VERSION);

    // Set application icon — load from the .ico file embedded alongside the exe.
    // This ensures icon appears on title bar, taskbar, and Alt+Tab.
    // The .ico is copied to the exe directory as part of the build.
    {
        QString exeDir = QCoreApplication::applicationDirPath();
        QString icoPath = exeDir + "/app_icon.ico";
        QIcon appIcon(icoPath);
        if (appIcon.isNull()) {
            // Fallback: try relative path (dev mode, running from build root)
            appIcon = QIcon(":/app_icon.ico");
        }
        if (!appIcon.isNull()) {
            app.setWindowIcon(appIcon);
        }
    }

    // 4. Initialize XAudio2 Engine
    if (!AudioEngine::instance().initialize()) {
        LOG_ERROR("Failed to initialize AudioEngine (XAudio2). Audio will be silent.");
    }

    // 5. Create and Show Main Window
    MainWindow mainWindow;
    mainWindow.show();

#ifdef _WIN32
    // Force-set the window icon via Win32 WM_SETICON.
    // This is the definitive way to make the Windows taskbar show the correct icon.
    // QApplication::setWindowIcon() alone is not sufficient for the taskbar button.
    {
        HICON hIconBig = (HICON)LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDI_APPICON),   // numeric ID 101
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),     // 32x32
            GetSystemMetrics(SM_CYICON),
            LR_SHARED
        );
        HICON hIconSmall = (HICON)LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDI_APPICON),   // numeric ID 101
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),   // 16x16
            GetSystemMetrics(SM_CYSMICON),
            LR_SHARED
        );
        HWND hwnd = (HWND)mainWindow.winId();
        if (hIconBig)   SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIconBig);
        if (hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    }
#endif

    LOG_INFO("MediaSwitcher running.");
    int result = app.exec();

    // 6. Shutdown AudioEngine before exit
    AudioEngine::instance().shutdown();

    LOG_INFO("MediaSwitcher exiting with code {}", result);
    return result;
}

