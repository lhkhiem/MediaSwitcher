#include "ObsDualMediaTestWindow.h"
#include "ObsProgramOutputWindow.h"
#include "AudioMeterWidget.h"
#include "AboutDialog.h"

#include "app/WorkspaceStateStore.h"
#include "common/config/Config.h"
#include "common/config/CopyrightSettings.h"
#include "common/logger/Logger.h"
#include "engine/diagnostics/MediaDiagnostics.h"
#include "engine/obs/ObsContext.h"
#include "engine/obs/ObsPlaybackBackend.h"
#include "engine/obs/ObsPlaylist.h"
#include "engine/obs/ObsSourceCatalog.h"
#include "engine/obs/ObsThumbnailService.h"

extern "C" {
#include <callback/signal.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <obs.h>
}

#include <QCloseEvent>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFileIconProvider>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QSpinBox>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QUrl>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

namespace {
QString formatMilliseconds(int64_t value) {
    const int64_t totalSeconds = std::max<int64_t>(0, value / 1000);
    if (totalSeconds < 3600) {
        return QStringLiteral("%1:%2")
            .arg(totalSeconds / 60, 2, 10, QChar('0'))
            .arg(totalSeconds % 60, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2:%3")
        .arg(totalSeconds / 3600, 2, 10, QChar('0'))
        .arg((totalSeconds / 60) % 60, 2, 10, QChar('0'))
        .arg(totalSeconds % 60, 2, 10, QChar('0'));
}

QString formatTimeline(int64_t positionMs, int64_t durationMs) {
    if (durationMs <= 0) return QStringLiteral("%1 / --:--").arg(formatMilliseconds(positionMs));
    return QStringLiteral("%1 / %2").arg(formatMilliseconds(positionMs), formatMilliseconds(durationMs));
}

QString catalogSourceName(const ObsCatalogSource& source, bool vietnamese) {
    if (source.systemSource && source.type == ObsCatalogSourceType::ColorBlank) {
        const QString original = QString::fromUtf8(source.displayName.c_str());
        const QString suffix = original.section(QLatin1Char(' '), -1);
        return vietnamese ? QStringLiteral("Nền trống %1").arg(suffix) : original;
    }
    if (!source.displayName.empty()) return QString::fromUtf8(source.displayName.c_str());
    return QFileInfo(QString::fromStdWString(source.path.wstring())).fileName();
}

QString catalogSourceBadge(const ObsCatalogSource& source, bool vietnamese) {
    if (!vietnamese) return QString::fromLatin1(obsCatalogSourceTypeName(source.type));
    switch (source.type) {
    case ObsCatalogSourceType::VideoFile: return QStringLiteral("VIDEO");
    case ObsCatalogSourceType::AudioFile: return QStringLiteral("ÂM THANH");
    case ObsCatalogSourceType::ImageFile: return QStringLiteral("HÌNH ẢNH");
    case ObsCatalogSourceType::RtspCamera: return QStringLiteral("RTSP");
    case ObsCatalogSourceType::ColorBlank: return QStringLiteral("NỀN");
    }
    return {};
}

class CatalogTileWidget final : public QWidget {
public:
    explicit CatalogTileWidget(std::function<void()> activate, QWidget* parent = nullptr)
        : QWidget(parent), m_activate(std::move(activate)) {}

protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_activate) m_activate();
        QWidget::mouseReleaseEvent(event);
    }

private:
    std::function<void()> m_activate;
};

QPixmap fitCatalogThumbnail(const QPixmap& source, int width) {
    const QSize canvasSize(width, width * 9 / 16);
    QPixmap canvas(canvasSize);
    canvas.fill(QColor(QStringLiteral("#0e1216")));
    if (source.isNull()) return canvas;

    QPainter painter(&canvas);
    const QPixmap fitted = source.scaled(canvasSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter.drawPixmap((canvasSize.width() - fitted.width()) / 2, (canvasSize.height() - fitted.height()) / 2, fitted);
    return canvas;
}

void onFadeVideoStopped(void* data, calldata_t*) {
    static_cast<std::atomic_bool*>(data)->store(true);
    LOG_INFO("OBS dual media: FADE video-stop signal received from libobs.");
}
}

ObsDualMediaTestWindow::ObsDualMediaTestWindow(ObsContext& context, QWidget* parent)
    : ObsDualMediaTestWindow(context, std::filesystem::path{}, parent) {}

ObsDualMediaTestWindow::ObsDualMediaTestWindow(ObsContext& context, const std::filesystem::path& mediaPath, QWidget* parent)
    : QWidget(parent), m_context(context), m_selectedProgramRenderMode(ObsRenderMode::AspectFit) {
    m_tripleZones[0] = {0, 0, 0, 640, 1080, ObsRenderMode::AspectFit, 0, 0, 0, false, false, 1.0f, true, false, false};
    m_tripleZones[1] = {1, 640, 0, 1920, 1080, ObsRenderMode::AspectFit, 0, 0, 0, false, false, 1.0f, false, false, false};
    m_tripleZones[2] = {2, 2560, 0, 640, 1080, ObsRenderMode::AspectFit, 0, 0, 0, false, false, 1.0f, true, false, false};
    const QSettings settings;
    m_language = settings.value(QStringLiteral("ui/language"), QStringLiteral("vi")).toString() == QStringLiteral("en")
        ? UiLanguage::English : UiLanguage::Vietnamese;
    setWindowTitle(QStringLiteral("MediaSwitcher OBS"));
    resize(1280, 720);
    setMinimumSize(760, 500);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet(QStringLiteral(
        "ObsDualMediaTestWindow { background: #161b20; color: #e8edf2; }"
        "QPushButton { background: #35424e; color: #f3f6f8; border: 1px solid #52616e; border-radius: 2px; min-height: 27px; padding: 2px 8px; }"
        "QPushButton:hover { background: #465967; }"
        "QPushButton:pressed { background: #28343d; }"
        "QPushButton:disabled { color: #b8c0c7; background: #303940; border-color: #4c565d; }"
        "QPushButton[loopActive=\"true\"] { background: #1f6b84; border-color: #53a9c6; }"
        "QMenuBar { background: #20272e; color: #eef3f6; border-bottom: 1px solid #3c4953; padding: 2px; }"
        "QMenuBar::item { padding: 5px 12px; background: transparent; }"
        "QMenuBar::item:selected { background: #35424e; }"
        "QMenu { background: #20272e; color: #eef3f6; border: 1px solid #52616e; }"
        "QMenu::item { padding: 7px 30px 7px 22px; }"
        "QMenu::item:selected { background: #1f6b84; }"
        "QComboBox { background: #27313a; color: #eff3f6; border: 1px solid #52616e; border-radius: 2px; min-height: 27px; padding-left: 7px; }"
        "QSlider::groove:horizontal { height: 4px; background: #0e1216; border: 1px solid #4d5962; }"
        "QSlider::sub-page:horizontal { background: #2389b8; }"
        "QSlider::handle:horizontal { width: 9px; margin: -5px 0; background: #d6e0e6; border: 1px solid #8b9aa5; }"
        "QSlider::groove:vertical { width: 4px; background: #10161b; border: 0; border-radius: 2px; }"
        "QSlider::sub-page:vertical { background: #10161b; border-radius: 2px; }"
        "QSlider::add-page:vertical { background: #18b9d7; border-radius: 2px; }"
        "QSlider::handle:vertical { height: 7px; margin: 0 -5px; background: #74e8ef; border: 1px solid #c5f7f9; border-radius: 3px; }"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);
    auto* menuBar = new QMenuBar(this);
    menuBar->setNativeMenuBar(false);
    auto* fileMenu = menuBar->addMenu(QStringLiteral("File"));
    bindLocalizedProperty(fileMenu, "title", "Tệp", "File");
    auto* outputMenu = menuBar->addMenu(QStringLiteral("Output"));
    bindLocalizedProperty(outputMenu, "title", "Đầu ra", "Output");
    QAction* selectOutputScreenAction = outputMenu->addAction(QStringLiteral("Select Output Screen..."));
    QAction* toggleOutputAction = outputMenu->addAction(QStringLiteral("Toggle Fullscreen Output"));
    outputMenu->addSeparator();
    QAction* tripleSettingsAction = outputMenu->addAction(QStringLiteral("3-Zone Canvas Setup..."));
    QAction* testPatternAction = outputMenu->addAction(QStringLiteral("Toggle LED Test Pattern"));
    bindLocalizedProperty(selectOutputScreenAction, "text", "Chọn màn hình output...", "Select Output Screen...");
    bindLocalizedProperty(toggleOutputAction, "text", "Bật/tắt output toàn màn hình", "Toggle Fullscreen Output");
    bindLocalizedProperty(tripleSettingsAction, "text", "Cấu hình Canvas 3 vùng...", "3-Zone Canvas Setup...");
    bindLocalizedProperty(testPatternAction, "text", "Bật/tắt lưới Test Pattern LED", "Toggle LED Test Pattern");
    connect(tripleSettingsAction, &QAction::triggered, this, &ObsDualMediaTestWindow::showTripleLayoutSettingsDialog);
    connect(testPatternAction, &QAction::triggered, this, &ObsDualMediaTestWindow::toggleTestPattern);
    rootLayout->setMenuBar(menuBar);
    m_switcherArea = new QWidget(this);
    m_switcherArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* switcherLayout = new QHBoxLayout(m_switcherArea);
    switcherLayout->setContentsMargins(0, 0, 0, 0);
    switcherLayout->setSpacing(8);
    QWidget* previewPanel = createPanel(m_preview, QStringLiteral("PREVIEW (PVW) - PAUSED"), QStringLiteral("#1d4552"));
    QWidget* programPanel = createPanel(m_program, QStringLiteral("PROGRAM (PGM) - LIVE AUDIO"), QStringLiteral("#482a32"));
    switcherLayout->addWidget(previewPanel, 1);
    auto* transitionWidget = new QWidget(this);
    transitionWidget->setFixedWidth(116);
    transitionWidget->setObjectName(QStringLiteral("transitionRail"));
    transitionWidget->setStyleSheet(QStringLiteral(
        "#transitionRail { background: #20272e; border-left: 1px solid #3c4953; border-right: 1px solid #3c4953; }"
        "QPushButton { min-height: 28px; font-weight: bold; }"));
    auto* transitionControls = new QVBoxLayout(transitionWidget);
    // Hai monitor có margin trên 5 px trước thanh title. Dùng cùng offset
    // để cụm chuyển cảnh bắt đầu đúng một hàng với title PVW/PGM.
    transitionControls->setContentsMargins(5, 5, 5, 12);
    transitionControls->setSpacing(6);
    transitionControls->setAlignment(Qt::AlignTop);
    m_quickPlayButton = new QPushButton(QStringLiteral("Quick Play"), transitionWidget);
    m_cutButton = new QPushButton(QStringLiteral("CUT"), transitionWidget);
    m_fadeButton = new QPushButton(QStringLiteral("FADE"), transitionWidget);
    m_fadeDuration = new QComboBox(transitionWidget);
    m_fadeDuration->addItem(QStringLiteral("300 ms"), 300);
    m_fadeDuration->addItem(QStringLiteral("700 ms"), 700);
    m_fadeDuration->addItem(QStringLiteral("1000 ms"), 1000);
    m_fadeDuration->addItem(QStringLiteral("1500 ms"), 1500);
    m_fadeDuration->setCurrentIndex(1);
    m_quickPlayButton->setText(QStringLiteral("QUICK PLAY"));
    m_cutButton->setText(QStringLiteral("CUT"));
    m_fadeButton->setText(QStringLiteral("FADE"));
    bindLocalizedProperty(m_quickPlayButton, "text", "PHÁT NHANH", "QUICK PLAY");
    bindLocalizedProperty(m_cutButton, "text", "CẮT", "CUT");
    bindLocalizedProperty(m_fadeButton, "text", "MỜ DẦN", "FADE");
    m_fullscreenButton = new QPushButton(QStringLiteral("FULL SCREEN"), transitionWidget);
    bindLocalizedProperty(m_fullscreenButton, "text", "FULL MÀN", "FULL SCREEN");
    bindLocalizedProperty(m_fullscreenButton, "toolTip",
                          "Mở output PGM toàn màn hình trên màn hình thứ hai",
                          "Open the PGM output fullscreen on the second display");
    transitionControls->addWidget(m_fullscreenButton);
    transitionControls->addSpacing(4);
    m_zoneSelectorContainer = new QWidget(transitionWidget);
    auto* zoneSelectorLayout = new QHBoxLayout(m_zoneSelectorContainer);
    zoneSelectorLayout->setContentsMargins(0, 0, 0, 0);
    zoneSelectorLayout->setSpacing(3);
    const char* zoneShortVi[3] = {"TRÁI", "CHÍNH", "PHẢI"};
    const char* zoneShortEn[3] = {"LEFT", "MAIN", "RIGHT"};
    for (int z = 0; z < 3; ++z) {
        auto* btn = new QPushButton(QString::fromUtf8(zoneShortVi[z]), m_zoneSelectorContainer);
        btn->setCheckable(true);
        btn->setChecked(z == m_activeZoneIndex);
        btn->setFixedHeight(24);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { font-size: 9px; font-weight: bold; padding: 1px 2px; background: #26313a; border: 1px solid #485865; }"
            "QPushButton:checked { background: #1f6b84; border: 1px solid #63d2ee; color: #ffffff; }"));
        bindLocalizedProperty(btn, "text", zoneShortVi[z], zoneShortEn[z]);
        connect(btn, &QPushButton::clicked, this, [this, z] { selectActiveZone(z); });
        zoneSelectorLayout->addWidget(btn, 1);
        m_railZoneButtons[static_cast<size_t>(z)] = btn;
    }
    m_zoneSelectorContainer->hide();
    transitionControls->addWidget(m_zoneSelectorContainer);
    transitionControls->addWidget(m_quickPlayButton);
    transitionControls->addWidget(m_cutButton);
    transitionControls->addWidget(m_fadeButton);
    transitionControls->addWidget(m_fadeDuration);

    auto* audioControls = new QHBoxLayout();
    audioControls->setContentsMargins(2, 6, 2, 0);
    audioControls->setSpacing(4);
    const auto addAudioStrip = [transitionWidget, audioControls](Panel& panel, const QString& role) {
        auto* strip = new QWidget(transitionWidget);
        strip->setObjectName(QStringLiteral("audioStrip"));
        strip->setStyleSheet(QStringLiteral(
            "#audioStrip { background: transparent; border: 0; }"));
        auto* stripLayout = new QVBoxLayout(strip);
        stripLayout->setContentsMargins(0, 0, 0, 0);
        stripLayout->setSpacing(3);

        auto* roleLabel = new QLabel(role, strip);
        roleLabel->setAlignment(Qt::AlignCenter);
        roleLabel->setStyleSheet(QStringLiteral(
            "color: #b7c5ce; border: 0; font-size: 9px; font-weight: bold;"));
        stripLayout->addWidget(roleLabel);

        auto* channels = new QHBoxLayout();
        channels->setContentsMargins(0, 0, 0, 0);
        channels->setSpacing(3);
        auto* faderColumn = new QVBoxLayout();
        faderColumn->setContentsMargins(0, 0, 0, 0);
        faderColumn->setSpacing(3);
        auto* meterColumn = new QVBoxLayout();
        meterColumn->setContentsMargins(0, 0, 0, 0);
        meterColumn->setSpacing(3);
        panel.volumeSlider->setParent(strip);
        panel.volumeSlider->setOrientation(Qt::Vertical);
        panel.volumeSlider->setFixedWidth(18);
        panel.volumeSlider->setMinimumHeight(90);
        panel.volumeSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        panel.audioMeter->setParent(strip);
        panel.audioMeter->setCompactMode(true);
        panel.audioMeter->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        faderColumn->addWidget(panel.volumeSlider, 1, Qt::AlignHCenter);
        panel.volumeValueLabel->setParent(strip);
        panel.volumeValueLabel->setAlignment(Qt::AlignCenter);
        faderColumn->addWidget(panel.volumeValueLabel, 0, Qt::AlignHCenter);
        panel.muteButton->setParent(strip);
        faderColumn->addWidget(panel.muteButton, 0, Qt::AlignHCenter);
        channels->addLayout(faderColumn);
        meterColumn->addWidget(panel.audioMeter, 1, Qt::AlignHCenter);
        meterColumn->addSpacing(panel.volumeValueLabel->sizeHint().height() + panel.muteButton->height() + 6);
        channels->addLayout(meterColumn);
        stripLayout->addLayout(channels, 1);
        audioControls->addWidget(strip, 1);
    };
    addAudioStrip(m_preview, QStringLiteral("PVW"));
    addAudioStrip(m_program, QStringLiteral("PGM"));
    transitionControls->addLayout(audioControls, 1);
    switcherLayout->addWidget(transitionWidget);
    switcherLayout->addWidget(programPanel, 1);
    rootLayout->addWidget(m_switcherArea);
    m_tripleDeckWidget = createTripleDeck(this);
    m_tripleDeckWidget->hide();
    rootLayout->addWidget(m_tripleDeckWidget);

    auto* inputBank = new QWidget(this);
    m_inputBank = inputBank;
    inputBank->setObjectName(QStringLiteral("inputBank"));
    inputBank->setMinimumHeight(218);
    inputBank->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    inputBank->setStyleSheet(QStringLiteral("#inputBank { background: #1d242b; border: 1px solid #3b4852; }"));
    auto* inputBankLayout = new QVBoxLayout(inputBank);
    inputBankLayout->setContentsMargins(8, 6, 8, 8);
    inputBankLayout->setSpacing(6);
    m_inputToolbarWidget = new QWidget(inputBank);
    m_inputToolbarWidget->setObjectName(QStringLiteral("inputToolbar"));
    auto* inputToolbar = new QHBoxLayout(m_inputToolbarWidget);
    inputToolbar->setContentsMargins(0, 0, 0, 0);
    inputToolbar->setSpacing(6);
    auto* inputTitle = new QLabel(QStringLiteral("INPUTS"), m_inputToolbarWidget);
    bindLocalizedProperty(inputTitle, "text", "NGUỒN VÀO", "INPUTS");
    inputTitle->setStyleSheet(QStringLiteral("color: #dce7ee; font-weight: bold; letter-spacing: 0px; border: 0;"));
    inputToolbar->addWidget(inputTitle);
    inputToolbar->addStretch();
    m_workspaceButton = new QPushButton(QStringLiteral("Workspace"), m_inputToolbarWidget);
    bindLocalizedProperty(m_workspaceButton, "text", "Workspace", "Workspace");
    auto* workspaceMenu = new QMenu(m_workspaceButton);
    QAction* openWorkspaceAction = workspaceMenu->addAction(QStringLiteral("Open Workspace..."));
    QAction* saveWorkspaceAction = workspaceMenu->addAction(QStringLiteral("Save Workspace"));
    QAction* saveWorkspaceAsAction = workspaceMenu->addAction(QStringLiteral("Save Workspace As..."));
    bindLocalizedProperty(openWorkspaceAction, "text", "Mở workspace...", "Open Workspace...");
    bindLocalizedProperty(saveWorkspaceAction, "text", "Lưu workspace", "Save Workspace");
    bindLocalizedProperty(saveWorkspaceAsAction, "text", "Lưu workspace thành...", "Save Workspace As...");
    openWorkspaceAction->setShortcut(QKeySequence::Open);
    saveWorkspaceAction->setShortcut(QKeySequence::Save);
    saveWorkspaceAsAction->setShortcut(QKeySequence::SaveAs);
    m_workspaceButton->setMenu(workspaceMenu);
    fileMenu->addAction(openWorkspaceAction);
    fileMenu->addSeparator();
    fileMenu->addAction(saveWorkspaceAction);
    fileMenu->addAction(saveWorkspaceAsAction);
    m_addSourceButton = new QPushButton(QStringLiteral("Add Input"), m_inputToolbarWidget);
    bindLocalizedProperty(m_addSourceButton, "text", "Thêm nguồn", "Add Input");
    auto* addMenu = new QMenu(m_addSourceButton);
    QAction* addMediaAction = addMenu->addAction(QStringLiteral("Video files..."));
    QAction* addImageAction = addMenu->addAction(QStringLiteral("Image files..."));
    addMenu->addSeparator();
    QAction* addRtspAction = addMenu->addAction(QStringLiteral("Network stream (RTSP)..."));
    bindLocalizedProperty(addMediaAction, "text", "Tệp video/âm thanh...", "Video/audio files...");
    bindLocalizedProperty(addImageAction, "text", "Tệp hình ảnh...", "Image files...");
    bindLocalizedProperty(addRtspAction, "text", "Luồng mạng (RTSP)...", "Network stream (RTSP)...");
    m_addSourceButton->setMenu(addMenu);
    m_openPlaylistButton = new QPushButton(QStringLiteral("Playlist"), m_inputToolbarWidget);
    bindLocalizedProperty(m_openPlaylistButton, "text", "Danh sách", "Playlist");
    m_playlistPreviousButton = new QPushButton(m_inputToolbarWidget);
    m_playlistPreviousButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    m_playlistPreviousButton->setFixedSize(30, 30);
    m_playlistPreviousButton->setToolTip(QStringLiteral("Previous Playlist"));
    m_playlistPreviousButton->setAccessibleName(QStringLiteral("Previous Playlist"));
    bindLocalizedProperty(m_playlistPreviousButton, "toolTip", "Mục Playlist trước", "Previous Playlist item");
    bindLocalizedProperty(m_playlistPreviousButton, "accessibleName", "Mục Playlist trước", "Previous Playlist item");
    m_playlistPreviousButton->setEnabled(false);
    m_playlistNextButton = new QPushButton(m_inputToolbarWidget);
    m_playlistNextButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    m_playlistNextButton->setFixedSize(30, 30);
    m_playlistNextButton->setToolTip(QStringLiteral("Next Playlist"));
    m_playlistNextButton->setAccessibleName(QStringLiteral("Next Playlist"));
    bindLocalizedProperty(m_playlistNextButton, "toolTip", "Mục Playlist tiếp theo", "Next Playlist item");
    bindLocalizedProperty(m_playlistNextButton, "accessibleName", "Mục Playlist tiếp theo", "Next Playlist item");
    m_playlistNextButton->setEnabled(false);
    auto* playlistTransport = new QWidget(m_inputToolbarWidget);
    playlistTransport->setObjectName(QStringLiteral("playlistTransport"));
    playlistTransport->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* playlistTransportLayout = new QHBoxLayout(playlistTransport);
    playlistTransportLayout->setContentsMargins(0, 0, 0, 0);
    playlistTransportLayout->setSpacing(4);
    playlistTransportLayout->addWidget(m_playlistPreviousButton);
    playlistTransportLayout->addWidget(m_openPlaylistButton);
    playlistTransportLayout->addWidget(m_playlistNextButton);
    m_typeLabel = new QLabel(QStringLiteral("Type"), m_inputToolbarWidget);
    bindLocalizedProperty(m_typeLabel, "text", "Loại", "Type");
    m_typeLabel->setStyleSheet(QStringLiteral("color: #b8c5ce; border: 0;"));
    m_sizeLabel = new QLabel(QStringLiteral("Size"), m_inputToolbarWidget);
    bindLocalizedProperty(m_sizeLabel, "text", "Cỡ", "Size");
    m_sizeLabel->setStyleSheet(QStringLiteral("color: #b8c5ce; border: 0;"));
    m_fpsLabel = new QLabel(QStringLiteral("FPS"), m_inputToolbarWidget);
    m_fpsLabel->setStyleSheet(QStringLiteral("color: #b8c5ce; border: 0;"));
    m_programViewLabel = new QLabel(QStringLiteral("VIEW"), m_inputToolbarWidget);
    m_programViewLabel->setStyleSheet(QStringLiteral("color: #b8c5ce; border: 0;"));
    m_catalogThumbnailSize = new QComboBox(m_inputToolbarWidget);
    m_catalogThumbnailSize->addItem(QStringLiteral("Small"), 110);
    m_catalogThumbnailSize->addItem(QStringLiteral("Normal"), 192);
    m_catalogThumbnailSize->addItem(QStringLiteral("Large"), 220);
    m_catalogThumbnailSize->setCurrentIndex(1);
    m_sourceTypeFilter = new QComboBox(m_inputToolbarWidget);
    m_sourceTypeFilter->addItem(QStringLiteral("All types"), -1);
    m_sourceTypeFilter->addItem(QStringLiteral("Video"), static_cast<int>(ObsCatalogSourceType::VideoFile));
    m_sourceTypeFilter->addItem(QStringLiteral("Audio"), static_cast<int>(ObsCatalogSourceType::AudioFile));
    m_sourceTypeFilter->addItem(QStringLiteral("Images"), static_cast<int>(ObsCatalogSourceType::ImageFile));
    m_sourceTypeFilter->addItem(QStringLiteral("RTSP cameras"), static_cast<int>(ObsCatalogSourceType::RtspCamera));
    m_sourceTypeFilter->addItem(QStringLiteral("Blank"), static_cast<int>(ObsCatalogSourceType::ColorBlank));
    m_projectFrameRate = new QComboBox(m_inputToolbarWidget);
    m_projectFrameRate->setFixedWidth(88);
    const QStringList frameRateLabels{
        QStringLiteral("23.976"), QStringLiteral("24"), QStringLiteral("25"), QStringLiteral("29.97"),
        QStringLiteral("30"), QStringLiteral("50"), QStringLiteral("59.94"), QStringLiteral("60"),
    };
    const auto& frameRates = ObsContext::supportedVideoFrameRates();
    const ObsVideoFrameRate currentFrameRate = m_context.videoFrameRate();
    for (size_t index = 0; index < frameRates.size(); ++index) {
        m_projectFrameRate->addItem(frameRateLabels.at(static_cast<int>(index)), frameRates[index].numerator);
        m_projectFrameRate->setItemData(static_cast<int>(index), frameRates[index].denominator, Qt::UserRole + 1);
        if (frameRates[index] == currentFrameRate) m_projectFrameRate->setCurrentIndex(static_cast<int>(index));
    }
    bindLocalizedProperty(m_projectFrameRate, "toolTip",
                          "FPS dự án cho toàn bộ PVW, PGM và output",
                          "Project FPS for PVW, PGM and output");
    m_programRenderMode = new QComboBox(m_inputToolbarWidget);
    m_programRenderMode->setFixedWidth(88);
    m_programRenderMode->addItem(QStringLiteral("Default"), static_cast<int>(ObsRenderMode::AspectFit));
    m_programRenderMode->addItem(QStringLiteral("Fit"), static_cast<int>(ObsRenderMode::FitToScreen));
    m_programRenderMode->setToolTip(QStringLiteral(
        "Default: giữ tỷ lệ hiển thị nguyên bản. Fit: kéo hình vừa toàn bộ màn hình."));
    bindLocalizedProperty(m_programRenderMode, "toolTip",
                          "Mặc định: giữ tỷ lệ hiển thị nguyên bản. Vừa màn hình: kéo hình phủ toàn bộ màn hình.",
                          "Default: preserve the original aspect ratio. Fit: stretch to fill the entire screen.");
    connect(m_programRenderMode, &QComboBox::currentIndexChanged,
            this, &ObsDualMediaTestWindow::setProgramRenderMode);
    m_languageLabel = new QLabel(QStringLiteral("LANGUAGE"), this);
    bindLocalizedProperty(m_languageLabel, "text", "NGÔN NGỮ", "LANGUAGE");
    m_languageLabel->setStyleSheet(QStringLiteral("color: #b8c5ce; border: 0;"));
    m_languageSelector = new QComboBox(this);
    m_languageSelector->setFixedWidth(58);
    m_languageSelector->addItem(QStringLiteral("VN"), QStringLiteral("vi"));
    m_languageSelector->addItem(QStringLiteral("EN"), QStringLiteral("en"));
    m_languageSelector->setCurrentIndex(m_language == UiLanguage::Vietnamese ? 0 : 1);
    bindLocalizedProperty(m_languageSelector, "toolTip", "Chọn ngôn ngữ giao diện", "Select interface language");
    connect(m_languageSelector, &QComboBox::currentIndexChanged, this, &ObsDualMediaTestWindow::setLanguage);
    m_layoutModeSelector = new QComboBox(m_inputToolbarWidget);
    m_layoutModeSelector->addItem(QStringLiteral("1 vùng (Single)"), 0);
    m_layoutModeSelector->addItem(QStringLiteral("3 vùng (Triple)"), 1);
    m_layoutModeSelector->setToolTip(QStringLiteral("Chế độ bố cục màn hình: 1 vùng hoặc 3 vùng độc lập"));
    connect(m_layoutModeSelector, &QComboBox::currentIndexChanged, this, [this](int idx) {
        setLayoutMode(idx == 1 ? LayoutMode::Triple : LayoutMode::Single);
    });
    inputToolbar->addWidget(m_layoutModeSelector);
    inputToolbar->addWidget(m_workspaceButton);
    inputToolbar->addWidget(m_addSourceButton);
    inputToolbar->addWidget(m_typeLabel);
    inputToolbar->addWidget(m_sourceTypeFilter);
    inputToolbar->addWidget(m_sizeLabel);
    inputToolbar->addWidget(m_catalogThumbnailSize);
    inputToolbar->addWidget(m_fpsLabel);
    inputToolbar->addWidget(m_projectFrameRate);
    inputToolbar->addWidget(m_programViewLabel);
    inputToolbar->addWidget(m_programRenderMode);
    auto* playlistSeparator = new QWidget(m_inputToolbarWidget);
    playlistSeparator->setObjectName(QStringLiteral("playlistSeparator"));
    playlistSeparator->setFixedWidth(1);
    playlistSeparator->setMinimumHeight(22);
    playlistSeparator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    playlistSeparator->setStyleSheet(QStringLiteral("background: #465661; border: 0;"));
    inputToolbar->addWidget(playlistSeparator);
    inputToolbar->addWidget(playlistTransport);
    inputBankLayout->addWidget(m_inputToolbarWidget);
    m_sourceCatalogList = new QListWidget(inputBank);
    m_sourceCatalogList->setViewMode(QListView::IconMode);
    m_sourceCatalogList->setFlow(QListView::LeftToRight);
    m_sourceCatalogList->setWrapping(true);
    m_sourceCatalogList->setResizeMode(QListView::Adjust);
    m_sourceCatalogList->setUniformItemSizes(true);
    m_sourceCatalogList->setIconSize(QSize(160, 90));
    m_sourceCatalogList->setGridSize(QSize(174, 100));
    m_sourceCatalogList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sourceCatalogList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sourceCatalogList->setStyleSheet(QStringLiteral(
        "QListWidget { background: #15181F; border: 0; }"
        "QListWidget::item { border: 0; padding: 0; margin: 0; background: transparent; }"
        "QListWidget::item:selected { background: transparent; }"));
    inputBankLayout->addWidget(m_sourceCatalogList, 1);
    rootLayout->addWidget(inputBank, 1);

    m_processMetricsLabel = new QLabel(this);
    m_processMetricsLabel->setMinimumHeight(26);
    m_processMetricsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_processMetricsLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: #11171c; color: #76d7ea; border: 1px solid #34434d; "
        "padding: 4px 8px; font-family: 'Consolas', 'Courier New', monospace; font-weight: bold; }"));

    m_copyrightButton = new QPushButton(this);
    m_copyrightButton->setFlat(true);
    m_copyrightButton->setMinimumHeight(26);
    m_copyrightButton->setMaximumWidth(360);
    m_copyrightButton->setCursor(Qt::PointingHandCursor);
    m_copyrightButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #11171c; color: #8bd49c; border: 1px solid #34434d; "
        "padding: 4px 10px; font-size: 11px; font-weight: bold; text-align: right; }"
        "QPushButton:hover { color: #74e8ef; border-color: #53a9c6; text-decoration: underline; }"
        "QPushButton:pressed { background: #1d2a31; }"));
    connect(m_copyrightButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::showAboutDialog);
    updateCopyrightDisplay();

    auto* footerLayout = new QHBoxLayout();
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(6);
    footerLayout->addWidget(m_processMetricsLabel, 1);
    auto* languageFooter = new QWidget(this);
    languageFooter->setObjectName(QStringLiteral("languageFooter"));
    languageFooter->setStyleSheet(QStringLiteral(
        "#languageFooter { background: #11171c; border: 1px solid #34434d; }"
        "#languageFooter QLabel { color: #9fb0ba; border: 0; font-size: 10px; font-weight: bold; }"));
    auto* languageFooterLayout = new QHBoxLayout(languageFooter);
    languageFooterLayout->setContentsMargins(8, 0, 2, 0);
    languageFooterLayout->setSpacing(6);
    languageFooterLayout->addWidget(m_languageLabel);
    languageFooterLayout->addWidget(m_languageSelector);
    footerLayout->addWidget(languageFooter, 0);
    footerLayout->addWidget(m_copyrightButton, 0);
    rootLayout->addLayout(footerLayout);

    connect(m_quickPlayButton, &QPushButton::clicked, this, [this] {
        if (m_layoutMode == LayoutMode::Triple) promotePreviewToZone(m_activeZoneIndex, "Quick Play");
        else promotePreviewToProgram("Quick Play");
    });
    connect(m_cutButton, &QPushButton::clicked, this, [this] {
        if (m_layoutMode == LayoutMode::Triple) promotePreviewToZone(m_activeZoneIndex, "CUT");
        else promotePreviewToProgram("CUT");
    });
    connect(m_fadeButton, &QPushButton::clicked, this, [this] {
        if (m_layoutMode == LayoutMode::Triple) promotePreviewToZone(m_activeZoneIndex, "CUT");
        else fadePreviewToProgram();
    });
    connect(m_fullscreenButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::toggleProgramOutputFullscreen);
    connect(selectOutputScreenAction, &QAction::triggered, this, &ObsDualMediaTestWindow::selectOutputScreen);
    connect(toggleOutputAction, &QAction::triggered, this, &ObsDualMediaTestWindow::toggleProgramOutputFullscreen);
    connect(openWorkspaceAction, &QAction::triggered, this, &ObsDualMediaTestWindow::openWorkspaceDocument);
    connect(saveWorkspaceAction, &QAction::triggered, this, [this] { saveWorkspaceDocument(false); });
    connect(saveWorkspaceAsAction, &QAction::triggered, this, [this] { saveWorkspaceDocument(true); });
    connect(addMediaAction, &QAction::triggered, this, [this] { addCatalogFiles(-1); });
    connect(addImageAction, &QAction::triggered, this, [this] { addCatalogFiles(static_cast<int>(ObsCatalogSourceType::ImageFile)); });
    connect(addRtspAction, &QAction::triggered, this, &ObsDualMediaTestWindow::addRtspSource);
    connect(m_openPlaylistButton, &QPushButton::clicked, this, [this] {
        if (m_playlistMode) {
            stopPlaylist();
        } else if (!m_playlist->empty()) {
            startPlaylist();
        } else {
            showPlaylistManager();
        }
    });
    connect(m_playlistPreviousButton, &QPushButton::clicked, this, [this] {
        navigatePlaylist(false, "Previous");
    });
    connect(m_playlistNextButton, &QPushButton::clicked, this, [this] {
        navigatePlaylist(true, "Next");
    });
    connect(m_catalogThumbnailSize, &QComboBox::currentIndexChanged, this, [this](int) {
        setCatalogThumbnailSize(m_catalogThumbnailSize->currentData().toInt());
    });
    connect(m_sourceTypeFilter, &QComboBox::currentIndexChanged, this, [this](int) { refreshCatalogUi(); });
    connect(m_projectFrameRate, &QComboBox::currentIndexChanged, this, &ObsDualMediaTestWindow::setProjectFrameRate);
    connect(m_sourceCatalogList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const auto source = m_sourceCatalog->find(item->data(Qt::UserRole).toULongLong());
        if (!source || m_fadeActive) return;
        stagePreviewSource(*source);
    });

    m_preview.backend = std::make_unique<ObsPlaybackBackend>(context);
    m_program.backend = std::make_unique<ObsPlaybackBackend>(context);
    m_program.backend->setRenderMode(m_selectedProgramRenderMode);
    m_playlist = std::make_unique<ObsPlaylist>();
    m_sourceCatalog = std::make_unique<ObsSourceCatalog>();
    m_workspaceStateStore = std::make_unique<WorkspaceStateStore>(
        std::filesystem::path(Config::getInstance().getWorkspacePath()) / "data" / "mediaswitcher-workspace.json");
    connect(&ObsThumbnailService::instance(), &ObsThumbnailService::thumbnailReady, this,
            [this](quint64 sourceId, const QImage& image, qint64 positionMs) {
        PlaybackSnapshot snapshot;
        snapshot.sourceId = static_cast<uint64_t>(sourceId);
        snapshot.positionMs = static_cast<int64_t>(positionMs);
        if (const auto source = m_sourceCatalog->find(snapshot.sourceId)) {
            snapshot.valid = true;
            snapshot.looping = false;
        }
        showSourceThumbnail(snapshot.sourceId, image, snapshot, false);
    });
    const uint64_t previewBlankId = m_sourceCatalog->addSystemBlank("Blank 1");
    const uint64_t programBlankId = m_sourceCatalog->addSystemBlank("Blank 2");
    refreshCatalogUi();
    m_preview.statusLabel->setText(localized("PVW trống", "PVW empty"));
    m_program.statusLabel->setText(localized("PGM trống", "PGM empty"));
    if (const auto blank = m_sourceCatalog->find(previewBlankId)) stagePreviewSource(*blank);
    if (const auto blank = m_sourceCatalog->find(programBlankId)) {
        m_program.backend->setAudioOutputEnabled(true);
        if (m_program.backend->open(*blank)) {
            setProgramSourceId(programBlankId);
            initializeDisplay(m_program);
        } else {
            LOG_ERROR("OBS app: Failed to open the initial Program Blank source.");
        }
    }
    const uint64_t restoredPreviewId = restoreWorkspace();
    if (const auto restoredPreview = m_sourceCatalog->find(restoredPreviewId)) {
        stagePreviewSource(*restoredPreview);
    } else {
        refreshCatalogUi();
    }
    refreshPlaylistButtonUi();

    if (!mediaPath.empty()) {
        if (!openPanel(m_preview, mediaPath, false) || !openPanel(m_program, mediaPath, true)) {
            closePanels();
            return;
        }
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this] {
        updatePanel(m_preview, QStringLiteral("PVW"));
        if (m_layoutMode == LayoutMode::Triple) {
            commitPendingZoneSwaps();
            updateTripleZonesUi();
        } else {
            updatePanel(m_program, QStringLiteral("PGM"));
            rememberProgramSnapshot(captureProgramSnapshot());
            finishFadeIfComplete();
            if (m_playlistMode && !m_fadeActive && !m_playlist->empty() && m_playlist->isAutoNext() && m_program.backend &&
                m_program.backend->hasEnded()) {
                navigatePlaylist(true, "Auto Next");
            }
        }
    });
    m_timer->start(100);
    m_previewSeekTimer = new QTimer(this);
    m_previewSeekTimer->setSingleShot(true);
    m_previewSeekTimer->setInterval(55);
    connect(m_previewSeekTimer, &QTimer::timeout, this, &ObsDualMediaTestWindow::applyStagedPreviewSeek);
    m_audioMeterTimer = new QTimer(this);
    m_audioMeterTimer->setTimerType(Qt::PreciseTimer);
    connect(m_audioMeterTimer, &QTimer::timeout, this, &ObsDualMediaTestWindow::updateAudioMeters);
    m_audioMeterTimer->start(16);
    m_processMetricsTimer = new QTimer(this);
    connect(m_processMetricsTimer, &QTimer::timeout, this, &ObsDualMediaTestWindow::updateProcessMetrics);
    m_processMetricsTimer->start(2000);
    applyLanguage();
    updateProcessMetrics();
    QTimer::singleShot(0, this, [this] {
        updateMonitorLayout();
        initializeDisplay(m_preview);
        initializeDisplay(m_program);
    });
}

