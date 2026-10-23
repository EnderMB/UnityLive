#!/usr/bin/env bash
# Bateria de verificacion SIN Unity: codec x transporte, con streams sinteticos generados por ffmpeg.
#   TEST_NATIVE=<ruta test_native>  FFMPEG=<ffmpeg con libx264/libx265>  MEDIAMTX=<ruta mediamtx> (opcional, para RTSP)
#   MCAST_IF=<IP de la interfaz para multicast> (defecto 127.0.0.1)
cd "$(dirname "$0")/.."
T="${TEST_NATIVE:-unity/Assets/Plugins/Linux/x86_64/test_native}"
FF="${FFMPEG:-ffmpeg}"
MIF="${MCAST_IF:-127.0.0.1}"
export LD_LIBRARY_PATH="$(dirname "$T"):${LD_LIBRARY_PATH:-}"
WORK=$(mktemp -d); PORT=24000; FAILS=0; ROWS=()
SRC=(-re -f lavfi -i testsrc=size=640x360:rate=25)
declare -A CID=([mpeg2]=2 [mpeg4]=12 [h264]=27 [hevc]=173)
declare -A ENC=([mpeg2]="-c:v mpeg2video -b:v 2M -g 25" [mpeg4]="-c:v mpeg4 -g 25"
  [h264]="-c:v libx264 -preset ultrafast -tune zerolatency -g 25" [hevc]="-c:v libx265 -preset ultrafast -x265-params log-level=none:keyint=25")

if [ -n "$MEDIAMTX" ]; then (cd "$(dirname "$MEDIAMTX")" && "$MEDIAMTX" >"$WORK/mtx.log" 2>&1 &) ; sleep 2; fi
cleanup() { pkill -f "$(basename "${MEDIAMTX:-__none__}")" 2>/dev/null; rm -rf "$WORK"; }; trap cleanup EXIT

# run <nombre> <codec> <url_receptor> <sender_first 0|1> -- <args de salida de ffmpeg>
run() {
  local name=$1 codec=$2 url=$3 first=$4; shift 5
  [ -n "${ONLY:-}" ] && ! [[ "$name" =~ $ONLY ]] && return
  local out="$WORK/o.txt"
  if [ "$first" = 1 ]; then
    timeout 7 $FF -loglevel error "${SRC[@]}" ${ENC[$codec]} "$@" >/dev/null 2>&1 & local sp=$!; sleep 2
    "$T" "$url" 5 >"$out" 2>&1
  else
    "$T" "$url" 7 >"$out" 2>&1 & local rp=$!; sleep 1.5
    timeout 5 $FF -loglevel error "${SRC[@]}" ${ENC[$codec]} "$@" >/dev/null 2>&1
    wait $rp
  fi
  wait 2>/dev/null
  local dec cid st err
  dec=$(grep -o 'decodificados=[0-9]*' "$out" | head -1 | cut -d= -f2); dec=${dec:-0}
  cid=$(grep -o 'codec=[0-9]*' "$out" | head -1 | cut -d= -f2)
  st=PASS; { [ "$dec" -ge 40 ] && [ "$cid" = "${CID[$codec]}" ]; } || { st=FAIL; FAILS=$((FAILS+1)); }
  ROWS+=("$(printf '%-6s %-26s decodificados=%-4s codec=%-4s' "$st" "$name" "$dec" "${cid:-?}")")
  [ $st = FAIL ] && echo "--- $name"; [ $st = FAIL ] && sed 's/^/    /' "$out" | tail -4
}
nextport() { PORT=$((PORT+2)); }

for c in mpeg2 mpeg4 h264 hevc; do
  nextport; run "UDP unicast  / TS    / $c" $c "udp://127.0.0.1:$PORT" 0 -- -f mpegts "udp://127.0.0.1:$PORT?pkt_size=1316"
  nextport; run "UDP multicast/ TS    / $c" $c "udp://@239.255.7.7:$PORT?localaddr=$MIF" 0 -- -f mpegts "udp://239.255.7.7:$PORT?pkt_size=1316&localaddr=$MIF&ttl=1"
  nextport; run "RTP          / TS    / $c" $c "rtp://127.0.0.1:$PORT" 0 -- -f rtp_mpegts "rtp://127.0.0.1:$PORT?pkt_size=1316"
  nextport; run "RTP          / SDP   / $c" $c "$WORK/$c.sdp" 1 -- -f rtp -sdp_file "$WORK/$c.sdp" "rtp://127.0.0.1:$PORT"
  if [ -n "$MEDIAMTX" ]; then
    for tr in tcp udp; do
      UVL_RTSP=$tr run "RTSP ($tr)   / $c" $c "rtsp://127.0.0.1:8554/$c$tr" 1 -- -f rtsp -rtsp_transport tcp "rtsp://127.0.0.1:8554/$c$tr"
    done
  fi
done

echo; echo "================ RESULTADOS ================"; printf '%s\n' "${ROWS[@]}"
echo "============================================"; echo "Fallos: $FAILS"; exit $FAILS
