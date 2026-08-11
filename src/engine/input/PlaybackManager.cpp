#include "PlaybackManager.h"
#include "engine/audio/AudioEngine.h"
#include "engine/diagnostics/MediaDiagnostics.h"
#include "common/logger/Logger.h"
#include <vector>
#include <string>

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
    fileSrc->setDecodeMode(role == PlaybackRole::Preload ? DecodeMode::Idle : DecodeMode::Active);

    m_roleInstances[role] = fileSrc;
    m_roleSlotIds[role] = slotId;

    const char* roleName = (role == PlaybackRole::Program) ? "Program" : ((role == PlaybackRole::Preview) ? "Preview" : "Preload");
    LOG_INFO("PlaybackManager: Explicitly activated role instance for role={} slotId=#{}", roleName, slotId);
}

void PlaybackManager::swapRoles() {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto pgmIt = m_roleInstances.find(PlaybackRole::Program);
    auto pvwIt = m_roleInstances.find(PlaybackRole::Preview);

    int oldPgmId = m_roleSlotIds.count(PlaybackRole::Program) ? m_roleSlotIds[PlaybackRole::Program] : -1;
    int oldPvwId = m_roleSlotIds.count(PlaybackRole::Preview) ? m_roleSlotIds[PlaybackRole::Preview] : -1;

    void* oldPgmPtr = (pgmIt != m_roleInstances.end() && pgmIt->second) ? pgmIt->second.get() : nullptr;
    void* oldPvwPtr = (pvwIt != m_roleInstances.end() && pvwIt->second) ? pvwIt->second.get() : nullptr;

    double oldPgmPos = (pgmIt != m_roleInstances.end() && pgmIt->second) ? pgmIt->second->positionSeconds() : 0.0;
    double oldPvwPos = (pvwIt != m_roleInstances.end() && pvwIt->second) ? pvwIt->second->positionSeconds() : 0.0;

    bool oldPgmPlaying = (pgmIt != m_roleInstances.end() && pgmIt->second) ? pgmIt->second->isPlaying() : false;
    bool oldPvwPlaying = (pvwIt != m_roleInstances.end() && pvwIt->second) ? pvwIt->second->isPlaying() : false;

    LOG_INFO("[SWAP BEFORE] PGM #{} (ptr={}) pos={:.2f}s playing={} | PVW #{} (ptr={}) pos={:.2f}s playing={}",
             oldPgmId, oldPgmPtr, oldPgmPos, oldPgmPlaying ? "true" : "false",
             oldPvwId, oldPvwPtr, oldPvwPos, oldPvwPlaying ? "true" : "false");

    std::swap(m_roleInstances[PlaybackRole::Program], m_roleInstances[PlaybackRole::Preview]);
    std::swap(m_roleSlotIds[PlaybackRole::Program], m_roleSlotIds[PlaybackRole::Preview]);
    std::swap(m_pgmSlotId, m_pvwSlotId);

    // Audio & Playback state normalization: Program MUST be PLAYING, Preview MUST be PAUSED
    if (m_roleInstances[PlaybackRole::Program]) {
        m_roleInstances[PlaybackRole::Program]->setAudioActive(true);
        m_roleInstances[PlaybackRole::Program]->setDecodeMode(DecodeMode::Active);
        m_roleInstances[PlaybackRole::Program]->play();
    }
    if (m_roleInstances[PlaybackRole::Preview]) {
        m_roleInstances[PlaybackRole::Preview]->setAudioActive(false);
        m_roleInstances[PlaybackRole::Preview]->setDecodeMode(DecodeMode::Active);
        m_roleInstances[PlaybackRole::Preview]->pause();
    }

    auto newPgmIt = m_roleInstances.find(PlaybackRole::Program);
    auto newPvwIt = m_roleInstances.find(PlaybackRole::Preview);

    void* newPgmPtr = (newPgmIt != m_roleInstances.end() && newPgmIt->second) ? newPgmIt->second.get() : nullptr;
    void* newPvwPtr = (newPvwIt != m_roleInstances.end() && newPvwIt->second) ? newPvwIt->second.get() : nullptr;

    double newPgmPos = (newPgmIt != m_roleInstances.end() && newPgmIt->second) ? newPgmIt->second->positionSeconds() : 0.0;
    double newPvwPos = (newPvwIt != m_roleInstances.end() && newPvwIt->second) ? newPvwIt->second->positionSeconds() : 0.0;

    bool newPgmPlaying = (newPgmIt != m_roleInstances.end() && newPgmIt->second) ? newPgmIt->second->isPlaying() : false;
    bool newPvwPlaying = (newPvwIt != m_roleInstances.end() && newPvwIt->second) ? newPvwIt->second->isPlaying() : false;

    LOG_INFO("[SWAP AFTER] PGM #{} (ptr={}) pos={:.2f}s playing={} | PVW #{} (ptr={}) pos={:.2f}s playing={}",
             m_pgmSlotId, newPgmPtr, newPgmPos, newPgmPlaying ? "true" : "false",
             m_pvwSlotId, newPvwPtr, newPvwPos, newPvwPlaying ? "true" : "false");

    std::string pvwDetails = "Slot #" + std::to_string(m_pvwSlotId) + " pos=" + std::to_string(newPvwPos) + "s playing=" + (newPvwPlaying ? "1" : "0");
    std::string pgmDetails = "Slot #" + std::to_string(m_pgmSlotId) + " pos=" + std::to_string(newPgmPos) + "s playing=" + (newPgmPlaying ? "1" : "0");
    MediaDiagnostics::instance().logSwapSnapshot(pvwDetails, pgmDetails, 4);
}

