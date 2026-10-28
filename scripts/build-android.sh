#!/usr/bin/env bash
# Requiere: ANDROID_NDK_HOME y FFMPEG_ANDROID_ROOT con subcarpetas por ABI:
#   $FFMPEG_ANDROID_ROOT/arm64-v8a/{include,lib}, .../armeabi-v7a, .../x86_64
# FFmpeg compilado con: --enable-shared --enable-jni --enable-mediacodec (sin --enable-gpl)
set -euo pipefail
cd "$(dirname "$0")/.."
: "${ANDROID_NDK_HOME:?define ANDROID_NDK_HOME}"
: "${FFMPEG_ANDROID_ROOT:?define FFMPEG_ANDROID_ROOT}"
for ABI in ${ABIS:-arm64-v8a armeabi-v7a x86_64}; do
  cmake -S . -B "build/android-$ABI" \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release \
    -DUVL_FFMPEG_ROOT="$FFMPEG_ANDROID_ROOT/$ABI"
  cmake --build "build/android-$ABI" -j
  cp -av "$FFMPEG_ANDROID_ROOT/$ABI"/lib/lib*.so "unity/Assets/Plugins/Android/libs/$ABI/"
done
