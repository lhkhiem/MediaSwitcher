#include "ObsLiveThumbnailWidget.h"

#include "engine/obs/ObsPlaybackBackend.h"

extern "C" {
#include <graphics/graphics.h>
#include <obs.h>
}

#include <QResizeEvent>
#include <QShowEvent>

ObsLiveThumbnailWidget::ObsLiveThumbnailWidget(ObsPlaybackBackend* backend, QWidget* parent)
    : QWidget(parent), m_backend(backend) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setStyleSheet(QStringLiteral("background: black;"));
}

ObsLiveThumbnailWidget::~ObsLiveThumbnailWidget() { destroyDisplay(); }

void ObsLiveThumbnailWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    initializeDisplay();
}

void ObsLiveThumbnailWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_display) obs_display_resize(m_display, static_cast<uint32_t>(width()), static_cast<uint32_t>(height()));
}

void ObsLiveThumbnailWidget::draw(void* parameter, uint32_t width, uint32_t height) {
    auto* widget = static_cast<ObsLiveThumbnailWidget*>(parameter);
    if (widget && widget->m_backend) widget->m_backend->render(width, height);
}

void ObsLiveThumbnailWidget::initializeDisplay() {
    if (m_display || !m_backend || !m_backend->isOpen() || width() <= 0 || height() <= 0) return;
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

void ObsLiveThumbnailWidget::destroyDisplay() {
    if (!m_display) return;
    obs_display_remove_draw_callback(m_display, draw, this);
    obs_display_destroy(m_display);
    m_display = nullptr;
}
