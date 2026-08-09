#include "AboutDialog.h"
#include "common/config/AppInfo.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QPixmap>
#include <QApplication>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUi();
}

void AboutDialog::setupUi() {
    this->setWindowTitle("Về phần mềm & Bản quyền - MediaSwitcher");
    this->setFixedSize(520, 420);
    this->setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // Modern Dark Studio Style
    this->setStyleSheet(R"(
        QDialog {
            background-color: #121318;
            color: #E0E0E0;
            font-family: 'Segoe UI', Arial, sans-serif;
        }
        QLabel {
            color: #CCCCCC;
        }
        QFrame#cardFrame {
            background-color: #1E1F28;
            border: 1px solid #2B2D3A;
            border-radius: 8px;
            padding: 12px;
        }
        QPushButton {
            background-color: #2B2D3A;
            color: #FFFFFF;
            font-weight: bold;
            border: 1px solid #3E4154;
            border-radius: 4px;
            padding: 8px 24px;
            min-width: 90px;
        }
        QPushButton:hover {
            background-color: #00ACC1;
            border-color: #00E5FF;
        }
    )");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 16);
    mainLayout->setSpacing(16);

    // 1. Header Section (App Title & Icon)
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(16);

    QLabel* iconLabel = new QLabel(this);
    iconLabel->setFixedSize(64, 64);
    iconLabel->setAlignment(Qt::AlignCenter);
    // Use the real application icon (set in main.cpp via IDI_APPICON resource)
    QIcon appIcon = QApplication::windowIcon();
    if (!appIcon.isNull()) {
        iconLabel->setPixmap(appIcon.pixmap(56, 56));
    } else {
        iconLabel->setText("MS");
    }
    iconLabel->setStyleSheet(R"(
        background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0A1628, stop:1 #1A0A2E);
        border-radius: 12px;
        border: 1px solid #2B3A5A;
        padding: 4px;
    )");
    headerLayout->addWidget(iconLabel);

    QVBoxLayout* titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);

    QLabel* titleLabel = new QLabel(AppInfo::FULL_NAME, this);
    titleLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #FFFFFF;");

    QLabel* versionLabel = new QLabel(QString("Phiên bản %1 (Build %2)").arg(AppInfo::VERSION, AppInfo::BUILD_DATE), this);
    versionLabel->setStyleSheet("font-size: 12px; color: #00E5FF; font-weight: bold;");

    QLabel* descLabel = new QLabel(AppInfo::DESCRIPTION, this);
    descLabel->setStyleSheet("font-size: 11px; color: #AAAAAA;");
    descLabel->setWordWrap(true);

    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(versionLabel);
    titleLayout->addWidget(descLabel);
    headerLayout->addLayout(titleLayout);

    mainLayout->addLayout(headerLayout);

    // 2. Info Card Frame (Developer & Copyright Details)
    QFrame* cardFrame = new QFrame(this);
    cardFrame->setObjectName("cardFrame");
    QVBoxLayout* cardLayout = new QVBoxLayout(cardFrame);
    cardLayout->setSpacing(10);

    // Developer / Tác giả
    QHBoxLayout* devLayout = new QHBoxLayout();
    QLabel* devTitle = new QLabel("👤 Nhà phát triển / Tác giả:", cardFrame);
    devTitle->setStyleSheet("font-weight: bold; color: #FF9800; font-size: 12px;");
    QLabel* devValue = new QLabel(AppInfo::DEVELOPER, cardFrame);
    devValue->setStyleSheet("font-weight: bold; color: #FFFFFF; font-size: 12px;");
    devLayout->addWidget(devTitle);
    devLayout->addWidget(devValue);
    devLayout->addStretch();
    cardLayout->addLayout(devLayout);

    // Organization / Công ty
    QHBoxLayout* orgLayout = new QHBoxLayout();
    QLabel* orgTitle = new QLabel("🏢 Đơn vị phát triển:", cardFrame);
    orgTitle->setStyleSheet("font-weight: bold; color: #FF9800; font-size: 12px;");
    QLabel* orgValue = new QLabel(AppInfo::COMPANY, cardFrame);
    orgValue->setStyleSheet("color: #DDDDDD; font-size: 12px;");
    orgLayout->addWidget(orgTitle);
    orgLayout->addWidget(orgValue);
    orgLayout->addStretch();
    cardLayout->addLayout(orgLayout);

    // Legal Copyright / Bản quyền
    QHBoxLayout* copyLayout = new QHBoxLayout();
    QLabel* copyTitle = new QLabel("© Bản quyền:", cardFrame);
    copyTitle->setStyleSheet("font-weight: bold; color: #FF9800; font-size: 12px;");
    QLabel* copyValue = new QLabel(AppInfo::COPYRIGHT, cardFrame);
    copyValue->setStyleSheet("color: #4CAF50; font-weight: bold; font-size: 12px;");
    copyLayout->addWidget(copyTitle);
    copyLayout->addWidget(copyValue);
    copyLayout->addStretch();
    cardLayout->addLayout(copyLayout);

    // Tech Stack / Công nghệ
    QHBoxLayout* techLayout = new QHBoxLayout();
    QLabel* techTitle = new QLabel("⚡ Công nghệ lõi:", cardFrame);
    techTitle->setStyleSheet("font-weight: bold; color: #81D4FA; font-size: 11px;");
    QLabel* techValue = new QLabel(AppInfo::TECH_STACK, cardFrame);
    techValue->setStyleSheet("color: #B0BEC5; font-size: 11px; font-family: Consolas, monospace;");
    techLayout->addWidget(techTitle);
    techLayout->addWidget(techValue);
    techLayout->addStretch();
    cardLayout->addLayout(techLayout);

    mainLayout->addWidget(cardFrame);

    // 3. Footer / Close Button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    QPushButton* closeBtn = new QPushButton("Đóng", this);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeBtn);

    mainLayout->addLayout(buttonLayout);
}
