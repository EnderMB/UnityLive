#!/usr/bin/env bash
# Prueba el nucleo nativo en un dispositivo Android REAL por adb (sin Unity, sin APK).
# Requisitos: adb con dispositivo conectado (depuracion USB), haber ejecutado build-android.sh,
#             ffmpeg en el PC (con libx264 para H.264) y mediamtx en el PC.
#   ABI=arm64-v8a FFMPEG=ffmpeg MEDIAMTX=/ruta/mediamtx FFMPEG_ANDROID_ROOT=... ./scripts/test-android.sh
# RTSP viaja por TCP a traves de "adb reverse", asi que NO depende de la red Wi-Fi (util en redes corporativas).
# LIMITE: en "adb shell" no hay JVM, asi que aqui se prueba decodificacion por SOFTWARE. MediaCodec y
#         el multicast (MulticastLock) solo se pueden validar dentro de una app Android (ver README).
set -uo pipefail
cd "$(dirname "$0")/.."
ABI="${ABI:-arm64-v8a}"; FF="${FFMPEG:-ffmpeg}"; D=/data/local/tmp/uvl
: "${FFMPEG_ANDROID_ROOT:?define FFMPEG_ANDROID_ROOT}"
adb get-state >/dev/null || { echo "No hay dispositivo adb"; exit 2; }
adb shell "mkdir -p $D"
adb push "build/android-$ABI/bin/test_native" "unity/Assets/Plugins/Android/libs/$ABI/libuvl_video.so" "$D/" >/dev/null
for l in "$FFMPEG_ANDROID_ROOT/$ABI"/lib/lib*.so; do adb push "$l" "$D/" >/dev/null; done
adb shell "chmod +x $D/test_native"
echo "Dispositivo: $(adb shell getprop ro.product.model) / Android $(adb shell getprop ro.build.version.release) / $(adb shell getprop ro.product.cpu.abi)"

if [ -n "${MEDIAMTX:-}" ]; then (cd "$(dirname "$MEDIAMTX")" && "$MEDIAMTX" >/dev/null 2>&1 &); sleep 2; fi
adb reverse tcp:8554 tcp:8554 >/dev/null
declare -A ENC=([mpeg2]="-c:v mpeg2video -b:v 2M -g 25" [mpeg4]="-c:v mpeg4 -g 25"
  [h264]="-c:v libx264 -preset ultrafast -tune zerolatency -g 25" [hevc]="-c:v libx265 -preset ultrafast -x265-params log-level=none:keyint=25")
declare -A CID=([mpeg2]=2 [mpeg4]=12 [h264]=27 [hevc]=173)
FAILS=0
for c in mpeg2 mpeg4 h264 hevc; do
  timeout 9 $FF -loglevel error -re -f lavfi -i testsrc=size=640x360:rate=25 ${ENC[$c]} -f rtsp -rtsp_transport tcp "rtsp://127.0.0.1:8554/$c" >/dev/null 2>&1 &
  sleep 2
  out=$(adb shell "cd $D && LD_LIBRARY_PATH=. UVL_RTSP=tcp UVL_HW=0 ./test_native rtsp://127.0.0.1:8554/$c 5" 2>&1 | tr -d '\r')
  wait 2>/dev/null
  dec=$(echo "$out" | grep -o 'decodificados=[0-9]*' | head -1 | cut -d= -f2); cid=$(echo "$out" | grep -o 'codec=[0-9]*' | head -1 | cut -d= -f2)
  if [ "${dec:-0}" -ge 40 ] && [ "$cid" = "${CID[$c]}" ]; then echo "PASS  Android RTSP(tcp) $c decodificados=$dec"; else echo "FAIL  Android RTSP(tcp) $c"; echo "$out" | sed 's/^/    /'; FAILS=$((FAILS+1)); fi
done
adb reverse --remove tcp:8554 >/dev/null; pkill -f "$(basename "${MEDIAMTX:-__none__}")" 2>/dev/null
echo "Fallos: $FAILS"; exit $FAILS
