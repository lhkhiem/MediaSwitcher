#pragma once

#include "IMediaSource.h"
#include <string>
#include <memory>
#include <QImage>

enum class InputType {
    ColorBars,
    VideoFile,
    ImageFile,
    RTSPStream
};

struct InputSlot {
    int id{0};
    std::string name;
    InputType type{InputType::ColorBars};
    std::string filePath;
    std::shared_ptr<IMediaSource> source;
    QImage thumbnail;
};
