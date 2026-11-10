#!/bin/sh
# Builds baseline and patched trees as the plain linux target (desktop OpenGL), runs both headless,
# and compares screenshots. Usage: linux.sh <wait seconds> [extra options.txt lines...]
WAIT=${1:-60}
shift
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq libgl1-mesa-dev procps >/dev/null 2>&1

if [ ! -f /work/classicube.zip ]; then
  curl -sS -o /work/classicube.zip http://static.classicube.net/default.zip || exit 1
fi

for v in base patched; do
  if [ ! -x /lin/$v/ClassiCube ]; then
    rm -rf /lin/$v && mkdir -p /lin/$v && tar -xf /work/$v.tar -C /lin/$v
    cd /lin/$v
    # test only change: fixed world seed so both builds generate the same map
    sed -i 's/seed = Random_Next(&rnd, Int32_MaxValue);/seed = 12345;/' src/Server.c
    if make linux RELEASE=1 -j"$(nproc)" > /work/linbuild-$v.log 2>&1; then echo "$v: LINUX BUILD OK"; else echo "$v: LINUX BUILD FAILED"; grep -n 'error' /work/linbuild-$v.log | head -20; exit 1; fi
  fi
done

Xvfb :98 -screen 0 900x520x24 >/dev/null 2>&1 &
XPID=$!
sleep 1
export DISPLAY=:98

for v in base patched; do
  rm -rf /run/lin-$v && mkdir -p /run/lin-$v/texpacks && cd /run/lin-$v
  cp /lin/$v/ClassiCube . && cp /work/classicube.zip texpacks/classicube.zip
  : > options.txt
  for o in "$@"; do echo "$o" >> options.txt; done

  ./ClassiCube --singleplayer > /work/linrun-$v.log 2>&1 &
  PID=$!
  sleep 5
  WID=$(xdotool search --name ClassiCube | head -1)
  [ -n "$WID" ] && xdotool windowfocus "$WID" 2>/dev/null
  sleep "$WAIT"
  if kill -0 $PID 2>/dev/null; then echo "$v: still running after ${WAIT}s, RSS $(ps -o rss= -p $PID) kB"; else echo "$v: EXITED EARLY"; tail -5 /work/linrun-$v.log; fi
  import -window root /work/lin-$v.png
  kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null
done
kill $XPID 2>/dev/null

cd /work
convert lin-base.png -crop 860x440+20+60 +repage la.png
convert lin-patched.png -crop 860x440+20+60 +repage lb.png
echo "differing pixels (of 378400): $(compare -metric AE la.png lb.png ldiff.png 2>&1)"
