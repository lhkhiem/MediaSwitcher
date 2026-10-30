#pragma once

#include <QWidget>
#include <QPixmap>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QLabel;
class QImage;
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
enum class ObsRenderMode;
class ObsPlaylist;
class ObsSourceCatalog;
class ObsProgramOutputWindow;
class AudioMeterWidget;
class WorkspaceStateStore;
struct WorkspaceState;
struct ObsCatalogSource;
class QCloseEvent;
class QResizeEvent;
class QShowEvent;
class QKeyEvent;
class QEvent;
class QScreen;
struct obs_display;
struct obs_source;
struct gs_texture;
struct gs_texture_render;
struct gs_stage_surface;
typedef struct obs_display obs_display_t;
typedef struct obs_source obs_source_t;
typedef struct gs_texture gs_texture_t;
typedef struct gs_texture_render gs_texrender_t;
typedef struct gs_stage_surface gs_stagesurf_t;

class ObsDualMediaTestWindow final : public QWidget {
    Q_OBJECT
public:
    explicit ObsDualMediaTestWindow(ObsContext& context, QWidget* parent = nullptr);
    ObsDualMediaTestWindow(ObsContext& context, const std::filesystem::path& mediaPath, QWidget* parent = nullptr);
    ~ObsDualMediaTestWindow() override;
    bool runCueOnlySmokeTest(const std::filesystem::path& mediaPath);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class UiLanguage { Vietnamese, English };
    enum class LayoutMode { Single = 0, Triple = 1 };

