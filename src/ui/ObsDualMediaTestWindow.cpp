#include "ObsDualMediaTestWindow.h"

#include "common/logger/Logger.h"
#include "engine/obs/ObsPlaybackBackend.h"
#include "engine/obs/ObsPlaylist.h"

extern "C" {
#include <callback/signal.h>
#include <graphics/graphics.h>
#include <obs.h>
}

#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QString formatMilliseconds(int64_t value) {
    const int64_t totalSeconds = std::max<int64_t>(0, value / 1000);
    return QStringLiteral("%1:%2:%3")
        .arg(totalSeconds / 3600, 2, 10, QChar('0'))
        .arg((totalSeconds / 60) % 60, 2, 10, QChar('0'))
        .arg(totalSeconds % 60, 2, 10, QChar('0'));
}

void onFadeVideoStopped(void* data, calldata_t*) {
    static_cast<std::atomic_bool*>(data)->store(true);
    LOG_INFO("OBS dual media: FADE video-stop signal received from libobs.");
}
}

ObsDualMediaTestWindow::ObsDualMediaTestWindow(ObsContext& context, const std::filesystem::path& mediaPath, QWidget* parent)
    : QWidget(parent), m_context(context) {
    setWindowTitle(QStringLiteral("MediaSwitcher OBS Dual PVW / PGM Test"));
    resize(1280, 720);
    setFocusPolicy(Qt::StrongFocus);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    layout->addWidget(createPanel(m_preview, QStringLiteral("PREVIEW (PVW) - PAUSED"), QStringLiteral("#FF9800")), 1);
    auto* transitionWidget = new QWidget(this);
    transitionWidget->setStyleSheet(QStringLiteral(
        "QPushButton:disabled { color: #D8D8D8; background-color: #353535; border: 1px solid #6C6C6C; }"
        "QComboBox:disabled { color: #D8D8D8; background-color: #353535; border: 1px solid #6C6C6C; }"));
    auto* transitionControls = new QVBoxLayout(transitionWidget);
    transitionControls->setAlignment(Qt::AlignCenter);
    m_takeButton = new QPushButton(QStringLiteral("TAKE"), transitionWidget);
    m_quickPlayButton = new QPushButton(QStringLiteral("Quick Play"), transitionWidget);
    m_cutButton = new QPushButton(QStringLiteral("CUT"), transitionWidget);
    m_fadeButton = new QPushButton(QStringLiteral("FADE"), transitionWidget);
    m_fadeDuration = new QComboBox(transitionWidget);
    m_fadeDuration->addItem(QStringLiteral("300 ms"), 300);
    m_fadeDuration->addItem(QStringLiteral("700 ms"), 700);
    m_fadeDuration->addItem(QStringLiteral("1000 ms"), 1000);
    m_fadeDuration->addItem(QStringLiteral("1500 ms"), 1500);
    m_fadeDuration->setCurrentIndex(1);
    for (QPushButton* button : {m_takeButton, m_quickPlayButton, m_cutButton, m_fadeButton}) {
        button->setMinimumWidth(88);
        transitionControls->addWidget(button);
    }
    transitionControls->addWidget(m_fadeDuration);
    transitionControls->addSpacing(12);
    m_loadPlaylistButton = new QPushButton(QStringLiteral("Add Playlist Files"), transitionWidget);
    m_playlistPreviousButton = new QPushButton(QStringLiteral("Previous"), transitionWidget);
    m_playlistNextButton = new QPushButton(QStringLiteral("Next"), transitionWidget);
    m_playlistLoop = new QCheckBox(QStringLiteral("Loop Playlist"), transitionWidget);
    m_autoNext = new QCheckBox(QStringLiteral("Auto Next"), transitionWidget);
    m_playlistLoop->setChecked(true);
    m_autoNext->setChecked(true);
    m_playlistStatus = new QLabel(QStringLiteral("Playlist: Off"), transitionWidget);
    m_playlistStatus->setWordWrap(true);
    transitionControls->addWidget(m_loadPlaylistButton);
    transitionControls->addWidget(m_playlistPreviousButton);
    transitionControls->addWidget(m_playlistNextButton);
    transitionControls->addWidget(m_playlistLoop);
    transitionControls->addWidget(m_autoNext);
    transitionControls->addWidget(m_playlistStatus);
    layout->addWidget(transitionWidget);
    layout->addWidget(createPanel(m_program, QStringLiteral("PROGRAM (PGM) - LIVE AUDIO"), QStringLiteral("#F44336")), 1);

    connect(m_takeButton, &QPushButton::clicked, this, [this] { promotePreviewToProgram("TAKE"); });
    connect(m_quickPlayButton, &QPushButton::clicked, this, [this] { promotePreviewToProgram("Quick Play"); });
    connect(m_cutButton, &QPushButton::clicked, this, [this] { promotePreviewToProgram("CUT"); });
    connect(m_fadeButton, &QPushButton::clicked, this, [this] { fadePreviewToProgram(); });
    connect(m_loadPlaylistButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::choosePlaylist);
    connect(m_playlistPreviousButton, &QPushButton::clicked, this, [this] { navigatePlaylist(false, "Previous"); });
    connect(m_playlistNextButton, &QPushButton::clicked, this, [this] { navigatePlaylist(true, "Next"); });
    connect(m_playlistLoop, &QCheckBox::toggled, this, [this](bool enabled) { m_playlist->setLoop(enabled); updatePlaylistStatus(); });
    connect(m_autoNext, &QCheckBox::toggled, this, [this](bool enabled) { m_playlist->setAutoNext(enabled); updatePlaylistStatus(); });

    m_preview.backend = std::make_unique<ObsPlaybackBackend>(context);
    m_program.backend = std::make_unique<ObsPlaybackBackend>(context);
    m_playlist = std::make_unique<ObsPlaylist>();
    if (!openPanel(m_preview, mediaPath, false) || !openPanel(m_program, mediaPath, true)) {
        closePanels();
        return;
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this] {
        updatePanel(m_preview, QStringLiteral("PVW"));
        updatePanel(m_program, QStringLiteral("PGM"));
        finishFadeIfComplete();
        if (!m_fadeActive && !m_playlist->empty() && m_playlist->isAutoNext() && m_program.backend &&
            m_program.backend->hasEnded()) {
            navigatePlaylist(true, "Auto Next");
        }
    });
    m_timer->start(200);
    QTimer::singleShot(0, this, [this] {
        initializeDisplay(m_preview);
        initializeDisplay(m_program);
        primeIndependentStates();
    });
}

