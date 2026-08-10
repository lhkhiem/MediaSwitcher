#pragma once

#include <string>
#include <QImage>

enum class SourceState {
    Idle,
    Preloading,
    Ready,
    Playing
};

enum class SourceType {
    ColorBars,
    VideoFile,
    ImageFile,
    RTSPStream,
    Playlist
};

struct SourceInfo {
    int id{0};
    std::string name;
    std::string filePath;
    SourceType type{SourceType::ColorBars};
    SourceState state{SourceState::Idle};

    int width{0};
    int height{0};
    double durationSeconds{0.0};
    double fps{30.0};
    std::string codecName;

    QImage thumbnail; // Scaled to 320x180 RGBA (~230 KB)
    bool thumbnailReady{false};
};
