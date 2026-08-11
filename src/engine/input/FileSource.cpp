#include "FileSource.h"
#include "engine/audio/AudioEngine.h"
#include "engine/diagnostics/MediaDiagnostics.h"
#include "common/logger/Logger.h"
#include <algorithm>
#include <chrono>
#include <cmath>

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

    // FFmpegDecoder handles BOTH video frames and audio PCM decode
    if (!m_decoder.open(m_filePath)) {
        LOG_ERROR("FileSource: Failed to open decoder for '{}'", m_filePath);
        return false;
    }

    m_opened  = true;
    m_playing = false;
    m_running = true;
    m_workerThread = std::thread(&FileSource::decodeWorkerLoop, this);

    LOG_INFO("FileSource: Opened '{}' (video:{} audio:{}) paused at frame 0.",
             m_filePath,
             m_decoder.hasVideo() ? "yes" : "no",
             m_decoder.hasAudio() ? "yes" : "no");
    return true;
}

void FileSource::close() {
    if (!m_opened) return;

    m_running = false;
    m_playing = false;
    m_audioActive = false;

    if (m_workerThread.joinable())
        m_workerThread.join();

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
}

void FileSource::pause() {
    m_playing = false;
}

double FileSource::durationSeconds() const { return m_decoder.durationSeconds(); }
double FileSource::positionSeconds()  const { return m_decoder.currentPositionSeconds(); }

void FileSource::setVolume(float vol) {
    m_volume.store(std::clamp(vol, 0.0f, 1.0f));
}

void FileSource::setMuted(bool mute) {
    m_muted.store(mute);
}

void FileSource::setAudioActive(bool active) {
    m_audioActive.store(active);
    LOG_DEBUG("FileSource '{}': audioActive = {}", m_filePath, active);
}

void FileSource::setDecodeMode(DecodeMode mode) {
    DecodeMode previous = m_decodeMode.exchange(mode);
    if (previous == mode) return;

    if (mode == DecodeMode::Active) {
        m_clockInitialized = false;
    }

    LOG_DEBUG("FileSource '{}': decodeMode = {}", m_filePath,
              mode == DecodeMode::Active ? "Active" : "Idle");
}

void FileSource::seekToSeconds(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    m_seekTarget.store(seconds);
}