ObsDualMediaTestWindow::~ObsDualMediaTestWindow() {
    // Thumbnail workers own short-lived libobs sources, so they must finish
    // before the window's ObsContext is allowed to shut libobs down.
    ObsThumbnailService::instance().cancelPendingAndWait();
    closePanels();
}

bool ObsDualMediaTestWindow::runCueOnlySmokeTest(const std::filesystem::path& mediaPath) {
    if (!std::filesystem::is_regular_file(mediaPath) || !m_sourceCatalog || !m_preview.backend) return false;

    const uint64_t firstId = m_sourceCatalog->add(mediaPath);
    const uint64_t secondId = m_sourceCatalog->add(mediaPath);
    const auto first = m_sourceCatalog->find(firstId);
    const auto second = m_sourceCatalog->find(secondId);
    if (!first || !second) return false;

    const auto waitForFrames = [](int milliseconds) {
        QEventLoop loop;
        QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
        loop.exec();
    };
    const auto waitForPaused = [&waitForFrames](ObsPlaybackBackend& backend) {
        for (int attempt = 0; attempt < 60; ++attempt) {
            backend.enforcePendingPause();
            obs_source_t* source = backend.nativeSource();
            const uint32_t sw = source ? std::max(obs_source_get_width(source), obs_source_get_base_width(source)) : 0;
            const uint32_t sh = source ? std::max(obs_source_get_height(source), obs_source_get_base_height(source)) : 0;
            if (backend.state() == ObsPlaybackState::Paused && sw > 0 && sh > 0) return true;
            waitForFrames(25);
        }
        return false;
    };
    const auto waitForPlaying = [&waitForFrames](ObsPlaybackBackend& backend) {
        for (int attempt = 0; attempt < 40; ++attempt) {
            if (backend.state() == ObsPlaybackState::Playing) return true;
            waitForFrames(25);
        }
        return false;
    };

    stagePreviewSource(*first);
    const bool firstPreloadWasCovered = m_preview.videoStack &&
        m_preview.videoStack->currentWidget() == m_preview.stagedFrameLabel;
    const bool firstReachedPause = waitForPaused(*m_preview.backend);
    waitForFrames(120);
    const PlaybackSnapshot firstCueSnapshot = capturePreviewSnapshot();
    const bool firstCueOnly = m_previewSourceId == firstId && m_stagedPreviewSnapshot.valid &&
        m_stagedPreviewSnapshot.sourceId == firstId && m_preview.backend->isOpen() &&
        firstPreloadWasCovered && firstReachedPause && !m_previewTransportStarted &&
        m_preview.videoStack->currentWidget() == m_preview.stagedFrameLabel &&
        firstCueSnapshot.positionMs == 0;

    stagePreviewSource(*second);
    const bool secondPreloadWasCovered = m_preview.videoStack &&
        m_preview.videoStack->currentWidget() == m_preview.stagedFrameLabel;
    const bool secondReachedPause = waitForPaused(*m_preview.backend);
    waitForFrames(120);
    const PlaybackSnapshot secondCueSnapshot = capturePreviewSnapshot();
    obs_source_t* secondPausedRuntime = m_preview.backend->nativeSource();
    const uint32_t pausedSourceWidth = secondPausedRuntime ? std::max(obs_source_get_width(secondPausedRuntime), obs_source_get_base_width(secondPausedRuntime)) : 0;
    const uint32_t pausedSourceHeight = secondPausedRuntime ? std::max(obs_source_get_height(secondPausedRuntime), obs_source_get_base_height(secondPausedRuntime)) : 0;
    const bool pausedNativeFrameReady = pausedSourceWidth > 0 && pausedSourceHeight > 0;
    const bool secondCueOnly = m_previewSourceId == secondId && m_stagedPreviewSnapshot.valid &&
        m_stagedPreviewSnapshot.sourceId == secondId && secondPausedRuntime &&
        secondPreloadWasCovered && secondReachedPause && !m_previewTransportStarted &&
        m_preview.videoStack->currentWidget() == m_preview.stagedFrameLabel &&
        secondCueSnapshot.positionMs == 0 &&
        pausedNativeFrameReady;

    // Regression test for the input-bank bug: a visible tile and its playhead
    // must be selected atomically, and a late add-time poster must not replace
    // that newer timeline snapshot.
    QImage timelineTile(640, 360, QImage::Format_ARGB32);
    timelineTile.fill(QColor(197, 39, 73));
    PlaybackSnapshot tileSnapshot;
    tileSnapshot.sourceId = secondId;
    tileSnapshot.positionMs = 12345;
    tileSnapshot.durationMs = 60000;
    tileSnapshot.valid = true;
    showSourceThumbnail(secondId, timelineTile, tileSnapshot, true);
    QImage latePoster(1280, 720, QImage::Format_ARGB32);
    latePoster.fill(QColor(17, 42, 68));
    PlaybackSnapshot latePosterSnapshot = tileSnapshot;
    latePosterSnapshot.positionMs = 500;
    showSourceThumbnail(secondId, latePoster, latePosterSnapshot, false);
    stagePreviewSource(*second);
    const QColor stagedCenter = m_stagedPreviewFrame.isNull() ? QColor() :
        m_stagedPreviewFrame.toImage().pixelColor(m_stagedPreviewFrame.width() / 2,
                                                   m_stagedPreviewFrame.height() / 2);
    const bool thumbnailSnapshotAtomic = m_stagedPreviewSnapshot.sourceId == secondId &&
        m_stagedPreviewSnapshot.positionMs == tileSnapshot.positionMs &&
        m_stagedPreviewFrameSourceId == secondId && stagedCenter == QColor(197, 39, 73) &&
        !m_previewTransportStarted && m_preview.videoStack->currentWidget() == m_preview.stagedFrameLabel;
    secondPausedRuntime = m_preview.backend->nativeSource();
    LOG_INFO("OBS cue-only smoke: static cue details first covered={} ready={} cue={} ms; second covered={} ready={} cue={} ms.",
             firstPreloadWasCovered, firstReachedPause, firstCueSnapshot.positionMs,
             secondPreloadWasCovered, secondReachedPause, secondCueSnapshot.positionMs);
    LOG_INFO("OBS cue-only smoke: thumbnail frame/time atomic={} cue={} ms color=({}, {}, {}).",
             thumbnailSnapshotAtomic, m_stagedPreviewSnapshot.positionMs,
             stagedCenter.red(), stagedCenter.green(), stagedCenter.blue());
    const bool transitionOpenedProgram = promotePreviewToProgram("Quick Play") &&
        m_programSourceId == secondId && m_program.backend && m_program.backend->isOpen() &&
        m_program.backend->nativeSource() == secondPausedRuntime;
    stagePreviewSource(*first);
    togglePanelPlayback(m_preview);
    const bool explicitPlayOpenedDecoder = waitForPlaying(*m_preview.backend) &&
        m_previewTransportStarted && m_preview.videoStack->currentWidget() == m_preview.videoSurface;
    obs_source_t* previewRuntime = m_preview.backend->nativeSource();
    const bool runtimeTransferredWithoutRecreate = previewRuntime && promotePreviewToProgram("Quick Play") &&
        m_program.backend->nativeSource() == previewRuntime;
    int64_t programDuration = 0;
    for (int attempt = 0; attempt < 40 && programDuration <= 20000; ++attempt) {
        programDuration = m_program.backend->durationMs();
        if (programDuration <= 20000) waitForFrames(25);
    }
    bool nonZeroProgramCueReached = false;
    if (programDuration > 20000 && m_program.backend->supportsTransport()) {
        const int64_t requestedCue = std::min<int64_t>(506000, programDuration / 2);
        m_program.backend->seekMs(requestedCue);
        waitForFrames(300);
        for (int attempt = 0; attempt < 40; ++attempt) {
            // OBS seeks a local H.264 file to its previous keyframe. The test
            // only needs a realistically deep running position here; CUT must
            // preserve the actual decoder position without issuing another
            // keyframe seek.
            if (m_program.backend->positionMs() >= requestedCue - 5000) {
                nonZeroProgramCueReached = true;
                break;
            }
            waitForFrames(25);
        }
    }
    stagePreviewSource(*second);
    const bool secondPreviewStarted = waitForPaused(*m_preview.backend);
    obs_source_t* programBeforeCut = m_program.backend->nativeSource();
    obs_source_t* previewBeforeCut = m_preview.backend->nativeSource();
    const int64_t programPositionBeforeCut = m_program.backend->positionMs();
    const int64_t previewPositionBeforeCut = capturePreviewSnapshot().positionMs;
    const bool cutCompleted = secondPreviewStarted && promotePreviewToProgram("CUT");
    const int64_t programCutDelta = m_program.backend->positionMs() - previewPositionBeforeCut;
    const int64_t previewCutDelta = m_preview.backend->positionMs() - programPositionBeforeCut;
    const bool cutPreservedBothRuntimes = cutCompleted &&
        m_program.backend->nativeSource() == previewBeforeCut && m_preview.backend->nativeSource() == programBeforeCut &&
        std::abs(programCutDelta) <= 250 && std::abs(previewCutDelta) <= 250;
    const bool cutFrozenFrameMatchesClock = cutCompleted && !m_previewTransportStarted &&
        m_stagedPreviewSnapshot.sourceId == m_previewSourceId &&
        m_stagedPreviewFrameSourceId == m_previewSourceId && !m_stagedPreviewFrame.isNull() &&
        m_preview.videoStack->currentWidget() == m_preview.stagedFrameLabel;
    LOG_INFO("OBS cue-only smoke: CUT clock deltas PGM={} ms PVW={} ms.", programCutDelta, previewCutDelta);
    obs_source_t* previewBeforeFade = m_preview.backend->nativeSource();
    obs_source_t* programBeforeFade = m_program.backend->nativeSource();
    const bool fadePreservedRuntime = previewBeforeFade && fadePreviewToProgram() &&
        m_program.backend->nativeSource() == previewBeforeFade;
    waitForFrames(1100);
    const bool fadeFrozenFrameMatchesClock = !m_fadeActive && programBeforeFade &&
        m_preview.backend->nativeSource() == programBeforeFade && !m_previewTransportStarted &&
        m_stagedPreviewSnapshot.sourceId == m_previewSourceId &&
        m_stagedPreviewFrameSourceId == m_previewSourceId && !m_stagedPreviewFrame.isNull() &&
        m_preview.videoStack->currentWidget() == m_preview.stagedFrameLabel;

    auto cachedRuntime = std::make_unique<ObsPlaybackBackend>(m_context);
    cachedRuntime->setAudioOutputEnabled(false);
    bool cachedRuntimeRestoredWithoutSeek = false;
    if (cachedRuntime->open(*first, true) && waitForPaused(*cachedRuntime)) {
        cachedRuntime->seekMs(12345);
        waitForFrames(150);
        cachedRuntime->enforcePendingPause();
        PlaybackSnapshot cachedSnapshot;
        cachedSnapshot.sourceId = firstId;
        cachedSnapshot.positionMs = cachedRuntime->positionMs();
        cachedSnapshot.durationMs = cachedRuntime->durationMs();
        cachedSnapshot.looping = cachedRuntime->isLooping();
        cachedSnapshot.valid = true;
        const QImage cachedFrame = captureBackendStill(*cachedRuntime);
        if (!cachedFrame.isNull()) showSourceThumbnail(firstId, cachedFrame, cachedSnapshot, true);
        obs_source_t* cachedNativeSource = cachedRuntime->nativeSource();
        m_pausedSourceRuntimes[firstId] = std::move(cachedRuntime);
        stagePreviewSnapshot(cachedSnapshot);
        cachedRuntimeRestoredWithoutSeek = m_preview.backend->nativeSource() == cachedNativeSource &&
            m_previewRuntimeMatchesStagedFrame && !m_previewTransportStarted &&
            std::abs(m_preview.backend->positionMs() - cachedSnapshot.positionMs) <= 100;
    }
    bool pausedSeekRenderedWithoutAutoplay = false;
    if (cachedRuntimeRestoredWithoutSeek && m_stagedPreviewSnapshot.durationMs > 10000) {
        const int64_t seekTarget = std::min<int64_t>(
            m_stagedPreviewSnapshot.durationMs - 1000, m_stagedPreviewSnapshot.positionMs + 7000);
        requestStagedPreviewSeek(seekTarget);
        waitForFrames(350);
        pausedSeekRenderedWithoutAutoplay = m_stagedPreviewSnapshot.positionMs == seekTarget &&
            m_stagedPreviewFrameSourceId == firstId && !m_stagedPreviewFrame.isNull() &&
            m_previewRuntimeMatchesStagedFrame && !m_previewTransportStarted &&
            m_preview.backend->state() == ObsPlaybackState::Paused &&
            m_preview.videoStack->currentWidget() == m_preview.stagedFrameLabel;
    }
    bool replayAfterEndedWithoutRestage = false;
    if (pausedSeekRenderedWithoutAutoplay && m_preview.backend->durationMs() > 1000) {
        const int64_t duration = m_preview.backend->durationMs();
        m_preview.backend->seekMs(duration - 100);
        m_preview.backend->play();
        bool reachedEnd = false;
        for (int attempt = 0; attempt < 480; ++attempt) {
            if (m_preview.backend->hasEnded() || m_preview.backend->state() == ObsPlaybackState::Ended) {
                reachedEnd = true;
                break;
            }
            waitForFrames(25);
        }
        if (reachedEnd) {
            m_preview.backend->play();
            const bool restarted = waitForPlaying(*m_preview.backend);
            waitForFrames(120);
            replayAfterEndedWithoutRestage = restarted && m_preview.backend->positionMs() < 3000;
        }
    }
    updateMonitorLayout();
    waitForFrames(50);
    const QSize previewVideoSize = m_preview.videoContainer->size();
    const QSize programVideoSize = m_program.videoContainer->size();
    const auto isSixteenByNine = [](const QSize& size) {
        return size.height() > 0 &&
            std::abs(static_cast<double>(size.width()) / size.height() - 16.0 / 9.0) <= 0.03;
    };
    const bool balancedMonitorLayout =
        std::abs(previewVideoSize.width() - programVideoSize.width()) <= 2 &&
        previewVideoSize.height() == programVideoSize.height() &&
        isSixteenByNine(previewVideoSize) && isSixteenByNine(programVideoSize);
    LOG_INFO("OBS cue-only smoke: monitor layout PVW={}x{} PGM={}x{} balanced={}.",
             previewVideoSize.width(), previewVideoSize.height(),
             programVideoSize.width(), programVideoSize.height(), balancedMonitorLayout);
    LOG_INFO("OBS cue-only smoke: paused source runtime restored without seek={}.",
             cachedRuntimeRestoredWithoutSeek);
    LOG_INFO("OBS cue-only smoke: paused PVW seek rendered without autoplay={}.",
             pausedSeekRenderedWithoutAutoplay);
    LOG_INFO("OBS cue-only smoke: replay after ENDED without thumbnail restage={}.",
             replayAfterEndedWithoutRestage);
    LOG_INFO("OBS cue-only smoke: firstStaticCue={} secondStaticCue={} transition={} explicitPlay={} runtimeIdentity={} nonZeroCue={} cutIdentity={} cutFrame={} fadeIdentity={} fadeFrame={}.",
             firstCueOnly, secondCueOnly, transitionOpenedProgram, explicitPlayOpenedDecoder,
             runtimeTransferredWithoutRecreate, nonZeroProgramCueReached,
             cutPreservedBothRuntimes, cutFrozenFrameMatchesClock,
             fadePreservedRuntime, fadeFrozenFrameMatchesClock);
    LOG_INFO("OBS cue-only smoke: hidden preload native frame={}x{}.", pausedSourceWidth, pausedSourceHeight);
    return firstCueOnly && secondCueOnly && thumbnailSnapshotAtomic &&
        transitionOpenedProgram && explicitPlayOpenedDecoder &&
        runtimeTransferredWithoutRecreate && nonZeroProgramCueReached &&
        cutPreservedBothRuntimes && cutFrozenFrameMatchesClock &&
        fadePreservedRuntime && fadeFrozenFrameMatchesClock && cachedRuntimeRestoredWithoutSeek &&
        pausedSeekRenderedWithoutAutoplay && replayAfterEndedWithoutRestage &&
        balancedMonitorLayout;
}

QString ObsDualMediaTestWindow::localized(const char* vietnamese, const char* english) const {
    return QString::fromUtf8(m_language == UiLanguage::Vietnamese ? vietnamese : english);
}

void ObsDualMediaTestWindow::bindLocalizedProperty(QObject* object, const char* propertyName,
                                                   const char* vietnamese, const char* english) {
    if (!object || !propertyName) return;
    const QByteArray property(propertyName);
    object->setProperty(QByteArray("i18n_" + property + "_vi").constData(), QString::fromUtf8(vietnamese));
    object->setProperty(QByteArray("i18n_" + property + "_en").constData(), QString::fromUtf8(english));
    object->setProperty(propertyName, localized(vietnamese, english));
}

void ObsDualMediaTestWindow::applyLanguage() {
    const auto applyObject = [this](QObject* object) {
        if (!object) return;
        for (const QByteArray property : {QByteArray("text"), QByteArray("toolTip"),
                                          QByteArray("accessibleName"), QByteArray("windowTitle")}) {
            const QByteArray key = "i18n_" + property +
                (m_language == UiLanguage::Vietnamese ? "_vi" : "_en");
            const QVariant value = object->property(key.constData());
            if (value.isValid()) object->setProperty(property.constData(), value);
        }
    };

    applyObject(this);
    for (QObject* object : findChildren<QObject*>()) applyObject(object);

    if (m_catalogThumbnailSize && m_catalogThumbnailSize->count() >= 3) {
        m_catalogThumbnailSize->setItemText(0, localized("Nhỏ", "Small"));
        m_catalogThumbnailSize->setItemText(1, localized("Vừa", "Normal"));
        m_catalogThumbnailSize->setItemText(2, localized("Lớn", "Large"));
    }
    if (m_sourceTypeFilter && m_sourceTypeFilter->count() >= 6) {
        m_sourceTypeFilter->setItemText(0, localized("Tất cả", "All types"));
        m_sourceTypeFilter->setItemText(1, localized("Video", "Video"));
        m_sourceTypeFilter->setItemText(2, localized("Âm thanh", "Audio"));
        m_sourceTypeFilter->setItemText(3, localized("Hình ảnh", "Images"));
        m_sourceTypeFilter->setItemText(4, localized("Camera RTSP", "RTSP cameras"));
        m_sourceTypeFilter->setItemText(5, localized("Nền trống", "Blank"));
    }
    if (m_programRenderMode && m_programRenderMode->count() >= 2) {
        m_programRenderMode->setItemText(0, localized("Mặc định", "Default"));
        m_programRenderMode->setItemText(1, localized("Vừa màn hình", "Fit"));
    }
    if (m_layoutModeSelector && m_layoutModeSelector->count() >= 2) {
        m_layoutModeSelector->setItemText(0, localized("1 vùng (Single)", "1 Zone (Single)"));
        m_layoutModeSelector->setItemText(1, localized("3 vùng (Triple)", "3 Zones (Triple)"));
    }
    for (auto& zone : m_tripleZones) {
        if (zone.fitCombo && zone.fitCombo->count() >= 3) {
            zone.fitCombo->setItemText(0, localized("Vừa (Fit)", "Fit"));
            zone.fitCombo->setItemText(1, localized("Lấp đầy (Fill)", "Fill"));
            zone.fitCombo->setItemText(2, localized("Kéo giãn (Stretch)", "Stretch"));
        }
    }
    updateInputToolbarPresentation();

    if (m_sourceCatalog) refreshCatalogUi();
    refreshPlaylistButtonUi();
    if (m_playlistDialog) refreshPlaylistUi();
    if (m_programOutput) {
        m_programOutput->setWindowTitle(localized("MediaSwitcher - Output chương trình",
                                                  "MediaSwitcher Program Output"));
    }
    updatePanel(m_preview, QStringLiteral("PVW"));
    if (m_layoutMode == LayoutMode::Triple) updateTripleZonesUi();
    else updatePanel(m_program, QStringLiteral("PGM"));
    updateCopyrightDisplay();
    updateProcessMetrics();
}

void ObsDualMediaTestWindow::setLanguage(int index) {
    if (!m_languageSelector || index < 0) return;
    const UiLanguage requested = m_languageSelector->itemData(index).toString() == QStringLiteral("en")
        ? UiLanguage::English : UiLanguage::Vietnamese;
    if (requested == m_language) return;
    m_language = requested;
    QSettings().setValue(QStringLiteral("ui/language"),
                         m_language == UiLanguage::Vietnamese ? QStringLiteral("vi") : QStringLiteral("en"));
    applyLanguage();
    LOG_INFO("OBS UI language changed to {}.", m_language == UiLanguage::Vietnamese ? "VI" : "EN");
}

void ObsDualMediaTestWindow::closeEvent(QCloseEvent* event) {
    m_closing = true;
    if (m_timer) m_timer->stop();
    saveWorkspace();
    closePanels();
    event->accept();
}

void ObsDualMediaTestWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this] {
        updateMonitorLayout();
        initializeDisplay(m_preview);
        initializeDisplay(m_program);
    });
}

void ObsDualMediaTestWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateMonitorLayout();
    if (m_layoutRefreshQueued) return;
    m_layoutRefreshQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_layoutRefreshQueued = false;
        if (m_closing) return;
        resizeDisplay(m_preview);
        resizeDisplay(m_program);
        updateStagedPreviewFrame();
    });
}

void ObsDualMediaTestWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange || m_closing) return;
    QTimer::singleShot(0, this, [this] {
        updateMonitorLayout();
        resizeDisplay(m_preview);
        resizeDisplay(m_program);
        updateStagedPreviewFrame();
    });
}

void ObsDualMediaTestWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_F11: toggleFullscreen(); break;
    case Qt::Key_Space: togglePanelPlayback(m_preview); break;
    case Qt::Key_Return:
        if (m_layoutMode == LayoutMode::Triple) toggleZonePlayback(m_activeZoneIndex);
        else togglePanelPlayback(m_program);
        break;
    case Qt::Key_C:
        if (m_layoutMode == LayoutMode::Triple) promotePreviewToZone(m_activeZoneIndex, "CUT");
        else promotePreviewToProgram("CUT");
        break;
    case Qt::Key_F:
        if (m_layoutMode == LayoutMode::Triple) promotePreviewToZone(m_activeZoneIndex, "CUT");
        else fadePreviewToProgram();
        break;
    case Qt::Key_1:
        if (m_layoutMode == LayoutMode::Triple) selectActiveZone(0);
        break;
    case Qt::Key_2:
        if (m_layoutMode == LayoutMode::Triple) selectActiveZone(1);
        break;
    case Qt::Key_3:
        if (m_layoutMode == LayoutMode::Triple) selectActiveZone(2);
        break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void ObsDualMediaTestWindow::draw(void* parameter, uint32_t width, uint32_t height) {
    auto* panel = static_cast<Panel*>(parameter);
    if (!panel || !panel->owner) return;
    if (panel == &panel->owner->m_program && panel->owner->m_layoutMode == LayoutMode::Triple) {
        panel->owner->renderProgramCanvas(width, height, true);
        return;
    }
    if (!panel->backend) return;
    panel->backend->render(width, height);
    captureSourceThumbnail(*panel);
}

