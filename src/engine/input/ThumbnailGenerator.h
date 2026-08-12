#pragma once

#include "SourceInfo.h"
#include <QObject>
#include <QImage>
#include <string>

class ThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    static ThumbnailGenerator& instance();

    // Submit asynchronous request to generate a thumbnail (320x180)
    void requestThumbnail(int sourceId, const std::string& filePath, SourceType type);
    void requestPreviewFrame(quint64 sourceId, const std::string& filePath, SourceType type, int64_t positionMs = 0);

signals:
    void thumbnailReady(int sourceId, QImage thumbnail);
    void previewFrameReady(quint64 sourceId, int64_t positionMs, int64_t durationMs, QImage frame);

private:
    ThumbnailGenerator() = default;
    ~ThumbnailGenerator() override = default;
    ThumbnailGenerator(const ThumbnailGenerator&) = delete;
    ThumbnailGenerator& operator=(const ThumbnailGenerator&) = delete;
};
