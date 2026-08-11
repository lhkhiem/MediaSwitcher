#include "FFmpegDecoder.h"
#include "engine/diagnostics/MediaDiagnostics.h"
#include "common/logger/Logger.h"
#include <algorithm>

FFmpegDecoder::FFmpegDecoder() {
    m_avFrame   = av_frame_alloc();
    m_audioFrame = av_frame_alloc();
    m_rgbaFrame  = av_frame_alloc();
    m_avPacket  = av_packet_alloc();
}

FFmpegDecoder::~FFmpegDecoder() {
    close();
    if (m_avFrame)    av_frame_free(&m_avFrame);
    if (m_audioFrame) av_frame_free(&m_audioFrame);
    if (m_rgbaFrame)  av_frame_free(&m_rgbaFrame);
    if (m_avPacket)   av_packet_free(&m_avPacket);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void FFmpegDecoder::clearPacketQueues() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto pkt : m_videoPacketQueue) { if (pkt) av_packet_free(&pkt); }
    m_videoPacketQueue.clear();
    m_videoPacketBytes = 0;
    for (auto pkt : m_audioPacketQueue) { if (pkt) av_packet_free(&pkt); }
    m_audioPacketQueue.clear();
}

void FFmpegDecoder::readPackets(size_t maxCount) {
    if (!m_isOpen || !m_formatContext || m_atFormatEof) return;

    size_t readCount = 0;
    int vPktsRead = 0;
    int aPktsRead = 0;

    while (m_isOpen && readCount < maxCount) {
        // Keep queues bounded, but do not let a full video queue starve audio.
        // Many containers interleave several video packets before the next audio packet;
        // stopping on the soft video limit alone causes repeated XAudio underruns.
        bool videoSoftFull = false;
        bool audioFull = false;
        bool videoHardFull = false;
        bool audioNeedsFill = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            // Preserve normal low-latency count throttling, but when audio is low
            // permit demux to cross dense H.264 video runs up to a fixed 128MB cap.
            const size_t videoHardLimit = MAX_VIDEO_PACKET_BYTES;
            const size_t audioLowWatermark = (std::min)(m_maxAudioQueueSize, size_t{128});

            videoSoftFull = (m_videoStreamIndex >= 0 && m_videoPacketQueue.size() >= m_maxVideoQueueSize);
            videoHardFull = (m_videoStreamIndex >= 0 && m_videoPacketBytes >= videoHardLimit);
            audioFull = (m_audioStreamIndex >= 0 && m_audioPacketQueue.size() >= m_maxAudioQueueSize);
            audioNeedsFill = (m_audioStreamIndex >= 0 && m_audioPacketQueue.size() < audioLowWatermark);

            if (audioFull || videoHardFull || (videoSoftFull && !audioNeedsFill)) {
                MediaDiagnostics::instance().recordDemuxRead(videoSoftFull, audioFull, vPktsRead, aPktsRead);
                break;
            }
        }

        int ret = av_read_frame(m_formatContext, m_avPacket);
        if (ret >= 0) {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_avPacket->stream_index == m_videoStreamIndex) {
                if (AVPacket* packet = av_packet_clone(m_avPacket)) {
                    m_videoPacketBytes += static_cast<size_t>((std::max)(packet->size, 0));
                    m_videoPacketQueue.push_back(packet);
                    ++vPktsRead;
                }
            } else if (m_avPacket->stream_index == m_audioStreamIndex) {
                m_audioPacketQueue.push_back(av_packet_clone(m_avPacket));
                ++aPktsRead;
            }
            av_packet_unref(m_avPacket);
            ++readCount;
        } else {
            m_atFormatEof = true;   // Format context reached true EOF
            break;
        }
    }

    if (readCount > 0) {
        MediaDiagnostics::instance().recordDemuxRead(false, false, vPktsRead, aPktsRead);
    }
}

void FFmpegDecoder::setQueueLimits(size_t maxVideoPackets, size_t maxAudioPackets) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_maxVideoQueueSize = maxVideoPackets;
    m_maxAudioQueueSize = maxAudioPackets;
}

