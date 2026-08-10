#pragma once

#include "IMediaSource.h"
#include "FileSource.h"
#include "ResourceManager.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>

class PlaybackManager {
public:
    PlaybackManager() = default;
    ~PlaybackManager() = default;

    // Update active PGM, PVW, and Preload slot assignments
    void updateState(int pgmSlotId, int pvwSlotId, int preloadSlotId,
                     const std::unordered_map<int, std::string>& slotPaths,
                     const std::unordered_map<int, SourceType>& slotTypes);

    // Set a slot to be preloaded in background
    void preloadSlot(int slotId, const std::string& filePath, SourceType type);

    // Retrieve active media source for a slot ID
    std::shared_ptr<IMediaSource> getSource(int slotId);

    void clear();

    size_t activeDecoderCount() const;

private:
    mutable std::mutex m_mutex;
    ResourceManager m_resourceManager;

    int m_pgmSlotId{-1};
    int m_pvwSlotId{-1};
    int m_preloadSlotId{-1};

    // Map slotId -> active FileSource instance
    std::unordered_map<int, std::shared_ptr<FileSource>> m_activeSources;
};
