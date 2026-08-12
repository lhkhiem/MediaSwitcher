#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class ObsPlaylist final {
public:
    void addSource(uint64_t sourceId);
    bool removeAt(size_t index);
    bool move(size_t from, size_t to);
    void clear();

    bool empty() const { return m_items.empty(); }
    size_t size() const { return m_items.size(); }
    size_t currentIndex() const { return m_currentIndex; }
    uint64_t currentSourceId() const;
    uint64_t sourceIdAt(size_t index) const;
    bool advance();
    bool previous();

    void setLoop(bool enabled) { m_loop = enabled; }
    bool isLooping() const { return m_loop; }
    void setAutoNext(bool enabled) { m_autoNext = enabled; }
    bool isAutoNext() const { return m_autoNext; }

private:
    std::vector<uint64_t> m_items;
    size_t m_currentIndex{0};
    bool m_loop{true};
    bool m_autoNext{true};
};
