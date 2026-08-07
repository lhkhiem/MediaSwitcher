#pragma once

#include "IMediaSource.h"
#include "engine/frame/FramePool.h"
#include <atomic>

class ColorBarsSource : public IMediaSource {
public:
    ColorBarsSource(int width = 1280, int height = 720);
    ~ColorBarsSource() override;

    bool open() override;
    void close() override;

    std::shared_ptr<Frame> getFrame() override;

private:
    void generateColorBars(Frame& frame, int animationOffset);

    int m_width;
    int m_height;
    std::atomic<bool> m_opened{false};
    FramePool m_framePool;
    int m_animationFrame{0};
};
