#include "ObsDualMediaTestWindow.h"
#include "ObsLiveThumbnailWidget.h"
#include "ObsProgramOutputWindow.h"

#include "common/logger/Logger.h"
#include "engine/input/ThumbnailGenerator.h"
#include "engine/obs/ObsPlaybackBackend.h"
#include "engine/obs/ObsPlaylist.h"
#include "engine/obs/ObsSourceCatalog.h"

extern "C" {
#include <callback/signal.h>
#include <graphics/graphics.h>
#include <obs.h>
}

#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileIconProvider>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

namespace {
QString formatMilliseconds(int64_t value) {
    const int64_t totalSeconds = std::max<int64_t>(0, value / 1000);
    return QStringLiteral("%1:%2:%3")
        .arg(totalSeconds / 3600, 2, 10, QChar('0'))
        .arg((totalSeconds / 60) % 60, 2, 10, QChar('0'))
        .arg(totalSeconds % 60, 2, 10, QChar('0'));
}

QString formatTimeline(int64_t positionMs, int64_t durationMs) {
    if (durationMs <= 0) return QStringLiteral("%1/--:--:--/--:--:--").arg(formatMilliseconds(positionMs));
    return QStringLiteral("%1/%2/-%3")
        .arg(formatMilliseconds(positionMs), formatMilliseconds(durationMs),
             formatMilliseconds(std::max<int64_t>(0, durationMs - positionMs)));
}

void onFadeVideoStopped(void* data, calldata_t*) {
    static_cast<std::atomic_bool*>(data)->store(true);
    LOG_INFO("OBS dual media: FADE video-stop signal received from libobs.");
}
}

ObsDualMediaTestWindow::ObsDualMediaTestWindow(ObsContext& context, QWidget* parent)
    : ObsDualMediaTestWindow(context, std::filesystem::path{}, parent) {}

