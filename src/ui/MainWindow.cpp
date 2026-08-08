#include "MainWindow.h"
#include "engine/renderer/DirectXWindow.h"
#include "common/logger/Logger.h"

#include <QToolBar>
#include <QFileDialog>
#include <QStatusBar>
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
    this->setWindowTitle("MediaSwitcher - LED Screen Broadcast & Media Engine (vMix Grade)");
    this->resize(1366, 768);

    // Dark Studio QSS Style
    this->setStyleSheet(R"(
        QMainWindow {
            background-color: #121318;
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

    // Toolbar
    QToolBar* toolbar = addToolBar("Main Toolbar");
    toolbar->setMovable(false);

    QAction* addVideoAction = toolbar->addAction("📂 + Add Media Input(s)");
    connect(addVideoAction, &QAction::triggered, this, &MainWindow::onAddVideoInput);

    QAction* addColorBarsAction = toolbar->addAction("🎨 + Add Color Bars");
    connect(addColorBarsAction, &QAction::triggered, this, &MainWindow::onAddColorBarsInput);

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

    // Broadcast Ultra-Compact vMix Control Bar for PREVIEW
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

    // Row 2: Sleek vMix-style Timeline Slider
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
    centerControlLayout->setSpacing(10);

    QPushButton* quickPlayBtn = new QPushButton("Quick Play", centralWidget);
    quickPlayBtn->setFixedSize(90, 42);
    quickPlayBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #00ACC1;
            color: #FFFFFF;
            font-weight: bold;
            font-size: 14px;
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
    cutBtn->setFixedSize(90, 48);
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
    fadeBtn->setFixedSize(90, 48);
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

    QLabel* durationLabel = new QLabel("Duration:", centralWidget);
    durationLabel->setStyleSheet("color: #AAAAAA; font-size: 11px; font-weight: bold;");
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

    topLayout->addLayout(centerControlLayout, 1);

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

    // Broadcast Ultra-Compact vMix Control Bar for PROGRAM (PGM) - LIVE
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

    // Row 2: Sleek vMix-style Timeline Slider (Red PGM Track)
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
    QGroupBox* dockGroup = new QGroupBox("INPUT CHANNELS (vMix Grid)", centralWidget);
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

    QScrollArea* scrollArea = new QScrollArea(dockGroup);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    m_dockContainer = new QWidget(scrollArea);
    m_dockLayout = new QHBoxLayout(m_dockContainer);
    m_dockLayout->setContentsMargins(0, 0, 0, 0);
    m_dockLayout->setSpacing(8);
    m_dockLayout->setAlignment(Qt::AlignLeft);

    scrollArea->setWidget(m_dockContainer);
    dockGroupLayout->addWidget(scrollArea);

    mainLayout->addWidget(dockGroup, 4);

    statusBar()->showMessage("MediaSwitcher LED Engine Ready. Dual Viewports Live.");
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
    if (!m_dockLayout) return;

    QLayoutItem* item;
    while ((item = m_dockLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    const auto& slotList = m_inputManager.inputSlots();
    int pvwId = m_inputManager.previewSlotId();
    int pgmId = m_inputManager.programSlotId();

    for (const auto& slot : slotList) {
        bool isPvw = (slot.id == pvwId);
        bool isPgm = (slot.id == pgmId);

        InputSlotWidget* slotWidget = new InputSlotWidget(slot, isPvw, isPgm, m_dockContainer);
        connect(slotWidget, &InputSlotWidget::clicked, this, [this](int slotId) {
            m_inputManager.setPreviewSlot(slotId);
        });

        m_dockLayout->addWidget(slotWidget);
    }
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

void MainWindow::onAddColorBarsInput() {
    int slotId = m_inputManager.addColorBarsSlot();
    if (slotId > 0) {
        m_inputManager.setPreviewSlot(slotId);
        statusBar()->showMessage(QString("Added Color Bars Input #%1").arg(slotId));
    }
}

void MainWindow::onQuickPlayClicked() {
    auto pvwSource = m_inputManager.previewSource();
    if (pvwSource) {
        pvwSource->play();
    }
    onCutClicked();
}

void MainWindow::onCutClicked() {
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

void MainWindow::onFadeClicked() {
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
}
