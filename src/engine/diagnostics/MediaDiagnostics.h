#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

struct ProcessMetricsSnapshot {
    bool valid{false};
    uint32_t processId{0};
    uint32_t logicalProcessorCount{1};
    uint32_t threadCount{0};
    uint32_t handleCount{0};
    uint64_t workingSetBytes{0};
    uint64_t privateBytes{0};
    uint64_t ioReadBytes{0};
    uint64_t ioWriteBytes{0};
    uint64_t uptimeSeconds{0};
    double cpuPercent{0.0};
};

class MediaDiagnostics {
public:
    static MediaDiagnostics& instance();

    void start();
    void stop();

    // Chỉ đọc snapshot đã cache; không gọi API hệ thống trên UI thread.
    ProcessMetricsSnapshot processMetricsSnapshot();

private:
    MediaDiagnostics() = default;
    ~MediaDiagnostics();

    void reporterLoop();
    ProcessMetricsSnapshot sampleProcessMetrics();

    std::atomic<bool> m_running{false};
    std::thread m_reporterThread;

    std::mutex m_processMetricsMutex;
    ProcessMetricsSnapshot m_latestProcessMetrics;
    uint64_t m_previousProcessTime100ns{0};
    uint64_t m_previousWallTime100ns{0};

};