ObsDualMediaTestWindow::ObsDualMediaTestWindow(ObsContext& context, const std::filesystem::path& mediaPath, QWidget* parent)
    : QWidget(parent), m_context(context) {
    setWindowTitle(QStringLiteral("MediaSwitcher OBS"));
    resize(1280, 720);
    setMinimumSize(760, 500);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet(QStringLiteral(
        "ObsDualMediaTestWindow { background: #161b20; color: #e8edf2; }"
        "QPushButton { background: #35424e; color: #f3f6f8; border: 1px solid #52616e; border-radius: 2px; min-height: 27px; padding: 2px 8px; }"
        "QPushButton:hover { background: #465967; }"
        "QPushButton:pressed { background: #28343d; }"
        "QPushButton:disabled { color: #b8c0c7; background: #303940; border-color: #4c565d; }"
        "QPushButton[loopActive=\"true\"] { background: #1f6b84; border-color: #53a9c6; }"
        "QComboBox { background: #27313a; color: #eff3f6; border: 1px solid #52616e; border-radius: 2px; min-height: 27px; padding-left: 7px; }"
        "QSlider::groove:horizontal { height: 4px; background: #0e1216; border: 1px solid #4d5962; }"
        "QSlider::sub-page:horizontal { background: #2389b8; }"
        "QSlider::handle:horizontal { width: 9px; margin: -5px 0; background: #d6e0e6; border: 1px solid #8b9aa5; }"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);
    auto* switcherLayout = new QHBoxLayout();
    switcherLayout->setSpacing(8);
    switcherLayout->addWidget(createPanel(m_preview, QStringLiteral("PREVIEW (PVW) - PAUSED"), QStringLiteral("#1d4552")), 1);
    auto* transitionWidget = new QWidget(this);
    transitionWidget->setFixedWidth(116);
    transitionWidget->setObjectName(QStringLiteral("transitionRail"));
    transitionWidget->setStyleSheet(QStringLiteral(
        "#transitionRail { background: #20272e; border-left: 1px solid #3c4953; border-right: 1px solid #3c4953; }"
        "QPushButton { min-height: 34px; font-weight: bold; }"));
    auto* transitionControls = new QVBoxLayout(transitionWidget);
    transitionControls->setContentsMargins(5, 12, 5, 12);
    transitionControls->setSpacing(6);
    transitionControls->setAlignment(Qt::AlignTop);
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
    m_quickPlayButton->setText(QStringLiteral("QUICK PLAY"));
    m_cutButton->setText(QStringLiteral("CUT"));
    m_fadeButton->setText(QStringLiteral("FADE"));
    m_takeButton->setText(QStringLiteral("TAKE"));
    transitionControls->addWidget(m_quickPlayButton);
    transitionControls->addWidget(m_cutButton);
    transitionControls->addWidget(m_fadeButton);
    transitionControls->addWidget(m_fadeDuration);
    transitionControls->addSpacing(14);
    transitionControls->addWidget(m_takeButton);
    m_fullscreenButton = new QPushButton(QStringLiteral("FULL SCREEN"), transitionWidget);
    m_fullscreenButton->setToolTip(QStringLiteral("Mở output PGM toàn màn hình trên màn hình thứ hai"));
    transitionControls->addWidget(m_fullscreenButton);
    auto* transitionHint = new QLabel(QStringLiteral("PVW -> PGM"), transitionWidget);
    transitionHint->setAlignment(Qt::AlignCenter);
    transitionHint->setStyleSheet(QStringLiteral("color: #9aa9b4; font-size: 10px; border: 0;"));
    transitionControls->addWidget(transitionHint);
    transitionControls->addStretch();
    switcherLayout->addWidget(transitionWidget);
    switcherLayout->addWidget(createPanel(m_program, QStringLiteral("PROGRAM (PGM) - LIVE AUDIO"), QStringLiteral("#482a32")), 1);
    rootLayout->addLayout(switcherLayout, 1);

    auto* inputBank = new QWidget(this);
    inputBank->setObjectName(QStringLiteral("inputBank"));
    inputBank->setFixedHeight(218);
    inputBank->setStyleSheet(QStringLiteral("#inputBank { background: #1d242b; border: 1px solid #3b4852; }"));
    auto* inputBankLayout = new QVBoxLayout(inputBank);
    inputBankLayout->setContentsMargins(8, 6, 8, 8);
    inputBankLayout->setSpacing(6);
    auto* inputToolbar = new QHBoxLayout();
    auto* inputTitle = new QLabel(QStringLiteral("INPUTS"), inputBank);
    inputTitle->setStyleSheet(QStringLiteral("color: #dce7ee; font-weight: bold; letter-spacing: 0px; border: 0;"));
    inputToolbar->addWidget(inputTitle);
    inputToolbar->addStretch();
    m_addSourceButton = new QPushButton(QStringLiteral("Add Input"), inputBank);
    m_removeSourceButton = new QPushButton(QStringLiteral("Remove"), inputBank);
    m_openPlaylistButton = new QPushButton(QStringLiteral("Playlist"), inputBank);
    m_catalogThumbnailSize = new QComboBox(inputBank);
    m_catalogThumbnailSize->addItem(QStringLiteral("Small"), 110);
    m_catalogThumbnailSize->addItem(QStringLiteral("Normal"), 160);
    m_catalogThumbnailSize->addItem(QStringLiteral("Large"), 220);
    m_catalogThumbnailSize->setCurrentIndex(1);
    inputToolbar->addWidget(m_addSourceButton);
    inputToolbar->addWidget(m_removeSourceButton);
    inputToolbar->addWidget(m_openPlaylistButton);
    inputToolbar->addWidget(m_catalogThumbnailSize);
    inputBankLayout->addLayout(inputToolbar);
    m_sourceCatalogList = new QListWidget(inputBank);
    m_sourceCatalogList->setViewMode(QListView::IconMode);
    m_sourceCatalogList->setFlow(QListView::LeftToRight);
    m_sourceCatalogList->setWrapping(true);
    m_sourceCatalogList->setResizeMode(QListView::Adjust);
    m_sourceCatalogList->setIconSize(QSize(160, 90));
    m_sourceCatalogList->setGridSize(QSize(174, 132));
    m_sourceCatalogList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sourceCatalogList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sourceCatalogList->setStyleSheet(QStringLiteral(
        "QListWidget { background: #15181F; border: 0; }"
        "QListWidget::item { color: #E7EDF5; border: 1px solid #343B47; background: #20242D; padding: 3px; }"
        "QListWidget::item:selected { border: 2px solid #46c4e0; background: #1d3b47; }"));
    inputBankLayout->addWidget(m_sourceCatalogList, 1);
    rootLayout->addWidget(inputBank);

    connect(m_takeButton, &QPushButton::clicked, this, [this] { promotePreviewToProgram("TAKE"); });
    connect(m_quickPlayButton, &QPushButton::clicked, this, [this] { promotePreviewToProgram("Quick Play"); });
    connect(m_cutButton, &QPushButton::clicked, this, [this] { promotePreviewToProgram("CUT"); });
    connect(m_fadeButton, &QPushButton::clicked, this, [this] { fadePreviewToProgram(); });
    connect(m_fullscreenButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::toggleProgramOutputFullscreen);
    connect(m_addSourceButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::addCatalogSource);
    connect(m_removeSourceButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::removeCatalogSource);
    connect(m_openPlaylistButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::showPlaylistManager);
    connect(m_catalogThumbnailSize, &QComboBox::currentIndexChanged, this, [this](int) {
        setCatalogThumbnailSize(m_catalogThumbnailSize->currentData().toInt());
    });
    connect(m_sourceCatalogList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const auto source = m_sourceCatalog->find(item->data(Qt::UserRole).toULongLong());
        if (!source || m_playlistMode || m_fadeActive) return;
        stagePreviewSource(*source);
    });

    m_stagedSeekTimer = new QTimer(this);
    m_stagedSeekTimer->setSingleShot(true);
    m_stagedSeekTimer->setInterval(120);
    connect(m_stagedSeekTimer, &QTimer::timeout, this, &ObsDualMediaTestWindow::requestStagedPreviewFrame);

    m_preview.backend = std::make_unique<ObsPlaybackBackend>(context);
    m_program.backend = std::make_unique<ObsPlaybackBackend>(context);
    m_playlist = std::make_unique<ObsPlaylist>();
    m_sourceCatalog = std::make_unique<ObsSourceCatalog>();
    connect(&ThumbnailGenerator::instance(), &ThumbnailGenerator::thumbnailReady, this, [this](int sourceId, const QImage& image) {
        if (!m_sourceCatalog->find(static_cast<uint64_t>(sourceId))) return;
        m_sourceThumbnails[static_cast<uint64_t>(sourceId)] = QPixmap::fromImage(image);
        refreshCatalogUi();
    });
    connect(&ThumbnailGenerator::instance(), &ThumbnailGenerator::previewFrameReady, this,
            [this](quint64 sourceId, int64_t positionMs, int64_t durationMs, const QImage& image) {
        if (sourceId == m_stagedPreviewSourceId) showStagedPreviewFrame(positionMs, durationMs, image);
    });
    m_preview.statusLabel->setText(QStringLiteral("PVW empty"));
    m_program.statusLabel->setText(QStringLiteral("PGM empty"));

    if (!mediaPath.empty()) {
        if (!openPanel(m_preview, mediaPath, false) || !openPanel(m_program, mediaPath, true)) {
            closePanels();
            return;
        }
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this] {
        updatePanel(m_preview, QStringLiteral("PVW"));
        updatePanel(m_program, QStringLiteral("PGM"));
        finishFadeIfComplete();
        if (m_playlistMode && !m_fadeActive && !m_playlist->empty() && m_playlist->isAutoNext() && m_program.backend &&
            m_program.backend->hasEnded()) {
            navigatePlaylist(true, "Auto Next");
        }
    });
    m_timer->start(200);
    QTimer::singleShot(0, this, [this] {
        initializeDisplay(m_preview);
        initializeDisplay(m_program);
        if (m_preview.backend->isOpen() && m_program.backend->isOpen()) primeIndependentStates();
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
    auto* group = new QWidget(this);
    group->setObjectName(QStringLiteral("monitorPanel"));
    group->setStyleSheet(QStringLiteral("#monitorPanel { background: #11161b; border: 0; }"));
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    auto* header = new QWidget(group);
    header->setObjectName(QStringLiteral("monitorHeader"));
    header->setFixedHeight(32);
    header->setStyleSheet(QStringLiteral("#monitorHeader { background: %1; border: 0; }").arg(color));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(7, 0, 6, 0);
    panel.sourceLabel = new QLabel(title, header);
    panel.sourceLabel->setStyleSheet(QStringLiteral("color: #f1f6f8; font-weight: bold; border: 0;"));
    panel.sourceLabel->setMinimumWidth(0);
    panel.sourceLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerLayout->addWidget(panel.sourceLabel, 1);
    panel.timeLabel = new QLabel(QStringLiteral("00:00:00/--:--:--/--:--:--"), header);
    panel.timeLabel->setStyleSheet(QStringLiteral(
        "color: #a9e9f7; border: 0; font-family: Consolas; font-size: 12px; font-weight: bold; padding: 0 4px;"));
    panel.timeLabel->setFixedWidth(260);
    panel.timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headerLayout->addWidget(panel.timeLabel);
    layout->addWidget(header);

    panel.videoContainer = new QWidget(group);
    panel.videoStack = new QStackedLayout(panel.videoContainer);
    panel.videoStack->setContentsMargins(0, 0, 0, 0);
    panel.videoSurface = new QWidget(panel.videoContainer);
    panel.videoSurface->setAttribute(Qt::WA_NativeWindow);
    panel.videoSurface->setStyleSheet(QStringLiteral("background: black;"));
    panel.stagedFrameLabel = new QLabel(panel.videoContainer);
    panel.stagedFrameLabel->setAlignment(Qt::AlignCenter);
    panel.stagedFrameLabel->setStyleSheet(QStringLiteral("background: black; color: #9AA4B2;"));
    panel.stagedFrameLabel->setText(QStringLiteral("No source selected"));
    panel.videoStack->addWidget(panel.stagedFrameLabel);
    panel.videoStack->addWidget(panel.videoSurface);
    panel.videoContainer->setMinimumSize(0, 0);
    layout->addWidget(panel.videoContainer, 1);

    auto* controls = new QHBoxLayout();
    controls->setSpacing(6);
    panel.playPauseButton = new QPushButton(group);
    panel.playPauseButton->setFixedSize(38, 28);
    panel.playPauseButton->setIcon(group->style()->standardIcon(QStyle::SP_MediaPlay));
    panel.playPauseButton->setToolTip(QStringLiteral("Phát PVW/PGM"));
    panel.loopButton = new QPushButton(group);
    panel.loopButton->setFixedSize(38, 28);
    panel.loopButton->setIcon(group->style()->standardIcon(QStyle::SP_BrowserReload));
    panel.loopButton->setToolTip(QStringLiteral("Bật/tắt lặp lại"));
    panel.loopButton->setProperty("loopActive", false);
    panel.resetButton = new QPushButton(group);
    panel.resetButton->setFixedSize(38, 28);
    panel.resetButton->setIcon(group->style()->standardIcon(QStyle::SP_MediaSkipBackward));
    panel.resetButton->setToolTip(QStringLiteral("Reset về đầu"));
    auto* backButton = new QPushButton(group);
    backButton->setFixedSize(38, 28);
    backButton->setIcon(group->style()->standardIcon(QStyle::SP_MediaSeekBackward));
    backButton->setToolTip(QStringLiteral("Lùi 10 giây"));
    auto* forwardButton = new QPushButton(group);
    forwardButton->setFixedSize(38, 28);
    forwardButton->setIcon(group->style()->standardIcon(QStyle::SP_MediaSeekForward));
    forwardButton->setToolTip(QStringLiteral("Tới 10 giây"));
    panel.seekSlider = new QSlider(Qt::Horizontal, group);
    panel.seekSlider->setRange(0, 1000);
    panel.statusLabel = new QLabel(group);
    controls->addWidget(panel.seekSlider, 1);
    controls->addWidget(backButton);
    controls->addWidget(forwardButton);
    controls->addWidget(panel.loopButton);
    controls->addWidget(panel.resetButton);
    controls->addWidget(panel.playPauseButton);
    panel.statusLabel->hide();
    layout->addLayout(controls);

    connect(panel.playPauseButton, &QPushButton::clicked, this, [this, &panel] { togglePanelPlayback(panel); });
    connect(panel.loopButton, &QPushButton::clicked, this, [this, &panel] { togglePanelLoop(panel); });
    connect(panel.resetButton, &QPushButton::clicked, this, [this, &panel] { resetPanel(panel); });
    connect(backButton, &QPushButton::clicked, this, [this, &panel] { seekPanel(panel, -10000); });
    connect(forwardButton, &QPushButton::clicked, this, [this, &panel] { seekPanel(panel, 10000); });
    connect(panel.seekSlider, &QSlider::sliderPressed, this, [&panel] { panel.sliderDragging = true; });
    connect(panel.seekSlider, &QSlider::sliderReleased, this, [this, &panel] {
        panel.sliderDragging = false;
        if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen())) {
            m_stagedPreviewPositionMs = m_stagedPreviewDurationMs * panel.seekSlider->value() / 1000;
            m_stagedSeekTimer->stop();
            requestStagedPreviewFrame();
            return;
        }
        if (!panel.backend) return;
        const int64_t duration = panel.backend->durationMs();
        if (duration > 0) panel.backend->seekMs(duration * panel.seekSlider->value() / 1000);
    });
    connect(panel.seekSlider, &QSlider::sliderMoved, this, [this, &panel](int value) {
        if (&panel != &m_preview || (panel.backend && panel.backend->isOpen()) || m_stagedPreviewDurationMs <= 0) return;
        m_stagedPreviewPositionMs = m_stagedPreviewDurationMs * value / 1000;
        m_stagedSeekTimer->start();
    });
    return group;
}

