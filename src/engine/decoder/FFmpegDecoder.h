#pragma once

#include "engine/frame/Frame.h"
#include <string>
#include <memory>
#include <vector>
#include <deque>
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

class FFmpegDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    bool open(const std::string& filePath);
    bool decodeNextFrame(Frame& outFrame);
    
    // Reads decoded and resampled audio samples (Stereo 48kHz float: L, R, L, R...)
    bool decodeAudioSamples(std::vector<float>& outPcmBuffer);

    bool seekToBeginning();
    bool seekToSeconds(double seconds);
    void close();

    bool isOpen() const { return m_isOpen; }
    bool hasAudio() const { return m_audioStreamIndex >= 0; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    double fps() const { return m_fps; }
    double durationSeconds() const { return m_durationSeconds; }
    double currentPositionSeconds() const { return m_currentPositionSeconds; }

private:
    void readPackets(size_t maxCount = 30);
    void clearPacketQueues();

    bool m_isOpen{false};
    std::string m_filePath;

    int m_width{0};
    int m_height{0};
    double m_fps{30.0};
    double m_durationSeconds{0.0};
    double m_currentPositionSeconds{0.0};
    int m_videoStreamIndex{-1};
    int m_audioStreamIndex{-1};

    AVFormatContext* m_formatContext{nullptr};
    AVCodecContext* m_codecContext{nullptr};
    AVCodecContext* m_audioCodecContext{nullptr};
    
    AVFrame* m_avFrame{nullptr};
    AVFrame* m_audioFrame{nullptr};
    AVFrame* m_rgbaFrame{nullptr};
    AVPacket* m_avPacket{nullptr};
    
    SwsContext* m_swsContext{nullptr};
    SwrContext* m_swrContext{nullptr};

    std::mutex m_queueMutex;
    std::deque<AVPacket*> m_videoPacketQueue;
    std::deque<AVPacket*> m_audioPacketQueue;
};