void ObsDualMediaTestWindow::captureSourceThumbnail(Panel& panel) {
    constexpr uint32_t thumbnailWidth = 320;
    constexpr uint32_t thumbnailHeight = 180;
    // Capture every render frame. The three-surface GPU ring and pending-update
    // coalescing keep readback asynchronous, so UI load sheds frames naturally
    // instead of making the live thumbnail advance in visible six-frame jumps.
    constexpr uint32_t captureEveryFrames = 1;

    if (!panel.owner || !panel.thumbnailCaptureEnabled.load() || panel.thumbnailUpdatePending.load() ||
        !panel.backend || !panel.backend->isOpen()) return;
    if ((panel.thumbnailFrameCounter.fetch_add(1) + 1U) % captureEveryFrames != 0) return;

    if (!panel.thumbnailTexrender) {
        panel.thumbnailTexrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
        panel.thumbnailStages[0] = gs_stagesurface_create(thumbnailWidth, thumbnailHeight, GS_BGRA);
        panel.thumbnailStages[1] = gs_stagesurface_create(thumbnailWidth, thumbnailHeight, GS_BGRA);
        panel.thumbnailStages[2] = gs_stagesurface_create(thumbnailWidth, thumbnailHeight, GS_BGRA);
        if (!panel.thumbnailTexrender || !panel.thumbnailStages[0] || !panel.thumbnailStages[1] || !panel.thumbnailStages[2]) {
            LOG_ERROR("OBS thumbnail: Failed to allocate frame capture textures.");
            return;
        }
        LOG_INFO("OBS thumbnail: GPU frame tap initialized at {}x{}.", thumbnailWidth, thumbnailHeight);
    }

    // gs_texrender is single-use until reset.  Without this call only the
    // first capture can begin and every later live thumbnail frame is dropped.
    gs_texrender_reset(panel.thumbnailTexrender);
    if (!gs_texrender_begin(panel.thumbnailTexrender, thumbnailWidth, thumbnailHeight)) return;
    const vec4 black{};
    gs_clear(GS_CLEAR_COLOR, &black, 0.0f, 0);
    panel.backend->render(thumbnailWidth, thumbnailHeight);
    gs_texrender_end(panel.thumbnailTexrender);

    gs_texture_t* texture = gs_texrender_get_texture(panel.thumbnailTexrender);
    if (!texture) return;
    const uint32_t writeStage = panel.thumbnailWriteStage;
    gs_stage_texture(panel.thumbnailStages[writeStage], texture);
    // Timestamp the same GPU-ring slot as the pixels. Reading position later
    // on the UI thread pairs a newer playhead with an older staged frame.
    panel.thumbnailStagePositionMs[writeStage] = panel.backend->positionMs();
    panel.thumbnailStageDurationMs[writeStage] = panel.backend->durationMs();
    panel.thumbnailStageLooping[writeStage] = panel.backend->isLooping();
    const uint32_t stagedFrameCount = panel.thumbnailStagedFrameCount.fetch_add(1) + 1U;

    // Do not map a staging surface until every slot in the asynchronous ring
    // has received a frame.  Mapping an untouched slot can produce a black
    // first thumbnail, especially for still images captured only once.
    if (stagedFrameCount < 3U) {
        panel.thumbnailWriteStage = (panel.thumbnailWriteStage + 1U) % 3U;
        return;
    }

    // D3D11 staging reads are asynchronous.  Keep two full render intervals
    // between stage and map so the UI never blocks waiting for the GPU.
    const uint32_t readyStage = (panel.thumbnailWriteStage + 1U) % 3U;
    uint8_t* data = nullptr;
    uint32_t lineSize = 0;
    if (gs_stagesurface_map(panel.thumbnailStages[readyStage], &data, &lineSize)) {
        QImage frame(static_cast<int>(thumbnailWidth), static_cast<int>(thumbnailHeight), QImage::Format_ARGB32);
        for (uint32_t row = 0; row < thumbnailHeight; ++row) {
            std::memcpy(frame.scanLine(static_cast<int>(row)), data + static_cast<size_t>(row) * lineSize,
                        static_cast<size_t>(thumbnailWidth) * 4U);
        }
        gs_stagesurface_unmap(panel.thumbnailStages[readyStage]);
        const uint64_t sourceId = panel.thumbnailSourceId.load();
        const uint64_t generation = panel.thumbnailGeneration.load();
        const int64_t positionMs = panel.thumbnailStagePositionMs[readyStage];
        const int64_t durationMs = panel.thumbnailStageDurationMs[readyStage];
        const bool looping = panel.thumbnailStageLooping[readyStage];
        if (!panel.thumbnailUpdatePending.exchange(true)) {
            QMetaObject::invokeMethod(panel.owner,
                [owner = panel.owner, panelPtr = &panel, sourceId, generation, positionMs, durationMs, looping,
                 frame = std::move(frame)]() mutable {
                    if (panelPtr->thumbnailGeneration.load() != generation ||
                        panelPtr->thumbnailSourceId.load() != sourceId) {
                        panelPtr->thumbnailUpdatePending.store(false);
                        return;
                    }
                    if (!owner->m_closing) {
                        PlaybackSnapshot snapshot;
                        snapshot.sourceId = sourceId;
                        snapshot.positionMs = positionMs;
                        snapshot.durationMs = durationMs;
                        snapshot.looping = looping;
                        snapshot.valid = sourceId != 0 && owner->isCatalogSourceAvailable(sourceId);
                        owner->showSourceThumbnail(sourceId, frame, snapshot, true);
                    }
                    panelPtr->thumbnailUpdatePending.store(false);
                    if (!panelPtr->thumbnailCaptureContinuous.load()) {
                        panelPtr->thumbnailCaptureEnabled.store(false);
                    }
                }, Qt::QueuedConnection);
        }
    }
    panel.thumbnailWriteStage = (panel.thumbnailWriteStage + 1U) % 3U;
}

QImage ObsDualMediaTestWindow::captureBackendStill(const ObsPlaybackBackend& backend) const {
    constexpr uint32_t captureWidth = 1280;
    constexpr uint32_t captureHeight = 720;
    if (!backend.isOpen() || !backend.nativeSource()) return {};

    QImage image;
    obs_enter_graphics();
    gs_texrender_t* texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    gs_stagesurf_t* stage = gs_stagesurface_create(captureWidth, captureHeight, GS_BGRA);
    if (texrender && stage && gs_texrender_begin(texrender, captureWidth, captureHeight)) {
        const vec4 black{};
        gs_clear(GS_CLEAR_COLOR, &black, 0.0f, 0);
        backend.render(captureWidth, captureHeight);
        gs_texrender_end(texrender);
        if (gs_texture_t* texture = gs_texrender_get_texture(texrender)) {
            // This readback intentionally synchronizes once at the transition
            // boundary. Unlike the live thumbnail ring, PVW must receive the
            // exact frame that was visible on PGM with the captured timestamp.
            gs_stage_texture(stage, texture);
            gs_flush();
            uint8_t* mappedData = nullptr;
            uint32_t lineSize = 0;
            if (gs_stagesurface_map(stage, &mappedData, &lineSize)) {
                image = QImage(static_cast<int>(captureWidth), static_cast<int>(captureHeight),
                               QImage::Format_ARGB32);
                for (uint32_t row = 0; row < captureHeight; ++row) {
                    std::memcpy(image.scanLine(static_cast<int>(row)),
                                mappedData + static_cast<size_t>(row) * lineSize,
                                static_cast<size_t>(captureWidth) * 4U);
                }
                gs_stagesurface_unmap(stage);
            }
        }
    }
    if (stage) gs_stagesurface_destroy(stage);
    if (texrender) gs_texrender_destroy(texrender);
    obs_leave_graphics();
    return image;
}

QWidget* ObsDualMediaTestWindow::createPanel(Panel& panel, const QString& title, const QString& color) {
    panel.owner = this;
    auto* group = new QWidget(this);
    group->setObjectName(QStringLiteral("monitorPanel"));
    group->setStyleSheet(QStringLiteral("#monitorPanel { background: #11161b; border: 0; }"));
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    auto* header = new QWidget(group);
    header->setObjectName(QStringLiteral("monitorHeader"));
    header->setFixedHeight(26);
    header->setStyleSheet(QStringLiteral("#monitorHeader { background: %1; border: 0; }").arg(color));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(7, 0, 6, 0);
    panel.sourceLabel = new QLabel(title, header);
    bindLocalizedProperty(panel.sourceLabel, "text",
                          &panel == &m_preview ? "XEM TRƯỚC (PVW) - TẠM DỪNG" : "CHƯƠNG TRÌNH (PGM) - ÂM THANH TRỰC TIẾP",
                          &panel == &m_preview ? "PREVIEW (PVW) - PAUSED" : "PROGRAM (PGM) - LIVE AUDIO");
    panel.sourceLabel->setStyleSheet(QStringLiteral("color: #f1f6f8; font-weight: bold; border: 0;"));
    panel.sourceLabel->setMinimumWidth(0);
    panel.sourceLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerLayout->addWidget(panel.sourceLabel, 1);
    layout->addWidget(header);

    panel.videoContainer = new QWidget(group);
    panel.videoStack = new QStackedLayout(panel.videoContainer);
    panel.videoStack->setContentsMargins(0, 0, 0, 0);
    panel.videoSurface = new QWidget(panel.videoContainer);
    panel.videoSurface->setAttribute(Qt::WA_NativeWindow);
    panel.videoSurface->setStyleSheet(QStringLiteral("background: black;"));
    panel.stagedFrameLabel = new QLabel(panel.videoContainer);
    panel.stagedFrameLabel->setAlignment(Qt::AlignCenter);
    panel.stagedFrameLabel->setStyleSheet(QStringLiteral("background: black; color: #9AA4B2;"));
    panel.stagedFrameLabel->clear();
    // QLabel normally contributes its pixmap size to sizeHint(). A 1280x720
    // PVW still could therefore steal width from PGM and squeeze the live
    // monitor. Ignore that intrinsic size; the switcher layout owns geometry.
    panel.stagedFrameLabel->setMinimumSize(0, 0);
    panel.videoStack->addWidget(panel.stagedFrameLabel);
    panel.videoStack->addWidget(panel.videoSurface);
    panel.videoContainer->setMinimumSize(0, 0);
    panel.videoContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    panel.videoSurface->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    panel.stagedFrameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    layout->addWidget(panel.videoContainer, 1);

    auto* controlsWidget = new QWidget(group);
    controlsWidget->setObjectName(QStringLiteral("monitorControls"));
    controlsWidget->setAttribute(Qt::WA_NativeWindow);
    controlsWidget->setAttribute(Qt::WA_AlwaysStackOnTop);
    controlsWidget->setFixedHeight(42);
    controlsWidget->setStyleSheet(QStringLiteral("#monitorControls { background: #11161b; border: 0; }"));
    auto* controls = new QHBoxLayout(controlsWidget);
    controls->setContentsMargins(0, 5, 0, 5);
    controls->setSpacing(4);
    panel.playPauseButton = new QPushButton(group);
    panel.playPauseButton->setFixedSize(32, 30);
    panel.playPauseButton->setIcon(group->style()->standardIcon(QStyle::SP_MediaPlay));
    panel.playPauseButton->setToolTip(QStringLiteral("Phát PVW/PGM"));
    bindLocalizedProperty(panel.playPauseButton, "toolTip", "Phát/Tạm dừng", "Play/Pause");
    panel.loopButton = new QPushButton(group);
    panel.loopButton->setFixedSize(32, 30);
    panel.loopButton->setIcon(group->style()->standardIcon(QStyle::SP_BrowserReload));
    panel.loopButton->setToolTip(QStringLiteral("Bật/tắt lặp lại"));
    bindLocalizedProperty(panel.loopButton, "toolTip", "Bật/tắt lặp lại", "Toggle loop");
    panel.loopButton->setProperty("loopActive", false);
    panel.resetButton = new QPushButton(group);
    panel.resetButton->setFixedSize(32, 30);
    panel.resetButton->setIcon(group->style()->standardIcon(QStyle::SP_MediaSkipBackward));
    panel.resetButton->setToolTip(QStringLiteral("Reset về đầu"));
    bindLocalizedProperty(panel.resetButton, "toolTip", "Đặt lại về đầu", "Reset to start");
    panel.seekBackButton = new QPushButton(group);
    panel.seekBackButton->setFixedSize(32, 30);
    panel.seekBackButton->setIcon(group->style()->standardIcon(QStyle::SP_MediaSeekBackward));
    panel.seekBackButton->setToolTip(QStringLiteral("Lùi 10 giây"));
    bindLocalizedProperty(panel.seekBackButton, "toolTip", "Lùi 10 giây", "Seek back 10 seconds");
    panel.seekForwardButton = new QPushButton(group);
    panel.seekForwardButton->setFixedSize(32, 30);
    panel.seekForwardButton->setIcon(group->style()->standardIcon(QStyle::SP_MediaSeekForward));
    panel.seekForwardButton->setToolTip(QStringLiteral("Tới 10 giây"));
    bindLocalizedProperty(panel.seekForwardButton, "toolTip", "Tiến 10 giây", "Seek forward 10 seconds");
    panel.seekSlider = new QSlider(Qt::Horizontal, group);
    panel.seekSlider->setRange(0, 1000);
    panel.timeLabel = new QLabel(QStringLiteral("00:00 / --:--"), group);
    panel.timeLabel->setStyleSheet(QStringLiteral(
        "color: #a9e9f7; border: 0; font-family: Consolas; font-size: 11px; font-weight: bold; padding: 0 2px;"));
    panel.timeLabel->setFixedWidth(114);
    panel.timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    panel.statusLabel = new QLabel(group);
    panel.volumeSlider = new QSlider(Qt::Horizontal, group);
    panel.volumeSlider->setRange(0, 100);
    panel.volumeSlider->setValue(100);
    panel.volumeSlider->setFixedWidth(58);
    panel.volumeSlider->setToolTip(QStringLiteral("Âm lượng"));
    bindLocalizedProperty(panel.volumeSlider, "toolTip", "Âm lượng", "Volume");
    panel.muteButton = new QPushButton(group);
    panel.muteButton->setCheckable(true);
    panel.muteButton->setFixedSize(26, 24);
    panel.muteButton->setIconSize(QSize(14, 14));
    panel.muteButton->setIcon(group->style()->standardIcon(QStyle::SP_MediaVolume));
    panel.muteButton->setToolTip(QStringLiteral("Tắt tiếng"));
    bindLocalizedProperty(panel.muteButton, "toolTip", "Tắt tiếng", "Mute");
    panel.muteButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #202b33; border: 0; border-radius: 3px; padding: 1px; }"
        "QPushButton:hover { background: #29414c; }"
        "QPushButton:checked { background: #642d3a; }"));
    panel.volumeValueLabel = new QLabel(QStringLiteral("100%"), group);
    panel.volumeValueLabel->setFixedWidth(26);
    panel.volumeValueLabel->setStyleSheet(QStringLiteral(
        "color: #79dce9; border: 0; font-size: 9px; font-weight: bold;"));
    panel.audioMeter = new AudioMeterWidget(group);
    panel.audioMeter->setToolTip(QStringLiteral("Mức âm thanh stereo L/R"));
    bindLocalizedProperty(panel.audioMeter, "toolTip", "Mức âm thanh stereo L/R", "Stereo L/R audio level");
    controls->addWidget(panel.seekSlider, 1);
    controls->addWidget(panel.timeLabel);
    controls->addWidget(panel.seekBackButton);
    controls->addWidget(panel.seekForwardButton);
    controls->addWidget(panel.loopButton);
    controls->addWidget(panel.resetButton);
    controls->addWidget(panel.playPauseButton);
    panel.statusLabel->hide();
    layout->addWidget(controlsWidget);

    connect(panel.playPauseButton, &QPushButton::clicked, this, [this, &panel] { togglePanelPlayback(panel); });
    connect(panel.loopButton, &QPushButton::clicked, this, [this, &panel] { togglePanelLoop(panel); });
    connect(panel.resetButton, &QPushButton::clicked, this, [this, &panel] { resetPanel(panel); });
    connect(panel.seekBackButton, &QPushButton::clicked, this, [this, &panel] { seekPanel(panel, -10000); });
    connect(panel.seekForwardButton, &QPushButton::clicked, this, [this, &panel] { seekPanel(panel, 10000); });
    connect(panel.volumeSlider, &QSlider::valueChanged, this, [this, &panel](int value) {
        panel.volume = static_cast<float>(value) / 100.0f;
        panel.volumeValueLabel->setText(QStringLiteral("%1%").arg(value));
        if (panel.backend) panel.backend->setVolume(panel.audioMuted ? 0.0f : panel.volume);
        if (&panel == &m_program && m_layoutMode == LayoutMode::Triple) {
            for (auto& z : m_tripleZones) {
                z.volume = panel.volume;
                if (z.activeBackend) z.activeBackend->setVolume((panel.audioMuted || z.audioMuted) ? 0.0f : z.volume);
            }
        }
    });
    connect(panel.muteButton, &QPushButton::toggled, this, [this, &panel](bool muted) {
        panel.audioMuted = muted;
        panel.muteButton->setIcon(style()->standardIcon(muted ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume));
        panel.muteButton->setToolTip(muted ? localized("Bật tiếng", "Unmute") : localized("Tắt tiếng", "Mute"));
        if (panel.backend) panel.backend->setVolume(muted ? 0.0f : panel.volume);
        if (&panel == &m_program && m_layoutMode == LayoutMode::Triple) {
            for (auto& z : m_tripleZones) {
                if (z.activeBackend) z.activeBackend->setVolume((muted || z.audioMuted) ? 0.0f : z.volume);
            }
        }
    });
    connect(panel.seekSlider, &QSlider::sliderPressed, this, [&panel] { panel.sliderDragging = true; });
    connect(panel.seekSlider, &QSlider::sliderMoved, this, [this, &panel](int value) {
        if (&panel != &m_preview || m_previewTransportStarted ||
            !m_stagedPreviewSnapshot.valid || m_stagedPreviewSnapshot.durationMs <= 0) return;
        requestStagedPreviewSeek(m_stagedPreviewSnapshot.durationMs * value / 1000);
    });
    connect(panel.seekSlider, &QSlider::sliderReleased, this, [this, &panel] {
        panel.sliderDragging = false;
        if (&panel == &m_preview &&
            (!panel.backend || !panel.backend->isOpen() || !m_previewTransportStarted)) {
            if (m_stagedPreviewSnapshot.valid && m_stagedPreviewSnapshot.durationMs > 0) {
                requestStagedPreviewSeek(
                    m_stagedPreviewSnapshot.durationMs * panel.seekSlider->value() / 1000);
            }
            return;
        }
        if (!panel.backend) return;
        const int64_t duration = panel.backend->durationMs();
        if (duration > 0) panel.backend->seekMs(duration * panel.seekSlider->value() / 1000);
    });
    return group;
}

bool ObsDualMediaTestWindow::openPanel(Panel& panel, const std::filesystem::path& mediaPath, bool audioOutput) {
    panel.backend->setAudioOutputEnabled(audioOutput);
    if (panel.backend->open(mediaPath, !audioOutput)) return true;
    panel.statusLabel->setText(localized("Không thể tạo nguồn media OBS.", "Could not create the OBS media source."));
    return false;
}

void ObsDualMediaTestWindow::initializeDisplay(Panel& panel) {
    if (panel.display) {
        if (panel.videoStack && panel.backend && panel.backend->isOpen()) {
            if (&panel != &m_preview || m_previewTransportStarted) {
                panel.videoStack->setCurrentWidget(panel.videoSurface);
            }
        }
        return;
    }
    if (!panel.videoSurface || !panel.backend || !panel.backend->isOpen()) return;
    gs_init_data graphicsData{};
    graphicsData.window.hwnd = reinterpret_cast<void*>(panel.videoSurface->winId());
    graphicsData.cx = static_cast<uint32_t>(panel.videoSurface->width());
    graphicsData.cy = static_cast<uint32_t>(panel.videoSurface->height());
    graphicsData.format = GS_BGRA;
    graphicsData.zsformat = GS_ZS_NONE;
    graphicsData.num_backbuffers = 2;
    panel.display = obs_display_create(&graphicsData, 0xFF000000);
    if (!panel.display) {
        LOG_ERROR("OBS dual media: obs_display_create failed.");
        panel.statusLabel->setText(localized("Không thể tạo màn hình OBS.", "Could not create the OBS display."));
        return;
    }
    obs_display_add_draw_callback(panel.display, draw, &panel);
    if (panel.videoStack && (&panel != &m_preview || m_previewTransportStarted)) {
        panel.videoStack->setCurrentWidget(panel.videoSurface);
    }
    LOG_INFO("OBS dual media: Display created.");
}

void ObsDualMediaTestWindow::destroyDisplay(Panel& panel) {
    if (panel.thumbnailTexrender || panel.thumbnailStages[0] || panel.thumbnailStages[1] || panel.thumbnailStages[2]) {
        obs_enter_graphics();
        if (panel.thumbnailStages[0]) gs_stagesurface_destroy(panel.thumbnailStages[0]);
        if (panel.thumbnailStages[1]) gs_stagesurface_destroy(panel.thumbnailStages[1]);
        if (panel.thumbnailStages[2]) gs_stagesurface_destroy(panel.thumbnailStages[2]);
        if (panel.thumbnailTexrender) gs_texrender_destroy(panel.thumbnailTexrender);
        obs_leave_graphics();
        panel.thumbnailStages[0] = nullptr;
        panel.thumbnailStages[1] = nullptr;
        panel.thumbnailStages[2] = nullptr;
        panel.thumbnailTexrender = nullptr;
        panel.thumbnailWriteStage = 0;
        panel.thumbnailFrameCounter = 0;
        panel.thumbnailStagedFrameCount = 0;
        panel.thumbnailUpdatePending.store(false);
    }
    if (!panel.display) return;
    obs_display_remove_draw_callback(panel.display, draw, &panel);
    obs_display_destroy(panel.display);
    panel.display = nullptr;
}

void ObsDualMediaTestWindow::resizeDisplay(Panel& panel) {
    if (!panel.display || !panel.videoSurface) return;
    const int width = panel.videoSurface->width();
    const int height = panel.videoSurface->height();
    if (width > 0 && height > 0) obs_display_resize(panel.display, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

void ObsDualMediaTestWindow::updateMonitorLayout() {
    if (!m_switcherArea || !m_preview.videoContainer || !m_program.videoContainer) return;

    updateInputToolbarPresentation();

    constexpr int transitionRailWidth = 116;
    constexpr int horizontalSpacing = 8;
    // Header, margins, gaps and the dedicated native transport bar.  The
    // extra headroom keeps a D3D11 child surface from overlapping controls.
    constexpr int panelChromeHeight = 94;
    const int usableWidth = std::max(0, m_switcherArea->width() - transitionRailWidth - horizontalSpacing * 2);
    const int panelWidth = usableWidth / 2;
    if (panelWidth <= 0) return;

    // createPanel() contributes 5 px of horizontal content margin per side.
    // Base the 16:9 height on the actual video width, not the outer panel.
    const int videoContentWidth = std::max(1, panelWidth - 10);
    const int videoHeight = std::max(1, videoContentWidth * 9 / 16);
    const int switcherHeight = videoHeight + panelChromeHeight;
    if (m_preview.videoContainer->height() != videoHeight) m_preview.videoContainer->setFixedHeight(videoHeight);
    if (m_program.videoContainer->height() != videoHeight) m_program.videoContainer->setFixedHeight(videoHeight);
    if (m_switcherArea->height() != switcherHeight) m_switcherArea->setFixedHeight(switcherHeight);
}

void ObsDualMediaTestWindow::updateInputToolbarPresentation() {
    if (!m_inputToolbarWidget || !m_workspaceButton || !m_addSourceButton || !m_openPlaylistButton ||
        !m_sourceTypeFilter || !m_catalogThumbnailSize || !m_projectFrameRate ||
        !m_programRenderMode || !m_typeLabel || !m_sizeLabel ||
        !m_fpsLabel || !m_programViewLabel) {
        return;
    }

    // Giữ nguyên bố cục chuẩn FHD; chỉ tinh gọn thanh công cụ ở màn hình
    // landscape nhỏ, nơi các nhãn riêng làm giao diện chật và rối.
    const bool compactLandscape = width() < 1600 && height() < width();
    for (QLabel* label : {m_typeLabel, m_sizeLabel, m_fpsLabel, m_programViewLabel}) {
        if (!label) continue;
        label->setVisible(true);
        label->setStyleSheet(compactLandscape
            ? QStringLiteral("color: #82939e; border: 0; font-size: 9px; font-weight: 600; letter-spacing: 0.5px;")
            : QStringLiteral("color: #b8c5ce; border: 0;"));
    }
    m_typeLabel->setText(localized("LOẠI", "TYPE"));
    m_sizeLabel->setText(localized("CỠ", "SIZE"));
    m_fpsLabel->setText(QStringLiteral("FPS"));
    m_programViewLabel->setText(localized("HIỂN THỊ", "VIEW"));

    const auto setWidth = [](QWidget* widget, int compactWidth, int standardWidth = 0) {
        if (!widget) return;
        if (compactWidth > 0) {
            widget->setFixedWidth(compactWidth);
        } else if (standardWidth > 0) {
            widget->setFixedWidth(standardWidth);
        } else {
            widget->setMinimumWidth(0);
            widget->setMaximumWidth(QWIDGETSIZE_MAX);
        }
    };

    if (compactLandscape) {
        m_inputToolbarWidget->setStyleSheet(QStringLiteral(R"(
            QWidget#inputToolbar { background: transparent; border: 0; }
            QWidget#inputToolbar QPushButton,
            QWidget#inputToolbar QComboBox {
                min-height: 30px;
                max-height: 30px;
                background: #26333d;
                color: #eef5f8;
                border: 1px solid #405563;
                border-radius: 6px;
                padding: 0 10px;
                font-size: 11px;
            }
            QWidget#inputToolbar QPushButton:hover,
            QWidget#inputToolbar QComboBox:hover {
                background: #304451;
                border-color: #4f91a8;
            }
            QWidget#inputToolbar QPushButton:pressed,
            QWidget#inputToolbar QComboBox:focus {
                background: #1d2a32;
                border-color: #55c7df;
            }
            QWidget#inputToolbar QComboBox::drop-down {
                width: 22px;
                border: 0;
            }
        )"));
        m_addSourceButton->setText(localized("＋ Nguồn", "＋ Input"));
        m_workspaceButton->setText(localized("Tệp", "File"));
        if (m_sourceTypeFilter->count() >= 6) {
            m_sourceTypeFilter->setItemText(0, localized("Tất cả", "All types"));
            m_sourceTypeFilter->setItemText(1, QStringLiteral("Video"));
            m_sourceTypeFilter->setItemText(2, localized("Âm thanh", "Audio"));
            m_sourceTypeFilter->setItemText(3, localized("Hình ảnh", "Images"));
            m_sourceTypeFilter->setItemText(4, QStringLiteral("RTSP"));
            m_sourceTypeFilter->setItemText(5, localized("Nền", "Blank"));
        }
        if (m_catalogThumbnailSize->count() >= 3) {
            m_catalogThumbnailSize->setItemText(0, localized("Nhỏ", "Small"));
            m_catalogThumbnailSize->setItemText(1, localized("Vừa", "Normal"));
            m_catalogThumbnailSize->setItemText(2, localized("Lớn", "Large"));
        }
        const QStringList compactFrameRateLabels{
            QStringLiteral("23.976"), QStringLiteral("24"), QStringLiteral("25"), QStringLiteral("29.97"),
            QStringLiteral("30"), QStringLiteral("50"), QStringLiteral("59.94"), QStringLiteral("60"),
        };
        for (int index = 0; index < std::min(m_projectFrameRate->count(), static_cast<int>(compactFrameRateLabels.size())); ++index) {
            m_projectFrameRate->setItemText(index, compactFrameRateLabels.at(index));
        }
        m_programRenderMode->setItemText(0, localized("Mặc định", "Default"));
        m_programRenderMode->setItemText(1, localized("Vừa", "Fit"));
        setWidth(m_addSourceButton, 104);
        setWidth(m_workspaceButton, 72);
        setWidth(m_openPlaylistButton, 96);
        setWidth(m_sourceTypeFilter, 102);
        setWidth(m_catalogThumbnailSize, 96);
        setWidth(m_projectFrameRate, 88);
        setWidth(m_programRenderMode, 102);
    } else {
        m_inputToolbarWidget->setStyleSheet(QString());
        m_addSourceButton->setText(localized("Thêm nguồn", "Add Input"));
        m_workspaceButton->setText(QStringLiteral("Workspace"));
        if (m_sourceTypeFilter->count() >= 6) {
            m_sourceTypeFilter->setItemText(0, localized("Tất cả", "All types"));
            m_sourceTypeFilter->setItemText(1, QStringLiteral("Video"));
            m_sourceTypeFilter->setItemText(2, localized("Âm thanh", "Audio"));
            m_sourceTypeFilter->setItemText(3, localized("Hình ảnh", "Images"));
            m_sourceTypeFilter->setItemText(4, localized("Camera RTSP", "RTSP cameras"));
            m_sourceTypeFilter->setItemText(5, localized("Nền trống", "Blank"));
        }
        if (m_catalogThumbnailSize->count() >= 3) {
            m_catalogThumbnailSize->setItemText(0, localized("Nhỏ", "Small"));
            m_catalogThumbnailSize->setItemText(1, localized("Vừa", "Normal"));
            m_catalogThumbnailSize->setItemText(2, localized("Lớn", "Large"));
        }
        const QStringList standardFrameRateLabels{
            QStringLiteral("23.976"), QStringLiteral("24"), QStringLiteral("25"), QStringLiteral("29.97"),
            QStringLiteral("30"), QStringLiteral("50"), QStringLiteral("59.94"), QStringLiteral("60"),
        };
        for (int index = 0; index < std::min(m_projectFrameRate->count(), static_cast<int>(standardFrameRateLabels.size())); ++index) {
            m_projectFrameRate->setItemText(index, standardFrameRateLabels.at(index));
        }
        m_programRenderMode->setItemText(0, localized("Mặc định", "Default"));
        m_programRenderMode->setItemText(1, localized("Vừa màn hình", "Fit"));
        setWidth(m_addSourceButton, 0);
        setWidth(m_workspaceButton, 0);
        setWidth(m_openPlaylistButton, 108);
        setWidth(m_sourceTypeFilter, 0);
        setWidth(m_catalogThumbnailSize, 0);
        setWidth(m_projectFrameRate, 88);
        setWidth(m_programRenderMode, 88);
    }

    m_sourceTypeFilter->setToolTip(localized("Lọc theo loại nguồn", "Filter by source type"));
    m_catalogThumbnailSize->setToolTip(localized("Kích thước hình thu nhỏ", "Thumbnail size"));
    m_programRenderMode->setToolTip(localized(
        "Chế độ hiển thị của Program", "Program display mode"));
}