bool ObsDualMediaTestWindow::openPanel(Panel& panel, const std::filesystem::path& mediaPath, bool audioOutput) {
    panel.backend->setAudioOutputEnabled(audioOutput);
    if (panel.backend->open(mediaPath, !audioOutput)) return true;
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
    if (panel.videoStack) panel.videoStack->setCurrentWidget(panel.videoSurface);
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
    if (!panel.backend || !panel.backend->isOpen()) {
        if (&panel == &m_preview && !m_stagedPreviewPath.empty()) {
            panel.playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            panel.playPauseButton->setToolTip(QStringLiteral("Phát PVW"));
            panel.timeLabel->setText(formatTimeline(m_stagedPreviewPositionMs, m_stagedPreviewDurationMs));
            const QString sourceText = QStringLiteral("PVW  %1")
                .arg(QFileInfo(QString::fromStdWString(m_stagedPreviewPath.filename().wstring())).fileName());
            panel.sourceLabel->setText(panel.sourceLabel->fontMetrics().elidedText(
                sourceText, Qt::ElideRight, panel.sourceLabel->width()));
        }
        return;
    }
    panel.backend->enforcePendingPause();
    const int64_t position = panel.backend->positionMs();
    const int64_t duration = panel.backend->durationMs();
    if (!panel.sliderDragging && duration > 0) panel.seekSlider->setValue(static_cast<int>(position * 1000 / duration));
    const bool isPlaying = panel.backend->state() == ObsPlaybackState::Playing;
    panel.playPauseButton->setIcon(style()->standardIcon(isPlaying ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    panel.playPauseButton->setToolTip(isPlaying ? QStringLiteral("Tạm dừng") : QStringLiteral("Phát"));
    panel.loopButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    panel.loopButton->setProperty("loopActive", panel.backend->isLooping());
    panel.loopButton->style()->unpolish(panel.loopButton);
    panel.loopButton->style()->polish(panel.loopButton);
    panel.loopButton->setToolTip(panel.backend->isLooping() ? QStringLiteral("Tắt lặp lại") : QStringLiteral("Bật lặp lại"));
    panel.timeLabel->setText(formatTimeline(position, duration));
    const QString sourceText = QStringLiteral("%1  %2")
        .arg(role, QFileInfo(QString::fromStdWString(panel.backend->mediaPath().filename().wstring())).fileName());
    panel.sourceLabel->setText(panel.sourceLabel->fontMetrics().elidedText(
        sourceText, Qt::ElideRight, panel.sourceLabel->width()));
}

void ObsDualMediaTestWindow::stagePreviewSource(const ObsCatalogSource& source) {
    stagePreviewAtPosition(source.path, source.id, 0);
}

void ObsDualMediaTestWindow::stagePreviewAtPosition(const std::filesystem::path& path, uint64_t sourceId, int64_t positionMs) {
    if (m_stagedSeekTimer) m_stagedSeekTimer->stop();
    destroyDisplay(m_preview);
    m_preview.backend->close();
    m_preview.backend->setAudioOutputEnabled(false);
    m_stagedPreviewPath = path;
    m_stagedPreviewSourceId = sourceId;
    m_previewSourceId = sourceId;
    m_stagedPreviewPositionMs = std::max<int64_t>(0, positionMs);
    m_stagedPreviewDurationMs = 0;
    m_stagedPreviewLoop = false;
    m_preview.seekSlider->setValue(0);
    m_preview.playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_preview.playPauseButton->setToolTip(QStringLiteral("Phát PVW"));
    m_preview.stagedFrameLabel->setPixmap({});
    m_preview.stagedFrameLabel->setText(QStringLiteral("Loading preview frame..."));
    if (m_preview.videoStack) m_preview.videoStack->setCurrentWidget(m_preview.stagedFrameLabel);

    requestStagedPreviewFrame();
    LOG_INFO("OBS app: Source #{} staged to PVW at {} ms without creating an OBS media source.", sourceId, m_stagedPreviewPositionMs);
}

void ObsDualMediaTestWindow::requestStagedPreviewFrame() {
    if (m_stagedPreviewPath.empty()) return;
    const QString suffix = QString::fromStdWString(m_stagedPreviewPath.extension().wstring()).toLower();
    const SourceType type = (suffix == QStringLiteral(".jpg") || suffix == QStringLiteral(".jpeg") ||
                             suffix == QStringLiteral(".png") || suffix == QStringLiteral(".bmp") ||
                             suffix == QStringLiteral(".webp"))
        ? SourceType::ImageFile : SourceType::VideoFile;
    ThumbnailGenerator::instance().requestPreviewFrame(m_stagedPreviewSourceId,
        QString::fromStdWString(m_stagedPreviewPath.wstring()).toUtf8().toStdString(), type, m_stagedPreviewPositionMs);
}

void ObsDualMediaTestWindow::showStagedPreviewFrame(int64_t positionMs, int64_t durationMs, const QImage& frame) {
    if (m_stagedPreviewPath.empty() || !m_preview.stagedFrameLabel) return;
    if (positionMs != m_stagedPreviewPositionMs) return;
    if (durationMs > 0) m_stagedPreviewDurationMs = durationMs;
    if (m_stagedPreviewDurationMs > 0) {
        m_preview.seekSlider->setValue(static_cast<int>(m_stagedPreviewPositionMs * 1000 / m_stagedPreviewDurationMs));
    }
    const QPixmap pixmap = QPixmap::fromImage(frame).scaled(m_preview.stagedFrameLabel->size(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_preview.stagedFrameLabel->setPixmap(pixmap);
    m_preview.stagedFrameLabel->setText({});
    if (m_preview.videoStack) m_preview.videoStack->setCurrentWidget(m_preview.stagedFrameLabel);
    LOG_INFO("OBS app: Decoded staged PVW frame for source #{} at {} ms.", m_stagedPreviewSourceId, positionMs);
}

bool ObsDualMediaTestWindow::openStagedPreview(bool play) {
    if (m_stagedPreviewPath.empty() || !m_preview.backend) return false;
    m_preview.backend->close();
    m_preview.backend->setAudioOutputEnabled(false);
    m_preview.backend->setLooping(m_stagedPreviewLoop);
    if (!m_preview.backend->open(m_stagedPreviewPath)) {
        m_preview.statusLabel->setText(QStringLiteral("PVW could not open the staged source."));
        return false;
    }
    m_preview.backend->seekMs(m_stagedPreviewPositionMs);
    initializeDisplay(m_preview);
    resizeDisplay(m_preview);
    if (play) m_preview.backend->play();
    else m_preview.backend->pause();
    return true;
}

void ObsDualMediaTestWindow::togglePanelPlayback(Panel& panel) {
    if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen())) {
        openStagedPreview(true);
        return;
    }
    if (!panel.backend) return;
    if (panel.backend->state() == ObsPlaybackState::Playing) panel.backend->pause();
    else panel.backend->play();
}

void ObsDualMediaTestWindow::togglePanelLoop(Panel& panel) {
    if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen())) {
        m_stagedPreviewLoop = !m_stagedPreviewLoop;
        panel.loopButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        panel.loopButton->setProperty("loopActive", m_stagedPreviewLoop);
        panel.loopButton->style()->unpolish(panel.loopButton);
        panel.loopButton->style()->polish(panel.loopButton);
        panel.loopButton->setToolTip(m_stagedPreviewLoop ? QStringLiteral("Tắt lặp lại") : QStringLiteral("Bật lặp lại"));
        return;
    }
    if (panel.backend) panel.backend->setLooping(!panel.backend->isLooping());
}

