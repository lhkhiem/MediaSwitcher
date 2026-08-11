#include "MediaDiagnostics.h"
#include "common/logger/Logger.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>

MediaDiagnostics& MediaDiagnostics::instance() {
    static MediaDiagnostics instance;
    return instance;
}

MediaDiagnostics::~MediaDiagnostics() {
    stop();
}

void MediaDiagnostics::start() {
    if (m_running) return;
    m_running = true;
    m_reporterThread = std::thread(&MediaDiagnostics::reporterLoop, this);
    LOG_INFO("MediaDiagnostics reporter thread started (Phase 0 - Observation Only).");
}

void MediaDiagnostics::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_reporterThread.joinable()) {
        m_reporterThread.join();
    }
    LOG_INFO("MediaDiagnostics reporter thread stopped.");
}

void MediaDiagnostics::getMemoryUsage(size_t& workingSetMb, size_t& privateBytesMb) {
    workingSetMb = 0;
    privateBytesMb = 0;
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        workingSetMb = pmc.WorkingSetSize / (1024 * 1024);
        privateBytesMb = pmc.PagefileUsage / (1024 * 1024);
    }
}

void MediaDiagnostics::recordAudioSubmit(size_t numFrames) {
    auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(m_gapMutex);
        if (m_hasFirstAudioSubmit) {
            double gapMs = std::chrono::duration<double, std::milli>(now - m_lastAudioSubmitTime).count();
            m_producerGapLastMs.store(gapMs);

            double currentMin = m_producerGapMinMs.load();
            while (gapMs < currentMin && !m_producerGapMinMs.compare_exchange_weak(currentMin, gapMs)) {}

            double currentMax = m_producerGapMaxMs.load();
            while (gapMs > currentMax && !m_producerGapMaxMs.compare_exchange_weak(currentMax, gapMs)) {}

            m_producerGapSumMs.fetch_add(gapMs);
            m_producerGapCount.fetch_add(1);
        } else {
            m_hasFirstAudioSubmit = true;
        }
        m_lastAudioSubmitTime = now;
    }

    m_audioSubmitCount.fetch_add(1);
    m_audioSubmittedSamples.fetch_add(numFrames * 2);
}

void MediaDiagnostics::recordAudioConsumed(size_t numFrames) {
    m_audioConsumedSamples.fetch_add(numFrames * 2);
}

void MediaDiagnostics::recordAudioQueueState(size_t samples, uint32_t buffersQueued) {
    m_audioQueueSamples.store(static_cast<uint32_t>(samples));
    m_audioBuffersQueued.store(buffersQueued);

    float ms = static_cast<float>((samples / 2) / 48000.0 * 1000.0);
    m_audioQueueMs.store(ms);

    float currentMin = m_audioMinQueueMs.load();
    while (ms < currentMin && !m_audioMinQueueMs.compare_exchange_weak(currentMin, ms)) {}

    float currentMax = m_audioMaxQueueMs.load();
    while (ms > currentMax && !m_audioMaxQueueMs.compare_exchange_weak(currentMax, ms)) {}
}

void MediaDiagnostics::recordTrueAudioUnderrun(double gapMs) {
    uint64_t newCount = m_audioUnderrunCount.fetch_add(1) + 1;
    LOG_WARN("[AUDIO WARNING] TRUE UNDERRUN #{} detected! ringBuffer=0ms | xaudioBuffersQueued={} | producerGap={:.1f}ms",
             newCount, m_audioBuffersQueued.load(), gapMs);
}

void MediaDiagnostics::recordVideoFrameDecoded() {
    m_videoDecodeCount.fetch_add(1);
}

void MediaDiagnostics::recordVideoFrameRendered() {
    m_videoFrameCount.fetch_add(1);
}

void MediaDiagnostics::recordVideoFrameDropped() {
    m_videoDroppedFrameCount.fetch_add(1);
}

void MediaDiagnostics::recordVideoPacingSleep(double sleepMs) {
    m_videoPacingSleepCount.fetch_add(1);
    m_videoPacingSleepTotalMs.fetch_add(sleepMs);

    double currentMax = m_videoPacingMaxSleepMs.load();
    while (sleepMs > currentMax && !m_videoPacingMaxSleepMs.compare_exchange_weak(currentMax, sleepMs)) {}
}

void MediaDiagnostics::recordAudioDrainDuration(double durationMs) {
    double currentMax = m_audioDrainMaxDurationMs.load();
    while (durationMs > currentMax && !m_audioDrainMaxDurationMs.compare_exchange_weak(currentMax, durationMs)) {}
}

void MediaDiagnostics::recordVideoDecodeDuration(double durationMs) {
    double currentMax = m_videoDecodeMaxDurationMs.load();
    while (durationMs > currentMax && !m_videoDecodeMaxDurationMs.compare_exchange_weak(currentMax, durationMs)) {}
}

void MediaDiagnostics::recordVideoPacingDuration(double durationMs) {
    double currentMax = m_videoPacingMaxDurationMs.load();
    while (durationMs > currentMax && !m_videoPacingMaxDurationMs.compare_exchange_weak(currentMax, durationMs)) {}
}

