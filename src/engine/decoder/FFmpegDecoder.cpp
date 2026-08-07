#include "FFmpegDecoder.h"
#include "common/logger/Logger.h"
#include <algorithm>

FFmpegDecoder::FFmpegDecoder() {
    m_avFrame = av_frame_alloc();
    m_rgbaFrame = av_frame_alloc();
    m_avPacket = av_packet_alloc();
}

FFmpegDecoder::~FFmpegDecoder() {
    close();
    if (m_avFrame) av_frame_free(&m_avFrame);
    if (m_rgbaFrame) av_frame_free(&m_rgbaFrame);
    if (m_avPacket) av_packet_free(&m_avPacket);
}

bool FFmpegDecoder::open(const std::string& filePath) {
    close();
    m_filePath = filePath;

    if (avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, nullptr) != 0) {
        LOG_ERROR("FFmpeg: Cannot open file '{}'", filePath);
        return false;
    }

    if (avformat_find_stream_info(m_formatContext, nullptr) < 0) {
        LOG_ERROR("FFmpeg: Cannot find stream info for '{}'", filePath);
        close();
        return false;
    }

    m_videoStreamIndex = -1;
    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        if (m_formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = static_cast<int>(i);
            break;
        }
    }

    if (m_videoStreamIndex == -1) {
        LOG_ERROR("FFmpeg: No video stream found in '{}'", filePath);
        close();
        return false;
    }

    AVCodecParameters* codecParams = m_formatContext->streams[m_videoStreamIndex]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        LOG_ERROR("FFmpeg: Unsupported codec for '{}'", filePath);
        close();
        return false;
    }

    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext) {
        LOG_ERROR("FFmpeg: Failed to allocate codec context.");
        close();
        return false;
    }

    if (avcodec_parameters_to_context(m_codecContext, codecParams) < 0) {
        LOG_ERROR("FFmpeg: Failed to copy codec params to context.");
        close();
        return false;
    }

    m_codecContext->thread_count = 4;
    m_codecContext->thread_type = FF_THREAD_FRAME;

    if (avcodec_open2(m_codecContext, codec, nullptr) < 0) {
        LOG_ERROR("FFmpeg: Failed to open codec.");
        close();
        return false;
    }

    m_width = m_codecContext->width;
    m_height = m_codecContext->height;

    AVRational streamFps = m_formatContext->streams[m_videoStreamIndex]->avg_frame_rate;
    if (streamFps.den > 0 && streamFps.num > 0) {
        m_fps = av_q2d(streamFps);
    } else {
        m_fps = 30.0;
    }

    m_isOpen = true;
    LOG_INFO("FFmpeg: Successfully opened '{}' ({}x{} @ {:.2f} fps)", filePath, m_width, m_height, m_fps);
    return true;
}

bool FFmpegDecoder::decodeNextFrame(Frame& outFrame) {
    if (!m_isOpen || !m_formatContext || !m_codecContext) return false;

    while (m_isOpen) {
        int ret = avcodec_receive_frame(m_codecContext, m_avFrame);
        if (ret == 0) {
            int frameW = m_avFrame->width > 0 ? m_avFrame->width : m_width;
            int frameH = m_avFrame->height > 0 ? m_avFrame->height : m_height;
            m_width = frameW;
            m_height = frameH;

            AVPixelFormat pixFmt = static_cast<AVPixelFormat>(m_avFrame->format);

            m_swsContext = sws_getCachedContext(
                m_swsContext,
                frameW, frameH, pixFmt,
                frameW, frameH, AV_PIX_FMT_RGBA,
                SWS_BICUBIC | SWS_ACCURATE_RND, nullptr, nullptr, nullptr
            );

            if (!m_swsContext) return false;

            if (outFrame.width() != frameW || outFrame.height() != frameH) {
                outFrame.resize(frameW, frameH, PixelFormat::RGBA32);
            }

            uint8_t* dstData[4] = { outFrame.data(), nullptr, nullptr, nullptr };
            int dstLinesize[4] = { outFrame.stride(), 0, 0, 0 };

            sws_scale(
                m_swsContext,
                m_avFrame->data,
                m_avFrame->linesize,
                0,
                frameH,
                dstData,
                dstLinesize
            );

            return true;
        }

        // Read next packet from container
        if (av_read_frame(m_formatContext, m_avPacket) >= 0) {
            if (m_avPacket->stream_index == m_videoStreamIndex) {
                avcodec_send_packet(m_codecContext, m_avPacket);
            }
            av_packet_unref(m_avPacket);
        } else {
            // EOF reached: send flush packet to decoder
            avcodec_send_packet(m_codecContext, nullptr);
            
            // Try receiving flushed frames
            ret = avcodec_receive_frame(m_codecContext, m_avFrame);
            if (ret == 0) {
                int frameW = m_avFrame->width > 0 ? m_avFrame->width : m_width;
                int frameH = m_avFrame->height > 0 ? m_avFrame->height : m_height;
                m_width = frameW;
                m_height = frameH;

                AVPixelFormat pixFmt = static_cast<AVPixelFormat>(m_avFrame->format);

                m_swsContext = sws_getCachedContext(
                    m_swsContext,
                    frameW, frameH, pixFmt,
                    frameW, frameH, AV_PIX_FMT_RGBA,
                    SWS_BICUBIC | SWS_ACCURATE_RND, nullptr, nullptr, nullptr
                );

                if (!m_swsContext) return false;

                if (outFrame.width() != frameW || outFrame.height() != frameH) {
                    outFrame.resize(frameW, frameH, PixelFormat::RGBA32);
                }

                uint8_t* dstData[4] = { outFrame.data(), nullptr, nullptr, nullptr };
                int dstLinesize[4] = { outFrame.stride(), 0, 0, 0 };

                sws_scale(
                    m_swsContext,
                    m_avFrame->data,
                    m_avFrame->linesize,
                    0,
                    frameH,
                    dstData,
                    dstLinesize
                );

                return true;
            }

            return false;
        }
    }

    return false;
}

bool FFmpegDecoder::seekToBeginning() {
    if (!m_isOpen || !m_formatContext) return false;
    if (m_videoStreamIndex < 0) return false;

    av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
    avcodec_flush_buffers(m_codecContext);
    return true;
}

void FFmpegDecoder::close() {
    m_isOpen = false;
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
    }
    m_width = 0;
    m_height = 0;
    m_videoStreamIndex = -1;
}
