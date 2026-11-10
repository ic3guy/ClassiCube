#!/bin/sh
# Tests the three convenience changes on the linux build of /work/patched.tar:
#  A) sp-worldsize=128 in Classic mode  B) Quit button on the Classic pause menu  C) window-fullscreen=true
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq libgl1-mesa-dev openbox >/dev/null 2>&1
[ -f /work/classicube.zip ] || curl -sS -o /work/classicube.zip http://static.classicube.net/default.zip

rm -rf /u/src && mkdir -p /u/src && tar -xf /work/patched.tar -C /u/src && cd /u/src
sed -i 's/seed = Random_Next(&rnd, Int32_MaxValue);/seed = 12345;/' src/Server.c
if make linux RELEASE=1 -j"$(nproc)" > /work/ubuild.log 2>&1; then echo "LINUX BUILD OK"; else echo "BUILD FAILED"; grep -n 'error' /work/ubuild.log | head; exit 1; fi

setup() {
  rm -rf /run/u-$1 && mkdir -p /run/u-$1/texpacks && cd /run/u-$1
  cp /u/src/ClassiCube . && cp /work/classicube.zip texpacks/classicube.zip
  : > options.txt
}

Xvfb :94 -screen 0 900x520x24 >/dev/null 2>&1 &
XPID=$!
sleep 1
export DISPLAY=:94

echo "=== A: Classic mode with sp-worldsize=128 ==="
setup a
printf 'mode-classic=true\nsp-worldsize=128\n' > options.txt
./ClassiCube --singleplayer > /work/urun-a.log 2>&1 &
PID=$!
sleep 5; WID=$(xdotool search --name ClassiCube | head -1); xdotool windowfocus "$WID" 2>/dev/null
sleep 60
import -window root /work/ui-a-small-classic.png
kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null

echo "=== B: Quit button on the Classic pause menu ==="
setup b
printf 'mode-classic=true\nsp-worldsize=128\n' > options.txt
./ClassiCube --singleplayer > /work/urun-b.log 2>&1 &
PID=$!
sleep 5; WID=$(xdotool search --name ClassiCube | head -1); xdotool windowfocus "$WID" 2>/dev/null
sleep 25
xdotool key Escape
sleep 2
import -window root /work/ui-b-pause.png
eval "$(xdotool getwindowgeometry --shell "$WID")"
echo "window at $X,$Y size ${WIDTH}x${HEIGHT}"
# quit button is 120x40, anchored 5 pixels from the bottom right corner of the window
xdotool mousemove $((X + WIDTH - 65)) $((Y + HEIGHT - 25))
sleep 1
xdotool click 1
sleep 4
if kill -0 $PID 2>/dev/null; then echo "B: STILL RUNNING after clicking quit"; kill -9 $PID; else echo "B: game exited after clicking Quit game"; fi

echo "=== C: window-fullscreen=true (with a window manager running) ==="
openbox >/dev/null 2>&1 &
OPID=$!
sleep 2
setup c
printf 'window-fullscreen=true\nsp-worldsize=128\n' > options.txt
./ClassiCube --singleplayer > /work/urun-c.log 2>&1 &
PID=$!
sleep 45
WID=$(xdotool search --name ClassiCube | head -1)
eval "$(xdotool getwindowgeometry --shell "$WID")"
echo "C: screen is 900x520, game window is ${WIDTH}x${HEIGHT} at $X,$Y"
import -window root /work/ui-c-fullscreen.png
kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null
kill $OPID $XPID 2>/dev/null
