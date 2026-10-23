// UnityLive - API C estable para recepcion y decodificacion de video en tiempo real.
#ifndef UVL_VIDEO_H
#define UVL_VIDEO_H
#include <stdint.h>

#if defined(_WIN32)
#  define UVL_API __declspec(dllexport)
#else
#  define UVL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UvlPlayer UvlPlayer;

enum UvlState {
    UVL_STATE_IDLE = 0,
    UVL_STATE_CONNECTING = 1,
    UVL_STATE_PLAYING = 2,
    UVL_STATE_RECONNECTING = 3,
    UVL_STATE_STOPPED = 4,
    UVL_STATE_ERROR = 5
};

enum UvlRtspTransport { UVL_RTSP_AUTO = 0, UVL_RTSP_TCP = 1, UVL_RTSP_UDP = 2 };

typedef struct UvlOptions {
    int32_t rtsp_transport;    // UvlRtspTransport
    int32_t low_latency;       // 1 = sin buffering, analisis corto
    int32_t hw_decode;         // 1 = intentar decodificador hardware (MediaCodec en Android)
    int32_t timeout_ms;        // sin datos durante N ms => error/reconexion (0 = sin limite)
    int32_t auto_reconnect;    // 1 = reconectar con backoff exponencial
    int32_t recv_buffer_bytes; // buffer de socket UDP/RTP (0 = defecto)
    int32_t flip_vertical;     // 1 = salida bottom-up (lo que espera Unity)
} UvlOptions;

typedef struct UvlStreamInfo {
    int32_t width, height;
    int32_t codec_id;          // AVCodecID de FFmpeg
    double  fps;
} UvlStreamInfo;

typedef struct UvlStats {
    uint64_t packets, frames_decoded, frames_dropped, bytes, reconnects;
} UvlStats;

UVL_API void        uvl_options_default(UvlOptions* o);
UVL_API UvlPlayer*  uvl_create(void);
UVL_API void        uvl_destroy(UvlPlayer* p);
// url: udp://@239.1.1.1:5000 (multicast) | udp://0.0.0.0:5000 | rtp://... | rtsp://... | srt/http/archivo
UVL_API int32_t     uvl_open(UvlPlayer* p, const char* url, const UvlOptions* opts);
UVL_API void        uvl_close(UvlPlayer* p);
UVL_API int32_t     uvl_get_state(UvlPlayer* p);
UVL_API int32_t     uvl_get_info(UvlPlayer* p, UvlStreamInfo* out);
UVL_API void        uvl_get_stats(UvlPlayer* p, UvlStats* out);
UVL_API int32_t     uvl_get_last_error(UvlPlayer* p, char* buf, int32_t cap);
// Devuelve 1 = frame nuevo copiado (RGBA32), 0 = no hay frame nuevo, -2 = buffer pequeno
UVL_API int32_t     uvl_try_get_frame(UvlPlayer* p, uint8_t* dst, int32_t dst_capacity,
                                      int64_t* pts_us, uint64_t* frame_counter);
UVL_API const char* uvl_version(void);
UVL_API void        uvl_set_jvm(void* java_vm);   // Android: necesario para MediaCodec

#ifdef __cplusplus
}
#endif
#endif
