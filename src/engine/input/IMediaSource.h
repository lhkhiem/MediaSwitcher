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

    // Playback control interface
    virtual double durationSeconds() const { return 0.0; }
    virtual double positionSeconds() const { return 0.0; }
    virtual void seekToSeconds(double seconds) {}
    virtual void setLoop(bool loop) {}
    virtual bool isLoop() const { return false; }
    virtual void play() {}
    virtual void pause() {}
    virtual bool isPlaying() const { return true; }
};
