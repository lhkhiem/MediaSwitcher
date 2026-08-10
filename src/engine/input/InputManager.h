#pragma once

#include "InputSlot.h"
#include "SourceRegistry.h"
#include "PlaybackManager.h"
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <unordered_map>
#include <QObject>

class InputManager : public QObject {
    Q_OBJECT
public:
    InputManager();
    ~InputManager() override;

    int addColorBarsSlot(const std::string& name = "Color Bars");
    int addFileSlot(const std::string& filePath, const std::string& name = "");
    bool removeSlot(int slotId);

    const std::vector<InputSlot>& inputSlots() const { return m_slots; }
    InputSlot* getSlot(int slotId);

    int previewSlotId() const { return m_previewSlotId; }
    int programSlotId() const { return m_programSlotId; }
    int preloadSlotId() const { return m_preloadSlotId; }

    void setPreviewSlot(int slotId);
    void setProgramSlot(int slotId);
    void preloadSlot(int slotId);
    void swapPreviewAndProgram();

    std::shared_ptr<IMediaSource> previewSource();
    std::shared_ptr<IMediaSource> programSource();

    size_t activeDecoderCount() const { return m_playbackManager.activeDecoderCount(); }

    using InputChangeCallback = std::function<void()>;
    void setOnInputListChanged(InputChangeCallback cb) { m_onInputListChanged = cb; }
    void setOnPreviewChanged(InputChangeCallback cb) { m_onPreviewChanged = cb; }
    void setOnProgramChanged(InputChangeCallback cb) { m_onProgramChanged = cb; }

private:
    void updateActivePlaybackInstances();

    std::mutex m_mutex;
    std::vector<InputSlot> m_slots;
    SourceRegistry m_registry;
    PlaybackManager m_playbackManager;
    int m_nextId{1};

    int m_previewSlotId{-1};
    int m_programSlotId{-1};
    int m_preloadSlotId{-1};

    InputChangeCallback m_onInputListChanged;
    InputChangeCallback m_onPreviewChanged;
    InputChangeCallback m_onProgramChanged;
};
