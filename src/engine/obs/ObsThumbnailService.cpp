#include "ObsThumbnailService.h"

#include "common/logger/Logger.h"

#include <QImageReader>
#include <QRunnable>
#include <QSize>
#include <QThread>

extern "C" {
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <obs.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>

namespace {

// Keep a 720p poster for the PVW still-frame workflow. Input tiles scale this
// image down, while PVW no longer enlarges a 320x180 bitmap when cue-only.
constexpr int kThumbnailWidth = 1280;
constexpr int kThumbnailHeight = 720;

QImage readImageThumbnail(const std::filesystem::path& path) {
    QImageReader reader(QString::fromStdWString(path.wstring()));
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        reader.setScaledSize(sourceSize.scaled(kThumbnailWidth, kThumbnailHeight, Qt::KeepAspectRatio));
    }
    return reader.read();
}

QImage renderVideoThumbnailFrame(obs_source_t* source, int maximumWidth, int maximumHeight) {
    const uint32_t sourceWidth = obs_source_get_base_width(source);
    const uint32_t sourceHeight = obs_source_get_base_height(source);
    if (sourceWidth == 0 || sourceHeight == 0) return {};

    const double scale = std::min(
        static_cast<double>(maximumWidth) / sourceWidth,
        static_cast<double>(maximumHeight) / sourceHeight);
    const uint32_t outputWidth = std::max(1U, static_cast<uint32_t>(std::lround(sourceWidth * scale)));
    const uint32_t outputHeight = std::max(1U, static_cast<uint32_t>(std::lround(sourceHeight * scale)));

    QImage image;
    obs_enter_graphics();
    gs_texrender_t* texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    gs_stagesurf_t* stage = gs_stagesurface_create(outputWidth, outputHeight, GS_BGRA);
    if (texrender && stage) {
        gs_texrender_reset(texrender);
        if (gs_texrender_begin(texrender, outputWidth, outputHeight)) {
            const vec4 black{};
            gs_clear(GS_CLEAR_COLOR, &black, 0.0f, 0);
            gs_viewport_push();
            gs_projection_push();
            gs_matrix_push();
            gs_set_viewport(0, 0, static_cast<int>(outputWidth), static_cast<int>(outputHeight));
            gs_ortho(0.0f, static_cast<float>(outputWidth), 0.0f, static_cast<float>(outputHeight), -100.0f, 100.0f);
            gs_matrix_scale3f(static_cast<float>(scale), static_cast<float>(scale), 1.0f);
            obs_source_video_render(source);
            gs_matrix_pop();
            gs_projection_pop();
            gs_viewport_pop();
            gs_texrender_end(texrender);

            if (gs_texture_t* texture = gs_texrender_get_texture(texrender)) {
                gs_stage_texture(stage, texture);
                gs_flush();
                uint8_t* data = nullptr;
                uint32_t lineSize = 0;
                if (gs_stagesurface_map(stage, &data, &lineSize)) {
                    image = QImage(static_cast<int>(outputWidth), static_cast<int>(outputHeight), QImage::Format_ARGB32);
                    for (uint32_t row = 0; row < outputHeight; ++row) {
                        std::memcpy(image.scanLine(static_cast<int>(row)), data + static_cast<size_t>(row) * lineSize,
                                    static_cast<size_t>(outputWidth) * 4U);
                    }
                    gs_stagesurface_unmap(stage);
                }
            }
        }
    }
    if (stage) gs_stagesurface_destroy(stage);
    if (texrender) gs_texrender_destroy(texrender);
    obs_leave_graphics();
    return image;
}

double visibleFrameScore(const QImage& image) {
    if (image.isNull()) return 0.0;
    const QImage sample = image.scaled(32, 18, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    double luminance = 0.0;
    for (int y = 0; y < sample.height(); ++y) {
        const auto* pixels = reinterpret_cast<const QRgb*>(sample.constScanLine(y));
        for (int x = 0; x < sample.width(); ++x) {
            luminance += 0.2126 * qRed(pixels[x]) + 0.7152 * qGreen(pixels[x]) + 0.0722 * qBlue(pixels[x]);
        }
    }
    return luminance / static_cast<double>(sample.width() * sample.height());
}

struct DecodedThumbnail {
    QImage image;
    int64_t positionMs{0};
};

DecodedThumbnail decodeVideoThumbnail(
    const std::filesystem::path& path,
    const std::function<void(const QImage&, int64_t)>& publishFastThumbnail) {
    static std::atomic_uint64_t decoderSequence{0};
    const QByteArray utf8Path = QString::fromStdWString(std::filesystem::absolute(path).wstring()).toUtf8();
    obs_data_t* settings = obs_data_create();
    if (!settings) return {};
    obs_data_set_bool(settings, "is_local_file", true);
    obs_data_set_string(settings, "local_file", utf8Path.constData());
    obs_data_set_bool(settings, "looping", false);
    // Let ffmpeg_source start only after it is attached to an active view.
    // An eager media_restart() can race its asynchronous initialization and
    // intermittently leave the poster decoder without a first frame.
    obs_data_set_bool(settings, "restart_on_activate", true);
    obs_data_set_bool(settings, "clear_on_media_end", true);

    const QByteArray sourceName = QStringLiteral("MediaSwitcher Thumbnail Decoder %1")
        .arg(decoderSequence.fetch_add(1) + 1).toUtf8();
    obs_source_t* source = obs_source_create("ffmpeg_source", sourceName.constData(), settings, nullptr);
    obs_data_release(settings);
    if (!source) return {};

    obs_view_t* view = obs_view_create();
    if (!view) {
        obs_source_release(source);
        return {};
    }
    obs_source_set_muted(source, true);
    obs_view_set_source(view, 0, source);
    obs_source_inc_active(source);

    QImage bestFrame;
    int64_t bestFramePositionMs = 0;
    double bestScore = -1.0;
    bool posterSeekRequested = false;
    bool fastThumbnailPublished = false;
    const auto startedAt = std::chrono::steady_clock::now();
    const auto deadline = startedAt + std::chrono::milliseconds(700);
    while (std::chrono::steady_clock::now() < deadline) {
        // Prefer the first decoded visible frame. It is normally available in
        // tens of milliseconds and avoids paying for a seek on every newly
        // added input. Only skip a black intro after a short grace period.
        if (!posterSeekRequested &&
            std::chrono::steady_clock::now() - startedAt >= std::chrono::milliseconds(100)) {
            const int64_t duration = obs_source_media_get_duration(source);
            if (duration > 0) {
                obs_source_media_set_time(source, std::min<int64_t>(500, duration / 10));
                posterSeekRequested = true;
            }
        }

        if (obs_source_get_base_width(source) > 0 && obs_source_get_base_height(source) > 0) {
            const int64_t candidatePositionMs = std::max<int64_t>(0, obs_source_media_get_time(source));
            QImage candidate = renderVideoThumbnailFrame(source, 320, 180);
            const double score = visibleFrameScore(candidate);
            if (score > bestScore) {
                bestScore = score;
                bestFrame = std::move(candidate);
                bestFramePositionMs = candidatePositionMs;
            }
            const bool fastDeadlineReached =
                std::chrono::steady_clock::now() - startedAt >= std::chrono::milliseconds(80);
            if (!fastThumbnailPublished && !bestFrame.isNull() &&
                (bestScore >= 2.0 || fastDeadlineReached)) {
                publishFastThumbnail(bestFrame, bestFramePositionMs);
                fastThumbnailPublished = true;
            }
            if (bestScore >= 12.0) break;
        }
        QThread::msleep(16);
    }

    // The tile already has a responsive 320x180 image. Render one high-quality
    // frame for PVW only after that first UI result has been published.
    const int64_t posterPositionMs = std::max<int64_t>(0, obs_source_media_get_time(source));
    QImage poster = renderVideoThumbnailFrame(source, kThumbnailWidth, kThumbnailHeight);
    int64_t finalPositionMs = posterPositionMs;
    if (poster.isNull()) {
        poster = bestFrame;
        finalPositionMs = bestFramePositionMs;
    }
    if (!fastThumbnailPublished && !bestFrame.isNull()) {
        publishFastThumbnail(bestFrame, bestFramePositionMs);
    }

    obs_source_media_stop(source);
    obs_source_dec_active(source);
    obs_view_set_source(view, 0, nullptr);
    obs_view_destroy(view);
    obs_source_release(source);
    return {std::move(poster), finalPositionMs};
}

class ThumbnailTask final : public QRunnable {
public:
    ThumbnailTask(uint64_t sourceId, std::filesystem::path path, ObsCatalogSourceType type,
                  ObsThumbnailService* owner)
        : m_sourceId(sourceId), m_path(std::move(path)), m_type(type), m_owner(owner) {}

    void run() override {
        QImage thumbnail;
        int64_t thumbnailPositionMs = 0;
        if (m_type == ObsCatalogSourceType::ImageFile) {
            thumbnail = readImageThumbnail(m_path);
        } else if (m_type == ObsCatalogSourceType::VideoFile) {
            DecodedThumbnail decoded = decodeVideoThumbnail(m_path,
                [this](const QImage& fastThumbnail, int64_t positionMs) {
                LOG_INFO("OBS thumbnail: Fast tile ready for source #{}.", m_sourceId);
                emit m_owner->thumbnailReady(static_cast<quint64>(m_sourceId), fastThumbnail,
                                             static_cast<qint64>(positionMs));
            });
            thumbnail = std::move(decoded.image);
            thumbnailPositionMs = decoded.positionMs;
        }

        if (thumbnail.isNull()) {
            LOG_WARN("OBS thumbnail: Could not generate a background poster for source #{}.", m_sourceId);
            return;
        }

        thumbnail = thumbnail.scaled(kThumbnailWidth, kThumbnailHeight,
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation);
        LOG_INFO("OBS thumbnail: Background poster ready for source #{}.", m_sourceId);
        emit m_owner->thumbnailReady(static_cast<quint64>(m_sourceId), std::move(thumbnail),
                                     static_cast<qint64>(thumbnailPositionMs));
    }

private:
    uint64_t m_sourceId;
    std::filesystem::path m_path;
    ObsCatalogSourceType m_type;
    ObsThumbnailService* m_owner;
};

} // namespace

ObsThumbnailService& ObsThumbnailService::instance() {
    static ObsThumbnailService service;
    return service;
}

ObsThumbnailService::ObsThumbnailService() {
    // Thumbnail decoders spend most of their lifetime waiting for ffmpeg I/O.
    // A small bounded fan-out prevents the third/fourth newly added input from
    // sitting behind two full decoder deadlines.
    m_workers.setMaxThreadCount(std::clamp(QThread::idealThreadCount(), 1, 6));
    m_workers.setExpiryTimeout(10000);
}

ObsThumbnailService::~ObsThumbnailService() {
    m_workers.waitForDone();
}

void ObsThumbnailService::request(uint64_t sourceId, const std::filesystem::path& path,
                                  ObsCatalogSourceType type) {
    if (sourceId == 0 || path.empty() ||
        (type != ObsCatalogSourceType::VideoFile && type != ObsCatalogSourceType::ImageFile)) return;

    auto* task = new ThumbnailTask(sourceId, path, type, this);
    task->setAutoDelete(true);
    m_workers.start(task);
}

void ObsThumbnailService::waitForDone() {
    m_workers.waitForDone();
}

void ObsThumbnailService::cancelPendingAndWait() {
    m_workers.clear();
    m_workers.waitForDone();
}
