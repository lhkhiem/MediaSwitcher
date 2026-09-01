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
#include <unordered_set>

namespace {
constexpr qint64 kMaximumWorkspaceBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaximumSources = 4096;
constexpr qsizetype kMaximumPlaylistItems = 16384;
constexpr int kWorkspaceVersion = 1;

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
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("MediaSwitcherWorkspace") ||
        root.value(QStringLiteral("version")).toInt(-1) != kWorkspaceVersion) {
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
    if (state.programSourceId != 0) {
        selection.insert(QStringLiteral("programSourceId"), sourceIdToJson(state.programSourceId));
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("MediaSwitcherWorkspace"));
    root.insert(QStringLiteral("version"), kWorkspaceVersion);
    root.insert(QStringLiteral("sources"), sources);
    root.insert(QStringLiteral("playlist"), playlist);
    root.insert(QStringLiteral("selection"), selection);

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
