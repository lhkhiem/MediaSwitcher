#pragma once

#include <QWidget>
#include <QPixmap>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

class QLabel;
class QImage;
class QComboBox;
class QCheckBox;
class QListWidget;
class QDialog;
class QPushButton;
class QProgressBar;
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
struct gs_texture_render;
struct gs_stage_surface;
typedef struct obs_display obs_display_t;
typedef struct obs_source obs_source_t;
typedef struct gs_texture_render gs_texrender_t;
typedef struct gs_stage_surface gs_stagesurf_t;

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
        ObsDualMediaTestWindow* owner{nullptr};
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
        QPushButton* seekBackButton{nullptr};
        QPushButton* seekForwardButton{nullptr};
        QSlider* volumeSlider{nullptr};
        QPushButton* muteButton{nullptr};
        QProgressBar* leftAudioMeter{nullptr};
        QProgressBar* rightAudioMeter{nullptr};
        obs_display_t* display{nullptr};
        obs_source_t* fadeTransition{nullptr};
        std::atomic_bool fadeVideoCompleted{false};
        std::atomic_uint64_t thumbnailSourceId{0};
        std::atomic_bool thumbnailCaptureEnabled{false};
        gs_texrender_t* thumbnailTexrender{nullptr};
        gs_stagesurf_t* thumbnailStages[3]{nullptr, nullptr, nullptr};
        uint32_t thumbnailWriteStage{0};
        uint32_t thumbnailFrameCounter{0};
        std::atomic_bool thumbnailFrameDelivered{false};
        bool sliderDragging{false};
        bool audioMuted{false};
        float volume{1.0f};
        float leftMeterLevel{0.0f};
        float rightMeterLevel{0.0f};
    };

    // A cue is the operator-visible PVW state.  It exists independently from
    // an optional OBS decoder used only when the operator explicitly presses
    // Play in PVW.  Keeping this separate prevents ffmpeg_source activation
    // from changing the cue's transport state behind the UI's back.
    struct PlaybackSnapshot {
        uint64_t sourceId{0};
        int64_t positionMs{0};
        int64_t durationMs{0};
        bool looping{false};
        bool valid{false};
    };

    static void draw(void* parameter, uint32_t width, uint32_t height);
    static void captureProgramThumbnail(Panel& panel);
    QWidget* createPanel(Panel& panel, const QString& title, const QString& color);
    bool openPanel(Panel& panel, const std::filesystem::path& mediaPath, bool audioOutput);
    void initializeDisplay(Panel& panel);
    void destroyDisplay(Panel& panel);
    void resizeDisplay(Panel& panel);
    void updateMonitorLayout();
    void updatePanel(Panel& panel, const QString& role);
    bool openCatalogSource(ObsPlaybackBackend& backend, uint64_t sourceId, bool startPaused = false);
    void stagePreviewSource(const ObsCatalogSource& source);
    void stagePreviewAtPosition(const std::filesystem::path& path, uint64_t sourceId, int64_t positionMs);
    void requestStagedPreviewFrame();
    void showStagedPreviewFrame(int64_t positionMs, int64_t durationMs, const QImage& frame);
    void showProgramThumbnail(uint64_t sourceId, const QImage& frame);
    bool openStagedPreview(bool play);
    PlaybackSnapshot capturePreviewSnapshot() const;
    PlaybackSnapshot captureProgramSnapshot() const;
    PlaybackSnapshot previewSnapshotForSource(const ObsCatalogSource& source) const;
    void rememberProgramSnapshot(const PlaybackSnapshot& snapshot);
    void stagePreviewSnapshot(const PlaybackSnapshot& snapshot);
    void clearStagedPreviewMetadata();
    void clearPreviewSource();
    void clearProgramSource();
    bool isCatalogSourceAvailable(uint64_t sourceId) const;
    void togglePanelPlayback(Panel& panel);
    void togglePanelLoop(Panel& panel);
    void resetPanel(Panel& panel);
    void seekPanel(Panel& panel, int64_t deltaMs);
    bool promotePreviewToProgram(const char* operation);
    bool fadePreviewToProgram();
    void finishFadeIfComplete();
    void releaseFadeTransition();
    void addCatalogSource();
    void addCatalogFiles(int sourceTypeFilter);
    void addRtspSource();
    void removeCatalogSource();
    void removeCatalogSource(uint64_t sourceId);
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
    QPushButton* m_openPlaylistButton{nullptr};
    QComboBox* m_catalogThumbnailSize{nullptr};
    QComboBox* m_sourceTypeFilter{nullptr};
    QPushButton* m_playlistPreviousButton{nullptr};
    QPushButton* m_playlistNextButton{nullptr};
    QComboBox* m_fadeDuration{nullptr};
    QCheckBox* m_playlistLoop{nullptr};
    QCheckBox* m_autoNext{nullptr};
    QLabel* m_playlistStatus{nullptr};
    QListWidget* m_sourceCatalogList{nullptr};
    QListWidget* m_playlistList{nullptr};
    QDialog* m_playlistDialog{nullptr};
    QWidget* m_switcherArea{nullptr};
    QWidget* m_inputBank{nullptr};
    std::unordered_map<uint64_t, QPixmap> m_sourceThumbnails;
    std::unordered_map<uint64_t, QLabel*> m_catalogThumbnailLabels;
    std::unordered_map<uint64_t, PlaybackSnapshot> m_lastProgramSnapshots;
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
    bool m_fadeActive{false};
    bool m_fadeCleanupQueued{false};
    bool m_playlistMode{false};
    PlaybackSnapshot m_fadeOutgoingSnapshot;
};
