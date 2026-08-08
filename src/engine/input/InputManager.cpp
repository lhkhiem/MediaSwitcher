#include "InputManager.h"
#include "ColorBarsSource.h"
#include "FileSource.h"
#include "engine/decoder/FFmpegDecoder.h"
#include "common/logger/Logger.h"
#include <filesystem>

InputManager::InputManager() {
    // Start empty without default ColorBars test pattern
}

InputManager::~InputManager() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& slot : m_slots) {
        if (slot.source) {
            slot.source->close();
        }
    }
    m_slots.clear();
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
        slot.thumbnail = img.copy();
    }

    m_slots.push_back(slot);
    int newId = slot.id;

    m_previewSlotId = newId;

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

    // 1. Extract static poster thumbnail synchronously without interfering with FileSource queue
    {
        FFmpegDecoder thumbDecoder;
        if (thumbDecoder.open(filePath)) {
            Frame f(1280, 720, PixelFormat::RGBA32);
            for (int i = 0; i < 15; ++i) {
                if (thumbDecoder.decodeNextFrame(f) && f.data() && f.width() > 0 && f.height() > 0) {
                    QImage img(f.data(), f.width(), f.height(), QImage::Format_RGBA8888);
                    slot.thumbnail = img.copy();

                    int w = img.width();
                    int h = img.height();
                    QRgb centerPix = img.pixel(w / 2, h / 2);
                    if (qRed(centerPix) + qGreen(centerPix) + qBlue(centerPix) > 25) {
                        break; // Found clear non-black poster frame
                    }
                }
            }
            thumbDecoder.close();
        }
    }

    // 2. Instantiate live FileSource for PREVIEW & PROGRAM playback
    slot.source = std::make_shared<FileSource>(filePath);
    slot.source->open();

    m_slots.push_back(slot);
    int newId = slot.id;

    m_previewSlotId = newId;

    LOG_INFO("InputManager: Added video slot #{} '{}'", newId, slot.name);

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
        if (it->source) it->source->close();
        m_slots.erase(it);

        if (m_previewSlotId == slotId) {
            m_previewSlotId = m_slots.empty() ? -1 : m_slots.front().id;
            if (m_onPreviewChanged) m_onPreviewChanged();
        }
        if (m_programSlotId == slotId) {
            m_programSlotId = m_slots.empty() ? -1 : m_slots.front().id;
            if (m_onProgramChanged) m_onProgramChanged();
        }

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
    }
    LOG_INFO("InputManager: Preview switched to slot #{}", slotId);
    if (m_onPreviewChanged) m_onPreviewChanged();
}

void InputManager::setProgramSlot(int slotId) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_programSlotId = slotId;
    }
    LOG_INFO("InputManager: Program switched to slot #{}", slotId);
    if (m_onProgramChanged) m_onProgramChanged();
}

void InputManager::swapPreviewAndProgram() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::swap(m_previewSlotId, m_programSlotId);
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
