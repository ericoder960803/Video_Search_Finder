#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <algorithm>
#include <thread>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include "types.hpp"
#include "readerwriterqueue.h"
#include "cache.hpp"

class VideoDecoder {
private:
    std::shared_ptr<VideoCache> cache;

    uint64_t get_canonical_hash(const std::vector<uint8_t>& pixels_8x8) {
        uint64_t avg = 0;
        for(uint8_t p : pixels_8x8) avg += p;
        avg /= 64;

        std::vector<uint64_t> variants;
        variants.reserve(8);

        for (int v = 0; v < 8; v++) {
            uint64_t h = 0;
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    int srcX = x, srcY = y;
                    switch (v) {
                        case 1: srcX = 7 - x; break;
                        case 2: srcY = 7 - y; break;
                        case 3: srcX = 7 - x; srcY = 7 - y; break;
                        case 4: srcX = y; srcY = x; break;
                        case 5: srcX = y; srcY = 7 - x; break;
                        case 6: srcX = y; srcY = 7 - x; break;
                        case 7: srcX = 7 - y; srcY = 7 - x; break;
                    }
                    if (pixels_8x8[srcY * 8 + srcX] >= avg) {
                        h |= (1ULL << (63 - (y * 8 + x)));
                    }
                }
            }
            variants.push_back(h);
        }
        return *std::min_element(variants.begin(), variants.end());
    }

    uint64_t sample_at_percent(AVFormatContext* fmt_ctx, AVCodecContext* ctx, int v_idx, int percent, double duration_sec, std::string& time_str) {
        double target_time = (duration_sec * percent) / 100.0;
        int64_t target_ts = (int64_t)(target_time * AV_TIME_BASE);

        char buf[64];
        snprintf(buf, sizeof(buf), "%d%% (%.2fs)", percent, target_time);
        if (!time_str.empty()) time_str += ", ";
        time_str += buf;

        av_seek_frame(fmt_ctx, -1, target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(ctx); 

        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        SwsContext* sws_ctx = sws_getContext(
            ctx->width, ctx->height, ctx->pix_fmt,
            8, 8, AV_PIX_FMT_GRAY8, SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        uint64_t result_hash = 0;
        int max_frames_to_skip = 150; 
        int skipped = 0;

        while (av_read_frame(fmt_ctx, pkt) >= 0) {
            if (pkt->stream_index == v_idx) {
                if (avcodec_send_packet(ctx, pkt) >= 0) {
                    while (avcodec_receive_frame(ctx, frame) >= 0) {
                        double current_time = frame->pts * av_q2d(fmt_ctx->streams[v_idx]->time_base);
                        if (current_time >= target_time || skipped >= max_frames_to_skip) {
                            std::vector<uint8_t> pixels_8x8(64);
                            uint8_t* dst_data[1] = { pixels_8x8.data() };
                            int dst_linesize[1] = { 8 };
                            sws_scale(sws_ctx, frame->data, frame->linesize, 0, ctx->height, dst_data, dst_linesize);
                            result_hash = get_canonical_hash(pixels_8x8);
                            goto end; 
                        }
                        skipped++;
                    }
                }
            }
            av_packet_unref(pkt);
        }

    end:
        if (sws_ctx) sws_freeContext(sws_ctx);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        return result_hash;
    }

    ResultTask process_video_deep(const VideoTask& task) {
        ResultTask res;
        res.path = task.path;
        res.filesize_mb = task.size / (1024 * 1024);
        res.is_poison = false;

        AVFormatContext* fmt_ctx = nullptr;
        if (avformat_open_input(&fmt_ctx, task.path.c_str(), nullptr, nullptr) < 0) {
            res.is_poison = true;
            return res;
        }
        
        if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
            avformat_close_input(&fmt_ctx);
            res.is_poison = true;
            return res;
        }

        const AVCodec* decoder = nullptr;
        int v_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
        if (v_idx < 0) {
            avformat_close_input(&fmt_ctx);
            res.is_poison = true;
            return res;
        }

        AVCodecContext* ctx = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(ctx, fmt_ctx->streams[v_idx]->codecpar);
        ctx->thread_count = 0; 
        if (avcodec_open2(ctx, decoder, nullptr) < 0) {
            avcodec_free_context(&ctx);
            avformat_close_input(&fmt_ctx);
            res.is_poison = true;
            return res;
        }

        res.resolution = std::to_string(ctx->width) + "x" + std::to_string(ctx->height);
        res.duration_sec = (int)(fmt_ctx->duration / AV_TIME_BASE);
        double duration_double = (double)fmt_ctx->duration / AV_TIME_BASE;
        res.bitrate_kbps = (int)(fmt_ctx->bit_rate / 1000);

        std::vector<int> percents = {10, 25, 50, 75};
        std::string detail;
        res.fingerprint.clear();

        bool all_points_success = true;
        for (int p : percents) {
            uint64_t h = sample_at_percent(fmt_ctx, ctx, v_idx, p, duration_double, detail);
            if (h != 0) {
                res.fingerprint.push_back(h);
            } else {
                all_points_success = false;
                break; 
            }
        }

        // 🛡️ 嚴格判定：必須抓滿 4 個點才算有效影片
        if (!all_points_success || res.fingerprint.size() < 4) {
            res.is_poison = true;
            res.fingerprint.clear();
        } else {
            res.match_detail = detail;
        }

        if (ctx) avcodec_free_context(&ctx); 
        if (fmt_ctx) avformat_close_input(&fmt_ctx);

        return res;
    }

public:
    VideoDecoder(std::shared_ptr<VideoCache> c = nullptr) : cache(c) {}

    void run(moodycamel::ReaderWriterQueue<VideoTask>& task_queue, 
             moodycamel::ReaderWriterQueue<ResultTask>& res_queue,
             std::atomic<bool>& scan_done) 
    {
        VideoTask task;
        while (true) {
            if (task_queue.try_dequeue(task)) {
                ResultTask result;
                if (cache && cache->get(task, result)) {
                } else {
                    result = process_video_deep(task);
                    if (cache) cache->put(task, result);
                }
                
                if (!result.is_poison && !result.fingerprint.empty()) {
                    while (!res_queue.try_enqueue(result)) {
                        std::this_thread::yield();
                    }
                }
            } 
            else if (scan_done.load()) {
                break;
            } 
            else {
                std::this_thread::yield();
            }
        }
    }
};