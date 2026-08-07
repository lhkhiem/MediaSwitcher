#pragma once

#include <cstdint>
#include <vector>

// Immutable Frame structure
class Frame {
public:
    Frame(int width, int height, int pixelFormat, int64_t timestamp)
        : m_width(width)
        , m_height(height)
        , m_pixelFormat(pixelFormat)
        , m_timestamp(timestamp)
    {
    }

    // No copy, no move semantics for now to keep it simple and safe
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    int width() const { return m_width; }
    int height() const { return m_height; }
    int pixelFormat() const { return m_pixelFormat; }
    int64_t timestamp() const { return m_timestamp; }

    // Placeholder for actual frame data (e.g., YUV/RGB buffers)
    // For Milestone 2, we just need the structural representation.

private:
    int m_width;
    int m_height;
    int m_pixelFormat;
    int64_t m_timestamp;
};
