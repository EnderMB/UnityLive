# Bateria de verificacion en Windows SIN Unity: codec x transporte con streams sinteticos de ffmpeg.
# Uso:  powershell -ExecutionPolicy Bypass -File scripts\test-matrix.ps1 -Ffmpeg C:\ffmpeg\...\bin\ffmpeg.exe
#   -MediaMtx C:\mediamtx\mediamtx.exe   (opcional, activa los casos RTSP)
#   -Codecs mpeg2,mpeg4,h264mf           (h264mf = codificador H.264 de Windows; h264/hevc requieren un ffmpeg con libx264/libx265)
param(
  [string]$Ffmpeg = "ffmpeg",
  [string]$TestNative = "unity\Assets\Plugins\Windows\x86_64\test_native.exe",
  [string]$MediaMtx = "",
  [string]$MulticastIf = "127.0.0.1",
  [string[]]$Codecs = @("mpeg2", "mpeg4")
)
Set-Location (Join-Path $PSScriptRoot "..")
$enc = @{
  mpeg2  = "-c:v mpeg2video -b:v 2M -g 25"; mpeg4 = "-c:v mpeg4 -g 25"
  h264   = "-c:v libx264 -preset ultrafast -tune zerolatency -g 25"
  hevc   = "-c:v libx265 -preset ultrafast -x265-params log-level=none:keyint=25"
  h264mf = "-c:v h264_mf -g 25"
}
$cid = @{ mpeg2 = 2; mpeg4 = 12; h264 = 27; hevc = 173; h264mf = 27 }
$work = Join-Path $env:TEMP ("uvl_" + [guid]::NewGuid().ToString("N")); New-Item -ItemType Directory $work | Out-Null
$src = "-loglevel error -re -f lavfi -i testsrc=size=640x360:rate=25"
$script:port = 24000; $script:fails = 0; $rows = @()
$mtx = $null
if ($MediaMtx) { $mtx = Start-Process $MediaMtx -WorkingDirectory (Split-Path $MediaMtx) -WindowStyle Hidden -PassThru; Start-Sleep 2 }

function Start-Sender($codec, $outArgs) {
  Start-Process $Ffmpeg -ArgumentList "$src $($enc[$codec]) $outArgs" -WindowStyle Hidden -PassThru
}
function Stop-After($proc, $sec) { if (-not $proc.WaitForExit($sec * 1000)) { $proc.Kill() } }

function Run-Case($name, $codec, $url, $senderFirst, $outArgs) {
  $out = Join-Path $work "out.txt"; $err = Join-Path $work "err.txt"
  if ($senderFirst) {
    $s = Start-Sender $codec $outArgs; Start-Sleep 2
    $r = Start-Process $TestNative -ArgumentList "$url 5" -RedirectStandardOutput $out -RedirectStandardError $err -WindowStyle Hidden -PassThru
    $r.WaitForExit(); Stop-After $s 4
  } else {
    $r = Start-Process $TestNative -ArgumentList "$url 7" -RedirectStandardOutput $out -RedirectStandardError $err -WindowStyle Hidden -PassThru
    Start-Sleep 1.5; $s = Start-Sender $codec $outArgs; Stop-After $s 5; $r.WaitForExit()
  }
  $txt = (Get-Content $out -Raw)
  $dec = 0; $c = "?"
  if ($txt -match 'decodificados=(\d+)') { $dec = [int]$Matches[1] }
  if ($txt -match 'codec=(\d+)') { $c = $Matches[1] }
  $ok = ($dec -ge 40) -and ($c -eq "$($cid[$codec])")
  if (-not $ok) { $script:fails++; Write-Host "--- $name"; Write-Host $txt }
  $script:rows += ("{0,-5} {1,-30} decodificados={2,-4} codec={3}" -f ($(if ($ok) { "PASS" } else { "FAIL" })), $name, $dec, $c)
}

foreach ($c in $Codecs) {
  $script:port += 2; $p = $script:port
  Run-Case "UDP unicast   / TS  / $c" $c "udp://127.0.0.1:$p" $false "-f mpegts udp://127.0.0.1:${p}?pkt_size=1316"
  $script:port += 2; $p = $script:port
  Run-Case "UDP multicast / TS  / $c" $c "udp://@239.255.7.7:${p}?localaddr=$MulticastIf" $false "-f mpegts udp://239.255.7.7:${p}?pkt_size=1316&localaddr=$MulticastIf&ttl=1"
  $script:port += 2; $p = $script:port
  Run-Case "RTP           / TS  / $c" $c "rtp://127.0.0.1:$p" $false "-f rtp_mpegts rtp://127.0.0.1:${p}?pkt_size=1316"
  $script:port += 2; $p = $script:port; $sdp = Join-Path $work "$c.sdp"
  Run-Case "RTP           / SDP / $c" $c $sdp $true "-f rtp -sdp_file `"$sdp`" rtp://127.0.0.1:$p"
  if ($MediaMtx) {
    foreach ($tr in "tcp", "udp") {
      $env:UVL_RTSP = $tr
      Run-Case "RTSP ($tr)    / $c" $c "rtsp://127.0.0.1:8554/$c$tr" $true "-f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/$c$tr"
    }
    Remove-Item Env:UVL_RTSP
  }
}
if ($mtx) { $mtx.Kill() }
Write-Host "`n================ RESULTADOS ================"; $rows | ForEach-Object { Write-Host $_ }
Write-Host "============================================`nFallos: $script:fails"
Remove-Item -Recurse -Force $work
exit $script:fails
