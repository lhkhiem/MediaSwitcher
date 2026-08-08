#pragma once

#include "engine/frame/Frame.h"
#include <memory>
#include <vector>

class IMediaSource {
public:
    virtual ~IMediaSource() = default;

    virtual bool open() = 0;
    virtual void close() = 0;
    
    // Returns the latest available frame, or nullptr if none
    virtual std::shared_ptr<Frame> getFrame() = 0;

    // Fills output buffer with interleaved audio PCM float samples (Stereo 48kHz: L, R...)
    virtual size_t getAudioSamples(float* buffer, size_t maxSamples) {
        (void)buffer;
        (void)maxSamples;
        return 0;
    }

    // Per-channel volume control interface
    virtual void setVolume(float vol) { (void)vol; }
    virtual float volume() const { return 1.0f; }
    virtual void setMuted(bool mute) { (void)mute; }
    virtual bool isMuted() const { return false; }

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
