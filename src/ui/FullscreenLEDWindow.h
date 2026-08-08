#pragma once

#include <QWidget>
#include <QScreen>
#include <QKeyEvent>
#include <QCloseEvent>
#include "engine/renderer/DirectXWindow.h"
#include "engine/input/IMediaSource.h"
#include <memory>

class FullscreenLEDWindow : public QWidget {
    Q_OBJECT

public:
    explicit FullscreenLEDWindow(QWidget *parent = nullptr);
    ~FullscreenLEDWindow() override;

    bool initOutput(QScreen* screen);
    void setMediaSource(std::shared_ptr<IMediaSource> source);
    DirectXWindow* directXWindow() const { return m_directXWindow; }

signals:
    void windowClosed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    DirectXWindow* m_directXWindow{nullptr};
};
