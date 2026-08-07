#pragma once

#include <QMainWindow>
#include "engine/input/IMediaSource.h"
#include <memory>

class DirectXWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void openVideoFile();
    void switchColorBars();
    void togglePlayPause();

private:
    void setupUi();

    DirectXWindow* m_directXWindow{nullptr};
    std::shared_ptr<IMediaSource> m_currentSource;
};
