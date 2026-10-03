#!/bin/sh
# Build the Raspberry Pi Zero 2W port and assemble an install bundle.
#
# Run on a Linux x86_64 host (or WSL) from anywhere inside the repository:
#     projects/APPLaunch/pizero2w/build.sh [--with-store]
# Output: projects/APPLaunch/pizero2w/bundle/  and  pizero2w-bundle.tar.gz
# Copy the tarball to the Pi, unpack it and run  sudo ./install.sh
#
# Needs: git, python3 (venv), an aarch64 cross toolchain (apt install gcc-aarch64-linux-gnu
# g++-aarch64-linux-gnu pkg-config libffi-dev libfreetype6-dev), and network access for the first
# build (the SDK downloads LVGL and prebuilt libraries). --with-store also builds the Store
# (projects/AppStore submodule).
set -eu

WITH_STORE=0
[ "${1:-}" = "--with-store" ] && WITH_STORE=1

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
OUT="$HERE/bundle"

command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || {
    echo "aarch64-linux-gnu-gcc not found: sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu" >&2
    exit 1
}

# Python tooling in a private venv (SCons and the SDK's helpers)
VENV="${VENV:-$REPO/.venv-pizero2w}"   # set VENV to reuse an existing virtualenv
if [ ! -x "$VENV/bin/scons" ]; then
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q parse scons requests tqdm setuptools-rust paramiko scp
fi
PATH="$VENV/bin:$PATH"

if [ -e "$REPO/.git" ]; then
    git -C "$REPO" submodule update --init --depth 1 SDK
    if [ "$WITH_STORE" = 1 ]; then git -C "$REPO" submodule update --init --depth 1 projects/AppStore; fi
fi

export APPLAUNCH_HW=pizero2w                      # selects the Pi hardware profile (settings_hw_profile.hpp)
export CONFIG_REPO_AUTOMATION=y                   # download SDK components without asking
export CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk

echo "== building the launcher"
( cd "$REPO/projects/APPLaunch" && scons -j"$(nproc)" )

if [ "$WITH_STORE" = 1 ]; then
    echo "== building the Store"
    ( cd "$REPO/projects/AppStore" && scons -j"$(nproc)" )
fi

echo "== building the framebuffer shim"
( cd "$HERE/vfb" && CC=aarch64-linux-gnu-gcc ./build.sh )

echo "== assembling the bundle"
rm -rf "$OUT"
mkdir -p "$OUT/payload/bin" "$OUT/payload/lib" "$OUT/payload/share" "$OUT/payload/etc"
cp "$REPO/projects/APPLaunch/dist/M5CardputerZero-APPLaunch" "$OUT/payload/bin/"
[ "$WITH_STORE" = 1 ] && cp "$REPO/projects/AppStore/dist/M5CardputerZero-AppStore" "$OUT/payload/bin/"
cp "$HERE/vfb/libapplaunch_vfb.so" "$OUT/payload/lib/"
cp -a "$REPO/projects/APPLaunch/dist/APPLaunch/." "$OUT/payload/share/"
if [ "$WITH_STORE" = 1 ] && [ -d "$REPO/projects/AppStore/dist/APPLaunch" ]; then
    cp -a "$REPO/projects/AppStore/dist/APPLaunch/." "$OUT/payload/share/"
fi
for file in APPLaunch.service launcher-ntp-default.service waveshare-pwm-backlight.dts \
            90-backlight-unblank.rules 91-applaunch-vkbd.rules \
            50-networkmanager-netdev.rules 51-launcher-time-power.rules; do
    cp "$HERE/$file" "$OUT/payload/etc/"
done
cp "$HERE/install.sh" "$HERE/config.txt.snippet" "$HERE/README.md" "$OUT/"
chmod +x "$OUT/install.sh"

tar -C "$HERE" -czf "$HERE/pizero2w-bundle.tar.gz" bundle
echo "== done: $HERE/pizero2w-bundle.tar.gz"
