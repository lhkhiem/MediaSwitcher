#include "ColorBarsSource.h"
#include "common/logger/Logger.h"
#include <chrono>

ColorBarsSource::ColorBarsSource(int width, int height)
    : m_width(width)
    , m_height(height)
    , m_framePool(5)
{
}

ColorBarsSource::~ColorBarsSource() {
    close();
}

bool ColorBarsSource::open() {
    m_opened = true;
    LOG_INFO("ColorBarsSource opened ({}x{}).", m_width, m_height);
    return true;
}

void ColorBarsSource::close() {
    m_opened = false;
    m_framePool.clear();
    LOG_INFO("ColorBarsSource closed.");
}

std::shared_ptr<Frame> ColorBarsSource::getFrame() {
    if (!m_opened) return nullptr;

    auto frame = m_framePool.acquire(m_width, m_height, PixelFormat::RGBA32);
    m_animationFrame++;
    generateColorBars(*frame, m_animationFrame);
    return frame;
}

void ColorBarsSource::generateColorBars(Frame& frame, int animationOffset) {
    uint8_t* ptr = frame.data();
    int width = frame.width();
    int height = frame.height();

    // Standard 8 color bars (RGBA)
    struct Color { uint8_t r, g, b, a; };
    const Color colors[8] = {
        {255, 255, 255, 255}, // White
        {255, 255,   0, 255}, // Yellow
        {  0, 255, 255, 255}, // Cyan
        {  0, 255,   0, 255}, // Green
        {255,   0, 255, 255}, // Magenta
        {255,   0,   0, 255}, // Red
        {  0,   0, 255, 255}, // Blue
        {  0,   0,   0, 255}  // Black
    };

    int barWidth = width / 8;
    int sweepBarPos = (animationOffset * 4) % width;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int barIndex = std::min(x / barWidth, 7);
            Color c = colors[barIndex];

            // Animated sweep indicator on bottom 15% of the frame
            if (y > height * 0.85 && std::abs(x - sweepBarPos) < 15) {
                c = {0, 220, 255, 255}; // Bright Cyan indicator
            }

            int index = (y * width + x) * 4;
            ptr[index + 0] = c.r;
            ptr[index + 1] = c.g;
            ptr[index + 2] = c.b;
            ptr[index + 3] = c.a;
        }
    }
}
