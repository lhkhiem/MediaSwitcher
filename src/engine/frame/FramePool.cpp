#include "FramePool.h"

FramePool::FramePool(size_t initialCapacity)
    : m_initialCapacity(initialCapacity)
{
}

std::shared_ptr<Frame> FramePool::acquire(int width, int height, PixelFormat format) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_pool.begin(); it != m_pool.end(); ++it) {
        auto frame = *it;
        if (frame->width() == width && frame->height() == height && frame->pixelFormat() == format) {
            m_pool.erase(it);
            return frame;
        }
    }

    // If no matching frame in pool, create a new one
    return std::make_shared<Frame>(width, height, format);
}

void FramePool::release(std::shared_ptr<Frame> frame) {
    if (!frame) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.push_back(frame);
}

void FramePool::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.clear();
}
