cmake_minimum_required(VERSION 3.16)
project(UnityLive LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# FFmpeg compartido (LGPL, sin --enable-gpl). Estructura: <root>/include y <root>/lib (o bin en Windows).
set(UVL_FFMPEG_ROOT "" CACHE PATH "Prefijo de FFmpeg para la plataforma/ABI actual")

find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h HINTS "${UVL_FFMPEG_ROOT}/include")
set(FFMPEG_LIBS "")
foreach(c avformat avcodec avutil swscale)
  find_library(FFMPEG_${c} NAMES ${c} HINTS "${UVL_FFMPEG_ROOT}/lib" "${UVL_FFMPEG_ROOT}/bin")
  if(NOT FFMPEG_${c})
    message(FATAL_ERROR "No se encontro lib${c}. Usa -DUVL_FFMPEG_ROOT=<ruta>")
  endif()
  list(APPEND FFMPEG_LIBS ${FFMPEG_${c}})
endforeach()
if(NOT FFMPEG_INCLUDE_DIR)
  message(FATAL_ERROR "Cabeceras de FFmpeg no encontradas. Usa -DUVL_FFMPEG_ROOT=<ruta>")
endif()

# Destino dentro del proyecto Unity
if(ANDROID)
  set(UVL_PLUGIN_DIR "${CMAKE_SOURCE_DIR}/unity/Assets/Plugins/Android/libs/${ANDROID_ABI}")
elseif(WIN32)
  set(UVL_PLUGIN_DIR "${CMAKE_SOURCE_DIR}/unity/Assets/Plugins/Windows/x86_64")
else()
  set(UVL_PLUGIN_DIR "${CMAKE_SOURCE_DIR}/unity/Assets/Plugins/Linux/x86_64")
endif()

add_library(uvl_video SHARED native/src/uvl_video.cpp)
target_include_directories(uvl_video PUBLIC native/include PRIVATE "${FFMPEG_INCLUDE_DIR}")
target_link_libraries(uvl_video PRIVATE ${FFMPEG_LIBS})
set_target_properties(uvl_video PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${UVL_PLUGIN_DIR}"
  RUNTIME_OUTPUT_DIRECTORY "${UVL_PLUGIN_DIR}"
  BUILD_WITH_INSTALL_RPATH ON
  INSTALL_RPATH "$ORIGIN")
if(ANDROID)
  target_link_libraries(uvl_video PRIVATE log android)
endif()

# Ejecutable de prueba (tambien en Android: se ejecuta con adb shell, sin necesidad de Unity)
add_executable(test_native tests/test_native.cpp)
target_link_libraries(test_native PRIVATE uvl_video)
if(ANDROID)
  set_target_properties(test_native PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
else()
  set_target_properties(test_native PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${UVL_PLUGIN_DIR}>")
endif()
