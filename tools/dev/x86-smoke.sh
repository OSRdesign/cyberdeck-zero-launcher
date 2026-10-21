#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# x86-smoke.sh: start the amd64 launcher headless in WSL on a virtual framebuffer and save what it drew (report 028,
# section 6: link and start-up failures, the pc profile's geometry). The binary of the amd64 build tree runs with the
# launcher's own vfb shim (LD_PRELOAD, a VFB2 file in /dev/shm standing in for /dev/fb0), its share/ seen at
# /usr/share/APPLaunch through a private overlay (unshare: no root, nothing changes on the build box), and the pc
# profile's environment (APPLAUNCH_DISPLAY=dpi-scaled, ...). Prints the launcher's display lines, whether it still
# runs, its peak RSS, and writes a PNG of the framebuffer.
#
#   tools/dev/x86-smoke.sh [--build-dir DIR] [--size WxH] [--panel-mm WxH] [--seconds N] [--out FILE] [ENV=VALUE ...]
#
#   --build-dir DIR  the amd64 build tree (default ~/zero7/<checkout folder name>-amd64, as wsl-build.sh --arch amd64)
#   --size WxH       the framebuffer (default 1280x800: the FZ-M1 tablet)
#   --panel-mm WxH   APPLAUNCH_PANEL_MM (default 150x93)
#   --seconds N      run time before the shot (default 12)
#   --out FILE       the PNG (default /tmp/x86-smoke-<W>x<H>.png); its log next to it (.log)
#   ENV=VALUE        more launcher environment (e.g. APPLAUNCH_OSK=force)
#   LD_LIBRARY_PATH from the caller goes through (a build box without libinput10 and the other runtime libraries).
#
# Runs in WSL; started from Git Bash on Windows it re-runs itself in WSL. Exit status 0 when the launcher still ran at
# the shot and the PNG was written.
set -euo pipefail

usage() { awk 'NR > 3 && /^#/ { sub(/^# ?/, ""); print; next } NR > 3 { exit }' "$0"; }
for a in "$@"; do
    case $a in -h | --help) usage; exit 0 ;; esac
done

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=lib.sh
. "$here/lib.sh"
reexec_in_wsl "${BASH_SOURCE[0]}" "$@"

build_dir=$(default_build_dir)-amd64
size=1280x800
panel_mm=150x93
seconds=12
out=
extra=()
while (($#)); do
    case $1 in
        --build-dir) build_dir=${2:?--build-dir needs a directory}; shift 2 ;;
        --size) size=${2:?--size needs WxH}; shift 2 ;;
        --panel-mm) panel_mm=${2:?--panel-mm needs WxH}; shift 2 ;;
        --seconds) seconds=${2:?--seconds needs a number}; shift 2 ;;
        --out) out=${2:?--out needs a file}; shift 2 ;;
        *=*) extra+=("$1"); shift ;;
        *) echo "unknown option $1 (see -h)" >&2; exit 2 ;;
    esac
done
[[ $size =~ ^([0-9]+)x([0-9]+)$ ]] || { echo "--size $size: WxH" >&2; exit 2; }
w=${BASH_REMATCH[1]} h=${BASH_REMATCH[2]}
out=${out:-/tmp/x86-smoke-${w}x${h}.png}
dist=$build_dir/projects/APPLaunch/dist
shim=$build_dir/projects/APPLaunch/pizero2w/vfb/libapplaunch_vfb.so
[[ -x $dist/cyberdeckzero && -d $dist/APPLaunch ]] || { echo "no amd64 build in $dist (tools/dev/wsl-build.sh --arch amd64)" >&2; exit 2; }
file -b "$dist/cyberdeckzero" | grep -q x86-64 || { echo "$dist/cyberdeckzero is not an x86-64 binary" >&2; exit 2; }
[[ -f $shim ]] || { echo "no vfb shim at $shim" >&2; exit 2; }

work=$(mktemp -d)
vfb=/dev/shm/x86-smoke-$$
cleanup() { rm -rf "$work"; rm -f "$vfb"; }
trap cleanup EXIT
mkdir -p "$work/upper" "$work/ovl" "$work/home/run"
chmod 700 "$work/home/run"

