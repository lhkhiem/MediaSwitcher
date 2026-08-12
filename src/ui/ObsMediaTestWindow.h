#pragma once

#include <QWidget>

#include <filesystem>
#include <memory>

class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class ObsContext;
class ObsPlaybackBackend;
struct obs_display;
typedef struct obs_display obs_display_t;

class ObsMediaTestWindow final : public QWidget {
    Q_OBJECT
public:
    ObsMediaTestWindow(ObsContext& context, const std::filesystem::path& mediaPath, QWidget* parent = nullptr);
    ~ObsMediaTestWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    static void draw(void* parameter, uint32_t width, uint32_t height);
    void initializeDisplay();
    void destroyDisplay();
    void resizeDisplay();
    void toggleFullscreen();
    void updateStatus();
    void seekRelative(int64_t deltaMs);
    void togglePlayPause();

    std::unique_ptr<ObsPlaybackBackend> m_backend;
    QWidget* m_videoSurface{nullptr};
    QLabel* m_statusLabel{nullptr};
    QSlider* m_positionSlider{nullptr};
    QPushButton* m_playPauseButton{nullptr};
    QPushButton* m_loopButton{nullptr};
    QTimer* m_timer{nullptr};
    obs_display_t* m_display{nullptr};
    qint64 m_lastDiagnosticMs{0};
    bool m_sliderDragging{false};
    bool m_closing{false};
};
