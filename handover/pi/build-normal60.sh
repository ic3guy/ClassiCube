#!/bin/sh
# Run from the repository root on 32-bit Raspberry Pi 3 OS.
# Builds the tested desktop OpenGL 2 backend without installing it.
set -eu
make -f misc/makefiles/linux.mk -j"${JOBS:-2}" RELEASE=1 \
    BUILD_DIR=build/pi3-normal60 TARGET=ClassiCube-normal60 OPT_LEVEL=3 \
    EXTRA_CFLAGS='-mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard -DCC_GFX_BACKEND=CC_GFX_BACKEND_GL2'