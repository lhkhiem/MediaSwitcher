#include "AudioMeterWidget.h"
#include <QPainter>
#include <QLinearGradient>
#include <algorithm>

AudioMeterWidget::AudioMeterWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(45, 110);
    setMaximumWidth(70);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &AudioMeterWidget::updateMeters);
    m_timer->start(33); // ~30 FPS UI refresh
}

void AudioMeterWidget::setLevels(float leftPeak, float rightPeak) {
    m_leftLevel = std::clamp(leftPeak, 0.0f, 1.0f);
    m_rightLevel = std::clamp(rightPeak, 0.0f, 1.0f);

    m_leftPeakHold = (std::max)(m_leftLevel, m_leftPeakHold * 0.95f);
    m_rightPeakHold = (std::max)(m_rightLevel, m_rightPeakHold * 0.95f);
}

void AudioMeterWidget::updateMeters() {
    // Audio is now handled by Qt QMediaPlayer internally.
    // Peak level metering not available without a custom audio sink.
    // Meter shows silence; can be enhanced later with QAudioSink probe.
    setLevels(0.0f, 0.0f);
    update();
}

void AudioMeterWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // Background container with dark glassmorphism style
    painter.fillRect(rect(), QColor(20, 22, 28));

    int barWidth = (w - 14) / 2;
    if (barWidth < 6) barWidth = 6;

    int topPadding = 18;
    int bottomPadding = 8;
    int barHeight = h - topPadding - bottomPadding;

    // Draw Headers (L, R)
    painter.setPen(QColor(160, 170, 190));
    QFont font = painter.font();
    font.setPointSize(8);
    font.setBold(true);
    painter.setFont(font);

    painter.drawText(QRect(5, 2, barWidth, 14), Qt::AlignCenter, "L");
    painter.drawText(QRect(9 + barWidth, 2, barWidth, 14), Qt::AlignCenter, "R");

    // Bar Rectangles
    QRect leftBarRect(5, topPadding, barWidth, barHeight);
    QRect rightBarRect(9 + barWidth, topPadding, barWidth, barHeight);

    // Draw Bar Backgrounds
    painter.fillRect(leftBarRect, QColor(32, 36, 44));
    painter.fillRect(rightBarRect, QColor(32, 36, 44));

    // Gradient for VU meters (Green -> Yellow -> Red)
    QLinearGradient grad(0, topPadding + barHeight, 0, topPadding);
    grad.setColorAt(0.0, QColor(46, 204, 113));   // Green at bottom
    grad.setColorAt(0.65, QColor(241, 196, 15));  // Yellow in middle
    grad.setColorAt(0.9, QColor(231, 76, 60));    // Red at top

    // Fill Left Bar Level
    int activeHeightL = static_cast<int>(barHeight * m_leftLevel);
    if (activeHeightL > 0) {
        QRect activeRectL(leftBarRect.x(), leftBarRect.bottom() - activeHeightL + 1, barWidth, activeHeightL);
        painter.fillRect(activeRectL, grad);
    }

    // Fill Right Bar Level
    int activeHeightR = static_cast<int>(barHeight * m_rightLevel);
    if (activeHeightR > 0) {
        QRect activeRectR(rightBarRect.x(), rightBarRect.bottom() - activeHeightR + 1, barWidth, activeHeightR);
        painter.fillRect(activeRectR, grad);
    }

    // Peak Hold Indicators
    if (m_leftPeakHold > 0.05f) {
        int peakYL = leftBarRect.bottom() - static_cast<int>(barHeight * m_leftPeakHold);
        painter.setPen(QColor(255, 255, 255));
        painter.drawLine(leftBarRect.x(), peakYL, leftBarRect.x() + barWidth - 1, peakYL);
    }

    if (m_rightPeakHold > 0.05f) {
        int peakYR = rightBarRect.bottom() - static_cast<int>(barHeight * m_rightPeakHold);
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