void ObsDualMediaTestWindow::updatePanel(Panel& panel, const QString& role) {
    if (!panel.backend || !panel.backend->isOpen()) return;
    panel.backend->enforcePendingPause();
    const ObsPlaybackState playbackState = panel.backend->state();
    const bool supportsTransport = panel.backend->supportsTransport();
    const bool supportsAudio = panel.backend->supportsAudio();
    const bool stagedCue = &panel == &m_preview && !m_previewTransportStarted &&
        m_stagedPreviewSnapshot.valid;
    const int64_t backendDuration = panel.backend->durationMs();
    if (stagedCue && backendDuration > 0) m_stagedPreviewSnapshot.durationMs = backendDuration;
    const int64_t position = stagedCue ? m_stagedPreviewSnapshot.positionMs : panel.backend->positionMs();
    const int64_t duration = stagedCue ? m_stagedPreviewSnapshot.durationMs : backendDuration;
    panel.seekSlider->setEnabled(supportsTransport && duration > 0);
    panel.playPauseButton->setEnabled(supportsTransport);
    panel.loopButton->setEnabled(supportsTransport);
    panel.resetButton->setEnabled(supportsTransport);
    panel.seekBackButton->setEnabled(supportsTransport);
    panel.seekForwardButton->setEnabled(supportsTransport);
    panel.volumeSlider->setEnabled(supportsAudio);
    panel.muteButton->setEnabled(supportsAudio);
    if (!supportsAudio) panel.audioMeter->reset();
    if (!panel.sliderDragging && supportsTransport && duration > 0) panel.seekSlider->setValue(static_cast<int>(position * 1000 / duration));
    const bool isPlaying = !stagedCue && playbackState == ObsPlaybackState::Playing;
    panel.playPauseButton->setIcon(style()->standardIcon(isPlaying ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    panel.playPauseButton->setToolTip(isPlaying ? localized("Tạm dừng", "Pause") : localized("Phát", "Play"));
    panel.loopButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    const bool looping = stagedCue ? m_stagedPreviewSnapshot.looping : panel.backend->isLooping();
    panel.loopButton->setProperty("loopActive", looping);
    panel.loopButton->style()->unpolish(panel.loopButton);
    panel.loopButton->style()->polish(panel.loopButton);
    panel.loopButton->setToolTip(looping
        ? localized("Tắt lặp lại", "Disable loop") : localized("Bật lặp lại", "Enable loop"));
    const bool sourceError = playbackState == ObsPlaybackState::Error;
    const bool sourceConnecting = panel.backend->isLiveInput() &&
        (playbackState == ObsPlaybackState::Opening || playbackState == ObsPlaybackState::Buffering ||
         playbackState == ObsPlaybackState::None);
    panel.timeLabel->setText(sourceError ? localized("MẤT KẾT NỐI", "OFFLINE")
        : (sourceConnecting ? localized("ĐANG KẾT NỐI", "CONNECTING")
        : (panel.backend->isLiveInput() ? QStringLiteral("LIVE")
        : (supportsTransport ? formatTimeline(position, duration) : QStringLiteral("STILL")))));
    panel.timeLabel->setStyleSheet(sourceError
        ? QStringLiteral("color: #ff7084; border: 0; font-family: Consolas; font-size: 11px; font-weight: bold; padding: 0 2px;")
        : (sourceConnecting
            ? QStringLiteral("color: #ffd36a; border: 0; font-family: Consolas; font-size: 11px; font-weight: bold; padding: 0 2px;")
            : QStringLiteral("color: #a9e9f7; border: 0; font-family: Consolas; font-size: 11px; font-weight: bold; padding: 0 2px;")));
    const uint64_t sourceId = &panel == &m_preview ? m_previewSourceId : m_programSourceId;
    const auto catalogSource = m_sourceCatalog ? m_sourceCatalog->find(sourceId) : std::nullopt;
    const QString sourceName = catalogSource ? catalogSourceName(*catalogSource, m_language == UiLanguage::Vietnamese)
        : QFileInfo(QString::fromStdWString(panel.backend->mediaPath().filename().wstring())).fileName();
    const QString sourceText = QStringLiteral("%1  %2").arg(role, sourceName);
    panel.sourceLabel->setText(panel.sourceLabel->fontMetrics().elidedText(
        sourceText, Qt::ElideRight, panel.sourceLabel->width()));
}

void ObsDualMediaTestWindow::updateAudioMeters() {
    const auto updateMeter = [this](Panel& panel) {
        if (&panel == &m_preview && !m_previewTransportStarted) {
            if (panel.audioMeter) panel.audioMeter->reset();
            return;
        }
        if (&panel == &m_program && m_layoutMode == LayoutMode::Triple) {
            if (!panel.audioMeter) return;
            for (auto& zone : m_tripleZones) {
                if (!zone.audioMuted && zone.activeBackend && zone.activeBackend->isOpen() && zone.activeBackend->supportsAudio()) {
                    panel.audioMeter->setLevels(zone.activeBackend->takeLeftAudioPeak(), zone.activeBackend->takeRightAudioPeak());
                    return;
                }
            }
            panel.audioMeter->reset();
            return;
        }
        if (!panel.audioMeter || !panel.backend || !panel.backend->isOpen() || !panel.backend->supportsAudio()) {
            if (panel.audioMeter) panel.audioMeter->reset();
            return;
        }
        panel.audioMeter->setLevels(panel.backend->takeLeftAudioPeak(), panel.backend->takeRightAudioPeak());
    };
    updateMeter(m_preview);
    updateMeter(m_program);
}

void ObsDualMediaTestWindow::updateProcessMetrics() {
    if (!m_processMetricsLabel) return;

    const ProcessMetricsSnapshot metrics = MediaDiagnostics::instance().processMetricsSnapshot();
    const auto toMiB = [](uint64_t bytes) { return bytes / (1024ULL * 1024ULL); };
    const QString uptime = formatMilliseconds(static_cast<int64_t>(metrics.uptimeSeconds) * 1000);
    const auto frameRate = m_context.videoFrameRate();
    const double fps = frameRate.denominator > 0
        ? static_cast<double>(frameRate.numerator) / static_cast<double>(frameRate.denominator)
        : 0.0;
    const size_t sourceCount = m_sourceCatalog ? m_sourceCatalog->sources().size() : 0;
    int activeDecoders =
        (m_preview.backend && m_preview.backend->isOpen() ? 1 : 0) +
        (m_program.backend && m_program.backend->isOpen() ? 1 : 0) +
        (m_program.fadeOutgoing && m_program.fadeOutgoing->isOpen() ? 1 : 0);
    if (m_layoutMode == LayoutMode::Triple) {
        activeDecoders = (m_preview.backend && m_preview.backend->isOpen() ? 1 : 0);
        for (const auto& z : m_tripleZones) {
            if (z.activeBackend && z.activeBackend->isOpen()) ++activeDecoders;
            if (z.pendingBackend && z.pendingBackend->isOpen()) ++activeDecoders;
        }
    }
    const bool compactLayout = width() < 990 || height() >= width();

    if (compactLayout) {
        m_processMetricsLabel->setText(
            localized("📊 CPU %1% | RAM %2 MB | %3 luồng | FPS %4 | ⏱ %5",
                      "📊 CPU %1% | RAM %2 MB | %3 threads | FPS %4 | ⏱ %5")
                .arg(metrics.cpuPercent, 0, 'f', 1)
                .arg(toMiB(metrics.workingSetBytes))
                .arg(metrics.threadCount)
                .arg(fps, 0, 'f', 2)
                .arg(uptime));
    } else {
        m_processMetricsLabel->setText(
            localized("📊 TIẾN TRÌNH | PID %1 | CPU %2% | RAM %3/%4 MB | %5 luồng | %6 handle | FPS %7 | Nguồn %8 | Decoder %9/3 | ⏱ %10",
                      "📊 PROCESS | PID %1 | CPU %2% | RAM %3/%4 MB | %5 threads | %6 handles | FPS %7 | Inputs %8 | Decoders %9/3 | ⏱ %10")
                .arg(metrics.processId)
                .arg(metrics.cpuPercent, 0, 'f', 1)
                .arg(toMiB(metrics.workingSetBytes))
                .arg(toMiB(metrics.privateBytes))
                .arg(metrics.threadCount)
                .arg(metrics.handleCount)
                .arg(fps, 0, 'f', 2)
                .arg(sourceCount)
                .arg(activeDecoders)
                .arg(uptime));
    }

    m_processMetricsLabel->setToolTip(
        localized("THÔNG SỐ TIẾN TRÌNH MEDIASWITCHER OBS\n"
                  "PID: %1\nCPU: %2% (%3 bộ xử lý logic)\n"
                  "RAM working set: %4 MB\nRAM private: %5 MB\n"
                  "Số luồng: %6\nSố handle: %7\n"
                  "I/O đã đọc: %8 MB\nI/O đã ghi: %9 MB\n"
                  "FPS dự án: %10\nNguồn: %11\nDecoder đang hoạt động: %12/3\n"
                  "Thời gian chạy: %13",
                  "MEDIASWITCHER OBS PROCESS METRICS\n"
                  "PID: %1\nCPU: %2% (%3 logical processors)\n"
                  "RAM working set: %4 MB\nRAM private: %5 MB\n"
                  "Threads: %6\nHandles: %7\n"
                  "I/O read: %8 MB\nI/O written: %9 MB\n"
                  "Project FPS: %10\nInputs: %11\nActive decoders: %12/3\n"
                  "Uptime: %13")
            .arg(metrics.processId)
            .arg(metrics.cpuPercent, 0, 'f', 1)
            .arg(metrics.logicalProcessorCount)
            .arg(toMiB(metrics.workingSetBytes))
            .arg(toMiB(metrics.privateBytes))
            .arg(metrics.threadCount)
            .arg(metrics.handleCount)
            .arg(toMiB(metrics.ioReadBytes))
            .arg(toMiB(metrics.ioWriteBytes))
            .arg(fps, 0, 'f', 2)
            .arg(sourceCount)
            .arg(activeDecoders)
            .arg(uptime));
}

void ObsDualMediaTestWindow::updateCopyrightDisplay() {
    if (!m_copyrightButton) return;

    const CopyrightInfo info = CopyrightSettings::load();
    const QString fullText = info.footerText.isEmpty()
        ? localized("Thông tin bản quyền", "Copyright information")
        : info.footerText;
    m_copyrightButton->setText(
        m_copyrightButton->fontMetrics().elidedText(fullText, Qt::ElideRight, 330));
    m_copyrightButton->setToolTip(
        QStringLiteral("%1\n%2")
            .arg(fullText,
                 localized("Nhấn để xem hoặc chỉnh sửa thông tin bản quyền.",
                           "Click to view or edit copyright information.")));
    m_copyrightButton->setAccessibleName(
        localized("Thông tin bản quyền", "Copyright information"));
}

void ObsDualMediaTestWindow::showAboutDialog() {
    AboutDialog dialog(this);
    dialog.exec();
    updateCopyrightDisplay();
}

void ObsDualMediaTestWindow::stagePreviewSource(const ObsCatalogSource& source) {
    // A live PGM click gets a full-resolution frame and its adjacent playhead
    // before staging. Other inputs use their last frame/playhead pair.
    if (source.id == m_programSourceId && m_program.backend && m_program.backend->isOpen()) {
        const QImage frame = captureBackendStill(*m_program.backend);
        const PlaybackSnapshot snapshot = captureProgramSnapshot();
        if (!frame.isNull() && snapshot.valid) showSourceThumbnail(source.id, frame, snapshot, true);
        stagePreviewSnapshot(snapshot.valid ? snapshot : previewSnapshotForSource(source));
        return;
    }
    stagePreviewSnapshot(previewSnapshotForSource(source));
}

void ObsDualMediaTestWindow::stagePreviewSnapshot(const PlaybackSnapshot& snapshot) {
    ++m_previewSeekGeneration;
    if (m_previewSeekTimer) m_previewSeekTimer->stop();
    if (!snapshot.valid) {
        clearPreviewSource();
        return;
    }
    const auto source = m_sourceCatalog->find(snapshot.sourceId);
    if (!source) {
        clearPreviewSource();
        return;
    }

    // Selecting an input preloads one native OBS runtime behind a static 720p
    // poster. The preloader is never exposed until explicit Play/CUT/FADE, so
    // ffmpeg preroll cannot look like auto-play on PVW. The saved cue position,
    // rather than the internal preroll clock, remains the operator truth.
    m_preview.backend->close();
    bool restoredRuntime = false;
    if (const auto cached = m_pausedSourceRuntimes.find(source->id);
        cached != m_pausedSourceRuntimes.end() && cached->second && cached->second->isOpen()) {
        m_preview.backend = std::move(cached->second);
        m_pausedSourceRuntimes.erase(cached);
        restoredRuntime = true;
    }
    m_preview.backend->setAudioOutputEnabled(false);
    m_previewTransportStarted = false;
    m_previewRuntimeMatchesStagedFrame = false;
    m_stagedPreviewFrame = {};
    m_stagedPreviewFrameSourceId = 0;
    m_stagedPreviewSnapshot = snapshot;
    // The selected tile is the operator-visible snapshot. Freeze precisely
    // that bitmap on PVW; never replace it with an unrelated add-time poster.
    if (const auto tileSnapshot = m_sourceThumbnailSnapshots.find(snapshot.sourceId);
        tileSnapshot != m_sourceThumbnailSnapshots.end() && tileSnapshot->second.valid &&
        std::llabs(tileSnapshot->second.positionMs - snapshot.positionMs) <= 50) {
        if (const auto timelineFrame = m_sourceTimelineFrames.find(snapshot.sourceId);
            timelineFrame != m_sourceTimelineFrames.end()) {
            const auto timelineSnapshot = m_sourceTimelineFrameSnapshots.find(snapshot.sourceId);
            if (timelineSnapshot != m_sourceTimelineFrameSnapshots.end() &&
                std::llabs(timelineSnapshot->second.positionMs - snapshot.positionMs) <= 50) {
                m_stagedPreviewFrame = timelineFrame->second;
            }
        }
        if (m_stagedPreviewFrame.isNull()) {
            if (const auto thumbnail = m_sourceThumbnails.find(snapshot.sourceId);
                thumbnail != m_sourceThumbnails.end()) m_stagedPreviewFrame = thumbnail->second;
        }
        if (!m_stagedPreviewFrame.isNull()) m_stagedPreviewFrameSourceId = snapshot.sourceId;
    }
    m_previewSourceId = source->id;
    m_preview.thumbnailGeneration.fetch_add(1);
    m_preview.thumbnailSourceId.store(source->id);
    m_preview.thumbnailCaptureContinuous.store(false);
    m_preview.thumbnailCaptureEnabled.store(false);
    m_preview.thumbnailUpdatePending.store(false);
    m_preview.thumbnailFrameCounter = 0;
    m_preview.thumbnailStagedFrameCount = 0;

    const bool hasTimeline = obsCatalogSourceHasTimeline(source->type);
    const bool canOpenPreview = hasTimeline || source->type == ObsCatalogSourceType::ImageFile ||
        source->type == ObsCatalogSourceType::RtspCamera;
    m_preview.seekSlider->setEnabled(hasTimeline && snapshot.durationMs > 0);
    m_preview.seekSlider->setValue(snapshot.durationMs > 0
        ? static_cast<int>(snapshot.positionMs * 1000 / snapshot.durationMs) : 0);
    m_preview.playPauseButton->setEnabled(canOpenPreview);
    m_preview.playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_preview.playPauseButton->setToolTip(localized("Phát nguồn trên PVW", "Play source on PVW"));
    m_preview.loopButton->setEnabled(hasTimeline);
    m_preview.loopButton->setProperty("loopActive", snapshot.looping);
    m_preview.loopButton->style()->unpolish(m_preview.loopButton);
    m_preview.loopButton->style()->polish(m_preview.loopButton);
    m_preview.resetButton->setEnabled(hasTimeline);
    m_preview.seekBackButton->setEnabled(hasTimeline);
    m_preview.seekForwardButton->setEnabled(hasTimeline);
    m_preview.timeLabel->setText(hasTimeline
        ? formatTimeline(snapshot.positionMs, snapshot.durationMs)
        : (source->type == ObsCatalogSourceType::RtspCamera ? QStringLiteral("LIVE") : QStringLiteral("STILL")));
    m_preview.sourceLabel->setText(m_preview.sourceLabel->fontMetrics().elidedText(
        QStringLiteral("PVW  %1").arg(catalogSourceName(*source, m_language == UiLanguage::Vietnamese)),
        Qt::ElideRight, m_preview.sourceLabel->width()));
    updateStagedPreviewFrame();
    bool preloaded = false;
    if (restoredRuntime) {
        m_preview.backend->setAudioOutputEnabled(false);
        m_preview.backend->setRenderMode(ObsRenderMode::AspectFit);
        m_preview.backend->setVolume(m_preview.audioMuted ? 0.0f : m_preview.volume);
        m_previewRuntimeMatchesStagedFrame = true;
        initializeDisplay(m_preview);
        resizeDisplay(m_preview);
        if (m_preview.videoStack) m_preview.videoStack->setCurrentWidget(m_preview.stagedFrameLabel);
        preloaded = true;
        LOG_INFO("OBS app: Restored paused runtime for PVW source #{} at {} ms without seeking.",
                 source->id, snapshot.positionMs);
    } else {
        preloaded = openStagedPreview(false);
    }
    refreshCatalogUi();
    LOG_INFO("OBS app: {} source #{} staged on PVW at {} ms; preloadPaused={} autoPlay=false.",
             obsCatalogSourceTypeName(source->type), source->id, snapshot.positionMs, preloaded);
}

void ObsDualMediaTestWindow::updateStagedPreviewFrame() {
    if (!m_preview.stagedFrameLabel || !m_stagedPreviewSnapshot.valid) return;

    QPixmap poster;
    if (m_stagedPreviewFrameSourceId == m_stagedPreviewSnapshot.sourceId &&
        !m_stagedPreviewFrame.isNull()) {
        poster = m_stagedPreviewFrame;
    } else if (const auto highResolutionPoster = m_sourcePosters.find(m_stagedPreviewSnapshot.sourceId);
        highResolutionPoster != m_sourcePosters.end()) {
        poster = highResolutionPoster->second;
    } else if (const auto thumbnail = m_sourceThumbnails.find(m_stagedPreviewSnapshot.sourceId);
               thumbnail != m_sourceThumbnails.end()) {
        poster = thumbnail->second;
    } else if (const auto source = m_sourceCatalog->find(m_stagedPreviewSnapshot.sourceId)) {
        if (source->type == ObsCatalogSourceType::ColorBlank) {
            poster = QPixmap(320, 180);
            poster.fill(Qt::black);
        } else {
            const QFileInfo info(QString::fromStdWString(source->path.wstring()));
            poster = QFileIconProvider().icon(info).pixmap(128, 128);
        }
    }

    const QSize target = m_preview.stagedFrameLabel->size().isValid()
        ? m_preview.stagedFrameLabel->size() : QSize(320, 180);
    QPixmap canvas(target);
    canvas.fill(Qt::black);
    if (!poster.isNull()) {
        QPainter painter(&canvas);
        const QPixmap fitted = poster.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawPixmap((target.width() - fitted.width()) / 2, (target.height() - fitted.height()) / 2, fitted);
    }
    m_preview.stagedFrameLabel->setPixmap(canvas);
    m_preview.stagedFrameLabel->setText({});
    if (m_preview.videoStack &&
        ((!m_preview.backend || !m_preview.backend->isOpen()) ||
         m_preview.videoStack->currentWidget() == m_preview.stagedFrameLabel)) {
        m_preview.videoStack->setCurrentWidget(m_preview.stagedFrameLabel);
    }
}

bool ObsDualMediaTestWindow::openStagedPreview(bool play) {
    if (!m_stagedPreviewSnapshot.valid || !m_preview.backend) return false;
    const auto source = m_sourceCatalog->find(m_stagedPreviewSnapshot.sourceId);
    if (!source) return false;

    m_preview.backend->setAudioOutputEnabled(false);
    m_preview.backend->setVolume(m_preview.audioMuted ? 0.0f : m_preview.volume);
    m_preview.backend->setLooping(m_stagedPreviewSnapshot.looping);
    if (!m_preview.backend->open(*source, !play)) return false;
    if (m_preview.backend->supportsTransport()) {
        m_preview.backend->seekMs(m_stagedPreviewSnapshot.positionMs);
    }
    if (play && m_preview.backend->supportsTransport()) m_preview.backend->play();
    const bool captureVisual = source->type == ObsCatalogSourceType::VideoFile ||
        source->type == ObsCatalogSourceType::ImageFile || source->type == ObsCatalogSourceType::RtspCamera;
    const bool captureContinuous = play && (source->type == ObsCatalogSourceType::VideoFile ||
        source->type == ObsCatalogSourceType::RtspCamera);
    m_preview.thumbnailGeneration.fetch_add(1);
    m_preview.thumbnailSourceId.store(source->id);
    m_preview.thumbnailCaptureContinuous.store(captureContinuous);
    // A cue-only preload remains covered by its high-resolution poster. Do not
    // let the 320x180 live tap overwrite that poster before explicit Play.
    m_preview.thumbnailCaptureEnabled.store(play && captureVisual);
    m_preview.thumbnailUpdatePending.store(false);
    m_preview.thumbnailFrameCounter = 0;
    m_preview.thumbnailStagedFrameCount = 0;
    m_previewTransportStarted = play;
    if (play) m_previewRuntimeMatchesStagedFrame = false;
    initializeDisplay(m_preview);
    resizeDisplay(m_preview);
    if (!play && m_preview.backend->supportsTransport()) {
        if (m_preview.videoStack) m_preview.videoStack->setCurrentWidget(m_preview.stagedFrameLabel);
    }
    LOG_INFO("OBS app: PVW {} opened source #{} at {} ms.",
             play ? "Play" : "paused preload", source->id, m_stagedPreviewSnapshot.positionMs);
    return true;
}

void ObsDualMediaTestWindow::showSourceThumbnail(uint64_t sourceId, const QImage& frame,
                                                  const PlaybackSnapshot& snapshot,
                                                  bool timelineFrame) {
    if (sourceId == 0 || !m_sourceCatalog->find(sourceId)) return;
    QPixmap pixmap = QPixmap::fromImage(frame);
    const bool highResolution = frame.width() > 320 || frame.height() > 180;
    if (highResolution && !timelineFrame) m_sourcePosters[sourceId] = pixmap;
    // A slow add-time poster must never overwrite a newer live frame/time pair.
    if (!timelineFrame && m_sourcesWithTimelineThumbnail.contains(sourceId)) return;
    if (timelineFrame) m_sourcesWithTimelineThumbnail.insert(sourceId);
    m_sourceThumbnails[sourceId] = std::move(pixmap);
    if (snapshot.valid && snapshot.sourceId == sourceId) {
        m_sourceThumbnailSnapshots[sourceId] = snapshot;
        if (timelineFrame && highResolution) {
            m_sourceTimelineFrames[sourceId] = m_sourceThumbnails[sourceId];
            m_sourceTimelineFrameSnapshots[sourceId] = snapshot;
        }
    }
    if (const auto label = m_catalogThumbnailLabels.find(sourceId);
        label != m_catalogThumbnailLabels.end() && label->second) {
        label->second->setPixmap(fitCatalogThumbnail(m_sourceThumbnails[sourceId], m_catalogThumbnailWidth));
    }
    if (sourceId == m_previewSourceId) {
        updateStagedPreviewFrame();
    }
}

ObsDualMediaTestWindow::PlaybackSnapshot ObsDualMediaTestWindow::capturePreviewSnapshot() const {
    PlaybackSnapshot snapshot;
    snapshot.sourceId = m_previewSourceId;
    if (!snapshot.sourceId) return snapshot;

    if (!m_preview.backend || !m_preview.backend->isOpen() || !m_previewTransportStarted) {
        if (m_stagedPreviewSnapshot.sourceId == snapshot.sourceId) return m_stagedPreviewSnapshot;
        return snapshot;
    }
    snapshot.positionMs = m_preview.backend->positionMs();
    snapshot.durationMs = m_preview.backend->durationMs();
    snapshot.looping = m_preview.backend->isLooping();
    snapshot.valid = isCatalogSourceAvailable(snapshot.sourceId);
    return snapshot;
}

ObsDualMediaTestWindow::PlaybackSnapshot ObsDualMediaTestWindow::captureProgramSnapshot() const {
    PlaybackSnapshot snapshot;
    snapshot.sourceId = m_programSourceId;
    if (!snapshot.sourceId || !m_program.backend || !m_program.backend->isOpen()) return snapshot;

    snapshot.positionMs = m_program.backend->positionMs();
    snapshot.durationMs = m_program.backend->durationMs();
    snapshot.looping = m_program.backend->isLooping();
    snapshot.valid = isCatalogSourceAvailable(snapshot.sourceId);
    return snapshot;
}

ObsDualMediaTestWindow::PlaybackSnapshot ObsDualMediaTestWindow::previewSnapshotForSource(const ObsCatalogSource& source) const {
    if (source.id == m_programSourceId) {
        const PlaybackSnapshot liveProgram = captureProgramSnapshot();
        if (liveProgram.valid) return liveProgram;
    }
    // Prefer the timestamp captured with the pixels currently visible in the
    // input tile. The periodic history timer is deliberately lower priority.
    if (const auto it = m_sourceThumbnailSnapshots.find(source.id);
        it != m_sourceThumbnailSnapshots.end() && it->second.valid) return it->second;
    if (const auto it = m_lastProgramSnapshots.find(source.id); it != m_lastProgramSnapshots.end()) return it->second;

    PlaybackSnapshot snapshot;
    snapshot.sourceId = source.id;
    snapshot.valid = true;
    return snapshot;
}

void ObsDualMediaTestWindow::rememberProgramSnapshot(const PlaybackSnapshot& snapshot) {
    if (!snapshot.valid || snapshot.sourceId == 0) return;
    m_lastProgramSnapshots[snapshot.sourceId] = snapshot;
}

bool ObsDualMediaTestWindow::openCatalogSource(ObsPlaybackBackend& backend, uint64_t sourceId, bool startPaused) {
    auto source = m_sourceCatalog->find(sourceId);
    if (!source) {
        LOG_ERROR("OBS app: Catalog source #{} is unavailable.", sourceId);
        return false;
    }
    return backend.open(*source, startPaused);
}

bool ObsDualMediaTestWindow::isCatalogSourceAvailable(uint64_t sourceId) const {
    return sourceId != 0 && m_sourceCatalog && m_sourceCatalog->find(sourceId).has_value();
}

void ObsDualMediaTestWindow::clearPreviewSource() {
    destroyDisplay(m_preview);
    if (m_preview.backend) {
        m_preview.backend->close();
        m_preview.backend->setAudioOutputEnabled(false);
    }
    m_preview.thumbnailGeneration.fetch_add(1);
    m_preview.thumbnailSourceId.store(0);
    m_preview.thumbnailCaptureContinuous.store(false);
    m_preview.thumbnailCaptureEnabled.store(false);
    m_preview.thumbnailUpdatePending.store(false);
    m_stagedPreviewSnapshot = {};
    m_stagedPreviewFrame = {};
    m_stagedPreviewFrameSourceId = 0;
    m_previewSourceId = 0;
    m_previewTransportStarted = false;
    m_previewRuntimeMatchesStagedFrame = false;
    m_preview.seekSlider->setValue(0);
    m_preview.seekSlider->setEnabled(false);
    m_preview.loopButton->setEnabled(true);
    m_preview.resetButton->setEnabled(false);
    m_preview.playPauseButton->setEnabled(true);
    m_preview.stagedFrameLabel->setPixmap({});
    m_preview.stagedFrameLabel->clear();
    if (m_preview.videoStack) m_preview.videoStack->setCurrentWidget(m_preview.stagedFrameLabel);
}

void ObsDualMediaTestWindow::clearProgramSource() {
    rememberProgramSnapshot(captureProgramSnapshot());
    destroyDisplay(m_program);
    if (m_program.backend) {
        m_program.backend->resetRenderSource();
        m_program.backend->close();
    }
    setProgramSourceId(0);
}

void ObsDualMediaTestWindow::togglePanelPlayback(Panel& panel) {
    if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen())) {
        openStagedPreview(true);
        return;
    }
    if (!panel.backend || !panel.backend->supportsTransport()) return;
    const bool pauseRequested = !(&panel == &m_preview && !m_previewTransportStarted) &&
        panel.backend->state() == ObsPlaybackState::Playing;
    if (&panel == &m_preview && !pauseRequested && !m_previewTransportStarted &&
        m_stagedPreviewSnapshot.valid && !m_previewRuntimeMatchesStagedFrame) {
        panel.backend->seekMs(m_stagedPreviewSnapshot.positionMs);
    }
    if (pauseRequested) panel.backend->pause();
    else panel.backend->play();

    if (&panel == &m_preview) {
        if (!pauseRequested && panel.videoStack) {
            panel.videoStack->setCurrentWidget(panel.videoSurface);
            resizeDisplay(panel);
            m_previewTransportStarted = true;
            m_previewRuntimeMatchesStagedFrame = false;
        }
        const auto source = m_sourceCatalog->find(m_previewSourceId);
        const bool captureVisual = source && (source->type == ObsCatalogSourceType::VideoFile ||
            source->type == ObsCatalogSourceType::ImageFile || source->type == ObsCatalogSourceType::RtspCamera);
        const bool captureContinuous = !pauseRequested && source &&
            (source->type == ObsCatalogSourceType::VideoFile || source->type == ObsCatalogSourceType::RtspCamera);
        panel.thumbnailGeneration.fetch_add(1);
        panel.thumbnailSourceId.store(m_previewSourceId);
        panel.thumbnailCaptureContinuous.store(captureContinuous);
        panel.thumbnailCaptureEnabled.store(captureVisual);
        panel.thumbnailUpdatePending.store(false);
        panel.thumbnailFrameCounter = 0;
        panel.thumbnailStagedFrameCount = 0;
    }
}

void ObsDualMediaTestWindow::togglePanelLoop(Panel& panel) {
    if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen() || !m_previewTransportStarted)) {
        if (!m_stagedPreviewSnapshot.valid) return;
        m_stagedPreviewSnapshot.looping = !m_stagedPreviewSnapshot.looping;
        panel.loopButton->setProperty("loopActive", m_stagedPreviewSnapshot.looping);
        panel.loopButton->style()->unpolish(panel.loopButton);
        panel.loopButton->style()->polish(panel.loopButton);
        return;
    }
    if (panel.backend && panel.backend->supportsTransport()) panel.backend->setLooping(!panel.backend->isLooping());
}

void ObsDualMediaTestWindow::seekPanel(Panel& panel, int64_t deltaMs) {
    if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen() || !m_previewTransportStarted)) {
        if (!m_stagedPreviewSnapshot.valid || m_stagedPreviewSnapshot.durationMs <= 0) return;
        requestStagedPreviewSeek(m_stagedPreviewSnapshot.positionMs + deltaMs);
        return;
    }
    if (panel.backend && panel.backend->supportsTransport()) panel.backend->seekMs(panel.backend->positionMs() + deltaMs);
}

void ObsDualMediaTestWindow::requestStagedPreviewSeek(int64_t positionMs) {
    if (!m_stagedPreviewSnapshot.valid || m_stagedPreviewSnapshot.durationMs <= 0) return;
    const int64_t target = std::clamp(positionMs, int64_t{0}, m_stagedPreviewSnapshot.durationMs);
    m_stagedPreviewSnapshot.positionMs = target;
    m_previewRuntimeMatchesStagedFrame = false;
    ++m_previewSeekGeneration;

    if (m_preview.seekSlider && !m_preview.sliderDragging) {
        m_preview.seekSlider->setValue(static_cast<int>(
            target * 1000 / m_stagedPreviewSnapshot.durationMs));
    }
    if (m_preview.timeLabel) {
        m_preview.timeLabel->setText(formatTimeline(target, m_stagedPreviewSnapshot.durationMs));
    }
    // Debounce GPU readback while the operator scrubs rapidly. The playhead
    // and time text remain immediate; the high-resolution paused frame follows
    // after the pointer rests briefly.
    if (m_previewSeekTimer) m_previewSeekTimer->start();
}

void ObsDualMediaTestWindow::applyStagedPreviewSeek() {
    if (m_closing || m_previewTransportStarted || !m_stagedPreviewSnapshot.valid ||
        !m_preview.backend || !m_preview.backend->isOpen() || !m_preview.backend->supportsTransport()) return;

    const uint64_t generation = m_previewSeekGeneration;
    const uint64_t sourceId = m_stagedPreviewSnapshot.sourceId;
    const int64_t target = m_stagedPreviewSnapshot.positionMs;
    m_preview.backend->setAudioOutputEnabled(false);
    m_preview.backend->pause();
    if (!m_preview.backend->seekMs(target)) return;
    m_preview.backend->enforcePendingPause();

    QTimer::singleShot(90, this, [this, generation, sourceId, target] {
        if (m_closing || generation != m_previewSeekGeneration || m_previewTransportStarted ||
            m_stagedPreviewSnapshot.sourceId != sourceId || m_stagedPreviewSnapshot.positionMs != target ||
            !m_preview.backend || !m_preview.backend->isOpen()) return;
        m_preview.backend->enforcePendingPause();
        const QImage frame = captureBackendStill(*m_preview.backend);
        if (frame.isNull()) {
            if (m_previewSeekTimer) m_previewSeekTimer->start(70);
            return;
        }

        PlaybackSnapshot snapshot = m_stagedPreviewSnapshot;
        const int64_t backendDuration = m_preview.backend->durationMs();
        if (backendDuration > 0) snapshot.durationMs = backendDuration;
        snapshot.positionMs = target;
        snapshot.looping = m_preview.backend->isLooping();
        snapshot.valid = true;
        m_stagedPreviewSnapshot = snapshot;
        showSourceThumbnail(sourceId, frame, snapshot, true);
        m_stagedPreviewFrame = QPixmap::fromImage(frame);
        m_stagedPreviewFrameSourceId = sourceId;
        m_previewRuntimeMatchesStagedFrame = true;
        updateStagedPreviewFrame();
        if (m_preview.videoStack) m_preview.videoStack->setCurrentWidget(m_preview.stagedFrameLabel);
        LOG_INFO("OBS app: PVW paused seek rendered source #{} at {} ms without autoplay.", sourceId, target);
    });
}

void ObsDualMediaTestWindow::resetPanel(Panel& panel) {
    if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen() || !m_previewTransportStarted)) {
        if (!m_stagedPreviewSnapshot.valid) return;
        requestStagedPreviewSeek(0);
        return;
    }
    if (panel.backend) panel.backend->seekMs(0);
}

