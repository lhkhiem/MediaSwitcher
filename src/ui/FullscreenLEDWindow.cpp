#include "FullscreenLEDWindow.h"
#include "common/logger/Logger.h"
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QWindow>

FullscreenLEDWindow::FullscreenLEDWindow(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_QuitOnClose, false);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_directXWindow = new DirectXWindow(this);
    layout->addWidget(m_directXWindow);
}

FullscreenLEDWindow::~FullscreenLEDWindow() {
    LOG_INFO("FullscreenLEDWindow destroyed.");
}

bool FullscreenLEDWindow::initOutput(QScreen* screen) {
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    if (!m_directXWindow->initDirectX()) {
        LOG_ERROR("FullscreenLEDWindow: Failed to initialize DirectX 11.");
        return false;
    }

    this->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    this->create(); // Force native window creation
    
    if (this->windowHandle()) {
        this->windowHandle()->setScreen(screen);
    }

    QRect rect = screen->geometry();
    this->setGeometry(rect);
    this->move(rect.topLeft());
    this->showNormal();
    this->showFullScreen();
    this->raise();
    this->activateWindow();

    LOG_INFO("FullscreenLEDWindow: Live LED Output pinned to screen '{}' ({}x{} @ offset {},{}).",
             screen->name().toStdString(),
             rect.width(), rect.height(),
             rect.x(), rect.y());

    return true;
}

void FullscreenLEDWindow::setMediaSource(std::shared_ptr<IMediaSource> source) {
    if (m_directXWindow) {
        m_directXWindow->setMediaSource(source);
    }
}

void FullscreenLEDWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        this->close();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void FullscreenLEDWindow::closeEvent(QCloseEvent *event) {
    emit windowClosed();
    QWidget::closeEvent(event);
}
