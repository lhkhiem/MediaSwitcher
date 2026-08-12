#include "ThumbnailGenerator.h"
#include "engine/decoder/FFmpegDecoder.h"
#include "common/logger/Logger.h"
#include <QThreadPool>
#include <QRunnable>
#include <QImageReader>
#include <QColor>

class ThumbnailTask : public QRunnable {
public:
    ThumbnailTask(int sourceId, std::string filePath, SourceType type, ThumbnailGenerator* owner)
        : m_sourceId(sourceId), m_filePath(std::move(filePath)), m_type(type), m_owner(owner) {}

    void run() override {
        QImage thumb;

        if (m_type == SourceType::ImageFile) {
            QImageReader reader(QString::fromStdString(m_filePath));
            reader.setAutoTransform(true);
            QImage orig = reader.read();
            if (!orig.isNull()) {
                thumb = orig.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        } else if (m_type == SourceType::VideoFile) {
            FFmpegDecoder decoder;
            if (decoder.open(m_filePath)) {
                if (decoder.durationSeconds() > 2.0) {
                    decoder.seekToSeconds(1.0);
                }

                Frame f(decoder.width() > 0 ? decoder.width() : 1280,
                        decoder.height() > 0 ? decoder.height() : 720,
                        PixelFormat::RGBA32);

                for (int i = 0; i < 10; ++i) {
                    if (decoder.decodeNextFrame(f) && f.data() && f.width() > 0 && f.height() > 0) {
                        QImage img(f.data(), f.width(), f.height(), QImage::Format_RGBA8888);
                        thumb = img.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation);

                        int w = img.width();
                        int h = img.height();
                        QRgb centerPix = img.pixel(w / 2, h / 2);
                        if (qRed(centerPix) + qGreen(centerPix) + qBlue(centerPix) > 25) {
                            break; // Found clear poster frame
                        }
                    }
                }
                decoder.close(); // DESTROY DECODER IMMEDIATELY AFTER THUMBNAIL
            }
        }

        if (thumb.isNull()) {
            thumb = QImage(320, 180, QImage::Format_RGBA8888);
            thumb.fill(QColor(34, 36, 51));
        }

        LOG_INFO("ThumbnailGenerator: Generated 320x180 thumbnail for slot #{}", m_sourceId);
        emit m_owner->thumbnailReady(m_sourceId, thumb);
    }

private:
    int m_sourceId;
    std::string m_filePath;
    SourceType m_type;
    ThumbnailGenerator* m_owner;
};

class PreviewFrameTask : public QRunnable {
public:
    PreviewFrameTask(quint64 sourceId, std::string filePath, SourceType type, int64_t positionMs, ThumbnailGenerator* owner)
        : m_sourceId(sourceId), m_filePath(std::move(filePath)), m_type(type), m_positionMs(positionMs), m_owner(owner) {}

    void run() override {
        QImage frame;
        int64_t durationMs = 0;
        if (m_type == SourceType::ImageFile) {
            QImageReader reader(QString::fromStdString(m_filePath));
            reader.setAutoTransform(true);
            frame = reader.read();
        } else if (m_type == SourceType::VideoFile) {
            FFmpegDecoder decoder;
            if (decoder.open(m_filePath)) {
                durationMs = static_cast<int64_t>(decoder.durationSeconds() * 1000.0);
                if (m_positionMs > 0) decoder.seekToSeconds(static_cast<double>(m_positionMs) / 1000.0);
                Frame decoded(decoder.width() > 0 ? decoder.width() : 1280,
                              decoder.height() > 0 ? decoder.height() : 720,
                              PixelFormat::RGBA32);
                for (int i = 0; i < 10; ++i) {
                    if (decoder.decodeNextFrame(decoded) && decoded.data() && decoded.width() > 0 && decoded.height() > 0) {
                        frame = QImage(decoded.data(), decoded.width(), decoded.height(), QImage::Format_RGBA8888).copy();
                        break;
                    }
                }
                decoder.close();
            }
        }
        if (frame.isNull()) {
            frame = QImage(320, 180, QImage::Format_RGBA8888);
            frame.fill(QColor(34, 36, 51));
        }
        emit m_owner->previewFrameReady(m_sourceId, m_positionMs, durationMs, frame);
    }

private:
    quint64 m_sourceId;
    std::string m_filePath;
    SourceType m_type;
    int64_t m_positionMs;
    ThumbnailGenerator* m_owner;
};

ThumbnailGenerator& ThumbnailGenerator::instance() {
    static ThumbnailGenerator inst;
    return inst;
}

void ThumbnailGenerator::requestThumbnail(int sourceId, const std::string& filePath, SourceType type) {
    auto* task = new ThumbnailTask(sourceId, filePath, type, this);
    task->setAutoDelete(true);
    QThreadPool::globalInstance()->start(task);
}

void ThumbnailGenerator::requestPreviewFrame(quint64 sourceId, const std::string& filePath, SourceType type, int64_t positionMs) {
    auto* task = new PreviewFrameTask(sourceId, filePath, type, positionMs, this);
    task->setAutoDelete(true);
    QThreadPool::globalInstance()->start(task);
}
