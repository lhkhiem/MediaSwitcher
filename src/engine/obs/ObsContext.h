#pragma once

#include <filesystem>
#include <string>
#include <vector>

class ObsContext final {
public:
    ObsContext() = default;
    ~ObsContext();

    ObsContext(const ObsContext&) = delete;
    ObsContext& operator=(const ObsContext&) = delete;

    bool initialize();
    void shutdown();

    bool isInitialized() const { return m_initialized; }
    const std::vector<std::string>& loadedModules() const { return m_loadedModules; }

private:
    bool initializeVideo();
    bool initializeAudio();
    bool loadModules();

    bool m_initialized{false};
    std::filesystem::path m_runtimeRoot;
    std::filesystem::path m_moduleConfigPath;
    std::vector<std::string> m_loadedModules;
};