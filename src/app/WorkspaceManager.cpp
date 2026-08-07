#include "WorkspaceManager.h"
#include "common/logger/Logger.h"
#include "common/config/Config.h"

#include <filesystem>

WorkspaceManager::WorkspaceManager() {
}

WorkspaceManager::~WorkspaceManager() {
}

bool WorkspaceManager::initialize() {
    std::string basePath = Config::getInstance().getWorkspacePath();
    LOG_INFO("Initializing workspace at: {}", basePath);

    if (!createDirectoryIfNotExists(basePath)) return false;
    if (!createDirectoryIfNotExists(basePath + "/logs")) return false;
    if (!createDirectoryIfNotExists(basePath + "/data")) return false;

    return true;
}

bool WorkspaceManager::createDirectoryIfNotExists(const std::string& path) {
    try {
        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
            LOG_INFO("Created directory: {}", path);
        }
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("Failed to create directory: {}. Error: {}", path, e.what());
        return false;
    }
}
