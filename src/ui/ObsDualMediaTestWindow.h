#pragma once

#include <QWidget>
#include <QPixmap>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

class QLabel;
class QComboBox;
class QCheckBox;
class QListWidget;
class QDialog;
class QPushButton;
class QSlider;
class QStackedLayout;
class QTimer;
class ObsContext;
class ObsPlaybackBackend;
class ObsPlaylist;
class ObsSourceCatalog;
class ObsProgramOutputWindow;
struct ObsCatalogSource;
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
    explicit ObsDualMediaTestWindow(ObsContext& context, QWidget* parent = nullptr);
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
        QWidget* videoContainer{nullptr};
        QWidget* videoSurface{nullptr};
        QLabel* stagedFrameLabel{nullptr};
        QStackedLayout* videoStack{nullptr};
        QLabel* sourceLabel{nullptr};
        QLabel* timeLabel{nullptr};
        QLabel* statusLabel{nullptr};
        QSlider* seekSlider{nullptr};
        QPushButton* playPauseButton{nullptr};
        QPushButton* loopButton{nullptr};
        QPushButton* resetButton{nullptr};
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
    bool openStagedPreview(bool play);
    void stagePreviewSource(const ObsCatalogSource& source);
    void stagePreviewAtPosition(const std::filesystem::path& path, uint64_t sourceId, int64_t positionMs);
    void clearPreviewSource();
    void clearProgramSource();
    bool isCatalogSourceAvailable(uint64_t sourceId) const;
    void requestStagedPreviewFrame();
    void showStagedPreviewFrame(int64_t positionMs, int64_t durationMs, const QImage& frame);
    void togglePanelPlayback(Panel& panel);
    void togglePanelLoop(Panel& panel);
    void resetPanel(Panel& panel);
    void seekPanel(Panel& panel, int64_t deltaMs);
    bool promotePreviewToProgram(const char* operation);
    bool fadePreviewToProgram();
    void finishFadeIfComplete();
    void releaseFadeTransition();
    void addCatalogSource();
    void removeCatalogSource();
    void addSelectedCatalogSourceToPlaylist();
    void removeSelectedPlaylistStep();
    void movePlaylistStep(int delta);
    bool startPlaylist();
    void stopPlaylist();
    bool activatePlaylistProgram(const char* reason);
    bool navigatePlaylist(bool forward, const char* reason);
    void refreshCatalogUi();
    void setCatalogThumbnailSize(int width);
    void setProgramSourceId(uint64_t sourceId);
    void refreshPlaylistUi();
    void showPlaylistManager();
    void primeIndependentStates();
    void toggleFullscreen();
    void toggleProgramOutputFullscreen();
    void closePanels();

    Panel m_preview;
    Panel m_program;
    std::unique_ptr<ObsPlaylist> m_playlist;
    std::unique_ptr<ObsSourceCatalog> m_sourceCatalog;
    std::unique_ptr<ObsProgramOutputWindow> m_programOutput;
    ObsContext& m_context;
    QTimer* m_timer{nullptr};
    QTimer* m_stagedSeekTimer{nullptr};
    QPushButton* m_quickPlayButton{nullptr};
    QPushButton* m_cutButton{nullptr};
    QPushButton* m_fadeButton{nullptr};
    QPushButton* m_fullscreenButton{nullptr};
    QPushButton* m_addSourceButton{nullptr};
    QPushButton* m_removeSourceButton{nullptr};
    QPushButton* m_openPlaylistButton{nullptr};
    QComboBox* m_catalogThumbnailSize{nullptr};
    QPushButton* m_playlistPreviousButton{nullptr};
    QPushButton* m_playlistNextButton{nullptr};
    QComboBox* m_fadeDuration{nullptr};
    QCheckBox* m_playlistLoop{nullptr};
    QCheckBox* m_autoNext{nullptr};
    QLabel* m_playlistStatus{nullptr};
    QListWidget* m_sourceCatalogList{nullptr};
    QListWidget* m_playlistList{nullptr};
    QDialog* m_playlistDialog{nullptr};
    std::unordered_map<uint64_t, QPixmap> m_sourceThumbnails;
    std::filesystem::path m_stagedPreviewPath;
    uint64_t m_stagedPreviewSourceId{0};
    uint64_t m_previewSourceId{0};
    int64_t m_stagedPreviewPositionMs{0};
    int64_t m_stagedPreviewDurationMs{0};
    bool m_stagedPreviewLoop{false};
    uint64_t m_programSourceId{0};
    uint64_t m_fadeOutgoingSourceId{0};
    int m_catalogThumbnailWidth{160};
    bool m_closing{false};
    bool m_statesPrimed{false};
    bool m_fadeActive{false};
    bool m_playlistMode{false};
};
