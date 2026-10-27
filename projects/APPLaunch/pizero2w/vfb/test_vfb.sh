#!/bin/sh
# Host test of the virtual framebuffer (VFB2, task 014 P5-2): builds the shim, fbtest and test_vfb_file with the host
# compiler, then runs fbtest under LD_PRELOAD against
#   - a VFB2 file (64 KiB header): the pixels must land at +65536, ioctls and the page flip must work;
#   - a VFB1 file (4096-byte header, an older launcher's): still served, pixels at +4096;
#   - a file with a bad magic: the shim must refuse it (fbtest then fails to open the real /dev/fb0 or draws on it:
#     the test only asserts it exits without a crash signal).
# Linux (WSL) only. Run from anywhere: sh projects/APPLaunch/pizero2w/vfb/test_vfb.sh
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
inc=$here/../../../../ext_components/cp0_lvgl/include
out=${TMPDIR:-/tmp}/applaunch-vfb-test
mkdir -p "$out"
cc=${HOST_CC:-gcc}
$cc -shared -fPIC -O2 -Wall -Wextra -Werror -o "$out/libapplaunch_vfb.so" "$here/applaunch_vfb_shim.c" -I"$inc" -ldl
$cc -O2 -Wall -Wextra -o "$out/fbtest" "$here/fbtest.c"
$cc -O2 -Wall -Wextra -Werror -o "$out/test_vfb_file" "$here/test_vfb_file.c" -I"$inc"

frames=4
run_fbtest() {
    APPLAUNCH_VFB=$1 FBTEST_FRAMES=$frames LD_PRELOAD="$out/libapplaunch_vfb.so" "$out/fbtest" 2>"$out/fbtest.err"
}

echo "== VFB2"
"$out/test_vfb_file" create2 "$out/vfb2"
run_fbtest "$out/vfb2"
cat "$out/fbtest.err"
grep -q '^fb: 320x170 virt 320x170 bpp=32 stride=1280 smem=217600$' "$out/fbtest.err"
grep -q '^fb flip: virt 320x340 smem=435200$' "$out/fbtest.err"
"$out/test_vfb_file" check "$out/vfb2" $frames | tee "$out/check.out"
grep -q 'magic VFB2, pixel data at +65536' "$out/check.out"

echo "== VFB1 (a file left by an older launcher)"
"$out/test_vfb_file" create1 "$out/vfb1"
run_fbtest "$out/vfb1"
"$out/test_vfb_file" check "$out/vfb1" $frames | tee "$out/check.out"
grep -q 'magic VFB1, pixel data at +4096' "$out/check.out"

echo "== bad magic: refused, no crash"
"$out/test_vfb_file" createbad "$out/vfbbad"
rc=0
run_fbtest "$out/vfbbad" || rc=$?
if [ "$rc" -ge 128 ]; then
    echo "FAIL: fbtest died with signal $((rc - 128)) on a bad file" >&2
    exit 1
fi
if ! od -A d -t x1 -j 32 -N 4 "$out/vfbbad" | head -1 | grep -q ' 00 00 00 00'; then
    echo "FAIL: the shim attached to a file with a bad magic" >&2
    exit 1
fi
echo "bad magic: fbtest exit $rc, file untouched"

echo "test_vfb.sh: PASS"
