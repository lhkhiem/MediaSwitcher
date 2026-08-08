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

#include <QCloseEvent>

#include <QSlider>
#include <QTimer>

class DirectXWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onAddVideoInput();
    void onAddColorBarsInput();
    void onQuickPlayClicked();
    void onCutClicked();
    void onFadeClicked();
    void onToggleFullscreenLED();
    void rebuildInputDock();
    void updateViewports();
    void onPvwPlayPauseClicked();
    void onPvwResetClicked();
    void onPvwLoopToggleClicked();
    void onPvwSeekSliderSliderMoved(int value);
    void onPgmPlayPauseClicked();
    void onPgmResetClicked();
    void onPgmLoopToggleClicked();
    void onPgmSeekSliderSliderMoved(int value);
    void updatePlaybackStatus();

private:
    void setupUi();
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

    QPushButton* m_pvwPlayPauseBtn{nullptr};
    QPushButton* m_pvwResetBtn{nullptr};
    QPushButton* m_pvwLoopBtn{nullptr};
    QSlider* m_pvwSeekSlider{nullptr};
    QLabel* m_pvwTimeLabel{nullptr};
    bool m_isUserSeeking{false};

    QPushButton* m_pgmPlayPauseBtn{nullptr};
    QPushButton* m_pgmResetBtn{nullptr};
    QPushButton* m_pgmLoopBtn{nullptr};
    QSlider* m_pgmSeekSlider{nullptr};
    QLabel* m_pgmTimeLabel{nullptr};
    bool m_isPgmUserSeeking{false};

    QTimer* m_playbackTimer{nullptr};
};
