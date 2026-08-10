#pragma once

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QTimer>
#include <QSlider>
#include <QPushButton>
#include "engine/input/InputSlot.h"
#include "engine/input/IMediaSource.h"
#include <memory>

class InputSlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit InputSlotWidget(const InputSlot& slot, bool isPvw, bool isPgm, QWidget *parent = nullptr);
    ~InputSlotWidget() override = default;

    int slotId() const { return m_slotId; }
    void setCardSize(int w, int h);

signals:
    void clicked(int slotId);
    void hovered(int slotId);
    void removeRequested(int slotId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;

private:
    int m_slotId{-1};
    std::string m_name;
    QImage m_thumbnail;
    std::shared_ptr<IMediaSource> m_source{nullptr};
    bool m_isPvw{false};
    bool m_isPgm{false};
    QTimer* m_refreshTimer{nullptr};
    QPushButton* m_playBtn{nullptr};
    QPushButton* m_audioBtn{nullptr};
    QSlider* m_volSlider{nullptr};
    QPushButton* m_closeBtn{nullptr};
};
