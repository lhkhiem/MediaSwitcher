#include "common/logger/Logger.h"
#include "common/config/AppInfo.h"
#include "app/WorkspaceManager.h"
#include "ui/MainWindow.h"
#include "engine/audio/AudioEngine.h"

#include <QApplication>
#include <iostream>

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

    // 4. Initialize XAudio2 Engine
    if (!AudioEngine::instance().initialize()) {
        LOG_ERROR("Failed to initialize AudioEngine (XAudio2). Audio will be silent.");
    }

    // 5. Create and Show Main Window
    MainWindow mainWindow;
    mainWindow.show();

    LOG_INFO("MediaSwitcher running.");
    int result = app.exec();

    // 6. Shutdown AudioEngine before exit
    AudioEngine::instance().shutdown();

    LOG_INFO("MediaSwitcher exiting with code {}", result);
    return result;
}
