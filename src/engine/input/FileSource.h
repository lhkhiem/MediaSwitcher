#pragma once

#include "IMediaSource.h"
#include "engine/decoder/FFmpegDecoder.h"
#include "engine/frame/FramePool.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <vector>

// Controls how aggressively a FileSource decodes.
// Active: full-rate decode (source is PVW or PGM)
// Idle:   minimal decode (source is not visible, conserve RAM/CPU)
enum class DecodeMode { Active, Idle };

class FileSource : public IMediaSource {
public:
    explicit FileSource(const std::string& filePath = "");
    ~FileSource() override;

    bool open() override;
    void close() override;

    std::shared_ptr<Frame> getFrame() override;

    void setVolume(float vol) override;
    float volume() const override { return m_volume.load(); }
    void setMuted(bool mute) override;
    bool isMuted() const override { return m_muted.load(); }

    double durationSeconds() const override;
    double positionSeconds() const override;
    void seekToSeconds(double seconds) override;
    void loopToBeginning() override;

    void setLoop(bool loop) override { m_loop = loop; }
    bool isLoop() const override { return m_loop; }

    void play() override;
    void pause() override;
    bool isPlaying() const override { return m_playing; }

    // Audio routing: enable/disable submitting decoded audio to AudioEngine
    void setAudioActive(bool active) override;
    bool isAudioActive() const override { return m_audioActive.load(); }

    const std::string& filePath() const { return m_filePath; }

    // Decode mode: Active = full rate (PVW/PGM), Idle = minimal RAM/CPU
    void setDecodeMode(DecodeMode mode);
    DecodeMode decodeMode() const { return m_decodeMode.load(); }

private:
    void decodeWorkerLoop();
    void drainAndSubmitAudio();  // Drain FFmpegDecoder audio queue → AudioEngine

    std::string m_filePath;
    std::atomic<bool> m_opened{false};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_loop{true};
    std::atomic<double> m_seekTarget{-1.0};
    std::atomic<float> m_volume{1.0f};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_audioActive{false};  // True when this is the PGM source

    FFmpegDecoder m_decoder;   // Video + Audio decode
    FramePool m_framePool;

    std::thread m_workerThread;
    std::atomic<bool> m_running{false};

    std::mutex m_frameMutex;
    std::shared_ptr<Frame> m_currentFrame;
    std::shared_ptr<Frame> m_lastValidFrame;

    std::atomic<DecodeMode> m_decodeMode{DecodeMode::Idle};

    // High resolution master wall clock for video frame pacing
    std::chrono::high_resolution_clock::time_point m_playbackStartTime;
    double m_playbackStartPts{0.0};
    bool m_clockInitialized{false};
};
