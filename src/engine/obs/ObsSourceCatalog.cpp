#include "ObsSourceCatalog.h"

#include <algorithm>

uint64_t ObsSourceCatalog::add(const std::filesystem::path& path) {
    const uint64_t id = m_nextId++;
    m_sources.push_back({id, path});
    return id;
}

bool ObsSourceCatalog::remove(uint64_t id) {
    const auto it = std::find_if(m_sources.begin(), m_sources.end(), [id](const ObsCatalogSource& source) {
        return source.id == id;
    });
    if (it == m_sources.end()) return false;
    m_sources.erase(it);
    return true;
}

std::optional<ObsCatalogSource> ObsSourceCatalog::find(uint64_t id) const {
    const auto it = std::find_if(m_sources.begin(), m_sources.end(), [id](const ObsCatalogSource& source) {
        return source.id == id;
    });
    if (it == m_sources.end()) return std::nullopt;
    return *it;
}
