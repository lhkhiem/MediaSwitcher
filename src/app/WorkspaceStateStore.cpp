#include "WorkspaceStateStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QString>

#include <optional>
#include <algorithm>
#include <unordered_set>

namespace {
constexpr qint64 kMaximumWorkspaceBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaximumSources = 4096;
constexpr qsizetype kMaximumPlaylistItems = 16384;
constexpr int kWorkspaceVersion = 2;

QString toQString(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

QString sourceTypeName(ObsCatalogSourceType type) {
    switch (type) {
    case ObsCatalogSourceType::VideoFile: return QStringLiteral("video");
    case ObsCatalogSourceType::AudioFile: return QStringLiteral("audio");
    case ObsCatalogSourceType::ImageFile: return QStringLiteral("image");
    case ObsCatalogSourceType::RtspCamera: return QStringLiteral("rtsp");
    case ObsCatalogSourceType::ColorBlank: return QStringLiteral("blank");
    }
    return {};
}

std::optional<ObsCatalogSourceType> parseSourceType(const QString& value) {
    if (value == QStringLiteral("video")) return ObsCatalogSourceType::VideoFile;
    if (value == QStringLiteral("audio")) return ObsCatalogSourceType::AudioFile;
    if (value == QStringLiteral("image")) return ObsCatalogSourceType::ImageFile;
    if (value == QStringLiteral("rtsp")) return ObsCatalogSourceType::RtspCamera;
    return std::nullopt;
}

QString sourceIdToJson(uint64_t sourceId) {
    return QString::number(sourceId);
}

std::optional<uint64_t> parseSourceId(const QJsonValue& value) {
    if (!value.isString()) return std::nullopt;
    bool valid = false;
    const qulonglong parsed = value.toString().toULongLong(&valid);
    if (!valid || parsed == 0) return std::nullopt;
    return static_cast<uint64_t>(parsed);
}

int64_t parsePosition(const QJsonValue& value) {
    if (!value.isString()) return 0;
    bool valid = false;
    const qlonglong parsed = value.toString().toLongLong(&valid);
    return valid ? std::max<qlonglong>(0, parsed) : 0;
}

void setError(std::string& error, const QString& value) {
    error = value.toUtf8().toStdString();
}
}

WorkspaceStateStore::WorkspaceStateStore(std::filesystem::path filePath)
    : m_filePath(std::move(filePath)) {}

WorkspaceLoadStatus WorkspaceStateStore::load(WorkspaceState& state, std::string& error) const {
    state = {};
    error.clear();

    QFile file(toQString(m_filePath));
    if (!file.exists()) return WorkspaceLoadStatus::NotFound;
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return WorkspaceLoadStatus::IoError;
    }
    if (file.size() < 0 || file.size() > kMaximumWorkspaceBytes) {
        setError(error, QStringLiteral("Kích thước workspace không hợp lệ."));
        return WorkspaceLoadStatus::Invalid;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, parseError.errorString());
        return WorkspaceLoadStatus::Invalid;
    }

    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("version")).toInt(-1);
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("MediaSwitcherWorkspace") ||
        (version != 1 && version != kWorkspaceVersion)) {
        setError(error, QStringLiteral("Định dạng hoặc phiên bản workspace không được hỗ trợ."));
        return WorkspaceLoadStatus::Invalid;
    }

    const QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
    if (sources.size() > kMaximumSources) {
        setError(error, QStringLiteral("Workspace chứa quá nhiều source."));
        return WorkspaceLoadStatus::Invalid;
    }

    std::unordered_set<uint64_t> sourceIds;
    state.sources.reserve(static_cast<size_t>(sources.size()));
    for (const QJsonValue& value : sources) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const auto sourceId = parseSourceId(object.value(QStringLiteral("id")));
        const auto type = parseSourceType(object.value(QStringLiteral("type")).toString());
        if (!sourceId || !type || !sourceIds.insert(*sourceId).second) continue;

        WorkspaceSourceRecord source;
        source.sourceId = *sourceId;
        source.type = *type;
        source.path = std::filesystem::path(object.value(QStringLiteral("path")).toString().toStdWString());
        source.endpoint = object.value(QStringLiteral("endpoint")).toString().toUtf8().toStdString();
        source.displayName = object.value(QStringLiteral("displayName")).toString().toUtf8().toStdString();
        if (source.type == ObsCatalogSourceType::RtspCamera ? source.endpoint.empty() : source.path.empty()) continue;
        state.sources.push_back(std::move(source));
    }

    const QJsonObject playlist = root.value(QStringLiteral("playlist")).toObject();
    state.playlistLoop = playlist.value(QStringLiteral("loop")).toBool(true);
    state.playlistAutoNext = playlist.value(QStringLiteral("autoNext")).toBool(true);
    const QJsonArray playlistIds = playlist.value(QStringLiteral("sourceIds")).toArray();
    if (playlistIds.size() > kMaximumPlaylistItems) {
        setError(error, QStringLiteral("Workspace chứa playlist quá dài."));
        return WorkspaceLoadStatus::Invalid;
    }
    state.playlistSourceIds.reserve(static_cast<size_t>(playlistIds.size()));
    for (const QJsonValue& value : playlistIds) {
        if (const auto sourceId = parseSourceId(value)) state.playlistSourceIds.push_back(*sourceId);
    }

    const QJsonObject selection = root.value(QStringLiteral("selection")).toObject();
    if (const auto previewId = parseSourceId(selection.value(QStringLiteral("previewSourceId")))) {
        state.previewSourceId = *previewId;
    }
    if (const auto programId = parseSourceId(selection.value(QStringLiteral("programSourceId")))) {
        state.programSourceId = *programId;
    }
    if (version >= 2) {
        const QJsonObject project = root.value(QStringLiteral("project")).toObject();
        const int fpsNumerator = project.value(QStringLiteral("fpsNumerator")).toInt(0);
        const int fpsDenominator = project.value(QStringLiteral("fpsDenominator")).toInt(0);
        state.fpsNumerator = fpsNumerator > 0 ? static_cast<uint32_t>(fpsNumerator) : 0;
        state.fpsDenominator = fpsDenominator > 0 ? static_cast<uint32_t>(fpsDenominator) : 0;
        state.programRenderMode = project.value(QStringLiteral("programRenderMode")).toInt(1);
        state.fadeDurationMs = std::clamp(project.value(QStringLiteral("fadeDurationMs")).toInt(700), 100, 10000);

        const QJsonObject interfaceState = root.value(QStringLiteral("interface")).toObject();
        state.thumbnailWidth = std::clamp(interfaceState.value(QStringLiteral("thumbnailWidth")).toInt(192), 80, 400);
        state.sourceTypeFilter = interfaceState.value(QStringLiteral("sourceTypeFilter")).toInt(-1);

        const QJsonObject preview = root.value(QStringLiteral("preview")).toObject();
        state.previewPositionMs = parsePosition(preview.value(QStringLiteral("positionMs")));
        state.previewLoop = preview.value(QStringLiteral("loop")).toBool(false);
        state.previewVolume = static_cast<float>(std::clamp(preview.value(QStringLiteral("volume")).toDouble(1.0), 0.0, 1.0));
        state.previewMuted = preview.value(QStringLiteral("muted")).toBool(false);

        const QJsonObject program = root.value(QStringLiteral("program")).toObject();
        state.programPositionMs = parsePosition(program.value(QStringLiteral("positionMs")));
        state.programLoop = program.value(QStringLiteral("loop")).toBool(false);
        state.programVolume = static_cast<float>(std::clamp(program.value(QStringLiteral("volume")).toDouble(1.0), 0.0, 1.0));
        state.programMuted = program.value(QStringLiteral("muted")).toBool(false);

        state.outputScreenName = root.value(QStringLiteral("output")).toObject()
            .value(QStringLiteral("screenName")).toString().toUtf8().toStdString();

        const QJsonObject triple = root.value(QStringLiteral("tripleLayout")).toObject();
        if (!triple.isEmpty()) {
            state.layoutMode = std::clamp(triple.value(QStringLiteral("layoutMode")).toInt(0), 0, 1);
            state.canvasWidth = std::clamp(triple.value(QStringLiteral("canvasWidth")).toInt(3200), 16, 16384);
            state.canvasHeight = std::clamp(triple.value(QStringLiteral("canvasHeight")).toInt(1080), 16, 16384);
            state.activeZoneIndex = std::clamp(triple.value(QStringLiteral("activeZoneIndex")).toInt(1), 0, 2);
            state.blackoutAll = triple.value(QStringLiteral("blackoutAll")).toBool(false);
            const QJsonArray zones = triple.value(QStringLiteral("zones")).toArray();
            for (int index = 0; index < std::min<int>(3, static_cast<int>(zones.size())); ++index) {
                const QJsonObject zoneObj = zones.at(index).toObject();
                auto& zone = state.zones[static_cast<size_t>(index)];
                zone.x = std::clamp(zoneObj.value(QStringLiteral("x")).toInt(zone.x), 0, 16384);
                zone.y = std::clamp(zoneObj.value(QStringLiteral("y")).toInt(zone.y), 0, 16384);
                zone.width = std::clamp(zoneObj.value(QStringLiteral("width")).toInt(zone.width), 16, 16384);
                zone.height = std::clamp(zoneObj.value(QStringLiteral("height")).toInt(zone.height), 16, 16384);
                zone.renderMode = std::clamp(zoneObj.value(QStringLiteral("renderMode")).toInt(1), 0, 2);
                if (const auto srcId = parseSourceId(zoneObj.value(QStringLiteral("sourceId")))) {
                    zone.sourceId = *srcId;
                } else {
                    zone.sourceId = 0;
                }
                zone.positionMs = parsePosition(zoneObj.value(QStringLiteral("positionMs")));
                zone.loop = zoneObj.value(QStringLiteral("loop")).toBool(false);
                zone.volume = static_cast<float>(std::clamp(zoneObj.value(QStringLiteral("volume")).toDouble(1.0), 0.0, 1.0));
                zone.muted = zoneObj.value(QStringLiteral("muted")).toBool(index != 1);
                zone.blackout = zoneObj.value(QStringLiteral("blackout")).toBool(false);
            }
        }
    }
    return WorkspaceLoadStatus::Loaded;
}

