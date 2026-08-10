#include "InputManager.h"
#include "ColorBarsSource.h"
#include "FileSource.h"
#include "ThumbnailGenerator.h"
#include "common/logger/Logger.h"
#include <filesystem>
#include <algorithm>

InputManager::InputManager() {
    connect(&ThumbnailGenerator::instance(), &ThumbnailGenerator::thumbnailReady,
            this, [this](int sourceId, QImage thumb) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_registry.updateThumbnail(sourceId, thumb);
        auto* slot = getSlot(sourceId);
        if (slot) {
            slot->thumbnail = thumb;
            slot->thumbnailReady = true;
        }
        if (m_onInputListChanged) {
            m_onInputListChanged();
        }
    });
}

InputManager::~InputManager() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playbackManager.clear();
    for (auto& slot : m_slots) {
        if (slot.source && slot.type == InputType::ColorBars) {
            slot.source->close();
            slot.source.reset();
        }
    }
    m_slots.clear();
    m_registry.clear();
}

void InputManager::updateActivePlaybackInstances() {
    std::unordered_map<int, std::string> slotPaths;
    std::unordered_map<int, SourceType> slotTypes;

    for (const auto& slot : m_slots) {
        slotPaths[slot.id] = slot.filePath;
        slotTypes[slot.id] = slot.type;
    }

    m_playbackManager.updateState(m_programSlotId, m_previewSlotId, m_preloadSlotId, slotPaths, slotTypes);

    for (auto& slot : m_slots) {
        if (slot.type == InputType::ColorBars) continue;

        auto pgmSrc = m_playbackManager.getSourceForSlot(slot.id, PlaybackRole::Program);
        auto pvwSrc = m_playbackManager.getSourceForSlot(slot.id, PlaybackRole::Preview);
        auto preloadSrc = m_playbackManager.getSourceForSlot(slot.id, PlaybackRole::Preload);

        if (pgmSrc || pvwSrc || preloadSrc) {
            slot.source = pgmSrc ? pgmSrc : (pvwSrc ? pvwSrc : preloadSrc);
            slot.state = (slot.id == m_programSlotId) ? SourceState::Playing : SourceState::Ready;
        } else {
            slot.source = nullptr;
            slot.state = SourceState::Idle;
        }
    }
}

int InputManager::addColorBarsSlot(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    InputSlot slot;
    slot.id = m_nextId++;
    slot.name = name.empty() ? ("Color Bars " + std::to_string(slot.id)) : name;
    slot.type = InputType::ColorBars;
    slot.source = std::make_shared<ColorBarsSource>(1280, 720);
    slot.source->open();

    auto frame = slot.source->getFrame();
    if (frame && frame->data() && frame->width() > 0 && frame->height() > 0) {
        QImage img(frame->data(), frame->width(), frame->height(), QImage::Format_RGBA8888);
        slot.thumbnail = img.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        slot.thumbnailReady = true;
    }

    SourceInfo info;
    info.id = slot.id;
    info.name = slot.name;
    info.type = slot.type;
    info.thumbnail = slot.thumbnail;
    info.thumbnailReady = true;
    m_registry.addSource(info);

    m_slots.push_back(slot);
    int newId = slot.id;

    if (m_previewSlotId == -1) {
        m_previewSlotId = newId;
    }

    updateActivePlaybackInstances();

    if (m_onInputListChanged) m_onInputListChanged();
    if (m_onPreviewChanged) m_onPreviewChanged();
    return newId;
}

