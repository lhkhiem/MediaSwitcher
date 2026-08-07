#include "MainWindow.h"
#include "engine/renderer/DirectXWindow.h"
#include "common/logger/Logger.h"

#include <QVBoxLayout>
#include <QWidget>

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
    this->setWindowTitle("MediaSwitcher");
    this->resize(1280, 720);

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
}
