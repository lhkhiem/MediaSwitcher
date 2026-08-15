#include "AudioMeterWidget.h"
#include "engine/audio/AudioEngine.h"
#include <QPainter>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kFloorDb = -60.0f;
constexpr float kReleaseSeconds = 0.22f;
constexpr float kPeakReleaseSeconds = 0.55f;
constexpr float kPeakHoldMilliseconds = 650.0f;

float amplitudeToMeter(float amplitude) {
    if (amplitude <= 0.001f) return 0.0f;
    const float decibels = 20.0f * std::log10(amplitude);
    return std::clamp((decibels - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
}
}

AudioMeterWidget::AudioMeterWidget(QWidget* parent, bool autoReadAudioEngine)
    : QWidget(parent)
{
    setMinimumSize(45, 110);
    setMaximumWidth(70);
    m_elapsed.start();

    if (autoReadAudioEngine) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &AudioMeterWidget::updateMeters);
        m_timer->start(16); // ~60 FPS cho meter phản hồi sát âm thanh.
    }
}

void AudioMeterWidget::setLevels(float leftPeak, float rightPeak) {
    const qint64 nowNs = m_elapsed.nsecsElapsed();
    const float elapsedSeconds = m_lastUpdateNs > 0
        ? std::clamp(static_cast<float>(nowNs - m_lastUpdateNs) / 1'000'000'000.0f, 0.001f, 0.1f)
        : 0.016f;
    m_lastUpdateNs = nowNs;

    const float left = std::clamp(leftPeak, 0.0f, 1.0f);
    const float right = std::clamp(rightPeak, 0.0f, 1.0f);
    const float release = std::exp(-elapsedSeconds / kReleaseSeconds);
    m_leftLevel = left >= m_leftLevel ? left : m_leftLevel * release;
    m_rightLevel = right >= m_rightLevel ? right : m_rightLevel * release;

    const auto updatePeakHold = [elapsedSeconds](float level, float& peak, float& holdMs) {
        if (level >= peak) {
            peak = level;
            holdMs = kPeakHoldMilliseconds;
            return;
        }
        holdMs -= elapsedSeconds * 1000.0f;
        if (holdMs <= 0.0f) peak *= std::exp(-elapsedSeconds / kPeakReleaseSeconds);
    };
    updatePeakHold(left, m_leftPeakHold, m_leftPeakHoldMs);
    updatePeakHold(right, m_rightPeakHold, m_rightPeakHoldMs);

    update();
}

void AudioMeterWidget::setCompactMode(bool compact) {
    m_compact = compact;
    if (compact) {
        setMinimumSize(16, 60);
        setMaximumWidth(16);
    } else {
        setMinimumSize(45, 110);
        setMaximumWidth(70);
    }
    updateGeometry();
    update();
}

void AudioMeterWidget::reset() {
    if (m_leftLevel == 0.0f && m_rightLevel == 0.0f &&
        m_leftPeakHold == 0.0f && m_rightPeakHold == 0.0f) return;
    m_leftLevel = 0.0f;
    m_rightLevel = 0.0f;
    m_leftPeakHold = 0.0f;
    m_rightPeakHold = 0.0f;
    m_leftPeakHoldMs = 0.0f;
    m_rightPeakHoldMs = 0.0f;
    update();
}

void AudioMeterWidget::updateMeters() {
    // Read real-time peak levels from AudioEngine (XAudio2 output)
    setLevels(AudioEngine::instance().getLeftPeak(), AudioEngine::instance().getRightPeak());
}

void AudioMeterWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    if (!m_compact) painter.fillRect(rect(), QColor(20, 22, 28));

    const int sidePadding = m_compact ? 2 : 5;
    const int channelGap = m_compact ? 2 : 4;
    const int barWidth = (std::max)(5, (w - sidePadding * 2 - channelGap) / 2);

    const int topPadding = m_compact ? 2 : 18;
    const int bottomPadding = m_compact ? 2 : 8;
    const int barHeight = (std::max)(1, h - topPadding - bottomPadding);

    // Draw Headers (L, R)
    if (!m_compact) {
        painter.setPen(QColor(160, 170, 190));
        QFont font = painter.font();
        font.setPointSize(8);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRect(sidePadding, 2, barWidth, 14), Qt::AlignCenter, QStringLiteral("L"));
        painter.drawText(QRect(sidePadding + barWidth + channelGap, 2, barWidth, 14), Qt::AlignCenter, QStringLiteral("R"));
    }

    // Bar Rectangles
    const QRect leftBarRect(sidePadding, topPadding, barWidth, barHeight);
    const QRect rightBarRect(sidePadding + barWidth + channelGap, topPadding, barWidth, barHeight);

    // Draw Bar Backgrounds
    painter.fillRect(leftBarRect, QColor(32, 36, 44));
    painter.fillRect(rightBarRect, QColor(32, 36, 44));

    // Gradient for VU meters (Green -> Yellow -> Red)
    QLinearGradient grad(0, topPadding + barHeight, 0, topPadding);
    grad.setColorAt(0.0, QColor(46, 204, 113));   // Green at bottom
    grad.setColorAt(0.65, QColor(241, 196, 15));  // Yellow in middle
    grad.setColorAt(0.9, QColor(231, 76, 60));    // Red at top

    // Fill Left Bar Level
    const int activeHeightL = static_cast<int>(barHeight * amplitudeToMeter(m_leftLevel));
    if (activeHeightL > 0) {
        QRect activeRectL(leftBarRect.x(), leftBarRect.bottom() - activeHeightL + 1, barWidth, activeHeightL);
        painter.fillRect(activeRectL, grad);
    }

    // Fill Right Bar Level
    const int activeHeightR = static_cast<int>(barHeight * amplitudeToMeter(m_rightLevel));
    if (activeHeightR > 0) {
        QRect activeRectR(rightBarRect.x(), rightBarRect.bottom() - activeHeightR + 1, barWidth, activeHeightR);
        painter.fillRect(activeRectR, grad);
    }

    // Peak Hold Indicators
    if (m_leftPeakHold > 0.05f) {
        const int peakYL = leftBarRect.bottom() - static_cast<int>(barHeight * amplitudeToMeter(m_leftPeakHold));
        painter.setPen(QColor(255, 255, 255));
        painter.drawLine(leftBarRect.x(), peakYL, leftBarRect.x() + barWidth - 1, peakYL);
    }

    if (m_rightPeakHold > 0.05f) {
        const int peakYR = rightBarRect.bottom() - static_cast<int>(barHeight * amplitudeToMeter(m_rightPeakHold));
        painter.setPen(QColor(255, 255, 255));
        painter.drawLine(rightBarRect.x(), peakYR, rightBarRect.x() + barWidth - 1, peakYR);
    }

    // Segment Grid Overlay
    painter.setPen(QColor(20, 22, 28, 180));
    for (int y = topPadding; y < topPadding + barHeight; y += 4) {
        painter.drawLine(leftBarRect.x(), y, leftBarRect.x() + barWidth, y);
        painter.drawLine(rightBarRect.x(), y, rightBarRect.x() + barWidth, y);
    }

    // Outer Border
    painter.setPen(QColor(50, 56, 68));
    painter.drawRect(leftBarRect);
    painter.drawRect(rightBarRect);
}
