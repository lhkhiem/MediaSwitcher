#pragma once

#include "engine/frame/Frame.h"
#include <string>
#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

class FFmpegDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    bool open(const std::string& filePath);
    bool decodeNextFrame(Frame& outFrame);
    bool seekToBeginning();
    bool seekToSeconds(double seconds);
    void close();

    bool isOpen() const { return m_isOpen; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    double fps() const { return m_fps; }
    double durationSeconds() const { return m_durationSeconds; }
    double currentPositionSeconds() const { return m_currentPositionSeconds; }

private:
    bool m_isOpen{false};
    std::string m_filePath;

    int m_width{0};
    int m_height{0};
    double m_fps{30.0};
    double m_durationSeconds{0.0};
    double m_currentPositionSeconds{0.0};
    int m_videoStreamIndex{-1};

    AVFormatContext* m_formatContext{nullptr};
    AVCodecContext* m_codecContext{nullptr};
    AVFrame* m_avFrame{nullptr};
    AVFrame* m_rgbaFrame{nullptr};
    AVPacket* m_avPacket{nullptr};
    SwsContext* m_swsContext{nullptr};
};
