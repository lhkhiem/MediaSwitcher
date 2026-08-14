#include "ObsPlaybackBackend.h"

#include "ObsContext.h"
#include "ObsSourceCatalog.h"
#include "common/logger/Logger.h"

extern "C" {
#include <graphics/graphics.h>
#include <obs.h>
#include <callback/signal.h>
}

#include <algorithm>
#include <cmath>
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
    return openConfiguredSource(kSourceType, settings, absolutePath, startPaused, true, true, false);
}

bool ObsPlaybackBackend::open(const ObsCatalogSource& source, bool startPaused) {
    if (source.type == ObsCatalogSourceType::VideoFile || source.type == ObsCatalogSourceType::AudioFile) {
        return open(source.path, startPaused);
    }

    if (!m_context.isInitialized()) {
        LOG_ERROR("OBS media: Cannot open source because ObsContext is not initialized.");
        return false;
    }

    obs_data_t* settings = obs_data_create();
    if (!settings) {
        LOG_ERROR("OBS media: Failed to allocate source settings.");
        return false;
    }

    if (source.type == ObsCatalogSourceType::ImageFile) {
        if (!std::filesystem::is_regular_file(source.path)) {
            LOG_ERROR("OBS image: File does not exist or is not a regular file: '{}'.", toUtf8Path(source.path));
            obs_data_release(settings);
            return false;
        }
        const auto absolutePath = std::filesystem::absolute(source.path);
        const QByteArray utf8Path = QString::fromStdWString(absolutePath.wstring()).toUtf8();
        obs_data_set_string(settings, "file", utf8Path.constData());
        obs_data_set_bool(settings, "unload", false);
        obs_data_set_bool(settings, "linear_alpha", false);
        return openConfiguredSource("image_source", settings, absolutePath, false, false, false, false);
    }

    if (source.type == ObsCatalogSourceType::RtspCamera) {
        if (source.endpoint.empty()) {
            LOG_ERROR("OBS RTSP: Refusing to open an empty endpoint.");
            obs_data_release(settings);
            return false;
        }
        obs_data_set_bool(settings, "is_local_file", false);
        obs_data_set_string(settings, "input", source.endpoint.c_str());
        obs_data_set_int(settings, "buffering_mb", 2);
        obs_data_set_int(settings, "reconnect_delay_sec", 3);
        obs_data_set_bool(settings, "hw_decode", true);
        obs_data_set_bool(settings, "clear_on_media_end", true);
        return openConfiguredSource(kSourceType, settings, std::filesystem::path(source.endpoint), false, false, true, true);
    }

    if (source.type == ObsCatalogSourceType::ColorBlank) {
        obs_data_set_int(settings, "color", 0x000000FF);
        obs_data_set_int(settings, "width", 1920);
        obs_data_set_int(settings, "height", 1080);
        return openConfiguredSource("color_source", settings, {}, false, false, false, false);
    }

    obs_data_release(settings);
    LOG_ERROR("OBS media: Unsupported source type.");
    return false;
}