void ObsDualMediaTestWindow::seekPanel(Panel& panel, int64_t deltaMs) {
    if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen()) && m_stagedPreviewDurationMs > 0) {
        m_stagedPreviewPositionMs = std::clamp(m_stagedPreviewPositionMs + deltaMs, int64_t{0}, m_stagedPreviewDurationMs);
        requestStagedPreviewFrame();
        return;
    }
    if (panel.backend) panel.backend->seekMs(panel.backend->positionMs() + deltaMs);
}

void ObsDualMediaTestWindow::resetPanel(Panel& panel) {
    if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen())) {
        if (m_stagedSeekTimer) m_stagedSeekTimer->stop();
        m_stagedPreviewPositionMs = 0;
        m_preview.seekSlider->setValue(0);
        requestStagedPreviewFrame();
        return;
    }
    if (panel.backend) panel.backend->seekMs(0);
}

bool ObsDualMediaTestWindow::promotePreviewToProgram(const char* operation) {
    if (m_fadeActive) {
        LOG_ERROR("OBS dual media: {} rejected while FADE is active.", operation);
        return false;
    }
    if (!m_program.backend || (m_stagedPreviewPath.empty() && (!m_preview.backend || !m_preview.backend->isOpen()))) {
        LOG_ERROR("OBS dual media: {} rejected because preview is unavailable.", operation);
        return false;
    }

    const bool previewIsOpen = m_preview.backend && m_preview.backend->isOpen();
    const std::filesystem::path previewAsset = previewIsOpen ? m_preview.backend->mediaPath() : m_stagedPreviewPath;
    const int64_t previewPosition = previewIsOpen ? m_preview.backend->positionMs() : m_stagedPreviewPositionMs;
    const bool previewLooping = previewIsOpen ? m_preview.backend->isLooping() : m_stagedPreviewLoop;
    const bool programIsOpen = m_program.backend->isOpen();
    const std::filesystem::path outgoingAsset = programIsOpen ? m_program.backend->mediaPath() : std::filesystem::path{};
    const int64_t outgoingPosition = programIsOpen ? m_program.backend->positionMs() : 0;
    const bool outgoingLooping = programIsOpen && m_program.backend->isLooping();
    const uint64_t outgoingSourceId = m_programSourceId;

    // Promotion creates a fresh Program runtime at PVW's exact playhead.
    if (previewIsOpen) m_preview.backend->pause();
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
    initializeDisplay(m_program);
    resizeDisplay(m_program);
    setProgramSourceId(m_stagedPreviewSourceId);
    if (programIsOpen) {
        stagePreviewAtPosition(outgoingAsset, outgoingSourceId, outgoingPosition);
        m_stagedPreviewLoop = outgoingLooping;
    }
    LOG_INFO("OBS dual media: {} moved PVW -> PGM at {} ms and PGM -> PVW at {} ms.",
             operation, previewPosition, outgoingPosition);

    QTimer::singleShot(1000, this, [this, operation] {
        if (m_closing) return;
        LOG_INFO("OBS dual media: {} result PVW state={} position={} ms | PGM state={} position={} ms.",
                 operation, static_cast<int>(m_preview.backend->state()), m_preview.backend->positionMs(),
                 static_cast<int>(m_program.backend->state()), m_program.backend->positionMs());
    });
    return true;
}

