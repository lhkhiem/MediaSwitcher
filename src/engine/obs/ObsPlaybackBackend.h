#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <string>

class ObsContext;
struct obs_source;
struct obs_view;
struct calldata;
typedef struct obs_source obs_source_t;
typedef struct obs_view obs_view_t;
typedef struct calldata calldata_t;

enum class ObsPlaybackState {
    None, Opening, Buffering, Playing, Paused, Stopped, Ended, Error,
};

class ObsPlaybackBackend final {
public:
    explicit ObsPlaybackBackend(ObsContext& context);
    ~ObsPlaybackBackend();
    ObsPlaybackBackend(const ObsPlaybackBackend&) = delete;
    ObsPlaybackBackend& operator=(const ObsPlaybackBackend&) = delete;

    bool open(const std::filesystem::path& path, bool startPaused = false);
    void close();
    void play();
    void pause();
    void enforcePendingPause();
    void stop();
    bool seekMs(int64_t milliseconds);
    int64_t positionMs() const;
    int64_t durationMs() const;
    ObsPlaybackState state() const;
    bool isAvailable() const;
    bool isOpen() const;
    const std::filesystem::path& mediaPath() const { return m_path; }
    void setLooping(bool enabled);
    bool isLooping() const { return m_looping; }
    bool hasEnded() const { return m_mediaEnded.load(); }
    void setAudioOutputEnabled(bool enabled);
    bool isAudioOutputEnabled() const { return m_audioOutputEnabled; }
    obs_source_t* nativeSource() const { return m_source; }
    void setRenderSource(obs_source_t* source);
    void resetRenderSource();
    void render(uint32_t width, uint32_t height) const;
    void logDiagnostics() const;

private:
    static ObsPlaybackState mapState(int obsState);
    static void onMediaStarted(void* data, calldata_t* calldata);
    static void onMediaEnded(void* data, calldata_t* calldata);
    bool setAudioMonitoring(bool enabled);
    void connectMediaSignals();
    void disconnectMediaSignals();
    void enforcePendingSeek();

    ObsContext& m_context;
    obs_source_t* m_source{nullptr};
    obs_view_t* m_view{nullptr};
    std::filesystem::path m_path;
    bool m_looping{false};
    bool m_audioOutputEnabled{true};
    bool m_audioMonitoringEnabled{false};
    bool m_sourceActive{false};
    bool m_mediaSignalsConnected{false};
    std::atomic_bool m_pauseRequested{false};
    std::atomic_bool m_mediaEnded{false};
    std::atomic_int64_t m_pendingSeekMs{-1};
    std::atomic_int m_pendingSeekAttempts{0};
};
