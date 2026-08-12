#include "ObsPlaybackBackend.h"

#include "ObsContext.h"
#include "common/logger/Logger.h"

extern "C" {
#include <graphics/graphics.h>
#include <obs.h>
#include <callback/signal.h>
}

#include <algorithm>
#include <QString>

namespace {
constexpr const char* kSourceType = "ffmpeg_source";
constexpr const char* kSourceName = "MediaSwitcher OBS Media Prototype";

std::string toUtf8Path(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring()).toUtf8().toStdString();
}
}

ObsPlaybackBackend::ObsPlaybackBackend(ObsContext& context) : m_context(context) {}
ObsPlaybackBackend::~ObsPlaybackBackend() { close(); }

bool ObsPlaybackBackend::open(const std::filesystem::path& path, bool startPaused) {
    close();
    m_pauseRequested.store(startPaused);
    if (!m_context.isInitialized()) {
        LOG_ERROR("OBS media: Cannot open source because ObsContext is not initialized.");
        return false;
    }
    if (!std::filesystem::is_regular_file(path)) {
        LOG_ERROR("OBS media: File does not exist or is not a regular file: '{}'.", toUtf8Path(path));
        return false;
    }

    const auto absolutePath = std::filesystem::absolute(path);
    const QByteArray utf8Path = QString::fromStdWString(absolutePath.wstring()).toUtf8();
    obs_data_t* settings = obs_data_create();
    if (!settings) {
        LOG_ERROR("OBS media: Failed to allocate source settings.");
        return false;
    }
    obs_data_set_bool(settings, "is_local_file", true);
    obs_data_set_string(settings, "local_file", utf8Path.constData());
    obs_data_set_bool(settings, "looping", m_looping);
    // Create the source inactive. This lets a Preview request register its
    // paused intent before ffmpeg_source receives its first start action.
    obs_data_set_bool(settings, "restart_on_activate", true);
    obs_data_set_bool(settings, "clear_on_media_end", true);

    LOG_INFO("OBS media: Creating ffmpeg_source with UTF-8 path.");
    m_source = obs_source_create(kSourceType, kSourceName, settings, nullptr);
    obs_data_release(settings);
    if (!m_source) {
        LOG_ERROR("OBS media: obs_source_create('{}') failed for '{}'.", kSourceType, toUtf8Path(absolutePath));
        return false;
    }
    LOG_INFO("OBS media: Source creation succeeded; creating view.");
    m_view = obs_view_create();
    if (!m_view) {
        LOG_ERROR("OBS media: obs_view_create failed.");
        obs_source_release(m_source);
        m_source = nullptr;
        return false;
    }

    LOG_INFO("OBS media: Attaching source to view.");
    obs_view_set_source(m_view, 0, m_source);
    connectMediaSignals();
    // obs_view_create creates an AUX view.  It renders video but does not give
    // the source an audio activation reference, which the WASAPI monitor needs.
    obs_source_inc_active(m_source);
    m_sourceActive = true;
    LOG_INFO("OBS media: Source activated for audio monitoring: active={}", obs_source_active(m_source));
    LOG_INFO("OBS media: Source attached; enabling audio monitoring.");
    m_path = absolutePath;
    m_audioMonitoringEnabled = m_audioOutputEnabled && setAudioMonitoring(true);
    LOG_INFO("OBS media: Created '{}' source for '{}'; startPaused={}", kSourceType, toUtf8Path(m_path), startPaused);
    return true;
}

void ObsPlaybackBackend::close() {
    if (!m_source && !m_view) return;
    LOG_INFO("OBS media: Destroying source for '{}'.", toUtf8Path(m_path));
    setAudioMonitoring(false);
    disconnectMediaSignals();
    if (m_source && m_sourceActive) {
        obs_source_dec_active(m_source);
        m_sourceActive = false;
    }
    if (m_view) {
        obs_view_set_source(m_view, 0, nullptr);
        obs_view_destroy(m_view);
        m_view = nullptr;
    }
    if (m_source) {
        obs_source_release(m_source);
        m_source = nullptr;
    }
    m_path.clear();
    m_audioMonitoringEnabled = false;
    m_sourceActive = false;
    m_pauseRequested.store(false);
    m_mediaEnded.store(false);
}

void ObsPlaybackBackend::play() {
    if (!m_source) return;

    m_pauseRequested.store(false);
    // Recreate the monitor before resuming so it starts from OBS's current media clock.
    if (m_audioOutputEnabled && !setAudioMonitoring(true)) {
        LOG_ERROR("OBS media: Play/resume cancelled because audio monitoring could not be enabled.");
        return;
    }
    obs_source_media_play_pause(m_source, false);
    m_mediaEnded.store(false);
    LOG_INFO("OBS media: Play/resume requested after audio monitor flush.");
}

