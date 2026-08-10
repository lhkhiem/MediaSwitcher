#pragma once

#include "SourceInfo.h"
#include <vector>
#include <mutex>
#include <optional>

class SourceRegistry {
public:
    SourceRegistry() = default;
    ~SourceRegistry() = default;

    int addSource(const SourceInfo& info);
    bool removeSource(int id);

    std::vector<SourceInfo> getAllSources() const;
    std::optional<SourceInfo> getSource(int id) const;
    bool updateThumbnail(int id, const QImage& thumbnail);
    bool updateState(int id, SourceState state);

    void clear();

private:
    mutable std::mutex m_mutex;
    std::vector<SourceInfo> m_sources;
};
