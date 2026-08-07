#include "FileSource.h"
#include "common/logger/Logger.h"
#include <chrono>

FileSource::FileSource(const std::string& filePath)
    : m_filePath(filePath)
    , m_framePool(15)
{
}

FileSource::~FileSource() {
    close();
}

bool FileSource::open() {
    if (m_filePath.empty()) return false;

    if (!m_decoder.open(m_filePath)) {
        LOG_ERROR("FileSource: Failed to open '{}'", m_filePath);
        return false;
    }

    m_opened = true;
    m_playing = true;
    m_running = true;

    m_workerThread = std::thread(&FileSource::decodeWorkerLoop, this);
    LOG_INFO("FileSource: Opened '{}' with worker thread.", m_filePath);
    return true;
}

void FileSource::close() {
    m_running = false;
    m_playing = false;

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    m_decoder.close();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_frameQueue.empty()) {
            m_frameQueue.pop();
        }
        m_lastFrame.reset();
    }

    m_framePool.clear();
    m_opened = false;
    LOG_INFO("FileSource: Closed.");
}

void FileSource::play() {
    m_playing = true;
}

void FileSource::pause() {
    m_playing = false;
}

std::shared_ptr<Frame> FileSource::getFrame() {
    if (!m_opened) return nullptr;

    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (!m_frameQueue.empty()) {
        m_lastFrame = m_frameQueue.front();
        m_frameQueue.pop();
    }
    return m_lastFrame;
}

void FileSource::decodeWorkerLoop() {
    double fps = m_decoder.fps();
    if (fps <= 0.0) fps = 30.0;
    int frameDelayMs = static_cast<int>(1000.0 / fps);

    while (m_running) {
        if (!m_playing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        bool queueFull = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_frameQueue.size() >= MAX_QUEUE_SIZE) {
                queueFull = true;
            }
        }

        if (queueFull) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        auto frame = m_framePool.acquire(m_decoder.width(), m_decoder.height(), PixelFormat::RGBA32);
        if (m_decoder.decodeNextFrame(*frame)) {
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_frameQueue.push(frame);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(frameDelayMs));
        } else {
            // End of file
            if (m_loop) {
                LOG_INFO("FileSource: End of file reached. Looping back to beginning.");
                m_decoder.seekToBeginning();
            } else {
                m_playing = false;
            }
        }
    }
}
