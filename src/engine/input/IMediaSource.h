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

    // Per-channel volume control interface
    virtual void setVolume(float vol) { (void)vol; }
    virtual float volume() const { return 1.0f; }
    virtual void setMuted(bool mute) { (void)mute; }
    virtual bool isMuted() const { return false; }

    // Playback control interface
    virtual double durationSeconds() const { return 0.0; }
    virtual double positionSeconds() const { return 0.0; }
    virtual void seekToSeconds(double seconds) {}
    // Seamless loop: flushes codec, seeks to 0, but does NOT clear the audio buffer.
    // Use this for looping instead of seekToSeconds(0) to preserve pre-buffered audio.
    virtual void loopToBeginning() { seekToSeconds(0.0); }
    virtual void setLoop(bool loop) {}
    virtual bool isLoop() const { return false; }
    virtual void play() {}
    virtual void pause() {}
    virtual bool isPlaying() const { return true; }

    // Audio routing: only the active PGM source should submit audio to AudioEngine
    virtual void setAudioActive(bool active) { (void)active; }
    virtual bool isAudioActive() const { return false; }
};
