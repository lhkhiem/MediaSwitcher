#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <mutex>
#include <thread>

class MediaDiagnostics {
public:
    static MediaDiagnostics& instance();

    void start();
    void stop();

    // --- Audio Metrics ---
    void recordAudioSubmit(size_t numFrames);
    void recordAudioConsumed(size_t numFrames);
    void recordAudioQueueState(size_t samples, uint32_t buffersQueued);
    void recordTrueAudioUnderrun(double gapMs);

    // --- Video Metrics ---
    void recordVideoFrameDecoded();
    void recordVideoFrameRendered();
    void recordVideoFrameDropped();
    void recordVideoPacingSleep(double sleepMs);

    // --- Worker Loop Timings ---
    void recordAudioDrainDuration(double durationMs);
    void recordVideoDecodeDuration(double durationMs);
    void recordVideoPacingDuration(double durationMs);
    void recordTimeSinceLastAudioDrain(double gapMs);

    // --- Demuxer Metrics ---
    void recordDemuxRead(bool videoFull, bool audioFull, int videoPktsRead, int audioPktsRead);

    // --- A/V Drift Metrics ---
    void recordAvPts(double videoPts, double audioPts);

    // --- Swap Snapshot ---
    void logSwapSnapshot(const std::string& pvwDetails, const std::string& pgmDetails, uint32_t xaudioBuffersQueued);

    // Get Memory Info
    static void getMemoryUsage(size_t& workingSetMb, size_t& privateBytesMb);

private:
    MediaDiagnostics() = default;
    ~MediaDiagnostics();

    void reporterLoop();

    std::atomic<bool> m_running{false};
    std::thread m_reporterThread;

    // Audio Atomicals
    std::atomic<uint64_t> m_audioSubmitCount{0};
    std::atomic<uint64_t> m_audioSubmittedSamples{0};
    std::atomic<uint64_t> m_audioConsumedSamples{0};
    std::atomic<uint64_t> m_audioUnderrunCount{0};
    std::atomic<uint32_t> m_audioQueueSamples{0};
    std::atomic<float>    m_audioQueueMs{0.0f};
    std::atomic<float>    m_audioMinQueueMs{999999.0f};
    std::atomic<float>    m_audioMaxQueueMs{0.0f};
    std::atomic<uint32_t> m_audioBuffersQueued{0};

    // Audio Producer Gap Tracking
    std::mutex m_gapMutex;
    std::chrono::steady_clock::time_point m_lastAudioSubmitTime{};
    bool m_hasFirstAudioSubmit{false};
    std::atomic<double> m_producerGapMinMs{999999.0};
    std::atomic<double> m_producerGapMaxMs{0.0};
    std::atomic<double> m_producerGapLastMs{0.0};
    std::atomic<double> m_producerGapSumMs{0.0};
    std::atomic<uint64_t> m_producerGapCount{0};

    // Video Atomicals
    std::atomic<uint64_t> m_videoFrameCount{0};
    std::atomic<uint64_t> m_videoDecodeCount{0};
    std::atomic<uint64_t> m_videoDroppedFrameCount{0};
    std::atomic<uint64_t> m_videoPacingSleepCount{0};
    std::atomic<double>   m_videoPacingSleepTotalMs{0.0};
    std::atomic<double>   m_videoPacingMaxSleepMs{0.0};

    // FileSource Loop Timing Atomicals
    std::atomic<double> m_audioDrainMaxDurationMs{0.0};
    std::atomic<double> m_videoDecodeMaxDurationMs{0.0};
    std::atomic<double> m_videoPacingMaxDurationMs{0.0};
    std::atomic<double> m_maxTimeSinceLastAudioDrainMs{0.0};
    std::atomic<double> m_lastTimeSinceAudioDrainMs{0.0};

    // Demuxer Atomicals
    std::atomic<uint64_t> m_demuxReadCount{0};
    std::atomic<uint64_t> m_videoQueueFullCount{0};
    std::atomic<uint64_t> m_audioQueueFullCount{0};
    std::atomic<uint64_t> m_videoPacketReadCount{0};
    std::atomic<uint64_t> m_audioPacketReadCount{0};

    // A/V Drift Atomicals
    std::atomic<double> m_lastVideoPts{0.0};
    std::atomic<double> m_lastAudioPts{0.0};
    std::atomic<double> m_lastDriftMs{0.0};
    std::atomic<bool>   m_hasPtsData{false};
};
