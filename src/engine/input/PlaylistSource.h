#pragma once

#include "IMediaSource.h"
#include "FileSource.h"
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <chrono>

struct PlaylistTrack {
    std::string filePath;
    std::string name;
    bool isImage{false};
    double imageDurationSec{5.0};
};

class PlaylistSource : public IMediaSource {
public:
    PlaylistSource();
    ~PlaylistSource() override;

    bool open() override;
    void close() override;
    std::shared_ptr<Frame> getFrame() override;
    size_t getAudioSamples(float* buffer, size_t maxSamples) override;

    double durationSeconds() const override;
    double positionSeconds() const override;
    void seekToSeconds(double seconds) override;
    void setLoop(bool loop) override;
    bool isLoop() const override;
    void play() override;
    void pause() override;
    bool isPlaying() const override;

    // Playlist Control API
    void addTrack(const std::string& filePath, double imageDurationSec = 5.0);
    void removeTrack(size_t index);
    void moveTrack(size_t fromIndex, size_t toIndex);
    void clearTracks();

    void nextTrack();
    void prevTrack();
    void setTrackIndex(size_t index);

    size_t currentTrackIndex() const;
    size_t trackCount() const;
    std::vector<PlaylistTrack> tracks() const;

    void setLoopPlaylist(bool loop) { m_loopPlaylist = loop; }
    bool isLoopPlaylist() const { return m_loopPlaylist; }

    void setLoopTrack(bool loop) { m_loopTrack = loop; }
    bool isLoopTrack() const { return m_loopTrack; }

    void setAutoAdvance(bool advance) { m_autoAdvance = advance; }
    bool isAutoAdvance() const { return m_autoAdvance; }

private:
    void loadCurrentTrack();

    mutable std::mutex m_mutex;
    std::vector<PlaylistTrack> m_tracks;
    size_t m_currentIndex{0};

    std::shared_ptr<FileSource> m_activeSource{nullptr};
    bool m_opened{false};
    bool m_playing{false};
    bool m_loopPlaylist{true};
    bool m_loopTrack{false};
    bool m_autoAdvance{true};

    std::chrono::steady_clock::time_point m_imageStartTime;
    bool m_imageTimerStarted{false};
};
