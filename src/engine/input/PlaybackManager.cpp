#include "PlaybackManager.h"
#include "common/logger/Logger.h"
#include <vector>

void PlaybackManager::updateState(int pgmSlotId, int pvwSlotId, int preloadSlotId,
                                   const std::unordered_map<int, std::string>& slotPaths,
                                   const std::unordered_map<int, SourceType>& slotTypes) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_pgmSlotId = pgmSlotId;
    m_pvwSlotId = pvwSlotId;
    m_preloadSlotId = preloadSlotId;

    std::vector<int> currentActiveIds;
    for (const auto& [id, src] : m_activeSources) {
        currentActiveIds.push_back(id);
    }

    // Determine slots that must be evicted to stay within budget
    std::vector<int> toEvict = m_resourceManager.enforceBudget(pgmSlotId, pvwSlotId, preloadSlotId, currentActiveIds);

    for (int evictId : toEvict) {
        auto it = m_activeSources.find(evictId);
        if (it != m_activeSources.end()) {
            if (it->second) {
                it->second->close();
            }
            m_activeSources.erase(it);
            LOG_INFO("PlaybackManager: Evicted PlaybackInstance for slot #{} (Max 3 budget enforced)", evictId);
        }
    }

    // Ensure desired active slots (PGM, PVW, Preload) have instances
    std::vector<int> desired = { pgmSlotId, pvwSlotId, preloadSlotId };
    for (int slotId : desired) {
        if (slotId <= 0) continue;

        auto typeIt = slotTypes.find(slotId);
        if (typeIt != slotTypes.end() && typeIt->second == SourceType::ColorBars) {
            continue; // ColorBars does not use PlaybackManager FileSource
        }

        if (m_activeSources.find(slotId) == m_activeSources.end()) {
            auto pathIt = slotPaths.find(slotId);
            if (pathIt != slotPaths.end() && !pathIt->second.empty()) {
                auto fileSrc = std::make_shared<FileSource>(pathIt->second);
                fileSrc->open();

                // If paused/preloading, decode 1 frame so it's ready instantly
                if (slotId != pgmSlotId && slotId != pvwSlotId) {
                    fileSrc->pause();
                }

                m_activeSources[slotId] = fileSrc;
                LOG_INFO("PlaybackManager: Created active PlaybackInstance for slot #{}", slotId);
            }
        }

        // Configure audio active state (Only PGM submits audio)
        auto srcIt = m_activeSources.find(slotId);
        if (srcIt != m_activeSources.end() && srcIt->second) {
            srcIt->second->setAudioActive(slotId == pgmSlotId);
        }
    }
}

void PlaybackManager::preloadSlot(int slotId, const std::string& filePath, SourceType type) {
    if (slotId <= 0 || type == SourceType::ColorBars || filePath.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // If already active or preloaded, do nothing
    if (m_activeSources.find(slotId) != m_activeSources.end()) return;

    // Check if we can add a preload within budget
    if (m_activeSources.size() >= ResourceManager::MAX_TOTAL_ACTIVE_PLAYBACKS) {
        // Evict current preload if available
        if (m_preloadSlotId > 0 && m_preloadSlotId != m_pgmSlotId && m_preloadSlotId != m_pvwSlotId) {
            auto it = m_activeSources.find(m_preloadSlotId);
            if (it != m_activeSources.end()) {
                if (it->second) it->second->close();
                m_activeSources.erase(it);
                LOG_INFO("PlaybackManager: Evicted old Preload slot #{} to make room for new Preload #{}", m_preloadSlotId, slotId);
            }
        }
    }

    if (m_activeSources.size() < ResourceManager::MAX_TOTAL_ACTIVE_PLAYBACKS) {
        m_preloadSlotId = slotId;
        auto fileSrc = std::make_shared<FileSource>(filePath);
        fileSrc->open();
        fileSrc->pause();
        m_activeSources[slotId] = fileSrc;
        LOG_INFO("PlaybackManager: Preloaded slot #{} (1st frame ready in background)", slotId);
    }
}

std::shared_ptr<IMediaSource> PlaybackManager::getSource(int slotId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_activeSources.find(slotId);
    return (it != m_activeSources.end()) ? it->second : nullptr;
}

void PlaybackManager::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, src] : m_activeSources) {
        if (src) src->close();
    }
    m_activeSources.clear();
    m_pgmSlotId = m_pvwSlotId = m_preloadSlotId = -1;
}

size_t PlaybackManager::activeDecoderCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeSources.size();
}
