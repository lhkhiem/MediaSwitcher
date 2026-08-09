#include "MainWindow.h"
#include "engine/renderer/DirectXWindow.h"
#include "ui/FullscreenLEDWindow.h"
#include "ui/InputSlotWidget.h"
#include "ui/GlobalPlaylistDialog.h"
#include "ui/AudioMeterWidget.h"
#include "ui/AboutDialog.h"
#include "common/config/AppInfo.h"
#include "engine/input/GlobalPlaylistController.h"
#include "engine/audio/AudioEngine.h"
#include "common/logger/Logger.h"
#include <chrono>

#include <QMenuBar>
#include <QToolBar>
#include <QFileDialog>
#include <QStatusBar>
#include <QMessageBox>
#include <QGroupBox>
#include <QStyle>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    LOG_INFO("MainWindow created.");
    setupUi();

    m_inputManager.setOnInputListChanged([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            rebuildInputDock();
        }, Qt::QueuedConnection);
    });

    m_inputManager.setOnPreviewChanged([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            updateViewports();
            rebuildInputDock();
        }, Qt::QueuedConnection);
    });

    m_inputManager.setOnProgramChanged([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            updateViewports();
            rebuildInputDock();
            activatePgmAudio();  // Route audio from new PGM source to AudioEngine
        }, Qt::QueuedConnection);
    });

    // Initial update
    rebuildInputDock();
    updateViewports();

    m_playbackTimer = new QTimer(this);
    connect(m_playbackTimer, &QTimer::timeout, this, &MainWindow::updatePlaybackStatus);
    m_playbackTimer->start(100);
}

MainWindow::~MainWindow() {
    if (m_ledOutputWindow) {
        m_ledOutputWindow->close();
        delete m_ledOutputWindow;
        m_ledOutputWindow = nullptr;
    }
    LOG_INFO("MainWindow destroyed.");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_ledOutputWindow) {
        m_ledOutputWindow->close();
        delete m_ledOutputWindow;
        m_ledOutputWindow = nullptr;
    }
    LOG_INFO("MainWindow closeEvent triggered. Exiting application.");
    QApplication::quit();
    event->accept();
}

