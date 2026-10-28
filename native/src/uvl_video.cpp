#include "uvl_video.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#ifdef __ANDROID__
#include <libavcodec/jni.h>
#endif
}

struct UvlPlayer {
    std::string url;
    UvlOptions opts{};
    std::thread worker;
    std::atomic<bool> stop{false};
    std::atomic<bool> timed_out{false};
    std::atomic<int> state{UVL_STATE_IDLE};
    std::atomic<int64_t> last_io_us{0};

    std::mutex mtx;                 // protege frame/info/err
    std::vector<uint8_t> frame, back;
    int64_t pts_us = 0;
    uint64_t frame_counter = 0, consumed_counter = 0;
    UvlStreamInfo info{};
    AVRational tb{1, 1000000};
    std::string err;

    std::atomic<uint64_t> packets{0}, decoded{0}, dropped{0}, bytes{0}, reconnects{0};
};

static bool starts_with(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
static bool ends_with(const std::string& s, const char* e) {
    size_t n = std::strlen(e);
    return s.size() >= n && s.compare(s.size() - n, n, e) == 0;
}

static void set_err(UvlPlayer* p, const char* what, int ret) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    if (ret < 0) av_strerror(ret, buf, sizeof buf);
    std::lock_guard<std::mutex> g(p->mtx);
    p->err = std::string(what) + (ret < 0 ? std::string(": ") + buf : "");
}

static int interrupt_cb(void* o) {
    auto* p = static_cast<UvlPlayer*>(o);
    if (p->stop.load()) return 1;
    int t = p->opts.timeout_ms;
    if (t > 0 && av_gettime_relative() - p->last_io_us.load() > (int64_t)t * 1000) {
        p->timed_out = true;
        return 1;
    }
    return 0;
}

static const AVCodec* pick_decoder(const UvlPlayer* p, const AVCodecParameters* par) {
#ifdef __ANDROID__
    if (p->opts.hw_decode) {
        const char* n = nullptr;
        switch (par->codec_id) {
            case AV_CODEC_ID_H264:       n = "h264_mediacodec";  break;
            case AV_CODEC_ID_HEVC:       n = "hevc_mediacodec";  break;
            case AV_CODEC_ID_MPEG4:      n = "mpeg4_mediacodec"; break;
            case AV_CODEC_ID_MPEG2VIDEO: n = "mpeg2_mediacodec"; break;
            default: break;
        }
        if (n) if (const AVCodec* c = avcodec_find_decoder_by_name(n)) return c;
    }
#else
    (void)p;
#endif
    return avcodec_find_decoder(par->codec_id);   // software (fallback)
}

static void publish(UvlPlayer* p, AVFrame* f, SwsContext** sws) {
    const int w = f->width, h = f->height;
    *sws = sws_getCachedContext(*sws, w, h, (AVPixelFormat)f->format, w, h,
                                AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!*sws) return;
    const size_t need = (size_t)w * h * 4;
    if (p->back.size() != need) p->back.resize(need);

    const int stride = w * 4;
    uint8_t* dst[4] = {nullptr};
    int dstride[4] = {0};
    if (p->opts.flip_vertical) { dst[0] = p->back.data() + (size_t)(h - 1) * stride; dstride[0] = -stride; }
    else                       { dst[0] = p->back.data();                            dstride[0] = stride;  }
    sws_scale(*sws, f->data, f->linesize, 0, h, dst, dstride);

    int64_t ts = f->best_effort_timestamp;
    int64_t pts = ts == AV_NOPTS_VALUE ? av_gettime_relative() : av_rescale_q(ts, p->tb, AVRational{1, 1000000});

    std::lock_guard<std::mutex> g(p->mtx);
    if (p->frame_counter != p->consumed_counter) p->dropped++;   // el consumidor no llego a tiempo
    p->frame.swap(p->back);
    p->info.width = w; p->info.height = h;
    p->pts_us = pts;
    p->frame_counter++;
    p->decoded++;
}

