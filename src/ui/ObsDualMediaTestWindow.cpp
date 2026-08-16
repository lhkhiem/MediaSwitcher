#include "ObsDualMediaTestWindow.h"
#include "ObsProgramOutputWindow.h"
#include "AudioMeterWidget.h"
#include "AboutDialog.h"

#include "common/config/CopyrightSettings.h"
#include "common/logger/Logger.h"
#include "engine/input/ThumbnailGenerator.h"
#include "engine/diagnostics/MediaDiagnostics.h"
#include "engine/obs/ObsContext.h"
#include "engine/obs/ObsPlaybackBackend.h"
#include "engine/obs/ObsPlaylist.h"
#include "engine/obs/ObsSourceCatalog.h"

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
#include <QEvent>
#include <QFileIconProvider>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
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
#include <cstring>
#include <functional>

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

    connect(m_quickPlayButton, &QPushButton::clicked, this, [this] { promotePreviewToProgram("Quick Play"); });
    connect(m_cutButton, &QPushButton::clicked, this, [this] { promotePreviewToProgram("CUT"); });
    connect(m_fadeButton, &QPushButton::clicked, this, [this] { fadePreviewToProgram(); });
    connect(m_fullscreenButton, &QPushButton::clicked, this, &ObsDualMediaTestWindow::toggleProgramOutputFullscreen);
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

    m_stagedSeekTimer = new QTimer(this);
    m_stagedSeekTimer->setSingleShot(true);
    m_stagedSeekTimer->setInterval(120);
    connect(m_stagedSeekTimer, &QTimer::timeout, this, &ObsDualMediaTestWindow::requestStagedPreviewFrame);

    m_preview.backend = std::make_unique<ObsPlaybackBackend>(context);
    m_program.backend = std::make_unique<ObsPlaybackBackend>(context);
    m_program.backend->setRenderMode(m_selectedProgramRenderMode);
    m_playlist = std::make_unique<ObsPlaylist>();
    m_sourceCatalog = std::make_unique<ObsSourceCatalog>();
    const uint64_t previewBlankId = m_sourceCatalog->addSystemBlank("Blank 1");
    const uint64_t programBlankId = m_sourceCatalog->addSystemBlank("Blank 2");
    refreshCatalogUi();
    refreshPlaylistButtonUi();
    connect(&ThumbnailGenerator::instance(), &ThumbnailGenerator::thumbnailReady, this, [this](int sourceId, const QImage& image) {
        if (!m_sourceCatalog->find(static_cast<uint64_t>(sourceId))) return;
        const uint64_t id = static_cast<uint64_t>(sourceId);
        m_sourceThumbnails[id] = QPixmap::fromImage(image);
        if (const auto label = m_catalogThumbnailLabels.find(id); label != m_catalogThumbnailLabels.end() && label->second) {
            label->second->setPixmap(fitCatalogThumbnail(m_sourceThumbnails[id], m_catalogThumbnailWidth));
        }
    });
    connect(&ThumbnailGenerator::instance(), &ThumbnailGenerator::previewFrameReady, this,
            [this](quint64 sourceId, int64_t positionMs, int64_t durationMs, const QImage& image) {
        if (sourceId == m_stagedPreviewSourceId) showStagedPreviewFrame(positionMs, durationMs, image);
    });
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

    if (!mediaPath.empty()) {
        if (!openPanel(m_preview, mediaPath, false) || !openPanel(m_program, mediaPath, true)) {
            closePanels();
            return;
        }
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this] {
        updatePanel(m_preview, QStringLiteral("PVW"));
        updatePanel(m_program, QStringLiteral("PGM"));
        rememberProgramSnapshot(captureProgramSnapshot());
        finishFadeIfComplete();
        if (m_playlistMode && !m_fadeActive && !m_playlist->empty() && m_playlist->isAutoNext() && m_program.backend &&
            m_program.backend->hasEnded()) {
            navigatePlaylist(true, "Auto Next");
        }
    });
    m_timer->start(200);
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

ObsDualMediaTestWindow::~ObsDualMediaTestWindow() { closePanels(); }

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
    updateInputToolbarPresentation();

    if (m_sourceCatalog) refreshCatalogUi();
    refreshPlaylistButtonUi();
    if (m_playlistDialog) refreshPlaylistUi();
    if (m_programOutput) {
        m_programOutput->setWindowTitle(localized("MediaSwitcher - Output chương trình",
                                                  "MediaSwitcher Program Output"));
    }
    updatePanel(m_preview, QStringLiteral("PVW"));
    updatePanel(m_program, QStringLiteral("PGM"));
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
    resizeDisplay(m_preview);
    resizeDisplay(m_program);
}

void ObsDualMediaTestWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange || m_closing) return;
    QTimer::singleShot(0, this, [this] {
        updateMonitorLayout();
        resizeDisplay(m_preview);
        resizeDisplay(m_program);
    });
}

void ObsDualMediaTestWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_F11: toggleFullscreen(); break;
    case Qt::Key_Space: togglePanelPlayback(m_preview); break;
    case Qt::Key_Return: togglePanelPlayback(m_program); break;
    case Qt::Key_C: promotePreviewToProgram("CUT"); break;
    case Qt::Key_F: fadePreviewToProgram(); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void ObsDualMediaTestWindow::draw(void* parameter, uint32_t width, uint32_t height) {
    auto* panel = static_cast<Panel*>(parameter);
    if (!panel || !panel->backend) return;
    panel->backend->render(width, height);
    captureProgramThumbnail(*panel);
}

void ObsDualMediaTestWindow::captureProgramThumbnail(Panel& panel) {
    constexpr uint32_t thumbnailWidth = 320;
    constexpr uint32_t thumbnailHeight = 180;
    constexpr uint32_t captureEveryFrames = 6;

    if (!panel.owner || !panel.thumbnailCaptureEnabled.load() || !panel.backend || !panel.backend->isOpen()) return;
    if (++panel.thumbnailFrameCounter % captureEveryFrames != 0) return;

    if (!panel.thumbnailTexrender) {
        panel.thumbnailTexrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
        panel.thumbnailStages[0] = gs_stagesurface_create(thumbnailWidth, thumbnailHeight, GS_BGRA);
        panel.thumbnailStages[1] = gs_stagesurface_create(thumbnailWidth, thumbnailHeight, GS_BGRA);
        panel.thumbnailStages[2] = gs_stagesurface_create(thumbnailWidth, thumbnailHeight, GS_BGRA);
        if (!panel.thumbnailTexrender || !panel.thumbnailStages[0] || !panel.thumbnailStages[1] || !panel.thumbnailStages[2]) {
            LOG_ERROR("OBS thumbnail: Failed to allocate the Program frame capture textures.");
            return;
        }
        LOG_INFO("OBS thumbnail: Program GPU frame tap initialized at {}x{}.", thumbnailWidth, thumbnailHeight);
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
    gs_stage_texture(panel.thumbnailStages[panel.thumbnailWriteStage], texture);

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
        if (!panel.thumbnailFrameDelivered.exchange(true)) {
            LOG_INFO("OBS thumbnail: Program GPU frame tap delivered its first frame for source #{}.", sourceId);
        }
        QMetaObject::invokeMethod(panel.owner, [owner = panel.owner, sourceId, frame = std::move(frame)]() mutable {
            if (!owner->m_closing) owner->showProgramThumbnail(sourceId, frame);
        }, Qt::QueuedConnection);
    }
    panel.thumbnailWriteStage = (panel.thumbnailWriteStage + 1U) % 3U;
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
    panel.videoStack->addWidget(panel.stagedFrameLabel);
    panel.videoStack->addWidget(panel.videoSurface);
    panel.videoContainer->setMinimumSize(0, 0);
    panel.videoContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    panel.videoSurface->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    panel.stagedFrameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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
    panel.audioMeter = new AudioMeterWidget(group, false);
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
    connect(panel.volumeSlider, &QSlider::valueChanged, this, [&panel](int value) {
        panel.volume = static_cast<float>(value) / 100.0f;
        panel.volumeValueLabel->setText(QStringLiteral("%1%").arg(value));
        if (panel.backend) panel.backend->setVolume(panel.audioMuted ? 0.0f : panel.volume);
    });
    connect(panel.muteButton, &QPushButton::toggled, this, [this, &panel](bool muted) {
        panel.audioMuted = muted;
        panel.muteButton->setIcon(style()->standardIcon(muted ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume));
        panel.muteButton->setToolTip(muted ? localized("Bật tiếng", "Unmute") : localized("Tắt tiếng", "Mute"));
        if (panel.backend) panel.backend->setVolume(muted ? 0.0f : panel.volume);
    });
    connect(panel.seekSlider, &QSlider::sliderPressed, this, [&panel] { panel.sliderDragging = true; });
    connect(panel.seekSlider, &QSlider::sliderReleased, this, [this, &panel] {
        panel.sliderDragging = false;
        if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen())) {
            if (m_stagedPreviewDurationMs > 0) {
                m_stagedPreviewPositionMs = m_stagedPreviewDurationMs * panel.seekSlider->value() / 1000;
                requestStagedPreviewFrame();
            }
            return;
        }
        if (!panel.backend) return;
        const int64_t duration = panel.backend->durationMs();
        if (duration > 0) panel.backend->seekMs(duration * panel.seekSlider->value() / 1000);
    });
    connect(panel.seekSlider, &QSlider::sliderMoved, this, [this, &panel](int value) {
        if (&panel != &m_preview || (panel.backend && panel.backend->isOpen()) || m_stagedPreviewDurationMs <= 0) return;
        m_stagedPreviewPositionMs = m_stagedPreviewDurationMs * value / 1000;
        m_stagedSeekTimer->start();
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
    if (panel.display || !panel.videoSurface || !panel.backend || !panel.backend->isOpen()) return;
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
    if (panel.videoStack) panel.videoStack->setCurrentWidget(panel.videoSurface);
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
        panel.thumbnailFrameDelivered.store(false);
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

    const int videoHeight = std::max(1, panelWidth * 9 / 16);
    const int switcherHeight = videoHeight + panelChromeHeight;
    if (m_preview.videoContainer->height() != videoHeight) m_preview.videoContainer->setFixedHeight(videoHeight);
    if (m_program.videoContainer->height() != videoHeight) m_program.videoContainer->setFixedHeight(videoHeight);
    if (m_switcherArea->height() != switcherHeight) m_switcherArea->setFixedHeight(switcherHeight);
}

void ObsDualMediaTestWindow::updateInputToolbarPresentation() {
    if (!m_inputToolbarWidget || !m_addSourceButton || !m_openPlaylistButton ||
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
        setWidth(m_openPlaylistButton, 96);
        setWidth(m_sourceTypeFilter, 102);
        setWidth(m_catalogThumbnailSize, 96);
        setWidth(m_projectFrameRate, 88);
        setWidth(m_programRenderMode, 102);
    } else {
        m_inputToolbarWidget->setStyleSheet(QString());
        m_addSourceButton->setText(localized("Thêm nguồn", "Add Input"));
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
    if (!panel.backend || !panel.backend->isOpen()) {
        if (&panel == &m_preview && !m_stagedPreviewPath.empty()) {
            const auto source = m_sourceCatalog->find(m_previewSourceId);
            const bool supportsTransport = source && obsCatalogSourceHasTimeline(source->type);
            const bool supportsAudio = source && source->type != ObsCatalogSourceType::ImageFile &&
                source->type != ObsCatalogSourceType::ColorBlank;
            panel.playPauseButton->setEnabled(supportsTransport);
            panel.loopButton->setEnabled(supportsTransport);
            panel.resetButton->setEnabled(supportsTransport);
            panel.seekBackButton->setEnabled(supportsTransport);
            panel.seekForwardButton->setEnabled(supportsTransport);
            panel.volumeSlider->setEnabled(supportsAudio);
            panel.muteButton->setEnabled(supportsAudio);
            panel.audioMeter->reset();
            panel.seekSlider->setEnabled(supportsTransport && m_stagedPreviewDurationMs > 0);
            panel.playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            panel.playPauseButton->setToolTip(supportsTransport
                ? localized("Phát PVW", "Play PVW") : localized("Ảnh tĩnh", "Still image"));
            panel.timeLabel->setText(supportsTransport
                ? formatTimeline(m_stagedPreviewPositionMs, m_stagedPreviewDurationMs)
                : QStringLiteral("STILL"));
            const QString name = source ? catalogSourceName(*source, m_language == UiLanguage::Vietnamese)
                                        : QString::fromStdWString(m_stagedPreviewPath.filename().wstring());
            panel.sourceLabel->setText(panel.sourceLabel->fontMetrics().elidedText(
                QStringLiteral("PVW  %1").arg(name), Qt::ElideRight, panel.sourceLabel->width()));
        }
        return;
    }
    panel.backend->enforcePendingPause();
    const bool supportsTransport = panel.backend->supportsTransport();
    const bool supportsAudio = panel.backend->supportsAudio();
    const int64_t position = panel.backend->positionMs();
    const int64_t duration = panel.backend->durationMs();
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
    const bool isPlaying = panel.backend->state() == ObsPlaybackState::Playing;
    panel.playPauseButton->setIcon(style()->standardIcon(isPlaying ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    panel.playPauseButton->setToolTip(isPlaying ? localized("Tạm dừng", "Pause") : localized("Phát", "Play"));
    panel.loopButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    panel.loopButton->setProperty("loopActive", panel.backend->isLooping());
    panel.loopButton->style()->unpolish(panel.loopButton);
    panel.loopButton->style()->polish(panel.loopButton);
    panel.loopButton->setToolTip(panel.backend->isLooping()
        ? localized("Tắt lặp lại", "Disable loop") : localized("Bật lặp lại", "Enable loop"));
    panel.timeLabel->setText(panel.backend->isLiveInput() ? QStringLiteral("LIVE")
        : (supportsTransport ? formatTimeline(position, duration) : QStringLiteral("STILL")));
    const uint64_t sourceId = &panel == &m_preview ? m_previewSourceId : m_programSourceId;
    const auto catalogSource = m_sourceCatalog ? m_sourceCatalog->find(sourceId) : std::nullopt;
    const QString sourceName = catalogSource ? catalogSourceName(*catalogSource, m_language == UiLanguage::Vietnamese)
        : QFileInfo(QString::fromStdWString(panel.backend->mediaPath().filename().wstring())).fileName();
    const QString sourceText = QStringLiteral("%1  %2").arg(role, sourceName);
    panel.sourceLabel->setText(panel.sourceLabel->fontMetrics().elidedText(
        sourceText, Qt::ElideRight, panel.sourceLabel->width()));
}

void ObsDualMediaTestWindow::updateAudioMeters() {
    const auto updateMeter = [](Panel& panel) {
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
    const int activeDecoders =
        (m_preview.backend && m_preview.backend->isOpen() ? 1 : 0) +
        (m_program.backend && m_program.backend->isOpen() ? 1 : 0) +
        (m_program.fadeOutgoing && m_program.fadeOutgoing->isOpen() ? 1 : 0);
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
    if (source.type == ObsCatalogSourceType::VideoFile || source.type == ObsCatalogSourceType::AudioFile ||
        source.type == ObsCatalogSourceType::ImageFile) {
        const PlaybackSnapshot snapshot = previewSnapshotForSource(source);
        stagePreviewAtPosition(source.path, source.id, snapshot.positionMs);
        m_stagedPreviewDurationMs = snapshot.durationMs;
        m_stagedPreviewLoop = snapshot.looping;
        return;
    }

    clearStagedPreviewMetadata();
    destroyDisplay(m_preview);
    m_preview.backend->close();
    m_preview.backend->setAudioOutputEnabled(false);
    m_preview.backend->setLooping(false);
    // Open video inputs before they are taken live.  startPaused records the
    // intent through ffmpeg_source startup, so PVW is seekable but silent.
    if (!m_preview.backend->open(source, true)) {
        m_preview.statusLabel->setText(localized("Không thể mở nguồn đã chọn trên PVW.",
                                                "Could not open the selected source on PVW."));
        return;
    }
    m_previewSourceId = source.id;
    initializeDisplay(m_preview);
    resizeDisplay(m_preview);
    refreshCatalogUi();
    LOG_INFO("OBS app: {} source #{} preloaded in PVW. transport={} systemSource={}",
             obsCatalogSourceTypeName(source.type), source.id, m_preview.backend->supportsTransport(), source.systemSource);
}

void ObsDualMediaTestWindow::stagePreviewAtPosition(const std::filesystem::path& path, uint64_t sourceId, int64_t positionMs) {
    if (m_stagedSeekTimer) m_stagedSeekTimer->stop();
    destroyDisplay(m_preview);
    m_preview.backend->close();
    m_preview.backend->setAudioOutputEnabled(false);
    m_stagedPreviewPath = path;
    m_stagedPreviewSourceId = sourceId;
    m_previewSourceId = sourceId;
    m_stagedPreviewPositionMs = std::max<int64_t>(0, positionMs);
    m_stagedPreviewDurationMs = 0;
    m_stagedPreviewLoop = false;
    m_preview.seekSlider->setValue(0);
    m_preview.seekSlider->setEnabled(false);
    m_preview.playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_preview.playPauseButton->setToolTip(localized("Phát PVW", "Play PVW"));
    m_preview.stagedFrameLabel->setPixmap({});
    m_preview.stagedFrameLabel->setText(localized("Đang tải khung hình xem trước...", "Loading preview frame..."));
    if (m_preview.videoStack) m_preview.videoStack->setCurrentWidget(m_preview.stagedFrameLabel);
    requestStagedPreviewFrame();
    refreshCatalogUi();
    LOG_INFO("OBS app: Source #{} staged in PVW at {} ms without creating an active OBS media source.", sourceId,
             m_stagedPreviewPositionMs);
}

void ObsDualMediaTestWindow::requestStagedPreviewFrame() {
    if (m_stagedPreviewPath.empty()) return;
    const QString suffix = QString::fromStdWString(m_stagedPreviewPath.extension().wstring()).toLower();
    const SourceType type = (suffix == QStringLiteral(".jpg") || suffix == QStringLiteral(".jpeg") ||
                             suffix == QStringLiteral(".png") || suffix == QStringLiteral(".bmp") ||
                             suffix == QStringLiteral(".webp"))
        ? SourceType::ImageFile : SourceType::VideoFile;
    ThumbnailGenerator::instance().requestPreviewFrame(m_stagedPreviewSourceId,
        QString::fromStdWString(m_stagedPreviewPath.wstring()).toUtf8().toStdString(), type, m_stagedPreviewPositionMs);
}

void ObsDualMediaTestWindow::showStagedPreviewFrame(int64_t positionMs, int64_t durationMs, const QImage& frame) {
    if (m_stagedPreviewPath.empty() || !m_preview.stagedFrameLabel || positionMs != m_stagedPreviewPositionMs) return;
    if (durationMs > 0) m_stagedPreviewDurationMs = durationMs;
    m_preview.seekSlider->setEnabled(m_stagedPreviewDurationMs > 0);
    if (m_stagedPreviewDurationMs > 0) {
        m_preview.seekSlider->setValue(static_cast<int>(m_stagedPreviewPositionMs * 1000 / m_stagedPreviewDurationMs));
    }
    m_preview.stagedFrameLabel->setPixmap(QPixmap::fromImage(frame).scaled(m_preview.stagedFrameLabel->size(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_preview.stagedFrameLabel->setText({});
    if (m_preview.videoStack) m_preview.videoStack->setCurrentWidget(m_preview.stagedFrameLabel);
    LOG_INFO("OBS app: Decoded staged PVW frame for source #{} at {} ms.", m_stagedPreviewSourceId, positionMs);
}

void ObsDualMediaTestWindow::showProgramThumbnail(uint64_t sourceId, const QImage& frame) {
    if (sourceId == 0 || sourceId != m_programSourceId || !m_sourceCatalog->find(sourceId)) return;
    m_sourceThumbnails[sourceId] = QPixmap::fromImage(frame);
    if (const auto label = m_catalogThumbnailLabels.find(sourceId);
        label != m_catalogThumbnailLabels.end() && label->second) {
        label->second->setPixmap(fitCatalogThumbnail(m_sourceThumbnails[sourceId], m_catalogThumbnailWidth));
    }
}

bool ObsDualMediaTestWindow::openStagedPreview(bool play) {
    if (m_stagedPreviewPath.empty() || !m_preview.backend) return false;
    m_preview.backend->close();
    m_preview.backend->setAudioOutputEnabled(false);
    m_preview.backend->setLooping(m_stagedPreviewLoop);
    if (!m_preview.backend->open(m_stagedPreviewPath)) return false;
    m_preview.backend->seekMs(m_stagedPreviewPositionMs);
    initializeDisplay(m_preview);
    resizeDisplay(m_preview);
    if (play) m_preview.backend->play();
    return true;
}

ObsDualMediaTestWindow::PlaybackSnapshot ObsDualMediaTestWindow::capturePreviewSnapshot() const {
    PlaybackSnapshot snapshot;
    snapshot.sourceId = m_previewSourceId;
    if (!snapshot.sourceId) return snapshot;

    if (m_preview.backend && m_preview.backend->isOpen()) {
        snapshot.positionMs = m_preview.backend->positionMs();
        snapshot.durationMs = m_preview.backend->durationMs();
        snapshot.looping = m_preview.backend->isLooping();
    } else {
        snapshot.positionMs = m_stagedPreviewPositionMs;
        snapshot.durationMs = m_stagedPreviewDurationMs;
        snapshot.looping = m_stagedPreviewLoop;
    }
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

void ObsDualMediaTestWindow::stagePreviewSnapshot(const PlaybackSnapshot& snapshot) {
    if (!snapshot.valid) {
        clearPreviewSource();
        return;
    }
    const auto source = m_sourceCatalog->find(snapshot.sourceId);
    if (!source) {
        clearPreviewSource();
        return;
    }

    if (source->type == ObsCatalogSourceType::VideoFile || source->type == ObsCatalogSourceType::AudioFile ||
        source->type == ObsCatalogSourceType::ImageFile) {
        stagePreviewAtPosition(source->path, source->id, snapshot.positionMs);
        m_stagedPreviewDurationMs = snapshot.durationMs;
        m_stagedPreviewLoop = snapshot.looping;
        return;
    }

    // RTSP and Blank sources have no seekable file cue.  They still use the
    // same catalog identity, but are opened as a normal muted PVW source.
    stagePreviewSource(*source);
}

void ObsDualMediaTestWindow::clearStagedPreviewMetadata() {
    if (m_stagedSeekTimer) m_stagedSeekTimer->stop();
    m_stagedPreviewPath.clear();
    m_stagedPreviewSourceId = 0;
    m_stagedPreviewPositionMs = 0;
    m_stagedPreviewDurationMs = 0;
    m_stagedPreviewLoop = false;
}

bool ObsDualMediaTestWindow::openCatalogSource(ObsPlaybackBackend& backend, uint64_t sourceId, bool startPaused) {
    const auto source = m_sourceCatalog->find(sourceId);
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
    clearStagedPreviewMetadata();
    destroyDisplay(m_preview);
    if (m_preview.backend) {
        m_preview.backend->close();
        m_preview.backend->setAudioOutputEnabled(false);
    }
    m_previewSourceId = 0;
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
    if (panel.backend->state() == ObsPlaybackState::Playing) panel.backend->pause();
    else panel.backend->play();
}

void ObsDualMediaTestWindow::togglePanelLoop(Panel& panel) {
    if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen())) {
        m_stagedPreviewLoop = !m_stagedPreviewLoop;
        return;
    }
    if (panel.backend && panel.backend->supportsTransport()) panel.backend->setLooping(!panel.backend->isLooping());
}

void ObsDualMediaTestWindow::seekPanel(Panel& panel, int64_t deltaMs) {
    if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen()) && m_stagedPreviewDurationMs > 0) {
        m_stagedPreviewPositionMs = std::clamp(m_stagedPreviewPositionMs + deltaMs, int64_t{0}, m_stagedPreviewDurationMs);
        requestStagedPreviewFrame();
        return;
    }
    if (panel.backend && panel.backend->supportsTransport()) panel.backend->seekMs(panel.backend->positionMs() + deltaMs);
}

void ObsDualMediaTestWindow::resetPanel(Panel& panel) {
    if (&panel == &m_preview && (!panel.backend || !panel.backend->isOpen())) {
        m_stagedPreviewPositionMs = 0;
        requestStagedPreviewFrame();
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

    const PlaybackSnapshot incoming = capturePreviewSnapshot();
    const PlaybackSnapshot outgoing = captureProgramSnapshot();
    if (!incoming.valid) {
        LOG_ERROR("OBS dual media: {} rejected because the PVW cue is unavailable.", operation);
        return false;
    }

    if (m_playlistMode) stopPlaylist();

    const bool shouldSwap = std::string_view(operation) == "CUT";
    rememberProgramSnapshot(outgoing);

    // There is deliberately one handoff path for static and playing PVW.  A
    // previous optimization swapped runtime objects only when PVW happened to
    // own an OBS decoder; that made CUT preserve a different playhead than a
    // staged cue and is the source of the reported reset-to-zero regressions.
    if (m_preview.backend->isOpen() && m_preview.backend->supportsTransport()) m_preview.backend->pause();
    m_program.backend->close();
    m_program.backend->setAudioOutputEnabled(true);
    m_program.backend->setLooping(incoming.looping);
    if (!openCatalogSource(*m_program.backend, incoming.sourceId)) {
        LOG_ERROR("OBS dual media: {} failed to create Program runtime from Preview asset.", operation);
        m_program.statusLabel->setText(localized("Không thể chuyển nguồn lên Program.", "Program promotion failed."));
        return false;
    }

    if (m_program.backend->supportsTransport()) m_program.backend->seekMs(incoming.positionMs);
    m_program.backend->play();
    initializeDisplay(m_program);
    resizeDisplay(m_program);
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

    const PlaybackSnapshot incomingSnapshot = capturePreviewSnapshot();
    const PlaybackSnapshot outgoingSnapshot = captureProgramSnapshot();
    if (!incomingSnapshot.valid) {
        LOG_ERROR("OBS dual media: FADE rejected because the PVW cue is unavailable.");
        return false;
    }
    if (m_playlistMode) stopPlaylist();
    const uint32_t duration = static_cast<uint32_t>(m_fadeDuration->currentData().toUInt());
    rememberProgramSnapshot(outgoingSnapshot);

    auto incoming = std::make_unique<ObsPlaybackBackend>(m_context);
    incoming->setRenderMode(m_selectedProgramRenderMode);
    incoming->setVolume(m_program.audioMuted ? 0.0f : m_program.volume);
    // Prepare incoming media silently, then transfer the single WASAPI monitor atomically at FADE start.
    incoming->setAudioOutputEnabled(false);
    incoming->setLooping(incomingSnapshot.looping);
    if (!openCatalogSource(*incoming, incomingSnapshot.sourceId)) {
        LOG_ERROR("OBS dual media: FADE failed to create incoming Program runtime.");
        return false;
    }
    if (incoming->supportsTransport()) incoming->seekMs(incomingSnapshot.positionMs);

    obs_source_t* transition = obs_source_create("fade_transition", "MediaSwitcher Fade", nullptr, nullptr);
    if (!transition) {
        LOG_ERROR("OBS dual media: FADE could not create OBS fade_transition source.");
        incoming->close();
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
        incoming->close();
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
            m_program.fadeOutgoing->close();
            m_program.fadeOutgoing.reset();
            if (m_fadeOutgoingSnapshot.valid) stagePreviewSnapshot(m_fadeOutgoingSnapshot);
            m_fadeOutgoingSnapshot = {};
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

#if 0 // Replaced by source-catalog playlist mode. Kept temporarily only to preserve Phase 8 history.
void ObsDualMediaTestWindow::choosePlaylist() {
    QFileDialog dialog(this, QStringLiteral("Chọn nhiều media cho OBS playlist"));
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilter(QStringLiteral("Media files (*.mp4 *.mkv *.mov *.avi *.m4v *.webm *.mp3 *.wav *.flac);;All files (*.*)"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    for (QListView* view : dialog.findChildren<QListView*>()) view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (QTreeView* view : dialog.findChildren<QTreeView*>()) view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    if (dialog.exec() != QDialog::Accepted) return;
    const QStringList selected = dialog.selectedFiles();
    if (selected.isEmpty()) return;

    std::vector<std::filesystem::path> items;
    items.reserve(static_cast<size_t>(selected.size()));
    for (const QString& path : selected) items.emplace_back(path.toStdWString());
    const bool wasEmpty = m_playlist->empty();
    m_playlist->appendItems(std::move(items));
    m_playlist->setLoop(m_playlistLoop->isChecked());
    m_playlist->setAutoNext(m_autoNext->isChecked());
    LOG_INFO("OBS playlist: now contains {} item(s) after file picker selection.", m_playlist->size());
    if (wasEmpty) activatePlaylistItem("Add Playlist Files");
    else {
        preparePlaylistLookahead();
        updatePlaylistStatus();
    }
}

bool ObsDualMediaTestWindow::activatePlaylistItem(const char* reason) {
    if (m_playlist->empty() || m_fadeActive) return false;

    const std::filesystem::path item = m_playlist->current();
    releasePreload();
    if (!m_program.backend) m_program.backend = std::make_unique<ObsPlaybackBackend>(m_context);
    m_program.backend->resetRenderSource();
    m_program.backend->close();
    m_program.backend->setAudioOutputEnabled(true);
    m_program.backend->setLooping(false);
    if (!m_program.backend->open(item)) {
        LOG_ERROR("OBS playlist: {} failed to open Program item '{}'.", reason, item.string());
        updatePlaylistStatus();
        return false;
    }
    m_program.backend->play();
    preparePlaylistLookahead();
    LOG_INFO("OBS playlist: {} activated item {}/{} '{}'.", reason, m_playlist->currentIndex() + 1,
             m_playlist->size(), item.string());
    updatePlaylistStatus();
    return true;
}

bool ObsDualMediaTestWindow::navigatePlaylist(bool forward, const char* reason) {
    if (m_fadeActive || m_playlist->empty()) return false;
    const bool changed = forward ? m_playlist->advance() : m_playlist->previous();
    if (!changed) {
        LOG_INFO("OBS playlist: {} ignored at playlist boundary with Loop disabled.", reason);
        updatePlaylistStatus();
        return false;
    }
    return activatePlaylistItem(reason);
}

void ObsDualMediaTestWindow::preparePlaylistLookahead() {
    releasePreload();
    if (m_playlist->empty() || m_fadeActive) {
        updatePlaylistStatus();
        return;
    }

    const std::filesystem::path* previewItem = m_playlist->offset(1);
    if (previewItem && m_playlist->size() > 1) {
        if (!m_preview.backend) m_preview.backend = std::make_unique<ObsPlaybackBackend>(m_context);
        m_preview.backend->resetRenderSource();
        m_preview.backend->close();
        m_preview.backend->setAudioOutputEnabled(false);
        m_preview.backend->setLooping(false);
        if (m_preview.backend->open(*previewItem, true)) m_preview.backend->pause();
        else LOG_ERROR("OBS playlist: failed to prepare Preview item '{}'.", previewItem->string());
    } else if (m_preview.backend) {
        m_preview.backend->resetRenderSource();
        m_preview.backend->close();
    }

    const std::filesystem::path* preloadItem = m_playlist->offset(2);
    if (preloadItem && m_playlist->size() > 2) {
        m_preload = std::make_unique<ObsPlaybackBackend>(m_context);
        m_preload->setAudioOutputEnabled(false);
        m_preload->setLooping(false);
        if (m_preload->open(*preloadItem, true)) {
            m_preload->pause();
            LOG_INFO("OBS playlist: preloaded item '{}'. Active playback count=3 (PGM, PVW, preload).",
                     preloadItem->string());
        } else {
            LOG_ERROR("OBS playlist: failed to preload '{}'.", preloadItem->string());
            m_preload.reset();
        }
    }
    updatePlaylistStatus();
}

void ObsDualMediaTestWindow::releasePreload() {
    if (!m_preload) return;
    m_preload->close();
    m_preload.reset();
    LOG_INFO("OBS playlist: released preload to preserve MAX_TOTAL_ACTIVE_PLAYBACKS=3.");
}

void ObsDualMediaTestWindow::updatePlaylistStatus() {
    if (!m_playlistStatus || !m_playlist || m_playlist->empty()) {
        if (m_playlistStatus) m_playlistStatus->setText(QStringLiteral("Playlist: Off"));
        return;
    }
    m_playlistStatus->setText(QStringLiteral("Playlist %1/%2\nPreload: %3")
        .arg(m_playlist->currentIndex() + 1)
        .arg(m_playlist->size())
        .arg(m_preload ? QStringLiteral("Ready") : QStringLiteral("None")));
}

#endif

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
    for (const QString& path : paths) {
        const uint64_t id = m_sourceCatalog->add(std::filesystem::path(path.toStdWString()));
        if (firstAddedSourceId == 0) firstAddedSourceId = id;
        const auto source = m_sourceCatalog->find(id);
        const SourceType thumbnailType = source && source->type == ObsCatalogSourceType::ImageFile
            ? SourceType::ImageFile : SourceType::VideoFile;
        LOG_INFO("OBS source catalog: added {} source #{}.", source ? obsCatalogSourceTypeName(source->type) : "UNKNOWN", id);
        ThumbnailGenerator::instance().requestThumbnail(static_cast<int>(id), path.toUtf8().toStdString(), thumbnailType);
    }
    refreshCatalogUi();
    if (const auto firstAddedSource = m_sourceCatalog->find(firstAddedSourceId)) {
        stagePreviewSource(*firstAddedSource);
        LOG_INFO("OBS source catalog: staged first added source #{} to PVW without playback.", firstAddedSourceId);
    }
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
}

void ObsDualMediaTestWindow::removeCatalogSource() {
    const auto* item = m_sourceCatalogList->currentItem();
    if (!item) return;
    removeCatalogSource(item->data(Qt::UserRole).toULongLong());
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
    if (sourceId == m_programSourceId) {
        QMessageBox::information(this, localized("PGM đang phát", "PGM is live"),
                                 localized("Không thể xoá nguồn đang phát trên PGM. Hãy chuyển nguồn khác lên PGM trước.",
                                           "The source currently live on PGM cannot be removed. Switch another source to PGM first."));
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
    rememberProgramSnapshot(captureProgramSnapshot());
    m_program.backend->resetRenderSource();
    m_program.backend->close();
    m_program.backend->setAudioOutputEnabled(true);
    m_program.backend->setLooping(false);
    if (!m_program.backend->open(*source)) return false;
    m_program.backend->play();
    initializeDisplay(m_program);
    resizeDisplay(m_program);
    setProgramSourceId(source->id);
    LOG_INFO("OBS playlist: {} activated PGM source #{}; PVW untouched.", reason, source->id);
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
        const bool vietnamese = m_language == UiLanguage::Vietnamese;
        const QString sourceName = catalogSourceName(source, vietnamese);
        auto* item = new QListWidgetItem(QStringLiteral("#%1  [%2] %3").arg(displaySlot).arg(catalogSourceBadge(source, vietnamese), sourceName), m_sourceCatalogList);
        item->setSizeHint(itemSize);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(source.id));
        item->setToolTip(source.type == ObsCatalogSourceType::RtspCamera ? QString::fromUtf8(source.endpoint.c_str()) : info.absoluteFilePath());
        auto* tile = new CatalogTileWidget([this, item, source] {
            m_sourceCatalogList->setCurrentItem(item);
            if (!m_fadeActive) stagePreviewSource(source);
        }, m_sourceCatalogList);
        tile->setFixedSize(itemSize);
        const bool isProgram = source.id == m_programSourceId && m_program.backend && m_program.backend->isOpen();
        const bool isPreview = source.id == m_previewSourceId;
        const QString borderColor = isProgram ? QStringLiteral("#8c4f5d")
            : (isPreview ? QStringLiteral("#3f91aa") : QStringLiteral("#303944"));
        const int borderWidth = (isProgram || isPreview) ? 2 : 1;
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
        auto* title = new QLabel(QStringLiteral("#%1  [%2]%3 %4")
            .arg(displaySlot)
            .arg(isProgram ? QStringLiteral("PGM") : catalogSourceBadge(source, vietnamese))
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

void ObsDualMediaTestWindow::setProgramSourceId(uint64_t sourceId) {
    m_programSourceId = sourceId;
    const auto source = m_sourceCatalog ? m_sourceCatalog->find(sourceId) : std::nullopt;
    const bool captureVideo = source && source->type == ObsCatalogSourceType::VideoFile;
    m_program.thumbnailSourceId.store(sourceId);
    m_program.thumbnailCaptureEnabled.store(captureVideo);
    m_program.thumbnailFrameCounter = 0;
    m_program.thumbnailFrameDelivered.store(false);
    refreshCatalogUi();
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
    LOG_INFO("OBS playlist: saved {} step(s); playback remains stopped until Play Playlist is pressed.", m_playlist->size());
}

void ObsDualMediaTestWindow::cancelPlaylistManager() {
    m_playlistDraft.clear();
    m_playlistEditing = false;
    if (m_playlistDialog) m_playlistDialog->hide();
    refreshPlaylistUi();
    LOG_INFO("OBS playlist: edit cancelled.");
}

void ObsDualMediaTestWindow::toggleFullscreen() {
    if (isFullScreen()) showNormal();
    else showFullScreen();
}

void ObsDualMediaTestWindow::toggleProgramOutputFullscreen() {
    if (m_programOutput && m_programOutput->isVisible()) {
        m_programOutput->hide();
        LOG_INFO("OBS program output: fullscreen output closed.");
        return;
    }

    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.size() < 2) {
        QMessageBox::information(this, localized("TOÀN MÀN HÌNH", "FULL SCREEN"),
                                 localized("Cần kết nối màn hình thứ hai để mở output PGM toàn màn hình.",
                                           "Connect a second display to open the PGM fullscreen output."));
        LOG_WARN("OBS program output: fullscreen output requested, but no second screen is available.");
        return;
    }

    if (!m_programOutput) {
        m_programOutput = std::make_unique<ObsProgramOutputWindow>([this] {
            return m_program.backend.get();
        });
    }
    m_programOutput->setWindowTitle(localized("MediaSwitcher - Output chương trình",
                                              "MediaSwitcher Program Output"));

    QScreen* outputScreen = screens.at(1);
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
}
