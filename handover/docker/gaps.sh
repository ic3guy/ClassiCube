#!/bin/sh
# Test only: builds /work/patched.tar as the linux target with counters added, then runs it in
# Classic mode with several draw merge gap values at once, printing draw calls and vertices per frame.
# Usage: gaps.sh <wait seconds> <gap>...
WAIT=$1
shift
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq libgl1-mesa-dev >/dev/null 2>&1
[ -f /work/classicube.zip ] || curl -sS -o /work/classicube.zip http://static.classicube.net/default.zip

rm -rf /g/src && mkdir -p /g/src && tar -xf /work/patched.tar -C /g/src && cd /g/src
sed -i 's/seed = Random_Next(&rnd, Int32_MaxValue);/seed = 12345;/' src/Server.c
sed -i '0,/^void Gfx_DrawIndexedTris_T2fC4b(int verticesCount, int startVertex, DrawHints hints) {/s//int dbg_draws;\nvoid Gfx_DrawIndexedTris_T2fC4b(int verticesCount, int startVertex, DrawHints hints) {\n\tdbg_draws++;/' src/Graphics_GL1.c
sed -i '0,/^\tif (Game_ScreenshotRequested) Game_TakeScreenshot();/s//\t{ extern int dbg_draws; static int dbg_frames, dbg_verts; dbg_verts += Game_Vertices \/ 200; if (++dbg_frames == 200) { int d = dbg_draws \/ 200; Platform_Log2("STATS draws %i verts %i", \&d, \&dbg_verts); dbg_draws = 0; dbg_frames = 0; dbg_verts = 0; } }\n\tif (Game_ScreenshotRequested) Game_TakeScreenshot();/' src/Game.c
if make linux RELEASE=1 -j"$(nproc)" > /work/gbuild.log 2>&1; then :; else echo "BUILD FAILED"; grep -n 'error' /work/gbuild.log | head; exit 1; fi

Xvfb :95 -screen 0 900x520x24 >/dev/null 2>&1 &
XPID=$!
sleep 1
export DISPLAY=:95

PIDS=""
for gap in "$@"; do
  rm -rf /run/g-$gap && mkdir -p /run/g-$gap/texpacks && cd /run/g-$gap
  cp /g/src/ClassiCube . && cp /work/classicube.zip texpacks/classicube.zip
  printf 'mode-classic=true\nfpslimit=LimitNone\ngfx-drawmergegap=%s\n' "$gap" > options.txt
  ./ClassiCube --singleplayer > /work/grun-$gap.log 2>&1 &
  PIDS="$PIDS $!"
done
sleep "$WAIT"
kill $PIDS 2>/dev/null; sleep 1; kill -9 $PIDS 2>/dev/null
for gap in "$@"; do
  echo "merge gap $gap: $(grep STATS /work/grun-$gap.log | tail -1)"
done
kill $XPID 2>/dev/null