ObsDualMediaTestWindow::~ObsDualMediaTestWindow() { closePanels(); }

void ObsDualMediaTestWindow::closeEvent(QCloseEvent* event) {
    m_closing = true;
    if (m_timer) m_timer->stop();
    closePanels();
    event->accept();
}

void ObsDualMediaTestWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this] {
        initializeDisplay(m_preview);
        initializeDisplay(m_program);
    });
}

void ObsDualMediaTestWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    resizeDisplay(m_preview);
    resizeDisplay(m_program);
}

void ObsDualMediaTestWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange || m_closing) return;
    QTimer::singleShot(0, this, [this] {
        resizeDisplay(m_preview);
        resizeDisplay(m_program);
    });
}

void ObsDualMediaTestWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_F11: toggleFullscreen(); break;
    case Qt::Key_Space: togglePanelPlayback(m_preview); break;
    case Qt::Key_Return: togglePanelPlayback(m_program); break;
    case Qt::Key_T: promotePreviewToProgram("TAKE"); break;
    case Qt::Key_C: promotePreviewToProgram("CUT"); break;
    case Qt::Key_F: fadePreviewToProgram(); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void ObsDualMediaTestWindow::draw(void* parameter, uint32_t width, uint32_t height) {
    auto* panel = static_cast<Panel*>(parameter);
    if (panel && panel->backend) panel->backend->render(width, height);
}

