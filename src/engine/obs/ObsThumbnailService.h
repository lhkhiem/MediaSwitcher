#pragma once

#include "ObsSourceCatalog.h"

#include <QImage>
#include <QObject>
#include <QThreadPool>

#include <cstdint>
#include <filesystem>

class ObsThumbnailService final : public QObject {
    Q_OBJECT

public:
    static ObsThumbnailService& instance();

    void request(uint64_t sourceId, const std::filesystem::path& path, ObsCatalogSourceType type);
    void waitForDone();
    void cancelPendingAndWait();

signals:
    // positionMs belongs to the exact decoded frame carried by thumbnail.
    // Keeping them in one signal prevents PVW from cueing one timestamp while
    // displaying a poster captured at another timestamp.
    void thumbnailReady(quint64 sourceId, QImage thumbnail, qint64 positionMs);

private:
    ObsThumbnailService();
    ~ObsThumbnailService() override;
    ObsThumbnailService(const ObsThumbnailService&) = delete;
    ObsThumbnailService& operator=(const ObsThumbnailService&) = delete;

    QThreadPool m_workers;
};
