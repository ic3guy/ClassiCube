#!/bin/sh
# Experimental full-distance renderer build for the 32-bit Raspberry Pi 3 OS.
# Run from the repository root. Does not install or change game options.
set -eu
make linux RELEASE=1
compiler=${CC:-cc}
for unit in HiZ Occlusion; do
    "$compiler" -fvisibility=hidden -fno-ident -pipe -fno-math-errno -O3 \
        -mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard \
        -c "src/$unit.c" -o "build/linux/src/$unit.o"
done
make linux RELEASE=1