    struct TripleZone {
        int index{0};
        int x{0};
        int y{0};
        int width{640};
        int height{1080};
        ObsRenderMode renderMode;
        uint64_t sourceId{0};
        uint64_t pendingSourceId{0};
        int64_t pendingPositionMs{0};
        bool pendingLooping{false};
        bool looping{false};
        float volume{1.0f};
        bool audioMuted{false};
        bool blackout{false};
        bool sliderDragging{false};
        std::unique_ptr<ObsPlaybackBackend> activeBackend;
        std::unique_ptr<ObsPlaybackBackend> pendingBackend;
        QWidget* cardWidget{nullptr};
        QPushButton* targetButton{nullptr};
        QLabel* infoLabel{nullptr};
        QLabel* statusBadge{nullptr};
        QLabel* timeLabel{nullptr};
        QSlider* seekSlider{nullptr};
        QPushButton* playPauseButton{nullptr};
        QPushButton* loopButton{nullptr};
        QPushButton* resetButton{nullptr};
        QComboBox* fitCombo{nullptr};
        QPushButton* muteButton{nullptr};
        QPushButton* blackoutButton{nullptr};
        QPushButton* takeButton{nullptr};
    };

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
        QLabel* volumeValueLabel{nullptr};
        AudioMeterWidget* audioMeter{nullptr};
        obs_display_t* display{nullptr};
        obs_source_t* fadeTransition{nullptr};
        std::atomic_bool fadeVideoCompleted{false};
        std::atomic_uint64_t thumbnailSourceId{0};
        std::atomic_bool thumbnailCaptureEnabled{false};
        std::atomic_bool thumbnailCaptureContinuous{false};
        std::atomic_bool thumbnailUpdatePending{false};
        std::atomic_uint64_t thumbnailGeneration{0};
        gs_texrender_t* thumbnailTexrender{nullptr};
        gs_stagesurf_t* thumbnailStages[3]{nullptr, nullptr, nullptr};
        int64_t thumbnailStagePositionMs[3]{0, 0, 0};
        int64_t thumbnailStageDurationMs[3]{0, 0, 0};
        bool thumbnailStageLooping[3]{false, false, false};
        uint32_t thumbnailWriteStage{0};
        std::atomic_uint32_t thumbnailFrameCounter{0};
        std::atomic_uint32_t thumbnailStagedFrameCount{0};
        bool sliderDragging{false};
        bool audioMuted{false};
        float volume{1.0f};
    };

    // Snapshot preserves a role's independent timeline while OBS sources are
    // recreated for TAKE, CUT and FADE operations.
    struct PlaybackSnapshot {
        uint64_t sourceId{0};
        int64_t positionMs{0};
        int64_t durationMs{0};
        bool looping{false};
        bool valid{false};
    };

    static void draw(void* parameter, uint32_t width, uint32_t height);
    static void captureSourceThumbnail(Panel& panel);
    QImage captureBackendStill(const ObsPlaybackBackend& backend) const;
    QWidget* createPanel(Panel& panel, const QString& title, const QString& color);
    bool openPanel(Panel& panel, const std::filesystem::path& mediaPath, bool audioOutput);
    void initializeDisplay(Panel& panel);
    void destroyDisplay(Panel& panel);
    void resizeDisplay(Panel& panel);
    void updateMonitorLayout();
    void updateInputToolbarPresentation();
    void updatePanel(Panel& panel, const QString& role);
    void updateAudioMeters();
    void updateProcessMetrics();
    void updateCopyrightDisplay();
    void showAboutDialog();
    QString localized(const char* vietnamese, const char* english) const;
    void bindLocalizedProperty(QObject* object, const char* propertyName, const char* vietnamese, const char* english);
    void applyLanguage();
    void setLanguage(int index);
    bool openCatalogSource(ObsPlaybackBackend& backend, uint64_t sourceId, bool startPaused = false);
    void stagePreviewSource(const ObsCatalogSource& source);
    void updateStagedPreviewFrame();
    bool openStagedPreview(bool play);
    void showSourceThumbnail(uint64_t sourceId, const QImage& frame,
                             const PlaybackSnapshot& snapshot = {}, bool timelineFrame = false);
    PlaybackSnapshot capturePreviewSnapshot() const;
    PlaybackSnapshot captureProgramSnapshot() const;
    PlaybackSnapshot previewSnapshotForSource(const ObsCatalogSource& source) const;
    void rememberProgramSnapshot(const PlaybackSnapshot& snapshot);
    void stagePreviewSnapshot(const PlaybackSnapshot& snapshot);
    void clearPreviewSource();
    void clearProgramSource();
    bool isCatalogSourceAvailable(uint64_t sourceId) const;
    void togglePanelPlayback(Panel& panel);
    void togglePanelLoop(Panel& panel);
    void resetPanel(Panel& panel);
    void seekPanel(Panel& panel, int64_t deltaMs);
    void requestStagedPreviewSeek(int64_t positionMs);
    void applyStagedPreviewSeek();
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
    void setProjectFrameRate(int index);
    void setProgramRenderMode(int index);
    void setProgramSourceId(uint64_t sourceId, bool refreshCatalog = true);
    void refreshPlaylistButtonUi();
    void refreshPlaylistUi();
    void showPlaylistManager();
    void savePlaylistManager();
    void cancelPlaylistManager();
    void toggleFullscreen();
    void toggleProgramOutputFullscreen();
    uint64_t restoreWorkspace();
    uint64_t appendWorkspaceState(const WorkspaceState& state);
    WorkspaceState captureWorkspaceState() const;
    bool replaceWorkspace(const WorkspaceState& state);
    bool saveWorkspace();
    bool saveWorkspaceTo(const std::filesystem::path& filePath);
    void saveWorkspaceDocument(bool saveAs);
    void openWorkspaceDocument();
    void applyWorkspaceSettings(const WorkspaceState& state, bool applyFrameRate);
    void selectOutputScreen();
    QScreen* selectedOutputScreen() const;
    void retryCatalogSource(uint64_t sourceId);
    void closePanels();
    QWidget* createTripleDeck(QWidget* parent);
    void renderProgramCanvas(uint32_t width, uint32_t height, bool mapCanvasToSurface);
    void setLayoutMode(LayoutMode mode);
    void selectActiveZone(int zoneIndex);
    bool promotePreviewToZone(int zoneIndex, const char* operation);
    void commitPendingZoneSwaps();
    void updateTripleZonesUi();
    void showTripleLayoutSettingsDialog();
    void toggleTestPattern();
    void rebuildTestPatternImage();
    void toggleZonePlayback(int zoneIndex);
    void toggleZoneLoop(int zoneIndex);
    void resetZone(int zoneIndex);
    void setZoneFitMode(int zoneIndex, int comboIndex);
    void toggleZoneAudioMaster(int zoneIndex);
    void toggleZoneBlackout(int zoneIndex);
    void toggleBlackoutAll(bool checked);

    Panel m_preview;
    Panel m_program;
    std::unique_ptr<ObsPlaylist> m_playlist;
    std::unique_ptr<ObsSourceCatalog> m_sourceCatalog;
    std::unique_ptr<WorkspaceStateStore> m_workspaceStateStore;
    std::unique_ptr<ObsProgramOutputWindow> m_programOutput;
    ObsContext& m_context;
    QTimer* m_timer{nullptr};
    QTimer* m_audioMeterTimer{nullptr};
    QTimer* m_processMetricsTimer{nullptr};
    QTimer* m_previewSeekTimer{nullptr};
    QLabel* m_processMetricsLabel{nullptr};
    QPushButton* m_copyrightButton{nullptr};
    QPushButton* m_quickPlayButton{nullptr};
    QPushButton* m_cutButton{nullptr};
    QPushButton* m_fadeButton{nullptr};
    QPushButton* m_fullscreenButton{nullptr};
    QPushButton* m_addSourceButton{nullptr};
    QPushButton* m_workspaceButton{nullptr};
    QPushButton* m_openPlaylistButton{nullptr};
    QComboBox* m_catalogThumbnailSize{nullptr};
    QComboBox* m_sourceTypeFilter{nullptr};
    QComboBox* m_projectFrameRate{nullptr};
    QComboBox* m_programRenderMode{nullptr};
    QComboBox* m_languageSelector{nullptr};
    QPushButton* m_playlistPreviousButton{nullptr};
    QPushButton* m_playlistNextButton{nullptr};
    QComboBox* m_fadeDuration{nullptr};
    QCheckBox* m_playlistLoop{nullptr};
    QCheckBox* m_autoNext{nullptr};
    QLabel* m_playlistStatus{nullptr};
    QPushButton* m_playlistSaveButton{nullptr};
    QPushButton* m_playlistCancelButton{nullptr};
    QListWidget* m_sourceCatalogList{nullptr};
    QListWidget* m_playlistList{nullptr};
    QDialog* m_playlistDialog{nullptr};
    QWidget* m_switcherArea{nullptr};
    QWidget* m_inputBank{nullptr};
    QWidget* m_inputToolbarWidget{nullptr};
    QLabel* m_typeLabel{nullptr};
    QLabel* m_sizeLabel{nullptr};
    QLabel* m_fpsLabel{nullptr};
    QLabel* m_programViewLabel{nullptr};
    QLabel* m_languageLabel{nullptr};
    std::unordered_map<uint64_t, QPixmap> m_sourcePosters;
    std::unordered_map<uint64_t, QPixmap> m_sourceThumbnails;
    std::unordered_map<uint64_t, QPixmap> m_sourceTimelineFrames;
    std::unordered_map<uint64_t, QLabel*> m_catalogThumbnailLabels;
    std::unordered_map<uint64_t, PlaybackSnapshot> m_sourceThumbnailSnapshots;
    std::unordered_map<uint64_t, PlaybackSnapshot> m_sourceTimelineFrameSnapshots;
    std::unordered_set<uint64_t> m_sourcesWithTimelineThumbnail;
    std::unordered_map<uint64_t, std::unique_ptr<ObsPlaybackBackend>> m_pausedSourceRuntimes;
    std::unordered_map<uint64_t, PlaybackSnapshot> m_lastProgramSnapshots;
    QPixmap m_stagedPreviewFrame;
    uint64_t m_stagedPreviewFrameSourceId{0};
    uint64_t m_previewSourceId{0};
    uint64_t m_programSourceId{0};
    uint64_t m_fadeOutgoingSourceId{0};
    uint64_t m_previewSeekGeneration{0};
    int m_catalogThumbnailWidth{160};
    bool m_closing{false};
    bool m_fadeActive{false};
    bool m_fadeCleanupQueued{false};
    bool m_previewTransportStarted{false};
    bool m_previewRuntimeMatchesStagedFrame{false};
    bool m_layoutRefreshQueued{false};
    bool m_playlistMode{false};
    bool m_playlistEditing{false};
    UiLanguage m_language{UiLanguage::Vietnamese};
    ObsRenderMode m_selectedProgramRenderMode;
    std::vector<uint64_t> m_playlistDraft;
    std::filesystem::path m_currentWorkspaceFile;
    std::string m_outputScreenName;
    PlaybackSnapshot m_fadeOutgoingSnapshot;
    PlaybackSnapshot m_stagedPreviewSnapshot;
    mutable std::mutex m_tripleRenderMutex;
    LayoutMode m_layoutMode{LayoutMode::Single};
    int m_canvasWidth{3200};
    int m_canvasHeight{1080};
    int m_activeZoneIndex{1};
    std::atomic_bool m_blackoutAll{false};
    std::atomic_bool m_testPatternActive{false};
    std::atomic_bool m_testPatternDirty{true};
    gs_texture_t* m_testPatternTexture{nullptr};
    QImage m_testPatternImage;
    std::array<TripleZone, 3> m_tripleZones;
    QWidget* m_tripleDeckWidget{nullptr};
    QComboBox* m_layoutModeSelector{nullptr};
    QPushButton* m_tripleConfigButton{nullptr};
    QPushButton* m_testPatternButton{nullptr};
    QPushButton* m_blackoutAllButton{nullptr};
    std::array<QPushButton*, 3> m_railZoneButtons{{nullptr, nullptr, nullptr}};
    QWidget* m_zoneSelectorContainer{nullptr};
};
