#pragma once

#include "IMediaSource.h"
#include "engine/decoder/FFmpegDecoder.h"
#include "engine/frame/FramePool.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

class FileSource : public IMediaSource {
public:
    explicit FileSource(const std::string& filePath = "");
    ~FileSource() override;

    bool open() override;
    void close() override;

    std::shared_ptr<Frame> getFrame() override;

    double durationSeconds() const override;
    double positionSeconds() const override;
    void seekToSeconds(double seconds) override;

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

    FFmpegDecoder m_decoder;
    FramePool m_framePool;

    std::thread m_workerThread;
    std::atomic<bool> m_running{false};

    std::mutex m_frameMutex;
    std::shared_ptr<Frame> m_currentFrame;
    std::shared_ptr<Frame> m_lastValidFrame;
};
