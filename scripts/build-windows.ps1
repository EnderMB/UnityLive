# Requiere: Visual Studio 2022, CMake y FFmpeg shared (LGPL) en $env:FFMPEG_ROOT (include\, lib\, bin\)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
if (-not $env:FFMPEG_ROOT) { throw "Define FFMPEG_ROOT" }
cmake -S . -B build\windows -G "Visual Studio 17 2022" -A x64 -DUVL_FFMPEG_ROOT="$env:FFMPEG_ROOT"
cmake --build build\windows --config Release
$dst = "unity\Assets\Plugins\Windows\x86_64"
Copy-Item "$env:FFMPEG_ROOT\bin\av*.dll", "$env:FFMPEG_ROOT\bin\sw*.dll", "$env:FFMPEG_ROOT\bin\postproc*.dll" -Destination $dst -ErrorAction SilentlyContinue
Write-Host "Plugin copiado en $dst"