// Una sesion completa: conectar -> decodificar hasta error/stop. Devuelve <0 si hay error.
static int run_session(UvlPlayer* p) {
    p->timed_out = false;
    p->last_io_us = av_gettime_relative();

    AVFormatContext* fmt = avformat_alloc_context();
    if (!fmt) return AVERROR(ENOMEM);
    fmt->interrupt_callback.callback = interrupt_cb;
    fmt->interrupt_callback.opaque = p;

    const bool rtsp = starts_with(p->url, "rtsp");
    const bool udp  = starts_with(p->url, "udp:") || starts_with(p->url, "rtp:");
    const bool sdp  = ends_with(p->url, ".sdp");   // RTP "puro" (H.264/HEVC/MPEG-4 sobre RTP) descrito por SDP

    AVDictionary* o = nullptr;
    if (p->opts.low_latency) {
        av_dict_set(&o, "fflags", "nobuffer", 0);
        av_dict_set(&o, "flags", "low_delay", 0);
        av_dict_set(&o, "probesize", "500000", 0);
        av_dict_set(&o, "analyzeduration", "500000", 0);
        av_dict_set(&o, "max_delay", "0", 0);
    } else {
        av_dict_set(&o, "analyzeduration", "2000000", 0);
    }
    if (p->opts.timeout_ms > 0) av_dict_set_int(&o, "timeout", (int64_t)p->opts.timeout_ms * 1000, 0);
    if (rtsp) {
        if (p->opts.rtsp_transport == UVL_RTSP_TCP) av_dict_set(&o, "rtsp_transport", "tcp", 0);
        if (p->opts.rtsp_transport == UVL_RTSP_UDP) av_dict_set(&o, "rtsp_transport", "udp", 0);
        av_dict_set(&o, "allowed_media_types", "video", 0);
    }
    if (sdp) {
        av_dict_set(&o, "protocol_whitelist", "file,udp,rtp,crypto,data", 0);
        av_dict_set(&o, "allowed_media_types", "video", 0);
        if (p->opts.recv_buffer_bytes > 0) av_dict_set_int(&o, "buffer_size", p->opts.recv_buffer_bytes, 0);
    }
    if (udp) {
        av_dict_set(&o, "reuse", "1", 0);               // varios receptores en el mismo puerto/grupo
        av_dict_set(&o, "overrun_nonfatal", "1", 0);
        av_dict_set(&o, "fifo_size", "1000000", 0);
        if (p->opts.recv_buffer_bytes > 0) av_dict_set_int(&o, "buffer_size", p->opts.recv_buffer_bytes, 0);
    }

    int ret = avformat_open_input(&fmt, p->url.c_str(), nullptr, &o);   // libera fmt si falla
    av_dict_free(&o);
    if (ret < 0) { set_err(p, p->timed_out ? "timeout abriendo" : "avformat_open_input", ret); return ret; }

    AVCodecContext* ctx = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    SwsContext* sws = nullptr;

    do {
        p->last_io_us = av_gettime_relative();
        if ((ret = avformat_find_stream_info(fmt, nullptr)) < 0) { set_err(p, "find_stream_info", ret); break; }

        int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (vi < 0) { ret = vi; set_err(p, "no hay stream de video", ret); break; }
        for (unsigned i = 0; i < fmt->nb_streams; i++)
            if ((int)i != vi) fmt->streams[i]->discard = AVDISCARD_ALL;

        AVStream* st = fmt->streams[vi];
        const AVCodec* dec = pick_decoder(p, st->codecpar);
        if (!dec) { ret = AVERROR_DECODER_NOT_FOUND; set_err(p, "decoder no disponible", ret); break; }
        ctx = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(ctx, st->codecpar);
        ctx->pkt_timebase = st->time_base;
        if (p->opts.low_latency) { ctx->flags |= AV_CODEC_FLAG_LOW_DELAY; ctx->flags2 |= AV_CODEC_FLAG2_FAST; }
        ctx->thread_count = 0;
        ctx->thread_type = p->opts.low_latency ? FF_THREAD_SLICE : (FF_THREAD_FRAME | FF_THREAD_SLICE);
        if ((ret = avcodec_open2(ctx, dec, nullptr)) < 0) { set_err(p, "avcodec_open2", ret); break; }

        {
            std::lock_guard<std::mutex> g(p->mtx);
            p->tb = st->time_base;
            p->info.codec_id = (int)st->codecpar->codec_id;
            p->info.width = ctx->width; p->info.height = ctx->height;
            AVRational r = av_guess_frame_rate(fmt, st, nullptr);
            p->info.fps = r.den ? av_q2d(r) : 0.0;
            p->err.clear();
        }
        p->state = UVL_STATE_PLAYING;

        while (!p->stop) {
            ret = av_read_frame(fmt, pkt);
            if (ret < 0) { if (p->timed_out) set_err(p, "timeout: sin datos", ret); else if (!p->stop) set_err(p, "read_frame", ret); break; }
            p->last_io_us = av_gettime_relative();
            if (pkt->stream_index != vi) { av_packet_unref(pkt); continue; }
            p->packets++; p->bytes += pkt->size;

            int s = avcodec_send_packet(ctx, pkt);
            av_packet_unref(pkt);
            if (s < 0 && s != AVERROR(EAGAIN)) continue;     // paquete corrupto: se tolera (UDP pierde datos)
            while (avcodec_receive_frame(ctx, fr) >= 0) { publish(p, fr, &sws); av_frame_unref(fr); }
        }
    } while (false);

    if (sws) sws_freeContext(sws);
    av_frame_free(&fr);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return p->stop ? 0 : (ret < 0 ? ret : AVERROR_EOF);
}

