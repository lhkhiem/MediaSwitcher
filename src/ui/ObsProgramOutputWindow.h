#pragma once

#include <QWidget>

#include <cstdint>
#include <functional>

class ObsPlaybackBackend;
class QHideEvent;
class QResizeEvent;
class QShowEvent;
struct obs_display;
typedef struct obs_display obs_display_t;

class ObsProgramOutputWindow final : public QWidget {
    Q_OBJECT
public:
    explicit ObsProgramOutputWindow(std::function<ObsPlaybackBackend*()> backendProvider, QWidget* parent = nullptr);
    ~ObsProgramOutputWindow() override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    static void draw(void* parameter, uint32_t width, uint32_t height);
    void initializeDisplay();
    void destroyDisplay();

    std::function<ObsPlaybackBackend*()> m_backendProvider;
    obs_display_t* m_display{nullptr};
};
