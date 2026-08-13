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
    const uint64_t id = m_nextId++;
    const auto firstBlank = std::find_if(m_sources.begin(), m_sources.end(), [](const ObsCatalogSource& source) {
        return source.systemSource;
    });
    m_sources.insert(firstBlank, {id, classifyObsLocalFile(path), path, {}, {}, false});
    reconcileSystemBlanks();
    return id;
}

uint64_t ObsSourceCatalog::addRtsp(std::string endpoint, std::string displayName) {
    const uint64_t id = m_nextId++;
    const auto firstBlank = std::find_if(m_sources.begin(), m_sources.end(), [](const ObsCatalogSource& source) {
        return source.systemSource;
    });
    m_sources.insert(firstBlank, {id, ObsCatalogSourceType::RtspCamera, {}, std::move(endpoint), std::move(displayName), false});
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
    size_t existingBlankCount = static_cast<size_t>(std::count_if(m_sources.begin(), m_sources.end(), [](const ObsCatalogSource& source) {
        return source.systemSource;
    }));
    while (existingBlankCount < 2) {
        const uint64_t id = m_nextId++;
        m_sources.push_back({id, ObsCatalogSourceType::ColorBlank, {}, {}, "Blank " + std::to_string(existingBlankCount + 1), true});
        ++existingBlankCount;
    }
}