// Helper: resample one audio frame and append PCM to outPcmBuffer
static bool resampleFrame(SwrContext* swr, AVFrame* frame,
                           std::vector<float>& outPcmBuffer)
{
    int outSamples = swr_get_out_samples(swr, frame->nb_samples);
    if (outSamples <= 0) return false;

    std::vector<float> resampled(static_cast<size_t>(outSamples) * 2);
    uint8_t* outData[1] = { reinterpret_cast<uint8_t*>(resampled.data()) };

    int converted = swr_convert(swr, outData, outSamples,
                                (const uint8_t**)frame->data, frame->nb_samples);
    if (converted > 0) {
        outPcmBuffer.insert(outPcmBuffer.end(),
                            resampled.begin(),
                            resampled.begin() + static_cast<ptrdiff_t>(converted) * 2);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// open / close
// ---------------------------------------------------------------------------

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
    for (unsigned i = 0; i < m_formatContext->nb_streams; ++i) {
        auto type = m_formatContext->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex == -1)
            m_videoStreamIndex = static_cast<int>(i);
        else if (type == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex == -1)
            m_audioStreamIndex = static_cast<int>(i);
    }
    if (m_videoStreamIndex == -1 && m_audioStreamIndex == -1) {
        LOG_ERROR("FFmpeg: No a/v stream found in '{}'", filePath);
        close();
        return false;
    }

    // Video decoder
    if (m_videoStreamIndex >= 0) {
        auto* cp    = m_formatContext->streams[m_videoStreamIndex]->codecpar;
        auto* codec = avcodec_find_decoder(cp->codec_id);
        if (codec) {
            m_codecContext = avcodec_alloc_context3(codec);
            if (m_codecContext) {
                avcodec_parameters_to_context(m_codecContext, cp);
                m_codecContext->thread_count = 4;
                m_codecContext->thread_type  = FF_THREAD_FRAME;
                if (avcodec_open2(m_codecContext, codec, nullptr) >= 0) {
                    m_width  = m_codecContext->width;
                    m_height = m_codecContext->height;
                    AVRational fps = m_formatContext->streams[m_videoStreamIndex]->avg_frame_rate;
                    m_fps = (fps.den > 0 && fps.num > 0) ? av_q2d(fps) : 30.0;
                }
            }
        }
    }

    // Audio decoder + resampler
    if (m_audioStreamIndex >= 0) {
        auto* ap    = m_formatContext->streams[m_audioStreamIndex]->codecpar;
        auto* codec = avcodec_find_decoder(ap->codec_id);
        if (codec) {
            m_audioCodecContext = avcodec_alloc_context3(codec);
            if (m_audioCodecContext) {
                avcodec_parameters_to_context(m_audioCodecContext, ap);
                if (avcodec_open2(m_audioCodecContext, codec, nullptr) >= 0) {
                    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
                    AVChannelLayout inLayout;
                    if (m_audioCodecContext->ch_layout.nb_channels > 0) {
                        av_channel_layout_copy(&inLayout, &m_audioCodecContext->ch_layout);
                    } else {
                        av_channel_layout_default(&inLayout, m_audioCodecContext->ch_layout.nb_channels > 0 ? m_audioCodecContext->ch_layout.nb_channels : 2);
                    }

                    swr_alloc_set_opts2(&m_swrContext,
                                       &outLayout, AV_SAMPLE_FMT_FLT, 48000,
                                       &inLayout,
                                       m_audioCodecContext->sample_fmt,
                                       m_audioCodecContext->sample_rate,
                                       0, nullptr);
                    av_channel_layout_uninit(&inLayout);
                    if (m_swrContext) swr_init(m_swrContext);
                }
            }
        }
    }

    m_durationSeconds = (m_formatContext->duration != AV_NOPTS_VALUE)
        ? static_cast<double>(m_formatContext->duration) / AV_TIME_BASE
        : 0.0;
    m_currentPositionSeconds = 0.0;
    m_atFormatEof = false;

    m_isOpen = true;
    LOG_INFO("FFmpeg: Opened '{}' (video:{} audio:{})", filePath,
             m_videoStreamIndex >= 0 ? "yes" : "no",
             m_audioStreamIndex >= 0 ? "yes" : "no");
    return true;
}

void FFmpegDecoder::close() {
    m_isOpen = false;
    m_atFormatEof = false;
    clearPacketQueues();

    if (m_swsContext)       { sws_freeContext(m_swsContext); m_swsContext = nullptr; }
    if (m_swrContext)       { swr_free(&m_swrContext); }
    if (m_codecContext)     { avcodec_free_context(&m_codecContext); }
    if (m_audioCodecContext){ avcodec_free_context(&m_audioCodecContext); }
    if (m_formatContext)    { avformat_close_input(&m_formatContext); }

    m_width = m_height = 0;
    m_durationSeconds = m_currentPositionSeconds = 0.0;
    m_videoStreamIndex = m_audioStreamIndex = -1;
}

