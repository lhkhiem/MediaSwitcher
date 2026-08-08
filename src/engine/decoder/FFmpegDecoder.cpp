#include "FFmpegDecoder.h"
#include "common/logger/Logger.h"
#include <algorithm>

FFmpegDecoder::FFmpegDecoder() {
    m_avFrame = av_frame_alloc();
    m_audioFrame = av_frame_alloc();
    m_rgbaFrame = av_frame_alloc();
    m_avPacket = av_packet_alloc();
}

FFmpegDecoder::~FFmpegDecoder() {
    close();
    if (m_avFrame) av_frame_free(&m_avFrame);
    if (m_audioFrame) av_frame_free(&m_audioFrame);
    if (m_rgbaFrame) av_frame_free(&m_rgbaFrame);
    if (m_avPacket) av_packet_free(&m_avPacket);
}

void FFmpegDecoder::clearPacketQueues() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto pkt : m_videoPacketQueue) {
        if (pkt) av_packet_free(&pkt);
    }
    m_videoPacketQueue.clear();

    for (auto pkt : m_audioPacketQueue) {
        if (pkt) av_packet_free(&pkt);
    }
    m_audioPacketQueue.clear();
}

void FFmpegDecoder::readPackets(size_t maxCount) {
    if (!m_isOpen || !m_formatContext) return;

    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_videoPacketQueue.size() >= 300 && m_audioPacketQueue.size() >= 500) return;

    size_t readCount = 0;
    while (m_isOpen && readCount < maxCount) {
        if (av_read_frame(m_formatContext, m_avPacket) >= 0) {
            if (m_avPacket->stream_index == m_videoStreamIndex) {
                AVPacket* pkt = av_packet_clone(m_avPacket);
                m_videoPacketQueue.push_back(pkt);
            } else if (m_avPacket->stream_index == m_audioStreamIndex) {
                AVPacket* pkt = av_packet_clone(m_avPacket);
                m_audioPacketQueue.push_back(pkt);
            }
            av_packet_unref(m_avPacket);
            readCount++;
        } else {
            break;
        }
    }
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
    m_audioStreamIndex = -1;

    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        auto codecType = m_formatContext->streams[i]->codecpar->codec_type;
        if (codecType == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex == -1) {
            m_videoStreamIndex = static_cast<int>(i);
        } else if (codecType == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex == -1) {
            m_audioStreamIndex = static_cast<int>(i);
        }
    }

    if (m_videoStreamIndex == -1 && m_audioStreamIndex == -1) {
        LOG_ERROR("FFmpeg: Neither video nor audio stream found in '{}'", filePath);
        close();
        return false;
    }

    // Setup Video Decoder
    if (m_videoStreamIndex >= 0) {
        AVCodecParameters* codecParams = m_formatContext->streams[m_videoStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
        if (codec) {
            m_codecContext = avcodec_alloc_context3(codec);
            if (m_codecContext) {
                avcodec_parameters_to_context(m_codecContext, codecParams);
                m_codecContext->thread_count = 4;
                m_codecContext->thread_type = FF_THREAD_FRAME;
                if (avcodec_open2(m_codecContext, codec, nullptr) >= 0) {
                    m_width = m_codecContext->width;
                    m_height = m_codecContext->height;
                    AVRational streamFps = m_formatContext->streams[m_videoStreamIndex]->avg_frame_rate;
                    if (streamFps.den > 0 && streamFps.num > 0) {
                        m_fps = av_q2d(streamFps);
                    } else {
                        m_fps = 30.0;
                    }
                }
            }
        }
    }

    // Setup Audio Decoder & Resampler
    if (m_audioStreamIndex >= 0) {
        AVCodecParameters* audioParams = m_formatContext->streams[m_audioStreamIndex]->codecpar;
        const AVCodec* audioCodec = avcodec_find_decoder(audioParams->codec_id);
        if (audioCodec) {
            m_audioCodecContext = avcodec_alloc_context3(audioCodec);
            if (m_audioCodecContext) {
                avcodec_parameters_to_context(m_audioCodecContext, audioParams);
                if (avcodec_open2(m_audioCodecContext, audioCodec, nullptr) >= 0) {
                    // Setup SwrContext for 48kHz Stereo IEEE Float
                    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
                    swr_alloc_set_opts2(
                        &m_swrContext,
                        &outLayout,
                        AV_SAMPLE_FMT_FLT,
                        48000,
                        &m_audioCodecContext->ch_layout,
                        m_audioCodecContext->sample_fmt,
                        m_audioCodecContext->sample_rate,
                        0, nullptr
                    );
                    if (m_swrContext) {
                        swr_init(m_swrContext);
                    }
                }
            }
        }
    }

    if (m_formatContext->duration != AV_NOPTS_VALUE) {
        m_durationSeconds = static_cast<double>(m_formatContext->duration) / AV_TIME_BASE;
    } else {
        m_durationSeconds = 0.0;
    }
    m_currentPositionSeconds = 0.0;

    m_isOpen = true;
    LOG_INFO("FFmpeg: Successfully opened '{}' (Video: {}, Audio: {})", 
             filePath, m_videoStreamIndex >= 0 ? "Yes" : "No", m_audioStreamIndex >= 0 ? "Yes" : "No");
    return true;
}

