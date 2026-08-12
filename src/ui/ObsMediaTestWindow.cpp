#include "ObsMediaTestWindow.h"

#include "engine/obs/ObsPlaybackBackend.h"
#include "common/logger/Logger.h"

extern "C" {
#include <graphics/graphics.h>
#include <obs.h>
}

#include <QCloseEvent>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QString formatMilliseconds(int64_t value) {
    const int64_t totalSeconds = std::max<int64_t>(0, value / 1000);
    return QStringLiteral("%1:%2:%3").arg(totalSeconds / 3600, 2, 10, QChar('0')).arg((totalSeconds / 60) % 60, 2, 10, QChar('0')).arg(totalSeconds % 60, 2, 10, QChar('0'));
}
}

ObsMediaTestWindow::ObsMediaTestWindow(ObsContext& context, const std::filesystem::path& mediaPath, QWidget* parent)
    : QWidget(parent), m_backend(std::make_unique<ObsPlaybackBackend>(context)) {
    setWindowTitle(QStringLiteral("MediaSwitcher OBS Media Test"));
    resize(960, 620);
    setFocusPolicy(Qt::StrongFocus);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    m_videoSurface = new QWidget(this);
    m_videoSurface->setAttribute(Qt::WA_NativeWindow);
    m_videoSurface->setMinimumSize(640, 360);
    m_videoSurface->setStyleSheet(QStringLiteral("background: black;"));
    layout->addWidget(m_videoSurface, 1);

    auto* controls = new QHBoxLayout();
    m_playPauseButton = new QPushButton(QStringLiteral("Pause"), this);
    auto* stopButton = new QPushButton(QStringLiteral("Stop"), this);
    auto* backButton = new QPushButton(QStringLiteral("-10s"), this);
    auto* forwardButton = new QPushButton(QStringLiteral("+10s"), this);
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_statusLabel = new QLabel(this);
    controls->addWidget(m_playPauseButton);
    controls->addWidget(stopButton);
    controls->addWidget(backButton);
    controls->addWidget(forwardButton);
    controls->addWidget(m_positionSlider, 1);
    controls->addWidget(m_statusLabel);
    layout->addLayout(controls);

    connect(m_playPauseButton, &QPushButton::clicked, this, &ObsMediaTestWindow::togglePlayPause);
    connect(stopButton, &QPushButton::clicked, this, [this] { m_backend->stop(); });
    connect(backButton, &QPushButton::clicked, this, [this] { seekRelative(-10000); });
    connect(forwardButton, &QPushButton::clicked, this, [this] { seekRelative(10000); });
    connect(m_positionSlider, &QSlider::sliderPressed, this, [this] { m_sliderDragging = true; });
    connect(m_positionSlider, &QSlider::sliderReleased, this, [this] {
        m_sliderDragging = false;
        const int64_t duration = m_backend->durationMs();
        if (duration > 0) m_backend->seekMs(duration * m_positionSlider->value() / 1000);
    });

    if (!m_backend->open(mediaPath)) {
        m_statusLabel->setText(QStringLiteral("Could not create OBS media source. See log."));
        return;
    }
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ObsMediaTestWindow::updateStatus);
    m_timer->start(200);
    QTimer::singleShot(0, this, [this] {
        initializeDisplay();
        m_backend->play();
    });
}

ObsMediaTestWindow::~ObsMediaTestWindow() { destroyDisplay(); m_backend->close(); }

void ObsMediaTestWindow::closeEvent(QCloseEvent* event) {
    m_closing = true;
    if (m_timer) m_timer->stop();
    destroyDisplay();
    m_backend->close();
    event->accept();
}

void ObsMediaTestWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this] { initializeDisplay(); });
}

void ObsMediaTestWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    resizeDisplay();
}

void ObsMediaTestWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange || m_closing) return;

    QTimer::singleShot(0, this, [this] { resizeDisplay(); });
}

void ObsMediaTestWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Space: togglePlayPause(); break;
    case Qt::Key_Left: seekRelative(-10000); break;
    case Qt::Key_Right: seekRelative(10000); break;
    case Qt::Key_S: m_backend->stop(); break;
    case Qt::Key_F11: toggleFullscreen(); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void ObsMediaTestWindow::draw(void* parameter, uint32_t width, uint32_t height) { static_cast<ObsMediaTestWindow*>(parameter)->m_backend->render(width, height); }

void ObsMediaTestWindow::initializeDisplay() {
    if (m_display || !m_videoSurface || !m_backend->isOpen()) return;
    gs_init_data graphicsData{};
    graphicsData.window.hwnd = reinterpret_cast<void*>(m_videoSurface->winId());
    graphicsData.cx = static_cast<uint32_t>(m_videoSurface->width());
    graphicsData.cy = static_cast<uint32_t>(m_videoSurface->height());
    graphicsData.format = GS_BGRA;
    graphicsData.zsformat = GS_ZS_NONE;
    graphicsData.num_backbuffers = 2;
    graphicsData.adapter = 0;
    m_display = obs_display_create(&graphicsData, 0xFF000000);
    if (!m_display) {
        LOG_ERROR("OBS media: obs_display_create failed.");
        m_statusLabel->setText(QStringLiteral("OBS display creation failed. See log."));
        return;
    }
    obs_display_add_draw_callback(m_display, draw, this);
    LOG_INFO("OBS media: Test display created.");
}

void ObsMediaTestWindow::destroyDisplay() {
    if (!m_display) return;
    obs_display_remove_draw_callback(m_display, draw, this);
    obs_display_destroy(m_display);
    m_display = nullptr;
    LOG_INFO("OBS media: Test display destroyed.");
}

void ObsMediaTestWindow::resizeDisplay() {
    if (!m_display || !m_videoSurface) return;

    const int width = m_videoSurface->width();
    const int height = m_videoSurface->height();
    if (width <= 0 || height <= 0) return;

    obs_display_resize(m_display, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    LOG_INFO("OBS media: Test display resized to {}x{}.", width, height);
}

void ObsMediaTestWindow::toggleFullscreen() {
    if (isFullScreen()) {
        showNormal();
        LOG_INFO("OBS media: Test window left fullscreen.");
    } else {
        showFullScreen();
        LOG_INFO("OBS media: Test window entered fullscreen.");
    }
}

void ObsMediaTestWindow::updateStatus() {
    if (!m_backend->isOpen()) return;
    const int64_t position = m_backend->positionMs();
    const int64_t duration = m_backend->durationMs();
    if (!m_sliderDragging && duration > 0) m_positionSlider->setValue(static_cast<int>(position * 1000 / duration));
    m_statusLabel->setText(QStringLiteral("%1 / %2 | state %3").arg(formatMilliseconds(position), formatMilliseconds(duration)).arg(static_cast<int>(m_backend->state())));
    m_playPauseButton->setText(m_backend->state() == ObsPlaybackState::Playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastDiagnosticMs == 0 || m_lastDiagnosticMs + 5000 <= now) { m_backend->logDiagnostics(); m_lastDiagnosticMs = now; }
}

void ObsMediaTestWindow::seekRelative(int64_t deltaMs) { m_backend->seekMs(m_backend->positionMs() + deltaMs); }
void ObsMediaTestWindow::togglePlayPause() { if (m_backend->state() == ObsPlaybackState::Playing) m_backend->pause(); else m_backend->play(); }
