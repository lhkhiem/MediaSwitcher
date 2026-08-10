#include "SourceRegistry.h"
#include <algorithm>

int SourceRegistry::addSource(const SourceInfo& info) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sources.push_back(info);
    return info.id;
}

bool SourceRegistry::removeSource(int id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_sources.begin(), m_sources.end(), [id](const SourceInfo& s) {
        return s.id == id;
    });
    if (it != m_sources.end()) {
        m_sources.erase(it);
        return true;
    }
    return false;
}

std::vector<SourceInfo> SourceRegistry::getAllSources() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sources;
}

std::optional<SourceInfo> SourceRegistry::getSource(int id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& s : m_sources) {
        if (s.id == id) return s;
    }
    return std::nullopt;
}

bool SourceRegistry::updateThumbnail(int id, const QImage& thumbnail) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& s : m_sources) {
        if (s.id == id) {
            s.thumbnail = thumbnail;
            s.thumbnailReady = true;
            return true;
        }
    }
    return false;
}

bool SourceRegistry::updateState(int id, SourceState state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& s : m_sources) {
        if (s.id == id) {
            s.state = state;
            return true;
        }
    }
    return false;
}

void SourceRegistry::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sources.clear();
}
