#!/bin/sh
# Builds the baseline and patched trees as the Raspberry Pi target
set -e
for v in base patched; do
  rm -rf /build/$v && mkdir -p /build/$v && tar -xf /work/$v.tar -C /build/$v
  cd /build/$v
  echo "=== building $v ==="
  if make rpi RELEASE=1 -j"$(nproc)" > /work/build-$v.log 2>&1; then echo "$v: BUILD OK"; else echo "$v: BUILD FAILED"; tail -40 /work/build-$v.log; exit 1; fi
  grep -i 'warning' /work/build-$v.log | head -20 || true
  ls -la ClassiCube
done