int InputManager::addFileSlot(const std::string& filePath, const std::string& name) {
    if (filePath.empty()) return -1;

    std::lock_guard<std::mutex> lock(m_mutex);
    InputSlot slot;
    slot.id = m_nextId++;

    std::filesystem::path p(filePath);
    if (name.empty()) {
        slot.name = p.filename().string();
    } else {
        slot.name = name;
    }

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp" || ext == ".gif" || ext == ".tiff") {
        slot.type = InputType::ImageFile;
    } else {
        slot.type = InputType::VideoFile;
    }
    slot.filePath = filePath;
    slot.state = SourceState::Idle;
    slot.source = nullptr;

    SourceInfo info;
    info.id = slot.id;
    info.name = slot.name;
    info.filePath = slot.filePath;
    info.type = slot.type;
    info.state = SourceState::Idle;
    m_registry.addSource(info);

    ThumbnailGenerator::instance().requestThumbnail(slot.id, filePath, slot.type);

    m_slots.push_back(slot);
    int newId = slot.id;

    if (m_previewSlotId == -1) {
        m_previewSlotId = newId;
    }

    updateActivePlaybackInstances();

    LOG_INFO("InputManager: Registered slot #{} '{}' (IDLE state)", newId, slot.name);

    if (m_onInputListChanged) m_onInputListChanged();
    if (m_onPreviewChanged) m_onPreviewChanged();
    return newId;
}

bool InputManager::removeSlot(int slotId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_slots.begin(), m_slots.end(), [slotId](const InputSlot& s) {
        return s.id == slotId;
    });

    if (it != m_slots.end()) {
        m_registry.removeSource(slotId);
        m_slots.erase(it);

        if (m_previewSlotId == slotId) {
            m_previewSlotId = m_slots.empty() ? -1 : m_slots.front().id;
        }
        if (m_programSlotId == slotId) {
            m_programSlotId = m_slots.empty() ? -1 : m_slots.front().id;
        }
        if (m_preloadSlotId == slotId) {
            m_preloadSlotId = -1;
        }

        updateActivePlaybackInstances();

        if (m_onPreviewChanged) m_onPreviewChanged();
        if (m_onProgramChanged) m_onProgramChanged();
        if (m_onInputListChanged) m_onInputListChanged();
        return true;
    }
    return false;
}

InputSlot* InputManager::getSlot(int slotId) {
    for (auto& slot : m_slots) {
        if (slot.id == slotId) return &slot;
    }
    return nullptr;
}

void InputManager::setPreviewSlot(int slotId) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_previewSlotId = slotId;
        updateActivePlaybackInstances();
    }
    LOG_INFO("InputManager: Preview switched to slot #{}", slotId);
    if (m_onPreviewChanged) m_onPreviewChanged();
}

void InputManager::setProgramSlot(int slotId) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_programSlotId = slotId;
        updateActivePlaybackInstances();
    }
    LOG_INFO("InputManager: Program switched to slot #{}", slotId);
    if (m_onProgramChanged) m_onProgramChanged();
}

void InputManager::preloadSlot(int slotId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (slotId <= 0 || slotId == m_previewSlotId || slotId == m_programSlotId) return;

    auto* slot = getSlot(slotId);
    if (slot && slot->type != InputType::ColorBars && !slot->filePath.empty()) {
        m_preloadSlotId = slotId;
        m_playbackManager.preloadSlot(slotId, slot->filePath, slot->type);
        auto activeSrc = m_playbackManager.getSource(PlaybackRole::Preload);
        if (activeSrc) {
            slot->state = SourceState::Preloading;
        }
    }
}

void InputManager::swapPreviewAndProgram() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::swap(m_previewSlotId, m_programSlotId);
        m_playbackManager.swapRoles();
    }
    LOG_INFO("InputManager: Swapped PVW (#{}) and PGM (#{})", m_previewSlotId, m_programSlotId);
    if (m_onPreviewChanged) m_onPreviewChanged();
    if (m_onProgramChanged) m_onProgramChanged();
}

std::shared_ptr<IMediaSource> InputManager::previewSource() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* slot = getSlot(m_previewSlotId);
    if (!slot) return nullptr;
    if (slot->type == InputType::ColorBars) return slot->source;
    return m_playbackManager.getSource(PlaybackRole::Preview);
}

std::shared_ptr<IMediaSource> InputManager::programSource() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* slot = getSlot(m_programSlotId);
    if (!slot) return nullptr;
    if (slot->type == InputType::ColorBars) return slot->source;
    return m_playbackManager.getSource(PlaybackRole::Program);
}
