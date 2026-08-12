#include "ObsPlaylist.h"

#include <stdexcept>

void ObsPlaylist::setItems(std::vector<std::filesystem::path> items) {
    m_items = std::move(items);
    m_currentIndex = 0;
}

void ObsPlaylist::appendItems(std::vector<std::filesystem::path> items) {
    m_items.insert(m_items.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
}

void ObsPlaylist::clear() {
    m_items.clear();
    m_currentIndex = 0;
}

const std::filesystem::path& ObsPlaylist::current() const {
    if (m_items.empty()) throw std::logic_error("OBS playlist has no current item");
    return m_items[m_currentIndex];
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

const std::filesystem::path* ObsPlaylist::offset(size_t distance) const {
    if (m_items.empty()) return nullptr;
    const size_t index = m_currentIndex + distance;
    if (index < m_items.size()) return &m_items[index];
    if (!m_loop) return nullptr;
    return &m_items[index % m_items.size()];
}
