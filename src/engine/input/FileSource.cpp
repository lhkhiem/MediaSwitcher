#include "FileSource.h"
#include "common/logger/Logger.h"
#include <QMetaObject>
#include <QUrl>
#include <chrono>
#include <algorithm>

FileSource::FileSource(const std::string& filePath)
    : m_filePath(filePath)
{
}

FileSource::~FileSource() {
    close();
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

bool FileSource::open() {
    if (m_opened) return true;
    if (m_filePath.empty()) return false;

    // FFmpeg decoder for VIDEO frames only
    if (!m_decoder.open(m_filePath)) {
        LOG_ERROR("FileSource: Failed to open decoder for '{}'", m_filePath);
        return false;
    }

    // Qt Multimedia for AUDIO — handles all codec/buffer/output internally
    m_audioOutput = new QAudioOutput();
    m_audioOutput->setVolume(m_volume.load());
    m_audioOutput->setMuted(m_muted.load());

    m_audioPlayer = new QMediaPlayer();
    m_audioPlayer->setAudioOutput(m_audioOutput);
    m_audioPlayer->setSource(QUrl::fromLocalFile(QString::fromStdString(m_filePath)));

    // Log any audio player errors
    QObject::connect(m_audioPlayer, &QMediaPlayer::errorOccurred,
        m_audioPlayer, [this](QMediaPlayer::Error error, const QString& errorString) {
            LOG_ERROR("FileSource audio error ({}): {}", (int)error, errorString.toStdString());
        });

    // Loop: restart audio when it reaches the end (if loop mode is on)
    QObject::connect(m_audioPlayer, &QMediaPlayer::mediaStatusChanged,
        m_audioPlayer, [this](QMediaPlayer::MediaStatus status) {
            LOG_DEBUG("FileSource audio status: {}", (int)status);
            if (status == QMediaPlayer::EndOfMedia && m_loop.load() && m_playing.load()) {
                m_audioPlayer->setPosition(0);
                m_audioPlayer->play();
            }
        });

    m_opened  = true;
    m_playing = false;
    m_running = true;
    m_workerThread = std::thread(&FileSource::decodeWorkerLoop, this);

    LOG_INFO("FileSource: Opened '{}' paused at frame 0.", m_filePath);
    return true;
}

void FileSource::close() {
    if (!m_opened) return;

    m_running = false;
    m_playing = false;

    if (m_workerThread.joinable())
        m_workerThread.join();

    // Stop and delete QMediaPlayer on main thread
    if (m_audioPlayer) {
        QMetaObject::invokeMethod(m_audioPlayer, [this]() {
            m_audioPlayer->stop();
            m_audioPlayer->deleteLater();
            m_audioPlayer = nullptr;
            if (m_audioOutput) {
                m_audioOutput->deleteLater();
                m_audioOutput = nullptr;
            }
        }, Qt::QueuedConnection);
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

// ---------------------------------------------------------------------------
// Playback control
// ---------------------------------------------------------------------------

void FileSource::play() {
    m_playing = true;
    if (m_audioPlayer) {
        QMetaObject::invokeMethod(m_audioPlayer, [this]() {
            m_audioPlayer->play();
        }, Qt::QueuedConnection);
    }
}

void FileSource::pause() {
    m_playing = false;
    if (m_audioPlayer) {
        QMetaObject::invokeMethod(m_audioPlayer, [this]() {
            m_audioPlayer->pause();
        }, Qt::QueuedConnection);
    }
}

double FileSource::durationSeconds() const { return m_decoder.durationSeconds(); }
double FileSource::positionSeconds()  const { return m_decoder.currentPositionSeconds(); }

void FileSource::setVolume(float vol) {
    m_volume.store(vol);
    if (m_audioOutput) {
        QMetaObject::invokeMethod(m_audioOutput, [this, vol]() {
            m_audioOutput->setVolume(vol);
        }, Qt::QueuedConnection);
    }
}

void FileSource::setMuted(bool mute) {
    m_muted.store(mute);
    if (m_audioOutput) {
        QMetaObject::invokeMethod(m_audioOutput, [this, mute]() {
            m_audioOutput->setMuted(mute);
        }, Qt::QueuedConnection);
    }
}

void FileSource::seekToSeconds(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    m_seekTarget.store(seconds);

    // Seek audio player on main thread
    if (m_audioPlayer) {
        qint64 ms = static_cast<qint64>(seconds * 1000.0);
        QMetaObject::invokeMethod(m_audioPlayer, [this, ms]() {
            m_audioPlayer->setPosition(ms);
        }, Qt::QueuedConnection);
    }
}

void FileSource::loopToBeginning() {
    // Reset video decoder
    m_decoder.seekToBeginning();
    m_clockInitialized = false;

    // Restart audio from beginning on main thread
    if (m_audioPlayer) {
        QMetaObject::invokeMethod(m_audioPlayer, [this]() {
            m_audioPlayer->setPosition(0);
            if (m_playing.load()) m_audioPlayer->play();
        }, Qt::QueuedConnection);
    }
}

// ---------------------------------------------------------------------------
// Frame access
// ---------------------------------------------------------------------------

std::shared_ptr<Frame> FileSource::getFrame() {
    if (!m_opened) return nullptr;
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (m_currentFrame)
        m_lastValidFrame = m_currentFrame;
    return m_lastValidFrame;
}

// ---------------------------------------------------------------------------
// Decode worker loop — VIDEO ONLY. Audio is handled by QMediaPlayer.
// ---------------------------------------------------------------------------

void FileSource::decodeWorkerLoop() {
    double fps = m_decoder.fps();
    if (fps <= 0.0) fps = 30.0;
    int frameDelayMs = static_cast<int>(1000.0 / fps);
    if (frameDelayMs < 5) frameDelayMs = 5;

    while (m_running) {

        // --- Handle user-initiated seek (video side) ---
        double seekSec = m_seekTarget.exchange(-1.0);
        if (seekSec >= 0.0) {
            m_decoder.seekToSeconds(seekSec);
            m_clockInitialized = false;
            // Decode one still frame so the UI shows the seek position
            auto frame = m_framePool.acquire(m_decoder.width(), m_decoder.height(), PixelFormat::RGBA32);
            if (m_decoder.decodeNextFrame(*frame)) {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_currentFrame   = frame;
                m_lastValidFrame = frame;
            }
        }

        // --- Paused: hold on first frame ---
        if (!m_playing) {
            m_clockInitialized = false;
            bool hasFrame = false;
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                hasFrame = (m_currentFrame != nullptr);
            }
            if (!hasFrame) {
                auto frame = m_framePool.acquire(m_decoder.width(), m_decoder.height(), PixelFormat::RGBA32);
                if (m_decoder.decodeNextFrame(*frame)) {
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    m_currentFrame   = frame;
                    m_lastValidFrame = frame;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        // --- Video-only path ---
        if (m_decoder.hasVideo()) {
            auto frame    = m_framePool.acquire(m_decoder.width(), m_decoder.height(), PixelFormat::RGBA32);
            bool gotVideo = m_decoder.decodeNextFrame(*frame);

            if (gotVideo) {
                {
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    m_currentFrame   = frame;
                    m_lastValidFrame = frame;
                }

                // High-precision video frame pacing to wall clock
                if (!m_clockInitialized) {
                    m_playbackStartTime = std::chrono::high_resolution_clock::now();
                    m_playbackStartPts  = frame->pts();
                    m_clockInitialized  = true;
                }

                double targetElapsed = frame->pts() - m_playbackStartPts;
                auto   now           = std::chrono::high_resolution_clock::now();
                double actualElapsed = std::chrono::duration<double>(now - m_playbackStartTime).count();
                double diffSec       = targetElapsed - actualElapsed;

                if (diffSec > 0.0) {
                    int ms = (std::min)(static_cast<int>(diffSec * 1000.0), 100);
                    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                } else if (diffSec < -0.1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(frameDelayMs));
                }

            } else {
                // Video EOF
                if (m_decoder.atFormatEof()) {
                    if (m_loop) {
                        loopToBeginning();
                    } else {
                        m_playing          = false;
                        m_clockInitialized = false;
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }

        } else {
            // Audio-only file: nothing for video thread to do
            // QMediaPlayer handles playback and looping completely
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}
