#pragma once

#include <QMainWindow>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QPalette>
#include <QColor>
#include "engine/input/InputManager.h"
#include "engine/input/GlobalPlaylistController.h"
#include "FullscreenLEDWindow.h"
#include "InputSlotWidget.h"
#include "AudioMeterWidget.h"
#include <memory>

#include <QCloseEvent>
#include <QSlider>
#include <QTimer>
#include <QLineEdit>
#include "engine/input/IMediaSource.h"
#include <thread>
#include <atomic>

class DirectXWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onAddVideoInput();
    void onAddPlaylistInput();
    void onQuickPlayClicked();
    void onTakeClicked();
    void onCutClicked(bool isManualUserAction = true);
    void onFadeClicked(bool isManualUserAction = true);
    void stopGlobalPlaylistUI();
    void onFTBClicked();
    void onTBarSliderMoved(int value);
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
    void updateProcessMetrics();
    void onCategoryFilterClicked(const QString& category);
    void onSearchTextChanged(const QString& text);

    void onToggleGlobalPlaylist();
    void onPauseGlobalPlaylist();
    void onPlaylistPrevClicked();
    void onPlaylistNextClicked();
    void onConfigGlobalPlaylist();

    void onMasterVolumeChanged(int value);
    void onMuteToggled();

    void onShowAboutDialog();
    void activatePgmAudio();

private:
    void setupUi();
    void populateScreenSelector();
    void activatePlaylistCurrentStep();

    DirectXWindow* m_pvwWindow{nullptr};
    DirectXWindow* m_pgmWindow{nullptr};
    FullscreenLEDWindow* m_ledOutputWindow{nullptr};

    InputManager m_inputManager;
    GlobalPlaylistController m_playlistController;

    QWidget* m_dockContainer{nullptr};
    QHBoxLayout* m_dockLayout{nullptr};
    QComboBox* m_fadeDurationCombo{nullptr};
    QComboBox* m_screenSelectorCombo{nullptr};
    QPushButton* m_fullscreenToggleBtn{nullptr};

    QGroupBox* m_pvwGroup{nullptr};
    QGroupBox* m_pgmGroup{nullptr};
    QPushButton* m_takeBtn{nullptr};
    QPushButton* m_ftbBtn{nullptr};
    QSlider* m_tbarSlider{nullptr};
    bool m_isFtbActive{false};

    AudioMeterWidget* m_audioMeterWidget{nullptr};
    QSlider* m_masterVolumeSlider{nullptr};
    QPushButton* m_muteBtn{nullptr};
    QLabel* m_volumeLabel{nullptr};

    QPushButton* m_playlistToggleBtn{nullptr};
    QPushButton* m_playlistPrevBtn{nullptr};
    QPushButton* m_playlistPauseBtn{nullptr};
    QPushButton* m_playlistNextBtn{nullptr};
    QPushButton* m_playlistConfigBtn{nullptr};

    enum class GridRowsMode { OneRow, TwoRows, ThreeRows, AutoGrid };
    enum class ThumbnailSize { Small, Normal, Large };

    GridRowsMode m_rowsMode{GridRowsMode::AutoGrid};
    ThumbnailSize m_thumbSize{ThumbnailSize::Normal};

    QString m_activeCategory{"ALL"};
    QString m_searchQuery{""};
    QLineEdit* m_searchInput{nullptr};

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
    QTimer* m_processMetricsTimer{nullptr};

    // Debug Resource Metrics Panel Label
    QLabel* m_metricsLabel{nullptr};

    // Tracks which source is currently outputting audio to AudioEngine
    std::shared_ptr<IMediaSource> m_pgmAudioSource;
};
