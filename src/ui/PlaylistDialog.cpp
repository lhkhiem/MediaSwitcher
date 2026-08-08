#include "PlaylistDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>

PlaylistDialog::PlaylistDialog(std::shared_ptr<PlaylistSource> playlistSource, QWidget *parent)
    : QDialog(parent)
    , m_playlistSource(playlistSource)
{
    this->setWindowTitle("Playlist Manager - MediaSwitcher");
    this->resize(640, 480);
    this->setStyleSheet(R"(
        QDialog {
            background-color: #161720;
            color: #FFFFFF;
        }
        QLabel {
            color: #CCCCCC;
        }
    )");

    setupUi();
    refreshTrackList();
}

void PlaylistDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // Title / Header
    QLabel* headerLabel = new QLabel("📋 Playlist Manager", this);
    headerLabel->setStyleSheet("color: #00ACC1; font-weight: bold; font-size: 15px;");
    mainLayout->addWidget(headerLabel);

    // Main Content: Left Track List + Right Action Buttons
    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(10);

    m_trackListWidget = new QListWidget(this);
    m_trackListWidget->setStyleSheet(R"(
        QListWidget {
            background-color: #0E0F14;
            color: #FFFFFF;
            border: 1px solid #2B2D3A;
            border-radius: 6px;
            font-size: 12px;
            padding: 4px;
        }
        QListWidget::item {
            padding: 6px;
            border-bottom: 1px solid #1C1E2A;
            border-radius: 4px;
        }
        QListWidget::item:hover {
            background-color: #212330;
        }
        QListWidget::item:selected {
            background-color: #00838F;
            color: #FFFFFF;
            font-weight: bold;
        }
    )");
    connect(m_trackListWidget, &QListWidget::itemDoubleClicked, this, &PlaylistDialog::onItemDoubleClicked);
    contentLayout->addWidget(m_trackListWidget, 1);

    // Right Actions Column
    QVBoxLayout* actionLayout = new QVBoxLayout();
    actionLayout->setSpacing(8);

    m_addFilesBtn = new QPushButton("📂 Add Files...", this);
    m_addFilesBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #00ACC1; color: #FFFFFF; font-weight: bold;
            border: none; border-radius: 5px; padding: 8px 14px;
        }
        QPushButton:hover { background-color: #26C6DA; }
    )");
    connect(m_addFilesBtn, &QPushButton::clicked, this, &PlaylistDialog::onAddFilesClicked);
    actionLayout->addWidget(m_addFilesBtn);

    m_removeBtn = new QPushButton("❌ Remove", this);
    m_removeBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #D32F2F; color: #FFFFFF; font-weight: bold;
            border: none; border-radius: 5px; padding: 6px 14px;
        }
        QPushButton:hover { background-color: #E53935; }
    )");
    connect(m_removeBtn, &QPushButton::clicked, this, &PlaylistDialog::onRemoveClicked);
    actionLayout->addWidget(m_removeBtn);

    actionLayout->addSpacing(6);

    m_moveUpBtn = new QPushButton("⬆ Move Up", this);
    m_moveUpBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #2B2D3A; color: #FFFFFF; font-weight: bold;
            border: 1px solid #44475A; border-radius: 5px; padding: 6px 14px;
        }
        QPushButton:hover { background-color: #383B4D; }
    )");
    connect(m_moveUpBtn, &QPushButton::clicked, this, &PlaylistDialog::onMoveUpClicked);
    actionLayout->addWidget(m_moveUpBtn);

    m_moveDownBtn = new QPushButton("⬇ Move Down", this);
    m_moveDownBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #2B2D3A; color: #FFFFFF; font-weight: bold;
            border: 1px solid #44475A; border-radius: 5px; padding: 6px 14px;
        }
        QPushButton:hover { background-color: #383B4D; }
    )");
    connect(m_moveDownBtn, &QPushButton::clicked, this, &PlaylistDialog::onMoveDownClicked);
    actionLayout->addWidget(m_moveDownBtn);

    actionLayout->addSpacing(6);

    m_clearBtn = new QPushButton("🗑 Clear All", this);
    m_clearBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #424242; color: #FFFFFF; font-weight: bold;
            border: none; border-radius: 5px; padding: 6px 14px;
        }
        QPushButton:hover { background-color: #616161; }
    )");
    connect(m_clearBtn, &QPushButton::clicked, this, &PlaylistDialog::onClearClicked);
    actionLayout->addWidget(m_clearBtn);

    actionLayout->addStretch();
    contentLayout->addLayout(actionLayout);

    mainLayout->addLayout(contentLayout, 1);

    // Playback Control Bar
    QHBoxLayout* playControlLayout = new QHBoxLayout();
    playControlLayout->setSpacing(8);

    m_prevBtn = new QPushButton("⏮ Prev", this);
    m_prevBtn->setFixedWidth(70);
    m_prevBtn->setStyleSheet("background-color: #2B2D3A; color: #FFF; font-weight: bold; padding: 6px; border-radius: 4px;");
    connect(m_prevBtn, &QPushButton::clicked, this, &PlaylistDialog::onPrevTrackClicked);
    playControlLayout->addWidget(m_prevBtn);

    m_playPauseBtn = new QPushButton("▶ Play", this);
    m_playPauseBtn->setFixedWidth(80);
    m_playPauseBtn->setStyleSheet("background-color: #388E3C; color: #FFF; font-weight: bold; padding: 6px; border-radius: 4px;");
    connect(m_playPauseBtn, &QPushButton::clicked, this, &PlaylistDialog::onPlayPauseClicked);
    playControlLayout->addWidget(m_playPauseBtn);

    m_nextBtn = new QPushButton("Next ⏭", this);
    m_nextBtn->setFixedWidth(70);
    m_nextBtn->setStyleSheet("background-color: #2B2D3A; color: #FFF; font-weight: bold; padding: 6px; border-radius: 4px;");
    connect(m_nextBtn, &QPushButton::clicked, this, &PlaylistDialog::onNextTrackClicked);
    playControlLayout->addWidget(m_nextBtn);

    playControlLayout->addStretch();

    // Loop & Duration Options
    if (m_playlistSource) {
        m_loopPlaylistCheckBox = new QCheckBox("Loop Playlist", this);
        m_loopPlaylistCheckBox->setChecked(m_playlistSource->isLoopPlaylist());
        m_loopPlaylistCheckBox->setStyleSheet("color: #4FC3F7; font-weight: bold;");
        connect(m_loopPlaylistCheckBox, &QCheckBox::toggled, this, &PlaylistDialog::onLoopPlaylistToggled);
        playControlLayout->addWidget(m_loopPlaylistCheckBox);

        m_loopTrackCheckBox = new QCheckBox("Loop Track", this);
        m_loopTrackCheckBox->setChecked(m_playlistSource->isLoopTrack());
        m_loopTrackCheckBox->setStyleSheet("color: #FFB74D; font-weight: bold;");
        connect(m_loopTrackCheckBox, &QCheckBox::toggled, this, &PlaylistDialog::onLoopTrackToggled);
        playControlLayout->addWidget(m_loopTrackCheckBox);
    }

    QLabel* imgDurLabel = new QLabel(" Image Duration:", this);
    imgDurLabel->setStyleSheet("color: #AAA; font-size: 11px;");
    playControlLayout->addWidget(imgDurLabel);

    m_imageDurationSpin = new QDoubleSpinBox(this);
    m_imageDurationSpin->setRange(1.0, 60.0);
    m_imageDurationSpin->setValue(5.0);
    m_imageDurationSpin->setSuffix(" s");
    m_imageDurationSpin->setFixedWidth(70);
    m_imageDurationSpin->setStyleSheet("background-color: #2B2D3A; color: #FFF; border: 1px solid #444; border-radius: 4px; padding: 2px;");
    connect(m_imageDurationSpin, &QDoubleSpinBox::valueChanged, this, &PlaylistDialog::onImageDurationChanged);
    playControlLayout->addWidget(m_imageDurationSpin);

    mainLayout->addLayout(playControlLayout);

    // Footer / Close Button
    QHBoxLayout* footerLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("Double-click track to play immediately.", this);
    m_statusLabel->setStyleSheet("color: #888888; font-style: italic; font-size: 11px;");
    footerLayout->addWidget(m_statusLabel);

    footerLayout->addStretch();

    QPushButton* closeBtn = new QPushButton("Close", this);
    closeBtn->setFixedWidth(90);
    closeBtn->setStyleSheet("background-color: #424242; color: #FFF; font-weight: bold; padding: 6px; border-radius: 4px;");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    footerLayout->addWidget(closeBtn);

    mainLayout->addLayout(footerLayout);
}

