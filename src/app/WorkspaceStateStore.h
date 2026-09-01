#pragma once

#include "engine/obs/ObsSourceCatalog.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct WorkspaceSourceRecord {
    uint64_t sourceId{0};
    ObsCatalogSourceType type{ObsCatalogSourceType::VideoFile};
    std::filesystem::path path;
    std::string endpoint;
    std::string displayName;
};

struct WorkspaceState {
    std::vector<WorkspaceSourceRecord> sources;
    std::vector<uint64_t> playlistSourceIds;
    uint64_t previewSourceId{0};
    uint64_t programSourceId{0};
    bool playlistLoop{true};
    bool playlistAutoNext{true};
};

enum class WorkspaceLoadStatus {
    Loaded,
    NotFound,
    Invalid,
    IoError,
};

class WorkspaceStateStore final {
public:
    explicit WorkspaceStateStore(std::filesystem::path filePath);

    WorkspaceLoadStatus load(WorkspaceState& state, std::string& error) const;
    bool save(const WorkspaceState& state, std::string& error) const;
    const std::filesystem::path& filePath() const { return m_filePath; }

private:
    std::filesystem::path m_filePath;
};
