#!/bin/sh
# Cross-build the virtual framebuffer shim for the Pi (aarch64). Run from this directory.
set -e
CC=${CC:-aarch64-linux-gnu-gcc}
$CC -shared -fPIC -O2 -Wall -Wextra -o libapplaunch_vfb.so applaunch_vfb_shim.c \
    -I../../../../ext_components/cp0_lvgl/include -ldl
echo "built $(pwd)/libapplaunch_vfb.so"
