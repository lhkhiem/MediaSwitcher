#include "ObsProgramOutputWindow.h"

#include "engine/obs/ObsPlaybackBackend.h"

extern "C" {
#include <graphics/graphics.h>
#include <obs.h>
}

#include <QHideEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>

ObsProgramOutputWindow::ObsProgramOutputWindow(std::function<ObsPlaybackBackend*()> backendProvider, QWidget* parent)
    : QWidget(parent), m_backendProvider(std::move(backendProvider)) {
    setAttribute(Qt::WA_NativeWindow);
    setStyleSheet(QStringLiteral("background: black;"));
    setWindowTitle(QStringLiteral("MediaSwitcher Program Output"));
}

ObsProgramOutputWindow::~ObsProgramOutputWindow() { destroyDisplay(); }

void ObsProgramOutputWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, &ObsProgramOutputWindow::initializeDisplay);
}

void ObsProgramOutputWindow::hideEvent(QHideEvent* event) {
    destroyDisplay();
    QWidget::hideEvent(event);
}

void ObsProgramOutputWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_display && width() > 0 && height() > 0) {
        obs_display_resize(m_display, static_cast<uint32_t>(width()), static_cast<uint32_t>(height()));
    }
}

void ObsProgramOutputWindow::draw(void* parameter, uint32_t width, uint32_t height) {
    auto* output = static_cast<ObsProgramOutputWindow*>(parameter);
    if (!output || !output->m_backendProvider) return;
    if (ObsPlaybackBackend* backend = output->m_backendProvider(); backend && backend->isOpen()) {
        backend->render(width, height);
    }
}

void ObsProgramOutputWindow::initializeDisplay() {
    if (m_display || width() <= 0 || height() <= 0) return;
    gs_init_data graphicsData{};
    graphicsData.window.hwnd = reinterpret_cast<void*>(winId());
    graphicsData.cx = static_cast<uint32_t>(width());
    graphicsData.cy = static_cast<uint32_t>(height());
    graphicsData.format = GS_BGRA;
    graphicsData.zsformat = GS_ZS_NONE;
    graphicsData.num_backbuffers = 2;
    m_display = obs_display_create(&graphicsData, 0xFF000000);
    if (m_display) obs_display_add_draw_callback(m_display, draw, this);
}

void ObsProgramOutputWindow::destroyDisplay() {
    if (!m_display) return;
    obs_display_remove_draw_callback(m_display, draw, this);
    obs_display_destroy(m_display);
    m_display = nullptr;
}
