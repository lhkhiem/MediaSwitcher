#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

class ObsContext;
struct obs_source;
struct obs_view;
typedef struct obs_source obs_source_t;
typedef struct obs_view obs_view_t;

enum class ObsPlaybackState {
    None, Opening, Buffering, Playing, Paused, Stopped, Ended, Error,
};

class ObsPlaybackBackend final {
public:
    explicit ObsPlaybackBackend(ObsContext& context);
    ~ObsPlaybackBackend();
    ObsPlaybackBackend(const ObsPlaybackBackend&) = delete;
    ObsPlaybackBackend& operator=(const ObsPlaybackBackend&) = delete;

    bool open(const std::filesystem::path& path);
    void close();
    void play();
    void pause();
    void stop();
    bool seekMs(int64_t milliseconds);
    int64_t positionMs() const;
    int64_t durationMs() const;
    ObsPlaybackState state() const;
    bool isAvailable() const;
    bool isOpen() const;
    void render(uint32_t width, uint32_t height) const;
    void logDiagnostics() const;

private:
    static ObsPlaybackState mapState(int obsState);
    bool setAudioMonitoring(bool enabled);

    ObsContext& m_context;
    obs_source_t* m_source{nullptr};
    obs_view_t* m_view{nullptr};
    std::filesystem::path m_path;
    bool m_audioMonitoringEnabled{false};
    bool m_sourceActive{false};
    bool m_mediaSignalsConnected{false};
};