# the VFB2 file (include/applaunch_vfb.h): the header, then w x h XRGB8888 pixels
python3 - "$vfb" "$w" "$h" <<'PY'
import struct, sys
path, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
header = 65536
with open(path, "wb") as f:
    f.truncate(header + w * 4 * h)
    # magic width height bpp stride yres_virtual yoffset frame attached | version header_bytes host_w host_h
    # win_x win_y win_w win_h scale ppmm canvas_mode kb_request kb_inset gen
    f.write(struct.pack("<9I", 0x32424656, w, h, 32, w * 4, h, 0, 0, 0))
    f.write(struct.pack("<4I4i6I", 2, header, w, h, 0, 0, w, h, 1, 0, 0, 0, 0, 0))
PY

env_args=(HOME="$work/home" XDG_RUNTIME_DIR="$work/home/run" LD_PRELOAD="$shim" APPLAUNCH_VFB="$vfb"
          APPLAUNCH_DISPLAY=dpi-scaled APPLAUNCH_BOARD=pc-smoke APPLAUNCH_FB=/dev/fb0 LV_LINUX_FBDEV_DEVICE=/dev/fb0
          APPLAUNCH_LOGICAL="${w}x${h}" APPLAUNCH_ROTATE=0 APPLAUNCH_PANEL_MM="$panel_mm" APPLAUNCH_TOUCH_DEV=auto
          APPLAUNCH_TOUCH_ORIENT=buffer APPLAUNCH_KBD_IGNORE=bus:0011 APPLAUNCH_HIDE_TILES=app_Store
          APPLAUNCH_STD_KEYBOARD=1 ${LD_LIBRARY_PATH:+LD_LIBRARY_PATH="$LD_LIBRARY_PATH"} "${extra[@]}")
log=${out%.png}.log
# a private mount namespace: /usr/share as an overlay with APPLaunch -> the build's dist/APPLaunch
unshare -rm sh -c 'mount -t overlay overlay -o "lowerdir=/usr/share,upperdir=$1/upper,workdir=$1/ovl" /usr/share &&
    ln -s "$2/APPLaunch" /usr/share/APPLaunch && cd "$2" && shift 2 && exec env "$@" ./cyberdeckzero' \
    sh "$work" "$dist" "${env_args[@]}" >"$log" 2>&1 &
pid=$!
peak=0
for ((i = 0; i < seconds * 2; i++)); do
    sleep 0.5
    kill -0 "$pid" 2>/dev/null || break
    child=$(pgrep -P "$pid" -x cyberdeckzero || echo "$pid")
    rss=$(awk '/^VmRSS:/ { print $2 }' "/proc/$child/status" 2>/dev/null || echo 0)
    ((rss > peak)) && peak=$rss
done
running=0
if kill -0 "$pid" 2>/dev/null; then running=1; kill "$pid" 2>/dev/null; sleep 1; kill -9 "$pid" 2>/dev/null || true; fi
wait "$pid" 2>/dev/null || true

python3 - "$vfb" "$w" "$h" "$out" <<'PY'
import struct, sys, zlib
path, w, h, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
with open(path, "rb") as f:
    f.seek(65536)
    data = f.read(w * 4 * h)
rows = bytearray()
for y in range(h):
    rows.append(0)
    line = data[y * w * 4:(y + 1) * w * 4]
    px = bytearray(w * 3)
    px[0::3], px[1::3], px[2::3] = line[2::4], line[1::4], line[0::4]   # XRGB8888 little endian: B G R X
    rows += px
def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
      chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b"")
open(out, "wb").write(png)
PY

grep -E '^\[(dpi|display-profile|display-metrics)\]|rror' "$log" | head -8 || true
echo "launcher $( ((running)) && echo 'still running at the shot' || echo 'had EXITED') after ${seconds}s, peak RSS ${peak} kB"
echo "png: $out   log: $log"
if [[ -n ${WSL_DISTRO_NAME:-} ]]; then echo "  from Windows: \\\\wsl.localhost\\$WSL_DISTRO_NAME${out//\//\\}"; fi
((running))
