#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <chrono>

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
        m_currentIndex = 0;
        m_stepStartTime = std::chrono::steady_clock::now();
    }

    void stop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_active = false;
        m_paused = false;
    }

    void pause() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_active) m_paused = true;
    }

    void resume() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_active) {
            m_paused = false;
            m_stepStartTime = std::chrono::steady_clock::now();
        }
    }

    void prevStep() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_steps.empty()) return;
        if (m_currentIndex == 0) {
            m_currentIndex = m_steps.size() - 1;
        } else {
            m_currentIndex--;
        }
        m_stepStartTime = std::chrono::steady_clock::now();
    }

    void nextStepManual() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_steps.empty()) return;
        m_currentIndex = (m_currentIndex + 1) % m_steps.size();
        m_stepStartTime = std::chrono::steady_clock::now();
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

    bool checkAdvance(double pgmPosition, double pgmDuration, int pgmSlotId, bool pgmIsPlaying) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_active || m_paused || m_steps.empty() || m_currentIndex >= m_steps.size()) return false;

        (void)pgmSlotId;

        auto now = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(now - m_stepStartTime).count();

        // Guard against instant re-trigger (minimum 0.8s per step)
        if (elapsedSec < 0.8) return false;

        const auto& currentStep = m_steps[m_currentIndex];

        bool shouldAdvance = false;

        if (currentStep.customDurationSec > 0.0) {
            if (elapsedSec >= currentStep.customDurationSec) {
                shouldAdvance = true;
            }
        } else { // Full Video / EOF mode
            if (pgmDuration > 0.5 && (pgmPosition >= pgmDuration - 0.05 || !pgmIsPlaying)) {
                shouldAdvance = true;
            } else if (pgmDuration <= 0.0 && elapsedSec >= 5.0) { // Still image fallback
                shouldAdvance = true;
            }
        }

        if (shouldAdvance) {
            m_currentIndex++;
            if (m_currentIndex >= m_steps.size()) {
                if (m_loop) {
                    m_currentIndex = 0;
                } else {
                    m_active = false;
                    return false;
                }
            }
            m_stepStartTime = now;
            return true;
        }

        return false;
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
    bool m_active{false};
    bool m_paused{false};
    bool m_loop{true};
    std::chrono::steady_clock::time_point m_stepStartTime;
};