void PlaylistDialog::refreshTrackList() {
    if (!m_playlistSource || !m_trackListWidget) return;

    m_trackListWidget->clear();
    const auto& tracks = m_playlistSource->tracks();
    size_t currentIdx = m_playlistSource->currentTrackIndex();

    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];
        QString typeTag = t.isImage ? QString("[IMAGE %1s]").arg(t.imageDurationSec) : "[VIDEO]";
        QString itemText = QString("#%1  %2  %3").arg(i + 1).arg(typeTag).arg(QString::fromStdString(t.name));

        QListWidgetItem* item = new QListWidgetItem(itemText, m_trackListWidget);
        if (i == currentIdx) {
            item->setIcon(QIcon()); // Highlight current playing track
            m_trackListWidget->setCurrentItem(item);
        }
    }

    if (m_playlistSource->isPlaying()) {
        m_playPauseBtn->setText("⏸ Pause");
        m_playPauseBtn->setStyleSheet("background-color: #FF9800; color: #FFF; font-weight: bold; padding: 6px; border-radius: 4px;");
    } else {
        m_playPauseBtn->setText("▶ Play");
        m_playPauseBtn->setStyleSheet("background-color: #388E3C; color: #FFF; font-weight: bold; padding: 6px; border-radius: 4px;");
    }
}