void ObsPlaybackBackend::pause() {
    if (!m_source) return;

    // ffmpeg_source may finish opening after this call. Keep this intent so
    // onMediaStarted can pause it again after OBS completes asynchronous open.
    m_pauseRequested.store(true);
    // Destroying the monitor stops its WASAPI client and discards PCM already queued to it.
    setAudioMonitoring(false);
    obs_source_media_play_pause(m_source, true);
    LOG_INFO("OBS media: Pause requested after audio monitor flush.");
}

void ObsPlaybackBackend::enforcePendingPause() {
    if (!m_source || !m_pauseRequested.load() || state() != ObsPlaybackState::Playing) return;

    // ffmpeg_source ignores a pause issued while its start callback is still
    // running. Call on the next UI timer tick, after media_started returns.
    setAudioMonitoring(false);
    obs_source_media_play_pause(m_source, true);
    LOG_INFO("OBS media: Applied pending pause after source startup completed.");
}

void ObsPlaybackBackend::stop() {
    if (!m_source) return;

    m_pauseRequested.store(true);
    setAudioMonitoring(false);
    obs_source_media_stop(m_source);
    m_mediaEnded.store(false);
    LOG_INFO("OBS media: Stop requested after audio monitor flush.");
}

bool ObsPlaybackBackend::seekMs(int64_t milliseconds) {
    if (!m_source) return false;
    const int64_t duration = durationMs();
    const int64_t target = std::clamp(milliseconds, int64_t{0}, duration > 0 ? duration : milliseconds);
    const bool resumeAfterSeek = state() == ObsPlaybackState::Playing;
    setAudioMonitoring(false);
    obs_source_media_set_time(m_source, target);
    m_mediaEnded.store(false);
    if (resumeAfterSeek && m_audioOutputEnabled && !setAudioMonitoring(true)) {
        LOG_ERROR("OBS media: Seek completed but audio monitoring could not be re-enabled.");
        return false;
    }
    LOG_INFO("OBS media: Seek requested to {} ms after audio monitor flush.", target);
    return true;
}

int64_t ObsPlaybackBackend::positionMs() const { return m_source ? obs_source_media_get_time(m_source) : 0; }
int64_t ObsPlaybackBackend::durationMs() const { return m_source ? obs_source_media_get_duration(m_source) : 0; }
ObsPlaybackState ObsPlaybackBackend::state() const { return m_source ? mapState(static_cast<int>(obs_source_media_get_state(m_source))) : ObsPlaybackState::None; }
bool ObsPlaybackBackend::isAvailable() const { return m_context.isInitialized(); }
bool ObsPlaybackBackend::isOpen() const { return m_source != nullptr; }

void ObsPlaybackBackend::setLooping(bool enabled) {
    if (m_looping == enabled) return;
    m_looping = enabled;
    if (!m_source) return;

    obs_data_t* settings = obs_source_get_settings(m_source);
    if (!settings) {
        LOG_ERROR("OBS media: Cannot update loop setting because source settings are unavailable.");
        return;
    }
    obs_data_set_bool(settings, "looping", enabled);
    obs_source_update(m_source, settings);
    obs_data_release(settings);
    LOG_INFO("OBS media: Looping {}.", enabled ? "enabled" : "disabled");
}

void ObsPlaybackBackend::setAudioOutputEnabled(bool enabled) {
    if (m_audioOutputEnabled == enabled) return;

    m_audioOutputEnabled = enabled;
    if (!m_source) return;

    if (!enabled) {
        setAudioMonitoring(false);
        LOG_INFO("OBS media: Audio output disabled for this playback instance.");
        return;
    }

    if (state() == ObsPlaybackState::Playing && !setAudioMonitoring(true)) {
        LOG_ERROR("OBS media: Failed to enable audio output for the playing instance.");
    } else {
        LOG_INFO("OBS media: Audio output enabled for this playback instance.");
    }
}

void ObsPlaybackBackend::setRenderSource(obs_source_t* source) {
    if (m_view) obs_view_set_source(m_view, 0, source);
}

void ObsPlaybackBackend::resetRenderSource() {
    setRenderSource(m_source);
}

void ObsPlaybackBackend::render(uint32_t width, uint32_t height) const {
    if (!m_view || width == 0 || height == 0) return;
    const uint32_t sourceWidth = m_source ? obs_source_get_base_width(m_source) : 0;
    const uint32_t sourceHeight = m_source ? obs_source_get_base_height(m_source) : 0;
    if (sourceWidth == 0 || sourceHeight == 0) return;
    const float scale = std::min(static_cast<float>(width) / sourceWidth, static_cast<float>(height) / sourceHeight);
    const float offsetX = (static_cast<float>(width) - sourceWidth * scale) / 2.0f;
    const float offsetY = (static_cast<float>(height) - sourceHeight * scale) / 2.0f;
    gs_viewport_push(); gs_projection_push(); gs_matrix_push();
    gs_set_viewport(0, 0, static_cast<int>(width), static_cast<int>(height));
    gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
    gs_matrix_translate3f(offsetX, offsetY, 0.0f);
    gs_matrix_scale3f(scale, scale, 1.0f);
    obs_view_render(m_view);
    gs_matrix_pop(); gs_projection_pop(); gs_viewport_pop();
}

