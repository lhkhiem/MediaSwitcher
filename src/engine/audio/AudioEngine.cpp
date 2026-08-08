#include "AudioEngine.h"
#include "common/logger/Logger.h"
#include <algorithm>
#include <cmath>

AudioEngine& AudioEngine::instance() {
    static AudioEngine instance;
    return instance;
}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize() {
    if (m_initialized) return true;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LOG_ERROR("AudioEngine: Failed to initialize COM (hr=0x{:08X})", static_cast<unsigned int>(hr));
    }

    hr = XAudio2Create(m_xaudio2.ReleaseAndGetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        LOG_ERROR("AudioEngine: Failed to create XAudio2 instance (hr=0x{:08X})", static_cast<unsigned int>(hr));
        return false;
    }

    hr = m_xaudio2->CreateMasteringVoice(&m_masteringVoice);
    if (FAILED(hr)) {
        LOG_ERROR("AudioEngine: Failed to create Mastering Voice (hr=0x{:08X})", static_cast<unsigned int>(hr));
        m_xaudio2.Reset();
        return false;
    }

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wfx.nChannels = 2; // Stereo
    wfx.nSamplesPerSec = 48000;
    wfx.wBitsPerSample = 32;
    wfx.nBlockAlign = wfx.nChannels * (wfx.wBitsPerSample / 8); // 8 bytes per stereo sample frame
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign; // 384,000 bytes/sec
    wfx.cbSize = 0;

    hr = m_xaudio2->CreateSourceVoice(&m_sourceVoice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, this);
    if (FAILED(hr)) {
        LOG_ERROR("AudioEngine: Failed to create Source Voice (hr=0x{:08X})", static_cast<unsigned int>(hr));
        m_masteringVoice->DestroyVoice();
        m_masteringVoice = nullptr;
        m_xaudio2.Reset();
        return false;
    }

    hr = m_sourceVoice->Start(0);
    if (FAILED(hr)) {
        LOG_ERROR("AudioEngine: Failed to start Source Voice.");
    }

    for (size_t i = 0; i < NUM_BUFFERS; ++i) {
        m_audioBuffers[i].resize(BUFFER_SIZE_BYTES, 0);
    }

    m_running = true;
    m_initialized = true;
    m_feedThread = std::thread(&AudioEngine::bufferFeedLoop, this);

    LOG_INFO("AudioEngine: Successfully initialized XAudio2 (48kHz Stereo Float).");
    return true;
}

void AudioEngine::shutdown() {
    if (!m_initialized) return;

    m_running = false;
    m_cv.notify_all();

    if (m_feedThread.joinable()) {
        m_feedThread.join();
    }

    if (m_sourceVoice) {
        m_sourceVoice->Stop(0);
        m_sourceVoice->FlushSourceBuffers();
        m_sourceVoice->DestroyVoice();
        m_sourceVoice = nullptr;
    }

    if (m_masteringVoice) {
        m_masteringVoice->DestroyVoice();
        m_masteringVoice = nullptr;
    }

    m_xaudio2.Reset();
    CoUninitialize();

    m_initialized = false;
    LOG_INFO("AudioEngine: Shutdown completed.");
}

void AudioEngine::submitAudioSamples(const float* samples, size_t numFrames) {
    if (!m_initialized || !samples || numFrames == 0) return;

    size_t numFloats = numFrames * 2; // Stereo L, R

    std::lock_guard<std::mutex> lock(m_bufferMutex);
    
    // Cap ring buffer to 2 seconds (192,000 floats) to prevent memory growth.
    // If full, do not insert extra samples (do NOT erase unplayed audio from the front!).
    if (m_ringBuffer.size() + numFloats > 192000) {
        return;
    }

    m_ringBuffer.insert(m_ringBuffer.end(), samples, samples + numFloats);
    m_cv.notify_one();
}

void AudioEngine::clearAudioBuffer() {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_ringBuffer.clear();
    m_leftPeak.store(0.0f);
    m_rightPeak.store(0.0f);
}

size_t AudioEngine::getRingBufferSize() {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    return m_ringBuffer.size();
}