void PlaylistDialog::onAddFilesClicked() {
    QString filter = "Media Files (*.mp4 *.mkv *.mov *.avi *.flv *.wmv *.webm *.ts *.m4v *.mpg *.mpeg *.vob *.3gp *.m2ts *.mts *.png *.jpg *.jpeg *.bmp *.webp *.gif *.tiff);;"
                     "All Files (*.*)";
    QStringList files = QFileDialog::getOpenFileNames(this, "Select Files for Playlist", "", filter);
    if (files.isEmpty() || !m_playlistSource) return;

    double imgDur = m_imageDurationSpin ? m_imageDurationSpin->value() : 5.0;
    for (const QString& file : files) {
        m_playlistSource->addTrack(file.toUtf8().toStdString(), imgDur);
    }
    refreshTrackList();
}

void PlaylistDialog::onRemoveClicked() {
    int row = m_trackListWidget->currentRow();
    if (row < 0 || !m_playlistSource) return;

    m_playlistSource->removeTrack(static_cast<size_t>(row));
    refreshTrackList();
}

void PlaylistDialog::onMoveUpClicked() {
    int row = m_trackListWidget->currentRow();
    if (row <= 0 || !m_playlistSource) return;

    m_playlistSource->moveTrack(static_cast<size_t>(row), static_cast<size_t>(row - 1));
    refreshTrackList();
    m_trackListWidget->setCurrentRow(row - 1);
}

void PlaylistDialog::onMoveDownClicked() {
    int row = m_trackListWidget->currentRow();
    if (row < 0 || !m_playlistSource) return;
    if (static_cast<size_t>(row + 1) >= m_playlistSource->trackCount()) return;

    m_playlistSource->moveTrack(static_cast<size_t>(row), static_cast<size_t>(row + 1));
    refreshTrackList();
    m_trackListWidget->setCurrentRow(row + 1);
}

void PlaylistDialog::onClearClicked() {
    if (!m_playlistSource) return;
    if (QMessageBox::question(this, "Clear Playlist", "Are you sure you want to remove all tracks?") == QMessageBox::Yes) {
        m_playlistSource->clearTracks();
        refreshTrackList();
    }
}

void PlaylistDialog::onItemDoubleClicked(QListWidgetItem *item) {
    Q_UNUSED(item);
    int row = m_trackListWidget->currentRow();
    if (row >= 0 && m_playlistSource) {
        m_playlistSource->setTrackIndex(static_cast<size_t>(row));
        m_playlistSource->play();
        refreshTrackList();
    }
}

void PlaylistDialog::onPrevTrackClicked() {
    if (m_playlistSource) {
        m_playlistSource->prevTrack();
        refreshTrackList();
    }
}

void PlaylistDialog::onPlayPauseClicked() {
    if (!m_playlistSource) return;
    if (m_playlistSource->isPlaying()) {
        m_playlistSource->pause();
    } else {
        m_playlistSource->play();
    }
    refreshTrackList();
}

void PlaylistDialog::onNextTrackClicked() {
    if (m_playlistSource) {
        m_playlistSource->nextTrack();
        refreshTrackList();
    }
}

void PlaylistDialog::onLoopPlaylistToggled(bool checked) {
    if (m_playlistSource) {
        m_playlistSource->setLoopPlaylist(checked);
    }
}

void PlaylistDialog::onLoopTrackToggled(bool checked) {
    if (m_playlistSource) {
        m_playlistSource->setLoopTrack(checked);
    }
}

void PlaylistDialog::onImageDurationChanged(double val) {
    Q_UNUSED(val);
}