bool ObsDualMediaTestWindow::fadePreviewToProgram() {
    if (m_fadeActive || !m_program.backend || (m_stagedPreviewPath.empty() && (!m_preview.backend || !m_preview.backend->isOpen()))) {
        LOG_ERROR("OBS dual media: FADE rejected because a transition is active or Preview is unavailable.");
        return false;
    }

    const bool previewIsOpen = m_preview.backend && m_preview.backend->isOpen();
    const std::filesystem::path previewAsset = previewIsOpen ? m_preview.backend->mediaPath() : m_stagedPreviewPath;
    const int64_t previewPosition = previewIsOpen ? m_preview.backend->positionMs() : m_stagedPreviewPositionMs;
    const bool previewLooping = previewIsOpen ? m_preview.backend->isLooping() : m_stagedPreviewLoop;
    const uint32_t duration = static_cast<uint32_t>(m_fadeDuration->currentData().toUInt());

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

    // libobs owns the visual interpolation; the outgoing Program is staged back into PVW at completion.
    if (previewIsOpen) m_preview.backend->pause();
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
    m_fadeOutgoingSourceId = m_programSourceId;
    m_program.backend = std::move(incoming);
    m_program.fadeTransition = transition;
    m_fadeActive = true;
    m_takeButton->setEnabled(false);
    m_quickPlayButton->setEnabled(false);
    m_cutButton->setEnabled(false);
    m_fadeButton->setEnabled(false);
    m_fadeDuration->setEnabled(false);
    setProgramSourceId(m_stagedPreviewSourceId);
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
        const std::filesystem::path outgoingAsset = m_program.fadeOutgoing->mediaPath();
        const int64_t outgoingPosition = m_program.fadeOutgoing->positionMs();
        const bool outgoingLooping = m_program.fadeOutgoing->isLooping();
        m_program.fadeOutgoing->close();
        m_program.fadeOutgoing.reset();
        stagePreviewAtPosition(outgoingAsset, m_fadeOutgoingSourceId, outgoingPosition);
        m_stagedPreviewLoop = outgoingLooping;
    }
    m_fadeActive = false;
    m_takeButton->setEnabled(true);
    m_quickPlayButton->setEnabled(true);
    m_cutButton->setEnabled(true);
    m_fadeButton->setEnabled(true);
    m_fadeDuration->setEnabled(true);
    LOG_INFO("OBS dual media: FADE completed. Outgoing PGM is staged in PVW at its final transition playhead.");
}

