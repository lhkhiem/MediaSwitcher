#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <chrono>
#include "common/logger/Logger.h"

enum class PlaylistState {
    Idle,
    Playing,
    Paused,
    Transitioning,
    Ended
};

struct GlobalPlaylistStep {
    int slotId{0};
    double customDurationSec{0.0}; // 0.0 = full video duration / EOF
    std::string transitionType{"FADE"}; // "CUT" or "FADE"
};

class GlobalPlaylistController {
public:
    GlobalPlaylistController() = default;

    void setSteps(const std::vector<GlobalPlaylistStep>& steps) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_steps = steps;
    }

    std::vector<GlobalPlaylistStep> steps() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_steps;
    }

    void start() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_steps.empty()) return;
        m_active = true;
        m_paused = false;
        m_state = PlaylistState::Playing;
        m_currentIndex = 0;
        m_stepStartTime = std::chrono::steady_clock::now();
        LOG_INFO("[PLAYLIST] Started. Total steps: {}", m_steps.size());
    }

    void stop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_active = false;
        m_paused = false;
        m_state = PlaylistState::Idle;
        LOG_INFO("[PLAYLIST] Stopped.");
    }

    void pause() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_active) {
            m_paused = true;
            m_state = PlaylistState::Paused;
            LOG_INFO("[PLAYLIST] Paused at step #{}", m_currentIndex);
        }
    }

    void resume() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_active) {
            m_paused = false;
            m_state = PlaylistState::Playing;
            m_stepStartTime = std::chrono::steady_clock::now();
            LOG_INFO("[PLAYLIST] Resumed at step #{}", m_currentIndex);
        }
    }

    // Single Transition Path - Advance (Auto or Manual)
    // Mutates m_currentIndex ONLY here. Sets state to Transitioning.
    bool advance(bool isManual = false) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_active || m_steps.empty()) return false;
        if (m_state == PlaylistState::Transitioning && !isManual) {
            return false; // Prevent double advance during transition
        }

        size_t oldIndex = m_currentIndex;
        int oldSlot = (oldIndex < m_steps.size()) ? m_steps[oldIndex].slotId : -1;

        size_t nextIdx = m_currentIndex + 1;
        if (nextIdx >= m_steps.size()) {
            if (m_loop) {
                nextIdx = 0;
            } else {
                m_active = false;
                m_state = PlaylistState::Ended;
                LOG_INFO("[PLAYLIST] Reached end of sequence (Loop OFF). Stopped.");
                return false;
            }
        }

        m_currentIndex = nextIdx;
        m_paused = false;
        m_state = PlaylistState::Transitioning;
        m_stepStartTime = std::chrono::steady_clock::now();

        int newSlot = m_steps[m_currentIndex].slotId;
        LOG_INFO("[PLAYLIST ADVANCE] manual={} oldIndex={} -> newIndex={} (oldSlot #{} -> newSlot #{}) loopEnabled={}",
                 isManual ? 1 : 0, oldIndex, m_currentIndex, oldSlot, newSlot, m_loop ? 1 : 0);

        return true;
    }

    // Single Transition Path - Previous (Manual)
    bool previous() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_active || m_steps.empty()) return false;

        size_t oldIndex = m_currentIndex;
        int oldSlot = (oldIndex < m_steps.size()) ? m_steps[oldIndex].slotId : -1;

        if (m_currentIndex == 0) {
            m_currentIndex = m_steps.size() - 1;
        } else {
            m_currentIndex--;
        }

        m_paused = false;
        m_state = PlaylistState::Transitioning;
        m_stepStartTime = std::chrono::steady_clock::now();

        int newSlot = m_steps[m_currentIndex].slotId;
        LOG_INFO("[PLAYLIST PREVIOUS] oldIndex={} -> newIndex={} (oldSlot #{} -> newSlot #{})",
                 oldIndex, m_currentIndex, oldSlot, newSlot);

        return true;
    }

    // Call when new item playback has been initiated on Program
    void completeTransition() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_active) {
            m_paused = false;
            m_state = PlaylistState::Playing;
            m_stepStartTime = std::chrono::steady_clock::now();
            LOG_INFO("[PLAYLIST PROGRAM] Transition completed. Playing step #{} (slot #{})",
                     m_currentIndex, (m_currentIndex < m_steps.size()) ? m_steps[m_currentIndex].slotId : -1);
        }
    }

    // Pure query function - checks if current item is finished.
    // DOES NOT mutate m_currentIndex or m_state.
    bool isCurrentItemFinished(double pgmPosition, double pgmDuration, bool pgmIsPlaying, bool pgmAtEof) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_active || m_paused || m_state != PlaylistState::Playing || m_steps.empty() || m_currentIndex >= m_steps.size()) {
            return false;
        }

        auto now = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(now - m_stepStartTime).count();

        // Guard against instant re-trigger (minimum 0.5s per step)
        if (elapsedSec < 0.5) return false;

        const auto& step = m_steps[m_currentIndex];

        if (step.customDurationSec > 0.0) {
            return (elapsedSec >= step.customDurationSec);
        } else {
            // Full Video / EOF mode
            // Explicit EOF condition: decoder at format EOF or position reached duration threshold
            if (pgmAtEof) {
                LOG_INFO("[PLAYLIST STATE] step={} slot={} pos={:.2f}/={:.2f} state=ENDED (format EOF)",
                         m_currentIndex, step.slotId, pgmPosition, pgmDuration);
                return true;
            }
            if (pgmDuration > 0.5 && pgmPosition >= (pgmDuration - 0.15)) {
                LOG_INFO("[PLAYLIST STATE] step={} slot={} pos={:.2f}/={:.2f} state=ENDED (duration threshold)",
                         m_currentIndex, step.slotId, pgmPosition, pgmDuration);
                return true;
            }
            if (pgmDuration <= 0.0 && elapsedSec >= 5.0) { // Still image fallback
                LOG_INFO("[PLAYLIST STATE] step={} slot={} elapsed={:.1f}s state=ENDED (still fallback)",
                         m_currentIndex, step.slotId, elapsedSec);
                return true;
            }
        }

        return false;
    }

    PlaylistState state() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_state;
    }

    bool isPaused() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_paused;
    }

    bool isActive() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_active;
    }

    size_t currentIndex() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_currentIndex;
    }

    void setLoop(bool loop) { m_loop = loop; }
    bool isLoop() const { return m_loop; }

    void resetStepTimer() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stepStartTime = std::chrono::steady_clock::now();
    }

    GlobalPlaylistStep currentStep() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_currentIndex < m_steps.size()) return m_steps[m_currentIndex];
        return GlobalPlaylistStep{};
    }

    GlobalPlaylistStep nextStep() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_steps.empty()) return GlobalPlaylistStep{};
        size_t nextIdx = (m_currentIndex + 1) % m_steps.size();
        return m_steps[nextIdx];
    }

private:
    mutable std::mutex m_mutex;
    std::vector<GlobalPlaylistStep> m_steps;
    size_t m_currentIndex{0};
    PlaylistState m_state{PlaylistState::Idle};
    bool m_active{false};
    bool m_paused{false};
    bool m_loop{true};
    std::chrono::steady_clock::time_point m_stepStartTime;
};
