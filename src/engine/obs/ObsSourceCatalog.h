#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

enum class ObsCatalogSourceType {
    VideoFile,
    AudioFile,
    ImageFile,
    RtspCamera,
    ColorBlank,
};

struct ObsCatalogSource {
    uint64_t id{0};
    ObsCatalogSourceType type{ObsCatalogSourceType::VideoFile};
    std::filesystem::path path;
    std::string endpoint;
    std::string displayName;
    bool systemSource{false};
};

ObsCatalogSourceType classifyObsLocalFile(const std::filesystem::path& path);
const char* obsCatalogSourceTypeName(ObsCatalogSourceType type);
bool obsCatalogSourceHasTimeline(ObsCatalogSourceType type);

class ObsSourceCatalog final {
public:
    uint64_t add(const std::filesystem::path& path);
    uint64_t addRtsp(std::string endpoint, std::string displayName);
    uint64_t addSystemBlank(std::string displayName);
    bool remove(uint64_t id);
    std::optional<ObsCatalogSource> find(uint64_t id) const;
    const std::vector<ObsCatalogSource>& sources() const { return m_sources; }

private:
    uint64_t replaceFirstBlank(ObsCatalogSourceType type, std::filesystem::path path, std::string endpoint,
                               std::string displayName);
    void reconcileSystemBlanks();

    uint64_t m_nextId{1};
    std::vector<ObsCatalogSource> m_sources;
};
