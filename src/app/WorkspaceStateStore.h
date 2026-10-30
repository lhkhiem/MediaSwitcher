#pragma once

#include "engine/obs/ObsSourceCatalog.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct WorkspaceZoneState {
    int x{0};
    int y{0};
    int width{640};
    int height{1080};
    int renderMode{0};
    uint64_t sourceId{0};
    int64_t positionMs{0};
    bool loop{false};
    float volume{1.0f};
    bool muted{false};
    bool blackout{false};
};

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
    uint32_t fpsNumerator{0};
    uint32_t fpsDenominator{0};
    int programRenderMode{0};
    int fadeDurationMs{700};
    int thumbnailWidth{192};
    int sourceTypeFilter{-1};
    int64_t previewPositionMs{0};
    int64_t programPositionMs{0};
    bool previewLoop{false};
    bool programLoop{false};
    float previewVolume{1.0f};
    float programVolume{1.0f};
    bool previewMuted{false};
    bool programMuted{false};
    std::string outputScreenName;
    int layoutMode{0};
    int canvasWidth{3200};
    int canvasHeight{1080};
    int activeZoneIndex{1};
    bool blackoutAll{false};
    std::array<WorkspaceZoneState, 3> zones{{
        {0, 0, 640, 1080, 0, 0, 0, false, 1.0f, true, false},
        {640, 0, 1920, 1080, 0, 0, 0, false, 1.0f, false, false},
        {2560, 0, 640, 1080, 0, 0, 0, false, 1.0f, true, false},
    }};
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