void MainWindow::setupUi() {
    this->setWindowTitle("MediaSwitcher - LED Screen Broadcast & Media Engine");
    this->resize(1366, 768);

    // Dark Studio QSS Style
    this->setStyleSheet(R"(
        QMainWindow {
            background-color: #121318;
        }
        QMenuBar {
            background-color: #161720;
            color: #D0D0D0;
            border-bottom: 1px solid #252633;
            font-weight: bold;
        }
        QMenuBar::item {
            padding: 4px 10px;
            background: transparent;
        }
        QMenuBar::item:selected {
            background-color: #2B2D3A;
            color: #00E5FF;
        }
        QMenu {
            background-color: #1E1F28;
            color: #E0E0E0;
            border: 1px solid #3E4154;
        }
        QMenu::item:selected {
            background-color: #00ACC1;
            color: #FFFFFF;
        }
        QToolBar {
            background-color: #1E1F28;
            border-bottom: 1px solid #2B2D3A;
            spacing: 8px;
            padding: 4px;
        }
        QToolButton {
            color: #E0E0E0;
            background-color: #2B2D3A;
            border: 1px solid #3E4154;
            border-radius: 4px;
            padding: 6px 12px;
            font-weight: bold;
        }
        QToolButton:hover {
            background-color: #3E4154;
            border-color: #5C607A;
        }
        QStatusBar {
            background-color: #181920;
            color: #A0A0A0;
            border-top: 1px solid #252633;
        }
    )");

    // Menu Bar
    QMenuBar* menuBar = this->menuBar();
    QMenu* helpMenu = menuBar->addMenu("Trợ giúp (&H)");
    QAction* aboutAction = helpMenu->addAction("ℹ️ Về MediaSwitcher & Bản quyền...");
    aboutAction->setShortcut(QKeySequence(Qt::Key_F1));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onShowAboutDialog);

    // Toolbar
    QToolBar* toolbar = addToolBar("Main Toolbar");
    toolbar->setMovable(false);

    QAction* addVideoAction = toolbar->addAction("📂 + Add Input(s)");
    connect(addVideoAction, &QAction::triggered, this, &MainWindow::onAddVideoInput);

    toolbar->addSeparator();

    // Multi-Monitor LED Output Selector
    QLabel* screenLabel = new QLabel(" Target Screen: ", this);
    screenLabel->setStyleSheet("color: #CCCCCC; font-weight: bold; font-size: 11px;");
    toolbar->addWidget(screenLabel);

    m_screenSelectorCombo = new QComboBox(this);
    m_screenSelectorCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #2B2D3A;
            color: #FFFFFF;
            border: 1px solid #44475A;
            border-radius: 4px;
            padding: 4px 8px;
            min-width: 180px;
        }
        QComboBox:hover {
            border-color: #00ACC1;
        }
        QComboBox::drop-down {
            border: none;
            width: 20px;
        }
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 5px solid #AAAAAA;
            margin-right: 6px;
        }
        QComboBox QAbstractItemView {
            background-color: #1E1F2E;
            color: #FFFFFF;
            border: 1px solid #44475A;
            selection-background-color: #00ACC1;
            selection-color: #FFFFFF;
            outline: none;
        }
        QComboBox QAbstractItemView::item {
            padding: 6px 10px;
            color: #FFFFFF;
        }
        QComboBox QAbstractItemView::item:hover {
            background-color: #2B2D3A;
            color: #00E5FF;
        }
    )");
    populateScreenSelector();
    toolbar->addWidget(m_screenSelectorCombo);

    m_fullscreenToggleBtn = new QPushButton("🖥 FULLSCREEN LED OUTPUT (OFF)", this);
    m_fullscreenToggleBtn->setCheckable(true);
    m_fullscreenToggleBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #D32F2F;
            color: #FFFFFF;
            font-weight: bold;
            border-radius: 4px;
            padding: 6px 12px;
        }
        QPushButton:checked {
            background-color: #388E3C;
        }
    )");
    connect(m_fullscreenToggleBtn, &QPushButton::clicked, this, &MainWindow::onToggleFullscreenLED);
    toolbar->addWidget(m_fullscreenToggleBtn);

    toolbar->addSeparator();

    QAction* aboutToolAction = toolbar->addAction("ℹ️ Bản quyền & Tác giả");
    connect(aboutToolAction, &QAction::triggered, this, &MainWindow::onShowAboutDialog);

    // Central Widget
    QWidget* centralWidget = new QWidget(this);
    this->setCentralWidget(centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // TOP PANEL: Dual Viewports (PVW & PGM) + Center Controls
    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->setSpacing(10);

    // 1. PREVIEW PANEL (PVW)
    QGroupBox* pvwGroup = new QGroupBox("PREVIEW (PVW)", centralWidget);
    pvwGroup->setStyleSheet(R"(
        QGroupBox {
            color: #FF9800;
            font-weight: bold;
            font-size: 13px;
            border: 2px solid #FF9800;
            border-radius: 6px;
            margin-top: 6px;
            padding-top: 10px;
            background-color: #181922;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 2px 8px;
            background-color: #FF9800;
            color: #000000;
            border-radius: 3px;
        }
    )");
    QVBoxLayout* pvwLayout = new QVBoxLayout(pvwGroup);
    pvwLayout->setContentsMargins(4, 16, 4, 4);

    m_pvwWindow = new DirectXWindow(pvwGroup);
    pvwLayout->addWidget(m_pvwWindow, 1); // Expand video viewport to fill space
    m_pvwWindow->initDirectX();

    // Broadcast Ultra-Compact Control Bar for PREVIEW
    QWidget* pvwBarWidget = new QWidget(pvwGroup);
    pvwBarWidget->setStyleSheet(R"(
        QWidget {
            background-color: #0E0F14;
            border-top: 1px solid #1E202C;
            border-bottom-left-radius: 4px;
            border-bottom-right-radius: 4px;
        }
    )");
    QVBoxLayout* pvwBarLayout = new QVBoxLayout(pvwBarWidget);
    pvwBarLayout->setContentsMargins(4, 2, 4, 2);
    pvwBarLayout->setSpacing(2);

    // Row 1: Time Display (Center) + Compact Action Buttons (Right)
    QHBoxLayout* pvwRow1 = new QHBoxLayout();
    pvwRow1->setContentsMargins(2, 0, 2, 0);
    pvwRow1->setSpacing(4);

    m_pvwTimeLabel = new QLabel("00:00:00 / 00:00:00", pvwBarWidget);
    m_pvwTimeLabel->setStyleSheet("color: #FFFFFF; font-family: Consolas, 'Courier New', monospace; font-weight: bold; font-size: 11px;");
    pvwRow1->addWidget(m_pvwTimeLabel);

    pvwRow1->addStretch();

    m_pvwLoopBtn = new QPushButton("Loop", pvwBarWidget);
    m_pvwLoopBtn->setFixedSize(48, 20);
    m_pvwLoopBtn->setCursor(Qt::PointingHandCursor);
    m_pvwLoopBtn->setToolTip("Toggle Video Loop");
    m_pvwLoopBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #388E3C;
            color: #FFFFFF;
            font-weight: bold;
            font-size: 10px;
            border: none;
            border-radius: 3px;
        }
        QPushButton:hover { background-color: #4CAF50; }
    )");
    connect(m_pvwLoopBtn, &QPushButton::clicked, this, &MainWindow::onPvwLoopToggleClicked);
    pvwRow1->addWidget(m_pvwLoopBtn);

    m_pvwResetBtn = new QPushButton("Reset", pvwBarWidget);
    m_pvwResetBtn->setFixedSize(48, 20);
    m_pvwResetBtn->setCursor(Qt::PointingHandCursor);
    m_pvwResetBtn->setToolTip("Reset to 00:00 (Pause)");
    m_pvwResetBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #2B2D3A;
            color: #DDDDDD;
            font-weight: bold;
            font-size: 10px;
            border: 1px solid #3E4154;
            border-radius: 3px;
        }
        QPushButton:hover {
            background-color: #3E4154;
            color: #FFFFFF;
        }
    )");
    connect(m_pvwResetBtn, &QPushButton::clicked, this, &MainWindow::onPvwResetClicked);
    pvwRow1->addWidget(m_pvwResetBtn);

    m_pvwPlayPauseBtn = new QPushButton("▶", pvwBarWidget);
    m_pvwPlayPauseBtn->setFixedSize(26, 20);
    m_pvwPlayPauseBtn->setCursor(Qt::PointingHandCursor);
    m_pvwPlayPauseBtn->setToolTip("Play/Pause");
    m_pvwPlayPauseBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #2B2D3A;
            color: #FFFFFF;
            font-weight: bold;
            font-size: 11px;
            border: 1px solid #3E4154;
            border-radius: 3px;
        }
        QPushButton:hover {
            background-color: #00ACC1;
        }
    )");
    connect(m_pvwPlayPauseBtn, &QPushButton::clicked, this, &MainWindow::onPvwPlayPauseClicked);
    pvwRow1->addWidget(m_pvwPlayPauseBtn);

    pvwBarLayout->addLayout(pvwRow1);

    // Row 2: Sleek Timeline Slider
    m_pvwSeekSlider = new QSlider(Qt::Horizontal, pvwBarWidget);
    m_pvwSeekSlider->setRange(0, 1000);
    m_pvwSeekSlider->setValue(0);
    m_pvwSeekSlider->setCursor(Qt::PointingHandCursor);
    m_pvwSeekSlider->setFixedHeight(10);
    m_pvwSeekSlider->setStyleSheet(R"(
        QSlider::groove:horizontal {
            height: 4px;
            background: #1C1E2A;
            border-radius: 2px;
        }
        QSlider::sub-page:horizontal {
            background: #FF9800;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #FFFFFF;
            border: 1px solid #FF9800;
            width: 10px;
            height: 10px;
            margin-top: -3px;
            margin-bottom: -3px;
            border-radius: 5px;
        }
        QSlider::handle:horizontal:hover {
            background: #FFE0B2;
        }
    )");
    connect(m_pvwSeekSlider, &QSlider::sliderPressed, this, [this]() { m_isUserSeeking = true; });
    connect(m_pvwSeekSlider, &QSlider::sliderReleased, this, [this]() {
        m_isUserSeeking = false;
        onPvwSeekSliderSliderMoved(m_pvwSeekSlider->value());
    });
    pvwBarLayout->addWidget(m_pvwSeekSlider);

    pvwLayout->addWidget(pvwBarWidget);

    topLayout->addWidget(pvwGroup, 5);

    // 2. CENTER TRANSITION CONTROL PANEL
    QVBoxLayout* centerControlLayout = new QVBoxLayout();
    centerControlLayout->setAlignment(Qt::AlignCenter);
    centerControlLayout->setSpacing(8);


    // FTB Emergency Button (Fade To Black)
    m_ftbBtn = new QPushButton("FTB", centralWidget);
    m_ftbBtn->setFixedSize(90, 36);
    m_ftbBtn->setToolTip("FTB - Fade To Black (F12)");
    m_ftbBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #212121;
            color: #E53935;
            font-weight: bold;
            font-size: 14px;
            border: 2px solid #B71C1C;
            border-radius: 6px;
        }
        QPushButton:hover {
            background-color: #B71C1C;
            color: #FFFFFF;
        }
    )");
    connect(m_ftbBtn, &QPushButton::clicked, this, &MainWindow::onFTBClicked);
    centerControlLayout->addWidget(m_ftbBtn);

    QPushButton* quickPlayBtn = new QPushButton("Quick Play", centralWidget);
    quickPlayBtn->setFixedSize(90, 38);
    quickPlayBtn->setToolTip("Play Preview & CUT Live (Space)");
    quickPlayBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #00ACC1;
            color: #FFFFFF;
            font-weight: bold;
            font-size: 13px;
            border: none;
            border-radius: 6px;
        }
        QPushButton:hover {
            background-color: #26C6DA;
        }
        QPushButton:pressed {
            background-color: #00838F;
        }
    )");
    connect(quickPlayBtn, &QPushButton::clicked, this, &MainWindow::onQuickPlayClicked);
    centerControlLayout->addWidget(quickPlayBtn);

    QPushButton* cutBtn = new QPushButton("CUT", centralWidget);
    cutBtn->setFixedSize(90, 44);
    cutBtn->setToolTip("CUT Transition (Enter)");
    cutBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #4CAF50;
            color: #FFFFFF;
            font-weight: bold;
            font-size: 16px;
            border: none;
            border-radius: 6px;
        }
        QPushButton:hover {
            background-color: #66BB6A;
        }
        QPushButton:pressed {
            background-color: #388E3C;
        }
    )");
    connect(cutBtn, &QPushButton::clicked, this, &MainWindow::onCutClicked);
    centerControlLayout->addWidget(cutBtn);

    QPushButton* fadeBtn = new QPushButton("FADE", centralWidget);
    fadeBtn->setFixedSize(90, 44);
    fadeBtn->setToolTip("FADE Transition (F1 / F5)");
    fadeBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #2196F3;
            color: #FFFFFF;
            font-weight: bold;
            font-size: 16px;
            border: none;
            border-radius: 6px;
        }
        QPushButton:hover {
            background-color: #42A5F5;
        }
        QPushButton:pressed {
            background-color: #1976D2;
        }
    )");
    connect(fadeBtn, &QPushButton::clicked, this, &MainWindow::onFadeClicked);
    centerControlLayout->addWidget(fadeBtn);

    // Manual T-Bar Transition Slider
    QLabel* tbarHeaderLabel = new QLabel("T-Bar", centralWidget);
    tbarHeaderLabel->setStyleSheet("color: #FF9800; font-size: 10px; font-weight: bold;");
    tbarHeaderLabel->setAlignment(Qt::AlignCenter);
    centerControlLayout->addWidget(tbarHeaderLabel);

    m_tbarSlider = new QSlider(Qt::Vertical, centralWidget);
    m_tbarSlider->setRange(0, 1000);
    m_tbarSlider->setValue(0);
    m_tbarSlider->setFixedHeight(75);
    m_tbarSlider->setCursor(Qt::PointingHandCursor);
    m_tbarSlider->setToolTip("T-Bar - Cần gạt chuyển cảnh thủ công (Manual Transition)");
    m_tbarSlider->setStyleSheet(R"(
        QSlider::groove:vertical {
            width: 8px;
            background: #1C1E2A;
            border-radius: 4px;
        }
        QSlider::sub-page:vertical {
            background: #2196F3;
            border-radius: 4px;
        }
        QSlider::handle:vertical {
            background: #FF9800;
            border: 1px solid #FFFFFF;
            height: 14px;
            margin-left: -5px;
            margin-right: -5px;
            border-radius: 4px;
        }
    )");
    connect(m_tbarSlider, &QSlider::valueChanged, this, &MainWindow::onTBarSliderMoved);
    connect(m_tbarSlider, &QSlider::sliderReleased, this, [this]() {
        if (m_tbarSlider->value() > 500) {
            onCutClicked();
        }
        m_tbarSlider->setValue(0);
    });
    centerControlLayout->addWidget(m_tbarSlider, 0, Qt::AlignCenter);

    QLabel* durationLabel = new QLabel("Fade Speed:", centralWidget);
    durationLabel->setStyleSheet("color: #AAAAAA; font-size: 10px; font-weight: bold;");
    durationLabel->setAlignment(Qt::AlignCenter);
    centerControlLayout->addWidget(durationLabel);

    m_fadeDurationCombo = new QComboBox(centralWidget);
    m_fadeDurationCombo->addItem("500 ms", 500);
    m_fadeDurationCombo->addItem("1000 ms", 1000);
    m_fadeDurationCombo->addItem("2000 ms", 2000);
    m_fadeDurationCombo->setFixedWidth(90);
    m_fadeDurationCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #2B2D3A;
            color: #FFFFFF;
            border: 1px solid #44475A;
            border-radius: 4px;
            padding: 4px;
        }
    )");
    centerControlLayout->addWidget(m_fadeDurationCombo);

    m_playlistToggleBtn = new QPushButton("▶ START PLAYLIST", centralWidget);
    m_playlistToggleBtn->setFixedWidth(110);
    m_playlistToggleBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #388E3C; color: #FFFFFF; font-weight: bold; font-size: 11px;
            border: 1px solid #4CAF50; border-radius: 4px; padding: 6px;
        }
        QPushButton:hover { background-color: #4CAF50; }
    )");
    connect(m_playlistToggleBtn, &QPushButton::clicked, this, &MainWindow::onToggleGlobalPlaylist);
    centerControlLayout->addWidget(m_playlistToggleBtn);

    // Playlist Navigation Cluster (PREV / PAUSE / NEXT)
    QHBoxLayout* playlistNavLayout = new QHBoxLayout();
    playlistNavLayout->setSpacing(4);

    m_playlistPrevBtn = new QPushButton("⏮", centralWidget);
    m_playlistPrevBtn->setFixedSize(32, 28);
    m_playlistPrevBtn->setEnabled(false);
    m_playlistPrevBtn->setToolTip("Jump to Previous Playlist Track");
    m_playlistPrevBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #0288D1; color: #FFFFFF; font-weight: bold; font-size: 12px;
            border: 1px solid #039BE5; border-radius: 4px;
        }
        QPushButton:hover { background-color: #039BE5; }
        QPushButton:disabled { background-color: #33364A; color: #666666; border: none; }
    )");
    connect(m_playlistPrevBtn, &QPushButton::clicked, this, &MainWindow::onPlaylistPrevClicked);
    playlistNavLayout->addWidget(m_playlistPrevBtn);

    m_playlistPauseBtn = new QPushButton("⏸", centralWidget);
    m_playlistPauseBtn->setFixedSize(38, 28);
    m_playlistPauseBtn->setEnabled(false);
    m_playlistPauseBtn->setToolTip("Pause / Resume Playlist");
    m_playlistPauseBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #F57F17; color: #FFFFFF; font-weight: bold; font-size: 12px;
            border: 1px solid #FBC02D; border-radius: 4px;
        }
        QPushButton:hover { background-color: #FBC02D; }
        QPushButton:disabled { background-color: #33364A; color: #666666; border: none; }
    )");
    connect(m_playlistPauseBtn, &QPushButton::clicked, this, &MainWindow::onPauseGlobalPlaylist);
    playlistNavLayout->addWidget(m_playlistPauseBtn);

    m_playlistNextBtn = new QPushButton("⏭", centralWidget);
    m_playlistNextBtn->setFixedSize(32, 28);
    m_playlistNextBtn->setEnabled(false);
    m_playlistNextBtn->setToolTip("Jump to Next Playlist Track");
    m_playlistNextBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #0288D1; color: #FFFFFF; font-weight: bold; font-size: 12px;
            border: 1px solid #039BE5; border-radius: 4px;
        }
        QPushButton:hover { background-color: #039BE5; }
        QPushButton:disabled { background-color: #33364A; color: #666666; border: none; }
    )");
    connect(m_playlistNextBtn, &QPushButton::clicked, this, &MainWindow::onPlaylistNextClicked);
    playlistNavLayout->addWidget(m_playlistNextBtn);

    centerControlLayout->addLayout(playlistNavLayout);

    m_playlistConfigBtn = new QPushButton("📋 Config Playlist", centralWidget);
    m_playlistConfigBtn->setFixedWidth(110);
    m_playlistConfigBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #00838F; color: #FFFFFF; font-size: 10px; font-weight: bold;
            border: 1px solid #00ACC1; border-radius: 4px; padding: 4px;
        }
        QPushButton:hover { background-color: #00ACC1; }
    )");
    connect(m_playlistConfigBtn, &QPushButton::clicked, this, &MainWindow::onConfigGlobalPlaylist);
    centerControlLayout->addWidget(m_playlistConfigBtn);

    topLayout->addLayout(centerControlLayout, 0);

    // Dedicated MASTER AUDIO Mixer Column
    QGroupBox* audioGroup = new QGroupBox("AUDIO", centralWidget);
    audioGroup->setStyleSheet(R"(
        QGroupBox {
            color: #29B6F6; font-weight: bold; font-size: 11px;
            border: 2px solid #0288D1; border-radius: 6px; margin-top: 6px; padding-top: 10px;
            background-color: #12131C;
        }
        QGroupBox::title {
            subcontrol-origin: margin; subcontrol-position: top center;
            padding: 2px 6px; background-color: #0288D1; color: #FFFFFF; border-radius: 3px;
        }
    )");
    QVBoxLayout* audioColLayout = new QVBoxLayout(audioGroup);
    audioColLayout->setContentsMargins(6, 12, 6, 6);
    audioColLayout->setSpacing(6);
    audioColLayout->setAlignment(Qt::AlignCenter);

    m_audioMeterWidget = new AudioMeterWidget(audioGroup);
    m_audioMeterWidget->setFixedHeight(120);
    audioColLayout->addWidget(m_audioMeterWidget, 0, Qt::AlignCenter);

    m_volumeLabel = new QLabel("100%", audioGroup);
    m_volumeLabel->setStyleSheet("color: #29B6F6; font-size: 11px; font-weight: bold;");
    m_volumeLabel->setAlignment(Qt::AlignCenter);
    audioColLayout->addWidget(m_volumeLabel);

    m_masterVolumeSlider = new QSlider(Qt::Vertical, audioGroup);
    m_masterVolumeSlider->setRange(0, 100);
    m_masterVolumeSlider->setValue(100);
    m_masterVolumeSlider->setFixedHeight(85);
    m_masterVolumeSlider->setCursor(Qt::PointingHandCursor);
    m_masterVolumeSlider->setToolTip("Master Audio Volume Fader");
    m_masterVolumeSlider->setStyleSheet(R"(
        QSlider::groove:vertical { width: 8px; background: #1C1E2A; border-radius: 4px; }
        QSlider::sub-page:vertical { background: #29B6F6; border-radius: 4px; }
        QSlider::handle:vertical {
            background: #FFFFFF; border: 1px solid #0288D1; height: 14px;
            margin-left: -4px; margin-right: -4px; border-radius: 4px;
        }
    )");
    connect(m_masterVolumeSlider, &QSlider::valueChanged, this, &MainWindow::onMasterVolumeChanged);
    audioColLayout->addWidget(m_masterVolumeSlider, 0, Qt::AlignCenter);

    m_muteBtn = new QPushButton("🔊 MUTE", audioGroup);
    m_muteBtn->setFixedSize(64, 24);
    m_muteBtn->setToolTip("Mute / Unmute Audio");
    m_muteBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #388E3C; color: #FFFFFF; font-weight: bold; font-size: 10px;
            border: 1px solid #4CAF50; border-radius: 3px;
        }
        QPushButton:hover { background-color: #4CAF50; }
    )");
    connect(m_muteBtn, &QPushButton::clicked, this, &MainWindow::onMuteToggled);
    audioColLayout->addWidget(m_muteBtn, 0, Qt::AlignCenter);

    topLayout->addWidget(audioGroup, 0);

    // 3. PROGRAM PANEL (PGM)
    QGroupBox* pgmGroup = new QGroupBox("PROGRAM (PGM) - LIVE", centralWidget);
    pgmGroup->setStyleSheet(R"(
        QGroupBox {
            color: #E53935;
            font-weight: bold;
            font-size: 13px;
            border: 2px solid #E53935;
            border-radius: 6px;
            margin-top: 6px;
            padding-top: 10px;
            background-color: #181922;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 2px 8px;
            background-color: #E53935;
            color: #FFFFFF;
            border-radius: 3px;
        }
    )");
    QVBoxLayout* pgmLayout = new QVBoxLayout(pgmGroup);
    pgmLayout->setContentsMargins(4, 16, 4, 4);

    m_pgmWindow = new DirectXWindow(pgmGroup);
    pgmLayout->addWidget(m_pgmWindow, 1); // Expand video viewport to fill space
    m_pgmWindow->initDirectX();

    // Broadcast Ultra-Compact Control Bar for PROGRAM (PGM) - LIVE
    QWidget* pgmBarWidget = new QWidget(pgmGroup);
    pgmBarWidget->setStyleSheet(R"(
        QWidget {
            background-color: #0E0F14;
            border-top: 1px solid #1E202C;
            border-bottom-left-radius: 4px;
            border-bottom-right-radius: 4px;
        }
    )");
    QVBoxLayout* pgmBarLayout = new QVBoxLayout(pgmBarWidget);
    pgmBarLayout->setContentsMargins(4, 2, 4, 2);
    pgmBarLayout->setSpacing(2);

    // Row 1: Time Display (Center) + Compact Action Buttons (Right)
    QHBoxLayout* pgmRow1 = new QHBoxLayout();
    pgmRow1->setContentsMargins(2, 0, 2, 0);
    pgmRow1->setSpacing(4);

    m_pgmTimeLabel = new QLabel("00:00:00 / 00:00:00", pgmBarWidget);
    m_pgmTimeLabel->setStyleSheet("color: #FFFFFF; font-family: Consolas, 'Courier New', monospace; font-weight: bold; font-size: 11px;");
    pgmRow1->addWidget(m_pgmTimeLabel);

    pgmRow1->addStretch();

    m_pgmLoopBtn = new QPushButton("Loop", pgmBarWidget);
    m_pgmLoopBtn->setFixedSize(48, 20);
    m_pgmLoopBtn->setCursor(Qt::PointingHandCursor);
    m_pgmLoopBtn->setToolTip("Toggle Video Loop");
    m_pgmLoopBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #388E3C;
            color: #FFFFFF;
            font-weight: bold;
            font-size: 10px;
            border: none;
            border-radius: 3px;
        }
        QPushButton:hover { background-color: #4CAF50; }
    )");
    connect(m_pgmLoopBtn, &QPushButton::clicked, this, &MainWindow::onPgmLoopToggleClicked);
    pgmRow1->addWidget(m_pgmLoopBtn);

    m_pgmResetBtn = new QPushButton("Reset", pgmBarWidget);
    m_pgmResetBtn->setFixedSize(48, 20);
    m_pgmResetBtn->setCursor(Qt::PointingHandCursor);
    m_pgmResetBtn->setToolTip("Reset to 00:00 (Pause)");
    m_pgmResetBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #2B2D3A;
            color: #DDDDDD;
            font-weight: bold;
            font-size: 10px;
            border: 1px solid #3E4154;
            border-radius: 3px;
        }
        QPushButton:hover {
            background-color: #3E4154;
            color: #FFFFFF;
        }
    )");
    connect(m_pgmResetBtn, &QPushButton::clicked, this, &MainWindow::onPgmResetClicked);
    pgmRow1->addWidget(m_pgmResetBtn);

    m_pgmPlayPauseBtn = new QPushButton("▶", pgmBarWidget);
    m_pgmPlayPauseBtn->setFixedSize(26, 20);
    m_pgmPlayPauseBtn->setCursor(Qt::PointingHandCursor);
    m_pgmPlayPauseBtn->setToolTip("Play/Pause");
    m_pgmPlayPauseBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #2B2D3A;
            color: #FFFFFF;
            font-weight: bold;
            font-size: 11px;
            border: 1px solid #3E4154;
            border-radius: 3px;
        }
        QPushButton:hover {
            background-color: #E53935;
        }
    )");
    connect(m_pgmPlayPauseBtn, &QPushButton::clicked, this, &MainWindow::onPgmPlayPauseClicked);
    pgmRow1->addWidget(m_pgmPlayPauseBtn);

    pgmBarLayout->addLayout(pgmRow1);

    // Row 2: Sleek Timeline Slider (Red PGM Track)
    m_pgmSeekSlider = new QSlider(Qt::Horizontal, pgmBarWidget);
    m_pgmSeekSlider->setRange(0, 1000);
    m_pgmSeekSlider->setValue(0);
    m_pgmSeekSlider->setCursor(Qt::PointingHandCursor);
    m_pgmSeekSlider->setFixedHeight(10);
    m_pgmSeekSlider->setStyleSheet(R"(
        QSlider::groove:horizontal {
            height: 4px;
            background: #1C1E2A;
            border-radius: 2px;
        }
        QSlider::sub-page:horizontal {
            background: #E53935;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #FFFFFF;
            border: 1px solid #E53935;
            width: 10px;
            height: 10px;
            margin-top: -3px;
            margin-bottom: -3px;
            border-radius: 5px;
        }
        QSlider::handle:horizontal:hover {
            background: #FFCDD2;
        }
    )");
    connect(m_pgmSeekSlider, &QSlider::sliderPressed, this, [this]() { m_isPgmUserSeeking = true; });
    connect(m_pgmSeekSlider, &QSlider::sliderReleased, this, [this]() {
        m_isPgmUserSeeking = false;
        onPgmSeekSliderSliderMoved(m_pgmSeekSlider->value());
    });
    pgmBarLayout->addWidget(m_pgmSeekSlider);

    pgmLayout->addWidget(pgmBarWidget);

    topLayout->addWidget(pgmGroup, 5);

    mainLayout->addLayout(topLayout, 6);

    // BOTTOM PANEL: Multi-Input Dock Grid
    QGroupBox* dockGroup = new QGroupBox("INPUT CHANNELS", centralWidget);
    dockGroup->setStyleSheet(R"(
        QGroupBox {
            color: #CCCCCC;
            font-weight: bold;
            font-size: 12px;
            border: 1px solid #2E3040;
            border-radius: 6px;
            margin-top: 6px;
            padding-top: 10px;
            background-color: #161720;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 2px 6px;
            background-color: #282A36;
            color: #AAAAAA;
        }
    )");

    QVBoxLayout* dockGroupLayout = new QVBoxLayout(dockGroup);
    dockGroupLayout->setContentsMargins(6, 12, 6, 6);

    QHBoxLayout* dockHeaderBar = new QHBoxLayout();
    dockHeaderBar->setContentsMargins(0, 0, 0, 4);
    dockHeaderBar->setSpacing(6);

    QLabel* categoryLabel = new QLabel("Filter:", dockGroup);
    categoryLabel->setStyleSheet("color: #AAAAAA; font-size: 11px; font-weight: bold;");
    dockHeaderBar->addWidget(categoryLabel);

    auto createCatBtn = [this, dockGroup](const QString& text, const QString& catKey, const QString& colorHex) {
        QPushButton* btn = new QPushButton(text, dockGroup);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(22);
        btn->setStyleSheet(QString(R"(
            QPushButton {
                background-color: #212330;
                color: %1;
                font-weight: bold;
                font-size: 10px;
                border: 1px solid #33364A;
                border-radius: 4px;
                padding: 0 8px;
            }
            QPushButton:hover {
                background-color: %1;
                color: #000000;
            }
        )").arg(colorHex));
        connect(btn, &QPushButton::clicked, this, [this, catKey]() { onCategoryFilterClicked(catKey); });
        return btn;
    };

    dockHeaderBar->addWidget(createCatBtn("All", "ALL", "#00ACC1"));
    dockHeaderBar->addWidget(createCatBtn("🎥 Videos", "VIDEO", "#FF9800"));
    dockHeaderBar->addWidget(createCatBtn("🖼 Images", "IMAGE", "#4CAF50"));

    // Rows Mode Toggle (1 Row / 2 Rows / Grid)
    QLabel* rowsLabel = new QLabel("Rows:", dockGroup);
    rowsLabel->setStyleSheet("color: #AAAAAA; font-size: 11px; font-weight: bold; margin-left: 8px;");
    dockHeaderBar->addWidget(rowsLabel);

    auto createRowBtn = [this, dockGroup](const QString& text, GridRowsMode mode) {
        QPushButton* btn = new QPushButton(text, dockGroup);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(22);
        btn->setStyleSheet(R"(
            QPushButton {
                background-color: #212330;
                color: #4FC3F7;
                font-weight: bold;
                font-size: 10px;
                border: 1px solid #33364A;
                border-radius: 4px;
                padding: 0 6px;
            }
            QPushButton:hover {
                background-color: #0288D1;
                color: #FFFFFF;
            }
        )");
        connect(btn, &QPushButton::clicked, this, [this, mode]() {
            m_rowsMode = mode;
            rebuildInputDock();
        });
        return btn;
    };
    dockHeaderBar->addWidget(createRowBtn("1 Row", GridRowsMode::OneRow));
    dockHeaderBar->addWidget(createRowBtn("2 Rows", GridRowsMode::TwoRows));
    dockHeaderBar->addWidget(createRowBtn("3 Rows", GridRowsMode::ThreeRows));
    dockHeaderBar->addWidget(createRowBtn("Grid", GridRowsMode::AutoGrid));

    // Thumbnail Size Toggle (Small / Normal / Large)
    QLabel* sizeLabel = new QLabel("Size:", dockGroup);
    sizeLabel->setStyleSheet("color: #AAAAAA; font-size: 11px; font-weight: bold; margin-left: 8px;");
    dockHeaderBar->addWidget(sizeLabel);

    auto createSizeBtn = [this, dockGroup](const QString& text, ThumbnailSize size) {
        QPushButton* btn = new QPushButton(text, dockGroup);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(22);
        btn->setStyleSheet(R"(
            QPushButton {
                background-color: #212330;
                color: #FFB74D;
                font-weight: bold;
                font-size: 10px;
                border: 1px solid #33364A;
                border-radius: 4px;
                padding: 0 6px;
            }
            QPushButton:hover {
                background-color: #F57C00;
                color: #FFFFFF;
            }
        )");
        connect(btn, &QPushButton::clicked, this, [this, size]() {
            m_thumbSize = size;
            rebuildInputDock();
        });
        return btn;
    };
    dockHeaderBar->addWidget(createSizeBtn("Small", ThumbnailSize::Small));
    dockHeaderBar->addWidget(createSizeBtn("Normal", ThumbnailSize::Normal));
    dockHeaderBar->addWidget(createSizeBtn("Large", ThumbnailSize::Large));

    dockHeaderBar->addStretch();

    m_searchInput = new QLineEdit(dockGroup);
    m_searchInput->setPlaceholderText("🔍 Search inputs...");
    m_searchInput->setFixedWidth(160);
    m_searchInput->setFixedHeight(22);
    m_searchInput->setStyleSheet(R"(
        QLineEdit {
            background-color: #212330;
            color: #FFFFFF;
            font-size: 11px;
            border: 1px solid #33364A;
            border-radius: 4px;
            padding: 2px 6px;
        }
        QLineEdit:focus {
            border: 1px solid #00ACC1;
        }
    )");
    connect(m_searchInput, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    dockHeaderBar->addWidget(m_searchInput);

    dockGroupLayout->addLayout(dockHeaderBar);

    QScrollArea* scrollArea = new QScrollArea(dockGroup);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    m_dockContainer = new QWidget(scrollArea);
    // Explicitly set dark background to prevent Qt default white/light palette
    m_dockContainer->setAutoFillBackground(true);
    QPalette dockPalette = m_dockContainer->palette();
    dockPalette.setColor(QPalette::Window, QColor("#161720"));
    m_dockContainer->setPalette(dockPalette);
    m_dockLayout = new QHBoxLayout(m_dockContainer);
    m_dockLayout->setContentsMargins(0, 0, 0, 0);
    m_dockLayout->setSpacing(8);
    m_dockLayout->setAlignment(Qt::AlignLeft);

    scrollArea->setWidget(m_dockContainer);
    dockGroupLayout->addWidget(scrollArea);

    mainLayout->addWidget(dockGroup, 4);

    statusBar()->showMessage("MediaSwitcher LED Engine Ready. Dual Viewports Live.");

    QPushButton* copyrightBtn = new QPushButton(QString("© %1").arg(AppInfo::COPYRIGHT), this);
    copyrightBtn->setFlat(true);
    copyrightBtn->setCursor(Qt::PointingHandCursor);
    copyrightBtn->setToolTip("Click để xem chi tiết thông tin Tác giả & Bản quyền phần mềm");
    copyrightBtn->setStyleSheet(R"(
        QPushButton {
            color: #81C784;
            font-weight: bold;
            font-size: 11px;
            border: none;
            padding: 2px 8px;
        }
        QPushButton:hover {
            color: #00E5FF;
            text-decoration: underline;
        }
    )");
    connect(copyrightBtn, &QPushButton::clicked, this, &MainWindow::onShowAboutDialog);
    statusBar()->addPermanentWidget(copyrightBtn);
}

void MainWindow::populateScreenSelector() {
    if (!m_screenSelectorCombo) return;

    m_screenSelectorCombo->clear();
    const auto screens = QGuiApplication::screens();

    int screenIdx = 0;
    for (QScreen* screen : screens) {
        QString label = QString("Display %1: %2 (%3x%4)")
                            .arg(screenIdx + 1)
                            .arg(screen->name())
                            .arg(screen->geometry().width())
                            .arg(screen->geometry().height());
        if (screen == QGuiApplication::primaryScreen()) {
            label += " [Primary]";
        }
        m_screenSelectorCombo->addItem(label, screenIdx);
        screenIdx++;
    }
}

void MainWindow::updateViewports() {
    auto pvwSource = m_inputManager.previewSource();
    if (m_pvwWindow) {
        m_pvwWindow->setMediaSource(pvwSource);
    }

    auto pgmSource = m_inputManager.programSource();
    if (m_pgmWindow) {
        m_pgmWindow->setMediaSource(pgmSource);
    }

    if (m_ledOutputWindow) {
        m_ledOutputWindow->setMediaSource(pgmSource);
    }
}

void MainWindow::onToggleFullscreenLED() {
    if (m_fullscreenToggleBtn->isChecked()) {
        int screenIdx = m_screenSelectorCombo->currentData().toInt();
        const auto screens = QGuiApplication::screens();
        QScreen* targetScreen = (screenIdx >= 0 && screenIdx < screens.size()) ? screens.at(screenIdx) : QGuiApplication::primaryScreen();

        if (m_ledOutputWindow) {
            m_ledOutputWindow->close();
            delete m_ledOutputWindow;
            m_ledOutputWindow = nullptr;
        }

        m_ledOutputWindow = new FullscreenLEDWindow();
        connect(m_ledOutputWindow, &FullscreenLEDWindow::windowClosed, this, [this]() {
            if (m_fullscreenToggleBtn) {
                m_fullscreenToggleBtn->setChecked(false);
                m_fullscreenToggleBtn->setText("🖥 FULLSCREEN LED OUTPUT (OFF)");
                statusBar()->showMessage("Fullscreen LED Output closed.");
            }
        });

        if (m_ledOutputWindow->initOutput(targetScreen)) {
            m_ledOutputWindow->setMediaSource(m_inputManager.programSource());
            m_fullscreenToggleBtn->setText("🖥 FULLSCREEN LED OUTPUT (ON)");
            statusBar()->showMessage(QString("Fullscreen LED Output ACTIVE on Display: %1").arg(m_screenSelectorCombo->currentText()));
        } else {
            delete m_ledOutputWindow;
            m_ledOutputWindow = nullptr;
            m_fullscreenToggleBtn->setChecked(false);
            m_fullscreenToggleBtn->setText("🖥 FULLSCREEN LED OUTPUT (OFF)");
        }
    } else {
        if (m_ledOutputWindow) {
            m_ledOutputWindow->close();
            delete m_ledOutputWindow;
            m_ledOutputWindow = nullptr;
        }
        m_fullscreenToggleBtn->setText("🖥 FULLSCREEN LED OUTPUT (OFF)");
        statusBar()->showMessage("Fullscreen LED Output closed.");
    }
}

void MainWindow::rebuildInputDock() {
    if (!m_dockContainer) return;

    if (m_dockContainer->layout()) {
        QLayoutItem* item;
        while ((item = m_dockContainer->layout()->takeAt(0)) != nullptr) {
            if (item->widget()) {
                delete item->widget();
            }
            delete item;
        }
        delete m_dockContainer->layout();
    }

    QGridLayout* gridLayout = new QGridLayout(m_dockContainer);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(6);
    gridLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    const auto& slotList = m_inputManager.inputSlots();
    int pvwId = m_inputManager.previewSlotId();
    int pgmId = m_inputManager.programSlotId();

    int cardW = 160;
    int cardH = 100;
    if (m_thumbSize == ThumbnailSize::Small) {
        cardW = 115;
        cardH = 72;
    } else if (m_thumbSize == ThumbnailSize::Large) {
        cardW = 200;
        cardH = 125;
    }

    int cardIndex = 0;
    for (const auto& slot : slotList) {
        // Category Filter
        if (m_activeCategory == "VIDEO" && slot.type != InputType::VideoFile) continue;
        if (m_activeCategory == "IMAGE" && slot.type != InputType::ImageFile) continue;

        // Search Query Filter
        if (!m_searchQuery.isEmpty()) {
            QString nameStr = QString::fromStdString(slot.name);
            if (!nameStr.contains(m_searchQuery, Qt::CaseInsensitive)) continue;
        }

        bool isPvw = (slot.id == pvwId);
        bool isPgm = (slot.id == pgmId);

        InputSlotWidget* slotWidget = new InputSlotWidget(slot, isPvw, isPgm, m_dockContainer);
        slotWidget->setCardSize(cardW, cardH);

        connect(slotWidget, &InputSlotWidget::clicked, this, [this](int slotId) {
            m_inputManager.setPreviewSlot(slotId);
        });
        connect(slotWidget, &InputSlotWidget::removeRequested, this, [this](int slotId) {
            m_inputManager.removeSlot(slotId);
        });

        if (m_rowsMode == GridRowsMode::ThreeRows) {
            int row = cardIndex % 3;
            int col = cardIndex / 3;
            gridLayout->addWidget(slotWidget, row, col);
        } else if (m_rowsMode == GridRowsMode::TwoRows) {
            int row = cardIndex % 2;
            int col = cardIndex / 2;
            gridLayout->addWidget(slotWidget, row, col);
        } else if (m_rowsMode == GridRowsMode::AutoGrid) {
            int maxColsPerLine = 8;
            int row = cardIndex / maxColsPerLine;
            int col = cardIndex % maxColsPerLine;
            gridLayout->addWidget(slotWidget, row, col);
        } else { // OneRow
            gridLayout->addWidget(slotWidget, 0, cardIndex);
        }

        cardIndex++;
    }
}

void MainWindow::onCategoryFilterClicked(const QString& category) {
    m_activeCategory = category;
    rebuildInputDock();
}

void MainWindow::onSearchTextChanged(const QString& text) {
    m_searchQuery = text.trimmed();
    rebuildInputDock();
}

void MainWindow::onAddVideoInput() {
    QString filter = "Media Files (*.mp4 *.mkv *.mov *.avi *.flv *.wmv *.webm *.ts *.m4v *.mpg *.mpeg *.vob *.3gp *.m2ts *.mts *.png *.jpg *.jpeg *.bmp *.webp *.gif *.tiff);;"
                     "Video Files (*.mp4 *.mkv *.mov *.avi *.flv *.wmv *.webm *.ts *.m4v *.mpg *.mpeg *.vob *.3gp *.m2ts *.mts);;"
                     "Image Files (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.tiff);;"
                     "All Files (*.*)";
    QStringList filePaths = QFileDialog::getOpenFileNames(this, "Select Media Files (Multi-Select)", "", filter);

    if (filePaths.isEmpty()) return;

    int addedCount = 0;
    int firstAddedSlotId = -1;
    for (const QString& filePath : filePaths) {
        if (filePath.isEmpty()) continue;
        std::string utf8Path = filePath.toUtf8().toStdString();
        int slotId = m_inputManager.addFileSlot(utf8Path);
        if (slotId > 0) {
            addedCount++;
            if (firstAddedSlotId <= 0) {
                firstAddedSlotId = slotId;
            }
        }
    }

    if (firstAddedSlotId > 0 && m_inputManager.previewSlotId() <= 0) {
        m_inputManager.setPreviewSlot(firstAddedSlotId);
    }

    if (addedCount == 1) {
        statusBar()->showMessage(QString("Added 1 Input: %1").arg(filePaths.first()));
    } else if (addedCount > 1) {
        statusBar()->showMessage(QString("Added %1 Inputs to Channel Grid").arg(addedCount));
    }
}

void MainWindow::onAddPlaylistInput() {
    // Deprecated standalone playlist
}

void MainWindow::stopGlobalPlaylistUI() {
    if (m_playlistController.isActive()) {
        m_playlistController.stop();
        for (const auto& slot : m_inputManager.inputSlots()) {
            if (slot.source) slot.source->setLoop(true);
        }
        if (m_playlistToggleBtn) {
            m_playlistToggleBtn->setText("▶ START PLAYLIST");
            m_playlistToggleBtn->setStyleSheet(R"(
                QPushButton {
                    background-color: #388E3C; color: #FFFFFF; font-weight: bold; font-size: 11px;
                    border: 1px solid #4CAF50; border-radius: 4px; padding: 6px;
                }
                QPushButton:hover { background-color: #4CAF50; }
            )");
        }
        if (m_playlistPrevBtn) m_playlistPrevBtn->setEnabled(false);
        if (m_playlistPauseBtn) {
            m_playlistPauseBtn->setEnabled(false);
            m_playlistPauseBtn->setText("⏸");
        }
        if (m_playlistNextBtn) m_playlistNextBtn->setEnabled(false);

        // Re-enable PGM Bar controls when Playlist stops
        if (m_pgmPlayPauseBtn) m_pgmPlayPauseBtn->setEnabled(true);
        if (m_pgmResetBtn) m_pgmResetBtn->setEnabled(true);
        if (m_pgmLoopBtn) m_pgmLoopBtn->setEnabled(true);
        if (m_pgmSeekSlider) m_pgmSeekSlider->setEnabled(true);

        statusBar()->showMessage("Manual transition executed. Exited Playlist Mode.");
    }
}

void MainWindow::onToggleGlobalPlaylist() {
    if (m_playlistController.isActive()) {
        stopGlobalPlaylistUI();
        statusBar()->showMessage("Exited Playlist Mode. Manual switching restored.");
    } else {
        if (m_playlistController.steps().empty()) {
            QMessageBox::warning(this, "Playlist Empty", "Please configure Playlist sequence first by clicking '📋 Config Playlist'.");
            onConfigGlobalPlaylist();
            return;
        }

        // Disable internal source looping for all inputs so playlist can advance on EOF
        for (const auto& slot : m_inputManager.inputSlots()) {
            if (slot.source) slot.source->setLoop(false);
        }

        m_playlistController.start();
        m_playlistToggleBtn->setText("⏹ EXIT PLAYLIST");
        m_playlistToggleBtn->setStyleSheet(R"(
            QPushButton {
                background-color: #D32F2F; color: #FFFFFF; font-weight: bold; font-size: 11px;
                border: 1px solid #F44336; border-radius: 4px; padding: 6px;
            }
            QPushButton:hover { background-color: #E53935; }
        )");

        if (m_playlistPrevBtn) m_playlistPrevBtn->setEnabled(true);
        if (m_playlistPauseBtn) {
            m_playlistPauseBtn->setEnabled(true);
            m_playlistPauseBtn->setText("⏸");
        }
        if (m_playlistNextBtn) m_playlistNextBtn->setEnabled(true);

        // Disable discrete PGM buttons during Playlist mode, BUT KEEP SEEK SLIDER ENABLED for video scrubbing
        if (m_pgmPlayPauseBtn) m_pgmPlayPauseBtn->setEnabled(false);
        if (m_pgmResetBtn) m_pgmResetBtn->setEnabled(false);
        if (m_pgmLoopBtn) m_pgmLoopBtn->setEnabled(false);
        if (m_pgmSeekSlider) m_pgmSeekSlider->setEnabled(true);

        auto step1 = m_playlistController.currentStep();
        if (step1.slotId > 0) {
            m_inputManager.setProgramSlot(step1.slotId);
            auto pgmSrc = m_inputManager.programSource();
            if (pgmSrc) {
                pgmSrc->seekToSeconds(0.0);
                pgmSrc->play();
            }
        }

        m_playlistController.resetStepTimer();
        statusBar()->showMessage("Running Global Broadcast Playlist Mode...");
    }
}

void MainWindow::onPauseGlobalPlaylist() {
    if (!m_playlistController.isActive()) return;

    if (m_playlistController.isPaused()) {
        m_playlistController.resume();
        auto pgmSrc = m_inputManager.programSource();
        if (pgmSrc) pgmSrc->play();
        m_playlistPauseBtn->setText("⏸");
        statusBar()->showMessage("Playlist Resumed.");
    } else {
        m_playlistController.pause();
        auto pgmSrc = m_inputManager.programSource();
        if (pgmSrc) pgmSrc->pause();
        m_playlistPauseBtn->setText("▶");
        statusBar()->showMessage("Playlist Paused.");
    }
}

void MainWindow::onPlaylistPrevClicked() {
    if (!m_playlistController.isActive()) return;
    m_playlistController.prevStep();
    advancePlaylistStep();
    statusBar()->showMessage("Playlist: Jumped to Previous Track.");
}

void MainWindow::onPlaylistNextClicked() {
    if (!m_playlistController.isActive()) return;
    m_playlistController.nextStepManual();
    advancePlaylistStep();
    statusBar()->showMessage("Playlist: Jumped to Next Track.");
}

void MainWindow::advancePlaylistStep() {
    auto currentStep = m_playlistController.currentStep();
    int targetSlotId = currentStep.slotId;
    if (targetSlotId <= 0) return;

    int currentPgmId = m_inputManager.programSlotId();
    if (targetSlotId == currentPgmId) return;

    auto currentPgmSource = m_inputManager.programSource();

    // Set target slot to PGM directly without altering PVW!
    m_inputManager.setProgramSlot(targetSlotId);

    auto newPgmSource = m_inputManager.programSource();
    if (newPgmSource) {
        newPgmSource->seekToSeconds(0.0);
        newPgmSource->play();
    }

    float duration = (currentStep.transitionType == "CUT") ? 1.0f : m_fadeDurationCombo->currentData().toFloat();
    if (duration <= 0.0f) duration = 500.0f;

    if (m_pgmWindow && newPgmSource) {
        if (currentPgmSource) {
            m_pgmWindow->renderer()->startTransition(currentPgmSource, newPgmSource, duration);
        }
    }

    if (m_ledOutputWindow && m_ledOutputWindow->directXWindow() && newPgmSource) {
        if (currentPgmSource) {
            m_ledOutputWindow->directXWindow()->renderer()->startTransition(currentPgmSource, newPgmSource, duration);
        }
    }

    if (currentPgmSource && currentPgmSource != newPgmSource) {
        currentPgmSource->pause();
    }

    m_playlistController.resetStepTimer();
}

void MainWindow::onConfigGlobalPlaylist() {
    GlobalPlaylistDialog dlg(m_inputManager, m_playlistController, this);
    dlg.exec();
}

void MainWindow::onQuickPlayClicked() {
    stopGlobalPlaylistUI();
    auto pvwSource = m_inputManager.previewSource();
    if (pvwSource) {
        pvwSource->play();
    }
    onCutClicked(true);
}

void MainWindow::onFTBClicked() {
    m_isFtbActive = !m_isFtbActive;
    // FTB: mute/unmute the active program source directly
    auto pgmSrc = m_inputManager.programSource();
    if (pgmSrc) {
        pgmSrc->setMuted(m_isFtbActive);
    }

    if (m_pgmWindow && m_pgmWindow->renderer()) {
        m_pgmWindow->renderer()->setFTB(m_isFtbActive, 500.0f);
    }
    if (m_ledOutputWindow && m_ledOutputWindow->directXWindow() && m_ledOutputWindow->directXWindow()->renderer()) {
        m_ledOutputWindow->directXWindow()->renderer()->setFTB(m_isFtbActive, 500.0f);
    }

    if (m_isFtbActive) {
        m_ftbBtn->setStyleSheet(R"(
            QPushButton {
                background-color: #B71C1C;
                color: #FFFF00;
                font-weight: bold;
                font-size: 14px;
                border: 2px solid #FFEB3B;
                border-radius: 6px;
            }
        )");
        m_ftbBtn->setText("FTB [ON]");
        statusBar()->showMessage("FTB (Fade To Black) ACTIVATED - Output is BLACK");
    } else {
        m_ftbBtn->setStyleSheet(R"(
            QPushButton {
                background-color: #212121;
                color: #E53935;
                font-weight: bold;
                font-size: 14px;
                border: 2px solid #B71C1C;
                border-radius: 6px;
            }
            QPushButton:hover {
                background-color: #B71C1C;
                color: #FFFFFF;
            }
        )");
        m_ftbBtn->setText("FTB");
        statusBar()->showMessage("FTB Deactivated - Output Restored");
    }
}

void MainWindow::onTBarSliderMoved(int value) {
    if (value > 0) {
        stopGlobalPlaylistUI();
    }

    float progress = static_cast<float>(value) / 1000.0f;
    auto pvwSource = m_inputManager.previewSource();
    auto pgmSource = m_inputManager.programSource();

    if (pvwSource && pgmSource) {
        if (m_pgmWindow && m_pgmWindow->renderer()) {
            m_pgmWindow->renderer()->setManualTransition(pgmSource, pvwSource, progress);
        }
        if (m_ledOutputWindow && m_ledOutputWindow->directXWindow() && m_ledOutputWindow->directXWindow()->renderer()) {
            m_ledOutputWindow->directXWindow()->renderer()->setManualTransition(pgmSource, pvwSource, progress);
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (!event) return;

    int key = event->key();
    Qt::KeyboardModifiers modifiers = event->modifiers();

    // 1. Spacebar = Quick Play
    if (key == Qt::Key_Space) {
        onQuickPlayClicked();
        event->accept();
        return;
    }

    // 2. Enter / Return = CUT
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        onCutClicked();
        event->accept();
        return;
    }

    // 3. F1 or F5 = FADE
    if (key == Qt::Key_F1 || key == Qt::Key_F5) {
        onFadeClicked();
        event->accept();
        return;
    }

    // 4. F12 = FTB
    if (key == Qt::Key_F12) {
        onFTBClicked();
        event->accept();
        return;
    }

    // 5. Number Keys 1..9
    if (key >= Qt::Key_1 && key <= Qt::Key_9) {
        int channelNum = key - Qt::Key_1 + 1; // 1 to 9
        const auto& slotList = m_inputManager.inputSlots();
        if (channelNum <= static_cast<int>(slotList.size())) {
            int slotId = slotList[channelNum - 1].id;
            if (modifiers & Qt::ShiftModifier) {
                // Shift + Number -> Send directly LIVE to PGM
                m_inputManager.setPreviewSlot(slotId);
                onCutClicked();
            } else {
                // Number -> Set to PREVIEW
                m_inputManager.setPreviewSlot(slotId);
            }
        }
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

void MainWindow::onCutClicked(bool isManualUserAction) {
    if (isManualUserAction) {
        stopGlobalPlaylistUI();
    }

    int pvwId = m_inputManager.previewSlotId();
    int pgmId = m_inputManager.programSlotId();

    if (pvwId <= 0) return;
    if (pvwId == pgmId) return;

    LOG_INFO("CUT triggered: PVW #{} <-> PGM #{}", pvwId, pgmId);

    auto pvwSource = m_inputManager.previewSource();
    auto pgmSource = m_inputManager.programSource();

    // Auto-play the Preview source from its current frame when transitioning to LIVE (PGM)
    if (pvwSource) {
        pvwSource->play();
    }

    // Auto-pause the former LIVE (PGM) source when it transitions back to Preview (PVW)
    if (pgmSource && pgmSource != pvwSource) {
        pgmSource->pause();
    }

    if (m_pgmWindow && pvwSource) {
        if (pgmSource) {
            m_pgmWindow->renderer()->startTransition(pgmSource, pvwSource, 1.0f);
        }
    }

    if (m_ledOutputWindow && m_ledOutputWindow->directXWindow() && pvwSource) {
        if (pgmSource) {
            m_ledOutputWindow->directXWindow()->renderer()->startTransition(pgmSource, pvwSource, 1.0f);
        }
    }

    if (pgmId <= 0) {
        m_inputManager.setProgramSlot(pvwId);
    } else {
        m_inputManager.swapPreviewAndProgram();
    }

    statusBar()->showMessage(QString("CUT Switch: Input #%1 is now LIVE").arg(pvwId));
}

void MainWindow::onFadeClicked(bool isManualUserAction) {
    if (isManualUserAction) {
        stopGlobalPlaylistUI();
    }

    int pvwId = m_inputManager.previewSlotId();
    int pgmId = m_inputManager.programSlotId();

    if (pvwId <= 0) return;
    if (pvwId == pgmId) return;

    float duration = m_fadeDurationCombo->currentData().toFloat();
    if (duration <= 0) duration = 500.0f;

    LOG_INFO("FADE triggered ({:.0f} ms): PVW #{} -> PGM #{}", duration, pvwId, pgmId);

    auto pvwSource = m_inputManager.previewSource();
    auto pgmSource = m_inputManager.programSource();

    // Auto-play the Preview source from its current frame when transitioning to LIVE (PGM)
    if (pvwSource) {
        pvwSource->play();
    }

    // Auto-pause the former LIVE (PGM) source when it transitions back to Preview (PVW)
    if (pgmSource && pgmSource != pvwSource) {
        pgmSource->pause();
    }

    if (m_pgmWindow && pvwSource) {
        if (pgmSource) {
            m_pgmWindow->renderer()->startTransition(pgmSource, pvwSource, duration);
        }
    }

    if (m_ledOutputWindow && m_ledOutputWindow->directXWindow() && pvwSource) {
        if (pgmSource) {
            m_ledOutputWindow->directXWindow()->renderer()->startTransition(pgmSource, pvwSource, duration);
        }
    }

    if (pgmId <= 0) {
        m_inputManager.setProgramSlot(pvwId);
    } else {
        m_inputManager.setProgramSlot(pvwId);
        m_inputManager.setPreviewSlot(pgmId);
    }

    statusBar()->showMessage(QString("FADE Switch (%1 ms): Input #%2 is now LIVE").arg(duration).arg(pvwId));
}

void MainWindow::onPvwPlayPauseClicked() {
    auto source = m_inputManager.previewSource();
    if (!source) return;

    if (source->isPlaying()) {
        source->pause();
    } else {
        source->play();
    }
}

void MainWindow::onPvwResetClicked() {
    auto source = m_inputManager.previewSource();
    if (source) {
        source->seekToSeconds(0.0);
        source->pause();
    }
}

void MainWindow::onPvwLoopToggleClicked() {
    auto source = m_inputManager.previewSource();
    if (!source) return;

    bool currentLoop = source->isLoop();
    source->setLoop(!currentLoop);
}

void MainWindow::onPvwSeekSliderSliderMoved(int value) {
    auto source = m_inputManager.previewSource();
    if (!source) return;

    double duration = source->durationSeconds();
    if (duration > 0.0) {
        double targetSec = (static_cast<double>(value) / 1000.0) * duration;
        source->seekToSeconds(targetSec);
    }
}

static QString formatTimeString(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    int totalSec = static_cast<int>(seconds);
    int hrs = totalSec / 3600;
    int mins = (totalSec % 3600) / 60;
    int secs = totalSec % 60;

    if (hrs > 0) {
        return QString("%1:%2:%3")
            .arg(hrs, 2, 10, QChar('0'))
            .arg(mins, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    } else {
        return QString("%1:%2")
            .arg(mins, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    }
}

void MainWindow::onPgmPlayPauseClicked() {
    auto source = m_inputManager.programSource();
    if (!source) return;

    if (source->isPlaying()) {
        source->pause();
    } else {
        source->play();
    }
}

void MainWindow::onPgmResetClicked() {
    auto source = m_inputManager.programSource();
    if (source) {
        source->seekToSeconds(0.0);
        source->pause();
    }
}

void MainWindow::onPgmLoopToggleClicked() {
    auto source = m_inputManager.programSource();
    if (!source) return;

    bool currentLoop = source->isLoop();
    source->setLoop(!currentLoop);
}

void MainWindow::onPgmSeekSliderSliderMoved(int value) {
    auto source = m_inputManager.programSource();
    if (!source) return;

    double duration = source->durationSeconds();
    if (duration > 0.0) {
        double targetSec = (static_cast<double>(value) / 1000.0) * duration;
        source->seekToSeconds(targetSec);
    }
}

void MainWindow::updatePlaybackStatus() {
    // 0. Check Global Broadcast Playlist Advancement (Timer Loop Tick)
    if (m_playlistController.isActive() && !m_playlistController.isPaused()) {
        auto pgmSrc = m_inputManager.programSource();
        double pgmPos = pgmSrc ? pgmSrc->positionSeconds() : 0.0;
        double pgmDur = pgmSrc ? pgmSrc->durationSeconds() : 0.0;
        bool pgmIsPlaying = pgmSrc ? pgmSrc->isPlaying() : false;
        int pgmId = m_inputManager.programSlotId();

        if (m_playlistController.checkAdvance(pgmPos, pgmDur, pgmId, pgmIsPlaying)) {
            advancePlaylistStep();
        }
    }

    // 1. PREVIEW (PVW) Status
    auto pvwSource = m_inputManager.previewSource();
    if (!pvwSource || pvwSource->durationSeconds() <= 0.0) {
        m_pvwTimeLabel->setText("00:00:00 / 00:00:00");
        m_pvwSeekSlider->setValue(0);
    } else {
        double pos = pvwSource->positionSeconds();
        double dur = pvwSource->durationSeconds();

        m_pvwTimeLabel->setText(QString("%1 / %2").arg(formatTimeString(pos)).arg(formatTimeString(dur)));

        if (!m_isUserSeeking && dur > 0.0) {
            int sliderVal = static_cast<int>((pos / dur) * 1000.0);
            m_pvwSeekSlider->setValue(std::clamp(sliderVal, 0, 1000));
        }

        if (pvwSource->isPlaying()) {
            m_pvwPlayPauseBtn->setText("⏸");
            m_pvwPlayPauseBtn->setStyleSheet(R"(
                QPushButton {
                    background-color: #1976D2;
                    color: #FFFFFF;
                    font-weight: bold;
                    font-size: 11px;
                    border: none;
                    border-radius: 3px;
                }
                QPushButton:hover { background-color: #2196F3; }
            )");
        } else {
            m_pvwPlayPauseBtn->setText("▶");
            m_pvwPlayPauseBtn->setStyleSheet(R"(
                QPushButton {
                    background-color: #2B2D3A;
                    color: #FFFFFF;
                    font-weight: bold;
                    font-size: 11px;
                    border: 1px solid #3E4154;
                    border-radius: 3px;
                }
                QPushButton:hover { background-color: #00ACC1; }
            )");
        }

        if (pvwSource->isLoop()) {
            m_pvwLoopBtn->setText("Loop");
            m_pvwLoopBtn->setStyleSheet(R"(
                QPushButton {
                    background-color: #388E3C;
                    color: #FFFFFF;
                    font-weight: bold;
                    font-size: 10px;
                    border: none;
                    border-radius: 3px;
                }
                QPushButton:hover { background-color: #4CAF50; }
            )");
        } else {
            m_pvwLoopBtn->setText("Loop");
            m_pvwLoopBtn->setStyleSheet(R"(
                QPushButton {
                    background-color: #2B2D3A;
                    color: #888888;
                    font-weight: bold;
                    font-size: 10px;
                    border: 1px solid #3E4154;
                    border-radius: 3px;
                }
                QPushButton:hover { background-color: #3E4154; color: #FFFFFF; }
            )");
        }
    }

    // 2. PROGRAM (LIVE / PGM) Status
    auto pgmSource = m_inputManager.programSource();
    if (!pgmSource || pgmSource->durationSeconds() <= 0.0) {
        m_pgmTimeLabel->setText("00:00:00 / 00:00:00");
        m_pgmSeekSlider->setValue(0);
    } else {
        double pos = pgmSource->positionSeconds();
        double dur = pgmSource->durationSeconds();

        m_pgmTimeLabel->setText(QString("%1 / %2").arg(formatTimeString(pos)).arg(formatTimeString(dur)));

        if (!m_isPgmUserSeeking && dur > 0.0) {
            int sliderVal = static_cast<int>((pos / dur) * 1000.0);
            m_pgmSeekSlider->setValue(std::clamp(sliderVal, 0, 1000));
        }

        if (pgmSource->isPlaying()) {
            m_pgmPlayPauseBtn->setText("⏸");
            m_pgmPlayPauseBtn->setStyleSheet(R"(
                QPushButton {
                    background-color: #E53935;
                    color: #FFFFFF;
                    font-weight: bold;
                    font-size: 11px;
                    border: none;
                    border-radius: 3px;
                }
                QPushButton:hover { background-color: #EF5350; }
            )");
        } else {
            m_pgmPlayPauseBtn->setText("▶");
            m_pgmPlayPauseBtn->setStyleSheet(R"(
                QPushButton {
                    background-color: #2B2D3A;
                    color: #FFFFFF;
                    font-weight: bold;
                    font-size: 11px;
                    border: 1px solid #3E4154;
                    border-radius: 3px;
                }
                QPushButton:hover { background-color: #E53935; }
            )");
        }

        if (pgmSource->isLoop()) {
            m_pgmLoopBtn->setText("Loop");
            m_pgmLoopBtn->setStyleSheet(R"(
                QPushButton {
                    background-color: #388E3C;
                    color: #FFFFFF;
                    font-weight: bold;
                    font-size: 10px;
                    border: none;
                    border-radius: 3px;
                }
                QPushButton:hover { background-color: #4CAF50; }
            )");
        } else {
            m_pgmLoopBtn->setText("Loop");
            m_pgmLoopBtn->setStyleSheet(R"(
                QPushButton {
                    background-color: #2B2D3A;
                    color: #888888;
                    font-weight: bold;
                    font-size: 10px;
                    border: 1px solid #3E4154;
                    border-radius: 3px;
                }
                QPushButton:hover { background-color: #3E4154; color: #FFFFFF; }
            )");
        }
    }

    // Audio pumping is handled by the dedicated m_audioPumpThread (audioPumpLoop),
    // NOT here, so that audio delivery is never blocked by Qt UI event processing.
}



void MainWindow::onMasterVolumeChanged(int value) {
    float vol = static_cast<float>(value) / 100.0f;
    // Route master volume through AudioEngine (XAudio2)
    AudioEngine::instance().setVolume(vol);
    if (m_volumeLabel) {
        m_volumeLabel->setText(QString("%1%").arg(value));
    }
}

void MainWindow::onMuteToggled() {
    bool isMuted = AudioEngine::instance().isMuted();
    bool newMuted = !isMuted;
    AudioEngine::instance().setMuted(newMuted);
    if (m_muteBtn) {
        m_muteBtn->setText(newMuted ? "🔇" : "🔊");
        m_muteBtn->setStyleSheet(newMuted ? R"(
            QPushButton {
                background-color: #D32F2F; color: #FFFFFF; font-weight: bold; font-size: 11px;
                border: 1px solid #E53935; border-radius: 3px;
            }
            QPushButton:hover { background-color: #E53935; }
        )" : R"(
            QPushButton {
                background-color: #388E3C; color: #FFFFFF; font-weight: bold; font-size: 11px;
                border: 1px solid #4CAF50; border-radius: 3px;
            }
            QPushButton:hover { background-color: #4CAF50; }
        )");
    }
}

void MainWindow::onShowAboutDialog() {
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::activatePgmAudio() {
    // Deactivate audio on the previous PGM source
    if (m_pgmAudioSource) {
        m_pgmAudioSource->setAudioActive(false);
        m_pgmAudioSource = nullptr;
    }

    // Activate audio on the new PGM source
    auto pgmSrc = m_inputManager.programSource();
    if (pgmSrc) {
        // Clear stale audio from the previous source
        AudioEngine::instance().clearAudioBuffer();
        AudioEngine::instance().resetAudioPts(pgmSrc->positionSeconds());
        pgmSrc->setAudioActive(true);
        m_pgmAudioSource = pgmSrc;
        LOG_INFO("MainWindow: Audio routed to PGM source pos={:.2f}s", pgmSrc->positionSeconds());
    }
}
