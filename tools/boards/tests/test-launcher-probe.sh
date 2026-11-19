#!/bin/sh
# SPDX-License-Identifier: MIT
#
# PC test of tools/boards/launcher-probe against recorded fixture trees (fake /proc, /sys, /boot, /etc below
# fixtures/<name>/root, passed as PROBE_ROOT) and golden reports (golden/<name>.txt). Changes nothing outside a temp
# dir, needs no root, no board, no network. Run (WSL or Linux, from an LF checkout, see tools/dev/README.md):
#
#   sh tools/boards/tests/test-launcher-probe.sh                 # compare with the golden files
#   UPDATE_GOLDEN=1 sh tools/boards/tests/test-launcher-probe.sh   # rewrite the golden files, then read the diff
#   python3 tools/boards/tests/gen-launcher-probe-fixtures.py      # rebuild the fixture trees
#
# Prints "ok" or "FAIL" per check and "N passed, M failed" at the end; exit status 0 only when nothing failed.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
PROBE="$HERE/../launcher-probe"
FIX="$HERE/fixtures"
GOLD="$HERE/golden"
REPO=$(cd "$HERE/../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0
FAIL=0
ok() { PASS=$((PASS + 1)); printf 'ok   %s\n' "$1"; }
ko() { FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$1"; [ -z "${2:-}" ] || printf '%s\n' "$2" | sed 's/^/     | /' | head -n 30; }
check() { if eval "$2"; then ok "$1"; else ko "$1" "${3:-}"; fi; }
has() { case "$OUT" in *"$1"*) return 0 ;; esac; return 1; }

# run the probe on fixture $1 with shell $2 (sh or bash); extra options after; output in $OUT, status in $RC
run() {
    name=$1; shell=$2; shift 2
    # shellcheck disable=SC1090
    . "$FIX/$name/probe.env"
    OUT=$(PROBE_ROOT="$FIX/$name/root" PROBE_MACHINE=$PROBE_MACHINE PROBE_USER=$PROBE_USER PROBE_DATE=$PROBE_DATE "$shell" "$PROBE" "$@" 2>"$TMP/err")
    RC=$?
}
tree_sum() { (cd "$FIX/$1" && find . -type f | sort | while read -r f; do cksum < "$f"; done | cksum); }

NAMES="pi4b-dsi-portrait pi3a-wsdsi43-forced hackberry-720 pc-hdmi-edid unknown-empty"
mkdir -p "$GOLD"

for n in $NAMES; do
    before=$(tree_sum "$n")
    run "$n" sh
    check "$n: exit status 0" '[ $RC = 0 ]'
    check "$n: nothing on stderr" '[ ! -s "$TMP/err" ]' "$(cat "$TMP/err")"
    if [ "${UPDATE_GOLDEN:-0}" = 1 ]; then printf '%s\n' "$OUT" > "$GOLD/$n.txt"; fi
    printf '%s\n' "$OUT" > "$TMP/$n.out"
    check "$n: equals golden/$n.txt" 'cmp -s "$TMP/$n.out" "$GOLD/$n.txt"' "$(diff "$GOLD/$n.txt" "$TMP/$n.out" 2>&1 | head -n 20)"
    check "$n: 11 delimited sections" '[ "$(printf "%s\n" "$OUT" | grep -c "^==== ")" = 11 ]'
    check "$n: ends with the end marker" 'printf "%s\n" "$OUT" | tail -n 1 | grep -q "^==== END OF REPORT ===="'
    check "$n: fixture tree unchanged (read only)" '[ "$(tree_sum "$n")" = "$before" ]'
    # redaction: nothing that identifies the board or the owner
    leaks=$(printf '%s\n' "$OUT" | grep -n -i -E 'probehost|pilot|hbuser|pcuser|deadbeef|cafebabe|sn4567890|ser-123456|never-print-this|aa:bb:cc|00:11:22|192\.168|10\.0\.0\.|(/c/|/mnt/)[A-Za-z]' || true)
    check "$n: nothing redactable leaks" '[ -z "$leaks" ]' "$leaks"
    check "$n: no shell error text in the report" '! printf "%s\n" "$OUT" | grep -q -E "(No such file|Permission denied|command not found|syntax error)"' "$(printf '%s\n' "$OUT" | grep -E '(No such file|Permission denied|command not found|syntax error)')"
    if command -v bash >/dev/null 2>&1; then
        run "$n" bash
        printf '%s\n' "$OUT" > "$TMP/$n.bash"
        check "$n: bash gives the same report as sh" 'cmp -s "$TMP/$n.out" "$TMP/$n.bash"'
    fi
done

# --- facts that must be in each report (the golden files say all of it; these say what matters)
run pi4b-dsi-portrait sh
check "pi4b portrait: model, SoC, kernel" 'has "device-tree model: Raspberry Pi 4 Model B Rev 1.5" && has "SoC: brcm,bcm2711" && has "kernel: 6.12.47+rpt-rpi-v8"'
check "pi4b portrait: DSI connector connected with 480x800" 'has "connector card1-DSI-1" && has "preferred (first listed): 480x800"'
check "pi4b portrait: framebuffer 480,800 16 bpp with RGB565 layout" 'has "size: 480,800  bpp: 16  stride: 960" && has "red 11/5 green 5/6 blue 0/5"'
check "pi4b portrait: panel node from the device tree with mm" 'has "compatible: waveshare,4.0inch-dsi" && has "width-mm: 52" && has "height-mm: 86"'
check "pi4b portrait: config.txt overlay line, no comments" 'has "dtoverlay=vc4-kms-dsi-waveshare-panel,4_0_inch" && ! has "rptl.io"'
check "pi4b portrait: Goodix touch ranges and 5 slots" 'has "ABS MT_POSITION_Y   min 0 max 799" && has "slots (ABS_MT_SLOT max + 1): 5" && has "INPUT_PROP_DIRECT"'
check "pi4b portrait: touchscreen kind" 'has "kind: touchscreen"'
check "pi4b portrait: backlight levels, sysfs logic" 'has "levels (0..255)" && has "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/10-0045"'
check "pi4b portrait: kernel log lines about the panel" 'has "probing panel waveshare,4.0inch-dsi"'
check "pi4b portrait: install.sh says unknown board for 480x800" 'has "board: unknown" && has "--board generic"'
check "pi4b portrait: no launcher installed note" 'has "no launcher installed or running"'

run pi3a-wsdsi43-forced sh
check "pi3a forced overlay: Pi 3A+ and 800x480 DSI" 'has "Raspberry Pi 3 Model A Plus Rev 1.1" && has "preferred (first listed): 800x480"'
check "pi3a forced overlay: config.txt forces the Waveshare overlay, auto-detect off" 'has "dtoverlay=vc4-kms-dsi-waveshare-800x480" && has "display_auto_detect=0"'
check "pi3a forced overlay: the shared 7-inch node and its touch" 'has "compatible: raspberrypi,7inch-dsi" && has "compatible: edt,edt-ft5406" && has "slots (ABS_MT_SLOT max + 1): 10"'
check "pi3a forced overlay: fbdev driver mm 154x86 shown" 'has "driver mm 154x86"'
check "pi3a forced overlay: install.sh picks pi3a-wsdsi43" 'has "board: pi3a-wsdsi43"'
check "pi3a forced overlay: installed files and states shown, secrets dropped" 'has "--- /etc/applaunch/board.conf" && has "--- /run/user/UID/applaunch/screen.state" && has "touch_slots=10" && ! has "wifi_psk"'
check "pi3a forced overlay: launcher service names" 'has "user unit cyberdeckzero.service: /home/redacted/.config/systemd/user/cyberdeckzero.service" && has "user unit APPLaunch.service: not found" && has "system unit cyberdeckzero-desktop.service: not found" && has "system unit cyberdeckzero-console.service: /etc/systemd/system/cyberdeckzero-console.service"'
check "pi3a forced overlay: the launcher process is listed" 'has "display processes running: cyberdeckzero"'

run hackberry-720 sh
check "hackberry: CM5 and DPI 720x720" 'has "Compute Module 5" && has "card0-DPI-1" && has "size: 720,720  bpp: 32"'
check "hackberry: desktop session with the desktop unit" 'has "a desktop or compositor owns the display" && has "system unit cyberdeckzero-desktop.service: /etc/systemd/system/cyberdeckzero-desktop.service" && has "display manager: enabled"'
check "hackberry: touch 0..719 with 2 slots, keyboard and trackpoint kinds" 'has "ABS X               min 0 max 719" && has "slots (ABS_MT_SLOT max + 1): 2" && has "kind: keyboard" && has "kind: mouse-or-trackpoint"'
check "hackberry: no backlight" 'has "none: no backlight class device"'
check "hackberry: install.sh picks hackberry-cm5 and a desktop session" 'has "board: hackberry-cm5" && has "session: desktop"'
check "hackberry: overlay from config.txt" 'has "dtoverlay=hackberrypicm5"'

run pc-hdmi-edid sh
check "pc: DMI identity and x86_64" 'has "dmi: FixtureCorp / Fixture Desktop 100, chassis type 3" && has "arch: x86_64" && has "dmi serial: redacted"'
check "pc: EDID decoded (vendor, size in mm, preferred timing, name)" 'has "vendor FIX, product code 0x1234" && has "image size 598 x 336 mm" && has "preferred (first detailed) timing: 1920x1080" && has "monitor name: FIXTURE-27" && has "EDID checksum ok" && has "60 x 34 cm"'
check "pc: serial of the EDID is redacted" 'has "serial redacted"'
check "pc: disconnected connectors have no EDID" 'has "status: disconnected"'
check "pc: no device tree is said, not an error" 'has "unavailable: no /proc/device-tree"'
check "pc: boot config unavailable on a PC" 'has "unavailable: no config.txt"'
check "pc: intel_backlight levels" 'has "intel_backlight: type raw, max 96000"'
check "pc: install.sh picks a pc- profile" 'has "board: pc-fixture-desktop-100"'
check "pc: X desktop detected" 'has "a desktop or compositor owns the display"'
run pc-hdmi-edid sh --edid-hex
check "pc --edid-hex: hex printed, serial bytes and text blanked" 'has "00 ff ff ff ff ff ff 00 19 38 34 12 00 00 00 00" && ! has "be ba fe ca" && ! has "ca fe ba be" && ! has "53 4e 34"'

run unknown-empty sh
check "unknown: every section says unavailable instead of failing" '[ "$(printf "%s\n" "$OUT" | grep -c "^unavailable:")" -ge 10 ]'
check "unknown: install.sh says unknown board" 'has "board: unknown"'
check "unknown: exit 0" '[ $RC = 0 ]'

# --- options
run hackberry-720 sh --out "$TMP/saved.txt"
check "--out: the file is written" '[ -s "$TMP/saved.txt" ] && has "(saved to $TMP/saved.txt)"'
printf '%s\n' "$OUT" | grep -v '^(saved to ' > "$TMP/stdout.txt"
check "--out: the file equals what was printed" 'cmp -s "$TMP/saved.txt" "$TMP/stdout.txt"' "$(diff "$TMP/saved.txt" "$TMP/stdout.txt" | head)"
run hackberry-720 sh --out "$TMP/no-such-dir/x.txt"
check "--out: an unwritable file is said, exit 0" '[ $RC = 0 ] && has "unavailable: cannot write"'
. "$FIX/hackberry-720/probe.env"
PROBE_ROOT="$FIX/hackberry-720/root" PROBE_MACHINE=aarch64 sh "$PROBE" --bogus >/dev/null 2>&1; rc=$?
check "an unknown option: exit status 2" '[ $rc = 2 ]'
PROBE_ROOT="$FIX/hackberry-720/root" sh "$PROBE" --out >/dev/null 2>&1; rc=$?
check "--out without a file: exit status 2" '[ $rc = 2 ]'
OUT=$(sh "$PROBE" -h 2>&1); rc=$?
check "-h: usage, exit 0" '[ $rc = 0 ] && has "launcher-probe" && has "PROBE_ROOT"'
run hackberry-720 sh --installer "$TMP/none.sh"
check "--installer with a missing file: unavailable, not an abort" 'has "unavailable: install.sh not found"'
OUT=$(PROBE_ROOT="$FIX/hackberry-720/root" PROBE_MACHINE=aarch64 PROBE_USER=hbuser sh "$PROBE" --installer "$TMP/none.sh" 2>&1)
check "9 section: nothing written by the dry run into the fixture root" '[ ! -e "$FIX/hackberry-720/root/etc/udev" ]'

# --- the probe without PROBE_ROOT on this machine: runs, exits 0, has every section, no stray files
before=$(ls -A "$TMP" | wc -l)
OUT=$(sh "$PROBE" 2>&1); rc=$?
check "live run on this machine: exit status 0" '[ $rc = 0 ]'
check "live run on this machine: 11 sections" '[ "$(printf "%s\n" "$OUT" | grep -c "^==== ")" = 11 ]'
check "live run on this machine: no password words or MAC-like text" '! printf "%s\n" "$OUT" | grep -q -i -E "wifi_psk|password|([0-9a-f]{2}:){5}[0-9a-f]{2}"'
check "live run on this machine: no IPv4 address" '! printf "%s\n" "$OUT" | grep -q -E "[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}"' "$(printf '%s\n' "$OUT" | grep -E '[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}')"
check "live run on this machine: leaves no file in the temp dir" '[ "$(ls -A "$TMP" | wc -l)" = "$before" ]'

# --- the other probe tools name the services that exist
for f in "$REPO/projects/APPLaunch/tools/board-probe/probe-board.sh" "$REPO/projects/APPLaunch/tools/board-probe/fb-test-pattern.py" "$REPO/projects/APPLaunch/tools/board-probe/README.md"; do
    bad=$(grep -n 'APPLaunch\.service' "$f" | grep -v -E 'v0\.7\.0|v0\.6\.0|earlier name|old name|before the rename' || true)
    check "$(basename "$f"): the launcher service is cyberdeckzero.service (the old name only as a note)" '[ -z "$bad" ] && grep -q "cyberdeckzero\.service" "$f"' "$bad"
done
check "probe-board.sh looks for the desktop unit too" 'grep -q "cyberdeckzero-desktop.service" "$REPO/projects/APPLaunch/tools/board-probe/probe-board.sh"'

# --- shellcheck, when it is installed
if command -v shellcheck >/dev/null 2>&1; then
    check "shellcheck: launcher-probe and this test are clean" 'shellcheck -s sh "$PROBE" "$0"' "$(shellcheck -s sh "$PROBE" "$0" 2>&1 | head -n 30)"
else
    echo "skip shellcheck (not installed)"
fi

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ]
