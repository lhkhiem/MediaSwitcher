#include "FileSource.h"
#include "common/logger/Logger.h"
#include <chrono>

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
}

std::shared_ptr<Frame> FileSource::getFrame() {
    if (!m_opened) return nullptr;

    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (m_currentFrame) {
        m_lastValidFrame = m_currentFrame;
    }
    return m_lastValidFrame;
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
            auto frame = m_framePool.acquire(m_decoder.width(), m_decoder.height(), PixelFormat::RGBA32);
            if (m_decoder.decodeNextFrame(*frame)) {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_currentFrame = frame;
                m_lastValidFrame = frame;
            }
        }

        if (!m_playing) {
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

        auto frame = m_framePool.acquire(m_decoder.width(), m_decoder.height(), PixelFormat::RGBA32);
        if (m_decoder.decodeNextFrame(*frame)) {
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_currentFrame = frame;
                m_lastValidFrame = frame;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(frameDelayMs));
        } else {
            // End of file
            if (m_loop) {
                m_decoder.seekToBeginning();
                std::this_thread::sleep_for(std::chrono::milliseconds(frameDelayMs));
            } else {
                m_playing = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    }
}