static void worker_main(UvlPlayer* p) {
    int backoff_ms = 500;
    bool first = true;
    while (!p->stop) {
        p->state = first ? UVL_STATE_CONNECTING : UVL_STATE_RECONNECTING;
        first = false;
        uint64_t before = p->decoded.load();
        run_session(p);
        if (p->stop) break;
        if (!p->opts.auto_reconnect) { p->state = UVL_STATE_ERROR; return; }
        if (p->decoded.load() != before) backoff_ms = 500;       // la sesion llego a funcionar
        p->reconnects++;
        p->state = UVL_STATE_RECONNECTING;
        for (int waited = 0; waited < backoff_ms && !p->stop; waited += 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        backoff_ms = backoff_ms * 2 > 5000 ? 5000 : backoff_ms * 2;
    }
    p->state = UVL_STATE_STOPPED;
}

// ----------------------------------------------------------------- API C
extern "C" {

void uvl_options_default(UvlOptions* o) {
    if (!o) return;
    o->rtsp_transport = UVL_RTSP_TCP;
    o->low_latency = 1;
    o->hw_decode = 1;
    o->timeout_ms = 5000;
    o->auto_reconnect = 1;
    o->recv_buffer_bytes = 4 * 1024 * 1024;
    o->flip_vertical = 1;
}

UvlPlayer* uvl_create(void) {
    static std::once_flag once;
    std::call_once(once, [] { avformat_network_init(); av_log_set_level(AV_LOG_WARNING); });
    return new UvlPlayer();
}

void uvl_destroy(UvlPlayer* p) { if (!p) return; uvl_close(p); delete p; }

int32_t uvl_open(UvlPlayer* p, const char* url, const UvlOptions* opts) {
    if (!p || !url) return -1;
    uvl_close(p);
    UvlOptions d; uvl_options_default(&d);
    p->opts = opts ? *opts : d;
    p->url = url;
    p->stop = false;
    p->packets = p->decoded = p->dropped = p->bytes = p->reconnects = 0;
    {
        std::lock_guard<std::mutex> g(p->mtx);
        p->frame_counter = p->consumed_counter = 0;
        p->info = UvlStreamInfo{}; p->err.clear();
    }
    p->worker = std::thread(worker_main, p);
    return 0;
}

void uvl_close(UvlPlayer* p) {
    if (!p) return;
    p->stop = true;
    if (p->worker.joinable()) p->worker.join();
    p->state = UVL_STATE_IDLE;
}

int32_t uvl_get_state(UvlPlayer* p) { return p ? p->state.load() : UVL_STATE_IDLE; }

int32_t uvl_get_info(UvlPlayer* p, UvlStreamInfo* out) {
    if (!p || !out) return -1;
    std::lock_guard<std::mutex> g(p->mtx);
    *out = p->info;
    return 0;
}

void uvl_get_stats(UvlPlayer* p, UvlStats* out) {
    if (!p || !out) return;
    out->packets = p->packets; out->frames_decoded = p->decoded; out->frames_dropped = p->dropped;
    out->bytes = p->bytes; out->reconnects = p->reconnects;
}

int32_t uvl_get_last_error(UvlPlayer* p, char* buf, int32_t cap) {
    if (!p || !buf || cap <= 0) return 0;
    std::lock_guard<std::mutex> g(p->mtx);
    std::strncpy(buf, p->err.c_str(), (size_t)cap - 1);
    buf[cap - 1] = 0;
    return (int32_t)std::strlen(buf);
}

int32_t uvl_try_get_frame(UvlPlayer* p, uint8_t* dst, int32_t cap, int64_t* pts_us, uint64_t* counter) {
    if (!p || !dst) return -1;
    std::lock_guard<std::mutex> g(p->mtx);
    if (p->frame_counter == p->consumed_counter) return 0;
    if ((size_t)cap < p->frame.size()) return -2;
    std::memcpy(dst, p->frame.data(), p->frame.size());
    p->consumed_counter = p->frame_counter;
    if (pts_us) *pts_us = p->pts_us;
    if (counter) *counter = p->frame_counter;
    return 1;
}

const char* uvl_version(void) { return "UnityLive 0.1 / " LIBAVFORMAT_IDENT; }

void uvl_set_jvm(void* vm) {
#ifdef __ANDROID__
    av_jni_set_java_vm(vm, nullptr);
#else
    (void)vm;
#endif
}

} // extern "C"
