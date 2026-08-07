#include "MainWindow.h"
#include "engine/renderer/DirectXWindow.h"
#include "engine/input/ColorBarsSource.h"
#include "engine/input/FileSource.h"
#include "common/logger/Logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    LOG_INFO("MainWindow created.");
    setupUi();
}

MainWindow::~MainWindow() {
    LOG_INFO("MainWindow destroyed.");
}

void MainWindow::setupUi() {
    this->setWindowTitle("MediaSwitcher - Professional Media Engine");
    this->resize(1280, 760);

    // Apply dark style sheet
    this->setStyleSheet(R"(
        QMainWindow {
            background-color: #121214;
        }
        QToolBar {
            background-color: #1e1e24;
            border-bottom: 1px solid #2d2d35;
            spacing: 8px;
            padding: 4px;
        }
        QToolButton {
            color: #e0e0e0;
            background-color: #2b2b36;
            border: 1px solid #3d3d4d;
            border-radius: 4px;
            padding: 6px 12px;
            font-weight: bold;
        }
        QToolButton:hover {
            background-color: #007acc;
            border-color: #0099ff;
            color: #ffffff;
        }
        QStatusBar {
            background-color: #1e1e24;
            color: #a0a0a0;
            border-top: 1px solid #2d2d35;
        }
    )");

    // Toolbar setup
    QToolBar* toolbar = addToolBar("Media Controls");
    toolbar->setMovable(false);

    QAction* openAction = toolbar->addAction("📂 Open Video File...");
    connect(openAction, &QAction::triggered, this, &MainWindow::openVideoFile);

    QAction* testBarsAction = toolbar->addAction("🎨 Test Pattern (Color Bars)");
    connect(testBarsAction, &QAction::triggered, this, &MainWindow::switchColorBars);

    toolbar->addSeparator();

    QAction* playPauseAction = toolbar->addAction("⏯ Play / Pause");
    connect(playPauseAction, &QAction::triggered, this, &MainWindow::togglePlayPause);

    // Central layout
    QWidget* centralWidget = new QWidget(this);
    this->setCentralWidget(centralWidget);

    QVBoxLayout* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);

    // Add DirectX Rendering Window
    m_directXWindow = new DirectXWindow(centralWidget);
    layout->addWidget(m_directXWindow);

    // Initialize DirectX 11 surface
    if (!m_directXWindow->initDirectX()) {
        LOG_ERROR("Failed to initialize DirectX 11 surface.");
    }

    statusBar()->showMessage("MediaSwitcher ready. Select a video file or test pattern source.");
}

void MainWindow::openVideoFile() {
    QString filter = "Video Files (*.mp4 *.mkv *.mov *.avi *.flv *.wmv *.webm);;All Files (*.*)";
    QString filePath = QFileDialog::getOpenFileName(this, "Select Video File", "", filter);

    if (filePath.isEmpty()) return;

    LOG_INFO("Selected video file: {}", filePath.toStdString());

    auto fileSource = std::make_shared<FileSource>(filePath.toStdString());
    m_currentSource = fileSource;
    m_directXWindow->setMediaSource(m_currentSource);

    statusBar()->showMessage(QString("Playing Video: %1").arg(filePath));
}

void MainWindow::switchColorBars() {
    LOG_INFO("Switching to ColorBars test pattern.");
    m_currentSource = std::make_shared<ColorBarsSource>(1280, 720);
    m_directXWindow->setMediaSource(m_currentSource);

    statusBar()->showMessage("Source: SMPTE Color Bars (60 FPS Test Pattern)");
}

void MainWindow::togglePlayPause() {
    auto fileSource = std::dynamic_pointer_cast<FileSource>(m_currentSource);
    if (fileSource) {
        if (fileSource->isPlaying()) {
            fileSource->pause();
            statusBar()->showMessage("Video Paused.");
        } else {
            fileSource->play();
            statusBar()->showMessage("Video Playing.");
        }
    }
}