void ObsDualMediaTestWindow::releaseFadeTransition() {
    if (!m_program.fadeTransition) return;
    signal_handler_disconnect(obs_source_get_signal_handler(m_program.fadeTransition), "source_transition_video_stop",
                              onFadeVideoStopped, &m_program.fadeVideoCompleted);
    obs_transition_clear(m_program.fadeTransition);
    obs_source_release(m_program.fadeTransition);
    m_program.fadeTransition = nullptr;
}

#if 0 // Replaced by source-catalog playlist mode. Kept temporarily only to preserve Phase 8 history.
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
        if (m_preview.backend->open(*previewItem, true)) m_preview.backend->pause();
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
        if (m_preload->open(*preloadItem, true)) {
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

#endif

void ObsDualMediaTestWindow::addCatalogSource() {
    QFileDialog dialog(this, QStringLiteral("Add media inputs"));
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilter(QStringLiteral(
        "Supported media (*.mp4 *.mkv *.mov *.avi *.m4v *.webm *.mp3 *.wav *.flac *.aac *.m4a *.jpg *.jpeg *.png *.bmp *.webp);;"
        "Video (*.mp4 *.mkv *.mov *.avi *.m4v *.webm);;"
        "Audio (*.mp3 *.wav *.flac *.aac *.m4a);;"
        "Images (*.jpg *.jpeg *.png *.bmp *.webp)"));
    if (dialog.exec() != QDialog::Accepted) return;

    const QStringList paths = dialog.selectedFiles();
    if (paths.isEmpty()) return;
    for (const QString& path : paths) {
        const uint64_t id = m_sourceCatalog->add(std::filesystem::path(path.toStdWString()));
        const QString suffix = QFileInfo(path).suffix().toLower();
        const SourceType type = (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg") ||
                                 suffix == QStringLiteral("png") || suffix == QStringLiteral("bmp") ||
                                 suffix == QStringLiteral("webp"))
            ? SourceType::ImageFile : SourceType::VideoFile;
        LOG_INFO("OBS source catalog: added source #{}.", id);
        ThumbnailGenerator::instance().requestThumbnail(static_cast<int>(id), path.toStdString(), type);
    }
    refreshCatalogUi();
}

void ObsDualMediaTestWindow::removeCatalogSource() {
    const auto* item = m_sourceCatalogList->currentItem();
    if (!item) return;
    const uint64_t sourceId = item->data(Qt::UserRole).toULongLong();
    for (size_t index = m_playlist->size(); index > 0; --index)
        if (m_playlist->sourceIdAt(index - 1) == sourceId) m_playlist->removeAt(index - 1);
    m_sourceCatalog->remove(sourceId);
    refreshCatalogUi();
    refreshPlaylistUi();
}

void ObsDualMediaTestWindow::addSelectedCatalogSourceToPlaylist() {
    const auto* item = m_sourceCatalogList->currentItem();
    if (!item) return;
    m_playlist->addSource(item->data(Qt::UserRole).toULongLong());
    refreshPlaylistUi();
}

void ObsDualMediaTestWindow::removeSelectedPlaylistStep() {
    const int row = m_playlistList->currentRow();
    if (row >= 0) m_playlist->removeAt(static_cast<size_t>(row));
    refreshPlaylistUi();
}

void ObsDualMediaTestWindow::movePlaylistStep(int delta) {
    const int row = m_playlistList->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= static_cast<int>(m_playlist->size())) return;
    if (m_playlist->move(static_cast<size_t>(row), static_cast<size_t>(target))) {
        refreshPlaylistUi();
        m_playlistList->setCurrentRow(target);
    }
}

