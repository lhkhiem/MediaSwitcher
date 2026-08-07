#pragma once

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include <QMouseEvent>
#include "engine/input/InputSlot.h"

class InputSlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit InputSlotWidget(const InputSlot& slot, bool isPvw, bool isPgm, QWidget *parent = nullptr);
    ~InputSlotWidget() override = default;

    int slotId() const { return m_slotId; }

signals:
    void clicked(int slotId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    int m_slotId{-1};
    std::string m_name;
    QImage m_thumbnail;
    bool m_isPvw{false};
    bool m_isPgm{false};
};
