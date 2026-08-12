#pragma once

#include <QWidget>

#include <atomic>
#include <filesystem>
#include <memory>

class QLabel;
class QComboBox;
class QCheckBox;
class QPushButton;
class QSlider;
class QTimer;
class ObsContext;
class ObsPlaybackBackend;
class ObsPlaylist;
class QCloseEvent;
class QResizeEvent;
class QShowEvent;
class QKeyEvent;
class QEvent;
struct obs_display;
struct obs_source;
typedef struct obs_display obs_display_t;
typedef struct obs_source obs_source_t;

class ObsDualMediaTestWindow final : public QWidget {
    Q_OBJECT
public:
    ObsDualMediaTestWindow(ObsContext& context, const std::filesystem::path& mediaPath, QWidget* parent = nullptr);
    ~ObsDualMediaTestWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Panel {
        std::unique_ptr<ObsPlaybackBackend> backend;
        std::unique_ptr<ObsPlaybackBackend> fadeOutgoing;
        QWidget* videoSurface{nullptr};
        QLabel* statusLabel{nullptr};
        QSlider* seekSlider{nullptr};
        QPushButton* playPauseButton{nullptr};
        QPushButton* loopButton{nullptr};
        obs_display_t* display{nullptr};
        obs_source_t* fadeTransition{nullptr};
        std::atomic_bool fadeVideoCompleted{false};
        bool sliderDragging{false};
    };

    static void draw(void* parameter, uint32_t width, uint32_t height);
    QWidget* createPanel(Panel& panel, const QString& title, const QString& color);
    bool openPanel(Panel& panel, const std::filesystem::path& mediaPath, bool audioOutput);
    void initializeDisplay(Panel& panel);
    void destroyDisplay(Panel& panel);
    void resizeDisplay(Panel& panel);
    void updatePanel(Panel& panel, const QString& role);
    void togglePanelPlayback(Panel& panel);
    void togglePanelLoop(Panel& panel);
    void seekPanel(Panel& panel, int64_t deltaMs);
    bool promotePreviewToProgram(const char* operation);
    bool fadePreviewToProgram();
    void finishFadeIfComplete();
    void releaseFadeTransition();
    void choosePlaylist();
    bool activatePlaylistItem(const char* reason);
    bool navigatePlaylist(bool forward, const char* reason);
    void preparePlaylistLookahead();
    void releasePreload();
    void updatePlaylistStatus();
    void primeIndependentStates();
    void toggleFullscreen();
    void closePanels();

    Panel m_preview;
    Panel m_program;
    std::unique_ptr<ObsPlaybackBackend> m_preload;
    std::unique_ptr<ObsPlaylist> m_playlist;
    ObsContext& m_context;
    QTimer* m_timer{nullptr};
    QPushButton* m_takeButton{nullptr};
    QPushButton* m_quickPlayButton{nullptr};
    QPushButton* m_cutButton{nullptr};
    QPushButton* m_fadeButton{nullptr};
    QPushButton* m_loadPlaylistButton{nullptr};
    QPushButton* m_playlistPreviousButton{nullptr};
    QPushButton* m_playlistNextButton{nullptr};
    QComboBox* m_fadeDuration{nullptr};
    QCheckBox* m_playlistLoop{nullptr};
    QCheckBox* m_autoNext{nullptr};
    QLabel* m_playlistStatus{nullptr};
    bool m_closing{false};
    bool m_statesPrimed{false};
    bool m_fadeActive{false};
};
