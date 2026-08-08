#include "FileSource.h"
#include "engine/audio/AudioEngine.h"
#include "common/logger/Logger.h"
#include <chrono>
#include <algorithm>

FileSource::FileSource(const std::string& filePath)
    : m_filePath(filePath)
{
}

FileSource::~FileSource() {
    close();
}

bool FileSource::open() {
    if (m_opened) return true;

    if (m_filePath.empty()) return false;

    if (!m_decoder.open(m_filePath)) {
        LOG_ERROR("FileSource: Failed to open decoder for '{}'", m_filePath);
        return false;
    }

    m_opened = true;
    m_playing = false; // Start paused at frame 0 (vMix standard)
    m_running = true;
    m_workerThread = std::thread(&FileSource::decodeWorkerLoop, this);

    LOG_INFO("FileSource: Opened '{}' paused at frame 0.", m_filePath);
    return true;
}

void FileSource::close() {
    if (!m_opened) return;

    m_running = false;
    m_playing = false;

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    m_decoder.close();
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_currentFrame.reset();
        m_lastValidFrame.reset();
    }
    {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        m_audioBuffer.clear();
    }

    m_opened = false;
    LOG_INFO("FileSource: Closed '{}'.", m_filePath);
}

void FileSource::play() {
    m_playing = true;
}

void FileSource::pause() {
    m_playing = false;
}

double FileSource::durationSeconds() const {
    return m_decoder.durationSeconds();
}

double FileSource::positionSeconds() const {
    return m_decoder.currentPositionSeconds();
}

void FileSource::seekToSeconds(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    m_seekTarget.store(seconds);
    {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        m_audioBuffer.clear();
    }
}

std::shared_ptr<Frame> FileSource::getFrame() {
    if (!m_opened) return nullptr;

    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (m_currentFrame) {
        m_lastValidFrame = m_currentFrame;
    }
    return m_lastValidFrame;
}

size_t FileSource::getAudioSamples(float* buffer, size_t maxSamples) {
    if (!m_opened || !buffer || maxSamples == 0) return 0;

    std::lock_guard<std::mutex> lock(m_audioMutex);
    if (m_audioBuffer.empty()) return 0;

    size_t count = (std::min)(maxSamples, m_audioBuffer.size());
    float gain = m_muted.load() ? 0.0f : m_volume.load();

    for (size_t i = 0; i < count; ++i) {
        buffer[i] = m_audioBuffer[i] * gain;
    }
    m_audioBuffer.erase(m_audioBuffer.begin(), m_audioBuffer.begin() + count);

    return count;
}

void FileSource::decodeWorkerLoop() {
    double fps = m_decoder.fps();
    if (fps <= 0.0) fps = 30.0;
    int frameDelayMs = static_cast<int>(1000.0 / fps);
    if (frameDelayMs < 5) frameDelayMs = 5;

    while (m_running) {
        double seekSec = m_seekTarget.exchange(-1.0);
        if (seekSec >= 0.0) {
            m_decoder.seekToSeconds(seekSec);
            m_clockInitialized = false;
            auto frame = m_framePool.acquire(m_decoder.width(), m_decoder.height(), PixelFormat::RGBA32);
            if (m_decoder.decodeNextFrame(*frame)) {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_currentFrame = frame;
                m_lastValidFrame = frame;
            }
        }

        if (!m_playing) {
            m_clockInitialized = false;
            // Decode initial frame 0 once if not decoded yet
            bool hasFrame = false;
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                hasFrame = (m_currentFrame != nullptr);
            }
            if (!hasFrame) {
                auto frame = m_framePool.acquire(m_decoder.width(), m_decoder.height(), PixelFormat::RGBA32);
                if (m_decoder.decodeNextFrame(*frame)) {
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    m_currentFrame = frame;
                    m_lastValidFrame = frame;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        // Decode audio samples using demuxer queue (Rate-throttled to 0.5s max buffer)
        if (m_decoder.hasAudio()) {
            bool needMoreAudio = false;
            {
                std::lock_guard<std::mutex> lock(m_audioMutex);
                needMoreAudio = (m_audioBuffer.size() < 48000); // 0.5s max buffer
            }

            if (needMoreAudio) {
                std::vector<float> audioPcm;
                m_decoder.decodeAudioSamples(audioPcm);
                if (!audioPcm.empty()) {
                    std::lock_guard<std::mutex> lock(m_audioMutex);
                    m_audioBuffer.insert(m_audioBuffer.end(), audioPcm.begin(), audioPcm.end());
                }
            }
        }

        // Decode video frame
        auto frame = m_framePool.acquire(m_decoder.width(), m_decoder.height(), PixelFormat::RGBA32);
        bool gotVideo = m_decoder.decodeNextFrame(*frame);

        if (gotVideo) {
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_currentFrame = frame;
                m_lastValidFrame = frame;
            }

            // High Precision Master Clock Pacing Algorithm (1.000x Speed Guarantee)
            if (!m_clockInitialized) {
                m_playbackStartTime = std::chrono::high_resolution_clock::now();
                m_playbackStartPts = frame->pts();
                m_clockInitialized = true;
            }

            double targetElapsed = frame->pts() - m_playbackStartPts;
            auto now = std::chrono::high_resolution_clock::now();
            double actualElapsed = std::chrono::duration<double>(now - m_playbackStartTime).count();
            double diffSec = targetElapsed - actualElapsed;

            if (diffSec > 0.0) {
                // Video is ahead of real-world wall clock: sleep for exact difference
                int sleepMs = (std::min)(static_cast<int>(diffSec * 1000.0), 100);
                if (sleepMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
                }
            } else if (diffSec < -0.1) {
                // Video is slightly behind real-world clock: 1ms micro sleep
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(frameDelayMs));
            }
        } else {
            // End of file
            if (m_loop) {
                m_decoder.seekToBeginning();
                m_clockInitialized = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(frameDelayMs));
            } else {
                m_playing = false;
                m_clockInitialized = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    }
}
