#include "InputSlotWidget.h"
#include <QPainter>
#include <QLinearGradient>

InputSlotWidget::InputSlotWidget(const InputSlot& slot, bool isPvw, bool isPgm, QWidget *parent)
    : QWidget(parent)
    , m_slotId(slot.id)
    , m_name(slot.name)
    , m_thumbnail(slot.thumbnail)
    , m_source(slot.source)
    , m_isPvw(isPvw)
    , m_isPgm(isPgm)
{
    this->setFixedSize(160, 100);
    this->setCursor(Qt::PointingHandCursor);

    m_playBtn = new QPushButton(this);
    m_playBtn->setFixedSize(22, 22);
    m_playBtn->move(160 - 28, 100 - 28);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setToolTip("Play/Pause Channel");
    m_playBtn->setStyleSheet(R"(
        QPushButton {
            background-color: rgba(20, 20, 28, 200);
            color: #FFFFFF;
            font-size: 10px;
            font-weight: bold;
            border: 1px solid #FF9800;
            border-radius: 11px;
        }
        QPushButton:hover {
            background-color: #FF9800;
            color: #000000;
        }
    )");

    connect(m_playBtn, &QPushButton::clicked, this, [this]() {
        if (m_source) {
            if (m_source->isPlaying()) {
                m_source->pause();
            } else {
                m_source->play();
            }
        }
    });

    m_closeBtn = new QPushButton("✕", this);
    m_closeBtn->setFixedSize(18, 18);
    m_closeBtn->move(160 - 22, 4);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setToolTip("Remove Input Slot");
    m_closeBtn->setStyleSheet(R"(
        QPushButton {
            background-color: rgba(180, 20, 20, 210);
            color: #FFFFFF;
            font-size: 10px;
            font-weight: bold;
            border: 1px solid #FF5252;
            border-radius: 9px;
        }
        QPushButton:hover {
            background-color: #FF1744;
            color: #FFFFFF;
        }
    )");
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        emit removeRequested(m_slotId);
    });

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (this->isVisible()) {
            this->update();
        }
    });
    m_refreshTimer->start(33); // Refresh thumbnail at ~30 FPS
}

void InputSlotWidget::setCardSize(int w, int h) {
    this->setFixedSize(w, h);
    if (m_playBtn) {
        int btnS = (h < 85) ? 18 : 22;
        m_playBtn->setFixedSize(btnS, btnS);
        m_playBtn->move(w - btnS - 6, h - btnS - 6);
    }
    if (m_closeBtn) {
        int closeS = (h < 85) ? 15 : 18;
        m_closeBtn->setFixedSize(closeS, closeS);
        m_closeBtn->move(w - closeS - 3, 3);
    }
    this->update();
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

    // 1. Draw Live Playing Thumbnail from Source (falls back to static poster if empty)
    bool drewLiveFrame = false;
    if (m_source) {
        auto frame = m_source->getFrame();
        if (frame && frame->data() && frame->width() > 0 && frame->height() > 0) {
            QImage img(frame->data(), frame->width(), frame->height(), QImage::Format_RGBA8888);
            painter.drawImage(rect(), img);
            drewLiveFrame = true;
        }
    }

    if (!drewLiveFrame) {
        if (!m_thumbnail.isNull()) {
            painter.drawImage(rect(), m_thumbnail);
        } else {
            painter.fillRect(rect(), QColor(34, 36, 51));
        }
    }

    // 2. Draw Top-Left Channel Number Badge (STRICTLY confined to badgeRect 22x16)
    int badgeW = (h < 85) ? 18 : 22;
    int badgeH = (h < 85) ? 14 : 16;
    QRect badgeRect(3, 3, badgeW, badgeH);

    QColor badgeBg = m_isPgm ? QColor(229, 57, 53) : (m_isPvw ? QColor(255, 152, 0) : QColor(0, 0, 0, 200));
    painter.fillRect(badgeRect, badgeBg);

    painter.setPen(QColor(255, 255, 255));
    QFont badgeFont = painter.font();
    badgeFont.setBold(true);
    badgeFont.setPixelSize(h < 85 ? 9 : 10);
    painter.setFont(badgeFont);
    painter.drawText(badgeRect, Qt::AlignCenter, QString::number(m_slotId));

    // 3. Draw Bottom Dark Gradient for Text Readability ONLY
    QLinearGradient grad(0, h * 0.5, 0, h);
    grad.setColorAt(0.0, QColor(0, 0, 0, 0));
    grad.setColorAt(1.0, QColor(0, 0, 0, 220));
    painter.fillRect(QRect(0, h / 2, w, h / 2), grad);

    // 4. Draw Badge Text Overlay
    QString statusBadge = "";
    if (m_isPgm && m_isPvw) {
        statusBadge = " [PVW/PGM]";
    } else if (m_isPgm) {
        statusBadge = " [PGM]";
    } else if (m_isPvw) {
        statusBadge = " [PVW]";
    }

    QString text = QString("%1%2").arg(QString::fromStdString(m_name)).arg(statusBadge);
    
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(h < 85 ? 9 : 11);
    painter.setFont(font);

    painter.setPen(QColor(255, 255, 255));
    QRect textRect = (h < 85) ? QRect(4, h - 20, w - 24, 18) : QRect(6, h - 28, w - 28, 24);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

    // 5. Update Play/Pause Overlay Button Icon
    if (m_playBtn && m_source) {
        if (m_source->isPlaying()) {
            m_playBtn->setText("⏸");
        } else {
            m_playBtn->setText("▶");
        }
    }

    // 6. Draw vMix Border (NO BRUSH - OUTLINE ONLY)
    QPen pen;
    if (m_isPgm && m_isPvw) {
        pen.setColor(QColor(255, 110, 0)); // Amber-Red for PVW+PGM
        pen.setWidth(3);
    } else if (m_isPgm) {
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
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 6, 6);
}