bool ObsDualMediaTestWindow::promotePreviewToProgram(const char* operation) {
    if (m_fadeActive) {
        LOG_ERROR("OBS dual media: {} rejected while FADE is active.", operation);
        return false;
    }
    if (!m_program.backend || !m_preview.backend) {
        LOG_ERROR("OBS dual media: {} rejected because preview is unavailable.", operation);
        return false;
    }

    const bool incomingTransportStarted = m_previewTransportStarted;
    const PlaybackSnapshot incoming = capturePreviewSnapshot();
    PlaybackSnapshot outgoing = captureProgramSnapshot();
    QImage outgoingFrame;
    if (!incoming.valid) {
        LOG_ERROR("OBS dual media: {} rejected because the PVW cue is unavailable.", operation);
        return false;
    }

    if (m_playlistMode) stopPlaylist();

    const bool shouldSwap = std::string_view(operation) == "CUT";
    rememberProgramSnapshot(outgoing);

    // If PVW already owns a live decoder, transfer that exact runtime/view to
    // PGM. No close/open/seek cycle means the OBS media clock and decoded frame
    // queue continue uninterrupted across the switch boundary.
    if (m_preview.backend->isOpen()) {
        if (!incomingTransportStarted && m_preview.backend->supportsTransport()) {
            // The hidden preloader may preroll internally even while libobs
            // reports Paused. Reset it to the operator's saved cue immediately
            // before transferring the runtime; do not capture the preroll clock.
            m_preview.backend->pause();
            if (!m_previewRuntimeMatchesStagedFrame) m_preview.backend->seekMs(incoming.positionMs);
        }
        if (shouldSwap && outgoing.valid) {
            if (m_program.backend->supportsTransport()) {
                m_program.backend->pause();
                outgoing = captureProgramSnapshot();
                rememberProgramSnapshot(outgoing);
            }
            outgoingFrame = captureBackendStill(*m_program.backend);
            if (!outgoingFrame.isNull()) showSourceThumbnail(outgoing.sourceId, outgoingFrame, outgoing, true);
            // Keep both swapchains alive. Removing callbacks forms a short
            // render-thread synchronization point; the pointer swap then takes
            // effect on the very next OBS display frame.
            if (m_preview.display) obs_display_remove_draw_callback(m_preview.display, draw, &m_preview);
            if (m_program.display) obs_display_remove_draw_callback(m_program.display, draw, &m_program);
            m_program.backend->setAudioOutputEnabled(false);
            m_preview.backend->setAudioOutputEnabled(true);
            std::swap(m_preview.backend, m_program.backend);

            m_program.backend->setRenderMode(m_selectedProgramRenderMode);
            m_program.backend->setVolume(m_program.audioMuted ? 0.0f : m_program.volume);
            setProgramSourceId(incoming.sourceId, false);

            m_preview.backend->setRenderMode(ObsRenderMode::AspectFit);
            m_preview.backend->setVolume(m_preview.audioMuted ? 0.0f : m_preview.volume);
            m_previewSourceId = outgoing.sourceId;
            // PVW is a frozen representation of the old PGM at the CUT
            // boundary. Keep its exact decoder paused behind the still so
            // timestamp, pixels and the next Play all refer to one moment.
            m_previewTransportStarted = false;
            m_previewRuntimeMatchesStagedFrame = true;
            m_stagedPreviewSnapshot = outgoing;
            m_stagedPreviewFrame = QPixmap::fromImage(outgoingFrame);
            m_stagedPreviewFrameSourceId = outgoing.sourceId;
            if (const auto source = m_sourceCatalog->find(outgoing.sourceId)) {
                m_preview.thumbnailGeneration.fetch_add(1);
                m_preview.thumbnailSourceId.store(outgoing.sourceId);
                m_preview.thumbnailCaptureEnabled.store(false);
                m_preview.thumbnailCaptureContinuous.store(false);
                m_preview.thumbnailUpdatePending.store(false);
                m_preview.thumbnailFrameCounter = 0;
                m_preview.thumbnailStagedFrameCount = 0;
            }
            if (m_preview.display) obs_display_add_draw_callback(m_preview.display, draw, &m_preview);
            else initializeDisplay(m_preview);
            if (m_program.display) obs_display_add_draw_callback(m_program.display, draw, &m_program);
            else initializeDisplay(m_program);
            updateStagedPreviewFrame();
            // Publish the exact paused PVW frame to PGM first, then advance its
            // clock. Starting playback while both draw callbacks were detached
            // let the decoder move ahead before the switch became visible.
            if (m_program.backend->supportsTransport()) m_program.backend->play();
            refreshCatalogUi();
            LOG_INFO("OBS dual media: CUT transferred native runtimes without recreating decoders; PGM={} ms PVW={} ms.",
                     incoming.positionMs, outgoing.positionMs);
        } else {
            destroyDisplay(m_preview);
            if (m_program.display) obs_display_remove_draw_callback(m_program.display, draw, &m_program);
            auto incomingRuntime = std::move(m_preview.backend);
            m_preview.backend = std::make_unique<ObsPlaybackBackend>(m_context);
            m_preview.backend->setAudioOutputEnabled(false);
            m_preview.backend->setVolume(m_preview.audioMuted ? 0.0f : m_preview.volume);

            // QUICK PLAY replaces PGM but still preserves its exact paused
            // runtime for a later input-tile recall. Closing it here forced a
            // new keyframe seek (commonly about three seconds backwards).
            if (outgoing.valid && m_program.backend->isOpen()) {
                if (m_program.backend->supportsTransport()) m_program.backend->pause();
                outgoing = captureProgramSnapshot();
                outgoingFrame = captureBackendStill(*m_program.backend);
                rememberProgramSnapshot(outgoing);
                if (!outgoingFrame.isNull()) {
                    showSourceThumbnail(outgoing.sourceId, outgoingFrame, outgoing, true);
                }
                m_program.backend->resetRenderSource();
                m_program.backend->setAudioOutputEnabled(false);
                m_pausedSourceRuntimes[outgoing.sourceId] = std::move(m_program.backend);
            } else {
                m_program.backend->setAudioOutputEnabled(false);
                m_program.backend->close();
            }
            m_program.backend = std::move(incomingRuntime);
            m_program.backend->setRenderMode(m_selectedProgramRenderMode);
            m_program.backend->setVolume(m_program.audioMuted ? 0.0f : m_program.volume);
            m_program.backend->setAudioOutputEnabled(true);
            setProgramSourceId(incoming.sourceId, false);
            if (m_program.display) obs_display_add_draw_callback(m_program.display, draw, &m_program);
            else initializeDisplay(m_program);
            if (m_program.backend->supportsTransport()) m_program.backend->play();
            stagePreviewSnapshot(incoming);
            LOG_INFO("OBS dual media: {} transferred the native PVW runtime to PGM at {} ms.",
                     operation, incoming.positionMs);
        }

        QTimer::singleShot(1000, this, [this, operation] {
            if (m_closing) return;
            const PlaybackSnapshot preview = capturePreviewSnapshot();
            const PlaybackSnapshot program = captureProgramSnapshot();
            LOG_INFO("OBS dual media: {} runtime-transfer result PVW #{} position={} ms | PGM #{} position={} ms.",
                     operation, preview.sourceId, preview.positionMs, program.sourceId, program.positionMs);
        });
        return true;
    }

    // Cue-only PVW has no runtime to transfer, so create PGM from its saved
    // snapshot. This path is used only before the operator starts PVW playback.
    m_program.backend->close();
    m_program.backend->setAudioOutputEnabled(true);
    m_program.backend->setLooping(incoming.looping);
    if (!openCatalogSource(*m_program.backend, incoming.sourceId, true)) {
        LOG_ERROR("OBS dual media: {} failed to create Program runtime from Preview asset.", operation);
        m_program.statusLabel->setText(localized("Không thể chuyển nguồn lên Program.", "Program promotion failed."));
        return false;
    }

    if (m_program.backend->supportsTransport()) m_program.backend->seekMs(incoming.positionMs);
    initializeDisplay(m_program);
    resizeDisplay(m_program);
    m_program.backend->play();
    setProgramSourceId(incoming.sourceId);
    if (shouldSwap && outgoing.valid) stagePreviewSnapshot(outgoing);
    if (shouldSwap) {
        LOG_INFO("OBS dual media: CUT swapped PVW -> PGM at {} ms and PGM -> PVW at {} ms.",
                 incoming.positionMs, outgoing.positionMs);
    } else {
        LOG_INFO("OBS dual media: {} promoted PVW -> PGM at {} ms; PVW was kept unchanged.",
                 operation, incoming.positionMs);
    }

    QTimer::singleShot(1000, this, [this, operation] {
        if (m_closing) return;
        const PlaybackSnapshot preview = capturePreviewSnapshot();
        const PlaybackSnapshot program = captureProgramSnapshot();
        LOG_INFO("OBS dual media: {} result PVW #{} position={} ms | PGM #{} position={} ms.",
                 operation, preview.sourceId, preview.positionMs, program.sourceId, program.positionMs);
    });
    return true;
}

bool ObsDualMediaTestWindow::fadePreviewToProgram() {
    if (m_fadeActive || !m_program.backend || !m_preview.backend) {
        LOG_ERROR("OBS dual media: FADE rejected because a transition is active or Preview is unavailable.");
        return false;
    }

    const bool incomingTransportStarted = m_previewTransportStarted;
    const PlaybackSnapshot incomingSnapshot = capturePreviewSnapshot();
    const PlaybackSnapshot outgoingSnapshot = captureProgramSnapshot();
    if (!incomingSnapshot.valid) {
        LOG_ERROR("OBS dual media: FADE rejected because the PVW cue is unavailable.");
        return false;
    }
    if (m_playlistMode) stopPlaylist();
    const uint32_t duration = static_cast<uint32_t>(m_fadeDuration->currentData().toUInt());
    rememberProgramSnapshot(outgoingSnapshot);

    const bool transferPreviewRuntime = m_preview.backend->isOpen();
    std::unique_ptr<ObsPlaybackBackend> incoming;
    if (transferPreviewRuntime) {
        if (!incomingTransportStarted && m_preview.backend->supportsTransport()) {
            m_preview.backend->pause();
            if (!m_previewRuntimeMatchesStagedFrame) m_preview.backend->seekMs(incomingSnapshot.positionMs);
        }
        destroyDisplay(m_preview);
        incoming = std::move(m_preview.backend);
        m_preview.backend = std::make_unique<ObsPlaybackBackend>(m_context);
        m_previewTransportStarted = false;
        m_previewRuntimeMatchesStagedFrame = false;
        m_preview.backend->setAudioOutputEnabled(false);
        m_preview.backend->setVolume(m_preview.audioMuted ? 0.0f : m_preview.volume);
    } else {
        incoming = std::make_unique<ObsPlaybackBackend>(m_context);
    }
    incoming->setRenderMode(m_selectedProgramRenderMode);
    incoming->setVolume(m_program.audioMuted ? 0.0f : m_program.volume);
    // Prepare incoming media silently, then transfer the single WASAPI monitor atomically at FADE start.
    incoming->setAudioOutputEnabled(false);
    incoming->setLooping(incomingSnapshot.looping);
    if (!transferPreviewRuntime && !openCatalogSource(*incoming, incomingSnapshot.sourceId, true)) {
        LOG_ERROR("OBS dual media: FADE failed to create incoming Program runtime.");
        return false;
    }
    if (!transferPreviewRuntime && incoming->supportsTransport()) incoming->seekMs(incomingSnapshot.positionMs);

    const auto restoreTransferredPreview = [this, &incoming, transferPreviewRuntime] {
        if (!transferPreviewRuntime || !incoming) return;
        incoming->setAudioOutputEnabled(false);
        incoming->setRenderMode(ObsRenderMode::AspectFit);
        incoming->setVolume(m_preview.audioMuted ? 0.0f : m_preview.volume);
        m_preview.backend = std::move(incoming);
        initializeDisplay(m_preview);
        resizeDisplay(m_preview);
    };

    obs_source_t* transition = obs_source_create("fade_transition", "MediaSwitcher Fade", nullptr, nullptr);
    if (!transition) {
        LOG_ERROR("OBS dual media: FADE could not create OBS fade_transition source.");
        if (transferPreviewRuntime) restoreTransferredPreview();
        else incoming->close();
        return false;
    }

    // libobs owns the visual interpolation; the outgoing Program is staged back into PVW at completion.
    if (m_preview.backend->isOpen() && m_preview.backend->supportsTransport()) m_preview.backend->pause();
    m_program.backend->setAudioOutputEnabled(false);
    incoming->setAudioOutputEnabled(true);
    // The incoming source must own the WASAPI monitor before its first play.
    // Starting it silently and attaching the monitor afterward can leave the
    // new Program video live without an audio output after a FADE.
    incoming->play();
    obs_transition_set(transition, m_program.backend->nativeSource());
    incoming->setRenderSource(transition);
    m_program.fadeVideoCompleted.store(false);
    signal_handler_connect(obs_source_get_signal_handler(transition), "source_transition_video_stop", onFadeVideoStopped,
                           &m_program.fadeVideoCompleted);
    if (!obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, duration, incoming->nativeSource())) {
        LOG_ERROR("OBS dual media: FADE failed to start OBS transition.");
        signal_handler_disconnect(obs_source_get_signal_handler(transition), "source_transition_video_stop", onFadeVideoStopped,
                                  &m_program.fadeVideoCompleted);
        incoming->resetRenderSource();
        obs_transition_clear(transition);
        obs_source_release(transition);
        if (transferPreviewRuntime) restoreTransferredPreview();
        else incoming->close();
        return false;
    }

    m_program.fadeOutgoing = std::move(m_program.backend);
    m_fadeOutgoingSourceId = outgoingSnapshot.sourceId;
    m_fadeOutgoingSnapshot = outgoingSnapshot;
    m_program.backend = std::move(incoming);
    m_program.fadeTransition = transition;
    m_fadeActive = true;
    m_quickPlayButton->setEnabled(false);
    m_cutButton->setEnabled(false);
    m_fadeButton->setEnabled(false);
    m_fadeDuration->setEnabled(false);
    setProgramSourceId(incomingSnapshot.sourceId);
    LOG_INFO("OBS dual media: FADE started for {} ms. PVW paused at {} ms; incoming PGM started with audio monitoring at the same position.",
             duration, incomingSnapshot.positionMs);
    if (transferPreviewRuntime) {
        LOG_INFO("OBS dual media: FADE preserved the native PVW runtime and media clock without decoder recreation.");
    }
    return true;
}

void ObsDualMediaTestWindow::finishFadeIfComplete() {
    if (!m_fadeActive || !m_program.fadeTransition || m_fadeCleanupQueued) return;

    const bool videoStopSignalReceived = m_program.fadeVideoCompleted.load();
    const float transitionTime = obs_transition_get_time(m_program.fadeTransition);
    if (!videoStopSignalReceived && transitionTime < 1.0f) return;

    if (!videoStopSignalReceived) {
        LOG_WARN("OBS dual media: FADE reached libobs transition time {} without video-stop signal; finalizing PGM UI.",
                 transitionTime);
    }

    // The incoming source has been playing since the FADE began.  Detach the
    // transition from the visible view first, then release transition and old
    // decoder on the next GUI turn.  Calling play() again or freeing all three
    // objects in this render-adjacent callback caused the visible end-of-fade
    // hitch reported by operators.
    m_program.backend->resetRenderSource();
    m_fadeCleanupQueued = true;
    QTimer::singleShot(0, this, [this] {
        if (m_closing) return;
        releaseFadeTransition();
        if (m_program.fadeOutgoing) {
            // The old PGM keeps running for the duration of the dissolve. Its
            // PVW cue therefore belongs to the fade-end boundary, not the
            // timestamp captured when FADE started.
            if (m_program.fadeOutgoing->supportsTransport()) m_program.fadeOutgoing->pause();
            PlaybackSnapshot frozen = m_fadeOutgoingSnapshot;
            frozen.sourceId = m_fadeOutgoingSourceId;
            frozen.positionMs = m_program.fadeOutgoing->positionMs();
            frozen.durationMs = m_program.fadeOutgoing->durationMs();
            frozen.looping = m_program.fadeOutgoing->isLooping();
            frozen.valid = isCatalogSourceAvailable(frozen.sourceId);
            const QImage frozenFrame = captureBackendStill(*m_program.fadeOutgoing);
            rememberProgramSnapshot(frozen);
            if (!frozenFrame.isNull()) showSourceThumbnail(frozen.sourceId, frozenFrame, frozen, true);

            if (frozen.valid) {
                if (m_preview.backend) m_preview.backend->close();
                m_preview.backend = std::move(m_program.fadeOutgoing);
                m_preview.backend->setAudioOutputEnabled(false);
                m_preview.backend->setRenderMode(ObsRenderMode::AspectFit);
                m_preview.backend->setVolume(m_preview.audioMuted ? 0.0f : m_preview.volume);
                m_previewSourceId = frozen.sourceId;
                m_previewTransportStarted = false;
                m_previewRuntimeMatchesStagedFrame = true;
                m_stagedPreviewSnapshot = frozen;
                m_stagedPreviewFrame = QPixmap::fromImage(frozenFrame);
                m_stagedPreviewFrameSourceId = frozen.sourceId;
                m_preview.thumbnailGeneration.fetch_add(1);
                m_preview.thumbnailSourceId.store(frozen.sourceId);
                m_preview.thumbnailCaptureEnabled.store(false);
                m_preview.thumbnailCaptureContinuous.store(false);
                m_preview.thumbnailUpdatePending.store(false);
                m_preview.thumbnailFrameCounter = 0;
                m_preview.thumbnailStagedFrameCount = 0;
                initializeDisplay(m_preview);
                resizeDisplay(m_preview);
                updateStagedPreviewFrame();
                refreshCatalogUi();
            } else {
                m_program.fadeOutgoing->close();
                m_program.fadeOutgoing.reset();
            }
            m_fadeOutgoingSnapshot = {};
            m_fadeOutgoingSourceId = 0;
        }
        m_fadeActive = false;
        m_fadeCleanupQueued = false;
        m_quickPlayButton->setEnabled(true);
        m_cutButton->setEnabled(true);
        m_fadeButton->setEnabled(true);
        m_fadeDuration->setEnabled(true);
        LOG_INFO("OBS dual media: FADE completed without restarting the active Program source.");
    });
}

void ObsDualMediaTestWindow::releaseFadeTransition() {
    if (!m_program.fadeTransition) return;
    signal_handler_disconnect(obs_source_get_signal_handler(m_program.fadeTransition), "source_transition_video_stop",
                              onFadeVideoStopped, &m_program.fadeVideoCompleted);
    obs_transition_clear(m_program.fadeTransition);
    obs_source_release(m_program.fadeTransition);
    m_program.fadeTransition = nullptr;
}


void ObsDualMediaTestWindow::addCatalogSource() {
    addCatalogFiles(-1);
}

void ObsDualMediaTestWindow::addCatalogFiles(int sourceTypeFilter) {
    QFileDialog dialog(this, sourceTypeFilter == static_cast<int>(ObsCatalogSourceType::ImageFile)
        ? localized("Thêm nguồn hình ảnh", "Add image inputs")
        : localized("Thêm nguồn media", "Add media inputs"));
    dialog.setFileMode(QFileDialog::ExistingFiles);
    if (sourceTypeFilter == static_cast<int>(ObsCatalogSourceType::ImageFile)) {
        dialog.setNameFilter(localized("Hình ảnh (*.jpg *.jpeg *.png *.bmp *.webp *.gif *.tiff)",
                                       "Images (*.jpg *.jpeg *.png *.bmp *.webp *.gif *.tiff)"));
    } else {
        dialog.setNameFilter(localized(
            "Tệp media (*.mp4 *.mkv *.mov *.avi *.m4v *.webm *.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus);;"
            "Video (*.mp4 *.mkv *.mov *.avi *.m4v *.webm);;"
            "Âm thanh (*.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus)",
            "Media files (*.mp4 *.mkv *.mov *.avi *.m4v *.webm *.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus);;"
            "Video (*.mp4 *.mkv *.mov *.avi *.m4v *.webm);;"
            "Audio (*.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus)"));
    }
    if (dialog.exec() != QDialog::Accepted) return;

    const QStringList paths = dialog.selectedFiles();
    if (paths.isEmpty()) return;
    uint64_t firstAddedSourceId = 0;
    std::vector<uint64_t> addedSourceIds;
    addedSourceIds.reserve(static_cast<size_t>(paths.size()));
    for (const QString& path : paths) {
        const uint64_t id = m_sourceCatalog->add(std::filesystem::path(path.toStdWString()));
        if (firstAddedSourceId == 0) firstAddedSourceId = id;
        addedSourceIds.push_back(id);
        const auto source = m_sourceCatalog->find(id);
        LOG_INFO("OBS source catalog: added {} source #{}.", source ? obsCatalogSourceTypeName(source->type) : "UNKNOWN", id);
    }
    if (const auto firstAddedSource = m_sourceCatalog->find(firstAddedSourceId)) {
        stagePreviewSource(*firstAddedSource);
        LOG_INFO("OBS source catalog: opened first added source #{} as a paused OBS PVW source.", firstAddedSourceId);
    } else {
        refreshCatalogUi();
    }
    // Every catalog item gets an independent background poster. The PVW frame
    // tap remains an upgrade path, but it must never be the only producer: a
    // paused/covered native surface may not deliver the three GPU frames needed
    // by the asynchronous readback ring.
    for (const uint64_t id : addedSourceIds) {
        if (const auto source = m_sourceCatalog->find(id)) {
            ObsThumbnailService::instance().request(source->id, source->path, source->type);
        }
    }
    saveWorkspace();
}

void ObsDualMediaTestWindow::addRtspSource() {
    bool accepted = false;
    const QString endpoint = QInputDialog::getText(this, localized("Thêm camera RTSP", "Add RTSP camera"), QStringLiteral("RTSP URL:"),
                                                    QLineEdit::Normal, QStringLiteral("rtsp://"), &accepted).trimmed();
    if (!accepted || endpoint.isEmpty()) return;

    const QUrl url(endpoint);
    if (!url.isValid() || url.scheme().compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) != 0 || url.host().isEmpty()) {
        QMessageBox::warning(this, localized("URL RTSP không hợp lệ", "Invalid RTSP URL"),
                             localized("Hãy nhập URL RTSP đầy đủ, ví dụ rtsp://camera-host:554/stream.",
                                       "Enter a complete RTSP URL, for example rtsp://camera-host:554/stream."));
        return;
    }
    const QString suggestedName = url.host() + (url.port() > 0 ? QStringLiteral(":%1").arg(url.port()) : QString{});
    const QString displayName = QInputDialog::getText(this, localized("Đặt tên camera RTSP", "Name RTSP camera"),
                                                       localized("Tên nguồn:", "Input name:"),
                                                       QLineEdit::Normal, suggestedName, &accepted).trimmed();
    if (!accepted) return;
    const uint64_t id = m_sourceCatalog->addRtsp(endpoint.toUtf8().toStdString(),
                                                   (displayName.isEmpty() ? suggestedName : displayName).toUtf8().toStdString());
    LOG_INFO("OBS source catalog: added RTSP source #{} endpoint='{}'.", id, endpoint.toUtf8().constData());
    refreshCatalogUi();
    if (const auto source = m_sourceCatalog->find(id)) stagePreviewSource(*source);
    saveWorkspace();
}

void ObsDualMediaTestWindow::removeCatalogSource() {
    const auto* item = m_sourceCatalogList->currentItem();
    if (!item) return;
    removeCatalogSource(item->data(Qt::UserRole).toULongLong());
}

void ObsDualMediaTestWindow::retryCatalogSource(uint64_t sourceId) {
    if (m_fadeActive || !m_sourceCatalog) return;
    auto source = m_sourceCatalog->find(sourceId);
    if (!source || source->systemSource) return;

    if (source->type != ObsCatalogSourceType::RtspCamera &&
        !std::filesystem::is_regular_file(source->path)) {
        const QString oldPath = QString::fromStdWString(source->path.wstring());
        const QString replacement = QFileDialog::getOpenFileName(
            this, localized("Tìm lại file nguồn", "Relink Source File"),
            QFileInfo(oldPath).absolutePath(),
            localized("Tệp media và hình ảnh (*.mp4 *.mkv *.mov *.avi *.m4v *.webm *.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus *.jpg *.jpeg *.png *.bmp *.webp *.gif *.tiff);;Tất cả file (*)",
                      "Media and image files (*.mp4 *.mkv *.mov *.avi *.m4v *.webm *.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus *.jpg *.jpeg *.png *.bmp *.webp *.gif *.tiff);;All files (*)"));
        if (replacement.isEmpty()) {
            refreshCatalogUi();
            return;
        }
        if (!m_sourceCatalog->relinkLocalFile(sourceId, std::filesystem::path(replacement.toStdWString()))) {
            QMessageBox::warning(this, localized("Không thể liên kết file", "Relink failed"),
                                 localized("File được chọn không hợp lệ hoặc không thể truy cập.",
                                           "The selected file is invalid or inaccessible."));
            return;
        }
        source = m_sourceCatalog->find(sourceId);
        if (!source) return;
        saveWorkspace();
        LOG_INFO("OBS source catalog: source #{} relinked to '{}'.", sourceId, replacement.toUtf8().constData());
    }

    if (sourceId == m_programSourceId && m_program.backend) {
        const PlaybackSnapshot snapshot = captureProgramSnapshot();
        m_program.backend->close();
        m_program.backend->setAudioOutputEnabled(true);
        m_program.backend->setVolume(m_program.audioMuted ? 0.0f : m_program.volume);
        m_program.backend->setLooping(snapshot.looping);
        if (!m_program.backend->open(*source)) {
            QMessageBox::warning(this, localized("Không thể mở lại nguồn", "Retry failed"),
                                 localized("Nguồn chưa thể kết nối hoặc mở lại. Hãy kiểm tra file, mạng và RTSP URL.",
                                           "The source could not be reopened. Check the file, network and RTSP URL."));
            refreshCatalogUi();
            return;
        }
        if (obsCatalogSourceHasTimeline(source->type) && snapshot.positionMs > 0) {
            m_program.backend->seekMs(snapshot.positionMs);
        }
        m_program.backend->play();
        initializeDisplay(m_program);
        resizeDisplay(m_program);
    } else {
        stagePreviewSource(*source);
    }

    if (source->type != ObsCatalogSourceType::RtspCamera) {
        ObsThumbnailService::instance().request(source->id, source->path, source->type);
    }
    refreshCatalogUi();
    LOG_INFO("OBS source catalog: retry requested for source #{}.", sourceId);
}

void ObsDualMediaTestWindow::removeCatalogSource(uint64_t sourceId) {
    if (m_fadeActive) {
        LOG_WARN("OBS source catalog: source #{} removal rejected while FADE is active.", sourceId);
        return;
    }
    const auto source = m_sourceCatalog->find(sourceId);
    if (source && source->systemSource) {
        QMessageBox::information(this, localized("Nguồn hệ thống", "System source"),
                                 localized("Nguồn nền trống là nguồn an toàn của hệ thống và không thể xoá.",
                                           "The Blank source is a protected system source and cannot be removed."));
        return;
    }

    const bool clearsPreview = sourceId == m_previewSourceId;
    const bool inTripleZone = m_layoutMode == LayoutMode::Triple &&
        (sourceId == m_tripleZones[0].sourceId || sourceId == m_tripleZones[1].sourceId || sourceId == m_tripleZones[2].sourceId);
    if ((m_layoutMode == LayoutMode::Single && sourceId == m_programSourceId) || inTripleZone) {
        QMessageBox::information(this, localized("PGM đang phát", "PGM is live"),
                                 localized("Không thể xoá nguồn đang phát trên PGM/Vùng. Hãy chuyển nguồn khác lên trước.",
                                           "The source currently live on PGM/Zone cannot be removed. Switch another source first."));
        LOG_WARN("OBS source catalog: removal of live Program source #{} rejected.", sourceId);
        return;
    }

    const auto& sources = m_sourceCatalog->sources();
    const auto removedIt = std::find_if(sources.begin(), sources.end(), [sourceId](const ObsCatalogSource& candidate) {
        return candidate.id == sourceId;
    });
    const size_t removedIndex = removedIt == sources.end() ? sources.size()
        : static_cast<size_t>(std::distance(sources.begin(), removedIt));
    uint64_t replacementPreviewId = 0;
    if (clearsPreview) {
        for (size_t index = removedIndex + 1; index < sources.size(); ++index) {
            if (!sources[index].systemSource && sources[index].id != sourceId) {
                replacementPreviewId = sources[index].id;
                break;
            }
        }
        if (replacementPreviewId == 0) {
            for (size_t index = removedIndex; index > 0; --index) {
                const auto& candidate = sources[index - 1];
                if (!candidate.systemSource && candidate.id != sourceId) {
                    replacementPreviewId = candidate.id;
                    break;
                }
            }
        }
    }
    for (size_t index = m_playlist->size(); index > 0; --index)
        if (m_playlist->sourceIdAt(index - 1) == sourceId) m_playlist->removeAt(index - 1);
    if (!m_sourceCatalog->remove(sourceId)) return;
    m_sourceThumbnails.erase(sourceId);
    m_sourcePosters.erase(sourceId);
    m_sourceTimelineFrames.erase(sourceId);
    m_sourceThumbnailSnapshots.erase(sourceId);
    m_sourceTimelineFrameSnapshots.erase(sourceId);
    m_sourcesWithTimelineThumbnail.erase(sourceId);
    m_pausedSourceRuntimes.erase(sourceId);
    m_lastProgramSnapshots.erase(sourceId);
    if (clearsPreview) {
        if (replacementPreviewId == 0) {
            const auto blank = std::find_if(m_sourceCatalog->sources().begin(), m_sourceCatalog->sources().end(),
                [](const ObsCatalogSource& candidate) { return candidate.systemSource; });
            if (blank != m_sourceCatalog->sources().end()) replacementPreviewId = blank->id;
        }
        if (const auto replacement = m_sourceCatalog->find(replacementPreviewId)) stagePreviewSource(*replacement);
        else clearPreviewSource();
    }
    LOG_INFO("OBS source catalog: removed source #{}; previewReplacement=#{}.", sourceId, replacementPreviewId);
    refreshCatalogUi();
    refreshPlaylistUi();
    saveWorkspace();
}

void ObsDualMediaTestWindow::addSelectedCatalogSourceToPlaylist() {
    const auto* item = m_sourceCatalogList->currentItem();
    if (!item) return;
    const uint64_t sourceId = item->data(Qt::UserRole).toULongLong();
    const auto source = m_sourceCatalog->find(sourceId);
    if (!source || !obsCatalogSourceHasTimeline(source->type)) {
        QMessageBox::information(this, localized("Nguồn Playlist", "Playlist source"),
                                 localized("Camera RTSP và ảnh tĩnh là nguồn trực tiếp/tĩnh nên không thể thêm vào Playlist timeline.",
                                           "RTSP cameras and still images are live/static sources and cannot be added to the timeline playlist."));
        return;
    }
    m_playlist->addSource(sourceId);
    refreshPlaylistUi();
    saveWorkspace();
}

void ObsDualMediaTestWindow::removeSelectedPlaylistStep() {
    const int row = m_playlistList->currentRow();
    if (row >= 0 && static_cast<size_t>(row) < m_playlistDraft.size()) {
        m_playlistDraft.erase(m_playlistDraft.begin() + row);
    }
    refreshPlaylistUi();
}

void ObsDualMediaTestWindow::movePlaylistStep(int delta) {
    const int row = m_playlistList->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= static_cast<int>(m_playlistDraft.size())) return;
    std::iter_swap(m_playlistDraft.begin() + row, m_playlistDraft.begin() + target);
    refreshPlaylistUi();
    m_playlistList->setCurrentRow(target);
}

bool ObsDualMediaTestWindow::startPlaylist() {
    if (m_playlist->empty() || m_fadeActive) return false;
    if (m_playlistLoop) m_playlist->setLoop(m_playlistLoop->isChecked());
    if (m_autoNext) m_playlist->setAutoNext(m_autoNext->isChecked());
    m_playlistMode = true;
    refreshPlaylistButtonUi();
    if (!activatePlaylistProgram("Start")) {
        m_playlistMode = false;
        refreshPlaylistButtonUi();
        return false;
    }
    return true;
}