void AudioEngine::resetAudioPts(double basePts) {
    m_baseAudioPts.store(basePts);
    if (m_sourceVoice) {
        XAUDIO2_VOICE_STATE state;
        m_sourceVoice->GetState(&state);
        m_samplesAtBasePts.store(state.SamplesPlayed);
    } else {
        m_samplesAtBasePts.store(0);
    }
}

double AudioEngine::getAudioPts() const {
    if (!m_sourceVoice) return m_baseAudioPts.load();
    XAUDIO2_VOICE_STATE state;
    m_sourceVoice->GetState(&state);
    uint64_t samplesPlayed = state.SamplesPlayed;
    uint64_t baseSamples = m_samplesAtBasePts.load();
    uint64_t deltaSamples = (samplesPlayed >= baseSamples) ? (samplesPlayed - baseSamples) : 0;
    return m_baseAudioPts.load() + (static_cast<double>(deltaSamples) / 48000.0);
}

void AudioEngine::setVolume(float volume) {
    float clampedVol = std::clamp(volume, 0.0f, 1.0f);
    m_volume.store(clampedVol);
    if (m_sourceVoice) {
        m_sourceVoice->SetVolume(m_muted.load() ? 0.0f : clampedVol);
    }
}

void AudioEngine::setMuted(bool muted) {
    m_muted.store(muted);
    if (m_sourceVoice) {
        m_sourceVoice->SetVolume(muted ? 0.0f : m_volume.load());
    }
}

STDMETHODIMP_(void) AudioEngine::OnBufferEnd(void* pBufferContext) {
    (void)pBufferContext;
    m_cv.notify_one();
}

void AudioEngine::bufferFeedLoop() {
    while (m_running) {
        if (!m_sourceVoice) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        XAUDIO2_VOICE_STATE state;
        m_sourceVoice->GetState(&state);

        // Keep 4-5 buffers queued at all times to prevent stuttering
        if (state.BuffersQueued < 4) {
            std::vector<float> chunk(960, 0.0f); // 480 frames stereo = 960 floats (10ms)
            bool hasData = false;

            {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                size_t avail = (std::min)(m_ringBuffer.size(), static_cast<size_t>(960));
                if (avail > 0) {
                    std::copy(m_ringBuffer.begin(), m_ringBuffer.begin() + avail, chunk.begin());
                    m_ringBuffer.erase(m_ringBuffer.begin(), m_ringBuffer.begin() + avail);
                    hasData = true;
                }
            }

            if (hasData) {
                // Apply Master Volume, Mute, and FTB Alpha
                float masterVol = m_muted.load() ? 0.0f : m_volume.load();
                float ftbAlpha = m_ftbAlpha.load();
                float effGain = masterVol * ftbAlpha;

                float maxL = 0.0f;
                float maxR = 0.0f;

                for (size_t i = 0; i < chunk.size(); i += 2) {
                    chunk[i] *= effGain;     // Left
                    chunk[i + 1] *= effGain; // Right

                    maxL = (std::max)(maxL, std::abs(chunk[i]));
                    maxR = (std::max)(maxR, std::abs(chunk[i + 1]));
                }

                // Smooth decay peak meter calculation
                float prevL = m_leftPeak.load();
                float prevR = m_rightPeak.load();
                m_leftPeak.store((std::max)(maxL, prevL * 0.88f));
                m_rightPeak.store((std::max)(maxR, prevR * 0.88f));

                auto& buf = m_audioBuffers[m_currentBufferIndex];
                memcpy(buf.data(), chunk.data(), BUFFER_SIZE_BYTES);

                XAUDIO2_BUFFER xbuf = {};
                xbuf.AudioBytes = static_cast<UINT32>(BUFFER_SIZE_BYTES);
                xbuf.pAudioData = buf.data();
                xbuf.pContext = nullptr;

                m_sourceVoice->SubmitSourceBuffer(&xbuf);
                m_currentBufferIndex = (m_currentBufferIndex + 1) % NUM_BUFFERS;
            }
        }

        std::unique_lock<std::mutex> lock(m_bufferMutex);
        m_cv.wait_for(lock, std::chrono::milliseconds(5));
    }
}