bool ObsDualMediaTestWindow::startPlaylist() {
    if (m_playlist->empty() || m_fadeActive) return false;
    m_playlist->setLoop(m_playlistLoop->isChecked());
    m_playlist->setAutoNext(m_autoNext->isChecked());
    m_playlistMode = true;
    return activatePlaylistProgram("Start");
}

void ObsDualMediaTestWindow::stopPlaylist() {
    if (!m_playlistMode) return;
    m_playlistMode = false;
    refreshPlaylistUi();
    LOG_INFO("OBS playlist: stopped; PVW remains untouched.");
}

bool ObsDualMediaTestWindow::activatePlaylistProgram(const char* reason) {
    if (!m_playlistMode || m_playlist->empty() || m_fadeActive) return false;
    const auto source = m_sourceCatalog->find(m_playlist->currentSourceId());
    if (!source) { stopPlaylist(); return false; }
    m_program.backend->resetRenderSource();
    m_program.backend->close();
    m_program.backend->setAudioOutputEnabled(true);
    m_program.backend->setLooping(false);
    if (!m_program.backend->open(source->path)) return false;
    m_program.backend->play();
    initializeDisplay(m_program);
    resizeDisplay(m_program);
    setProgramSourceId(source->id);
    LOG_INFO("OBS playlist: {} activated PGM source #{}; PVW untouched.", reason, source->id);
    refreshPlaylistUi();
    return true;
}

bool ObsDualMediaTestWindow::navigatePlaylist(bool forward, const char* reason) {
    if (!m_playlistMode || m_fadeActive) return false;
    if (!(forward ? m_playlist->advance() : m_playlist->previous())) {
        if (std::string_view(reason) == "Auto Next") stopPlaylist();
        return false;
    }
    return activatePlaylistProgram(reason);
}

void ObsDualMediaTestWindow::refreshCatalogUi() {
    m_sourceCatalogList->clear();
    for (const auto& source : m_sourceCatalog->sources()) {
        const QFileInfo info(QString::fromStdWString(source.path.wstring()));
        auto* item = new QListWidgetItem(QStringLiteral("#%1  %2").arg(source.id).arg(info.fileName()), m_sourceCatalogList);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(source.id));
        item->setToolTip(info.absoluteFilePath());
        if (source.id == m_programSourceId && m_program.backend && m_program.backend->isOpen()) {
            auto* tile = new QWidget(m_sourceCatalogList);
            tile->setStyleSheet(QStringLiteral("background: #281c20; border: 2px solid #bd6576;"));
            auto* tileLayout = new QVBoxLayout(tile);
            tileLayout->setContentsMargins(3, 3, 3, 3);
            tileLayout->setSpacing(2);
            auto* liveThumbnail = new ObsLiveThumbnailWidget(m_program.backend.get(), tile);
            liveThumbnail->setFixedSize(m_catalogThumbnailWidth, m_catalogThumbnailWidth * 9 / 16);
            auto* title = new QLabel(QStringLiteral("#%1  %2  [PGM LIVE]").arg(source.id).arg(info.fileName()), tile);
            title->setStyleSheet(QStringLiteral("color: #ffdbe2; font-size: 10px; border: 0;"));
            title->setWordWrap(false);
            title->setAttribute(Qt::WA_TransparentForMouseEvents);
            tileLayout->addWidget(liveThumbnail);
            tileLayout->addWidget(title);
            item->setSizeHint(QSize(m_catalogThumbnailWidth + 14, m_catalogThumbnailWidth * 9 / 16 + 40));
            m_sourceCatalogList->setItemWidget(item, tile);
        } else {
            const auto thumbnail = m_sourceThumbnails.find(source.id);
            if (thumbnail != m_sourceThumbnails.end()) item->setIcon(QIcon(thumbnail->second));
            else item->setIcon(QFileIconProvider().icon(info));
        }
    }
}

void ObsDualMediaTestWindow::setCatalogThumbnailSize(int width) {
    if (!m_sourceCatalogList) return;
    m_catalogThumbnailWidth = width;
    const int height = width * 9 / 16;
    m_sourceCatalogList->setIconSize(QSize(width, height));
    m_sourceCatalogList->setGridSize(QSize(width + 14, height + 42));
    refreshCatalogUi();
}

void ObsDualMediaTestWindow::setProgramSourceId(uint64_t sourceId) {
    if (m_programSourceId == sourceId) return;
    m_programSourceId = sourceId;
    refreshCatalogUi();
}

void ObsDualMediaTestWindow::refreshPlaylistUi() {
    if (!m_playlistList) return;
    m_playlistList->clear();
    for (size_t index = 0; index < m_playlist->size(); ++index) {
        const auto source = m_sourceCatalog->find(m_playlist->sourceIdAt(index));
        const QString name = source ? QFileInfo(QString::fromStdWString(source->path.wstring())).fileName()
                                    : QStringLiteral("Missing source");
        auto* item = new QListWidgetItem(QStringLiteral("%1. %2").arg(index + 1).arg(name), m_playlistList);
        if (m_playlistMode && index == m_playlist->currentIndex()) item->setText(item->text() + QStringLiteral("  [PGM]"));
    }
    m_playlistStatus->setText(m_playlist->empty() ? QStringLiteral("Playlist: Empty")
        : QStringLiteral("Playlist: %1 | PGM-only %2/%3")
            .arg(m_playlistMode ? QStringLiteral("Running") : QStringLiteral("Ready"))
            .arg(m_playlist->currentIndex() + 1).arg(m_playlist->size()));
}