void ObsDualMediaTestWindow::stopPlaylist() {
    if (!m_playlistMode) return;
    m_playlistMode = false;
    if (m_program.backend && m_program.backend->supportsTransport()) m_program.backend->pause();
    refreshPlaylistButtonUi();
    refreshPlaylistUi();
    LOG_INFO("OBS playlist: stopped; PGM paused in place and PVW remains untouched.");
}

bool ObsDualMediaTestWindow::activatePlaylistProgram(const char* reason) {
    if (!m_playlistMode || m_playlist->empty() || m_fadeActive) return false;
    const auto source = m_sourceCatalog->find(m_playlist->currentSourceId());
    if (!source) { stopPlaylist(); return false; }
    if (m_program.backend && m_program.backend->isOpen() && m_programSourceId != 0) {
        if (m_program.backend->supportsTransport()) m_program.backend->pause();
        const PlaybackSnapshot outgoing = captureProgramSnapshot();
        const QImage outgoingFrame = captureBackendStill(*m_program.backend);
        rememberProgramSnapshot(outgoing);
        if (!outgoingFrame.isNull()) showSourceThumbnail(outgoing.sourceId, outgoingFrame, outgoing, true);
        m_program.backend->resetRenderSource();
        m_program.backend->setAudioOutputEnabled(false);
        if (outgoing.valid && obsCatalogSourceHasTimeline(m_sourceCatalog->find(outgoing.sourceId)->type)) {
            m_pausedSourceRuntimes[outgoing.sourceId] = std::move(m_program.backend);
        }
    }
    if (m_program.backend) m_program.backend->close();
    bool restoredRuntime = false;
    if (const auto cached = m_pausedSourceRuntimes.find(source->id);
        cached != m_pausedSourceRuntimes.end() && cached->second && cached->second->isOpen()) {
        m_program.backend = std::move(cached->second);
        m_pausedSourceRuntimes.erase(cached);
        restoredRuntime = true;
    }
    if (!m_program.backend) m_program.backend = std::make_unique<ObsPlaybackBackend>(m_context);
    m_program.backend->setRenderMode(m_selectedProgramRenderMode);
    m_program.backend->setVolume(m_program.audioMuted ? 0.0f : m_program.volume);
    m_program.backend->setAudioOutputEnabled(true);
    m_program.backend->setLooping(false);
    if (!restoredRuntime && !m_program.backend->open(*source)) return false;
    m_program.backend->play();
    initializeDisplay(m_program);
    resizeDisplay(m_program);
    setProgramSourceId(source->id);
    LOG_INFO("OBS playlist: {} activated PGM source #{}; PVW untouched; restoredRuntime={}.",
             reason, source->id, restoredRuntime);
    refreshPlaylistUi();
    return true;
}

bool ObsDualMediaTestWindow::navigatePlaylist(bool forward, const char* reason) {
    if (!m_playlistMode || m_fadeActive) return false;
    if (!(forward ? m_playlist->advance() : m_playlist->previous())) {
        if (std::string_view(reason) == "Auto Next") stopPlaylist();
        return false;
    }
    return activatePlaylistProgram(reason);
}

void ObsDualMediaTestWindow::refreshCatalogUi() {
    m_catalogThumbnailLabels.clear();
    m_sourceCatalogList->clear();
    const int selectedType = m_sourceTypeFilter ? m_sourceTypeFilter->currentData().toInt() : -1;
    const int thumbnailHeight = m_catalogThumbnailWidth * 9 / 16;
    const QSize itemSize(m_catalogThumbnailWidth + 14, thumbnailHeight + 10);
    const auto& sources = m_sourceCatalog->sources();
    for (size_t index = 0; index < sources.size(); ++index) {
        const auto& source = sources[index];
        if (selectedType >= 0 && selectedType != static_cast<int>(source.type)) continue;
        const int displaySlot = static_cast<int>(index + 1);
        const QFileInfo info(QString::fromStdWString(source.path.wstring()));
        const bool missingLocalFile = source.type != ObsCatalogSourceType::RtspCamera &&
            source.type != ObsCatalogSourceType::ColorBlank && !info.isFile();
        const bool vietnamese = m_language == UiLanguage::Vietnamese;
        const QString sourceName = catalogSourceName(source, vietnamese);
        const QString availability = missingLocalFile ? localized("THIẾU FILE", "MISSING")
            : catalogSourceBadge(source, vietnamese);
        auto* item = new QListWidgetItem(QStringLiteral("#%1  [%2] %3").arg(displaySlot).arg(availability, sourceName), m_sourceCatalogList);
        item->setSizeHint(itemSize);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(source.id));
        item->setToolTip(source.type == ObsCatalogSourceType::RtspCamera ? QString::fromUtf8(source.endpoint.c_str()) : info.absoluteFilePath());
        auto* tile = new CatalogTileWidget([this, item, source, missingLocalFile] {
            m_sourceCatalogList->setCurrentItem(item);
            if (m_fadeActive) return;
            if (missingLocalFile) retryCatalogSource(source.id);
            else stagePreviewSource(source);
        }, m_sourceCatalogList);
        tile->setFixedSize(itemSize);
        const bool isSingleProgram = m_layoutMode == LayoutMode::Single &&
            source.id == m_programSourceId && m_program.backend && m_program.backend->isOpen();
        const bool isZoneLeft = m_layoutMode == LayoutMode::Triple &&
            source.id == m_tripleZones[0].sourceId && m_tripleZones[0].activeBackend && m_tripleZones[0].activeBackend->isOpen();
        const bool isZoneMain = m_layoutMode == LayoutMode::Triple &&
            source.id == m_tripleZones[1].sourceId && m_tripleZones[1].activeBackend && m_tripleZones[1].activeBackend->isOpen();
        const bool isZoneRight = m_layoutMode == LayoutMode::Triple &&
            source.id == m_tripleZones[2].sourceId && m_tripleZones[2].activeBackend && m_tripleZones[2].activeBackend->isOpen();
        const bool isProgram = isSingleProgram || isZoneLeft || isZoneMain || isZoneRight;
        const bool isPreview = source.id == m_previewSourceId;
        const QString borderColor = missingLocalFile ? QStringLiteral("#d34a5d")
            : (isProgram ? QStringLiteral("#8c4f5d")
            : (isPreview ? QStringLiteral("#3f91aa") : QStringLiteral("#303944")));
        const int borderWidth = (missingLocalFile || isProgram || isPreview) ? 2 : 1;
        tile->setStyleSheet(QStringLiteral("background: #1b2128; border: %1px solid %2;")
            .arg(borderWidth).arg(borderColor));
        auto* tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(4, 4, 4, 4);
        tileLayout->setSpacing(0);
        tileLayout->setAlignment(Qt::AlignTop);
        // Catalog thumbnails are static during playback.  A live tile used a
        // third obs_display and competed with PGM on the render thread.
        QWidget* previewWidget = nullptr;
        {
            const auto thumbnail = m_sourceThumbnails.find(source.id);
            QPixmap previewPixmap;
            if (thumbnail != m_sourceThumbnails.end()) previewPixmap = fitCatalogThumbnail(thumbnail->second, m_catalogThumbnailWidth);
            else if (source.type == ObsCatalogSourceType::RtspCamera) {
                previewPixmap = fitCatalogThumbnail(style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(m_catalogThumbnailWidth, thumbnailHeight),
                                                    m_catalogThumbnailWidth);
            } else if (source.type == ObsCatalogSourceType::ColorBlank) {
                QPixmap blank(m_catalogThumbnailWidth, thumbnailHeight);
                blank.fill(Qt::black);
                previewPixmap = fitCatalogThumbnail(blank, m_catalogThumbnailWidth);
            } else {
                previewPixmap = fitCatalogThumbnail(QFileIconProvider().icon(info).pixmap(m_catalogThumbnailWidth, thumbnailHeight),
                                                    m_catalogThumbnailWidth);
            }
            auto* thumbnailLabel = new QLabel(tile);
            thumbnailLabel->setFixedSize(m_catalogThumbnailWidth, thumbnailHeight);
            thumbnailLabel->setPixmap(previewPixmap);
            thumbnailLabel->setAlignment(Qt::AlignCenter);
            thumbnailLabel->setStyleSheet(QStringLiteral("background: #0e1216; border: 0;"));
            m_catalogThumbnailLabels[source.id] = thumbnailLabel;
            previewWidget = thumbnailLabel;
        }
        previewWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
        tileLayout->addWidget(previewWidget, 0, Qt::AlignHCenter);
        QString zoneTag;
        if (m_layoutMode == LayoutMode::Triple && isProgram) {
            QStringList tags;
            if (isZoneLeft) tags << QStringLiteral("L");
            if (isZoneMain) tags << QStringLiteral("M");
            if (isZoneRight) tags << QStringLiteral("R");
            zoneTag = QStringLiteral("PGM:") + tags.join(QLatin1Char('/'));
        } else if (isSingleProgram) {
            zoneTag = QStringLiteral("PGM");
        } else {
            zoneTag = catalogSourceBadge(source, vietnamese);
        }
        auto* title = new QLabel(QStringLiteral("#%1  [%2]%3 %4")
            .arg(displaySlot)
            .arg(missingLocalFile ? localized("THIẾU FILE", "MISSING") : zoneTag)
            .arg(isPreview && !isProgram ? QStringLiteral(" PVW") : QString{})
            .arg(sourceName), tile);
        title->setFixedHeight(19);
        title->setWordWrap(false);
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        title->setStyleSheet(QStringLiteral(
            "color: #f0f5f8; background: rgba(10, 14, 18, 205); font-size: 10px; border: 0; padding: 0 4px;"));
        title->setText(title->fontMetrics().elidedText(title->text(), Qt::ElideRight, m_catalogThumbnailWidth));
        title->setGeometry(4, 4, m_catalogThumbnailWidth, 19);
        title->raise();
        if (!source.systemSource) {
            auto* removeButton = new QToolButton(tile);
            removeButton->setFixedSize(16, 16);
            removeButton->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
            removeButton->setToolTip(localized("Xoá nguồn", "Remove source"));
            removeButton->setStyleSheet(QStringLiteral(
                "QToolButton { background: #8f2638; border: 1px solid #cf7685; border-radius: 1px; padding: 0; }"
                "QToolButton:hover { background: #c93449; border-color: #ffd0d6; }"
                "QToolButton:pressed { background: #771827; }"));
            removeButton->move(itemSize.width() - 17, -1);
            connect(removeButton, &QToolButton::clicked, this, [this, sourceId = source.id] { removeCatalogSource(sourceId); });
            removeButton->raise();

            if (missingLocalFile || source.type == ObsCatalogSourceType::RtspCamera) {
                auto* retryButton = new QToolButton(tile);
                retryButton->setFixedSize(18, 16);
                retryButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
                retryButton->setToolTip(localized("Thử kết nối/mở lại nguồn", "Retry source"));
                retryButton->setStyleSheet(QStringLiteral(
                    "QToolButton { background: #245b70; border: 1px solid #68b5cd; border-radius: 1px; padding: 0; }"
                    "QToolButton:hover { background: #3383a0; border-color: #b9efff; }"));
                retryButton->move(itemSize.width() - 37, -1);
                connect(retryButton, &QToolButton::clicked, this,
                        [this, sourceId = source.id] { retryCatalogSource(sourceId); });
                retryButton->raise();
            }
        }
        m_sourceCatalogList->setItemWidget(item, tile);
    }
}

void ObsDualMediaTestWindow::setCatalogThumbnailSize(int width) {
    if (!m_sourceCatalogList) return;
    m_catalogThumbnailWidth = width;
    const int height = width * 9 / 16;
    m_sourceCatalogList->setIconSize(QSize(width, height));
    m_sourceCatalogList->setGridSize(QSize(width + 14, height + 10));
    refreshCatalogUi();
}

void ObsDualMediaTestWindow::setProjectFrameRate(int index) {
    if (!m_projectFrameRate || index < 0) return;

    const ObsVideoFrameRate requested{
        m_projectFrameRate->itemData(index, Qt::UserRole).toUInt(),
        m_projectFrameRate->itemData(index, Qt::UserRole + 1).toUInt(),
    };
    if (m_context.setVideoFrameRate(requested)) {
        resizeDisplay(m_preview);
        resizeDisplay(m_program);
        LOG_INFO("OBS app: Project FPS selected: {}/{}.", requested.numerator, requested.denominator);
        return;
    }

    const ObsVideoFrameRate active = m_context.videoFrameRate();
    const auto& frameRates = ObsContext::supportedVideoFrameRates();
    const auto activeIt = std::find(frameRates.begin(), frameRates.end(), active);
    const QSignalBlocker blocker(m_projectFrameRate);
    if (activeIt != frameRates.end()) {
        m_projectFrameRate->setCurrentIndex(static_cast<int>(std::distance(frameRates.begin(), activeIt)));
    }
    QMessageBox::warning(this, localized("FPS dự án", "Project FPS"),
                         localized("Không thể đổi FPS khi OBS đang có output hoạt động.",
                                   "FPS cannot be changed while an OBS output is active."));
}

void ObsDualMediaTestWindow::setProgramRenderMode(int index) {
    if (!m_programRenderMode || index < 0) return;
    m_selectedProgramRenderMode = static_cast<ObsRenderMode>(m_programRenderMode->itemData(index).toInt());
    if (m_program.backend) m_program.backend->setRenderMode(m_selectedProgramRenderMode);
    if (m_program.fadeOutgoing) m_program.fadeOutgoing->setRenderMode(m_selectedProgramRenderMode);
    LOG_INFO("OBS Program render mode changed to {}.",
             m_selectedProgramRenderMode == ObsRenderMode::FitToScreen ? "FIT" : "DEFAULT");
}

void ObsDualMediaTestWindow::setProgramSourceId(uint64_t sourceId, bool refreshCatalog) {
    m_programSourceId = sourceId;
    const auto source = m_sourceCatalog ? m_sourceCatalog->find(sourceId) : std::nullopt;
    const bool captureVideo = source && (source->type == ObsCatalogSourceType::VideoFile ||
        source->type == ObsCatalogSourceType::ImageFile || source->type == ObsCatalogSourceType::RtspCamera);
    const bool captureContinuous = source && (source->type == ObsCatalogSourceType::VideoFile ||
        source->type == ObsCatalogSourceType::RtspCamera);
    m_program.thumbnailGeneration.fetch_add(1);
    m_program.thumbnailSourceId.store(sourceId);
    m_program.thumbnailCaptureContinuous.store(captureContinuous);
    m_program.thumbnailCaptureEnabled.store(captureVideo);
    m_program.thumbnailUpdatePending.store(false);
    m_program.thumbnailFrameCounter = 0;
    m_program.thumbnailStagedFrameCount = 0;
    if (refreshCatalog) refreshCatalogUi();
}

void ObsDualMediaTestWindow::refreshPlaylistButtonUi() {
    if (!m_openPlaylistButton || !m_playlist) return;

    const bool hasPlaylist = !m_playlist->empty();
    const bool canNavigate = hasPlaylist && m_playlistMode && !m_fadeActive && m_playlist->size() > 1;
    if (m_playlistPreviousButton) {
        m_playlistPreviousButton->setEnabled(canNavigate &&
            (m_playlist->isLooping() || m_playlist->currentIndex() > 0));
    }
    if (m_playlistNextButton) {
        m_playlistNextButton->setEnabled(canNavigate &&
            (m_playlist->isLooping() || m_playlist->currentIndex() + 1 < m_playlist->size()));
    }

    if (m_playlistMode) {
        m_openPlaylistButton->setText(localized("Dừng DS", "Stop Playlist"));
        m_openPlaylistButton->setToolTip(localized("Dừng danh sách phát", "Stop playlist"));
        m_openPlaylistButton->setStyleSheet(QStringLiteral(
            "QPushButton { background: #9f2636; color: #ffffff; border: 1px solid #ff8794; font-weight: bold; }"
            "QPushButton:hover { background: #c43146; }"
            "QPushButton:pressed { background: #7e1d2b; }"));
        return;
    }

    if (!m_playlist->empty()) {
        m_openPlaylistButton->setText(localized("Phát DS", "Play Playlist"));
        m_openPlaylistButton->setToolTip(localized("Phát danh sách", "Play playlist"));
        m_openPlaylistButton->setStyleSheet(QStringLiteral(
            "QPushButton { background: #168044; color: #ffffff; border: 1px solid #55c47f; font-weight: bold; }"
            "QPushButton:hover { background: #1b9952; }"
            "QPushButton:pressed { background: #106934; }"));
        return;
    }

    m_openPlaylistButton->setText(localized("Danh sách", "Playlist"));
    m_openPlaylistButton->setToolTip(localized("Mở trình quản lý danh sách phát", "Open playlist manager"));
    m_openPlaylistButton->setStyleSheet(QString());
}

void ObsDualMediaTestWindow::refreshPlaylistUi() {
    refreshPlaylistButtonUi();
    if (!m_playlistList || !m_playlistStatus) return;

    const bool editing = m_playlistEditing && !m_playlistMode;
    const size_t itemCount = editing ? m_playlistDraft.size() : m_playlist->size();

    m_playlistList->clear();
    for (size_t index = 0; index < itemCount; ++index) {
        const uint64_t sourceId = editing ? m_playlistDraft[index] : m_playlist->sourceIdAt(index);
        const auto source = m_sourceCatalog->find(sourceId);
        const QString name = source ? catalogSourceName(*source, m_language == UiLanguage::Vietnamese)
                                    : localized("Thiếu nguồn", "Missing source");
        auto* item = new QListWidgetItem(QStringLiteral("%1. %2").arg(index + 1).arg(name), m_playlistList);
        if (m_playlistMode && index == m_playlist->currentIndex()) item->setText(item->text() + QStringLiteral("  [PGM]"));
    }

    if (editing) {
        m_playlistStatus->setText(itemCount == 0 ? localized("Danh sách: Trống", "Playlist: Empty")
            : localized("Danh sách: Đang chỉnh sửa | %1 mục", "Playlist: Editing | %1 item(s)").arg(itemCount));
    } else {
        m_playlistStatus->setText(m_playlist->empty() ? localized("Danh sách: Trống", "Playlist: Empty")
            : localized("Danh sách: %1 | Chỉ PGM %2/%3", "Playlist: %1 | PGM-only %2/%3")
                .arg(m_playlistMode ? localized("Đang chạy", "Running") : localized("Sẵn sàng", "Ready"))
                .arg(m_playlist->currentIndex() + 1).arg(m_playlist->size()));
    }
}

void ObsDualMediaTestWindow::showPlaylistManager() {
    if (!m_playlistDialog) {
        m_playlistDialog = new QDialog(this);
        bindLocalizedProperty(m_playlistDialog, "windowTitle", "Quản lý danh sách phát OBS", "OBS Playlist Manager");
        m_playlistDialog->resize(760, 470);
        auto* layout = new QVBoxLayout(m_playlistDialog);
        auto* columns = new QHBoxLayout();

        auto* availableColumn = new QVBoxLayout();
        auto* availableLabel = new QLabel(m_playlistDialog);
        bindLocalizedProperty(availableLabel, "text", "Nguồn có sẵn", "Available Inputs");
        availableColumn->addWidget(availableLabel);
        auto* available = new QListWidget(m_playlistDialog);
        available->setObjectName(QStringLiteral("playlistAvailableInputs"));
        availableColumn->addWidget(available, 1);
        columns->addLayout(availableColumn, 1);

        auto* commands = new QVBoxLayout();
        auto* add = new QPushButton(QStringLiteral(">"), m_playlistDialog);
        auto* remove = new QPushButton(QStringLiteral("<"), m_playlistDialog);
        commands->addStretch();
        commands->addWidget(add);
        commands->addWidget(remove);
        commands->addStretch();
        columns->addLayout(commands);

        auto* playlistColumn = new QVBoxLayout();
        auto* playlistLabel = new QLabel(m_playlistDialog);
        bindLocalizedProperty(playlistLabel, "text", "Danh sách phát PGM", "PGM Playlist");
        playlistColumn->addWidget(playlistLabel);
        m_playlistList = new QListWidget(m_playlistDialog);
        playlistColumn->addWidget(m_playlistList, 1);
        auto* reorder = new QHBoxLayout();
        auto* up = new QPushButton(QStringLiteral("Up"), m_playlistDialog);
        auto* down = new QPushButton(QStringLiteral("Down"), m_playlistDialog);
        bindLocalizedProperty(up, "text", "Lên", "Up");
        bindLocalizedProperty(down, "text", "Xuống", "Down");
        reorder->addWidget(up);
        reorder->addWidget(down);
        playlistColumn->addLayout(reorder);
        columns->addLayout(playlistColumn, 1);
        layout->addLayout(columns, 1);

        auto* footer = new QHBoxLayout();
        m_playlistLoop = new QCheckBox(QStringLiteral("Loop Playlist"), m_playlistDialog);
        m_autoNext = new QCheckBox(QStringLiteral("Auto Next"), m_playlistDialog);
        bindLocalizedProperty(m_playlistLoop, "text", "Lặp danh sách", "Loop Playlist");
        bindLocalizedProperty(m_autoNext, "text", "Tự động tiếp", "Auto Next");
        m_playlistLoop->setChecked(m_playlist->isLooping());
        m_autoNext->setChecked(m_playlist->isAutoNext());
        m_playlistStatus = new QLabel(m_playlistDialog);
        m_playlistSaveButton = new QPushButton(QStringLiteral("Save"), m_playlistDialog);
        m_playlistCancelButton = new QPushButton(QStringLiteral("Cancel"), m_playlistDialog);
        bindLocalizedProperty(m_playlistSaveButton, "text", "Lưu", "Save");
        bindLocalizedProperty(m_playlistCancelButton, "text", "Huỷ", "Cancel");
        footer->addWidget(m_playlistLoop);
        footer->addWidget(m_autoNext);
        footer->addWidget(m_playlistStatus, 1);
        footer->addWidget(m_playlistSaveButton);
        footer->addWidget(m_playlistCancelButton);
        layout->addLayout(footer);

        connect(add, &QPushButton::clicked, this, [this, available] {
            const auto* item = available->currentItem();
            if (!item) return;
            m_playlistDraft.push_back(item->data(Qt::UserRole).toULongLong());
            refreshPlaylistUi();
        });
        connect(remove, &QPushButton::clicked, this, &ObsDualMediaTestWindow::removeSelectedPlaylistStep);
        connect(up, &QPushButton::clicked, this, [this] { movePlaylistStep(-1); });
        connect(down, &QPushButton::clicked, this, [this] { movePlaylistStep(1); });
        connect(m_playlistSaveButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::savePlaylistManager);
        connect(m_playlistCancelButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::cancelPlaylistManager);
        connect(m_playlistDialog, &QDialog::rejected, this, &ObsDualMediaTestWindow::cancelPlaylistManager);
        connect(m_playlistLoop, &QCheckBox::toggled, this, [this](bool) { refreshPlaylistUi(); });
        connect(m_autoNext, &QCheckBox::toggled, this, [this](bool) { refreshPlaylistUi(); });
    }

    m_playlistDraft.clear();
    for (size_t index = 0; index < m_playlist->size(); ++index) m_playlistDraft.push_back(m_playlist->sourceIdAt(index));
    m_playlistEditing = true;
    if (m_playlistLoop) {
        const QSignalBlocker blocker(m_playlistLoop);
        m_playlistLoop->setChecked(m_playlist->isLooping());
    }
    if (m_autoNext) {
        const QSignalBlocker blocker(m_autoNext);
        m_autoNext->setChecked(m_playlist->isAutoNext());
    }

    auto* available = m_playlistDialog->findChild<QListWidget*>(QStringLiteral("playlistAvailableInputs"));
    available->clear();
    for (const auto& source : m_sourceCatalog->sources()) {
        if (!obsCatalogSourceHasTimeline(source.type)) continue;
        const QFileInfo info(QString::fromStdWString(source.path.wstring()));
        auto* item = new QListWidgetItem(QStringLiteral("#%1  %2").arg(source.id).arg(info.fileName()), available);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(source.id));
    }
    refreshPlaylistUi();
    m_playlistDialog->show();
    m_playlistDialog->raise();
    m_playlistDialog->activateWindow();
}

void ObsDualMediaTestWindow::savePlaylistManager() {
    if (!m_playlist) return;
    m_playlist->clear();
    for (const uint64_t sourceId : m_playlistDraft) m_playlist->addSource(sourceId);
    if (m_playlistLoop) m_playlist->setLoop(m_playlistLoop->isChecked());
    if (m_autoNext) m_playlist->setAutoNext(m_autoNext->isChecked());
    m_playlistDraft.clear();
    m_playlistEditing = false;
    if (m_playlistDialog) m_playlistDialog->hide();
    refreshPlaylistUi();
    saveWorkspace();
    LOG_INFO("OBS playlist: saved {} step(s); playback remains stopped until Play Playlist is pressed.", m_playlist->size());
}

void ObsDualMediaTestWindow::cancelPlaylistManager() {
    m_playlistDraft.clear();
    m_playlistEditing = false;
    if (m_playlistDialog) m_playlistDialog->hide();
    refreshPlaylistUi();
    LOG_INFO("OBS playlist: edit cancelled.");
}

uint64_t ObsDualMediaTestWindow::restoreWorkspace() {
    if (!m_workspaceStateStore || !m_sourceCatalog || !m_playlist) return 0;

    WorkspaceState state;
    std::string error;
    const WorkspaceLoadStatus status = m_workspaceStateStore->load(state, error);
    if (status == WorkspaceLoadStatus::NotFound) {
        LOG_INFO("OBS workspace: no saved workspace found at '{}'.",
                 m_workspaceStateStore->filePath().string());
        return 0;
    }
    if (status != WorkspaceLoadStatus::Loaded) {
        LOG_WARN("OBS workspace: ignored unreadable workspace '{}': {}",
                 m_workspaceStateStore->filePath().string(), error);
        return 0;
    }

    applyWorkspaceSettings(state, false);
    const uint64_t restoredPreviewId = appendWorkspaceState(state);
    LOG_INFO("OBS workspace: automatic session loaded from '{}'.", m_workspaceStateStore->filePath().string());
    return restoredPreviewId;
}

uint64_t ObsDualMediaTestWindow::appendWorkspaceState(const WorkspaceState& state) {
    if (!m_sourceCatalog || !m_playlist) return 0;

    std::unordered_map<uint64_t, uint64_t> restoredIds;
    restoredIds.reserve(state.sources.size());
    size_t skippedSources = 0;
    for (const WorkspaceSourceRecord& savedSource : state.sources) {
        uint64_t restoredId = 0;
        if (savedSource.type == ObsCatalogSourceType::RtspCamera) {
            const QUrl endpoint(QString::fromUtf8(savedSource.endpoint.c_str()));
            if (endpoint.isValid() && endpoint.scheme().compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) == 0 &&
                !endpoint.host().isEmpty()) {
                restoredId = m_sourceCatalog->addRtsp(savedSource.endpoint, savedSource.displayName);
            }
        } else if (savedSource.type != ObsCatalogSourceType::ColorBlank && !savedSource.path.empty()) {
            restoredId = m_sourceCatalog->add(savedSource.path);
        }

        if (restoredId == 0) {
            ++skippedSources;
            continue;
        }
        restoredIds.emplace(savedSource.sourceId, restoredId);
        if (const auto source = m_sourceCatalog->find(restoredId);
            source && source->type != ObsCatalogSourceType::RtspCamera) {
            ObsThumbnailService::instance().request(source->id, source->path, source->type);
        }
    }

    m_playlist->clear();
    m_playlist->setLoop(state.playlistLoop);
    m_playlist->setAutoNext(state.playlistAutoNext);
    for (const uint64_t savedSourceId : state.playlistSourceIds) {
        const auto restored = restoredIds.find(savedSourceId);
        if (restored == restoredIds.end()) continue;
        const auto source = m_sourceCatalog->find(restored->second);
        if (source && obsCatalogSourceHasTimeline(source->type)) m_playlist->addSource(source->id);
    }

    uint64_t restoredPreviewId = 0;
    if (const auto restored = restoredIds.find(state.previewSourceId); restored != restoredIds.end()) {
        restoredPreviewId = restored->second;
        m_lastProgramSnapshots[restoredPreviewId] = {
            restoredPreviewId, state.previewPositionMs, 0, state.previewLoop, true};
    }
    if (const auto restored = restoredIds.find(state.programSourceId); restored != restoredIds.end()) {
        m_lastProgramSnapshots[restored->second] = {
            restored->second, state.programPositionMs, 0, state.programLoop, true};
    }

    for (size_t z = 0; z < 3; ++z) {
        auto& zone = m_tripleZones[z];
        const auto& savedZone = state.zones[z];
        uint64_t restoredZoneSourceId = 0;
        if (const auto it = restoredIds.find(savedZone.sourceId); it != restoredIds.end()) {
            restoredZoneSourceId = it->second;
            m_lastProgramSnapshots[restoredZoneSourceId] = {
                restoredZoneSourceId, savedZone.positionMs, 0, savedZone.loop, true};
        }
        zone.sourceId = restoredZoneSourceId;
    }

    if (state.layoutMode == 1) {
        setLayoutMode(LayoutMode::Triple);
    } else {
        setLayoutMode(LayoutMode::Single);
    }

    LOG_INFO("OBS workspace: restored {} source(s), {} playlist step(s); skipped {} invalid source(s).",
             restoredIds.size(), m_playlist->size(), skippedSources);
    return restoredPreviewId;
}

WorkspaceState ObsDualMediaTestWindow::captureWorkspaceState() const {
    WorkspaceState state;
    if (!m_sourceCatalog || !m_playlist) return state;
    state.sources.reserve(m_sourceCatalog->sources().size());
    for (const ObsCatalogSource& source : m_sourceCatalog->sources()) {
        if (source.systemSource || source.type == ObsCatalogSourceType::ColorBlank) continue;
        state.sources.push_back({source.id, source.type, source.path, source.endpoint, source.displayName});
    }
    state.playlistSourceIds.reserve(m_playlist->size());
    for (size_t index = 0; index < m_playlist->size(); ++index) {
        state.playlistSourceIds.push_back(m_playlist->sourceIdAt(index));
    }
    state.previewSourceId = m_previewSourceId;
    state.programSourceId = m_programSourceId;
    state.playlistLoop = m_playlist->isLooping();
    state.playlistAutoNext = m_playlist->isAutoNext();
    const ObsVideoFrameRate frameRate = m_context.videoFrameRate();
    state.fpsNumerator = frameRate.numerator;
    state.fpsDenominator = frameRate.denominator;
    state.programRenderMode = static_cast<int>(m_selectedProgramRenderMode);
    state.fadeDurationMs = m_fadeDuration ? m_fadeDuration->currentData().toInt() : 700;
    state.thumbnailWidth = m_catalogThumbnailWidth;
    state.sourceTypeFilter = m_sourceTypeFilter ? m_sourceTypeFilter->currentData().toInt() : -1;
    const PlaybackSnapshot preview = capturePreviewSnapshot();
    const PlaybackSnapshot program = captureProgramSnapshot();
    state.previewPositionMs = preview.positionMs;
    state.programPositionMs = program.positionMs;
    state.previewLoop = preview.looping;
    state.programLoop = program.looping;
    state.previewVolume = m_preview.volume;
    state.programVolume = m_program.volume;
    state.previewMuted = m_preview.audioMuted;
    state.programMuted = m_program.audioMuted;
    state.outputScreenName = m_outputScreenName;
    state.layoutMode = m_layoutMode == LayoutMode::Triple ? 1 : 0;
    state.canvasWidth = m_canvasWidth;
    state.canvasHeight = m_canvasHeight;
    state.activeZoneIndex = m_activeZoneIndex;
    state.blackoutAll = m_blackoutAll.load();
    for (size_t z = 0; z < 3; ++z) {
        const auto& zone = m_tripleZones[z];
        auto& out = state.zones[z];
        out.x = zone.x;
        out.y = zone.y;
        out.width = zone.width;
        out.height = zone.height;
        out.renderMode = static_cast<int>(zone.renderMode);
        out.sourceId = zone.sourceId;
        out.positionMs = zone.activeBackend && zone.activeBackend->isOpen() ? zone.activeBackend->positionMs() : 0;
        out.loop = zone.activeBackend && zone.activeBackend->isOpen() ? zone.activeBackend->isLooping() : zone.looping;
        out.volume = zone.volume;
        out.muted = zone.audioMuted;
        out.blackout = zone.blackout;
    }
    return state;
}

