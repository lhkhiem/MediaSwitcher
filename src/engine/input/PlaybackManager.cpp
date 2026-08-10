#include "PlaybackManager.h"
#include "common/logger/Logger.h"
#include <vector>

void PlaybackManager::activateSource(PlaybackRole role, int slotId, const std::string& filePath, SourceType type) {
    if (slotId <= 0 || type == SourceType::ColorBars || filePath.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    auto slotIt = m_roleSlotIds.find(role);
    if (slotIt != m_roleSlotIds.end() && slotIt->second == slotId) {
        // Instance already active for this role and slot
        return;
    }

    // Close old instance for this role if existing
    auto oldIt = m_roleInstances.find(role);
    if (oldIt != m_roleInstances.end() && oldIt->second) {
        oldIt->second->close();
    }

    auto fileSrc = std::make_shared<FileSource>(filePath);
    fileSrc->open();

    if (role == PlaybackRole::Program) {
        fileSrc->play();
    } else {
        fileSrc->pause();
    }
    fileSrc->setAudioActive(role == PlaybackRole::Program);

    m_roleInstances[role] = fileSrc;
    m_roleSlotIds[role] = slotId;

    const char* roleName = (role == PlaybackRole::Program) ? "Program" : ((role == PlaybackRole::Preview) ? "Preview" : "Preload");
    LOG_INFO("PlaybackManager: Explicitly activated role instance for role={} slotId=#{}", roleName, slotId);
}

void PlaybackManager::updateState(int pgmSlotId, int pvwSlotId, int preloadSlotId,
                                   const std::unordered_map<int, std::string>& slotPaths,
                                   const std::unordered_map<int, SourceType>& slotTypes) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_pgmSlotId = pgmSlotId;
    m_pvwSlotId = pvwSlotId;
    m_preloadSlotId = preloadSlotId;

    auto handleRoleSlot = [&](PlaybackRole role, int slotId) {
        const char* roleName = (role == PlaybackRole::Program) ? "Program" : ((role == PlaybackRole::Preview) ? "Preview" : "Preload");

        if (slotId <= 0) {
            auto it = m_roleInstances.find(role);
            if (it != m_roleInstances.end()) {
                if (it->second) it->second->close();
                m_roleInstances.erase(it);
                m_roleSlotIds.erase(role);
            }
            return;
        }

        auto typeIt = slotTypes.find(slotId);
        if (typeIt != slotTypes.end() && typeIt->second == SourceType::ColorBars) {
            auto it = m_roleInstances.find(role);
            if (it != m_roleInstances.end()) {
                if (it->second) it->second->close();
                m_roleInstances.erase(it);
                m_roleSlotIds.erase(role);
            }
            return;
        }

        auto slotIt = m_roleSlotIds.find(role);
        if (slotIt == m_roleSlotIds.end() || slotIt->second != slotId) {
            auto pathIt = slotPaths.find(slotId);
            if (pathIt != slotPaths.end() && !pathIt->second.empty()) {
                auto oldIt = m_roleInstances.find(role);
                if (oldIt != m_roleInstances.end() && oldIt->second) {
                    oldIt->second->close();
                }

                auto fileSrc = std::make_shared<FileSource>(pathIt->second);
                fileSrc->open();

                if (role == PlaybackRole::Program) {
                    fileSrc->play();
                } else {
                    fileSrc->pause();
                }

                fileSrc->setAudioActive(role == PlaybackRole::Program);

                m_roleInstances[role] = fileSrc;
                m_roleSlotIds[role] = slotId;
                LOG_INFO("PlaybackManager: Allocated role instance for role={} slotId=#{}", roleName, slotId);
            }
        } else {
            auto srcIt = m_roleInstances.find(role);
            if (srcIt != m_roleInstances.end() && srcIt->second) {
                srcIt->second->setAudioActive(role == PlaybackRole::Program);
            }
        }
    };

    // Allocate in priority order: Program -> Preview -> Preload
    handleRoleSlot(PlaybackRole::Program, pgmSlotId);
    handleRoleSlot(PlaybackRole::Preview, pvwSlotId);

    if (m_roleInstances.size() < ResourceManager::MAX_TOTAL_ACTIVE_PLAYBACKS) {
        handleRoleSlot(PlaybackRole::Preload, preloadSlotId);
    } else {
        handleRoleSlot(PlaybackRole::Preload, -1);
    }
}

void PlaybackManager::preloadSlot(int slotId, const std::string& filePath, SourceType type) {
    if (slotId <= 0 || type == SourceType::ColorBars || filePath.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // If already assigned to a role, return
    for (const auto& [role, id] : m_roleSlotIds) {
        if (id == slotId) return;
    }

    if (m_roleInstances.size() < ResourceManager::MAX_TOTAL_ACTIVE_PLAYBACKS) {
        m_preloadSlotId = slotId;
        auto fileSrc = std::make_shared<FileSource>(filePath);
        fileSrc->open();
        fileSrc->pause();
        fileSrc->setAudioActive(false);

        m_roleInstances[PlaybackRole::Preload] = fileSrc;
        m_roleSlotIds[PlaybackRole::Preload] = slotId;
        LOG_INFO("PlaybackManager: Preloaded slot #{} for role=Preload", slotId);
    }
}

// Read-only lookup. DOES NOT create decoders/instances implicitly.
std::shared_ptr<IMediaSource> PlaybackManager::getSource(PlaybackRole role) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_roleInstances.find(role);
    return (it != m_roleInstances.end()) ? it->second : nullptr;
}

std::shared_ptr<IMediaSource> PlaybackManager::getSourceForSlot(int slotId, PlaybackRole role) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto slotIt = m_roleSlotIds.find(role);
    if (slotIt != m_roleSlotIds.end() && slotIt->second == slotId) {
        auto srcIt = m_roleInstances.find(role);
        return (srcIt != m_roleInstances.end()) ? srcIt->second : nullptr;
    }
    return nullptr;
}

void PlaybackManager::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [role, src] : m_roleInstances) {
        if (src) src->close();
    }
    m_roleInstances.clear();
    m_roleSlotIds.clear();
    m_pgmSlotId = m_pvwSlotId = m_preloadSlotId = -1;
}

size_t PlaybackManager::activeDecoderCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_roleInstances.size();
}