bool WorkspaceStateStore::save(const WorkspaceState& state, std::string& error) const {
    error.clear();
    QJsonArray sources;
    for (const WorkspaceSourceRecord& source : state.sources) {
        if (source.sourceId == 0 || source.type == ObsCatalogSourceType::ColorBlank) continue;
        QJsonObject object;
        object.insert(QStringLiteral("id"), sourceIdToJson(source.sourceId));
        object.insert(QStringLiteral("type"), sourceTypeName(source.type));
        object.insert(QStringLiteral("path"), toQString(source.path));
        object.insert(QStringLiteral("endpoint"), QString::fromUtf8(source.endpoint.c_str()));
        object.insert(QStringLiteral("displayName"), QString::fromUtf8(source.displayName.c_str()));
        sources.append(object);
    }

    QJsonArray playlistIds;
    for (const uint64_t sourceId : state.playlistSourceIds) playlistIds.append(sourceIdToJson(sourceId));

    QJsonObject playlist;
    playlist.insert(QStringLiteral("sourceIds"), playlistIds);
    playlist.insert(QStringLiteral("loop"), state.playlistLoop);
    playlist.insert(QStringLiteral("autoNext"), state.playlistAutoNext);

    QJsonObject selection;
    if (state.previewSourceId != 0) {
        selection.insert(QStringLiteral("previewSourceId"), sourceIdToJson(state.previewSourceId));
    }

    QJsonObject project;
    project.insert(QStringLiteral("fpsNumerator"), static_cast<int>(state.fpsNumerator));
    project.insert(QStringLiteral("fpsDenominator"), static_cast<int>(state.fpsDenominator));
    project.insert(QStringLiteral("programRenderMode"), state.programRenderMode);
    project.insert(QStringLiteral("fadeDurationMs"), state.fadeDurationMs);

    QJsonObject interfaceState;
    interfaceState.insert(QStringLiteral("thumbnailWidth"), state.thumbnailWidth);
    interfaceState.insert(QStringLiteral("sourceTypeFilter"), state.sourceTypeFilter);

    QJsonObject preview;
    preview.insert(QStringLiteral("positionMs"), QString::number(state.previewPositionMs));
    preview.insert(QStringLiteral("loop"), state.previewLoop);
    preview.insert(QStringLiteral("volume"), state.previewVolume);
    preview.insert(QStringLiteral("muted"), state.previewMuted);

    QJsonObject program;
    program.insert(QStringLiteral("positionMs"), QString::number(state.programPositionMs));
    program.insert(QStringLiteral("loop"), state.programLoop);
    program.insert(QStringLiteral("volume"), state.programVolume);
    program.insert(QStringLiteral("muted"), state.programMuted);

    QJsonObject output;
    output.insert(QStringLiteral("screenName"), QString::fromUtf8(state.outputScreenName.c_str()));
    if (state.programSourceId != 0) {
        selection.insert(QStringLiteral("programSourceId"), sourceIdToJson(state.programSourceId));
    }

    QJsonArray zonesArray;
    for (const auto& zone : state.zones) {
        QJsonObject zoneObj;
        zoneObj.insert(QStringLiteral("x"), zone.x);
        zoneObj.insert(QStringLiteral("y"), zone.y);
        zoneObj.insert(QStringLiteral("width"), zone.width);
        zoneObj.insert(QStringLiteral("height"), zone.height);
        zoneObj.insert(QStringLiteral("renderMode"), zone.renderMode);
        if (zone.sourceId != 0) {
            zoneObj.insert(QStringLiteral("sourceId"), sourceIdToJson(zone.sourceId));
        }
        zoneObj.insert(QStringLiteral("positionMs"), QString::number(zone.positionMs));
        zoneObj.insert(QStringLiteral("loop"), zone.loop);
        zoneObj.insert(QStringLiteral("volume"), zone.volume);
        zoneObj.insert(QStringLiteral("muted"), zone.muted);
        zoneObj.insert(QStringLiteral("blackout"), zone.blackout);
        zonesArray.append(zoneObj);
    }

    QJsonObject tripleLayout;
    tripleLayout.insert(QStringLiteral("layoutMode"), state.layoutMode);
    tripleLayout.insert(QStringLiteral("canvasWidth"), state.canvasWidth);
    tripleLayout.insert(QStringLiteral("canvasHeight"), state.canvasHeight);
    tripleLayout.insert(QStringLiteral("activeZoneIndex"), state.activeZoneIndex);
    tripleLayout.insert(QStringLiteral("blackoutAll"), state.blackoutAll);
    tripleLayout.insert(QStringLiteral("zones"), zonesArray);

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("MediaSwitcherWorkspace"));
    root.insert(QStringLiteral("version"), kWorkspaceVersion);
    root.insert(QStringLiteral("sources"), sources);
    root.insert(QStringLiteral("playlist"), playlist);
    root.insert(QStringLiteral("selection"), selection);
    root.insert(QStringLiteral("project"), project);
    root.insert(QStringLiteral("interface"), interfaceState);
    root.insert(QStringLiteral("preview"), preview);
    root.insert(QStringLiteral("program"), program);
    root.insert(QStringLiteral("output"), output);
    root.insert(QStringLiteral("tripleLayout"), tripleLayout);

    const QFileInfo destination(toQString(m_filePath));
    if (!destination.dir().mkpath(QStringLiteral("."))) {
        setError(error, QStringLiteral("Không thể tạo thư mục workspace."));
        return false;
    }

    QSaveFile file(destination.absoluteFilePath());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        setError(error, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}