void ObsDualMediaTestWindow::applyWorkspaceSettings(const WorkspaceState& state, bool applyFrameRate) {
    if (applyFrameRate && state.fpsNumerator > 0 && state.fpsDenominator > 0) {
        const ObsVideoFrameRate requested{state.fpsNumerator, state.fpsDenominator};
        if (m_context.setVideoFrameRate(requested) && m_projectFrameRate) {
            const auto& frameRates = ObsContext::supportedVideoFrameRates();
            const auto selected = std::find(frameRates.begin(), frameRates.end(), requested);
            if (selected != frameRates.end()) {
                const QSignalBlocker blocker(m_projectFrameRate);
                m_projectFrameRate->setCurrentIndex(static_cast<int>(std::distance(frameRates.begin(), selected)));
            }
        }
    }

    if (m_programRenderMode) {
        const int index = m_programRenderMode->findData(state.programRenderMode);
        if (index >= 0) {
            const QSignalBlocker blocker(m_programRenderMode);
            m_programRenderMode->setCurrentIndex(index);
            m_selectedProgramRenderMode = static_cast<ObsRenderMode>(state.programRenderMode);
        }
    }
    if (m_program.backend) m_program.backend->setRenderMode(m_selectedProgramRenderMode);
    if (m_fadeDuration) {
        const int index = m_fadeDuration->findData(state.fadeDurationMs);
        if (index >= 0) m_fadeDuration->setCurrentIndex(index);
    }
    if (m_catalogThumbnailSize) {
        const int index = m_catalogThumbnailSize->findData(state.thumbnailWidth);
        if (index >= 0) m_catalogThumbnailSize->setCurrentIndex(index);
    }
    if (m_sourceTypeFilter) {
        const int index = m_sourceTypeFilter->findData(state.sourceTypeFilter);
        if (index >= 0) m_sourceTypeFilter->setCurrentIndex(index);
    }
    if (m_preview.volumeSlider) m_preview.volumeSlider->setValue(qRound(state.previewVolume * 100.0f));
    if (m_program.volumeSlider) m_program.volumeSlider->setValue(qRound(state.programVolume * 100.0f));
    if (m_preview.muteButton) m_preview.muteButton->setChecked(state.previewMuted);
    if (m_program.muteButton) m_program.muteButton->setChecked(state.programMuted);
    m_outputScreenName = state.outputScreenName;
    m_canvasWidth = std::clamp(state.canvasWidth, 16, 16384);
    m_canvasHeight = std::clamp(state.canvasHeight, 16, 16384);
    m_activeZoneIndex = std::clamp(state.activeZoneIndex, 0, 2);
    m_blackoutAll.store(state.blackoutAll);
    if (m_blackoutAllButton) {
        const QSignalBlocker blocker(m_blackoutAllButton);
        m_blackoutAllButton->setChecked(state.blackoutAll);
    }
    for (size_t z = 0; z < 3; ++z) {
        auto& zone = m_tripleZones[z];
        const auto& in = state.zones[z];
        zone.x = in.x;
        zone.y = in.y;
        zone.width = in.width;
        zone.height = in.height;
        zone.renderMode = static_cast<ObsRenderMode>(std::clamp(in.renderMode, 0, 2));
        zone.looping = in.loop;
        zone.volume = in.volume;
        zone.audioMuted = in.muted;
        zone.blackout = in.blackout;
        if (zone.fitCombo) {
            const int fitIdx = zone.fitCombo->findData(static_cast<int>(zone.renderMode));
            if (fitIdx >= 0) {
                const QSignalBlocker blocker(zone.fitCombo);
                zone.fitCombo->setCurrentIndex(fitIdx);
            }
        }
        if (zone.blackoutButton) {
            const QSignalBlocker blocker(zone.blackoutButton);
            zone.blackoutButton->setChecked(zone.blackout);
        }
    }
    m_testPatternDirty.store(true);
    selectActiveZone(m_activeZoneIndex);
}

bool ObsDualMediaTestWindow::replaceWorkspace(const WorkspaceState& state) {
    if (m_fadeActive) {
        QMessageBox::warning(this, localized("Đang chuyển cảnh", "Transition active"),
                             localized("Không thể mở workspace khi FADE đang chạy.",
                                       "A workspace cannot be opened while FADE is active."));
        return false;
    }

    if (m_playlistMode) stopPlaylist();
    cancelPlaylistManager();
    ObsThumbnailService::instance().cancelPendingAndWait();
    closePanels();

    m_sourcePosters.clear();
    m_sourceThumbnails.clear();
    m_sourceTimelineFrames.clear();
    m_catalogThumbnailLabels.clear();
    m_sourceThumbnailSnapshots.clear();
    m_sourceTimelineFrameSnapshots.clear();
    m_sourcesWithTimelineThumbnail.clear();
    m_lastProgramSnapshots.clear();
    m_stagedPreviewFrame = {};
    m_stagedPreviewSnapshot = {};
    m_stagedPreviewFrameSourceId = 0;
    m_previewSourceId = 0;
    m_programSourceId = 0;

    m_playlist = std::make_unique<ObsPlaylist>();
    m_sourceCatalog = std::make_unique<ObsSourceCatalog>();
    applyWorkspaceSettings(state, true);
    const uint64_t previewBlankId = m_sourceCatalog->addSystemBlank("Blank 1");
    const uint64_t programBlankId = m_sourceCatalog->addSystemBlank("Blank 2");
    if (const auto blank = m_sourceCatalog->find(previewBlankId)) stagePreviewSource(*blank);
    if (const auto blank = m_sourceCatalog->find(programBlankId)) {
        m_program.backend->setAudioOutputEnabled(true);
        if (!m_program.backend->open(*blank)) {
            LOG_ERROR("OBS workspace: failed to open the safe Program Blank while replacing workspace.");
            return false;
        }
        setProgramSourceId(programBlankId, false);
        initializeDisplay(m_program);
    }

    const uint64_t restoredPreviewId = appendWorkspaceState(state);
    if (const auto restoredPreview = m_sourceCatalog->find(restoredPreviewId)) stagePreviewSource(*restoredPreview);
    refreshCatalogUi();
    refreshPlaylistButtonUi();
    refreshPlaylistUi();
    QTimer::singleShot(0, this, [this] {
        initializeDisplay(m_preview);
        initializeDisplay(m_program);
        updateMonitorLayout();
    });
    return true;
}

bool ObsDualMediaTestWindow::saveWorkspace() {
    if (!m_workspaceStateStore) return false;
    const WorkspaceState state = captureWorkspaceState();

    std::string error;
    if (!m_workspaceStateStore->save(state, error)) {
        LOG_ERROR("OBS workspace: failed to save '{}': {}", m_workspaceStateStore->filePath().string(), error);
        return false;
    }
    LOG_INFO("OBS workspace: atomically saved {} source(s) and {} playlist step(s) to '{}'.",
             state.sources.size(), state.playlistSourceIds.size(), m_workspaceStateStore->filePath().string());
    return true;
}

bool ObsDualMediaTestWindow::saveWorkspaceTo(const std::filesystem::path& filePath) {
    WorkspaceStateStore store(filePath);
    const WorkspaceState state = captureWorkspaceState();
    std::string error;
    if (!store.save(state, error)) {
        LOG_ERROR("OBS workspace document: failed to save '{}': {}", filePath.string(), error);
        QMessageBox::critical(this, localized("Không thể lưu workspace", "Cannot save workspace"),
                              localized("Không thể ghi file workspace:\n%1", "Cannot write workspace file:\n%1")
                                  .arg(QString::fromUtf8(error.c_str())));
        return false;
    }
    LOG_INFO("OBS workspace document: saved {} source(s) and {} playlist step(s) to '{}'.",
             state.sources.size(), state.playlistSourceIds.size(), filePath.string());
    return true;
}

void ObsDualMediaTestWindow::saveWorkspaceDocument(bool saveAs) {
    QString selectedPath;
    if (!saveAs && !m_currentWorkspaceFile.empty()) {
        selectedPath = QString::fromStdWString(m_currentWorkspaceFile.wstring());
    } else {
        QSettings settings;
        const QString initialDirectory = settings.value(
            QStringLiteral("workspace/lastDirectory"), QDir::homePath()).toString();
        selectedPath = QFileDialog::getSaveFileName(
            this, localized("Lưu workspace", "Save Workspace"),
            QDir(initialDirectory).filePath(QStringLiteral("program1.ms")),
            localized("Workspace MediaSwitcher (*.ms)", "MediaSwitcher Workspace (*.ms)"));
        if (selectedPath.isEmpty()) return;
        if (QFileInfo(selectedPath).suffix().compare(QStringLiteral("ms"), Qt::CaseInsensitive) != 0) {
            selectedPath += QStringLiteral(".ms");
        }
        settings.setValue(QStringLiteral("workspace/lastDirectory"), QFileInfo(selectedPath).absolutePath());
    }

    const std::filesystem::path filePath(selectedPath.toStdWString());
    if (!saveWorkspaceTo(filePath)) return;
    m_currentWorkspaceFile = filePath;
    setWindowTitle(QStringLiteral("MediaSwitcher OBS — %1").arg(QFileInfo(selectedPath).fileName()));
    QMessageBox::information(this, localized("Đã lưu workspace", "Workspace saved"),
                             localized("Đã lưu workspace vào:\n%1", "Workspace saved to:\n%1").arg(selectedPath));
}

void ObsDualMediaTestWindow::openWorkspaceDocument() {
    QSettings settings;
    const QString initialDirectory = settings.value(
        QStringLiteral("workspace/lastDirectory"), QDir::homePath()).toString();
    const QString selectedPath = QFileDialog::getOpenFileName(
        this, localized("Mở workspace", "Open Workspace"), initialDirectory,
        localized("Workspace MediaSwitcher (*.ms)", "MediaSwitcher Workspace (*.ms)"));
    if (selectedPath.isEmpty()) return;

    WorkspaceState state;
    std::string error;
    const std::filesystem::path filePath(selectedPath.toStdWString());
    const WorkspaceLoadStatus status = WorkspaceStateStore(filePath).load(state, error);
    if (status != WorkspaceLoadStatus::Loaded) {
        QMessageBox::critical(this, localized("Workspace không hợp lệ", "Invalid workspace"),
                              localized("Không thể mở file workspace:\n%1", "Cannot open workspace file:\n%1")
                                  .arg(QString::fromUtf8(error.c_str())));
        LOG_WARN("OBS workspace document: rejected '{}': {}", filePath.string(), error);
        return;
    }

    saveWorkspace();
    if (!replaceWorkspace(state)) return;
    m_currentWorkspaceFile = filePath;
    settings.setValue(QStringLiteral("workspace/lastDirectory"), QFileInfo(selectedPath).absolutePath());
    setWindowTitle(QStringLiteral("MediaSwitcher OBS — %1").arg(QFileInfo(selectedPath).fileName()));
    saveWorkspace();
    LOG_INFO("OBS workspace document: opened '{}'.", filePath.string());
}

void ObsDualMediaTestWindow::toggleFullscreen() {
    if (isFullScreen()) showNormal();
    else showFullScreen();
}

QScreen* ObsDualMediaTestWindow::selectedOutputScreen() const {
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (!m_outputScreenName.empty()) {
        const QString selectedName = QString::fromUtf8(m_outputScreenName.c_str());
        const auto selected = std::find_if(screens.begin(), screens.end(), [&selectedName](QScreen* screen) {
            return screen && screen->name() == selectedName;
        });
        return selected != screens.end() ? *selected : nullptr;
    }
    return screens.size() >= 2 ? screens.at(1) : nullptr;
}

void ObsDualMediaTestWindow::selectOutputScreen() {
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.isEmpty()) return;

    QStringList choices;
    int selectedIndex = 0;
    const QString currentName = QString::fromUtf8(m_outputScreenName.c_str());
    for (int index = 0; index < screens.size(); ++index) {
        QScreen* screen = screens.at(index);
        const QRect geometry = screen->geometry();
        choices.push_back(QStringLiteral("%1 — %2×%3 (%4,%5)%6")
            .arg(screen->name())
            .arg(geometry.width()).arg(geometry.height())
            .arg(geometry.x()).arg(geometry.y())
            .arg(screen == QGuiApplication::primaryScreen()
                ? localized(" — Màn hình chính", " — Primary") : QString{}));
        if (screen->name() == currentName) selectedIndex = index;
    }

    bool accepted = false;
    const QString selected = QInputDialog::getItem(
        this, localized("Chọn màn hình output", "Select Output Screen"),
        localized("Màn hình dùng cho PGM toàn màn hình:", "Screen for fullscreen PGM output:"),
        choices, selectedIndex, false, &accepted);
    if (!accepted) return;
    const int index = choices.indexOf(selected);
    if (index < 0 || index >= screens.size()) return;

    const bool reopenOutput = m_programOutput && m_programOutput->isVisible();
    if (reopenOutput) m_programOutput->hide();
    m_outputScreenName = screens.at(index)->name().toUtf8().toStdString();
    saveWorkspace();
    LOG_INFO("OBS program output: selected screen '{}'.", m_outputScreenName);
    if (reopenOutput) toggleProgramOutputFullscreen();
}

void ObsDualMediaTestWindow::toggleProgramOutputFullscreen() {
    if (m_programOutput && m_programOutput->isVisible()) {
        m_programOutput->hide();
        LOG_INFO("OBS program output: fullscreen output closed.");
        return;
    }

    QScreen* outputScreen = selectedOutputScreen();
    if (!outputScreen) {
        QMessageBox::information(this, localized("TOÀN MÀN HÌNH", "FULL SCREEN"),
                                 localized("Màn hình output đã chọn không còn khả dụng. Hãy vào Đầu ra → Chọn màn hình output.",
                                           "The selected output screen is unavailable. Choose Output → Select Output Screen."));
        LOG_WARN("OBS program output: selected fullscreen screen is unavailable.");
        return;
    }

    if (m_layoutMode == LayoutMode::Triple) {
        const QRect screenGeom = outputScreen->geometry();
        if (screenGeom.width() != m_canvasWidth || screenGeom.height() != m_canvasHeight) {
            const auto reply = QMessageBox::warning(
                this,
                localized("Cảnh báo độ phân giải Output", "Output Resolution Mismatch"),
                localized("Canvas 3 vùng khai báo: %1×%2 px\nMàn hình output '%3' hiện tại: %4×%5 px\n\n"
                          "Để mapping đúng từng pixel sang bộ xử lý LED (Colorlight), kích thước màn hình nên khớp với Canvas.\n"
                          "Bạn có muốn tiếp tục mở Output (vẽ đúng toạ độ pixel gốc) không?",
                          "Configured 3-zone Canvas: %1×%2 px\nSelected output screen '%3': %4×%5 px\n\n"
                          "For pixel-perfect LED processor mapping, screen resolution should match Canvas.\n"
                          "Do you want to open Output anyway (1:1 pixel mapping)?")
                    .arg(m_canvasWidth).arg(m_canvasHeight)
                    .arg(outputScreen->name())
                    .arg(screenGeom.width()).arg(screenGeom.height()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (reply != QMessageBox::Yes) return;
        }
    }

    if (!m_programOutput) {
        m_programOutput = std::make_unique<ObsProgramOutputWindow>([this](uint32_t w, uint32_t h) {
            if (m_layoutMode == LayoutMode::Triple) {
                renderProgramCanvas(w, h, false);
            } else if (m_program.backend && m_program.backend->isOpen()) {
                m_program.backend->render(w, h);
            }
        }, nullptr, true);
    }
    m_programOutput->setWindowTitle(localized("MediaSwitcher - Output chương trình",
                                              "MediaSwitcher Program Output"));

    m_programOutput->setGeometry(outputScreen->geometry());
    m_programOutput->winId();
    if (m_programOutput->windowHandle()) m_programOutput->windowHandle()->setScreen(outputScreen);
    m_programOutput->showFullScreen();
    m_programOutput->raise();
    LOG_INFO("OBS program output: fullscreen PGM output opened on screen '{}'.", outputScreen->name().toStdString());
}

void ObsDualMediaTestWindow::closePanels() {
    if (m_programOutput) m_programOutput->hide();
    if (m_program.backend) m_program.backend->resetRenderSource();
    releaseFadeTransition();
    for (Panel* panel : {&m_preview, &m_program}) {
        destroyDisplay(*panel);
        if (panel->backend) panel->backend->close();
        if (panel->fadeOutgoing) panel->fadeOutgoing->close();
    }
    {
        std::lock_guard<std::mutex> lock(m_tripleRenderMutex);
        for (auto& zone : m_tripleZones) {
            if (zone.activeBackend) zone.activeBackend->close();
            if (zone.pendingBackend) zone.pendingBackend->close();
            zone.activeBackend.reset();
            zone.pendingBackend.reset();
        }
        if (m_testPatternTexture) {
            obs_enter_graphics();
            gs_texture_destroy(m_testPatternTexture);
            obs_leave_graphics();
            m_testPatternTexture = nullptr;
        }
    }
    m_pausedSourceRuntimes.clear();
}

QWidget* ObsDualMediaTestWindow::createTripleDeck(QWidget* parent) {
    auto* deck = new QWidget(parent);
    deck->setObjectName(QStringLiteral("tripleDeck"));
    deck->setStyleSheet(QStringLiteral(
        "#tripleDeck { background: #182026; border: 1px solid #384955; }"
        "QPushButton { min-height: 24px; padding: 1px 6px; font-size: 11px; }"
        "QComboBox { min-height: 24px; font-size: 11px; }"));
    auto* deckLayout = new QVBoxLayout(deck);
    deckLayout->setContentsMargins(8, 6, 8, 6);
    deckLayout->setSpacing(6);

    auto* topBar = new QHBoxLayout();
    topBar->setContentsMargins(0, 0, 0, 0);
    topBar->setSpacing(8);
    auto* deckTitle = new QLabel(QStringLiteral("3-ZONE COMPOSITE DECK"), deck);
    bindLocalizedProperty(deckTitle, "text", "BẢNG ĐIỀU KHIỂN 3 VÙNG ĐỘC LẬP (SEAMLESS CUT)", "3-ZONE INDEPENDENT DECK (SEAMLESS CUT)");
    deckTitle->setStyleSheet(QStringLiteral("color: #7ee3f2; font-weight: bold; font-size: 11px; border: 0;"));
    topBar->addWidget(deckTitle);
    topBar->addStretch();

    m_tripleConfigButton = new QPushButton(QStringLiteral("Canvas & Zones..."), deck);
    bindLocalizedProperty(m_tripleConfigButton, "text", "⚙ Cấu hình tọa độ 3 vùng...", "⚙ Configure 3-Zone Canvas...");
    connect(m_tripleConfigButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::showTripleLayoutSettingsDialog);
    topBar->addWidget(m_tripleConfigButton);

    m_testPatternButton = new QPushButton(QStringLiteral("Test Pattern"), deck);
    m_testPatternButton->setCheckable(true);
    bindLocalizedProperty(m_testPatternButton, "text", "🏁 Test Pattern LED", "🏁 LED Test Pattern");
    m_testPatternButton->setStyleSheet(QStringLiteral(
        "QPushButton:checked { background: #1e7055; border: 1px solid #63eab9; font-weight: bold; }"));
    connect(m_testPatternButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::toggleTestPattern);
    topBar->addWidget(m_testPatternButton);

    m_blackoutAllButton = new QPushButton(QStringLiteral("BLACKOUT ALL"), deck);
    m_blackoutAllButton->setCheckable(true);
    bindLocalizedProperty(m_blackoutAllButton, "text", "⬛ BLACKOUT TOÀN BỘ", "⬛ BLACKOUT ALL");
    m_blackoutAllButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #4a232b; border: 1px solid #8a4452; font-weight: bold; }"
        "QPushButton:checked { background: #c9263d; border: 1px solid #ff99a8; color: #ffffff; }"));
    connect(m_blackoutAllButton, &QPushButton::toggled, this, &ObsDualMediaTestWindow::toggleBlackoutAll);
    topBar->addWidget(m_blackoutAllButton);
    deckLayout->addLayout(topBar);

    auto* cardsRow = new QHBoxLayout();
    cardsRow->setContentsMargins(0, 0, 0, 0);
    cardsRow->setSpacing(8);
    const char* zoneTitleVi[3] = {"VÙNG 1 — CÁNH TRÁI (LEFT)", "VÙNG 2 — MÀN CHÍNH (MAIN)", "VÙNG 3 — CÁNH PHẢI (RIGHT)"};
    const char* zoneTitleEn[3] = {"ZONE 1 — LEFT WING", "ZONE 2 — MAIN SCREEN", "ZONE 3 — RIGHT WING"};
    for (int z = 0; z < 3; ++z) {
        auto& zone = m_tripleZones[static_cast<size_t>(z)];
        auto* card = new QWidget(deck);
        zone.cardWidget = card;
        card->setObjectName(QStringLiteral("zoneCard"));
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(6, 5, 6, 5);
        cardLayout->setSpacing(4);

        auto* headerRow = new QHBoxLayout();
        headerRow->setSpacing(4);
        zone.targetButton = new QPushButton(QString::fromUtf8(zoneTitleVi[z]), card);
        zone.targetButton->setCheckable(true);
        zone.targetButton->setChecked(z == m_activeZoneIndex);
        bindLocalizedProperty(zone.targetButton, "text", zoneTitleVi[z], zoneTitleEn[z]);
        zone.targetButton->setStyleSheet(QStringLiteral(
            "QPushButton { text-align: left; font-weight: bold; background: #222d36; border: 1px solid #425563; }"
            "QPushButton:checked { background: #1a5c73; border: 1px solid #5ad4f0; color: #ffffff; }"));
        connect(zone.targetButton, &QPushButton::clicked, this, [this, z] { selectActiveZone(z); });
        headerRow->addWidget(zone.targetButton, 1);

        zone.statusBadge = new QLabel(QStringLiteral("IDLE"), card);
        zone.statusBadge->setAlignment(Qt::AlignCenter);
        zone.statusBadge->setFixedWidth(68);
        zone.statusBadge->setStyleSheet(QStringLiteral(
            "background: #2b353d; color: #b8c4ce; font-size: 10px; font-weight: bold; border-radius: 2px; padding: 2px;"));
        headerRow->addWidget(zone.statusBadge);

        zone.takeButton = new QPushButton(QStringLiteral("TAKE PVW"), card);
        bindLocalizedProperty(zone.takeButton, "text", "ĐƯA PVW LÊN", "TAKE PVW");
        zone.takeButton->setStyleSheet(QStringLiteral(
            "QPushButton { background: #1c6b4a; border: 1px solid #52c794; font-weight: bold; }"
            "QPushButton:hover { background: #24875e; }"));
        connect(zone.takeButton, &QPushButton::clicked, this, [this, z] {
            selectActiveZone(z);
            promotePreviewToZone(z, "CUT");
        });
        headerRow->addWidget(zone.takeButton);
        cardLayout->addLayout(headerRow);

        zone.infoLabel = new QLabel(card);
        zone.infoLabel->setStyleSheet(QStringLiteral("color: #d6e4ec; font-size: 11px; border: 0;"));
        cardLayout->addWidget(zone.infoLabel);

        auto* transportRow = new QHBoxLayout();
        transportRow->setSpacing(4);
        zone.playPauseButton = new QPushButton(card);
        zone.playPauseButton->setFixedSize(28, 24);
        zone.playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        connect(zone.playPauseButton, &QPushButton::clicked, this, [this, z] { toggleZonePlayback(z); });
        transportRow->addWidget(zone.playPauseButton);

        zone.resetButton = new QPushButton(card);
        zone.resetButton->setFixedSize(28, 24);
        zone.resetButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
        connect(zone.resetButton, &QPushButton::clicked, this, [this, z] { resetZone(z); });
        transportRow->addWidget(zone.resetButton);

        zone.loopButton = new QPushButton(card);
        zone.loopButton->setFixedSize(28, 24);
        zone.loopButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        zone.loopButton->setProperty("loopActive", zone.looping);
        connect(zone.loopButton, &QPushButton::clicked, this, [this, z] { toggleZoneLoop(z); });
        transportRow->addWidget(zone.loopButton);

        zone.seekSlider = new QSlider(Qt::Horizontal, card);
        zone.seekSlider->setRange(0, 1000);
        connect(zone.seekSlider, &QSlider::sliderPressed, this, [this, z] {
            m_tripleZones[static_cast<size_t>(z)].sliderDragging = true;
        });
        connect(zone.seekSlider, &QSlider::sliderReleased, this, [this, z] {
            auto& zn = m_tripleZones[static_cast<size_t>(z)];
            zn.sliderDragging = false;
            if (zn.activeBackend && zn.activeBackend->isOpen() && zn.activeBackend->supportsTransport()) {
                const int64_t dur = zn.activeBackend->durationMs();
                if (dur > 0) zn.activeBackend->seekMs(dur * zn.seekSlider->value() / 1000);
            }
        });
        transportRow->addWidget(zone.seekSlider, 1);

        zone.timeLabel = new QLabel(QStringLiteral("00:00 / --:--"), card);
        zone.timeLabel->setFixedWidth(96);
        zone.timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        zone.timeLabel->setStyleSheet(QStringLiteral("color: #9de4f2; font-family: Consolas; font-size: 10px; font-weight: bold; border: 0;"));
        transportRow->addWidget(zone.timeLabel);

        zone.fitCombo = new QComboBox(card);
        zone.fitCombo->addItem(QStringLiteral("Fit"), static_cast<int>(ObsRenderMode::AspectFit));
        zone.fitCombo->addItem(QStringLiteral("Fill"), static_cast<int>(ObsRenderMode::AspectFill));
        zone.fitCombo->addItem(QStringLiteral("Stretch"), static_cast<int>(ObsRenderMode::FitToScreen));
        connect(zone.fitCombo, &QComboBox::currentIndexChanged, this, [this, z](int idx) { setZoneFitMode(z, idx); });
        transportRow->addWidget(zone.fitCombo);

        zone.muteButton = new QPushButton(card);
        zone.muteButton->setFixedSize(28, 24);
        zone.muteButton->setIcon(style()->standardIcon(zone.audioMuted ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume));
        connect(zone.muteButton, &QPushButton::clicked, this, [this, z] { toggleZoneAudioMaster(z); });
        transportRow->addWidget(zone.muteButton);

        zone.blackoutButton = new QPushButton(QStringLiteral("BLK"), card);
        zone.blackoutButton->setCheckable(true);
        zone.blackoutButton->setFixedWidth(38);
        zone.blackoutButton->setStyleSheet(QStringLiteral(
            "QPushButton:checked { background: #b8253a; border: 1px solid #ff8c9c; color: #fff; font-weight: bold; }"));
        connect(zone.blackoutButton, &QPushButton::toggled, this, [this, z](bool) { toggleZoneBlackout(z); });
        transportRow->addWidget(zone.blackoutButton);

        cardLayout->addLayout(transportRow);
        cardsRow->addWidget(card, 1);
    }
    deckLayout->addLayout(cardsRow);
    return deck;
}

void ObsDualMediaTestWindow::renderProgramCanvas(uint32_t width, uint32_t height, bool mapCanvasToSurface) {
    if (width == 0 || height == 0) return;
    const vec4 black{};
    gs_clear(GS_CLEAR_COLOR, &black, 0.0f, 0);
    if (m_blackoutAll.load()) return;

    std::lock_guard<std::mutex> lock(m_tripleRenderMutex);
    const int canvasW = std::max(1, m_canvasWidth);
    const int canvasH = std::max(1, m_canvasHeight);

    float scale = 1.0f;
    int baseOffsetX = 0;
    int baseOffsetY = 0;
    if (mapCanvasToSurface) {
        scale = std::min(static_cast<float>(width) / static_cast<float>(canvasW),
                         static_cast<float>(height) / static_cast<float>(canvasH));
        baseOffsetX = static_cast<int>((static_cast<float>(width) - static_cast<float>(canvasW) * scale) * 0.5f);
        baseOffsetY = static_cast<int>((static_cast<float>(height) - static_cast<float>(canvasH) * scale) * 0.5f);
    }

    if (m_testPatternActive.load()) {
        if (m_testPatternDirty.exchange(false) || !m_testPatternTexture) {
            rebuildTestPatternImage();
            if (m_testPatternTexture) {
                gs_texture_destroy(m_testPatternTexture);
                m_testPatternTexture = nullptr;
            }
            if (!m_testPatternImage.isNull()) {
                const uint8_t* bits = m_testPatternImage.constBits();
                m_testPatternTexture = gs_texture_create(
                    static_cast<uint32_t>(m_testPatternImage.width()),
                    static_cast<uint32_t>(m_testPatternImage.height()),
                    GS_BGRA, 1, &bits, 0);
            }
        }
        if (m_testPatternTexture) {
            const uint32_t dstW = mapCanvasToSurface
                ? static_cast<uint32_t>(std::max(1.0f, static_cast<float>(canvasW) * scale))
                : static_cast<uint32_t>(canvasW);
            const uint32_t dstH = mapCanvasToSurface
                ? static_cast<uint32_t>(std::max(1.0f, static_cast<float>(canvasH) * scale))
                : static_cast<uint32_t>(canvasH);
            gs_viewport_push();
            gs_projection_push();
            gs_matrix_push();
            gs_set_viewport(baseOffsetX, baseOffsetY, static_cast<int>(dstW), static_cast<int>(dstH));
            gs_ortho(0.0f, static_cast<float>(dstW), 0.0f, static_cast<float>(dstH), -100.0f, 100.0f);
            gs_matrix_identity();
            gs_effect_t* effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
            if (effect) {
                gs_eparam_t* imageParam = gs_effect_get_param_by_name(effect, "image");
                gs_effect_set_texture(imageParam, m_testPatternTexture);
                while (gs_effect_loop(effect, "Draw")) {
                    gs_draw_sprite(m_testPatternTexture, 0, dstW, dstH);
                }
            }
            gs_matrix_pop();
            gs_projection_pop();
            gs_viewport_pop();
            return;
        }
    }

    for (const auto& zone : m_tripleZones) {
        if (zone.blackout || !zone.activeBackend || !zone.activeBackend->isOpen()) continue;
        const int zx = baseOffsetX + static_cast<int>(std::round(static_cast<float>(zone.x) * scale));
        const int zy = baseOffsetY + static_cast<int>(std::round(static_cast<float>(zone.y) * scale));
        const uint32_t zw = static_cast<uint32_t>(std::max(1.0f, std::round(static_cast<float>(zone.width) * scale)));
        const uint32_t zh = static_cast<uint32_t>(std::max(1.0f, std::round(static_cast<float>(zone.height) * scale)));
        zone.activeBackend->renderRect(zx, zy, zw, zh);
    }
}

