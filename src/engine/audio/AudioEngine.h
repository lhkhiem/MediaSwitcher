#pragma once

#include <xaudio2.h>
#include <wrl/client.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <memory>

class AudioEngine : public IXAudio2VoiceCallback {
public:
    static AudioEngine& instance();

    bool initialize();
    void shutdown();

    // Submit interleaved float samples (Stereo 48kHz, 2 channels: L, R, L, R...)
    void submitAudioSamples(const float* samples, size_t numFrames);
    void clearAudioBuffer();
    size_t getRingBufferSize();

    void setVolume(float volume); // 0.0f to 1.0f
    float volume() const { return m_volume.load(); }

    void setMuted(bool muted);
    bool isMuted() const { return m_muted.load(); }

    void setFtbAlpha(float alpha) { m_ftbAlpha.store(alpha); } // 1.0 = normal, 0.0 = total silence

    // Master Audio PTS clock tracking
    void resetAudioPts(double basePts = 0.0);
    double getAudioPts() const;

    // Audio level meter values (0.0f to 1.0f linear scale)
    float getLeftPeak() const { return m_leftPeak.load(); }
    float getRightPeak() const { return m_rightPeak.load(); }

    // IXAudio2VoiceCallback implementation
    STDMETHOD_(void, OnVoiceProcessingPassStart)(UINT32 BytesRequired) override {}
    STDMETHOD_(void, OnVoiceProcessingPassEnd)() override {}
    STDMETHOD_(void, OnStreamEnd)() override {}
    STDMETHOD_(void, OnBufferStart)(void* pBufferContext) override {}
    STDMETHOD_(void, OnBufferEnd)(void* pBufferContext) override;
    STDMETHOD_(void, OnLoopEnd)(void* pBufferContext) override {}
    STDMETHOD_(void, OnVoiceError)(void* pBufferContext, HRESULT Error) override {}

private:
    AudioEngine() = default;
    ~AudioEngine();

    void bufferFeedLoop();

    Microsoft::WRL::ComPtr<IXAudio2> m_xaudio2;
    IXAudio2MasteringVoice* m_masteringVoice{nullptr};
    IXAudio2SourceVoice* m_sourceVoice{nullptr};

    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_running{false};
    std::atomic<float> m_volume{1.0f};
    std::atomic<bool> m_muted{false};
    std::atomic<float> m_ftbAlpha{1.0f};

    std::atomic<float> m_leftPeak{0.0f};
    std::atomic<float> m_rightPeak{0.0f};

    std::atomic<double> m_baseAudioPts{0.0};
    std::atomic<uint64_t> m_samplesAtBasePts{0};

    std::mutex m_bufferMutex;
    std::vector<float> m_ringBuffer;

    // Multi-buffering for XAudio2 submits
    static constexpr size_t NUM_BUFFERS = 8;
    static constexpr size_t BUFFER_SIZE_BYTES = 480 * 2 * sizeof(float); // 10ms @ 48kHz stereo = 960 floats (3840 bytes)
    std::vector<uint8_t> m_audioBuffers[NUM_BUFFERS];
    size_t m_currentBufferIndex{0};

    std::thread m_feedThread;
    std::condition_variable m_cv;
};
