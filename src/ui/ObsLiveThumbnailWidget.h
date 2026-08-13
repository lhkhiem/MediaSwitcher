#pragma once

#include <QWidget>
#include <cstdint>
#include <functional>

class ObsPlaybackBackend;
class QResizeEvent;
class QShowEvent;
struct obs_display;
typedef struct obs_display obs_display_t;

class ObsLiveThumbnailWidget final : public QWidget {
    Q_OBJECT
public:
    explicit ObsLiveThumbnailWidget(std::function<ObsPlaybackBackend*()> backendProvider, QWidget* parent = nullptr);
    ~ObsLiveThumbnailWidget() override;

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    static void draw(void* parameter, uint32_t width, uint32_t height);
    void initializeDisplay();
    void destroyDisplay();

    std::function<ObsPlaybackBackend*()> m_backendProvider;
    obs_display_t* m_display{nullptr};
};
