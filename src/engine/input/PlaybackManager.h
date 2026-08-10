#pragma once

#include "IMediaSource.h"
#include "FileSource.h"
#include "ResourceManager.h"
#include "PlaybackRole.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>

class PlaybackManager {
public:
    PlaybackManager() = default;
    ~PlaybackManager() = default;

    // Explicitly activate/allocate a slot for a specific PlaybackRole.
    void activateSource(PlaybackRole role, int slotId, const std::string& filePath, SourceType type);

    // Update active PGM, PVW, and Preload slot assignments according to ResourceManager budget.
    void updateState(int pgmSlotId, int pvwSlotId, int preloadSlotId,
                     const std::unordered_map<int, std::string>& slotPaths,
                     const std::unordered_map<int, SourceType>& slotTypes);

    // Set a slot to be preloaded in background lazily
    void preloadSlot(int slotId, const std::string& filePath, SourceType type);

    // Read-only lookup for active media source by PlaybackRole.
    // MUST NOT implicitly create a decoder/playback instance.
    std::shared_ptr<IMediaSource> getSource(PlaybackRole role) const;

    // Read-only lookup for role and slotId match
    std::shared_ptr<IMediaSource> getSourceForSlot(int slotId, PlaybackRole role) const;

    void clear();

    size_t activeDecoderCount() const;

private:
    mutable std::mutex m_mutex;
    ResourceManager m_resourceManager;

    int m_pgmSlotId{-1};
    int m_pvwSlotId{-1};
    int m_preloadSlotId{-1};

    // Role-based instances: map PlaybackRole -> dedicated FileSource instance
    std::unordered_map<PlaybackRole, std::shared_ptr<FileSource>> m_roleInstances;
    std::unordered_map<PlaybackRole, int> m_roleSlotIds;
};
