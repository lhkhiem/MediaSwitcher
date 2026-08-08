#pragma once

#include "InputSlot.h"
#include <vector>
#include <mutex>
#include <memory>
#include <functional>

class InputManager {
public:
    InputManager();
    ~InputManager();

    int addColorBarsSlot(const std::string& name = "Color Bars");
    int addFileSlot(const std::string& filePath, const std::string& name = "");
    int addPlaylistSlot(const std::string& name = "Playlist");
    bool removeSlot(int slotId);

    const std::vector<InputSlot>& inputSlots() const { return m_slots; }
    InputSlot* getSlot(int slotId);

    int previewSlotId() const { return m_previewSlotId; }
    int programSlotId() const { return m_programSlotId; }

    void setPreviewSlot(int slotId);
    void setProgramSlot(int slotId);
    void swapPreviewAndProgram();

    std::shared_ptr<IMediaSource> previewSource();
    std::shared_ptr<IMediaSource> programSource();

    using InputChangeCallback = std::function<void()>;
    void setOnInputListChanged(InputChangeCallback cb) { m_onInputListChanged = cb; }
    void setOnPreviewChanged(InputChangeCallback cb) { m_onPreviewChanged = cb; }
    void setOnProgramChanged(InputChangeCallback cb) { m_onProgramChanged = cb; }

private:
    std::mutex m_mutex;
    std::vector<InputSlot> m_slots;
    int m_nextId{1};

    int m_previewSlotId{-1};
    int m_programSlotId{-1};

    InputChangeCallback m_onInputListChanged;
    InputChangeCallback m_onPreviewChanged;
    InputChangeCallback m_onProgramChanged;
};