void PlaybackManager::updateState(int pgmSlotId, int pvwSlotId, int preloadSlotId,
                                   const std::unordered_map<int, std::string>& slotPaths,
                                   const std::unordered_map<int, SourceType>& slotTypes) {
    std::lock_guard<std::mutex> lock(m_mutex);

    int oldPgmId = m_roleSlotIds.count(PlaybackRole::Program) ? m_roleSlotIds[PlaybackRole::Program] : -1;
    int oldPvwId = m_roleSlotIds.count(PlaybackRole::Preview) ? m_roleSlotIds[PlaybackRole::Preview] : -1;

    // Detect direct role swap condition
    if (pgmSlotId > 0 && pvwSlotId > 0 && pgmSlotId != pvwSlotId &&
        pgmSlotId == oldPvwId && pvwSlotId == oldPgmId) {

        LOG_INFO("PlaybackManager: Handling role swap in updateState PGM #{} <-> PVW #{}", pgmSlotId, pvwSlotId);

        std::swap(m_roleInstances[PlaybackRole::Program], m_roleInstances[PlaybackRole::Preview]);
        std::swap(m_roleSlotIds[PlaybackRole::Program], m_roleSlotIds[PlaybackRole::Preview]);
        m_pgmSlotId = pgmSlotId;
        m_pvwSlotId = pvwSlotId;
        m_preloadSlotId = preloadSlotId;

        if (m_roleInstances[PlaybackRole::Program]) {
            m_roleInstances[PlaybackRole::Program]->setAudioActive(true);
            m_roleInstances[PlaybackRole::Program]->setDecodeMode(DecodeMode::Active);
            m_roleInstances[PlaybackRole::Program]->play();
        }
        if (m_roleInstances[PlaybackRole::Preview]) {
            m_roleInstances[PlaybackRole::Preview]->setAudioActive(false);
            m_roleInstances[PlaybackRole::Preview]->setDecodeMode(DecodeMode::Active);
            m_roleInstances[PlaybackRole::Preview]->pause();
        }
        return;
    }

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
                fileSrc->setDecodeMode(role == PlaybackRole::Preload ? DecodeMode::Idle : DecodeMode::Active);

                m_roleInstances[role] = fileSrc;
                m_roleSlotIds[role] = slotId;
                LOG_INFO("PlaybackManager: Allocated role instance for role={} slotId=#{}", roleName, slotId);
            }
        } else {
            auto srcIt = m_roleInstances.find(role);
            if (srcIt != m_roleInstances.end() && srcIt->second) {
                srcIt->second->setAudioActive(role == PlaybackRole::Program);
                srcIt->second->setDecodeMode(role == PlaybackRole::Preload ? DecodeMode::Idle : DecodeMode::Active);
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
        fileSrc->setDecodeMode(DecodeMode::Idle);

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
