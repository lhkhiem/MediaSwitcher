#include "ObsSourceCatalog.h"

#include <algorithm>
#include <cctype>

namespace {
std::string lowercaseExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}
}

ObsCatalogSourceType classifyObsLocalFile(const std::filesystem::path& path) {
    const std::string extension = lowercaseExtension(path);
    if (extension == ".jpg" || extension == ".jpeg" || extension == ".png" || extension == ".bmp" ||
        extension == ".webp" || extension == ".gif" || extension == ".tiff") {
        return ObsCatalogSourceType::ImageFile;
    }
    if (extension == ".mp3" || extension == ".wav" || extension == ".flac" || extension == ".aac" ||
        extension == ".m4a" || extension == ".ogg" || extension == ".opus") {
        return ObsCatalogSourceType::AudioFile;
    }
    return ObsCatalogSourceType::VideoFile;
}

const char* obsCatalogSourceTypeName(ObsCatalogSourceType type) {
    switch (type) {
    case ObsCatalogSourceType::VideoFile: return "VIDEO";
    case ObsCatalogSourceType::AudioFile: return "AUDIO";
    case ObsCatalogSourceType::ImageFile: return "IMAGE";
    case ObsCatalogSourceType::RtspCamera: return "RTSP";
    case ObsCatalogSourceType::ColorBlank: return "BLANK";
    }
    return "UNKNOWN";
}

bool obsCatalogSourceHasTimeline(ObsCatalogSourceType type) {
    return type == ObsCatalogSourceType::VideoFile || type == ObsCatalogSourceType::AudioFile;
}

uint64_t ObsSourceCatalog::add(const std::filesystem::path& path) {
    return replaceFirstBlank(classifyObsLocalFile(path), path, {}, {});
}

uint64_t ObsSourceCatalog::addRtsp(std::string endpoint, std::string displayName) {
    return replaceFirstBlank(ObsCatalogSourceType::RtspCamera, {}, std::move(endpoint), std::move(displayName));
}

uint64_t ObsSourceCatalog::replaceFirstBlank(ObsCatalogSourceType type, std::filesystem::path path, std::string endpoint,
                                             std::string displayName) {
    const auto firstBlank = std::find_if(m_sources.begin(), m_sources.end(), [](const ObsCatalogSource& source) {
        return source.systemSource;
    });
    if (firstBlank != m_sources.end()) {
        // Fill the first visible Blank before appending an input.  Display
        // slot numbers are recalculated from catalog order by the UI.
        firstBlank->type = type;
        firstBlank->path = std::move(path);
        firstBlank->endpoint = std::move(endpoint);
        firstBlank->displayName = std::move(displayName);
        firstBlank->systemSource = false;
        const uint64_t id = firstBlank->id;
        reconcileSystemBlanks();
        return id;
    }

    const uint64_t id = m_nextId++;
    m_sources.push_back({id, type, std::move(path), std::move(endpoint), std::move(displayName), false});
    reconcileSystemBlanks();
    return id;
}

uint64_t ObsSourceCatalog::addSystemBlank(std::string displayName) {
    const uint64_t id = m_nextId++;
    m_sources.push_back({id, ObsCatalogSourceType::ColorBlank, {}, {}, std::move(displayName), true});
    return id;
}

bool ObsSourceCatalog::remove(uint64_t id) {
    const auto it = std::find_if(m_sources.begin(), m_sources.end(), [id](const ObsCatalogSource& source) {
        return source.id == id;
    });
    if (it == m_sources.end()) return false;

    m_sources.erase(it);
    reconcileSystemBlanks();
    return true;
}

std::optional<ObsCatalogSource> ObsSourceCatalog::find(uint64_t id) const {
    const auto it = std::find_if(m_sources.begin(), m_sources.end(), [id](const ObsCatalogSource& source) {
        return source.id == id;
    });
    if (it == m_sources.end()) return std::nullopt;
    return *it;
}

void ObsSourceCatalog::reconcileSystemBlanks() {
    // Blank is a placeholder only when the catalog has fewer than two input
    // slots.  Do not maintain two hidden Blank records in addition to real
    // inputs: that was the cause of duplicate Blank #5/#6 tiles after remove.
    while (m_sources.size() < 2) {
        const uint64_t id = m_nextId++;
        m_sources.push_back({id, ObsCatalogSourceType::ColorBlank, {}, {}, {}, true});
    }
    for (size_t index = 0; index < m_sources.size(); ++index) {
        if (m_sources[index].systemSource) {
            m_sources[index].displayName = "Blank " + std::to_string(index + 1);
        }
    }
}
