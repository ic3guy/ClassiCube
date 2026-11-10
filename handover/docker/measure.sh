#!/bin/sh
# Test only: builds the given source tarballs as the linux target with counters added, runs each
# in Classic mode at two view distances, and prints average map draw calls and vertices per frame.
# Usage: measure.sh <wait seconds> <name>...   (expects /work/<name>.tar)
WAIT=$1
shift
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq libgl1-mesa-dev >/dev/null 2>&1
[ -f /work/classicube.zip ] || curl -sS -o /work/classicube.zip http://static.classicube.net/default.zip

for v in "$@"; do
  rm -rf /m/$v && mkdir -p /m/$v && tar -xf /work/$v.tar -C /m/$v && cd /m/$v
  sed -i 's/seed = Random_Next(&rnd, Int32_MaxValue);/seed = 12345;/' src/Server.c
  # count calls to the function that draws map chunk geometry
  sed -i '0,/^void Gfx_DrawIndexedTris_T2fC4b(int verticesCount, int startVertex, DrawHints hints) {/s//int dbg_draws;\nvoid Gfx_DrawIndexedTris_T2fC4b(int verticesCount, int startVertex, DrawHints hints) {\n\tdbg_draws++;/' src/Graphics_GL1.c
  # log averages every 200 frames, just before the frame is presented
  sed -i '0,/^\tif (Game_ScreenshotRequested) Game_TakeScreenshot();/s//\t{ extern int dbg_draws; static int dbg_frames, dbg_verts; dbg_verts += Game_Vertices \/ 200; if (++dbg_frames == 200) { int d = dbg_draws \/ 200; Platform_Log2("STATS draws %i verts %i", \&d, \&dbg_verts); dbg_draws = 0; dbg_frames = 0; dbg_verts = 0; } }\n\tif (Game_ScreenshotRequested) Game_TakeScreenshot();/' src/Game.c
  if make linux RELEASE=1 -j"$(nproc)" > /work/mbuild-$v.log 2>&1; then :; else echo "$v: BUILD FAILED"; grep -n 'error' /work/mbuild-$v.log | head; exit 1; fi
done

Xvfb :96 -screen 0 900x520x24 >/dev/null 2>&1 &
XPID=$!
sleep 1
export DISPLAY=:96

for dist in 512 128; do
  for v in "$@"; do
    rm -rf /run/m-$v && mkdir -p /run/m-$v/texpacks && cd /run/m-$v
    cp /m/$v/ClassiCube . && cp /work/classicube.zip texpacks/classicube.zip
    printf 'mode-classic=true\nviewdist=%s\nfpslimit=LimitNone\n' "$dist" > options.txt
    ./ClassiCube --singleplayer > /work/mrun-$v-$dist.log 2>&1 &
    PID=$!
    sleep 5
    WID=$(xdotool search --name ClassiCube | head -1)
    [ -n "$WID" ] && xdotool windowfocus "$WID" 2>/dev/null
    sleep "$WAIT"
    kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null
    echo "viewdist $dist  $v: $(grep STATS /work/mrun-$v-$dist.log | tail -1)"
  done
done
kill $XPID 2>/dev/null
