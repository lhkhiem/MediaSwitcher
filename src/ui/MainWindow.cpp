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

    QAction* addVideoAction = toolbar->addAction("📂 + Add Video Input");
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
    pvwLayout->addWidget(m_pvwWindow);
    m_pvwWindow->initDirectX();

    topLayout->addWidget(pvwGroup, 5);

    // 2. CENTER TRANSITION CONTROL PANEL
    QVBoxLayout* centerControlLayout = new QVBoxLayout();
    centerControlLayout->setAlignment(Qt::AlignCenter);
    centerControlLayout->setSpacing(12);

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
    pgmLayout->addWidget(m_pgmWindow);
    m_pgmWindow->initDirectX();

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
    QString filter = "Video Files (*.mp4 *.mkv *.mov *.avi *.flv *.wmv *.webm);;All Files (*.*)";
    QString filePath = QFileDialog::getOpenFileName(this, "Select Video File", "", filter);

    if (filePath.isEmpty()) return;

    std::string utf8Path = filePath.toUtf8().toStdString();
    int slotId = m_inputManager.addFileSlot(utf8Path);
    if (slotId > 0) {
        m_inputManager.setPreviewSlot(slotId);
        statusBar()->showMessage(QString("Added Video Input #%1: %2").arg(slotId).arg(filePath));
    }
}

void MainWindow::onAddColorBarsInput() {
    int slotId = m_inputManager.addColorBarsSlot();
    if (slotId > 0) {
        m_inputManager.setPreviewSlot(slotId);
        statusBar()->showMessage(QString("Added Color Bars Input #%1").arg(slotId));
    }
}

void MainWindow::onCutClicked() {
    int pvwId = m_inputManager.previewSlotId();
    int pgmId = m_inputManager.programSlotId();

    if (pvwId <= 0) return;
    if (pvwId == pgmId) return;

    LOG_INFO("CUT triggered: PVW #{} <-> PGM #{}", pvwId, pgmId);

    auto pvwSource = m_inputManager.previewSource();
    auto pgmSource = m_inputManager.programSource();

    if (m_pgmWindow && pgmSource && pvwSource) {
        m_pgmWindow->renderer()->startTransition(pgmSource, pvwSource, 1.0f);
    }

    if (m_ledOutputWindow && m_ledOutputWindow->directXWindow() && pgmSource && pvwSource) {
        m_ledOutputWindow->directXWindow()->renderer()->startTransition(pgmSource, pvwSource, 1.0f);
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

    if (m_pgmWindow && pgmSource && pvwSource) {
        m_pgmWindow->renderer()->startTransition(pgmSource, pvwSource, duration);
    }

    if (m_ledOutputWindow && m_ledOutputWindow->directXWindow() && pgmSource && pvwSource) {
        m_ledOutputWindow->directXWindow()->renderer()->startTransition(pgmSource, pvwSource, duration);
    }

    if (pgmId <= 0) {
        m_inputManager.setProgramSlot(pvwId);
    } else {
        m_inputManager.setProgramSlot(pvwId);
        m_inputManager.setPreviewSlot(pgmId);
    }

    statusBar()->showMessage(QString("FADE Switch (%1 ms): Input #%2 is now LIVE").arg(duration).arg(pvwId));
}
