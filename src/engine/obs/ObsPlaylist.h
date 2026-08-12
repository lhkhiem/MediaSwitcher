#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

class ObsPlaylist final {
public:
    void setItems(std::vector<std::filesystem::path> items);
    void appendItems(std::vector<std::filesystem::path> items);
    void clear();

    bool empty() const { return m_items.empty(); }
    size_t size() const { return m_items.size(); }
    size_t currentIndex() const { return m_currentIndex; }
    const std::filesystem::path& current() const;
    bool advance();
    bool previous();
    const std::filesystem::path* offset(size_t distance) const;

    void setLoop(bool enabled) { m_loop = enabled; }
    bool isLooping() const { return m_loop; }
    void setAutoNext(bool enabled) { m_autoNext = enabled; }
    bool isAutoNext() const { return m_autoNext; }

private:
    std::vector<std::filesystem::path> m_items;
    size_t m_currentIndex{0};
    bool m_loop{true};
    bool m_autoNext{true};
};
