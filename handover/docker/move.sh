#!/bin/sh
# Walking test for the linux builds made by linux.sh: holds the forward key so chunks load and
# unload while moving, then lets the world settle and takes a screenshot of each build.
# Usage: move.sh <walk seconds> <settle seconds> [extra options.txt lines...]
WALK=${1:-10}
SETTLE=${2:-20}
shift; shift

Xvfb :97 -screen 0 900x520x24 >/dev/null 2>&1 &
XPID=$!
sleep 1
export DISPLAY=:97

for v in base patched; do
  rm -rf /run/mv-$v && mkdir -p /run/mv-$v/texpacks && cd /run/mv-$v
  cp /lin/$v/ClassiCube . && cp /work/classicube.zip texpacks/classicube.zip
  : > options.txt
  for o in "$@"; do echo "$o" >> options.txt; done

  ./ClassiCube --singleplayer > /work/mvrun-$v.log 2>&1 &
  PID=$!
  sleep 20
  WID=$(xdotool search --name ClassiCube | head -1)
  [ -n "$WID" ] && xdotool windowfocus "$WID" 2>/dev/null
  sleep 1
  xdotool keydown w
  sleep 2
  import -window root /work/mv-$v-walking.png
  sleep "$WALK"
  xdotool keyup w
  sleep "$SETTLE"
  if kill -0 $PID 2>/dev/null; then echo "$v: still running after walking"; else echo "$v: EXITED EARLY"; tail -5 /work/mvrun-$v.log; fi
  import -window root /work/mv-$v-settled.png
  kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null
done
kill $XPID 2>/dev/null
