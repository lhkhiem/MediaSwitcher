#include "MediaDiagnostics.h"
#include "common/logger/Logger.h"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>

namespace {
constexpr uint64_t HUNDRED_NS_PER_SECOND = 10'000'000ULL;

uint64_t fileTimeToUint64(const FILETIME& value) {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

uint32_t currentProcessThreadCount(DWORD processId) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    uint32_t count = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == processId) ++count;
        } while (Thread32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return count;
}
}

MediaDiagnostics& MediaDiagnostics::instance() {
    static MediaDiagnostics instance;
    return instance;
}

MediaDiagnostics::~MediaDiagnostics() {
    stop();
}

void MediaDiagnostics::start() {
    if (m_running) return;
    sampleProcessMetrics();
    m_running = true;
    m_reporterThread = std::thread(&MediaDiagnostics::reporterLoop, this);
    LOG_INFO("MediaDiagnostics process reporter thread started.");
}

void MediaDiagnostics::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_reporterThread.joinable()) {
        m_reporterThread.join();
    }
    LOG_INFO("MediaDiagnostics reporter thread stopped.");
}

ProcessMetricsSnapshot MediaDiagnostics::processMetricsSnapshot() {
    std::lock_guard<std::mutex> lock(m_processMetricsMutex);
    return m_latestProcessMetrics;
}

ProcessMetricsSnapshot MediaDiagnostics::sampleProcessMetrics() {
    std::lock_guard<std::mutex> lock(m_processMetricsMutex);

    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const uint64_t wallTime = fileTimeToUint64(now);
    constexpr uint64_t MIN_SAMPLE_INTERVAL_100NS = HUNDRED_NS_PER_SECOND / 2;
    if (m_latestProcessMetrics.valid && wallTime >= m_previousWallTime100ns &&
        wallTime - m_previousWallTime100ns < MIN_SAMPLE_INTERVAL_100NS) {
        return m_latestProcessMetrics;
    }

    ProcessMetricsSnapshot result;
    const HANDLE process = GetCurrentProcess();
    const DWORD processId = GetCurrentProcessId();
    result.processId = processId;
    result.logicalProcessorCount = std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));

    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
        result.workingSetBytes = static_cast<uint64_t>(memory.WorkingSetSize);
        result.privateBytes = static_cast<uint64_t>(memory.PrivateUsage);
    }

    DWORD handleCount = 0;
    if (GetProcessHandleCount(process, &handleCount)) result.handleCount = handleCount;
    result.threadCount = currentProcessThreadCount(processId);

    IO_COUNTERS io{};
    if (GetProcessIoCounters(process, &io)) {
        result.ioReadBytes = io.ReadTransferCount;
        result.ioWriteBytes = io.WriteTransferCount;
    }

    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
        const uint64_t processTime = fileTimeToUint64(kernel) + fileTimeToUint64(user);

        if (m_previousWallTime100ns > 0 && wallTime > m_previousWallTime100ns) {
            const uint64_t processDelta = processTime - m_previousProcessTime100ns;
            const uint64_t wallDelta = wallTime - m_previousWallTime100ns;
            result.cpuPercent = std::clamp(
                (static_cast<double>(processDelta) / static_cast<double>(wallDelta)) *
                    100.0 / static_cast<double>(result.logicalProcessorCount),
                0.0,
                100.0);
        }

        m_previousProcessTime100ns = processTime;
        m_previousWallTime100ns = wallTime;
        const uint64_t creationTime = fileTimeToUint64(creation);
        if (wallTime >= creationTime) {
            result.uptimeSeconds = (wallTime - creationTime) / HUNDRED_NS_PER_SECOND;
        }
        result.valid = true;
    }

    m_latestProcessMetrics = result;
    return result;
}

void MediaDiagnostics::reporterLoop() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (!m_running) break;

        const ProcessMetricsSnapshot processMetrics = sampleProcessMetrics();


        LOG_INFO("[PROCESS DIAG] PID={} | CPU={:.1f}% | WorkingSet={} MB | PrivateBytes={} MB | Threads={} | Handles={} | Uptime={}s | IO Read={} MB Write={} MB",
                 processMetrics.processId,
                 processMetrics.cpuPercent,
                 processMetrics.workingSetBytes / (1024 * 1024),
                 processMetrics.privateBytes / (1024 * 1024),
                 processMetrics.threadCount,
                 processMetrics.handleCount,
                 processMetrics.uptimeSeconds,
                 processMetrics.ioReadBytes / (1024 * 1024),
                 processMetrics.ioWriteBytes / (1024 * 1024));
    }
}