void FileSource::loopToBeginning() {
    if (m_decoder.hasAudio()) {
        std::vector<float> pcm;
        m_decoder.drainAudio(pcm);
        if (!pcm.empty() && m_audioActive.load()) {
            float vol = m_muted.load() ? 0.0f : m_volume.load();
            if (std::abs(vol - 1.0f) > 0.001f) {
                for (auto& s : pcm) s *= vol;
            }
            AudioEngine::instance().submitAudioSamples(pcm.data(), pcm.size() / 2);
        }
    }

    m_decoder.seekToBeginning();
    m_clockInitialized = false;
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
// Audio: drain FFmpegDecoder audio queue and submit to AudioEngine
// ---------------------------------------------------------------------------

void FileSource::drainAndSubmitAudio() {
    if (!m_decoder.hasAudio() || !m_audioActive.load()) return;

    // Keep up to three seconds of decoded PCM ahead of XAudio2. A single decoder call
    // produces at most 250ms, which is insufficient for heavier 1080p sources.
    constexpr size_t TARGET_AUDIO_FLOATS = 48000 * 2 * 3;
    constexpr size_t MAX_AUDIO_DECODE_BATCHES = 4;

    for (size_t batch = 0; batch < MAX_AUDIO_DECODE_BATCHES && m_audioActive.load() &&
         AudioEngine::instance().getRingBufferSize() < TARGET_AUDIO_FLOATS; ++batch) {
        std::vector<float> pcm;
        if (!m_decoder.decodeAudioSamples(pcm) || pcm.empty()) {
            break;
        }

        float vol = m_muted.load() ? 0.0f : m_volume.load();
        if (std::abs(vol - 1.0f) > 0.001f) {
            for (auto& sample : pcm) sample *= vol;
        }
        AudioEngine::instance().submitAudioSamples(pcm.data(), pcm.size() / 2);
    }
}

// ---------------------------------------------------------------------------
// Decode worker loop — Video pacing + Audio draining
// ---------------------------------------------------------------------------

void FileSource::decodeWorkerLoop() {
    double fps = m_decoder.fps();
    if (fps <= 0.0) fps = 30.0;
    int frameDelayMs = static_cast<int>(1000.0 / fps);
    if (frameDelayMs < 5) frameDelayMs = 5;

    auto lastAudioDrainTime = std::chrono::steady_clock::now();
    bool hasLastAudioDrain = false;

    while (m_running) {
        DecodeMode decodeMode = m_decodeMode.load();

        // --- Handle user-initiated seek ---
        double seekSec = m_seekTarget.exchange(-1.0);
        if (seekSec >= 0.0) {
            m_decoder.seekToSeconds(seekSec);
            m_clockInitialized = false;
            // Clear AudioEngine buffer so stale audio doesn't play after seek
            if (m_audioActive.load()) {
                AudioEngine::instance().clearAudioBuffer();
                AudioEngine::instance().resetAudioPts(seekSec);
            }
            // Decode one still frame so the UI shows the seek position immediately
            auto frame = m_framePool.acquire(m_decoder.width(), m_decoder.height(), PixelFormat::RGBA32);
            if (m_decoder.decodeNextFrame(*frame)) {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_currentFrame   = frame;
                m_lastValidFrame = frame;
            }
        }

        // --- Paused: hold on first frame, no audio ---
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

        // --- Drain audio packets (always call to prevent queue overflow) ---
        // Submits to AudioEngine only when m_audioActive == true
        auto drainStart = std::chrono::steady_clock::now();
        if (hasLastAudioDrain) {
            double gapMs = std::chrono::duration<double, std::milli>(drainStart - lastAudioDrainTime).count();
            MediaDiagnostics::instance().recordTimeSinceLastAudioDrain(gapMs);
        }
        lastAudioDrainTime = drainStart;
        hasLastAudioDrain = true;

        drainAndSubmitAudio();

        auto drainEnd = std::chrono::steady_clock::now();
        double drainDur = std::chrono::duration<double, std::milli>(drainEnd - drainStart).count();
        MediaDiagnostics::instance().recordAudioDrainDuration(drainDur);

        // --- Video decode path ---
        if (m_decoder.hasVideo()) {
            auto frame    = m_framePool.acquire(m_decoder.width(), m_decoder.height(), PixelFormat::RGBA32);

            auto decodeStart = std::chrono::steady_clock::now();
            bool gotVideo = m_decoder.decodeNextFrame(*frame);
            auto decodeEnd = std::chrono::steady_clock::now();

            double decodeDur = std::chrono::duration<double, std::milli>(decodeEnd - decodeStart).count();
            MediaDiagnostics::instance().recordVideoDecodeDuration(decodeDur);

            if (gotVideo) {
                MediaDiagnostics::instance().recordVideoFrameDecoded();
                {
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    m_currentFrame   = frame;
                    m_lastValidFrame = frame;
                }

                if (m_audioActive.load()) {
                    double vPts = frame->pts();
                    double aPts = AudioEngine::instance().getAudioPts();
                    MediaDiagnostics::instance().recordAvPts(vPts, aPts);
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

                auto pacingStart = std::chrono::steady_clock::now();
                if (diffSec > 0.0) {
                    int ms = (std::min)(static_cast<int>(diffSec * 1000.0), 100);
                    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                } else if (diffSec < -0.1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(frameDelayMs));
                }
                auto pacingEnd = std::chrono::steady_clock::now();
                double pacingDur = std::chrono::duration<double, std::milli>(pacingEnd - pacingStart).count();
                MediaDiagnostics::instance().recordVideoPacingDuration(pacingDur);
                MediaDiagnostics::instance().recordVideoPacingSleep(pacingDur);

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
            // Audio-only file: drainAndSubmitAudio() is called above.
            // Just check for EOF / loop here.
            if (m_decoder.atFormatEof()) {
                if (m_loop) {
                    loopToBeginning();
                } else {
                    m_playing          = false;
                    m_clockInitialized = false;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        if (decodeMode == DecodeMode::Idle && !m_audioActive.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            m_clockInitialized = false;
        }
    }
}
