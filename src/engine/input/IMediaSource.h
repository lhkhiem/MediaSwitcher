#pragma once

#include "engine/frame/Frame.h"
#include <memory>

class IMediaSource {
public:
    virtual ~IMediaSource() = default;

    virtual bool open() = 0;
    virtual void close() = 0;
    
    // Returns the latest available frame, or nullptr if none
    virtual std::shared_ptr<Frame> getFrame() = 0;
};