void MediaDiagnostics::recordTimeSinceLastAudioDrain(double gapMs) {
    m_lastTimeSinceAudioDrainMs.store(gapMs);
    double currentMax = m_maxTimeSinceLastAudioDrainMs.load();
    while (gapMs > currentMax && !m_maxTimeSinceLastAudioDrainMs.compare_exchange_weak(currentMax, gapMs)) {}
}

void MediaDiagnostics::recordDemuxRead(bool videoFull, bool audioFull, int videoPktsRead, int audioPktsRead) {
    m_demuxReadCount.fetch_add(1);
    if (videoFull) m_videoQueueFullCount.fetch_add(1);
    if (audioFull) m_audioQueueFullCount.fetch_add(1);
    if (videoPktsRead > 0) m_videoPacketReadCount.fetch_add(videoPktsRead);
    if (audioPktsRead > 0) m_audioPacketReadCount.fetch_add(audioPktsRead);
}

void MediaDiagnostics::recordAvPts(double videoPts, double audioPts) {
    m_lastVideoPts.store(videoPts);
    m_lastAudioPts.store(audioPts);
    double drift = (videoPts - audioPts) * 1000.0;
    m_lastDriftMs.store(drift);
    m_hasPtsData.store(true);
}

void MediaDiagnostics::logSwapSnapshot(const std::string& pvwDetails, const std::string& pgmDetails, uint32_t xaudioBuffersQueued) {
    LOG_INFO("[SWAP AUDIO SNAPSHOT] PVW:[{}] | PGM:[{}] | XAudioBuffersQueued={}", pvwDetails, pgmDetails, xaudioBuffersQueued);
}

void MediaDiagnostics::reporterLoop() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (!m_running) break;

        size_t workingSetMb = 0, privateBytesMb = 0;
        getMemoryUsage(workingSetMb, privateBytesMb);

        double gapMin = m_producerGapMinMs.load();
        double gapMax = m_producerGapMaxMs.load();
        double gapLast = m_producerGapLastMs.load();
        uint64_t gapCount = m_producerGapCount.load();
        double gapAvg = gapCount > 0 ? (m_producerGapSumMs.load() / gapCount) : 0.0;

        float qMs = m_audioQueueMs.load();
        float qMinMs = m_audioMinQueueMs.load();
        float qMaxMs = m_audioMaxQueueMs.load();
        uint32_t qBufs = m_audioBuffersQueued.load();
        uint64_t underruns = m_audioUnderrunCount.load();

        if (qMinMs > 990000.0f) qMinMs = 0.0f;
        if (gapMin > 990000.0) gapMin = 0.0;

        LOG_INFO("[AUDIO DIAG] queue={:.1f}ms (min={:.1f}ms max={:.1f}ms) | xaudioBuffers={} | underruns={} | producerGap(ms): last={:.1f} min={:.1f} max={:.1f} avg={:.1f}",
                 qMs, qMinMs, qMaxMs, qBufs, underruns, gapLast, gapMin, gapMax, gapAvg);

        double pacingMaxMs = m_videoPacingMaxSleepMs.load();
        uint64_t vDecode = m_videoDecodeCount.load();
        uint64_t vRender = m_videoFrameCount.load();
        uint64_t vDropped = m_videoDroppedFrameCount.load();

        LOG_INFO("[VIDEO DIAG] decodedFrames={} renderedFrames={} droppedFrames={} | pacingMaxSleep={:.1f}ms",
                 vDecode, vRender, vDropped, pacingMaxMs);

        double audioDrainMax = m_audioDrainMaxDurationMs.load();
        double videoDecodeMax = m_videoDecodeMaxDurationMs.load();
        double videoPacingMax = m_videoPacingMaxDurationMs.load();
        double gapAudioDrainMax = m_maxTimeSinceLastAudioDrainMs.load();

        LOG_INFO("[TIMING DIAG] maxAudioDrainDur={:.1f}ms | maxVideoDecodeDur={:.1f}ms | maxVideoPacingDur={:.1f}ms | maxGapBetweenAudioDrain={:.1f}ms",
                 audioDrainMax, videoDecodeMax, videoPacingMax, gapAudioDrainMax);

        uint64_t demuxReads = m_demuxReadCount.load();
        uint64_t vFull = m_videoQueueFullCount.load();
        uint64_t aFull = m_audioQueueFullCount.load();
        uint64_t vPkts = m_videoPacketReadCount.load();
        uint64_t aPkts = m_audioPacketReadCount.load();

        LOG_INFO("[DEMUX DIAG] reads={} | videoQueueFullCount={} | audioQueueFullCount={} | videoPktsRead={} | audioPktsRead={}",
                 demuxReads, vFull, aFull, vPkts, aPkts);

        if (m_hasPtsData.load()) {
            double vPts = m_lastVideoPts.load();
            double aPts = m_lastAudioPts.load();
            double drift = m_lastDriftMs.load();
            LOG_INFO("[AV DIAG] videoPts={:.3f}s | audioPts={:.3f}s | A/V drift={:+.1f}ms", vPts, aPts, drift);
        } else {
            LOG_INFO("[AV DIAG] A/V drift=UNAVAILABLE (waiting for active PGM playback)");
        }

        LOG_INFO("[MEM DIAG] WorkingSet={} MB | PrivateBytes={} MB", workingSetMb, privateBytesMb);
    }
}
