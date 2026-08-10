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
    for (auto& slot : m_slots) {
        if (slot.source) {
            slot.source->close();
            slot.source.reset();
        }
    }
    m_slots.clear();
    m_registry.clear();
}

void InputManager::updateActivePlaybackInstances() {
    for (auto& slot : m_slots) {
        bool isActive = (slot.id == m_previewSlotId || slot.id == m_programSlotId);

        if (isActive) {
            if (slot.type != InputType::ColorBars && !slot.source) {
                slot.source = std::make_shared<FileSource>(slot.filePath);
                slot.source->open();
                slot.state = SourceState::Playing;
                LOG_INFO("InputManager: Created active PlaybackInstance for slot #{} '{}'", slot.id, slot.name);
            }
            if (slot.source) {
                bool isPgm = (slot.id == m_programSlotId);
                slot.source->setAudioActive(isPgm);
            }
        } else {
            if (slot.type != InputType::ColorBars && slot.source) {
                slot.source->close();
                slot.source.reset();
                slot.state = SourceState::Idle;
                LOG_INFO("InputManager: Evicted idle PlaybackInstance for slot #{} '{}' (0 decoders/threads)", slot.id, slot.name);
            }
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
    slot.source = nullptr; // ZERO persistent decoders/threads in Idle state

    SourceInfo info;
    info.id = slot.id;
    info.name = slot.name;
    info.filePath = slot.filePath;
    info.type = slot.type;
    info.state = SourceState::Idle;
    m_registry.addSource(info);

    // Request 320x180 thumbnail extraction asynchronously
    ThumbnailGenerator::instance().requestThumbnail(slot.id, filePath, slot.type);

    m_slots.push_back(slot);
    int newId = slot.id;

    if (m_previewSlotId == -1) {
        m_previewSlotId = newId;
    }

    updateActivePlaybackInstances();

    LOG_INFO("InputManager: Registered slot #{} '{}' (IDLE state, 0 decoders)", newId, slot.name);

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
        if (it->source) {
            it->source->close();
            it->source.reset();
        }
        m_registry.removeSource(slotId);
        m_slots.erase(it);

        if (m_previewSlotId == slotId) {
            m_previewSlotId = m_slots.empty() ? -1 : m_slots.front().id;
        }
        if (m_programSlotId == slotId) {
            m_programSlotId = m_slots.empty() ? -1 : m_slots.front().id;
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

void InputManager::swapPreviewAndProgram() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::swap(m_previewSlotId, m_programSlotId);
        updateActivePlaybackInstances();
    }
    LOG_INFO("InputManager: Swapped PVW (#{}) and PGM (#{})", m_previewSlotId, m_programSlotId);
    if (m_onPreviewChanged) m_onPreviewChanged();
    if (m_onProgramChanged) m_onProgramChanged();
}

std::shared_ptr<IMediaSource> InputManager::previewSource() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* slot = getSlot(m_previewSlotId);
    return slot ? slot->source : nullptr;
}

std::shared_ptr<IMediaSource> InputManager::programSource() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* slot = getSlot(m_programSlotId);
    return slot ? slot->source : nullptr;
}
