#pragma once

#include <cstdint>
#include <vector>

enum class PixelFormat {
    RGBA32 = 0,
    BGRA32 = 1,
    NV12   = 2
};

// Frame structure encapsulating pixel data and timestamp (PTS)
class Frame {
public:
    Frame(int width, int height, PixelFormat format, double ptsInSeconds = 0.0)
        : m_width(width)
        , m_height(height)
        , m_pixelFormat(format)
        , m_pts(ptsInSeconds)
        , m_stride(width * 4)
    {
        m_data.resize(m_stride * height, 0);
    }

    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    int width() const { return m_width; }
    int height() const { return m_height; }
    PixelFormat pixelFormat() const { return m_pixelFormat; }
    double pts() const { return m_pts; }
    void setPts(double ptsInSeconds) { m_pts = ptsInSeconds; }
    int stride() const { return m_stride; }

    uint8_t* data() { return m_data.data(); }
    const uint8_t* data() const { return m_data.data(); }
    size_t dataSize() const { return m_data.size(); }

    void resize(int width, int height, PixelFormat format) {
        m_width = width;
        m_height = height;
        m_pixelFormat = format;
        m_stride = width * 4;
        m_data.resize(m_stride * height);
    }

private:
    int m_width;
    int m_height;
    PixelFormat m_pixelFormat;
    double m_pts{0.0};
    int m_stride;
    std::vector<uint8_t> m_data;
};
