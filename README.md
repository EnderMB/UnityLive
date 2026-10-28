# UnityLive

Plugin Unity (Windows x64, Linux x64, Android arm64/armv7/x86_64) para recibir y reproducir video en tiempo real.

## Arquitectura
```
Unity C#  (UvlPlayer : MonoBehaviour)
   │ P/Invoke (API C estable, uvl_video.h)
libuvl_video  ── hilo propio: recibir → demux → decodificar → RGBA (ultimo frame)
   │
FFmpeg compartido LGPL: libavformat / libavcodec / libswscale / libavutil
```
- **Protocolos**: UDP unicast, UDP multicast, RTP, RTSP (TCP/UDP) — todos vía libavformat.
- **Contenedores**: MPEG-TS (UDP/RTP/archivo), RTP payloads nativos, RTSP.
- **Codecs**: MPEG-2, MPEG-4 Part 2, H.264/AVC, H.265/HEVC.
- **Decodificación**: software en todas las plataformas; MediaCodec en Android (`hardwareDecode`), con fallback a software.
- Politica "ultimo frame gana": si Unity no consume a tiempo, se descarta (estadistica `framesDropped`), la latencia no se acumula.

## URLs
| Caso | URL |
|---|---|
| UDP unicast | `udp://0.0.0.0:5000` |
| UDP multicast | `udp://@239.255.0.1:5000` |
| Multicast, varias NIC | `udp://@239.255.0.1:5000?localaddr=192.168.1.20` |
| RTP | `rtp://0.0.0.0:5004` (necesita SDP si no es MPEG-TS) |
| RTSP | `rtsp://user:pass@host:554/stream` |

## Compilar
FFmpeg debe ser **shared + LGPL** (sin `--enable-gpl`, sin `--enable-nonfree`). Para Android: `--enable-jni --enable-mediacodec`.
- Linux:   `FFMPEG_ROOT=/ruta ./scripts/build-linux.sh`
- Windows: `$env:FFMPEG_ROOT="C:\ffmpeg"; .\scripts\build-windows.ps1`
- Android: `ANDROID_NDK_HOME=... FFMPEG_ANDROID_ROOT=... ./scripts/build-android.sh`
- Test:    `test_native udp://@239.255.0.1:5000 10`

Las librerias quedan en `unity/Assets/Plugins/...`. En el Inspector de Unity restringe cada .dll/.so a su plataforma.

## Android
Fusiona `Plugins/Android/AndroidManifest.uvl.xml` con tu manifiesto (INTERNET + CHANGE_WIFI_MULTICAST_STATE).
`UvlPlayer` adquiere el MulticastLock y pasa la JVM a FFmpeg automaticamente.

## Verificacion sin Unity
| Plataforma | Comando | Que valida |
|---|---|---|
| Linux   | `MEDIAMTX=/ruta/mediamtx ./scripts/test-matrix.sh` | 4 codecs x (UDP, multicast, RTP/TS, RTP/SDP, RTSP tcp/udp) |
| Windows | `powershell -ExecutionPolicy Bypass -File scripts\test-matrix.ps1 -Ffmpeg <ffmpeg.exe> -MediaMtx <mediamtx.exe>` | igual (solo los codecs que tu ffmpeg pueda emitir) |
| Android | `FFMPEG_ANDROID_ROOT=... MEDIAMTX=... ./scripts/test-android.sh` | decodificacion por software en el dispositivo real, RTSP/TCP via `adb reverse` |
| Wrapper C# | `cd tests/csharp && dotnet run -- udp://127.0.0.1:5000 8` | el `UvlNative.cs` real (structs, firmas) cargando la libreria |
| Imagen | `test_native <url> 6 frame.ppm` | volcado del frame para inspeccion visual |

Cada caso es PASS si decodifica >= 40 frames y el codec detectado es el esperado.
Lo que SOLO se puede validar en una app Android: MediaCodec (hardware) y multicast por Wi-Fi (MulticastLock).
