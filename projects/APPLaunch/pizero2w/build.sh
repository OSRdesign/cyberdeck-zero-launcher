#!/bin/sh
# Build CyberdeckZero Launcher for the Raspberry Pi boards (arm64) or for PCs (amd64) and assemble an install bundle.
#
# Run on a Linux x86_64 host (or WSL) from anywhere inside the repository:
#     projects/APPLaunch/pizero2w/build.sh [--arch arm64|amd64] [--with-store] [--no-legacy-name]
# Output: projects/APPLaunch/pizero2w/bundle/  and  cyberdeckzero-launcher-<version>-<arch>.tar.gz next to this script
# (<version>: APPLAUNCH_VERSION, default "dev"; <arch>: arm64 by default), plus, for arm64, a copy of that file named
# pizero2w-bundle.tar.gz, the quick-start name of the releases before the rename (wanted for the first release after it
# only: --no-legacy-name, or LEGACY_BUNDLE_NAME=0 in the environment, leaves it out).
# Copy the tarball to the board, unpack it and run  sudo ./install.sh   (to remove it again: ./uninstall.sh)
#
# Needs: git, python3 (venv), network access for the first build (the SDK downloads LVGL and prebuilt libraries), and
#   arm64 (default): an aarch64 cross toolchain (apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu pkg-config
#     libffi-dev libfreetype6-dev);
#   amd64 (a native build on an x86_64 host, report 028): gcc, g++, pkg-config and the -dev packages (apt install
#     libinput-dev libxkbcommon-dev libudev-dev libdbus-1-dev libglib2.0-dev libfreetype-dev, and libssl-dev for the
#     Store). Its binaries need glibc 2.38 or newer, like the arm64 ones.
# --with-store also builds the Store (projects/AppStore submodule; amd64: with its own cross config minus NEON).
set -eu

WITH_STORE=0
LEGACY_NAME=${LEGACY_BUNDLE_NAME:-1}
ARCH=arm64
while [ $# -gt 0 ]; do
    case "$1" in
        --with-store) WITH_STORE=1 ;;
        --no-legacy-name) LEGACY_NAME=0 ;;
        --arch) [ $# -ge 2 ] || { echo "--arch needs arm64 or amd64" >&2; exit 2; }; ARCH=$2; shift ;;
        --arch=*) ARCH=${1#--arch=} ;;
        -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
case "$ARCH" in
    arm64|amd64) ;;
    *) echo "--arch $ARCH: arm64 or amd64" >&2; exit 2 ;;
esac

# The version goes into the tarball's name (and, through SConstruct, into About: a semantic version there).
VERSION=${APPLAUNCH_VERSION:-dev}
case "$VERSION" in
    ""|*[!A-Za-z0-9.+-]*) echo "APPLAUNCH_VERSION \"$VERSION\": letters, digits, '.', '+' and '-' only" >&2; exit 2 ;;
esac
TARBALL="cyberdeckzero-launcher-$VERSION-$ARCH.tar.gz"
LEGACY_TARBALL="pizero2w-bundle.tar.gz"   # arm64 only
[ "$ARCH" = arm64 ] || LEGACY_NAME=0

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
OUT="$HERE/bundle"

if [ "$ARCH" = arm64 ]; then
    command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || {
        echo "aarch64-linux-gnu-gcc not found: sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu" >&2
        exit 1
    }
    CONFIG=linux_x86_cross_cp0_config_defaults.mk
    VFB_CC=aarch64-linux-gnu-gcc
