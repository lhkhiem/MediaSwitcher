#pragma once

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPushButton>
#include "engine/input/InputSlot.h"
#include <string>

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
    SourceType m_type{SourceType::ColorBars};
    QImage m_thumbnail;
    bool m_isPvw{false};
    bool m_isPgm{false};
    QPushButton* m_closeBtn{nullptr};
};
