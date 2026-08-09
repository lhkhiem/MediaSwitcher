#pragma once

#include "IMediaSource.h"
#include "engine/decoder/FFmpegDecoder.h"
#include "engine/frame/FramePool.h"
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QString>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

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

    const std::string& filePath() const { return m_filePath; }

private:
    void decodeWorkerLoop();

    std::string m_filePath;
    std::atomic<bool> m_opened{false};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_loop{true};
    std::atomic<double> m_seekTarget{-1.0};
    std::atomic<float> m_volume{1.0f};
    std::atomic<bool> m_muted{false};

    FFmpegDecoder m_decoder;   // Video decode only
    FramePool m_framePool;

    std::thread m_workerThread;
    std::atomic<bool> m_running{false};

    std::mutex m_frameMutex;
    std::shared_ptr<Frame> m_currentFrame;
    std::shared_ptr<Frame> m_lastValidFrame;

    // Qt Multimedia — handles audio playback completely (codec, buffer, output)
    QMediaPlayer* m_audioPlayer{nullptr};
    QAudioOutput* m_audioOutput{nullptr};

    // High resolution master wall clock for video frame pacing
    std::chrono::high_resolution_clock::time_point m_playbackStartTime;
    double m_playbackStartPts{0.0};
    bool m_clockInitialized{false};
};
