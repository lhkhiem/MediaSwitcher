#include "PlaylistSource.h"
#include "common/logger/Logger.h"
#include <filesystem>
#include <algorithm>

PlaylistSource::PlaylistSource() {}

PlaylistSource::~PlaylistSource() {
    close();
}

bool PlaylistSource::open() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_opened = true;
    loadCurrentTrack();
    return true;
}

void PlaylistSource::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeSource) {
        m_activeSource->close();
        m_activeSource.reset();
    }
    m_opened = false;
}

void PlaylistSource::loadCurrentTrack() {
    if (m_activeSource) {
        m_activeSource->close();
        m_activeSource.reset();
    }

    if (m_tracks.empty() || m_currentIndex >= m_tracks.size()) {
        return;
    }

    const auto& track = m_tracks[m_currentIndex];
    m_activeSource = std::make_shared<FileSource>(track.filePath);
    m_activeSource->open();

    if (m_playing) {
        m_activeSource->play();
    } else {
        m_activeSource->pause();
    }

    m_imageTimerStarted = false;
    LOG_INFO("PlaylistSource: Loaded track #{}/{} '{}'", m_currentIndex + 1, m_tracks.size(), track.name);
}

std::shared_ptr<Frame> PlaylistSource::getFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_activeSource || m_tracks.empty() || m_currentIndex >= m_tracks.size()) {
        return nullptr;
    }

    auto frame = m_activeSource->getFrame();
    const auto& track = m_tracks[m_currentIndex];

    if (track.isImage) {
        if (m_playing) {
            auto now = std::chrono::steady_clock::now();
            if (!m_imageTimerStarted) {
                m_imageStartTime = now;
                m_imageTimerStarted = true;
            } else {
                float elapsed = std::chrono::duration<float>(now - m_imageStartTime).count();
                if (elapsed >= track.imageDurationSec) {
                    if (m_loopTrack) {
                        m_imageStartTime = now;
                    } else if (m_autoAdvance) {
                        m_currentIndex++;
                        if (m_currentIndex >= m_tracks.size()) {
                            m_currentIndex = m_loopPlaylist ? 0 : (m_tracks.size() - 1);
                        }
                        loadCurrentTrack();
                    }
                }
            }
        }
    } else { // Video Track
        if (m_playing) {
            double pos = m_activeSource->positionSeconds();
            double dur = m_activeSource->durationSeconds();
            if (dur > 0.5 && pos >= dur - 0.05) {
                if (m_loopTrack) {
                    // Use loopToBeginning() — drains audio codec tail before seeking,
                    // and does NOT clear m_audioBuffer, so pre-buffered audio plays out.
                    m_activeSource->loopToBeginning();
                } else if (m_autoAdvance) {
                    m_currentIndex++;
                    if (m_currentIndex >= m_tracks.size()) {
                        m_currentIndex = m_loopPlaylist ? 0 : (m_tracks.size() - 1);
                    }
                    loadCurrentTrack();
                }
            }
        }
    }

    return frame;
}


double PlaylistSource::durationSeconds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeSource) return m_activeSource->durationSeconds();
    return 0.0;
}

double PlaylistSource::positionSeconds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeSource) return m_activeSource->positionSeconds();
    return 0.0;
}

void PlaylistSource::seekToSeconds(double seconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeSource) {
        m_activeSource->seekToSeconds(seconds);
    }
}

void PlaylistSource::setLoop(bool loop) {
    setLoopPlaylist(loop);
}

bool PlaylistSource::isLoop() const {
    return isLoopPlaylist();
}

void PlaylistSource::play() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playing = true;
    if (m_activeSource) m_activeSource->play();
    m_imageStartTime = std::chrono::steady_clock::now();
    m_imageTimerStarted = true;
}

void PlaylistSource::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playing = false;
    if (m_activeSource) m_activeSource->pause();
}

bool PlaylistSource::isPlaying() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playing;
}

void PlaylistSource::addTrack(const std::string& filePath, double imageDurationSec) {
    if (filePath.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    PlaylistTrack item;
    item.filePath = filePath;

    std::filesystem::path p(filePath);
    item.name = p.filename().string();
    item.imageDurationSec = imageDurationSec <= 0.0 ? 5.0 : imageDurationSec;

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp" || ext == ".gif" || ext == ".tiff") {
        item.isImage = true;
    } else {
        item.isImage = false;
    }

    m_tracks.push_back(item);

    if (m_tracks.size() == 1 && m_opened) {
        m_currentIndex = 0;
        loadCurrentTrack();
    }
}

void PlaylistSource::removeTrack(size_t index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_tracks.size()) return;

    m_tracks.erase(m_tracks.begin() + index);
    if (m_currentIndex >= m_tracks.size()) {
        m_currentIndex = m_tracks.empty() ? 0 : (m_tracks.size() - 1);
    }
    loadCurrentTrack();
}

void PlaylistSource::moveTrack(size_t fromIndex, size_t toIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (fromIndex >= m_tracks.size() || toIndex >= m_tracks.size() || fromIndex == toIndex) return;

    auto item = m_tracks[fromIndex];
    m_tracks.erase(m_tracks.begin() + fromIndex);
    m_tracks.insert(m_tracks.begin() + toIndex, item);

    if (m_currentIndex == fromIndex) {
        m_currentIndex = toIndex;
    }
}

void PlaylistSource::clearTracks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tracks.clear();
    m_currentIndex = 0;
    if (m_activeSource) {
        m_activeSource->close();
        m_activeSource.reset();
    }
}

void PlaylistSource::nextTrack() {
    if (m_tracks.empty()) return;
    m_currentIndex++;
    if (m_currentIndex >= m_tracks.size()) {
        m_currentIndex = m_loopPlaylist ? 0 : (m_tracks.size() - 1);
    }
    loadCurrentTrack();
}

void PlaylistSource::prevTrack() {
    if (m_tracks.empty()) return;
    if (m_currentIndex > 0) {
        m_currentIndex--;
    } else if (m_loopPlaylist) {
        m_currentIndex = m_tracks.size() - 1;
    }
    loadCurrentTrack();
}

void PlaylistSource::setTrackIndex(size_t index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_tracks.size()) return;
    m_currentIndex = index;
    loadCurrentTrack();
}

size_t PlaylistSource::currentTrackIndex() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentIndex;
}

size_t PlaylistSource::trackCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tracks.size();
}

std::vector<PlaylistTrack> PlaylistSource::tracks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tracks;
}
