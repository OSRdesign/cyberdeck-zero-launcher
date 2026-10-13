#!/bin/sh
# Build the Raspberry Pi port (CyberdeckZero Launcher) and assemble an install bundle.
#
# Run on a Linux x86_64 host (or WSL) from anywhere inside the repository:
#     projects/APPLaunch/pizero2w/build.sh [--with-store] [--no-legacy-name]
# Output: projects/APPLaunch/pizero2w/bundle/  and  cyberdeckzero-launcher-<version>-arm64.tar.gz next to this script
# (<version>: APPLAUNCH_VERSION, default "dev"), plus a copy of that file named pizero2w-bundle.tar.gz, the quick-start
# name of the releases before the rename (wanted for the first release after it only: --no-legacy-name, or
# LEGACY_BUNDLE_NAME=0 in the environment, leaves it out).
# Copy the tarball to the Pi, unpack it and run  sudo ./install.sh   (to remove it again: ./uninstall.sh)
#
# Needs: git, python3 (venv), an aarch64 cross toolchain (apt install gcc-aarch64-linux-gnu
# g++-aarch64-linux-gnu pkg-config libffi-dev libfreetype6-dev), and network access for the first
# build (the SDK downloads LVGL and prebuilt libraries). --with-store also builds the Store
# (projects/AppStore submodule).
set -eu

WITH_STORE=0
LEGACY_NAME=${LEGACY_BUNDLE_NAME:-1}
for arg in "$@"; do
    case "$arg" in
        --with-store) WITH_STORE=1 ;;
        --no-legacy-name) LEGACY_NAME=0 ;;
        -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg (see --help)" >&2; exit 2 ;;
    esac
done

# The version goes into the tarball's name (and, through SConstruct, into About: a semantic version there).
VERSION=${APPLAUNCH_VERSION:-dev}
case "$VERSION" in
    ""|*[!A-Za-z0-9.+-]*) echo "APPLAUNCH_VERSION \"$VERSION\": letters, digits, '.', '+' and '-' only" >&2; exit 2 ;;
esac
TARBALL="cyberdeckzero-launcher-$VERSION-arm64.tar.gz"
LEGACY_TARBALL="pizero2w-bundle.tar.gz"

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
# A bundle is a release build unless the caller sets the channel: development-only pages (System > Keyboard test) stay
# hidden on devices, and About / System > Channel reads "release". tools/dev/wsl-build.sh passes "development".
export APPLAUNCH_CHANNEL="${APPLAUNCH_CHANNEL:-release}"

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
mkdir -p "$OUT/payload/bin" "$OUT/payload/lib" "$OUT/payload/share" "$OUT/payload/etc" "$OUT/payload/doc"
cp "$REPO/projects/APPLaunch/dist/cyberdeckzero" "$OUT/payload/bin/"
[ "$WITH_STORE" = 1 ] && cp "$REPO/projects/AppStore/dist/M5CardputerZero-AppStore" "$OUT/payload/bin/"
cp "$HERE/vfb/libapplaunch_vfb.so" "$OUT/payload/lib/"
cp -a "$REPO/projects/APPLaunch/dist/APPLaunch/." "$OUT/payload/share/"
if [ "$WITH_STORE" = 1 ] && [ -d "$REPO/projects/AppStore/dist/APPLaunch" ]; then
    cp -a "$REPO/projects/AppStore/dist/APPLaunch/." "$OUT/payload/share/"
fi
# Not in the Pi bundle (install.sh removes them from an earlier install: RETIRED_FILES there):
#   share/images/logo_lcd.png   the stock device's start-up logo, shown only by the stock start-up path
#   adb/                        the stock USB ADB gadget helpers (no ADB on the Pi builds: Settings has no Developer page)
rm -f "$OUT/payload/share/share/images/logo_lcd.png"
rm -rf "$OUT/payload/share/adb"
for file in cyberdeckzero.service launcher-ntp-default.service waveshare-pwm-backlight.dts \
            90-backlight-unblank.rules 91-applaunch-vkbd.rules 93-applaunch-rfkill.rules \
            50-networkmanager-netdev.rules 51-launcher-time-power.rules \
            cyberdeckzero-console.service cyberdeckzero-desktop.service 52-cyberdeckzero-desktop.rules \
            cyberdeckzero.desktop; do
    cp "$HERE/$file" "$OUT/payload/etc/"
done
# the text console helper the units run (install.sh puts it in /usr/share/APPLaunch/bin)
cp "$HERE/cyberdeckzero-console" "$OUT/payload/bin/"
chmod +x "$OUT/payload/bin/cyberdeckzero-console"
( cd "$OUT/payload/share" && find . -type f | sed 's|^\./||' | LC_ALL=C sort ) > "$OUT/payload/share.manifest"   # for uninstall.sh

# Licences and notices (install.sh puts them in /usr/share/doc/cyberdeckzero-launcher/): the project's LICENSE, the
# licence texts of LICENSES/, the component inventory, the LICENSE files of the components in ext_components/, the
# LVGL and FreeType texts, the licence files of the fonts in the bundle, and the Store's LICENSE when it is built.
DOC="$OUT/payload/doc"
mkdir -p "$DOC/LICENSES" "$DOC/components" "$DOC/fonts"
cp "$REPO/LICENSE" "$DOC/"
cp "$REPO/LICENSES/"* "$DOC/LICENSES/"
for f in "$REPO/docs/OPEN_SOURCE_COMPONENTS.md" "$REPO/ext_components/THIRD_PARTY_NOTICES.md"; do
    if [ -f "$f" ]; then cp "$f" "$DOC/"; fi
done
for f in "$REPO"/ext_components/*/LICENSE; do
    [ -f "$f" ] || continue
    name=$(basename "$(dirname "$f")")
    cp "$f" "$DOC/components/$name-LICENSE"
done
for f in "$REPO/projects/APPLaunch/main/lvgl/LVGL-LICENSE.txt" "$REPO/projects/APPLaunch/main/lvgl/freetype-LICENSE.txt"; do
    if [ -f "$f" ]; then cp "$f" "$DOC/components/"; fi
done
for f in "$OUT/payload/share/share/font"/*; do
    case "${f##*/}" in *OFL*|*LICENSE*|*LICENCE*|*COPYING*) cp "$f" "$DOC/fonts/" ;; esac
done
if [ "$WITH_STORE" = 1 ] && [ -f "$REPO/projects/AppStore/LICENSE" ]; then
    cp "$REPO/projects/AppStore/LICENSE" "$DOC/components/AppStore-LICENSE"
fi
chmod -R u=rwX,go=rX "$DOC"

cp "$HERE/install.sh" "$HERE/uninstall.sh" "$HERE/default-sources.sh" "$HERE/config.txt.snippet" "$HERE/README.md" "$OUT/"
chmod +x "$OUT/install.sh" "$OUT/uninstall.sh" "$OUT/default-sources.sh"

# one tarball per build: no stale file of another version (or of the earlier name) next to it
rm -f "$HERE"/cyberdeckzero-launcher-*-arm64.tar.gz "$HERE/$LEGACY_TARBALL"
tar -C "$HERE" -czf "$HERE/$TARBALL" bundle
echo "== done: $HERE/$TARBALL"
if [ "$LEGACY_NAME" = 1 ]; then
    cp "$HERE/$TARBALL" "$HERE/$LEGACY_TARBALL"
    echo "   same file as $HERE/$LEGACY_TARBALL (the quick-start name before the rename; drop with --no-legacy-name)"
fi