// ---------------------------------------------------------------------------
// Video decoding
// ---------------------------------------------------------------------------

bool FFmpegDecoder::decodeNextFrame(Frame& outFrame) {
    if (!m_isOpen || !m_formatContext || !m_codecContext) return false;

    while (m_isOpen) {
        // Try to receive a decoded video frame
        int ret = avcodec_receive_frame(m_codecContext, m_avFrame);
        if (ret == 0) {
            int w = m_avFrame->width  > 0 ? m_avFrame->width  : m_width;
            int h = m_avFrame->height > 0 ? m_avFrame->height : m_height;
            m_width = w; m_height = h;

            auto* stream = m_formatContext->streams[m_videoStreamIndex];
            if (m_avFrame->pts != AV_NOPTS_VALUE)
                m_currentPositionSeconds = m_avFrame->pts * av_q2d(stream->time_base);
            else if (m_avFrame->pkt_dts != AV_NOPTS_VALUE)
                m_currentPositionSeconds = m_avFrame->pkt_dts * av_q2d(stream->time_base);

            auto pixFmt = static_cast<AVPixelFormat>(m_avFrame->format);
            m_swsContext = sws_getCachedContext(m_swsContext,
                w, h, pixFmt, w, h, AV_PIX_FMT_RGBA,
                SWS_BICUBIC | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
            if (!m_swsContext) return false;

            if (outFrame.width() != w || outFrame.height() != h)
                outFrame.resize(w, h, PixelFormat::RGBA32);

            uint8_t* dst[4]  = { outFrame.data(), nullptr, nullptr, nullptr };
            int      dl[4]   = { outFrame.stride(), 0, 0, 0 };
            sws_scale(m_swsContext, m_avFrame->data, m_avFrame->linesize, 0, h, dst, dl);
            outFrame.setPts(m_currentPositionSeconds);
            return true;
        }

        // Feed the next video packet
        AVPacket* pkt = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (!m_videoPacketQueue.empty()) {
                pkt = m_videoPacketQueue.front();
                m_videoPacketQueue.pop_front();
                m_videoPacketBytes -= (std::min)(m_videoPacketBytes, static_cast<size_t>((std::max)(pkt->size, 0)));
            }
        }
        if (pkt) {
            avcodec_send_packet(m_codecContext, pkt);
            av_packet_free(&pkt);
        } else {
            readPackets(30);
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_videoPacketQueue.empty()) {
                if (!m_atFormatEof) {
                    // Video queue temporarily empty - NOT EOF.
                    // This can happen when readPackets hit the video queue limit
                    // but there are still more packets in the file.
                    // Return false; caller (FileSource::decodeWorkerLoop) will check
                    // atFormatEof() and retry instead of triggering a loop/seek.
                    return false;
                }
                // True video EOF — flush remaining frames from video codec
                avcodec_send_packet(m_codecContext, nullptr);
                ret = avcodec_receive_frame(m_codecContext, m_avFrame);
                if (ret == 0) {
                    int w = m_avFrame->width  > 0 ? m_avFrame->width  : m_width;
                    int h = m_avFrame->height > 0 ? m_avFrame->height : m_height;
                    m_width = w; m_height = h;

                    auto pixFmt = static_cast<AVPixelFormat>(m_avFrame->format);
                    m_swsContext = sws_getCachedContext(m_swsContext,
                        w, h, pixFmt, w, h, AV_PIX_FMT_RGBA,
                        SWS_BICUBIC | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
                    if (!m_swsContext) return false;
                    if (outFrame.width() != w || outFrame.height() != h)
                        outFrame.resize(w, h, PixelFormat::RGBA32);
                    uint8_t* dst[4] = { outFrame.data(), nullptr, nullptr, nullptr };
                    int      dl[4]  = { outFrame.stride(), 0, 0, 0 };
                    sws_scale(m_swsContext, m_avFrame->data, m_avFrame->linesize, 0, h, dst, dl);
                    return true;
                }
                return false;   // Video truly done
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Audio decoding — normal path
// ---------------------------------------------------------------------------

bool FFmpegDecoder::decodeAudioSamples(std::vector<float>& outPcmBuffer) {
    if (!m_isOpen || !m_audioCodecContext || !m_swrContext || m_audioStreamIndex < 0) return false;

    readPackets(120);

    bool decodedAny = false;

    // Limit per-call output to 0.5s to keep m_audioBuffer stable
    while (m_isOpen && outPcmBuffer.size() < 24000) {
        AVPacket* pkt = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (!m_audioPacketQueue.empty()) {
                pkt = m_audioPacketQueue.front();
                m_audioPacketQueue.pop_front();
            }
        }
        if (!pkt) break;    // Queue empty — normal, more will arrive later

        avcodec_send_packet(m_audioCodecContext, pkt);
        av_packet_free(&pkt);

        while (m_isOpen) {
            int ret = avcodec_receive_frame(m_audioCodecContext, m_audioFrame);
            if (ret != 0) break;
            if (resampleFrame(m_swrContext, m_audioFrame, outPcmBuffer))
                decodedAny = true;
        }
    }

    return decodedAny;
}

// ---------------------------------------------------------------------------
// Audio decoder flush — MUST be called before every seek to recover samples
// that the codec holds internally (decoder delay, AAC/MP3 priming frames etc.)
// ---------------------------------------------------------------------------

bool FFmpegDecoder::drainAudio(std::vector<float>& outPcmBuffer) {
    if (!m_audioCodecContext || !m_swrContext || m_audioStreamIndex < 0) return false;

    // 1. First, decode any remaining packets already in the queue
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_audioPacketQueue.empty()) {
            AVPacket* pkt = m_audioPacketQueue.front();
            m_audioPacketQueue.pop_front();
            avcodec_send_packet(m_audioCodecContext, pkt);
            av_packet_free(&pkt);

            while (true) {
                int ret = avcodec_receive_frame(m_audioCodecContext, m_audioFrame);
                if (ret != 0) break;
                resampleFrame(m_swrContext, m_audioFrame, outPcmBuffer);
            }
        }
    }

    // 2. Send flush (nullptr) packet to drain codec internal delay buffers
    avcodec_send_packet(m_audioCodecContext, nullptr);

    bool got = false;
    while (true) {
        int ret = avcodec_receive_frame(m_audioCodecContext, m_audioFrame);
        if (ret != 0) break;
        if (resampleFrame(m_swrContext, m_audioFrame, outPcmBuffer))
            got = true;
    }

    // 3. Flush any resampler internal latency
    while (true) {
        int remaining = swr_get_out_samples(m_swrContext, 0);
        if (remaining <= 0) break;
        std::vector<float> tail(static_cast<size_t>(remaining) * 2);
        uint8_t* outData[1] = { reinterpret_cast<uint8_t*>(tail.data()) };
        int converted = swr_convert(m_swrContext, outData, remaining, nullptr, 0);
        if (converted <= 0) break;
        outPcmBuffer.insert(outPcmBuffer.end(), tail.begin(),
                            tail.begin() + static_cast<ptrdiff_t>(converted) * 2);
        got = true;
    }

    return got;
}

// ---------------------------------------------------------------------------
// Seeking
// ---------------------------------------------------------------------------

bool FFmpegDecoder::seekToBeginning() {
    return seekToSeconds(0.0);
}

bool FFmpegDecoder::seekToSeconds(double seconds) {
    if (!m_isOpen || !m_formatContext) return false;

    clearPacketQueues();

    if (m_videoStreamIndex >= 0) {
        auto* stream = m_formatContext->streams[m_videoStreamIndex];
        int64_t ts = static_cast<int64_t>(seconds / av_q2d(stream->time_base));
        av_seek_frame(m_formatContext, m_videoStreamIndex, ts, AVSEEK_FLAG_BACKWARD);
    } else if (m_audioStreamIndex >= 0) {
        auto* stream = m_formatContext->streams[m_audioStreamIndex];
        int64_t ts = static_cast<int64_t>(seconds / av_q2d(stream->time_base));
        av_seek_frame(m_formatContext, m_audioStreamIndex, ts, AVSEEK_FLAG_BACKWARD);
    }

    if (m_codecContext)      avcodec_flush_buffers(m_codecContext);
    if (m_audioCodecContext) avcodec_flush_buffers(m_audioCodecContext);

    m_currentPositionSeconds = seconds;
    m_atFormatEof = false;
    return true;
}