bool ObsPlaybackBackend::openConfiguredSource(const char* sourceType, obs_data_t* settings, const std::filesystem::path& reference,
                                              bool startPaused, bool supportsTransport, bool supportsAudio, bool liveInput) {
    close();
    m_pauseRequested.store(startPaused && supportsTransport);
    if (!m_context.isInitialized()) {
        LOG_ERROR("OBS media: Cannot open source because ObsContext is not initialized.");
        obs_data_release(settings);
        return false;
    }
    m_supportsTransport = supportsTransport;
    m_supportsAudio = supportsAudio;
    m_liveInput = liveInput;
    LOG_INFO("OBS media: Creating '{}' source.", sourceType);
    m_source = obs_source_create(sourceType, kSourceName, settings, nullptr);
    obs_data_release(settings);
    if (!m_source) {
        LOG_ERROR("OBS media: obs_source_create('{}') failed for '{}'.", sourceType, toUtf8Path(reference));
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

    // obs_view_set_source activates ffmpeg_source synchronously. Subscribe
    // before attaching, otherwise media_started can be emitted before a
    // Preview source has a chance to apply its initial pause request.
    connectMediaSignals();
    connectAudioCapture();
    LOG_INFO("OBS media: Attaching source to view.");
    obs_view_set_source(m_view, 0, m_source);
    // The active reference keeps ffmpeg_source decoding the first PVW frame.
    // Audio monitoring remains disabled for PVW; this is a decode/render
    // lifetime reference, not an audio route.
    obs_source_inc_active(m_source);
    m_sourceActive = true;
    LOG_INFO("OBS media: Source activated for decode/render: active={}", obs_source_active(m_source));
    if (m_pauseRequested.load()) {
        // Queue the pause before ffmpeg_source receives its first automatic
        // start action. media_started repeats this request only as a guard for
        // source implementations that finish opening asynchronously.
        obs_source_media_play_pause(m_source, true);
        LOG_INFO("OBS media: Queued initial pause before source startup.");
    }
    LOG_INFO("OBS media: Source attached; enabling audio monitoring.");
    m_path = reference;
    m_audioMonitoringEnabled = m_audioOutputEnabled && m_supportsAudio && setAudioMonitoring(true);
    LOG_INFO("OBS media: Created '{}' source for '{}'; startPaused={} live={}", sourceType, toUtf8Path(m_path), startPaused,
             m_liveInput);
    return true;
}

void ObsPlaybackBackend::close() {
    if (!m_source && !m_view) return;
    LOG_INFO("OBS media: Destroying source for '{}'.", toUtf8Path(m_path));
    setAudioMonitoring(false);
    disconnectAudioCapture();
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
    m_pendingSeekMs.store(-1);
    m_pendingSeekAttempts.store(0);
    m_leftAudioPeak.store(0.0f);
    m_rightAudioPeak.store(0.0f);
    m_supportsTransport = true;
    m_supportsAudio = true;
    m_liveInput = false;
}

void ObsPlaybackBackend::play() {
    if (!m_source || !m_supportsTransport) return;

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
    if (!m_source || !m_supportsTransport) return;

    // ffmpeg_source may finish opening after this call. Keep this intent so
    // onMediaStarted can pause it again after OBS completes asynchronous open.
    m_pauseRequested.store(true);
    // Destroying the monitor stops its WASAPI client and discards PCM already queued to it.
    setAudioMonitoring(false);
    obs_source_media_play_pause(m_source, true);
    LOG_INFO("OBS media: Pause requested after audio monitor flush.");
}

void ObsPlaybackBackend::enforcePendingPause() {
    enforcePendingSeek();
    if (!m_source || !m_supportsTransport || !m_pauseRequested.load()) return;

    const ObsPlaybackState currentState = state();
    if (currentState == ObsPlaybackState::Paused || currentState == ObsPlaybackState::Stopped || currentState == ObsPlaybackState::Ended) {
        return;
    }

    // A newly activated ffmpeg_source can remain Opening/Buffering before it
    // reaches Playing. Keep queueing pause through those transient states until
    // libobs reports a stable Paused state instead of only reacting to Playing.
    setAudioMonitoring(false);
    obs_source_media_play_pause(m_source, true);
    LOG_INFO("OBS media: Reasserted pending pause while state={}.", static_cast<int>(currentState));
}

void ObsPlaybackBackend::stop() {
    if (!m_source || !m_supportsTransport) return;

    m_pauseRequested.store(true);
    setAudioMonitoring(false);
    obs_source_media_stop(m_source);
    m_mediaEnded.store(false);
    LOG_INFO("OBS media: Stop requested after audio monitor flush.");
}

bool ObsPlaybackBackend::seekMs(int64_t milliseconds) {
    if (!m_source || !m_supportsTransport) return false;
    const int64_t duration = durationMs();
    const int64_t target = std::clamp(milliseconds, int64_t{0}, duration > 0 ? duration : milliseconds);
    m_pendingSeekMs.store(target);
    m_pendingSeekAttempts.store(0);
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

void ObsPlaybackBackend::enforcePendingSeek() {
    if (!m_source || !m_supportsTransport) return;
    const int64_t target = m_pendingSeekMs.load();
    if (target < 0) return;

    const ObsPlaybackState currentState = state();
    if (currentState == ObsPlaybackState::Opening || currentState == ObsPlaybackState::Buffering ||
        currentState == ObsPlaybackState::None) {
        return;
    }

    const int attempt = m_pendingSeekAttempts.fetch_add(1);
    if (attempt > 0) {
        m_pendingSeekMs.store(-1);
        return;
    }

    // ffmpeg_source can ignore a seek issued while its asynchronous media open
    // is still completing. Re-issue it once after media becomes active. Do not
    // recreate the WASAPI monitor here: doing that every timer tick caused PGM
    // to flash and repeatedly interrupt audio.
    obs_source_media_set_time(m_source, target);
    m_mediaEnded.store(false);
    LOG_INFO("OBS media: Applied one deferred seek to {} ms after source initialization.", target);
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
    if (m_audioOutputEnabled == enabled) {
        if (!m_source) return;
        if (!enabled) {
            setAudioMonitoring(false);
            if (m_sourceActive) {
                obs_source_dec_active(m_source);
                m_sourceActive = false;
            }
        } else {
            if (m_supportsAudio && !m_sourceActive) {
                obs_source_inc_active(m_source);
                m_sourceActive = true;
                LOG_INFO("OBS media: Source activated for Program audio output.");
            }
            if (m_supportsAudio && state() == ObsPlaybackState::Playing) {
                setAudioMonitoring(true);
            }
        }
        return;
    }

    m_audioOutputEnabled = enabled;
    if (!m_source) return;

    if (!enabled) {
        setAudioMonitoring(false);
        if (m_sourceActive) {
            obs_source_dec_active(m_source);
            m_sourceActive = false;
        }
        LOG_INFO("OBS media: Audio output disabled for this playback instance.");
        return;
    }

    if (m_supportsAudio && !m_sourceActive) {
        obs_source_inc_active(m_source);
        m_sourceActive = true;
        LOG_INFO("OBS media: Source activated for Program audio output.");
    }
    if (m_supportsAudio && state() == ObsPlaybackState::Playing && !setAudioMonitoring(true)) {
        LOG_ERROR("OBS media: Failed to enable audio output for the playing instance.");
    } else {
        LOG_INFO("OBS media: Audio output enabled for this playback instance.");
    }
}

void ObsPlaybackBackend::setVolume(float volume) {
    if (!m_source || !m_supportsAudio) return;
    obs_source_set_volume(m_source, std::clamp(volume, 0.0f, 1.0f));
}

float ObsPlaybackBackend::volume() const {
    return m_source && m_supportsAudio ? obs_source_get_volume(m_source) : 0.0f;
}

float ObsPlaybackBackend::takeLeftAudioPeak() { return m_leftAudioPeak.exchange(0.0f); }
float ObsPlaybackBackend::takeRightAudioPeak() { return m_rightAudioPeak.exchange(0.0f); }

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
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    switch (m_renderMode.load()) {
    case ObsRenderMode::FitToScreen:
        scaleX = static_cast<float>(width) / sourceWidth;
        scaleY = static_cast<float>(height) / sourceHeight;
        break;
    case ObsRenderMode::AspectFit:
    default: {
        const float scale = std::min(static_cast<float>(width) / sourceWidth, static_cast<float>(height) / sourceHeight);
        scaleX = scale;
        scaleY = scale;
        offsetX = (static_cast<float>(width) - sourceWidth * scale) / 2.0f;
        offsetY = (static_cast<float>(height) - sourceHeight * scale) / 2.0f;
        break;
    }
    }
    gs_viewport_push(); gs_projection_push(); gs_matrix_push();
    gs_set_viewport(0, 0, static_cast<int>(width), static_cast<int>(height));
    gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
    gs_matrix_translate3f(offsetX, offsetY, 0.0f);
    gs_matrix_scale3f(scaleX, scaleY, 1.0f);
    obs_view_render(m_view);
    gs_matrix_pop(); gs_projection_pop(); gs_viewport_pop();
}

void ObsPlaybackBackend::logDiagnostics() const {
    LOG_INFO("OBS media: state={} position={} ms duration={} ms looping={} ended={} audioOutput={} monitored={} sourceActive={}", static_cast<int>(state()), positionMs(), durationMs(), m_looping, m_mediaEnded.load(), m_audioOutputEnabled, m_audioMonitoringEnabled, m_source && obs_source_active(m_source));
}

void ObsPlaybackBackend::onMediaStarted(void* data, calldata_t*) {
    auto* backend = static_cast<ObsPlaybackBackend*>(data);
    backend->m_mediaEnded.store(false);
    if (backend->m_pauseRequested.load()) {
        // libobs queues media commands behind a mutex, so this is processed
        // after the source has completed its asynchronous media_started path.
        obs_source_media_play_pause(backend->m_source, true);
        LOG_INFO("OBS media: Queued pause directly from media_started.");
    }
    LOG_INFO("OBS media: Source signalled media_started.");
}

void ObsPlaybackBackend::onMediaEnded(void* data, calldata_t*) {
    auto* backend = static_cast<ObsPlaybackBackend*>(data);
    backend->m_mediaEnded.store(true);
    LOG_INFO("OBS media: Source signalled media_ended. looping={}", backend->m_looping);
}

void ObsPlaybackBackend::onAudioCaptured(void* data, obs_source_t*, const audio_data* audioData, bool muted) {
    auto* backend = static_cast<ObsPlaybackBackend*>(data);
    if (!backend || !audioData || muted || !audioData->data[0]) return;

    const auto* left = reinterpret_cast<const float*>(audioData->data[0]);
    const auto* right = audioData->data[1] ? reinterpret_cast<const float*>(audioData->data[1]) : left;
    const uint32_t frames = std::min<uint32_t>(audioData->frames, 2048);
    float leftPeak = 0.0f;
    float rightPeak = 0.0f;
    for (uint32_t index = 0; index < frames; ++index) {
        leftPeak = std::max(leftPeak, std::abs(left[index]));
        rightPeak = std::max(rightPeak, std::abs(right[index]));
    }
    backend->m_leftAudioPeak.store(std::clamp(leftPeak, 0.0f, 1.0f));
    backend->m_rightAudioPeak.store(std::clamp(rightPeak, 0.0f, 1.0f));
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

void ObsPlaybackBackend::connectAudioCapture() {
    if (!m_source || m_audioCaptureConnected || !m_supportsAudio) return;
    obs_source_add_audio_capture_callback(m_source, onAudioCaptured, this);
    m_audioCaptureConnected = true;
}

void ObsPlaybackBackend::disconnectAudioCapture() {
    if (!m_source || !m_audioCaptureConnected) return;
    obs_source_remove_audio_capture_callback(m_source, onAudioCaptured, this);
    m_audioCaptureConnected = false;
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
    if (!m_supportsAudio) return !enabled;

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