void ObsDualMediaTestWindow::setLayoutMode(LayoutMode mode) {
    if (m_fadeActive) {
        if (m_layoutModeSelector) {
            const QSignalBlocker blocker(m_layoutModeSelector);
            m_layoutModeSelector->setCurrentIndex(m_layoutMode == LayoutMode::Triple ? 1 : 0);
        }
        return;
    }
    if (m_layoutMode == mode) {
        if (m_tripleDeckWidget && (m_tripleDeckWidget->isHidden() != (mode == LayoutMode::Single))) {
            m_tripleDeckWidget->setVisible(mode == LayoutMode::Triple);
        }
        return;
    }
    if (m_playlistMode) stopPlaylist();

    m_layoutMode = mode;
    if (m_layoutModeSelector) {
        const QSignalBlocker blocker(m_layoutModeSelector);
        m_layoutModeSelector->setCurrentIndex(mode == LayoutMode::Triple ? 1 : 0);
    }
    if (m_zoneSelectorContainer) m_zoneSelectorContainer->setVisible(mode == LayoutMode::Triple);
    if (m_tripleDeckWidget) m_tripleDeckWidget->setVisible(mode == LayoutMode::Triple);
    if (m_fadeButton) m_fadeButton->setVisible(mode == LayoutMode::Single);
    if (m_fadeDuration) m_fadeDuration->setVisible(mode == LayoutMode::Single);
    if (m_openPlaylistButton) m_openPlaylistButton->setVisible(mode == LayoutMode::Single);
    if (m_playlistPreviousButton) m_playlistPreviousButton->setVisible(mode == LayoutMode::Single);
    if (m_playlistNextButton) m_playlistNextButton->setVisible(mode == LayoutMode::Single);

    if (mode == LayoutMode::Triple) {
        if (m_program.backend && m_program.backend->isOpen()) {
            m_program.backend->setAudioOutputEnabled(false);
            if (m_program.backend->supportsTransport()) m_program.backend->pause();
        }
        m_pausedSourceRuntimes.clear();

        uint64_t fallbackBlankId = 0;
        if (m_sourceCatalog) {
            for (const auto& src : m_sourceCatalog->sources()) {
                if (src.systemSource && src.type == ObsCatalogSourceType::ColorBlank) {
                    fallbackBlankId = src.id;
                    break;
                }
            }
        }

        for (size_t z = 0; z < 3; ++z) {
            auto& zone = m_tripleZones[z];
            if (!zone.activeBackend) zone.activeBackend = std::make_unique<ObsPlaybackBackend>(m_context);
            zone.activeBackend->setRenderMode(zone.renderMode);
            zone.activeBackend->setVolume((m_program.audioMuted || zone.audioMuted) ? 0.0f : m_program.volume);
            zone.activeBackend->setAudioOutputEnabled(!zone.audioMuted);
            zone.activeBackend->setLooping(zone.looping);

            const uint64_t targetId = (zone.sourceId != 0 && m_sourceCatalog && m_sourceCatalog->find(zone.sourceId))
                ? zone.sourceId : fallbackBlankId;
            if (targetId != 0 && !zone.activeBackend->isOpen()) {
                if (const auto src = m_sourceCatalog->find(targetId)) {
                    if (zone.activeBackend->open(*src, false)) {
                        zone.sourceId = targetId;
                        if (const auto snap = m_lastProgramSnapshots.find(targetId);
                            snap != m_lastProgramSnapshots.end() && snap->second.positionMs > 0 &&
                            zone.activeBackend->supportsTransport()) {
                            zone.activeBackend->seekMs(snap->second.positionMs);
                        }
                        if (zone.activeBackend->supportsTransport()) zone.activeBackend->play();
                    }
                }
            }
        }
        initializeDisplay(m_program);
        selectActiveZone(m_activeZoneIndex);
        updateTripleZonesUi();
        LOG_INFO("OBS layout: switched to 3-Zone mode (canvas={}x{}).", m_canvasWidth, m_canvasHeight);
    } else {
        m_testPatternActive.store(false);
        if (m_testPatternButton) {
            const QSignalBlocker blocker(m_testPatternButton);
            m_testPatternButton->setChecked(false);
        }
        {
            std::lock_guard<std::mutex> lock(m_tripleRenderMutex);
            for (auto& zone : m_tripleZones) {
                if (zone.activeBackend && zone.activeBackend->isOpen() && zone.sourceId != 0) {
                    PlaybackSnapshot snap;
                    snap.sourceId = zone.sourceId;
                    snap.positionMs = zone.activeBackend->positionMs();
                    snap.durationMs = zone.activeBackend->durationMs();
                    snap.looping = zone.activeBackend->isLooping();
                    snap.valid = true;
                    rememberProgramSnapshot(snap);
                }
                if (zone.activeBackend) zone.activeBackend->close();
                if (zone.pendingBackend) zone.pendingBackend->close();
                zone.activeBackend.reset();
                zone.pendingBackend.reset();
                zone.pendingSourceId = 0;
            }
        }
        if (m_program.backend) {
            if (!m_program.backend->isOpen() && m_programSourceId != 0) {
                openCatalogSource(*m_program.backend, m_programSourceId, false);
            }
            m_program.backend->setAudioOutputEnabled(true);
            m_program.backend->setVolume(m_program.audioMuted ? 0.0f : m_program.volume);
            if (m_program.backend->supportsTransport()) m_program.backend->play();
            initializeDisplay(m_program);
        }
        LOG_INFO("OBS layout: switched to Single-Zone mode.");
    }
    refreshCatalogUi();
    updateMonitorLayout();
}

void ObsDualMediaTestWindow::selectActiveZone(int zoneIndex) {
    m_activeZoneIndex = std::clamp(zoneIndex, 0, 2);
    for (int z = 0; z < 3; ++z) {
        if (m_railZoneButtons[static_cast<size_t>(z)]) {
            const QSignalBlocker blocker(m_railZoneButtons[static_cast<size_t>(z)]);
            m_railZoneButtons[static_cast<size_t>(z)]->setChecked(z == m_activeZoneIndex);
        }
        if (m_tripleZones[static_cast<size_t>(z)].targetButton) {
            const QSignalBlocker blocker(m_tripleZones[static_cast<size_t>(z)].targetButton);
            m_tripleZones[static_cast<size_t>(z)].targetButton->setChecked(z == m_activeZoneIndex);
        }
        if (m_tripleZones[static_cast<size_t>(z)].cardWidget) {
            m_tripleZones[static_cast<size_t>(z)].cardWidget->setStyleSheet(
                z == m_activeZoneIndex
                    ? QStringLiteral("#zoneCard { background: #1e2933; border: 2px solid #49c5e0; border-radius: 3px; }")
                    : QStringLiteral("#zoneCard { background: #141b21; border: 1px solid #32404b; border-radius: 3px; }"));
        }
    }
    updateTripleZonesUi();
}

bool ObsDualMediaTestWindow::promotePreviewToZone(int zoneIndex, const char* operation) {
    if (zoneIndex < 0 || zoneIndex >= 3) return false;
    const PlaybackSnapshot incoming = capturePreviewSnapshot();
    if (!incoming.valid || !m_sourceCatalog) return false;
    const auto source = m_sourceCatalog->find(incoming.sourceId);
    if (!source) return false;

    auto& zone = m_tripleZones[static_cast<size_t>(zoneIndex)];
    const bool incomingTransportStarted = m_previewTransportStarted;

    // Fast path: PVW already owns a warmed-up decoder for this exact source.
    if (m_preview.backend && m_preview.backend->isOpen() && m_previewSourceId == incoming.sourceId) {
        if (!incomingTransportStarted && m_preview.backend->supportsTransport()) {
            m_preview.backend->pause();
            if (!m_previewRuntimeMatchesStagedFrame) m_preview.backend->seekMs(incoming.positionMs);
        }
        obs_source_t* src = m_preview.backend->nativeSource();
        const uint32_t sw = src ? std::max(obs_source_get_width(src), obs_source_get_base_width(src)) : 0;
        const uint32_t sh = src ? std::max(obs_source_get_height(src), obs_source_get_base_height(src)) : 0;
        const bool frameReady = sw > 0 && sh > 0;
        if (frameReady || source->type == ObsCatalogSourceType::AudioFile || source->type == ObsCatalogSourceType::ColorBlank) {
            destroyDisplay(m_preview);
            auto incomingRuntime = std::move(m_preview.backend);
            m_preview.backend = std::make_unique<ObsPlaybackBackend>(m_context);
            m_preview.backend->setAudioOutputEnabled(false);
            m_preview.backend->setVolume(m_preview.audioMuted ? 0.0f : m_preview.volume);
            m_previewTransportStarted = false;
            m_previewRuntimeMatchesStagedFrame = false;

            incomingRuntime->setRenderMode(zone.renderMode);
            incomingRuntime->setLooping(incoming.looping);
            incomingRuntime->setVolume((m_program.audioMuted || zone.audioMuted) ? 0.0f : m_program.volume);
            incomingRuntime->setAudioOutputEnabled(!zone.audioMuted);
            if (incomingRuntime->supportsTransport()) incomingRuntime->play();

            std::unique_ptr<ObsPlaybackBackend> outgoingRuntime;
            uint64_t outgoingSourceId = 0;
            {
                std::lock_guard<std::mutex> lock(m_tripleRenderMutex);
                if (zone.pendingBackend) zone.pendingBackend->close();
                zone.pendingBackend.reset();
                zone.pendingSourceId = 0;
                outgoingRuntime = std::move(zone.activeBackend);
                outgoingSourceId = zone.sourceId;
                zone.activeBackend = std::move(incomingRuntime);
                zone.sourceId = incoming.sourceId;
                zone.looping = incoming.looping;
            }

            if (outgoingRuntime && outgoingRuntime->isOpen()) {
                if (outgoingRuntime->supportsTransport()) outgoingRuntime->pause();
                PlaybackSnapshot outSnap;
                outSnap.sourceId = outgoingSourceId;
                outSnap.positionMs = outgoingRuntime->positionMs();
                outSnap.durationMs = outgoingRuntime->durationMs();
                outSnap.looping = outgoingRuntime->isLooping();
                outSnap.valid = isCatalogSourceAvailable(outgoingSourceId);
                rememberProgramSnapshot(outSnap);
                const QImage still = captureBackendStill(*outgoingRuntime);
                if (!still.isNull()) showSourceThumbnail(outgoingSourceId, still, outSnap, true);
                outgoingRuntime->setAudioOutputEnabled(false);
                outgoingRuntime->close();
            }

            stagePreviewSnapshot(incoming);
            updateTripleZonesUi();
            refreshCatalogUi();
            LOG_INFO("OBS 3-zone: {} seamlessly swapped warmed PVW runtime into Zone #{} (source #{}, {} ms).",
                     operation, zoneIndex + 1, incoming.sourceId, incoming.positionMs);
            return true;
        }
    }

    // Double-buffered background warmup: keep current activeBackend rendering on LED without any black gap.
    auto pending = std::make_unique<ObsPlaybackBackend>(m_context);
    pending->setRenderMode(zone.renderMode);
    pending->setLooping(incoming.looping);
    pending->setVolume((m_program.audioMuted || zone.audioMuted) ? 0.0f : m_program.volume);
    pending->setAudioOutputEnabled(false);
    if (!pending->open(*source, false)) {
        LOG_ERROR("OBS 3-zone: failed to open pending source #{} for Zone #{}.", incoming.sourceId, zoneIndex + 1);
        return false;
    }
    if (pending->supportsTransport() && incoming.positionMs > 0) {
        pending->seekMs(incoming.positionMs);
    }
    if (pending->supportsTransport()) pending->play();

    {
        std::lock_guard<std::mutex> lock(m_tripleRenderMutex);
        if (zone.pendingBackend) zone.pendingBackend->close();
        zone.pendingBackend = std::move(pending);
        zone.pendingSourceId = incoming.sourceId;
        zone.pendingPositionMs = incoming.positionMs;
        zone.pendingLooping = incoming.looping;
    }
    commitPendingZoneSwaps();
    updateTripleZonesUi();
    LOG_INFO("OBS 3-zone: {} queued seamless background preload for Zone #{} (source #{}).",
             operation, zoneIndex + 1, incoming.sourceId);
    return true;
}

void ObsDualMediaTestWindow::commitPendingZoneSwaps() {
    for (int z = 0; z < 3; ++z) {
        auto& zone = m_tripleZones[static_cast<size_t>(z)];
        if (!zone.pendingBackend || !zone.pendingBackend->isOpen()) continue;

        obs_source_t* src = zone.pendingBackend->nativeSource();
        const auto catSource = m_sourceCatalog ? m_sourceCatalog->find(zone.pendingSourceId) : std::nullopt;
        const bool isAudioOrBlank = catSource &&
            (catSource->type == ObsCatalogSourceType::AudioFile || catSource->type == ObsCatalogSourceType::ColorBlank);
        const uint32_t sw = src ? std::max(obs_source_get_width(src), obs_source_get_base_width(src)) : 0;
        const uint32_t sh = src ? std::max(obs_source_get_height(src), obs_source_get_base_height(src)) : 0;
        const bool videoReady = sw > 0 && sh > 0 &&
            zone.pendingBackend->state() != ObsPlaybackState::Opening;
        if (!isAudioOrBlank && !videoReady) continue;

        std::unique_ptr<ObsPlaybackBackend> outgoingRuntime;
        uint64_t outgoingSourceId = 0;
        {
            std::lock_guard<std::mutex> lock(m_tripleRenderMutex);
            outgoingRuntime = std::move(zone.activeBackend);
            outgoingSourceId = zone.sourceId;
            zone.activeBackend = std::move(zone.pendingBackend);
            zone.sourceId = zone.pendingSourceId;
            zone.looping = zone.pendingLooping;
            zone.pendingSourceId = 0;
        }
        zone.activeBackend->setAudioOutputEnabled(!zone.audioMuted);
        zone.activeBackend->setVolume((m_program.audioMuted || zone.audioMuted) ? 0.0f : m_program.volume);
        if (zone.activeBackend->supportsTransport()) zone.activeBackend->play();

        if (outgoingRuntime && outgoingRuntime->isOpen()) {
            if (outgoingRuntime->supportsTransport()) outgoingRuntime->pause();
            PlaybackSnapshot outSnap;
            outSnap.sourceId = outgoingSourceId;
            outSnap.positionMs = outgoingRuntime->positionMs();
            outSnap.durationMs = outgoingRuntime->durationMs();
            outSnap.looping = outgoingRuntime->isLooping();
            outSnap.valid = isCatalogSourceAvailable(outgoingSourceId);
            rememberProgramSnapshot(outSnap);
            outgoingRuntime->setAudioOutputEnabled(false);
            outgoingRuntime->close();
        }
        refreshCatalogUi();
        LOG_INFO("OBS 3-zone: committed seamless swap on Zone #{} to source #{}.", z + 1, zone.sourceId);
    }
}

void ObsDualMediaTestWindow::updateTripleZonesUi() {
    if (m_layoutMode != LayoutMode::Triple) return;

    const char* zoneShortVi[3] = {"TRÁI", "CHÍNH", "PHẢI"};
    const char* zoneShortEn[3] = {"LEFT", "MAIN", "RIGHT"};
    const QString activeZoneName = localized(zoneShortVi[m_activeZoneIndex], zoneShortEn[m_activeZoneIndex]);
    if (m_program.sourceLabel) {
        m_program.sourceLabel->setText(
            localized("PGM 3 VÙNG (%1×%2) — Đích TAKE: [%3]%4",
                      "3-ZONE PGM (%1×%2) — Target: [%3]%4")
                .arg(m_canvasWidth).arg(m_canvasHeight)
                .arg(activeZoneName)
                .arg(m_blackoutAll.load() ? QStringLiteral(" [BLACKOUT ALL]") : QString{}));
    }

    for (int z = 0; z < 3; ++z) {
        auto& zone = m_tripleZones[static_cast<size_t>(z)];
        const auto src = m_sourceCatalog ? m_sourceCatalog->find(zone.sourceId) : std::nullopt;
        const QString srcName = src ? catalogSourceName(*src, m_language == UiLanguage::Vietnamese)
                                    : localized("Chưa có nguồn", "Empty");
        if (zone.infoLabel) {
            zone.infoLabel->setText(QStringLiteral("[%1,%2  %3×%4]  %5")
                .arg(zone.x).arg(zone.y).arg(zone.width).arg(zone.height).arg(srcName));
        }

        const bool hasBackend = zone.activeBackend && zone.activeBackend->isOpen();
        const ObsPlaybackState st = hasBackend ? zone.activeBackend->state() : ObsPlaybackState::None;
        const bool isPending = zone.pendingBackend && zone.pendingBackend->isOpen();

        if (zone.statusBadge) {
            if (m_blackoutAll.load() || zone.blackout) {
                zone.statusBadge->setText(QStringLiteral("BLACKOUT"));
                zone.statusBadge->setStyleSheet(QStringLiteral("background: #111; color: #ff6e80; font-size: 10px; font-weight: bold; border: 1px solid #ff6e80; border-radius: 2px;"));
            } else if (isPending) {
                zone.statusBadge->setText(localized("NẠP...", "CUEING"));
                zone.statusBadge->setStyleSheet(QStringLiteral("background: #6e551c; color: #ffe28a; font-size: 10px; font-weight: bold; border-radius: 2px;"));
            } else if (st == ObsPlaybackState::Error) {
                zone.statusBadge->setText(localized("LỖI", "ERROR"));
                zone.statusBadge->setStyleSheet(QStringLiteral("background: #7a2030; color: #ffb3be; font-size: 10px; font-weight: bold; border-radius: 2px;"));
            } else if (st == ObsPlaybackState::Playing) {
                zone.statusBadge->setText(QStringLiteral("PLAYING"));
                zone.statusBadge->setStyleSheet(QStringLiteral("background: #196b43; color: #9ef7c6; font-size: 10px; font-weight: bold; border-radius: 2px;"));
            } else if (st == ObsPlaybackState::Paused || st == ObsPlaybackState::Buffering || st == ObsPlaybackState::Opening) {
                zone.statusBadge->setText(QStringLiteral("PAUSED"));
                zone.statusBadge->setStyleSheet(QStringLiteral("background: #6e551c; color: #ffe28a; font-size: 10px; font-weight: bold; border-radius: 2px;"));
            } else if (hasBackend) {
                zone.statusBadge->setText(QStringLiteral("LIVE"));
                zone.statusBadge->setStyleSheet(QStringLiteral("background: #196b43; color: #9ef7c6; font-size: 10px; font-weight: bold; border-radius: 2px;"));
            } else {
                zone.statusBadge->setText(QStringLiteral("EMPTY"));
                zone.statusBadge->setStyleSheet(QStringLiteral("background: #2b353d; color: #9aa7b0; font-size: 10px; font-weight: bold; border-radius: 2px;"));
            }
        }

        const bool supportsTrans = hasBackend && zone.activeBackend->supportsTransport();
        const int64_t pos = supportsTrans ? zone.activeBackend->positionMs() : 0;
        const int64_t dur = supportsTrans ? zone.activeBackend->durationMs() : 0;
        if (zone.seekSlider) {
            zone.seekSlider->setEnabled(supportsTrans && dur > 0);
            if (!zone.sliderDragging && supportsTrans && dur > 0) {
                zone.seekSlider->setValue(static_cast<int>(pos * 1000 / dur));
            }
        }
        if (zone.playPauseButton) {
            zone.playPauseButton->setEnabled(supportsTrans);
            zone.playPauseButton->setIcon(style()->standardIcon(
                st == ObsPlaybackState::Playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
        }
        if (zone.resetButton) zone.resetButton->setEnabled(supportsTrans);
        if (zone.loopButton) {
            zone.loopButton->setEnabled(supportsTrans);
            const bool lp = hasBackend ? zone.activeBackend->isLooping() : zone.looping;
            zone.loopButton->setProperty("loopActive", lp);
            zone.loopButton->style()->unpolish(zone.loopButton);
            zone.loopButton->style()->polish(zone.loopButton);
        }
        if (zone.timeLabel) {
            zone.timeLabel->setText(supportsTrans ? formatTimeline(pos, dur)
                : (hasBackend && zone.activeBackend->isLiveInput() ? QStringLiteral("LIVE") : QStringLiteral("STILL")));
        }
        if (zone.muteButton) {
            zone.muteButton->setIcon(style()->standardIcon(
                zone.audioMuted ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume));
            zone.muteButton->setStyleSheet(zone.audioMuted
                ? QStringLiteral("QPushButton { background: #26313a; }")
                : QStringLiteral("QPushButton { background: #1b6b85; border: 1px solid #63d2ee; }"));
        }
    }
}

void ObsDualMediaTestWindow::showTripleLayoutSettingsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(localized("Cấu hình Canvas & 3 vùng hiển thị LED", "Configure 3-Zone LED Canvas & Regions"));
    dialog.resize(560, 380);
    auto* layout = new QVBoxLayout(&dialog);

    auto* presetRow = new QHBoxLayout();
    auto* presetLabel = new QLabel(localized("Nhập nhanh (Trái | Chính | Phải):", "Quick Preset (Left | Main | Right):"), &dialog);
    auto* leftWSpin = new QSpinBox(&dialog);
    auto* mainWSpin = new QSpinBox(&dialog);
    auto* rightWSpin = new QSpinBox(&dialog);
    auto* commonHSpin = new QSpinBox(&dialog);
    for (QSpinBox* sp : {leftWSpin, mainWSpin, rightWSpin, commonHSpin}) {
        sp->setRange(16, 16384);
    }
    leftWSpin->setValue(m_tripleZones[0].width);
    mainWSpin->setValue(m_tripleZones[1].width);
    rightWSpin->setValue(m_tripleZones[2].width);
    commonHSpin->setValue(m_canvasHeight);
    auto* applyPresetBtn = new QPushButton(localized("Tự tính X/Y & Canvas", "Auto-calc X/Y & Canvas"), &dialog);
    presetRow->addWidget(presetLabel);
    presetRow->addWidget(leftWSpin);
    presetRow->addWidget(mainWSpin);
    presetRow->addWidget(rightWSpin);
    presetRow->addWidget(new QLabel(QStringLiteral("× H:"), &dialog));
    presetRow->addWidget(commonHSpin);
    presetRow->addWidget(applyPresetBtn);
    layout->addLayout(presetRow);

    auto* canvasBox = new QGroupBox(localized("Kích thước Canvas tổng (Pixel)", "Total Canvas Resolution (Pixels)"), &dialog);
    auto* canvasRow = new QHBoxLayout(canvasBox);
    auto* canvasWSpin = new QSpinBox(canvasBox);
    auto* canvasHSpin = new QSpinBox(canvasBox);
    canvasWSpin->setRange(16, 16384);
    canvasHSpin->setRange(16, 16384);
    canvasWSpin->setValue(m_canvasWidth);
    canvasHSpin->setValue(m_canvasHeight);
    canvasRow->addWidget(new QLabel(localized("Rộng (Width):", "Width:"), canvasBox));
    canvasRow->addWidget(canvasWSpin);
    canvasRow->addWidget(new QLabel(localized("Cao (Height):", "Height:"), canvasBox));
    canvasRow->addWidget(canvasHSpin);
    layout->addWidget(canvasBox);

    auto* zonesBox = new QGroupBox(localized("Tọa độ & Kích thước từng vùng tự do (X, Y, Rộng, Cao)", "Custom Zone Rectangles (X, Y, Width, Height)"), &dialog);
    auto* zonesGrid = new QVBoxLayout(zonesBox);
    QSpinBox* xSpins[3]{};
    QSpinBox* ySpins[3]{};
    QSpinBox* wSpins[3]{};
    QSpinBox* hSpins[3]{};
    const QString labels[3] = {
        localized("Vùng 1 (Trái):", "Zone 1 (Left):"),
        localized("Vùng 2 (Chính):", "Zone 2 (Main):"),
        localized("Vùng 3 (Phải):", "Zone 3 (Right):"),
    };
    for (int z = 0; z < 3; ++z) {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel(labels[z], zonesBox);
        lbl->setFixedWidth(110);
        row->addWidget(lbl);
        xSpins[z] = new QSpinBox(zonesBox);
        ySpins[z] = new QSpinBox(zonesBox);
        wSpins[z] = new QSpinBox(zonesBox);
        hSpins[z] = new QSpinBox(zonesBox);
        xSpins[z]->setRange(0, 16384);
        ySpins[z]->setRange(0, 16384);
        wSpins[z]->setRange(16, 16384);
        hSpins[z]->setRange(16, 16384);
        xSpins[z]->setValue(m_tripleZones[static_cast<size_t>(z)].x);
        ySpins[z]->setValue(m_tripleZones[static_cast<size_t>(z)].y);
        wSpins[z]->setValue(m_tripleZones[static_cast<size_t>(z)].width);
        hSpins[z]->setValue(m_tripleZones[static_cast<size_t>(z)].height);
        row->addWidget(new QLabel(QStringLiteral("X:"), zonesBox));
        row->addWidget(xSpins[z]);
        row->addWidget(new QLabel(QStringLiteral("Y:"), zonesBox));
        row->addWidget(ySpins[z]);
        row->addWidget(new QLabel(QStringLiteral("W:"), zonesBox));
        row->addWidget(wSpins[z]);
        row->addWidget(new QLabel(QStringLiteral("H:"), zonesBox));
        row->addWidget(hSpins[z]);
        zonesGrid->addLayout(row);
    }
    layout->addWidget(zonesBox);

    connect(applyPresetBtn, &QPushButton::clicked, &dialog, [&] {
        const int lw = leftWSpin->value();
        const int mw = mainWSpin->value();
        const int rw = rightWSpin->value();
        const int ch = commonHSpin->value();
        canvasWSpin->setValue(lw + mw + rw);
        canvasHSpin->setValue(ch);
        xSpins[0]->setValue(0);        ySpins[0]->setValue(0); wSpins[0]->setValue(lw); hSpins[0]->setValue(ch);
        xSpins[1]->setValue(lw);       ySpins[1]->setValue(0); wSpins[1]->setValue(mw); hSpins[1]->setValue(ch);
        xSpins[2]->setValue(lw + mw);  ySpins[2]->setValue(0); wSpins[2]->setValue(rw); hSpins[2]->setValue(ch);
    });

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    auto* okBtn = new QPushButton(localized("Áp dụng & Lưu", "Apply & Save"), &dialog);
    auto* cancelBtn = new QPushButton(localized("Huỷ", "Cancel"), &dialog);
    connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    buttons->addWidget(okBtn);
    buttons->addWidget(cancelBtn);
    layout->addLayout(buttons);

    if (dialog.exec() != QDialog::Accepted) return;

    {
        std::lock_guard<std::mutex> lock(m_tripleRenderMutex);
        m_canvasWidth = canvasWSpin->value();
        m_canvasHeight = canvasHSpin->value();
        for (int z = 0; z < 3; ++z) {
            auto& zone = m_tripleZones[static_cast<size_t>(z)];
            zone.x = xSpins[z]->value();
            zone.y = ySpins[z]->value();
            zone.width = wSpins[z]->value();
            zone.height = hSpins[z]->value();
        }
        m_testPatternDirty.store(true);
    }
    updateTripleZonesUi();
    saveWorkspace();
    LOG_INFO("OBS 3-zone: updated canvas={}x{} and zone rectangles.", m_canvasWidth, m_canvasHeight);
}

void ObsDualMediaTestWindow::toggleTestPattern() {
    const bool enabled = m_testPatternButton ? m_testPatternButton->isChecked() : !m_testPatternActive.load();
    m_testPatternActive.store(enabled);
    if (m_testPatternButton) {
        const QSignalBlocker blocker(m_testPatternButton);
        m_testPatternButton->setChecked(enabled);
    }
    m_testPatternDirty.store(true);
    LOG_INFO("OBS 3-zone: LED Test Pattern {}.", enabled ? "ENABLED" : "DISABLED");
}

void ObsDualMediaTestWindow::rebuildTestPatternImage() {
    const int cw = std::clamp(m_canvasWidth, 64, 8192);
    const int ch = std::clamp(m_canvasHeight, 64, 8192);
    QImage img(cw, ch, QImage::Format_ARGB32);
    img.fill(QColor(12, 16, 20));

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QColor bgColors[3] = {QColor(22, 58, 92), QColor(28, 78, 54), QColor(88, 36, 62)};
    const QColor borderColors[3] = {QColor(0, 225, 255), QColor(80, 255, 140), QColor(255, 110, 185)};
    const QString names[3] = {QStringLiteral("LEFT (ZONE 1)"), QStringLiteral("MAIN (ZONE 2)"), QStringLiteral("RIGHT (ZONE 3)")};

    for (int z = 0; z < 3; ++z) {
        const auto& zone = m_tripleZones[static_cast<size_t>(z)];
        const QRect r(zone.x, zone.y, zone.width, zone.height);
        p.fillRect(r, bgColors[z]);

        p.setPen(QPen(QColor(255, 255, 255, 38), 1));
        for (int gx = r.left(); gx <= r.right(); gx += 64) p.drawLine(gx, r.top(), gx, r.bottom());
        for (int gy = r.top(); gy <= r.bottom(); gy += 64) p.drawLine(r.left(), gy, r.right(), gy);

        p.setPen(QPen(borderColors[z], 4));
        p.drawRect(r.adjusted(2, 2, -2, -2));

        const QPoint center = r.center();
        p.setPen(QPen(Qt::yellow, 2));
        p.drawLine(center.x() - 30, center.y(), center.x() + 30, center.y());
        p.drawLine(center.x(), center.y() - 30, center.x(), center.y() + 30);

        QFont font = p.font();
        font.setPixelSize(std::clamp(zone.width / 14, 18, 56));
        font.setBold(true);
        p.setFont(font);
        p.setPen(Qt::white);
        const QString label = QStringLiteral("%1\nx=%2, y=%3\n%4 × %5 px")
            .arg(names[z]).arg(zone.x).arg(zone.y).arg(zone.width).arg(zone.height);
        p.drawText(r, Qt::AlignCenter, label);
    }
    p.end();
    m_testPatternImage = std::move(img);
}

void ObsDualMediaTestWindow::toggleZonePlayback(int zoneIndex) {
    if (zoneIndex < 0 || zoneIndex >= 3) return;
    auto& zone = m_tripleZones[static_cast<size_t>(zoneIndex)];
    if (!zone.activeBackend || !zone.activeBackend->isOpen() || !zone.activeBackend->supportsTransport()) return;
    if (zone.activeBackend->state() == ObsPlaybackState::Playing) zone.activeBackend->pause();
    else zone.activeBackend->play();
    updateTripleZonesUi();
}

void ObsDualMediaTestWindow::toggleZoneLoop(int zoneIndex) {
    if (zoneIndex < 0 || zoneIndex >= 3) return;
    auto& zone = m_tripleZones[static_cast<size_t>(zoneIndex)];
    zone.looping = !zone.looping;
    if (zone.activeBackend && zone.activeBackend->isOpen()) {
        zone.activeBackend->setLooping(zone.looping);
    }
    updateTripleZonesUi();
}

void ObsDualMediaTestWindow::resetZone(int zoneIndex) {
    if (zoneIndex < 0 || zoneIndex >= 3) return;
    auto& zone = m_tripleZones[static_cast<size_t>(zoneIndex)];
    if (zone.activeBackend && zone.activeBackend->isOpen() && zone.activeBackend->supportsTransport()) {
        zone.activeBackend->seekMs(0);
    }
    updateTripleZonesUi();
}

void ObsDualMediaTestWindow::setZoneFitMode(int zoneIndex, int comboIndex) {
    if (zoneIndex < 0 || zoneIndex >= 3) return;
    auto& zone = m_tripleZones[static_cast<size_t>(zoneIndex)];
    if (!zone.fitCombo || comboIndex < 0) return;
    zone.renderMode = static_cast<ObsRenderMode>(zone.fitCombo->itemData(comboIndex).toInt());
    if (zone.activeBackend) zone.activeBackend->setRenderMode(zone.renderMode);
    if (zone.pendingBackend) zone.pendingBackend->setRenderMode(zone.renderMode);
    saveWorkspace();
}

void ObsDualMediaTestWindow::toggleZoneAudioMaster(int zoneIndex) {
    if (zoneIndex < 0 || zoneIndex >= 3) return;
    const bool currentlyMuted = m_tripleZones[static_cast<size_t>(zoneIndex)].audioMuted;
    for (int z = 0; z < 3; ++z) {
        auto& zone = m_tripleZones[static_cast<size_t>(z)];
        zone.audioMuted = (z == zoneIndex) ? !currentlyMuted : true;
        if (zone.activeBackend) {
            zone.activeBackend->setAudioOutputEnabled(!zone.audioMuted);
            zone.activeBackend->setVolume((m_program.audioMuted || zone.audioMuted) ? 0.0f : m_program.volume);
        }
    }
    updateTripleZonesUi();
    saveWorkspace();
}

void ObsDualMediaTestWindow::toggleZoneBlackout(int zoneIndex) {
    if (zoneIndex < 0 || zoneIndex >= 3) return;
    auto& zone = m_tripleZones[static_cast<size_t>(zoneIndex)];
    zone.blackout = zone.blackoutButton ? zone.blackoutButton->isChecked() : !zone.blackout;
    updateTripleZonesUi();
}

void ObsDualMediaTestWindow::toggleBlackoutAll(bool checked) {
    m_blackoutAll.store(checked);
    updateTripleZonesUi();
}
