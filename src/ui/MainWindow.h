#pragma once

#include <QMainWindow>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QScrollArea>
#include "engine/input/InputManager.h"
#include "FullscreenLEDWindow.h"
#include "InputSlotWidget.h"
#include <memory>

class DirectXWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onAddVideoInput();
    void onAddColorBarsInput();
    void onCutClicked();
    void onFadeClicked();
    void onToggleFullscreenLED();
    void rebuildInputDock();

private:
    void setupUi();
    void updateViewports();
    void populateScreenSelector();

    DirectXWindow* m_pvwWindow{nullptr};
    DirectXWindow* m_pgmWindow{nullptr};
    FullscreenLEDWindow* m_ledOutputWindow{nullptr};

    InputManager m_inputManager;

    QWidget* m_dockContainer{nullptr};
    QHBoxLayout* m_dockLayout{nullptr};
    QComboBox* m_fadeDurationCombo{nullptr};
    QComboBox* m_screenSelectorCombo{nullptr};
    QPushButton* m_fullscreenToggleBtn{nullptr};
};
