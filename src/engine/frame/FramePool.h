#pragma once

#include "Frame.h"
#include <memory>
#include <vector>
#include <mutex>

class FramePool {
public:
    // maxSize: maximum number of frames to keep pooled.
    // Frames returned beyond this limit are dropped (freed immediately).
    explicit FramePool(size_t initialCapacity = 3, size_t maxSize = 3);
    ~FramePool() = default;

    // Acquire a Frame from the pool (or create a new one if pool is empty)
    std::shared_ptr<Frame> acquire(int width, int height, PixelFormat format);

    // Return a Frame to the pool for reuse
    void release(std::shared_ptr<Frame> frame);

    // Clear pooled resources
    void clear();

private:
    std::vector<std::shared_ptr<Frame>> m_pool;
    std::mutex m_mutex;
    size_t m_initialCapacity;
    size_t m_maxSize;  // Pool will not hold more than this many frames
};
