#pragma once

#include <QMainWindow>
#include <memory>

class DirectXWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void setupUi();

    DirectXWindow* m_directXWindow;
};
