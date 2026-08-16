#include "AboutDialog.h"

#include "common/config/AppInfo.h"
#include "common/config/CopyrightSettings.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUi();
    loadContent();
}

void AboutDialog::setupUi() {
    setWindowTitle(QStringLiteral("Về phần mềm & Bản quyền - MediaSwitcher"));
    resize(700, 620);
    setMinimumSize(560, 500);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setStyleSheet(R"(
        QDialog {
            background-color: #121318;
            color: #E0E0E0;
            font-family: 'Segoe UI', Arial, sans-serif;
        }
        QLabel { color: #CCCCCC; }
        QTextBrowser {
            background-color: #1E1F28;
            color: #E8EDF2;
            border: 1px solid #343746;
            border-radius: 8px;
            padding: 14px;
            font-family: 'Segoe UI', Arial, sans-serif;
            font-size: 12px;
        }
        QPushButton {
            background-color: #2B2D3A;
            color: #FFFFFF;
            font-weight: bold;
            border: 1px solid #3E4154;
            border-radius: 4px;
            padding: 8px 18px;
            min-width: 90px;
        }
        QPushButton:hover { background-color: #00ACC1; border-color: #00E5FF; }
        QPushButton#openFileButton { background-color: #00838F; border-color: #00ACC1; }
        QPushButton#openFileButton:hover { background-color: #00ACC1; }
    )");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 16);
    mainLayout->setSpacing(14);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(16);
    auto* iconLabel = new QLabel(this);
    iconLabel->setFixedSize(64, 64);
    iconLabel->setAlignment(Qt::AlignCenter);
    const QIcon appIcon = QApplication::windowIcon();
    if (!appIcon.isNull()) {
        iconLabel->setPixmap(appIcon.pixmap(56, 56));
    } else {
        iconLabel->setText(QStringLiteral("MS"));
    }
    iconLabel->setStyleSheet(R"(
        background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0A1628, stop:1 #1A0A2E);
        border-radius: 12px;
        border: 1px solid #2B3A5A;
        padding: 4px;
    )");
    headerLayout->addWidget(iconLabel);

    auto* titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(3);
    auto* titleLabel = new QLabel(AppInfo::FULL_NAME, this);
    titleLabel->setTextFormat(Qt::PlainText);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold; color: #FFFFFF;"));
    auto* versionLabel = new QLabel(
        QStringLiteral("Phiên bản %1 (Build %2)").arg(AppInfo::VERSION, AppInfo::BUILD_DATE), this);
    versionLabel->setTextFormat(Qt::PlainText);
    versionLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #00E5FF; font-weight: bold;"));
    auto* hintLabel = new QLabel(
        QStringLiteral("Nội dung được đóng gói từ copyright.txt và chỉ hiển thị trong bản production."), this);
    hintLabel->setTextFormat(Qt::PlainText);
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #AAAAAA;"));
    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(versionLabel);
    titleLayout->addWidget(hintLabel);
    headerLayout->addLayout(titleLayout, 1);
    mainLayout->addLayout(headerLayout);

    m_contentView = new QTextBrowser(this);
    m_contentView->setOpenExternalLinks(false);
    m_contentView->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    mainLayout->addWidget(m_contentView, 1);

    auto* buttonBox = new QDialogButtonBox(this);
    auto* closeButton = buttonBox->addButton(QStringLiteral("Đóng"), QDialogButtonBox::RejectRole);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void AboutDialog::loadContent() {
    const CopyrightInfo info = CopyrightSettings::load();
    m_contentView->setPlainText(info.details);
}
