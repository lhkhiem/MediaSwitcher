#pragma once

#include <QWidget>
#include <QTimer>
#include <QPaintEvent>
#include <QElapsedTimer>

class AudioMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit AudioMeterWidget(QWidget* parent = nullptr, bool autoReadAudioEngine = true);
    ~AudioMeterWidget() override = default;

    void setLevels(float leftPeak, float rightPeak);
    void setCompactMode(bool compact);
    void reset();

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void updateMeters();

private:
    float m_leftLevel{0.0f};
    float m_rightLevel{0.0f};
    float m_leftPeakHold{0.0f};
    float m_rightPeakHold{0.0f};
    float m_leftPeakHoldMs{0.0f};
    float m_rightPeakHoldMs{0.0f};
    bool m_compact{false};
    QElapsedTimer m_elapsed;
    qint64 m_lastUpdateNs{0};

    QTimer* m_timer{nullptr};
};
