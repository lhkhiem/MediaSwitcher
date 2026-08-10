#include "InputSlotWidget.h"
#include <QPainter>
#include <QLinearGradient>

InputSlotWidget::InputSlotWidget(const InputSlot& slot, bool isPvw, bool isPgm, QWidget *parent)
    : QWidget(parent)
    , m_slotId(slot.id)
    , m_name(slot.name)
    , m_type(slot.type)
    , m_thumbnail(slot.thumbnail)
    , m_isPvw(isPvw)
    , m_isPgm(isPgm)
{
    this->setFixedSize(160, 100);
    this->setCursor(Qt::PointingHandCursor);

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
}

void InputSlotWidget::setCardSize(int w, int h) {
    this->setFixedSize(w, h);

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

void InputSlotWidget::enterEvent(QEnterEvent *event) {
    emit hovered(m_slotId);
    QWidget::enterEvent(event);
}

void InputSlotWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // 1. Render Static Thumbnail Background
    if (!m_thumbnail.isNull()) {
        painter.drawImage(rect(), m_thumbnail);
    } else {
        painter.fillRect(rect(), QColor(28, 30, 42));

        // Center Icon fallback if thumbnail not ready
        painter.setPen(QColor(120, 130, 150));
        QFont iconFont = painter.font();
        iconFont.setPixelSize(h < 85 ? 18 : 24);
        painter.setFont(iconFont);
        const char* fallbackIcon = (m_type == SourceType::ImageFile) ? "🖼" : ((m_type == SourceType::VideoFile) ? "🎬" : "🎨");
        painter.drawText(rect(), Qt::AlignCenter, fallbackIcon);
    }

    // 2. Center Static Play Overlay for VIDEO ONLY
    if (m_type == SourceType::VideoFile) {
        int playIconSize = (h < 85) ? 22 : 28;
        QRect playRect((w - playIconSize) / 2, (h - playIconSize) / 2 - 4, playIconSize, playIconSize);
        
        painter.setBrush(QColor(20, 24, 38, 160));
        painter.setPen(QColor(255, 255, 255, 200));
        painter.drawEllipse(playRect);

        QFont playFont = painter.font();
        playFont.setBold(true);
        playFont.setPixelSize(h < 85 ? 10 : 12);
        painter.setFont(playFont);
        painter.setPen(QColor(255, 255, 255));
        painter.drawText(playRect.adjusted(2, 0, 0, 0), Qt::AlignCenter, "▶");
    } else if (m_type == SourceType::RTSPStream) {
        // RTSP LIVE Badge
        QRect liveBadgeRect(w - 55, h - 22, 50, 16);
        painter.fillRect(liveBadgeRect, QColor(229, 57, 53, 220));
        painter.setPen(QColor(255, 255, 255));
        QFont liveFont = painter.font();
        liveFont.setBold(true);
        liveFont.setPixelSize(9);
        painter.setFont(liveFont);
        painter.drawText(liveBadgeRect, Qt::AlignCenter, "LIVE");
    }

    // 3. Top-Left State Badge (IDLE, PVW, PGM, PVW+PGM)
    QString badgeText;
    QColor badgeBg;
    int badgeW = 22;
    int badgeH = (h < 85) ? 14 : 16;

    if (m_isPgm && m_isPvw) {
        badgeText = "PVW+PGM";
        badgeBg = QColor(255, 110, 0); // Amber/Gold #FF6E00
        badgeW = (h < 85) ? 48 : 56;
    } else if (m_isPgm) {
        badgeText = "PGM";
        badgeBg = QColor(229, 57, 53); // Red #E53935
        badgeW = (h < 85) ? 28 : 32;
    } else if (m_isPvw) {
        badgeText = "PVW";
        badgeBg = QColor(255, 152, 0); // Orange #FF9800
        badgeW = (h < 85) ? 28 : 32;
    } else {
        badgeText = QString::number(m_slotId);
        badgeBg = QColor(28, 30, 42, 220); // Dark Slate #1C1E2A
        badgeW = (h < 85) ? 18 : 22;
    }

    QRect badgeRect(3, 3, badgeW, badgeH);
    painter.fillRect(badgeRect, badgeBg);

    painter.setPen(QColor(255, 255, 255));
    QFont badgeFont = painter.font();
    badgeFont.setBold(true);
    badgeFont.setPixelSize(h < 85 ? 8 : 10);
    painter.setFont(badgeFont);
    painter.drawText(badgeRect, Qt::AlignCenter, badgeText);

    // 4. Bottom Dark Gradient Overlay for Name Readability
    QLinearGradient grad(0, h * 0.4, 0, h);
    grad.setColorAt(0.0, QColor(0, 0, 0, 0));
    grad.setColorAt(1.0, QColor(0, 0, 0, 230));
    painter.fillRect(QRect(0, static_cast<int>(h * 0.4), w, static_cast<int>(h * 0.6)), grad);

    // 5. Source Name & Type Text Overlay
    QString statusBadge = "";
    if (m_isPgm && m_isPvw) {
        statusBadge = " [PVW/PGM]";
    } else if (m_isPgm) {
        statusBadge = " [PGM]";
    } else if (m_isPvw) {
        statusBadge = " [PVW]";
    }

    const char* typePrefix = (m_type == SourceType::ImageFile) ? "🖼 " : ((m_type == SourceType::VideoFile) ? "🎬 " : "🎨 ");
    QString text = QString("%1%2%3").arg(typePrefix).arg(QString::fromStdString(m_name)).arg(statusBadge);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(h < 85 ? 9 : 11);
    painter.setFont(font);

    painter.setPen(QColor(255, 255, 255));
    int textWidthLimit = w - 12;
    QRect textRect = (h < 85) ? QRect(4, h - 18, textWidthLimit, 16) : QRect(6, h - 22, textWidthLimit, 18);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

    // 6. Card Border
    QPen pen;
    if (m_isPgm && m_isPvw) {
        pen.setColor(QColor(255, 110, 0)); // Amber/Gold #FF6E00 for PVW+PGM
        pen.setWidth(3);
    } else if (m_isPgm) {
        pen.setColor(QColor(229, 57, 53)); // Red #E53935 for PGM
        pen.setWidth(3);
    } else if (m_isPvw) {
        pen.setColor(QColor(255, 152, 0)); // Orange #FF9800 for PVW
        pen.setWidth(3);
    } else {
        pen.setColor(QColor(58, 61, 82));  // Dark Grey Slate #3A3D52 for IDLE
        pen.setWidth(1);
    }

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 6, 6);
}
