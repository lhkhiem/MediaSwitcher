#include "InputSlotWidget.h"
#include <QPainter>
#include <QLinearGradient>

InputSlotWidget::InputSlotWidget(const InputSlot& slot, bool isPvw, bool isPgm, QWidget *parent)
    : QWidget(parent)
    , m_slotId(slot.id)
    , m_name(slot.name)
    , m_thumbnail(slot.thumbnail)
    , m_isPvw(isPvw)
    , m_isPgm(isPgm)
{
    this->setFixedSize(160, 100);
    this->setCursor(Qt::PointingHandCursor);
}

void InputSlotWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_slotId);
    }
    QWidget::mousePressEvent(event);
}

void InputSlotWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // 1. Draw Fixed Static Poster Thumbnail (Captured at creation, never alters playback queue)
    if (!m_thumbnail.isNull()) {
        painter.drawImage(rect(), m_thumbnail);
    } else {
        painter.fillRect(rect(), QColor(34, 36, 51));
    }

    // 2. Draw Bottom Dark Gradient Overlay for Text Readability
    QLinearGradient grad(0, h * 0.4, 0, h);
    grad.setColorAt(0.0, QColor(0, 0, 0, 0));
    grad.setColorAt(1.0, QColor(0, 0, 0, 220));
    painter.fillRect(rect(), grad);

    // 3. Draw Badge Text Overlay
    QString badge = "";
    if (m_isPgm) {
        badge = " [PGM]";
    } else if (m_isPvw) {
        badge = " [PVW]";
    }

    QString text = QString("[%1] %2%3").arg(m_slotId).arg(QString::fromStdString(m_name)).arg(badge);
    
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(11);
    painter.setFont(font);

    painter.setPen(QColor(255, 255, 255));
    QRect textRect(6, h - 28, w - 12, 24);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

    // 4. Draw vMix Border
    QPen pen;
    if (m_isPgm) {
        pen.setColor(QColor(229, 57, 53)); // Red for PGM
        pen.setWidth(3);
    } else if (m_isPvw) {
        pen.setColor(QColor(255, 152, 0)); // Orange for PVW
        pen.setWidth(3);
    } else {
        pen.setColor(QColor(58, 61, 82));  // Dark Grey
        pen.setWidth(1);
    }
    painter.setPen(pen);
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 6, 6);
}
