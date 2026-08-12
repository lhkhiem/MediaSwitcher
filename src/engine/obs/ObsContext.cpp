#include "ObsContext.h"

#include "common/logger/Logger.h"

#include <QCoreApplication>

extern "C" {
#include <obs.h>
}

namespace {
constexpr uint32_t OBS_WIDTH = 1920;
constexpr uint32_t OBS_HEIGHT = 1080;
constexpr uint32_t OBS_FPS = 60;
constexpr const char* OBS_PLUGIN_BIN_RELATIVE_PATH = "obs-plugins/64bit";
constexpr const char* OBS_PLUGIN_DATA_RELATIVE_PATH = "data/obs-plugins";

void collectModule(void* data, obs_module_t* module) {
    auto* modules = static_cast<std::vector<std::string>*>(data);
    const char* name = obs_get_module_name(module);
    const char* fileName = obs_get_module_file_name(module);
    modules->emplace_back(name ? name : (fileName ? fileName : "<unnamed>"));
}
}

ObsContext::~ObsContext() {
    shutdown();
}

bool ObsContext::initialize() {
    if (m_initialized) return true;

    m_runtimeRoot = (std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) / ".." / "..").lexically_normal();
    m_moduleConfigPath = m_runtimeRoot / "obs-module-config";
    std::error_code error;
    std::filesystem::create_directories(m_moduleConfigPath, error);
    if (error) {
        LOG_ERROR("OBS: Cannot create module config directory '{}': {}", m_moduleConfigPath.string(), error.message());
        return false;
    }

    const auto coreDataPath = m_runtimeRoot / "data" / "libobs";
    if (!std::filesystem::is_directory(coreDataPath)) {
        LOG_ERROR("OBS: Core data directory is missing: '{}'.", coreDataPath.string());
        return false;
    }

    LOG_INFO("OBS: Validated libobs data directory '{}'.", coreDataPath.string());
    LOG_INFO("OBS: Runtime root: '{}'.", m_runtimeRoot.string());
    LOG_INFO("OBS: Starting libobs with graphics backend libobs-d3d11.");
    if (!obs_startup("en-US", m_moduleConfigPath.string().c_str(), nullptr)) {
        LOG_ERROR("OBS: obs_startup failed.");
        return false;
    }
    m_initialized = true;
    LOG_INFO("OBS: libobs startup succeeded. Version: {}.", obs_get_version_string());

    if (!initializeVideo() || !initializeAudio() || !loadModules()) {
        shutdown();
        return false;
    }

    LOG_INFO("OBS: Foundation initialized successfully. Loaded {} module(s).", m_loadedModules.size());
    return true;
}

bool ObsContext::initializeVideo() {
    obs_video_info videoInfo{};
    videoInfo.graphics_module = "libobs-d3d11";
    videoInfo.fps_num = OBS_FPS;
    videoInfo.fps_den = 1;
    videoInfo.base_width = OBS_WIDTH;
    videoInfo.base_height = OBS_HEIGHT;
    videoInfo.output_width = OBS_WIDTH;
    videoInfo.output_height = OBS_HEIGHT;
    videoInfo.output_format = VIDEO_FORMAT_NV12;
    videoInfo.colorspace = VIDEO_CS_DEFAULT;
    videoInfo.range = VIDEO_RANGE_DEFAULT;
    videoInfo.scale_type = OBS_SCALE_BICUBIC;
    videoInfo.adapter = 0;
    videoInfo.gpu_conversion = true;

    const int result = obs_reset_video(&videoInfo);
    if (result != OBS_VIDEO_SUCCESS) {
        LOG_ERROR("OBS: obs_reset_video failed with code {}.", result);
        return false;
    }

    LOG_INFO("OBS: Video initialized: {}x{} @ {}/{} FPS using libobs-d3d11.", OBS_WIDTH, OBS_HEIGHT, OBS_FPS, 1);
    return true;
}

bool ObsContext::initializeAudio() {
    obs_audio_info audioInfo{};
    audioInfo.samples_per_sec = 48000;
    audioInfo.speakers = SPEAKERS_STEREO;

    if (!obs_reset_audio(&audioInfo)) {
        LOG_ERROR("OBS: obs_reset_audio failed.");
        return false;
    }

    LOG_INFO("OBS: Audio core initialized: 48000 Hz stereo.");
    return true;
}

bool ObsContext::loadModules() {
    const auto pluginBin = m_runtimeRoot / OBS_PLUGIN_BIN_RELATIVE_PATH;
    const auto pluginData = m_runtimeRoot / OBS_PLUGIN_DATA_RELATIVE_PATH;

    if (!std::filesystem::is_directory(pluginBin) || !std::filesystem::is_directory(pluginData / "obs-ffmpeg")) {
        LOG_ERROR("OBS: Runtime module paths are missing. bin='{}' data='{}'.", pluginBin.string(), pluginData.string());
        return false;
    }

    LOG_INFO("OBS: Discovering modules from canonical runtime paths bin='{}' data='{}'.", pluginBin.string(), pluginData.string());

    obs_module_failure_info failures{};
    obs_load_all_modules2(&failures);
    obs_post_load_modules();
    obs_enum_modules(collectModule, &m_loadedModules);

    if (failures.count > 0) {
        for (size_t index = 0; index < failures.count; ++index) {
            LOG_ERROR("OBS: Failed to load module '{}'.", failures.failed_modules[index]);
        }
        obs_module_failure_info_free(&failures);
        return false;
    }
    obs_module_failure_info_free(&failures);

    if (!obs_get_module("obs-ffmpeg")) {
        LOG_ERROR("OBS: Required media module 'obs-ffmpeg' was not loaded.");
        return false;
    }
    if (m_loadedModules.empty()) {
        LOG_ERROR("OBS: No modules were loaded from the configured runtime.");
        return false;
    }

    for (const auto& module : m_loadedModules) {
        LOG_INFO("OBS: Loaded module '{}'.", module);
    }
    return true;
}

void ObsContext::shutdown() {
    if (!m_initialized) return;

    LOG_INFO("OBS: Shutting down libobs after releasing {} loaded module reference(s).", m_loadedModules.size());
    m_loadedModules.clear();
    obs_shutdown();
    m_initialized = false;
    LOG_INFO("OBS: libobs shutdown completed.");
}