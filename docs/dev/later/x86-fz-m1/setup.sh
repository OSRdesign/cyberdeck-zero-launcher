#!/bin/sh
# Scratch only: native x86_64 build of the launcher's device (cp0) backend, outside the repos.
set -e
S=/tmp/x86scratch/sysroot
B=/tmp/x86scratch/bin
mkdir -p $B
# host runtime libs next to the extracted -dev symlinks
for l in libudev.so.1 libdbus-1.so.3 libglib-2.0.so.0 libgio-2.0.so.0 libgobject-2.0.so.0 libgmodule-2.0.so.0 \
         libxkbcommon.so.0 libffi.so.8 libpcre2-8.so.0 libmount.so.1 libblkid.so.1 libselinux.so.1 libz.so.1; do
  for f in /usr/lib/x86_64-linux-gnu/$l*; do cp -an "$f" $S/usr/lib/x86_64-linux-gnu/ 2>/dev/null || true; done
done
INC="-isystem $S/usr/include -isystem $S/usr/include/glib-2.0 -isystem $S/usr/lib/x86_64-linux-gnu/glib-2.0/include \
 -isystem $S/usr/include/dbus-1.0 -isystem $S/usr/lib/x86_64-linux-gnu/dbus-1.0/include -isystem /usr/include/freetype2 \
 -isystem /usr/include/libpng16 -isystem $S/usr/include/SDL2"
LIB="-L$S/usr/lib/x86_64-linux-gnu -Wl,-rpath-link,$S/usr/lib/x86_64-linux-gnu"
for t in gcc g++; do
  printf '#!/bin/sh\nexec /usr/bin/%s %s %s "$@"\n' "$t" "$INC" "$LIB" > $B/x86s-$t
  chmod +x $B/x86s-$t
done
for t in ar ranlib as ld objcopy objdump strip nm size cpp readelf; do ln -sf /usr/bin/$t $B/x86s-$t; done
cd /tmp/x86scratch/launcher/projects/APPLaunch
grep -v -e NEON -e DRAW_SW_ASM config_defaults.mk > x86_native_cp0_config_defaults.mk
printf 'CONFIG_TOOLCHAIN_PREFIX="x86s-"\nCONFIG_TOOLCHAIN_PATH="%s"\n' "$B" >> x86_native_cp0_config_defaults.mk
echo setup done
