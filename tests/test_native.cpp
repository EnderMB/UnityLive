// Uso: test_native <url> [segundos] [volcado.ppm]
//  p.ej. test_native udp://@239.255.0.1:5000 10
//        test_native rtsp://camara/stream 10
#include "uvl_video.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("uso: %s <url> [segundos]\n", argv[0]); return 2; }
    int secs = argc > 2 ? std::atoi(argv[2]) : 10;
    const char* dump = argc > 3 ? argv[3] : nullptr;
    int dw = 0, dh = 0; std::vector<uint8_t> last;
    std::printf("%s\n", uvl_version());

    UvlPlayer* p = uvl_create();
    UvlOptions o; uvl_options_default(&o);
    if (const char* r = std::getenv("UVL_RTSP"))           // UVL_RTSP=udp|tcp|auto
        o.rtsp_transport = !std::strcmp(r, "udp") ? UVL_RTSP_UDP : !std::strcmp(r, "auto") ? UVL_RTSP_AUTO : UVL_RTSP_TCP;
    if (const char* h = std::getenv("UVL_HW")) o.hw_decode = std::atoi(h);
    uvl_open(p, argv[1], &o);

    std::vector<uint8_t> buf;
    uint64_t shown = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(secs)) {
        UvlStreamInfo i; uvl_get_info(p, &i);
        size_t need = (size_t)i.width * i.height * 4;
        if (need > buf.size()) buf.resize(need);
        int64_t pts; uint64_t n;
        if (need && uvl_try_get_frame(p, buf.data(), (int)buf.size(), &pts, &n) == 1) {
            shown++;
            if (dump) { last.assign(buf.begin(), buf.begin() + need); dw = i.width; dh = i.height; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    UvlStats s; uvl_get_stats(p, &s);
    char err[256]; uvl_get_last_error(p, err, sizeof err);
    UvlStreamInfo i; uvl_get_info(p, &i);
    std::printf("estado=%d %dx%d codec=%d fps=%.2f\npaquetes=%llu bytes=%llu decodificados=%llu mostrados=%llu descartados=%llu reconexiones=%llu\nerror='%s'\n",
        uvl_get_state(p), i.width, i.height, i.codec_id, i.fps,
        (unsigned long long)s.packets, (unsigned long long)s.bytes, (unsigned long long)s.frames_decoded,
        (unsigned long long)shown, (unsigned long long)s.frames_dropped, (unsigned long long)s.reconnects, err);
    if (dump && !last.empty()) {   // guarda el ultimo frame (RGBA, ya en orientacion Unity: bottom-up) como PPM top-down
        if (FILE* f = std::fopen(dump, "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", dw, dh);
            for (int y = dh - 1; y >= 0; y--)
                for (int x = 0; x < dw; x++) std::fwrite(&last[((size_t)y * dw + x) * 4], 1, 3, f);
            std::fclose(f);
        }
    }
    uvl_destroy(p);
    return s.frames_decoded > 0 ? 0 : 1;
}
