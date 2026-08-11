#include "AudioEngine.h"
#include "engine/diagnostics/MediaDiagnostics.h"
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

    m_voiceStarted.store(false);
    m_waitingForPreroll.store(true);

    for (size_t i = 0; i < NUM_BUFFERS; ++i) {
        m_audioBuffers[i].resize(BUFFER_SIZE_BYTES, 0);
    }

    m_running = true;
    m_initialized = true;
    m_feedThread = std::thread(&AudioEngine::bufferFeedLoop, this);

    MediaDiagnostics::instance().start();

    LOG_INFO("AudioEngine: Successfully initialized XAudio2 (48kHz Stereo Float).");
    return true;
}

void AudioEngine::shutdown() {
    if (!m_initialized) return;

    MediaDiagnostics::instance().stop();

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
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);

        // Always insert submitted samples into ring buffer. The output thread
        // applies preroll and underrun smoothing before handing data to XAudio2.
        m_ringBuffer.insert(m_ringBuffer.end(), samples, samples + numFloats);
    }

    MediaDiagnostics::instance().recordAudioSubmit(numFrames);
    m_cv.notify_one();
}

void AudioEngine::clearAudioBuffer() {
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    if (m_sourceVoice) {
        m_sourceVoice->Stop(0);
        m_sourceVoice->FlushSourceBuffers();
    }

    m_voiceStarted.store(false);
    m_waitingForPreroll.store(true);
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

        if (m_waitingForPreroll.load()) {
            bool shouldStartVoice = false;
            size_t queuedSamples = 0;
            {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                queuedSamples = m_ringBuffer.size();
                shouldStartVoice = queuedSamples >= PREROLL_FLOATS;
            }

            MediaDiagnostics::instance().recordAudioQueueState(queuedSamples, 0);

            if (!shouldStartVoice) {
                std::unique_lock<std::mutex> lock(m_bufferMutex);
                m_cv.wait_for(lock, std::chrono::milliseconds(5));
                continue;
            }

            m_waitingForPreroll.store(false);
        }

        XAUDIO2_VOICE_STATE state;
        m_sourceVoice->GetState(&state);
        // Queue physical output protection. Partial PCM is submitted at its exact length
        // so decoder bursts never create silence between audio blocks.
        while (m_running && m_sourceVoice && state.BuffersQueued < OUTPUT_QUEUE_BUFFERS) {
            std::vector<float> chunk(BUFFER_FLOATS);
            size_t copiedFloats = 0;

            {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                copiedFloats = (std::min)(m_ringBuffer.size(), BUFFER_FLOATS);
                if (copiedFloats == 0) {
                    MediaDiagnostics::instance().recordAudioQueueState(m_ringBuffer.size(), state.BuffersQueued);
                    break;
                }

                std::copy_n(m_ringBuffer.begin(), copiedFloats, chunk.begin());
                m_ringBuffer.erase(m_ringBuffer.begin(), m_ringBuffer.begin() + copiedFloats);
                MediaDiagnostics::instance().recordAudioConsumed(copiedFloats / 2);
                MediaDiagnostics::instance().recordAudioQueueState(m_ringBuffer.size(), state.BuffersQueued);
            }

            // Apply Master Volume, Mute, and FTB Alpha.
            float masterVol = m_muted.load() ? 0.0f : m_volume.load();
            float ftbAlpha = m_ftbAlpha.load();
            float effGain = masterVol * ftbAlpha;

            float maxL = 0.0f;
            float maxR = 0.0f;

            for (size_t i = 0; i < chunk.size(); i += 2) {
                chunk[i] *= effGain;
                chunk[i + 1] *= effGain;

                maxL = (std::max)(maxL, std::abs(chunk[i]));
                maxR = (std::max)(maxR, std::abs(chunk[i + 1]));
            }

            float prevL = m_leftPeak.load();
            float prevR = m_rightPeak.load();
            m_leftPeak.store((std::max)(maxL, prevL * 0.88f));
            m_rightPeak.store((std::max)(maxR, prevR * 0.88f));

            auto& buf = m_audioBuffers[m_currentBufferIndex];
            memcpy(buf.data(), chunk.data(), BUFFER_SIZE_BYTES);

            XAUDIO2_BUFFER xbuf = {};
            xbuf.AudioBytes = static_cast<UINT32>(copiedFloats * sizeof(float));
            xbuf.pAudioData = buf.data();
            xbuf.pContext = nullptr;

            m_sourceVoice->SubmitSourceBuffer(&xbuf);
            m_currentBufferIndex = (m_currentBufferIndex + 1) % NUM_BUFFERS;
            state.BuffersQueued++;
        }

        // A physical underrun is only possible once XAudio2 itself has no
        // queued buffer. Re-prime before resuming so we never inject silence.
        if (m_voiceStarted.load() && state.BuffersQueued == 0) {
            MediaDiagnostics::instance().recordTrueAudioUnderrun(0.0);
            m_sourceVoice->Stop(0);
            m_voiceStarted.store(false);
            m_waitingForPreroll.store(true);
        }

        if (!m_voiceStarted.load() && state.BuffersQueued > 0) {
            HRESULT hr = m_sourceVoice->Start(0);
            if (SUCCEEDED(hr)) {
                m_voiceStarted.store(true);
            } else {
                LOG_ERROR("AudioEngine: Failed to start Source Voice after queue priming (hr=0x{:08X}).", static_cast<unsigned int>(hr));
            }
        }

        std::unique_lock<std::mutex> lock(m_bufferMutex);
        m_cv.wait_for(lock, std::chrono::milliseconds(5));
    }
}