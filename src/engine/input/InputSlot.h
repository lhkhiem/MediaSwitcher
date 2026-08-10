#pragma once

#include "IMediaSource.h"
#include "SourceInfo.h"
#include <string>
#include <memory>
#include <QImage>

using InputType = SourceType;

struct InputSlot {
    int id{0};
    std::string name;
    InputType type{InputType::ColorBars};
    std::string filePath;
    std::shared_ptr<IMediaSource> source{nullptr}; // Active playback instance (nullptr when Idle)
    QImage thumbnail;
    bool thumbnailReady{false};
    SourceState state{SourceState::Idle};
};
