#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ObsVideoFrameRate {
    uint32_t numerator{60000};
    uint32_t denominator{1001};

    bool operator==(const ObsVideoFrameRate&) const = default;
};

class ObsContext final {
public:
    ObsContext() = default;
    ~ObsContext();

    ObsContext(const ObsContext&) = delete;
    ObsContext& operator=(const ObsContext&) = delete;

    bool initialize();
    void shutdown();
    bool setVideoFrameRate(ObsVideoFrameRate frameRate);

    bool isInitialized() const { return m_initialized; }
    ObsVideoFrameRate videoFrameRate() const { return m_videoFrameRate; }
    const std::vector<std::string>& loadedModules() const { return m_loadedModules; }
    static const std::array<ObsVideoFrameRate, 8>& supportedVideoFrameRates();

private:
    void loadVideoFrameRate();
    void saveVideoFrameRate() const;
    bool initializeVideo();
    bool initializeAudio();
    bool loadModules();

    bool m_initialized{false};
    ObsVideoFrameRate m_videoFrameRate;
    std::filesystem::path m_runtimeRoot;
    std::filesystem::path m_moduleConfigPath;
    std::vector<std::string> m_loadedModules;
};
