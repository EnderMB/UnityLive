#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
FFMPEG_ROOT="${FFMPEG_ROOT:-/usr}"
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release -DUVL_FFMPEG_ROOT="$FFMPEG_ROOT"
cmake --build build/linux -j"$(nproc)"
# Si usas un FFmpeg propio (recomendado, LGPL), copia sus .so junto al plugin
if [ "$FFMPEG_ROOT" != "/usr" ]; then
  cp -av "$FFMPEG_ROOT"/lib/libav{format,codec,util}.so* "$FFMPEG_ROOT"/lib/libsw{scale,resample}.so* \
     unity/Assets/Plugins/Linux/x86_64/
fi