QWidget* ObsDualMediaTestWindow::createPanel(Panel& panel, const QString& title, const QString& color) {
    auto* group = new QGroupBox(title, this);
    group->setStyleSheet(QStringLiteral("QGroupBox { color: %1; font-weight: bold; border: 2px solid %1; margin-top: 8px; padding-top: 12px; } QGroupBox::title { subcontrol-origin: margin; left: 8px; }").arg(color));
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(4, 12, 4, 4);

    panel.videoSurface = new QWidget(group);
    panel.videoSurface->setAttribute(Qt::WA_NativeWindow);
    panel.videoSurface->setMinimumSize(480, 270);
    panel.videoSurface->setStyleSheet(QStringLiteral("background: black;"));
    layout->addWidget(panel.videoSurface, 1);

    auto* controls = new QHBoxLayout();
    panel.playPauseButton = new QPushButton(QStringLiteral("Play"), group);
    panel.loopButton = new QPushButton(QStringLiteral("Loop: Off"), group);
    auto* backButton = new QPushButton(QStringLiteral("-10s"), group);
    auto* forwardButton = new QPushButton(QStringLiteral("+10s"), group);
    panel.seekSlider = new QSlider(Qt::Horizontal, group);
    panel.seekSlider->setRange(0, 1000);
    panel.statusLabel = new QLabel(group);
    controls->addWidget(panel.playPauseButton);
    controls->addWidget(panel.loopButton);
    controls->addWidget(backButton);
    controls->addWidget(forwardButton);
    controls->addWidget(panel.seekSlider, 1);
    controls->addWidget(panel.statusLabel);
    layout->addLayout(controls);

    connect(panel.playPauseButton, &QPushButton::clicked, this, [this, &panel] { togglePanelPlayback(panel); });
    connect(panel.loopButton, &QPushButton::clicked, this, [this, &panel] { togglePanelLoop(panel); });
    connect(backButton, &QPushButton::clicked, this, [this, &panel] { seekPanel(panel, -10000); });
    connect(forwardButton, &QPushButton::clicked, this, [this, &panel] { seekPanel(panel, 10000); });
    connect(panel.seekSlider, &QSlider::sliderPressed, this, [&panel] { panel.sliderDragging = true; });
    connect(panel.seekSlider, &QSlider::sliderReleased, this, [&panel] {
        panel.sliderDragging = false;
        if (!panel.backend) return;
        const int64_t duration = panel.backend->durationMs();
        if (duration > 0) panel.backend->seekMs(duration * panel.seekSlider->value() / 1000);
    });
    return group;
}

bool ObsDualMediaTestWindow::openPanel(Panel& panel, const std::filesystem::path& mediaPath, bool audioOutput) {
    panel.backend->setAudioOutputEnabled(audioOutput);
    if (panel.backend->open(mediaPath)) return true;
    panel.statusLabel->setText(QStringLiteral("Không thể tạo OBS media source."));
    return false;
}

void ObsDualMediaTestWindow::initializeDisplay(Panel& panel) {
    if (panel.display || !panel.videoSurface || !panel.backend || !panel.backend->isOpen()) return;
    gs_init_data graphicsData{};
    graphicsData.window.hwnd = reinterpret_cast<void*>(panel.videoSurface->winId());
    graphicsData.cx = static_cast<uint32_t>(panel.videoSurface->width());
    graphicsData.cy = static_cast<uint32_t>(panel.videoSurface->height());
    graphicsData.format = GS_BGRA;
    graphicsData.zsformat = GS_ZS_NONE;
    graphicsData.num_backbuffers = 2;
    panel.display = obs_display_create(&graphicsData, 0xFF000000);
    if (!panel.display) {
        LOG_ERROR("OBS dual media: obs_display_create failed.");
        panel.statusLabel->setText(QStringLiteral("Không thể tạo OBS display."));
        return;
    }
    obs_display_add_draw_callback(panel.display, draw, &panel);
    LOG_INFO("OBS dual media: Display created.");
}

void ObsDualMediaTestWindow::destroyDisplay(Panel& panel) {
    if (!panel.display) return;
    obs_display_remove_draw_callback(panel.display, draw, &panel);
    obs_display_destroy(panel.display);
    panel.display = nullptr;
}

