#include "ObsPlaylist.h"

#include <stdexcept>

void ObsPlaylist::addSource(uint64_t sourceId) {
    if (sourceId != 0) m_items.push_back(sourceId);
}

bool ObsPlaylist::removeAt(size_t index) {
    if (index >= m_items.size()) return false;
    m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_items.empty()) m_currentIndex = 0;
    else if (m_currentIndex >= m_items.size()) m_currentIndex = m_items.size() - 1;
    return true;
}

bool ObsPlaylist::move(size_t from, size_t to) {
    if (from >= m_items.size() || to >= m_items.size() || from == to) return false;
    const uint64_t sourceId = m_items[from];
    m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(from));
    m_items.insert(m_items.begin() + static_cast<std::ptrdiff_t>(to), sourceId);
    if (m_currentIndex == from) m_currentIndex = to;
    return true;
}

void ObsPlaylist::clear() {
    m_items.clear();
    m_currentIndex = 0;
}

uint64_t ObsPlaylist::currentSourceId() const {
    if (m_items.empty()) throw std::logic_error("OBS playlist has no current item");
    return m_items[m_currentIndex];
}

uint64_t ObsPlaylist::sourceIdAt(size_t index) const {
    if (index >= m_items.size()) throw std::out_of_range("OBS playlist index is invalid");
    return m_items[index];
}

bool ObsPlaylist::advance() {
    if (m_items.empty()) return false;
    if (m_currentIndex + 1 < m_items.size()) {
        ++m_currentIndex;
        return true;
    }
    if (!m_loop) return false;
    m_currentIndex = 0;
    return true;
}

bool ObsPlaylist::previous() {
    if (m_items.empty()) return false;
    if (m_currentIndex > 0) {
        --m_currentIndex;
        return true;
    }
    if (!m_loop) return false;
    m_currentIndex = m_items.size() - 1;
    return true;
}