bool FFmpegDecoder::decodeNextFrame(Frame& outFrame) {
    if (!m_isOpen || !m_formatContext || !m_codecContext) return false;

    while (m_isOpen) {
        // 1. Try receiving decoded video frame
        int ret = avcodec_receive_frame(m_codecContext, m_avFrame);
        if (ret == 0) {
            int frameW = m_avFrame->width > 0 ? m_avFrame->width : m_width;
            int frameH = m_avFrame->height > 0 ? m_avFrame->height : m_height;
            m_width = frameW;
            m_height = frameH;

            AVStream* stream = m_formatContext->streams[m_videoStreamIndex];
            if (m_avFrame->pts != AV_NOPTS_VALUE) {
                m_currentPositionSeconds = m_avFrame->pts * av_q2d(stream->time_base);
            } else if (m_avFrame->pkt_dts != AV_NOPTS_VALUE) {
                m_currentPositionSeconds = m_avFrame->pkt_dts * av_q2d(stream->time_base);
            }

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

            outFrame.setPts(m_currentPositionSeconds);
            return true;
        }

        // 2. Feed video packets from queue
        AVPacket* pktToFeed = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (!m_videoPacketQueue.empty()) {
                pktToFeed = m_videoPacketQueue.front();
                m_videoPacketQueue.pop_front();
            }
        }

        if (pktToFeed) {
            avcodec_send_packet(m_codecContext, pktToFeed);
            av_packet_free(&pktToFeed);
        } else {
            readPackets(30);
            
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_videoPacketQueue.empty()) {
                // EOF reached
                avcodec_send_packet(m_codecContext, nullptr);
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
                    sws_scale(m_swsContext, m_avFrame->data, m_avFrame->linesize, 0, frameH, dstData, dstLinesize);
                    return true;
                }
                return false;
            }
        }
    }

    return false;
}

bool FFmpegDecoder::decodeAudioSamples(std::vector<float>& outPcmBuffer) {
    if (!m_isOpen || !m_audioCodecContext || !m_swrContext || m_audioStreamIndex < 0) return false;

    readPackets(30);

    bool decodedAny = false;

    while (m_isOpen) {
        AVPacket* pktToFeed = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (!m_audioPacketQueue.empty()) {
                pktToFeed = m_audioPacketQueue.front();
                m_audioPacketQueue.pop_front();
            }
        }

        if (pktToFeed) {
            avcodec_send_packet(m_audioCodecContext, pktToFeed);
            av_packet_free(&pktToFeed);
        } else {
            break;
        }

        while (m_isOpen) {
            int ret = avcodec_receive_frame(m_audioCodecContext, m_audioFrame);
            if (ret == 0) {
                int outSamples = swr_get_out_samples(m_swrContext, m_audioFrame->nb_samples);
                if (outSamples > 0) {
                    std::vector<float> resampled(outSamples * 2);
                    uint8_t* outData[1] = { reinterpret_cast<uint8_t*>(resampled.data()) };

                    int converted = swr_convert(
                        m_swrContext,
                        outData,
                        outSamples,
                        (const uint8_t**)m_audioFrame->data,
                        m_audioFrame->nb_samples
                    );

                    if (converted > 0) {
                        outPcmBuffer.insert(outPcmBuffer.end(), resampled.begin(), resampled.begin() + (converted * 2));
                        decodedAny = true;
                    }
                }
            } else {
                break;
            }
        }
    }

    return decodedAny;
}

bool FFmpegDecoder::seekToBeginning() {
    return seekToSeconds(0.0);
}

bool FFmpegDecoder::seekToSeconds(double seconds) {
    if (!m_isOpen || !m_formatContext) return false;

    clearPacketQueues();

    if (m_videoStreamIndex >= 0) {
        AVStream* stream = m_formatContext->streams[m_videoStreamIndex];
        int64_t targetTimestamp = static_cast<int64_t>(seconds / av_q2d(stream->time_base));
        av_seek_frame(m_formatContext, m_videoStreamIndex, targetTimestamp, AVSEEK_FLAG_BACKWARD);
    } else if (m_audioStreamIndex >= 0) {
        AVStream* stream = m_formatContext->streams[m_audioStreamIndex];
        int64_t targetTimestamp = static_cast<int64_t>(seconds / av_q2d(stream->time_base));
        av_seek_frame(m_formatContext, m_audioStreamIndex, targetTimestamp, AVSEEK_FLAG_BACKWARD);
    }

    if (m_codecContext) avcodec_flush_buffers(m_codecContext);
    if (m_audioCodecContext) avcodec_flush_buffers(m_audioCodecContext);

    m_currentPositionSeconds = seconds;
    return true;
}

void FFmpegDecoder::close() {
    m_isOpen = false;
    clearPacketQueues();

    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    if (m_swrContext) {
        swr_free(&m_swrContext);
    }
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }
    if (m_audioCodecContext) {
        avcodec_free_context(&m_audioCodecContext);
    }
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
    }
    m_width = 0;
    m_height = 0;
    m_durationSeconds = 0.0;
    m_currentPositionSeconds = 0.0;
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
}