else
    # amd64 is a native build: the host's gcc and -dev packages (found by pkg-config)
    [ "$(uname -m)" = x86_64 ] || { echo "--arch amd64 builds natively: run it on an x86_64 host" >&2; exit 1; }
    MODS="libinput xkbcommon libudev dbus-1 gio-2.0 freetype2"
    [ "$WITH_STORE" = 1 ] && MODS="$MODS openssl"
    # shellcheck disable=SC2086
    if ! command -v gcc >/dev/null 2>&1 || ! command -v g++ >/dev/null 2>&1 || ! command -v pkg-config >/dev/null 2>&1 \
            || ! pkg-config --exists $MODS; then
        echo "amd64 build: gcc, g++, pkg-config and the -dev packages are needed (pkg-config: $MODS):" >&2
        echo "  sudo apt install gcc g++ pkg-config libinput-dev libxkbcommon-dev libudev-dev libdbus-1-dev libglib2.0-dev libfreetype-dev libssl-dev" >&2
        exit 1
    fi
    CONFIG=linux_x86_64_native_cp0_config_defaults.mk
    VFB_CC=${CC:-gcc}
fi

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
export CONFIG_DEFAULT_FILE=$CONFIG
# A bundle is a release build unless the caller sets the channel: development-only pages (System > Keyboard test) stay
# hidden on devices, and About / System > Channel reads "release". tools/dev/wsl-build.sh passes "development".
export APPLAUNCH_CHANNEL="${APPLAUNCH_CHANNEL:-release}"

echo "== building the launcher"
( cd "$REPO/projects/APPLaunch" && scons -j"$(nproc)" )

if [ "$WITH_STORE" = 1 ]; then
    echo "== building the Store"
    if [ "$ARCH" = arm64 ]; then
        ( cd "$REPO/projects/AppStore" && scons -j"$(nproc)" )
    else
        # The Store is the upstream submodule, unchanged: its own cross config without the aarch64 toolchain and the
        # NEON lines, written next to its build output (no "cross" in the name: no sysroot)
        mkdir -p "$REPO/projects/AppStore/build"
        STORE_CONFIG="$REPO/projects/AppStore/build/amd64_native_config_defaults.mk"
        sed 's/\r$//' "$REPO/projects/AppStore/linux_x86_cross_cp0_config_defaults.mk" \
            | grep -v -e '^CONFIG_TOOLCHAIN_PREFIX=' -e 'LV_DRAW_SW_ASM_NEON' -e 'LV_USE_DRAW_SW_ASM' > "$STORE_CONFIG"
        # its SConstruct takes FreeType's headers from the cross sysroot only: the build box's, as toolchain flags
        FT_FLAGS=$(pkg-config --cflags freetype2)
        ( cd "$REPO/projects/AppStore" && CONFIG_DEFAULT_FILE="$STORE_CONFIG" \
            CONFIG_TOOLCHAIN_FLAGS="${CONFIG_TOOLCHAIN_FLAGS:+$CONFIG_TOOLCHAIN_FLAGS }$FT_FLAGS" scons -j"$(nproc)" )
    fi
fi

echo "== building the framebuffer shim ($ARCH)"
( cd "$HERE/vfb" && CC=$VFB_CC ./build.sh )

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
#   share/images/logo_lcd.png   the upstream start-up logo (no longer in the repository since report 029)
#   adb/                        the upstream USB ADB gadget helpers (no longer in the repository since report 029)
rm -f "$OUT/payload/share/share/images/logo_lcd.png"
rm -rf "$OUT/payload/share/adb"
for file in cyberdeckzero.service launcher-ntp-default.service waveshare-pwm-backlight.dts \
            hyperpixel4-pwm-backlight.dts \
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

# one tarball per build and arch: no stale file of another version (or of the earlier name) next to it
rm -f "$HERE"/cyberdeckzero-launcher-*-"$ARCH".tar.gz
if [ "$ARCH" = arm64 ]; then rm -f "$HERE/$LEGACY_TARBALL"; fi
tar -C "$HERE" -czf "$HERE/$TARBALL" bundle
echo "== done: $HERE/$TARBALL"
if [ "$LEGACY_NAME" = 1 ]; then
    cp "$HERE/$TARBALL" "$HERE/$LEGACY_TARBALL"
    echo "   same file as $HERE/$LEGACY_TARBALL (the quick-start name before the rename; drop with --no-legacy-name)"
fi
