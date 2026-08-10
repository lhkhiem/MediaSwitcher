#pragma once

#include "SourceInfo.h"
#include <mutex>
#include <vector>
#include <algorithm>

class ResourceManager {
public:
    static constexpr size_t MAX_TOTAL_ACTIVE_PLAYBACKS = 3;

    ResourceManager() = default;
    ~ResourceManager() = default;

    // Enforce resource budget (PGM > PVW > Preload > IDLE)
    // Returns a list of slot IDs that should be evicted to IDLE.
    std::vector<int> enforceBudget(int pgmSlotId, int pvwSlotId, int preloadSlotId,
                                   const std::vector<int>& allActiveSlots) {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::vector<int> toEvict;
        std::vector<int> desiredActive;

        // Add by priority: PGM -> PVW -> Preload
        if (pgmSlotId > 0) {
            desiredActive.push_back(pgmSlotId);
        }
        if (pvwSlotId > 0 && std::find(desiredActive.begin(), desiredActive.end(), pvwSlotId) == desiredActive.end()) {
            desiredActive.push_back(pvwSlotId);
        }
        if (preloadSlotId > 0 && std::find(desiredActive.begin(), desiredActive.end(), preloadSlotId) == desiredActive.end()) {
            if (desiredActive.size() < MAX_TOTAL_ACTIVE_PLAYBACKS) {
                desiredActive.push_back(preloadSlotId);
            }
        }

        for (int slotId : allActiveSlots) {
            if (std::find(desiredActive.begin(), desiredActive.end(), slotId) == desiredActive.end()) {
                toEvict.push_back(slotId);
            }
        }

        return toEvict;
    }

private:
    mutable std::mutex m_mutex;
};
