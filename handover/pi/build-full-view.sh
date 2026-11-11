#!/bin/sh
# Experimental 32-bit Pi 3 OpenGL 2 build; run from the repository root.
# Produces ClassiCube-full-view without changing the installed executable.
set -eu
make -f misc/makefiles/linux.mk -j2 RELEASE=1 BUILD_DIR=build/pi3-full-view \
    TARGET=ClassiCube-full-view OPT_LEVEL=3 \
    EXTRA_CFLAGS='-mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard -DCC_GFX_BACKEND=CC_GFX_BACKEND_GL2'
