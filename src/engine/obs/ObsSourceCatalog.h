#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

struct ObsCatalogSource {
    uint64_t id{0};
    std::filesystem::path path;
};

class ObsSourceCatalog final {
public:
    uint64_t add(const std::filesystem::path& path);
    bool remove(uint64_t id);
    std::optional<ObsCatalogSource> find(uint64_t id) const;
    const std::vector<ObsCatalogSource>& sources() const { return m_sources; }

private:
    uint64_t m_nextId{1};
    std::vector<ObsCatalogSource> m_sources;
};
