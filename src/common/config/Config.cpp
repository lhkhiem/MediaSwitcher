#include "Config.h"
#include "common/logger/Logger.h"
#include <fstream>
#include <iostream>

// Note: For parsing JSON, we would typically use a library like nlohmann/json.
// For milestone 1 skeleton, we mock the load/save.

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

bool Config::load(const std::string& filepath) {
    LOG_INFO("Loading config from: {}", filepath);
    // TODO: Parse actual JSON config
    return true;
}

bool Config::save(const std::string& filepath) {
    LOG_INFO("Saving config to: {}", filepath);
    // TODO: Write actual JSON config
    return true;
}

std::string Config::getWorkspacePath() const {
    return m_workspacePath;
}

void Config::setWorkspacePath(const std::string& path) {
    m_workspacePath = path;
}