void ObsDualMediaTestWindow::showPlaylistManager() {
    if (!m_playlistDialog) {
        m_playlistDialog = new QDialog(this);
        m_playlistDialog->setWindowTitle(QStringLiteral("OBS Playlist Manager"));
        m_playlistDialog->resize(760, 470);
        auto* layout = new QVBoxLayout(m_playlistDialog);
        auto* columns = new QHBoxLayout();

        auto* availableColumn = new QVBoxLayout();
        availableColumn->addWidget(new QLabel(QStringLiteral("Available Inputs"), m_playlistDialog));
        auto* available = new QListWidget(m_playlistDialog);
        available->setObjectName(QStringLiteral("playlistAvailableInputs"));
        availableColumn->addWidget(available, 1);
        columns->addLayout(availableColumn, 1);

        auto* commands = new QVBoxLayout();
        auto* add = new QPushButton(QStringLiteral(">"), m_playlistDialog);
        auto* remove = new QPushButton(QStringLiteral("<"), m_playlistDialog);
        commands->addStretch();
        commands->addWidget(add);
        commands->addWidget(remove);
        commands->addStretch();
        columns->addLayout(commands);

        auto* playlistColumn = new QVBoxLayout();
        playlistColumn->addWidget(new QLabel(QStringLiteral("PGM Playlist"), m_playlistDialog));
        m_playlistList = new QListWidget(m_playlistDialog);
        playlistColumn->addWidget(m_playlistList, 1);
        auto* reorder = new QHBoxLayout();
        auto* up = new QPushButton(QStringLiteral("Up"), m_playlistDialog);
        auto* down = new QPushButton(QStringLiteral("Down"), m_playlistDialog);
        reorder->addWidget(up);
        reorder->addWidget(down);
        playlistColumn->addLayout(reorder);
        columns->addLayout(playlistColumn, 1);
        layout->addLayout(columns, 1);

        auto* footer = new QHBoxLayout();
        m_playlistLoop = new QCheckBox(QStringLiteral("Loop Playlist"), m_playlistDialog);
        m_autoNext = new QCheckBox(QStringLiteral("Auto Next"), m_playlistDialog);
        m_playlistLoop->setChecked(m_playlist->isLooping());
        m_autoNext->setChecked(m_playlist->isAutoNext());
        m_playlistStatus = new QLabel(m_playlistDialog);
        auto* start = new QPushButton(QStringLiteral("Start"), m_playlistDialog);
        auto* stop = new QPushButton(QStringLiteral("Stop"), m_playlistDialog);
        footer->addWidget(m_playlistLoop);
        footer->addWidget(m_autoNext);
        footer->addWidget(m_playlistStatus, 1);
        footer->addWidget(start);
        footer->addWidget(stop);
        layout->addLayout(footer);

        connect(add, &QPushButton::clicked, this, [this, available] {
            const auto* item = available->currentItem();
            if (!item) return;
            m_playlist->addSource(item->data(Qt::UserRole).toULongLong());
            refreshPlaylistUi();
        });
        connect(remove, &QPushButton::clicked, this, &ObsDualMediaTestWindow::removeSelectedPlaylistStep);
        connect(up, &QPushButton::clicked, this, [this] { movePlaylistStep(-1); });
        connect(down, &QPushButton::clicked, this, [this] { movePlaylistStep(1); });
        connect(start, &QPushButton::clicked, this, &ObsDualMediaTestWindow::startPlaylist);
        connect(stop, &QPushButton::clicked, this, &ObsDualMediaTestWindow::stopPlaylist);
        connect(m_playlistLoop, &QCheckBox::toggled, this, [this](bool enabled) { m_playlist->setLoop(enabled); refreshPlaylistUi(); });
        connect(m_autoNext, &QCheckBox::toggled, this, [this](bool enabled) { m_playlist->setAutoNext(enabled); refreshPlaylistUi(); });
    }

    auto* available = m_playlistDialog->findChild<QListWidget*>(QStringLiteral("playlistAvailableInputs"));
    available->clear();
    for (const auto& source : m_sourceCatalog->sources()) {
        const QFileInfo info(QString::fromStdWString(source.path.wstring()));
        auto* item = new QListWidgetItem(QStringLiteral("#%1  %2").arg(source.id).arg(info.fileName()), available);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(source.id));
    }
    refreshPlaylistUi();
    m_playlistDialog->show();
    m_playlistDialog->raise();
    m_playlistDialog->activateWindow();
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

void ObsDualMediaTestWindow::toggleProgramOutputFullscreen() {
    if (m_programOutput && m_programOutput->isVisible()) {
        m_programOutput->hide();
        LOG_INFO("OBS program output: fullscreen output closed.");
        return;
    }

    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.size() < 2) {
        QMessageBox::information(this, QStringLiteral("FULL SCREEN"),
                                 QStringLiteral("Cần kết nối màn hình thứ hai để mở output PGM toàn màn hình."));
        LOG_WARN("OBS program output: fullscreen output requested, but no second screen is available.");
        return;
    }

    if (!m_programOutput) {
        m_programOutput = std::make_unique<ObsProgramOutputWindow>([this] {
            return m_program.backend.get();
        });
    }

    QScreen* outputScreen = screens.at(1);
    m_programOutput->setGeometry(outputScreen->geometry());
    m_programOutput->winId();
    if (m_programOutput->windowHandle()) m_programOutput->windowHandle()->setScreen(outputScreen);
    m_programOutput->showFullScreen();
    m_programOutput->raise();
    LOG_INFO("OBS program output: fullscreen PGM output opened on screen '{}'.", outputScreen->name().toStdString());
}

void ObsDualMediaTestWindow::closePanels() {
    if (m_programOutput) m_programOutput->hide();
    if (m_program.backend) m_program.backend->resetRenderSource();
    releaseFadeTransition();
    for (Panel* panel : {&m_preview, &m_program}) {
        destroyDisplay(*panel);
        if (panel->backend) panel->backend->close();
        if (panel->fadeOutgoing) panel->fadeOutgoing->close();
    }
}