void ObsDualMediaTestWindow::resizeDisplay(Panel& panel) {
    if (!panel.display || !panel.videoSurface) return;
    const int width = panel.videoSurface->width();
    const int height = panel.videoSurface->height();
    if (width > 0 && height > 0) obs_display_resize(panel.display, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

void ObsDualMediaTestWindow::updatePanel(Panel& panel, const QString& role) {
    if (!panel.backend || !panel.backend->isOpen()) return;
    const int64_t position = panel.backend->positionMs();
    const int64_t duration = panel.backend->durationMs();
    if (!panel.sliderDragging && duration > 0) panel.seekSlider->setValue(static_cast<int>(position * 1000 / duration));
    panel.playPauseButton->setText(panel.backend->state() == ObsPlaybackState::Playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
    panel.loopButton->setText(panel.backend->isLooping() ? QStringLiteral("Loop: On") : QStringLiteral("Loop: Off"));
    panel.statusLabel->setText(QStringLiteral("%1 %2 / %3 | state %4")
        .arg(role, formatMilliseconds(position), formatMilliseconds(duration))
        .arg(static_cast<int>(panel.backend->state())));
}

void ObsDualMediaTestWindow::togglePanelPlayback(Panel& panel) {
    if (!panel.backend) return;
    if (panel.backend->state() == ObsPlaybackState::Playing) panel.backend->pause();
    else panel.backend->play();
}

void ObsDualMediaTestWindow::togglePanelLoop(Panel& panel) {
    if (panel.backend) panel.backend->setLooping(!panel.backend->isLooping());
}

void ObsDualMediaTestWindow::seekPanel(Panel& panel, int64_t deltaMs) {
    if (panel.backend) panel.backend->seekMs(panel.backend->positionMs() + deltaMs);
}

bool ObsDualMediaTestWindow::promotePreviewToProgram(const char* operation) {
    if (m_fadeActive) {
        LOG_ERROR("OBS dual media: {} rejected while FADE is active.", operation);
        return false;
    }
    if (!m_preview.backend || !m_program.backend || !m_preview.backend->isOpen()) {
        LOG_ERROR("OBS dual media: {} rejected because preview is unavailable.", operation);
        return false;
    }

    const std::filesystem::path previewAsset = m_preview.backend->mediaPath();
    const int64_t previewPosition = m_preview.backend->positionMs();
    const bool previewLooping = m_preview.backend->isLooping();

    // Promotion copies asset and position into a fresh Program runtime. It never swaps instances.
    m_preview.backend->pause();
    m_program.backend->close();
    m_program.backend->setAudioOutputEnabled(true);
    m_program.backend->setLooping(previewLooping);
    if (!m_program.backend->open(previewAsset)) {
        LOG_ERROR("OBS dual media: {} failed to create Program runtime from Preview asset.", operation);
        m_program.statusLabel->setText(QStringLiteral("Program promotion failed."));
        return false;
    }

    m_program.backend->seekMs(previewPosition);
    m_program.backend->play();
    LOG_INFO("OBS dual media: {} promoted PVW asset to a new PGM runtime at {} ms; PVW remains paused.", operation, previewPosition);

    QTimer::singleShot(1000, this, [this, operation] {
        if (m_closing) return;
        LOG_INFO("OBS dual media: {} result PVW state={} position={} ms | PGM state={} position={} ms.",
                 operation, static_cast<int>(m_preview.backend->state()), m_preview.backend->positionMs(),
                 static_cast<int>(m_program.backend->state()), m_program.backend->positionMs());
    });
    return true;
}

bool ObsDualMediaTestWindow::fadePreviewToProgram() {
    if (m_fadeActive || !m_preview.backend || !m_program.backend || !m_preview.backend->isOpen()) {
        LOG_ERROR("OBS dual media: FADE rejected because a transition is active or Preview is unavailable.");
        return false;
    }

    const std::filesystem::path previewAsset = m_preview.backend->mediaPath();
    const int64_t previewPosition = m_preview.backend->positionMs();
    const bool previewLooping = m_preview.backend->isLooping();
    const uint32_t duration = static_cast<uint32_t>(m_fadeDuration->currentData().toUInt());

    // Fade needs outgoing PGM, incoming PGM and PVW. Evict the lowest-priority preload first.
    releasePreload();

    auto incoming = std::make_unique<ObsPlaybackBackend>(m_context);
    // Prepare incoming media silently, then transfer the single WASAPI monitor atomically at FADE start.
    incoming->setAudioOutputEnabled(false);
    incoming->setLooping(previewLooping);
    if (!incoming->open(previewAsset)) {
        LOG_ERROR("OBS dual media: FADE failed to create incoming Program runtime.");
        return false;
    }
    incoming->seekMs(previewPosition);
    incoming->play();

    obs_source_t* transition = obs_source_create("fade_transition", "MediaSwitcher Fade", nullptr, nullptr);
    if (!transition) {
        LOG_ERROR("OBS dual media: FADE could not create OBS fade_transition source.");
        incoming->close();
        return false;
    }

    // libobs owns the visual interpolation; PVW is paused and PGM is never swapped with it.
    m_preview.backend->pause();
    m_program.backend->setAudioOutputEnabled(false);
    incoming->setAudioOutputEnabled(true);
    obs_transition_set(transition, m_program.backend->nativeSource());
    incoming->setRenderSource(transition);
    m_program.fadeVideoCompleted.store(false);
    signal_handler_connect(obs_source_get_signal_handler(transition), "source_transition_video_stop", onFadeVideoStopped,
                           &m_program.fadeVideoCompleted);
    if (!obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, duration, incoming->nativeSource())) {
        LOG_ERROR("OBS dual media: FADE failed to start OBS transition.");
        signal_handler_disconnect(obs_source_get_signal_handler(transition), "source_transition_video_stop", onFadeVideoStopped,
                                  &m_program.fadeVideoCompleted);
        incoming->resetRenderSource();
        obs_transition_clear(transition);
        obs_source_release(transition);
        incoming->close();
        return false;
    }

    m_program.fadeOutgoing = std::move(m_program.backend);
    m_program.backend = std::move(incoming);
    m_program.fadeTransition = transition;
    m_fadeActive = true;
    m_takeButton->setEnabled(false);
    m_quickPlayButton->setEnabled(false);
    m_cutButton->setEnabled(false);
    m_fadeButton->setEnabled(false);
    m_fadeDuration->setEnabled(false);
    LOG_INFO("OBS dual media: FADE started for {} ms. PVW paused at {} ms; incoming PGM started at the same position.", duration, previewPosition);
    return true;
}

void ObsDualMediaTestWindow::finishFadeIfComplete() {
    if (!m_fadeActive || !m_program.fadeTransition) return;

    const bool videoStopSignalReceived = m_program.fadeVideoCompleted.load();
    const float transitionTime = obs_transition_get_time(m_program.fadeTransition);
    if (!videoStopSignalReceived && transitionTime < 1.0f) return;

    if (!videoStopSignalReceived) {
        LOG_WARN("OBS dual media: FADE reached libobs transition time {} without video-stop signal; finalizing PGM UI.",
                 transitionTime);
    }

    m_program.backend->resetRenderSource();
    releaseFadeTransition();
    if (m_program.fadeOutgoing) {
        m_program.fadeOutgoing->close();
        m_program.fadeOutgoing.reset();
    }
    m_fadeActive = false;
    m_takeButton->setEnabled(true);
    m_quickPlayButton->setEnabled(true);
    m_cutButton->setEnabled(true);
    m_fadeButton->setEnabled(true);
    m_fadeDuration->setEnabled(true);
    preparePlaylistLookahead();
    LOG_INFO("OBS dual media: FADE completed. PVW remains paused; incoming PGM is live.");
}

void ObsDualMediaTestWindow::releaseFadeTransition() {
    if (!m_program.fadeTransition) return;
    signal_handler_disconnect(obs_source_get_signal_handler(m_program.fadeTransition), "source_transition_video_stop",
                              onFadeVideoStopped, &m_program.fadeVideoCompleted);
    obs_transition_clear(m_program.fadeTransition);
    obs_source_release(m_program.fadeTransition);
    m_program.fadeTransition = nullptr;
}

void ObsDualMediaTestWindow::choosePlaylist() {
    QFileDialog dialog(this, QStringLiteral("Chọn nhiều media cho OBS playlist"));
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilter(QStringLiteral("Media files (*.mp4 *.mkv *.mov *.avi *.m4v *.webm *.mp3 *.wav *.flac);;All files (*.*)"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    for (QListView* view : dialog.findChildren<QListView*>()) view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (QTreeView* view : dialog.findChildren<QTreeView*>()) view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    if (dialog.exec() != QDialog::Accepted) return;
    const QStringList selected = dialog.selectedFiles();
    if (selected.isEmpty()) return;

    std::vector<std::filesystem::path> items;
    items.reserve(static_cast<size_t>(selected.size()));
    for (const QString& path : selected) items.emplace_back(path.toStdWString());
    const bool wasEmpty = m_playlist->empty();
    m_playlist->appendItems(std::move(items));
    m_playlist->setLoop(m_playlistLoop->isChecked());
    m_playlist->setAutoNext(m_autoNext->isChecked());
    LOG_INFO("OBS playlist: now contains {} item(s) after file picker selection.", m_playlist->size());
    if (wasEmpty) activatePlaylistItem("Add Playlist Files");
    else {
        preparePlaylistLookahead();
        updatePlaylistStatus();
    }
}

bool ObsDualMediaTestWindow::activatePlaylistItem(const char* reason) {
    if (m_playlist->empty() || m_fadeActive) return false;

    const std::filesystem::path item = m_playlist->current();
    releasePreload();
    if (!m_program.backend) m_program.backend = std::make_unique<ObsPlaybackBackend>(m_context);
    m_program.backend->resetRenderSource();
    m_program.backend->close();
    m_program.backend->setAudioOutputEnabled(true);
    m_program.backend->setLooping(false);
    if (!m_program.backend->open(item)) {
        LOG_ERROR("OBS playlist: {} failed to open Program item '{}'.", reason, item.string());
        updatePlaylistStatus();
        return false;
    }
    m_program.backend->play();
    preparePlaylistLookahead();
    LOG_INFO("OBS playlist: {} activated item {}/{} '{}'.", reason, m_playlist->currentIndex() + 1,
             m_playlist->size(), item.string());
    updatePlaylistStatus();
    return true;
}

bool ObsDualMediaTestWindow::navigatePlaylist(bool forward, const char* reason) {
    if (m_fadeActive || m_playlist->empty()) return false;
    const bool changed = forward ? m_playlist->advance() : m_playlist->previous();
    if (!changed) {
        LOG_INFO("OBS playlist: {} ignored at playlist boundary with Loop disabled.", reason);
        updatePlaylistStatus();
        return false;
    }
    return activatePlaylistItem(reason);
}

void ObsDualMediaTestWindow::preparePlaylistLookahead() {
    releasePreload();
    if (m_playlist->empty() || m_fadeActive) {
        updatePlaylistStatus();
        return;
    }

    const std::filesystem::path* previewItem = m_playlist->offset(1);
    if (previewItem && m_playlist->size() > 1) {
        if (!m_preview.backend) m_preview.backend = std::make_unique<ObsPlaybackBackend>(m_context);
        m_preview.backend->resetRenderSource();
        m_preview.backend->close();
        m_preview.backend->setAudioOutputEnabled(false);
        m_preview.backend->setLooping(false);
        if (m_preview.backend->open(*previewItem)) m_preview.backend->pause();
        else LOG_ERROR("OBS playlist: failed to prepare Preview item '{}'.", previewItem->string());
    } else if (m_preview.backend) {
        m_preview.backend->resetRenderSource();
        m_preview.backend->close();
    }

    const std::filesystem::path* preloadItem = m_playlist->offset(2);
    if (preloadItem && m_playlist->size() > 2) {
        m_preload = std::make_unique<ObsPlaybackBackend>(m_context);
        m_preload->setAudioOutputEnabled(false);
        m_preload->setLooping(false);
        if (m_preload->open(*preloadItem)) {
            m_preload->pause();
            LOG_INFO("OBS playlist: preloaded item '{}'. Active playback count=3 (PGM, PVW, preload).",
                     preloadItem->string());
        } else {
            LOG_ERROR("OBS playlist: failed to preload '{}'.", preloadItem->string());
            m_preload.reset();
        }
    }
    updatePlaylistStatus();
}

void ObsDualMediaTestWindow::releasePreload() {
    if (!m_preload) return;
    m_preload->close();
    m_preload.reset();
    LOG_INFO("OBS playlist: released preload to preserve MAX_TOTAL_ACTIVE_PLAYBACKS=3.");
}

void ObsDualMediaTestWindow::updatePlaylistStatus() {
    if (!m_playlistStatus || !m_playlist || m_playlist->empty()) {
        if (m_playlistStatus) m_playlistStatus->setText(QStringLiteral("Playlist: Off"));
        return;
    }
    m_playlistStatus->setText(QStringLiteral("Playlist %1/%2\nPreload: %3")
        .arg(m_playlist->currentIndex() + 1)
        .arg(m_playlist->size())
        .arg(m_preload ? QStringLiteral("Ready") : QStringLiteral("None")));
}

void ObsDualMediaTestWindow::primeIndependentStates() {
    if (m_statesPrimed) return;
    m_statesPrimed = true;
    m_preview.backend->pause();
    m_preview.backend->seekMs(90000);
    m_program.backend->seekMs(30000);
    m_program.backend->play();
    LOG_INFO("OBS dual media: Independent states requested: PVW=90s paused, PGM=30s playing.");
    QTimer::singleShot(1000, this, [this] {
        if (m_closing) return;
        LOG_INFO("OBS dual media: PVW state={} position={} ms | PGM state={} position={} ms.",
                 static_cast<int>(m_preview.backend->state()), m_preview.backend->positionMs(),
                 static_cast<int>(m_program.backend->state()), m_program.backend->positionMs());
    });
}

void ObsDualMediaTestWindow::toggleFullscreen() {
    if (isFullScreen()) showNormal();
    else showFullScreen();
}

void ObsDualMediaTestWindow::closePanels() {
    if (m_program.backend) m_program.backend->resetRenderSource();
    releaseFadeTransition();
    releasePreload();
    for (Panel* panel : {&m_preview, &m_program}) {
        destroyDisplay(*panel);
        if (panel->backend) panel->backend->close();
        if (panel->fadeOutgoing) panel->fadeOutgoing->close();
    }
}