void ObsPlaybackBackend::logDiagnostics() const {
    LOG_INFO("OBS media: state={} position={} ms duration={} ms looping={} ended={} audioOutput={} monitored={} sourceActive={}", static_cast<int>(state()), positionMs(), durationMs(), m_looping, m_mediaEnded.load(), m_audioOutputEnabled, m_audioMonitoringEnabled, m_source && obs_source_active(m_source));
}

void ObsPlaybackBackend::onMediaStarted(void* data, calldata_t*) {
    auto* backend = static_cast<ObsPlaybackBackend*>(data);
    backend->m_mediaEnded.store(false);
    if (backend->m_pauseRequested.load()) LOG_INFO("OBS media: Pending pause will be applied after startup callback.");
    LOG_INFO("OBS media: Source signalled media_started.");
}

void ObsPlaybackBackend::onMediaEnded(void* data, calldata_t*) {
    auto* backend = static_cast<ObsPlaybackBackend*>(data);
    backend->m_mediaEnded.store(true);
    LOG_INFO("OBS media: Source signalled media_ended. looping={}", backend->m_looping);
}

void ObsPlaybackBackend::connectMediaSignals() {
    if (!m_source || m_mediaSignalsConnected) return;
    signal_handler_t* signalHandler = obs_source_get_signal_handler(m_source);
    signal_handler_connect(signalHandler, "media_started", onMediaStarted, this);
    signal_handler_connect(signalHandler, "media_ended", onMediaEnded, this);
    m_mediaSignalsConnected = true;
}

void ObsPlaybackBackend::disconnectMediaSignals() {
    if (!m_source || !m_mediaSignalsConnected) return;
    signal_handler_t* signalHandler = obs_source_get_signal_handler(m_source);
    signal_handler_disconnect(signalHandler, "media_started", onMediaStarted, this);
    signal_handler_disconnect(signalHandler, "media_ended", onMediaEnded, this);
    m_mediaSignalsConnected = false;
}

ObsPlaybackState ObsPlaybackBackend::mapState(int obsState) {
    switch (static_cast<obs_media_state>(obsState)) {
    case OBS_MEDIA_STATE_NONE: return ObsPlaybackState::None;
    case OBS_MEDIA_STATE_OPENING: return ObsPlaybackState::Opening;
    case OBS_MEDIA_STATE_BUFFERING: return ObsPlaybackState::Buffering;
    case OBS_MEDIA_STATE_PLAYING: return ObsPlaybackState::Playing;
    case OBS_MEDIA_STATE_PAUSED: return ObsPlaybackState::Paused;
    case OBS_MEDIA_STATE_STOPPED: return ObsPlaybackState::Stopped;
    case OBS_MEDIA_STATE_ENDED: return ObsPlaybackState::Ended;
    default: return ObsPlaybackState::Error;
    }
}

bool ObsPlaybackBackend::setAudioMonitoring(bool enabled) {
    if (!m_source) return false;

    if (!enabled) {
        if (m_audioMonitoringEnabled) {
            obs_source_set_monitoring_type(m_source, OBS_MONITORING_TYPE_NONE);
            m_audioMonitoringEnabled = false;
            LOG_INFO("OBS media: Audio monitor destroyed and queued WASAPI audio discarded.");
        }
        return true;
    }

    if (m_audioMonitoringEnabled) return true;
    if (!obs_audio_monitoring_available()) {
        LOG_ERROR("OBS media: libobs audio monitoring is unavailable; no speaker output can be validated.");
        return false;
    }
    if (!obs_set_audio_monitoring_device("Default", "default")) {
        LOG_ERROR("OBS media: Failed to select the default audio monitoring device.");
        return false;
    }
    obs_source_set_monitoring_type(m_source, OBS_MONITORING_TYPE_MONITOR_ONLY);
    m_audioMonitoringEnabled = obs_source_get_monitoring_type(m_source) == OBS_MONITORING_TYPE_MONITOR_ONLY;
    if (!m_audioMonitoringEnabled) {
        LOG_ERROR("OBS media: Failed to enable libobs WASAPI monitoring.");
        return false;
    }
    LOG_INFO("OBS media: Enabled libobs WASAPI monitoring to the default output device.");
    return true;
}
