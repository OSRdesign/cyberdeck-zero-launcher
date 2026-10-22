#!/bin/sh
# PC test of install.sh's board detection and profile handling, and of uninstall.sh's plan, against the fake
# /proc and /sys trees in fixtures/ (SYSROOT and UNINSTALL_ROOT test hooks), of default-sources.sh (the default
# app source) against a temp state folder and a fake Store backend, and of a whole install, the migration from the
# names of v0.6.0 and before, the switch between the appliance and the desktop variant (a desktop installed or removed
# later), and a whole uninstall into a fake root (INSTALL_FAKE_SYSTEM=1, stand-ins for systemctl,
# runuser, sudo and the other system commands first on PATH). Changes nothing outside a temp dir, needs no root and
# no Pi. Run:   sh projects/APPLaunch/pizero2w/tests/test-install-board.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
INSTALL="$HERE/../install.sh"
UNINSTALL="$HERE/../uninstall.sh"
FIX="$HERE/fixtures"
# branding-allow-begin: the launcher binary's name of v0.6.0 and before (the migration keeps it as a link)
OLDBIN=M5CardputerZero-APPLaunch
# branding-allow-end
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export INSTALL_GLIBC_VERSION=2.41
PASS=0
FAIL=0
ok() { PASS=$((PASS + 1)); printf 'ok   %s\n' "$1"; }
ko() { FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$1"; [ -z "${2:-}" ] || printf '%s\n' "$2" | sed 's/^/     | /'; }
# fresh copy of a fixture
root() { rm -rf "$TMP/$1"; cp -R "$FIX/$1" "$TMP/$1"; echo "$TMP/$1"; }
# run install.sh; output in $OUT, exit code in $RC
run() { OUT=$(SYSROOT="$R" sh "$INSTALL" "$@" 2>&1); RC=$?; }
has() { case "$OUT" in *"$1"*) return 0 ;; esac; return 1; }
check() { if eval "$2"; then ok "$1"; else ko "$1" "$OUT"; fi; }

# --- deck: detected, no profile, no gpio rule, PWM boot config as before
R=$(root zero2w)
run --dry-run
check "zero2w dry-run: exit 0" '[ $RC = 0 ]'
check "zero2w dry-run: board zero2w" 'has "board: zero2w"'
check "zero2w dry-run: no profile file" 'has "zero2w: no profile file"'
check "zero2w dry-run: no gpio backlight rule" '! has "92-applaunch-backlight-gpio" && ! has "92-applaunch-backlight-sysfs"'
check "zero2w dry-run: PWM boot config as today" 'has "boot config: PWM backlight overlay"'
check "zero2w dry-run: nothing written" '[ ! -e "$R/etc" ]'
run --board-only
check "zero2w board-only: no profile written" '[ $RC = 0 ] && [ ! -e "$R/etc/applaunch/board.conf" ] && [ ! -e "$R/etc/udev" ]'

# --- Pi 3A+ with the Luckfox 3.5" panel
R=$(root pi3a-luckfox35)
run --dry-run
check "pi3a dry-run: exit 0" '[ $RC = 0 ]'
check "pi3a dry-run: board pi3a-luckfox35" 'has "board: pi3a-luckfox35"'
check "pi3a dry-run: fb1 picked" 'has "APPLAUNCH_FB=/dev/fb1"'
check "pi3a dry-run: panel size for the layout service" 'has "APPLAUNCH_PANEL_MM=79x49"'
check "pi3a dry-run: gpio backlight rule planned" 'has "92-applaunch-backlight-gpio.rules"'
check "pi3a dry-run: no PWM overlay" 'has "cmdline cursor/consoleblank only"'
check "pi3a dry-run: nothing written" '[ ! -e "$R/etc" ]'
run --board-only
P="$R/etc/applaunch/board.conf"
check "pi3a board-only: profile written" '[ $RC = 0 ] && [ -f "$P" ]'
check "pi3a board-only: gpio rule written" '[ -f "$R/etc/udev/rules.d/92-applaunch-backlight-gpio.rules" ]'
EXPECT='APPLAUNCH_BOARD=pi3a-luckfox35
APPLAUNCH_FB=/dev/fb1
APPLAUNCH_LOGICAL=480x320
APPLAUNCH_ROTATE=90
APPLAUNCH_COMPAT_SCALE=1
APPLAUNCH_PANEL_MM=79x49
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
APPLAUNCH_BACKLIGHT=gpio:/sys/class/backlight/backlight_gpio
LV_LINUX_FBDEV_DEVICE=/dev/fb1
APPLAUNCH_TOUCH_SWAP_XY=0
APPLAUNCH_TOUCH_INVERT_X=0
APPLAUNCH_TOUCH_INVERT_Y=0'
GOT=$(grep -v '^#' "$P")
if [ "$GOT" = "$EXPECT" ]; then ok "pi3a profile: exact keys and values"; else ko "pi3a profile: exact keys and values" "$GOT"; fi
# shellcheck disable=SC1090
GOT=$( ( set -a; . "$P"; echo "$APPLAUNCH_FB $APPLAUNCH_ROTATE $APPLAUNCH_TOUCH_ORIENT $APPLAUNCH_PANEL_MM" ) )
check "pi3a profile: valid sh syntax" '[ "$GOT" = "/dev/fb1 90 buffer 79x49" ]'
check "pi3a profile: only KEY=VALUE or comment lines" '! grep -v -E "^(#.*|[A-Z_][A-Z0-9_]*=[^ ]*)$" "$P" >/dev/null'
SUM1=$(cksum < "$P")
run --board-only
check "pi3a second run: idempotent" '[ $RC = 0 ] && has "already up to date" && [ "$(cksum < "$P")" = "$SUM1" ] && [ ! -e "$P.bak" ]'
sed -i 's/^APPLAUNCH_ROTATE=90/APPLAUNCH_ROTATE=270/' "$P"
EDITED=$(cksum < "$P")
run --board-only
check "pi3a hand-edited: kept without --force" 'has "edited by hand: kept" && [ "$(cksum < "$P")" = "$EDITED" ] && [ ! -e "$P.bak" ]'
run --board-only --force
check "pi3a hand-edited + --force: replaced, .bak kept" '[ "$(cksum < "$P")" = "$SUM1" ] && grep -q "^APPLAUNCH_ROTATE=270" "$P.bak"'

# --- the panel's framebuffer is found by name, not assumed to be fb1
R=$(root pi3a-luckfox35)
mv "$R/sys/class/graphics/fb1" "$R/sys/class/graphics/fb0"
run --dry-run
check "pi3a panel on fb0: APPLAUNCH_FB=/dev/fb0" 'has "APPLAUNCH_FB=/dev/fb0" && has "LV_LINUX_FBDEV_DEVICE=/dev/fb0"'

# --- 3A+ whose SPI connector is not connected: unknown
R=$(root pi3a-luckfox35)
echo disconnected > "$R/sys/class/drm/card0-SPI-1/status"
run --dry-run
check "pi3a without connected panel: unknown" 'has "board: unknown" && has "probe-board.sh"'

# --- Pi 3A+ with the Waveshare 4.3" DSI panel (dtoverlay=vc4-kms-dsi-waveshare-800x480): KMS framebuffer vc4drmfb
# 800x480 on fb0
R=$(root pi3a-wsdsi43)
run --dry-run
# the detection does not depend on the overlay's name: the same plan with either overlay in config.txt
mkdir -p "$R/boot/firmware"
DRY0=$OUT
printf 'dtoverlay=vc4-kms-v3d\ndtoverlay=vc4-kms-dsi-waveshare-800x480\n' > "$R/boot/firmware/config.txt"
run --dry-run; DRY1=$OUT
printf 'dtoverlay=vc4-kms-v3d\ndtoverlay=vc4-kms-dsi-7inch\n' > "$R/boot/firmware/config.txt"
run --dry-run; DRY2=$OUT
rm -rf "$R/boot"
check "wsdsi43 dry-run: the same plan whatever the DSI overlay in config.txt" '[ "$DRY0" = "$DRY1" ] && [ "$DRY1" = "$DRY2" ]'
OUT=$DRY0
check "wsdsi43 dry-run: exit 0, board pi3a-wsdsi43" '[ $RC = 0 ] && has "board: pi3a-wsdsi43" && ! has "board: pi3a-luckfox35"'
check "wsdsi43 dry-run: fb0 and the DSI connector seen" 'has "drm card0-DSI-1: connected, first mode 800x480" && has "/dev/fb0: name \"vc4drmfb\", size 800,480, 16 bpp" && has "backlight: 10-0045"'
check "wsdsi43 dry-run: profile planned, backlight of the DSI regulator" 'has "pi3a-wsdsi43: write" && has "APPLAUNCH_FB=/dev/fb0" && has "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/10-0045"'
check "wsdsi43 dry-run: config.txt left alone, cmdline arguments only, no gpio rule" 'has "boot config: cmdline cursor/consoleblank only" && ! has "PWM backlight overlay +" && ! has "92-applaunch-backlight-gpio"'
check "wsdsi43 dry-run: nothing written" '[ ! -e "$R/etc" ]'
run --board-only
P="$R/etc/applaunch/board.conf"
check "wsdsi43 board-only: profile written, no gpio rule" '[ $RC = 0 ] && [ -f "$P" ] && [ ! -e "$R/etc/udev/rules.d/92-applaunch-backlight-gpio.rules" ]'
BLR="$R/etc/udev/rules.d/92-applaunch-backlight-sysfs.rules"
check "wsdsi43 board-only: udev rule lets the video group set the DSI backlight levels" '[ -f "$BLR" ] && grep -q "KERNEL==\"10-0045\"" "$BLR" && grep -q "chgrp video /sys/class/backlight/%k/brightness; chmod g+w /sys/class/backlight/%k/brightness" "$BLR"'
EXPECT='APPLAUNCH_BOARD=pi3a-wsdsi43
APPLAUNCH_FB=/dev/fb0
APPLAUNCH_LOGICAL=800x480
APPLAUNCH_ROTATE=0
APPLAUNCH_COMPAT_SCALE=2
APPLAUNCH_PANEL_MM=94x56
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/10-0045
LV_LINUX_FBDEV_DEVICE=/dev/fb0
APPLAUNCH_TOUCH_SWAP_XY=0
APPLAUNCH_TOUCH_INVERT_X=0
APPLAUNCH_TOUCH_INVERT_Y=0'
GOT=$(grep -v '^#' "$P")
if [ "$GOT" = "$EXPECT" ]; then ok "wsdsi43 profile: exact keys and values (the board's working profile)"; else ko "wsdsi43 profile: exact keys and values (the board's working profile)" "$GOT"; fi
# shellcheck disable=SC1090
GOT=$( ( set -a; . "$P"; echo "$APPLAUNCH_FB $APPLAUNCH_LOGICAL $APPLAUNCH_ROTATE $APPLAUNCH_COMPAT_SCALE $APPLAUNCH_BACKLIGHT" ) )
check "wsdsi43 profile: valid sh syntax" '[ "$GOT" = "/dev/fb0 800x480 0 2 sysfs:/sys/class/backlight/10-0045" ]'
check "wsdsi43 profile: only KEY=VALUE or comment lines, no fbcon" '! grep -v -E "^(#.*|[A-Z_][A-Z0-9_]*=[^ ]*)$" "$P" >/dev/null && ! grep -qi fbcon "$P"'
SUM1=$(cksum < "$P")
run --board-only
check "wsdsi43 second run: idempotent" '[ $RC = 0 ] && has "already up to date" && [ "$(cksum < "$P")" = "$SUM1" ] && [ ! -e "$P.bak" ]'
# the owner's hand-written profile (first line a comment, no install.sh checksum): kept, and the dry run says so
R=$(root pi3a-wsdsi43); P="$R/etc/applaunch/board.conf"; mkdir -p "$R/etc/applaunch"
cat > "$P" <<'EOF'
# written by hand for the Waveshare 4.3" DSI panel
APPLAUNCH_BOARD=pi3a-wsdsi43
APPLAUNCH_FB=/dev/fb0
APPLAUNCH_LOGICAL=800x480
APPLAUNCH_ROTATE=0
APPLAUNCH_COMPAT_SCALE=2
APPLAUNCH_PANEL_MM=94x56
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/10-0045
LV_LINUX_FBDEV_DEVICE=/dev/fb0
APPLAUNCH_TOUCH_SWAP_XY=0
APPLAUNCH_TOUCH_INVERT_X=0
APPLAUNCH_TOUCH_INVERT_Y=0
EOF
HAND=$(cksum < "$P")
run --dry-run
check "wsdsi43 hand-written profile: dry run says kept" '[ $RC = 0 ] && has "profile: pi3a-wsdsi43: $P was edited by hand: kept (use --force to replace it)" && ! has "--- $P"'
run --board-only
check "wsdsi43 hand-written profile: kept byte for byte, no .bak" 'has "edited by hand: kept" && [ "$(cksum < "$P")" = "$HAND" ] && [ ! -e "$P.bak" ]'
# the backlight device: the regulator 10-0045, else the only one there, else none (no line)
R=$(root pi3a-wsdsi43); mv "$R/sys/class/backlight/10-0045" "$R/sys/class/backlight/rpi_backlight"
run --dry-run
check "wsdsi43 with another single backlight device: that one" 'has "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/rpi_backlight" && has "92-applaunch-backlight-sysfs.rules (video group may set the brightness of rpi_backlight)"'
rm -rf "$R/sys/class/backlight"
run --dry-run
check "wsdsi43 without a backlight device: no APPLAUNCH_BACKLIGHT line, no backlight rule" '[ $RC = 0 ] && has "board: pi3a-wsdsi43" && ! has "APPLAUNCH_BACKLIGHT=" && ! has "92-applaunch-backlight-sysfs"'
# unambiguous against the SPI profile and against other DSI or HDMI set-ups
R=$(root pi3a-wsdsi43); echo disconnected > "$R/sys/class/drm/card0-DSI-1/status"
run --dry-run
check "wsdsi43 with the DSI connector disconnected: unknown, config.txt still left alone" 'has "board: unknown" && has "probe-board.sh" && has "boot config: cmdline cursor/consoleblank only"'
R=$(root pi3a-wsdsi43); echo 1024x600 > "$R/sys/class/drm/card0-DSI-1/modes"; echo 1024,600 > "$R/sys/class/graphics/fb0/virtual_size"
run --dry-run
check "wsdsi43 with a 1024x600 DSI panel: unknown" 'has "board: unknown"'
R=$(root pi3a-wsdsi43); echo 640,480 > "$R/sys/class/graphics/fb0/virtual_size"
run --dry-run
check "wsdsi43 with an 800x480 mode but another framebuffer size: unknown" 'has "board: unknown"'
R=$(root pi3a-wsdsi43); printf 'Raspberry Pi 4 Model B Rev 1.5\n' > "$R/proc/device-tree/model"
run --dry-run
check "the DSI panel on another model (Pi 4): unknown, the deck's boot config as before for an unknown model" 'has "board: unknown" && has "boot config: PWM backlight overlay"'
# both panels at once on a 3A+: not guessed, the reason given; --board chooses
R=$(root pi3a-wsdsi43)
cp -R "$FIX/pi3a-luckfox35/sys/class/drm/card0-SPI-1" "$R/sys/class/drm/card1-SPI-1"
cp -R "$FIX/pi3a-luckfox35/sys/class/graphics/fb1" "$R/sys/class/graphics/fb1"
run --dry-run
check "3A+ with both panels: unknown, both named, install would stop, no PWM overlay" '[ $RC = 0 ] && has "board: unknown" && has "two panels connected (SPI /dev/fb1: pi3a-luckfox35, DSI /dev/fb0: pi3a-wsdsi43): choose one with --board" && has "would stop here" && has "boot config: cmdline cursor/consoleblank only"'
run --dry-run --board pi3a-wsdsi43
check "3A+ with both panels, --board pi3a-wsdsi43: fb0" '[ $RC = 0 ] && has "board: pi3a-wsdsi43 (forced" && has "APPLAUNCH_FB=/dev/fb0"'
run --dry-run --board pi3a-luckfox35
check "3A+ with both panels, --board pi3a-luckfox35: fb1" '[ $RC = 0 ] && has "board: pi3a-luckfox35 (forced" && has "APPLAUNCH_FB=/dev/fb1"'
# each forced profile needs its own panel
R=$(root pi3a-wsdsi43)
run --dry-run --board pi3a-luckfox35
check "--board pi3a-luckfox35 on the DSI board: refused, exit 1" '[ $RC = 1 ] && has "no 320x480 \"panel-mipi-dbi\""'
R=$(root pi3a-luckfox35)
run --dry-run --board pi3a-wsdsi43
check "--board pi3a-wsdsi43 on the SPI board: refused, exit 1" '[ $RC = 1 ] && has "no 800x480 \"vc4drmfb\""'
check "the SPI board is still pi3a-luckfox35" 'run --dry-run; has "board: pi3a-luckfox35" && ! has "pi3a-wsdsi43"'
check "install.sh writes no fbcon= argument anywhere" '! grep -n "fbcon=" "$INSTALL" | grep -v "^[0-9]*:[[:space:]]*#" | grep -q .'

# --- unknown board (a CM5 uConsole: DSI panel, no profile): facts printed, nothing written, install stops
R=$(root unknown-cm5-uconsole)
run --dry-run
check "unknown dry-run: facts and probe advice" '[ $RC = 0 ] && has "board: unknown" && has "Compute Module 5" && has "drm card1-DSI-2: connected" && has "drm-rp1-dsidrmf" && has "probe-board.sh"'
check "unknown dry-run: says install would stop" 'has "would stop here"'
run --board-only
check "unknown board-only: nothing written" '[ $RC = 0 ] && [ ! -e "$R/etc" ]'

# --- forced profiles
R=$(root zero2w)
run --dry-run --board pi3a-luckfox35
check "--board pi3a on a deck (no panel fb): refused, exit 1" '[ $RC = 1 ] && has "no 320x480"'
run --dry-run --board nosuch
check "--board unknown name: exit 2" '[ $RC = 2 ]'
R=$(root pi3a-luckfox35)
run --dry-run --board zero2w
check "--board zero2w on a 3A+: no profile" 'has "board: zero2w (forced" && has "zero2w: no profile file"'
R=$(root unknown-cm5-uconsole)
mkdir -p "$R/sys/class/graphics/fb1" && cp "$FIX/pi3a-luckfox35/sys/class/graphics/fb1/"* "$R/sys/class/graphics/fb1/"
run --dry-run --board pi3a-luckfox35
check "--board pi3a with a matching fb: profile planned" '[ $RC = 0 ] && has "APPLAUNCH_FB=/dev/fb1"'
R=$(root pi5-hyperpixel4)
run --dry-run --board hackberry-cm5
check "--board hackberry-cm5 on a Pi 5 (no 720x720 fb): refused, exit 1" '[ $RC = 1 ] && has "no 720x720 \"drm-rp1-dpi\""'

# --- deck with a stale profile: generated one moved away, hand-edited one kept
R=$(root pi3a-luckfox35); run --board-only; STALE="$R/etc/applaunch/board.conf"
D=$(root zero2w); mkdir -p "$D/etc/applaunch"; cp "$STALE" "$D/etc/applaunch/board.conf"
R=$D; run --board-only
check "zero2w stale generated profile: moved to .bak" '[ ! -e "$R/etc/applaunch/board.conf" ] && [ -f "$R/etc/applaunch/board.conf.bak" ]'
D=$(root zero2w); mkdir -p "$D/etc/applaunch"; printf 'APPLAUNCH_BOARD=mine\n' > "$D/etc/applaunch/board.conf"
R=$D; run --board-only
check "zero2w hand-written profile: kept" '[ -f "$R/etc/applaunch/board.conf" ] && has "edited by hand: kept"'

# --- glibc floor
R=$(root pi3a-luckfox35)
OUT=$(INSTALL_GLIBC_VERSION=2.36 SYSROOT="$R" sh "$INSTALL" --board-only 2>&1); RC=$?
check "glibc 2.36: refused, exit 1, nothing written" '[ $RC = 1 ] && has "glibc 2.36" && has "2.38" && [ ! -e "$R/etc" ]'
OUT=$(INSTALL_GLIBC_VERSION=2.38 SYSROOT="$R" sh "$INSTALL" --dry-run 2>&1); RC=$?
check "glibc 2.38: accepted" '[ $RC = 0 ]'
OUT=$(INSTALL_GLIBC_VERSION=3.0 SYSROOT="$R" sh "$INSTALL" --dry-run 2>&1); RC=$?
check "glibc 3.0: accepted" '[ $RC = 0 ]'
OUT=$(INSTALL_GLIBC_VERSION= SYSROOT="$R" sh "$INSTALL" --dry-run 2>&1); RC=$?
check "glibc of this host read without the hook: exit 0 (host glibc >= 2.38) or 1 with the message" '[ $RC = 0 ] || has "need glibc 2.38"'

# --- the SYSROOT hook never runs a real install
OUT=$(SYSROOT="$TMP/none" sh "$INSTALL" 2>&1); RC=$?
check "SYSROOT without --dry-run/--board-only: refused" '[ $RC = 2 ]'

# --- uninstall.sh plan (dry run only) removes the profile and the gpio rule
R=$(root pi3a-luckfox35); run --board-only
mkdir -p "$TMP/home"
OUT=$(HOME="$TMP/home" UNINSTALL_ROOT="$R" sh "$UNINSTALL" --dry-run --no-boot-config 2>&1); RC=$?
check "uninstall dry-run: exit 0" '[ $RC = 0 ]'
check "uninstall dry-run: removes board.conf" 'has "rm -f $R/etc/applaunch/board.conf"'
check "uninstall dry-run: removes the gpio rule" 'has "rm -f $R/etc/udev/rules.d/92-applaunch-backlight-gpio.rules"'
R=$(root pi3a-wsdsi43); run --board-only
OUT=$(HOME="$TMP/home" UNINSTALL_ROOT="$R" sh "$UNINSTALL" --dry-run --no-boot-config 2>&1); RC=$?
check "uninstall dry-run: removes the sysfs backlight rule" '[ $RC = 0 ] && has "rm -f $R/etc/udev/rules.d/92-applaunch-backlight-sysfs.rules"'
check "uninstall dry-run: changed nothing" '[ -f "$R/etc/applaunch/board.conf" ]'

# --- appliance boards: the deck and the Pi 3A+ get the text console unit, nothing of the desktop variant
for B in zero2w pi3a-luckfox35; do
    R=$(root $B)
    run --dry-run --user tester
    check "$B dry-run: appliance session" 'has "session: appliance" && ! has "session: desktop"'
    check "$B dry-run: user service and console unit as before" 'has "user service (as for the deck): cyberdeckzero.service" && has "cyberdeckzero-console.service frees the panel"'
    check "$B dry-run: no desktop variant" '! has "cyberdeckzero-desktop.service" && ! has "52-cyberdeckzero-desktop"'
done

# --- desktop markers in a fake root: the display manager enabled for boot (Debian's alias link), running processes
desktop() {  # desktop ROOT [DM_UNIT|-] [PROCESS ...]: "-" = no display-manager.service link; the unit file is there
    d=$1; shift
    if [ "$1" != - ]; then
        mkdir -p "$d/etc/systemd/system" "$d/lib/systemd/system"
        ln -sf "/lib/systemd/system/$1" "$d/etc/systemd/system/display-manager.service"
        echo "[Unit]" > "$d/lib/systemd/system/$1"
    fi
    shift
    pid=700
    for p in "$@"; do pid=$((pid + 1)); mkdir -p "$d/proc/$pid"; printf '%s\n' "$p" > "$d/proc/$pid/comm"; done
}
unit_text() { printf '%s\n' "$OUT" | sed -n '/^--- \/etc\/systemd\/system\/cyberdeckzero-desktop.service$/,/^--- /p'; }
rules_text() { printf '%s\n' "$OUT" | sed -n '/^--- \/etc\/polkit-1\/rules.d\/52-cyberdeckzero-desktop.rules$/,$p'; }

# --- Pi 5 + HyperPixel 4 on a Lite image (no desktop): known board, appliance variant
R=$(root pi5-hyperpixel4)
run --dry-run --user tester
check "pi5 dry-run: board pi5-hyperpixel4" '[ $RC = 0 ] && has "board: pi5-hyperpixel4" && has "session: appliance"'
check "pi5 dry-run: test_env values (fb0, 800x480, rotate 90, touch by capability)" 'has "APPLAUNCH_FB=/dev/fb0" && has "APPLAUNCH_LOGICAL=800x480" && has "APPLAUNCH_ROTATE=90" && has "APPLAUNCH_TOUCH_DEV=auto" && has "APPLAUNCH_TOUCH_ORIENT=buffer"'
check "pi5 dry-run: nothing written" '[ ! -e "$R/etc" ]'

# --- HackBerry CM5 booting to its desktop: the desktop variant
R=$(root hackberry-cm5)
desktop "$R" lightdm.service systemd lightdm labwc Xwayland bash
run --dry-run --user tester
check "hackberry dry-run: board hackberry-cm5, exit 0" '[ $RC = 0 ] && has "board: hackberry-cm5"'
check "hackberry dry-run: desktop session and its evidence" 'has "session: desktop (display manager lightdm.service enabled; running: labwc+Xwayland+lightdm)"'
check "hackberry dry-run: profile from test_env (fb0, 720x720, rotate 0)" 'has "APPLAUNCH_BOARD=hackberry-cm5" && has "APPLAUNCH_FB=/dev/fb0" && has "APPLAUNCH_LOGICAL=720x720" && has "APPLAUNCH_ROTATE=0" && has "LV_LINUX_FBDEV_DEVICE=/dev/fb0"'
check "hackberry dry-run: no boot config, no user service, getty left alone" 'has "boot config: none on a desktop board" && has "no user service, no lingering, getty@tty1 left as it is" && ! has "user service (as for the deck)" && ! has "PWM backlight"'
check "hackberry dry-run: menu entry, polkit rule, not started now" 'has "/usr/share/applications/cyberdeckzero.desktop: \"CyberdeckZero Launcher\"" && has "52-cyberdeckzero-desktop.rules: tester may start and stop cyberdeckzero-desktop.service" && has "not started now"'
check "hackberry dry-run: no text console unit (the desktop unit does it)" '! has "cyberdeckzero-console.service frees"'
check "hackberry dry-run: nothing written" '[ ! -e "$R/etc/applaunch" ] && [ ! -e "$R/etc/polkit-1" ] && [ ! -e "$R/usr" ]'
U=$(unit_text)
uhas() { case "$U" in *"$1"*) return 0 ;; esac; return 1; }
check "desktop unit: runs as the user, with a login session" 'uhas "User=tester" && uhas "PAMName=login"'
check "desktop unit: stops the display manager, ordered" 'uhas "Conflicts=lightdm.service" && uhas "After=lightdm.service"'
check "desktop unit: frees the panel from the text console as root, then a VT without a login prompt" 'uhas "ExecStartPre=-+/usr/share/APPLaunch/bin/cyberdeckzero-console unbind
ExecStartPre=-+/usr/bin/timeout 5 /usr/bin/chvt 7"'
check "desktop unit: gives the desktop back as root, the poweroff guard in the helper" 'uhas "ExecStopPost=+/usr/share/APPLaunch/bin/cyberdeckzero-console restore-desktop lightdm.service"'
check "desktop unit: never restarted, no [Install] (cannot be enabled)" 'uhas "Restart=no" && ! printf "%s\n" "$U" | grep -qx "\[Install\]" && ! uhas "WantedBy"'
check "desktop unit: board profile, session marker, launcher" 'uhas "EnvironmentFile=-/etc/applaunch/board.conf" && uhas "WorkingDirectory=/usr/share/APPLaunch" && uhas "Environment=APPLAUNCH_SESSION=desktop" && uhas "Environment=APPLAUNCH_DISPLAY=dpi-scaled" && uhas "ExecStart=/usr/bin/stdbuf -oL -eL /usr/share/APPLaunch/bin/cyberdeckzero"'
check "desktop unit: described as the product" 'uhas "Description=CyberdeckZero Launcher (takes the screen"'
check "desktop unit: every placeholder filled" '! uhas "@USER@" && ! uhas "@DM@"'
U=$(rules_text)
check "desktop polkit rule: that user, that unit, start/stop/restart" 'uhas "subject.user == \"tester\"" && uhas "action.lookup(\"unit\") == \"cyberdeckzero-desktop.service\"" && uhas "org.freedesktop.systemd1.manage-units" && ! uhas "@USER@"'
run --board-only
P="$R/etc/applaunch/board.conf"
EXPECT='APPLAUNCH_BOARD=hackberry-cm5
APPLAUNCH_FB=/dev/fb0
APPLAUNCH_LOGICAL=720x720
APPLAUNCH_ROTATE=0
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
LV_LINUX_FBDEV_DEVICE=/dev/fb0'
GOT=$(grep -v '^#' "$P" 2>/dev/null)
if [ "$GOT" = "$EXPECT" ]; then ok "hackberry profile: exact keys and values"; else ko "hackberry profile: exact keys and values" "$GOT"; fi
check "hackberry profile: only KEY=VALUE or comment lines" '! grep -v -E "^(#.*|[A-Z_][A-Z0-9_]*=[^ ]*)$" "$P" >/dev/null'
run --dry-run --user "bad user"
check "desktop dry-run with an odd user name: shown only (the real install refuses it)" '[ $RC = 0 ]'
check "install.sh refuses odd user names for the desktop files" 'grep -q "check_user_name \"\$TARGET_USER\"" "$INSTALL"'

# --- Pi 5 desktop found by its processes only (no alias link): the display manager is taken from them
R=$(root pi5-hyperpixel4)
desktop "$R" - labwc lightdm
run --dry-run --user tester
check "pi5 desktop by processes: session desktop, lightdm" 'has "session: desktop (running: labwc+lightdm)" && [ -n "$(unit_text | grep -x "Conflicts=lightdm.service")" ]'
# another display manager: the unit follows it
R=$(root pi5-hyperpixel4)
desktop "$R" sddm.service
run --dry-run --user tester
check "pi5 with sddm: Conflicts/After/restore name sddm.service" 'U=$(unit_text); uhas "Conflicts=sddm.service" && uhas "After=sddm.service" && uhas "restore-desktop sddm.service"'
# a compositor without a display manager: lightdm assumed, said so
R=$(root pi5-hyperpixel4)
desktop "$R" - wayfire
run --dry-run --user tester
check "pi5 compositor only: lightdm.service assumed" 'has "no display manager unit found: lightdm.service assumed; running: wayfire"'
# the deck has no desktop variant (its settings live in cyberdeckzero.service)
R=$(root zero2w)
desktop "$R" lightdm.service lightdm
run --dry-run --user tester
check "zero2w with a desktop: install would stop" '[ $RC = 0 ] && has "session: desktop" && has "has no desktop variant: install.sh would stop here"'
# a Pi 3A+ with a desktop: its profile works for the desktop unit as well
R=$(root pi3a-luckfox35)
desktop "$R" lightdm.service lightdm
run --dry-run --user tester
check "pi3a with a desktop: desktop variant with its profile" 'has "session: desktop" && has "APPLAUNCH_FB=/dev/fb1" && has "boot config: none on a desktop board"'

# --- the units and files shipped in the bundle
D="$HERE/.."
check "console unit: after multi-user.target, inside the boot transaction" 'grep -qx "DefaultDependencies=no" "$D/cyberdeckzero-console.service" && grep -qx "After=multi-user.target" "$D/cyberdeckzero-console.service" && grep -qx "WantedBy=multi-user.target" "$D/cyberdeckzero-console.service"'
check "console unit: unbind at start, bind at stop" 'grep -qx "ExecStart=/usr/share/APPLaunch/bin/cyberdeckzero-console unbind" "$D/cyberdeckzero-console.service" && grep -qx "ExecStop=/usr/share/APPLaunch/bin/cyberdeckzero-console bind" "$D/cyberdeckzero-console.service" && grep -qx "RemainAfterExit=yes" "$D/cyberdeckzero-console.service"'
check "launcher user service still waits for the system to be running" 'grep -q "is-system-running --wait" "$D/cyberdeckzero.service"'
check "user service: the product's description, the new binary, the board profile and working directory as before" 'grep -qx "Description=CyberdeckZero Launcher" "$D/cyberdeckzero.service" && grep -qx "ExecStart=/usr/bin/stdbuf -oL -eL /usr/share/APPLaunch/bin/cyberdeckzero" "$D/cyberdeckzero.service" && grep -qx "EnvironmentFile=-/etc/applaunch/board.conf" "$D/cyberdeckzero.service" && grep -qx "WorkingDirectory=/usr/share/APPLaunch" "$D/cyberdeckzero.service"'
check "units: no board or panel name in a description" '! grep -h "^Description=" "$D"/*.service | grep -q -e "Zero 2" -e Waveshare'
check "menu entry: starts the unit, no terminal" 'grep -qx "Exec=systemctl --no-block start cyberdeckzero-desktop.service" "$D/cyberdeckzero.desktop" && grep -qx "Terminal=false" "$D/cyberdeckzero.desktop" && grep -qx "Name=CyberdeckZero Launcher" "$D/cyberdeckzero.desktop"'
for f in cyberdeckzero.service cyberdeckzero-console cyberdeckzero-console.service cyberdeckzero-desktop.service \
         52-cyberdeckzero-desktop.rules cyberdeckzero.desktop dist/cyberdeckzero payload/doc; do
    check "build.sh puts $f in the bundle" 'grep -q "$f" "$D/build.sh"'
done
check "build.sh leaves the stock start-up logo and the ADB helpers out" 'grep -q "rm -f \"\$OUT/payload/share/share/images/logo_lcd.png\"" "$D/build.sh" && grep -q "rm -rf \"\$OUT/payload/share/adb\"" "$D/build.sh"'
check "build.sh names the tarball by version and arch (arm64 by default), with the earlier name as a copy" 'grep -q "TARBALL=\"cyberdeckzero-launcher-\$VERSION-\$ARCH.tar.gz\"" "$D/build.sh" && grep -qx "ARCH=arm64" "$D/build.sh" && grep -q "LEGACY_TARBALL=\"pizero2w-bundle.tar.gz\"" "$D/build.sh"'
check "build.sh: the earlier name is an arm64 copy only, amd64 builds natively with its own config" 'grep -qx "\[ \"\$ARCH\" = arm64 \] || LEGACY_NAME=0" "$D/build.sh" && grep -q "CONFIG=linux_x86_64_native_cp0_config_defaults.mk" "$D/build.sh" && [ -f "$D/../linux_x86_64_native_cp0_config_defaults.mk" ]'
check "amd64 config: the device config without the NEON lines" '[ "$(sed "s/\r\$//" "$D/../config_defaults.mk" | grep -v -e "^#" -e "LV_DRAW_SW_ASM_NEON" -e "LV_USE_DRAW_SW_ASM")" = "$(sed "s/\r\$//" "$D/../linux_x86_64_native_cp0_config_defaults.mk" | grep -v "^#")" ] && grep -qx "CONFIG_APPLAUNCH_LINUX_CP0=y" "$D/../linux_x86_64_native_cp0_config_defaults.mk"'
check "units: the dpi-scaled backend in both launcher units" 'grep -qx "Environment=APPLAUNCH_DISPLAY=dpi-scaled" "$D/cyberdeckzero.service" && grep -qx "Environment=APPLAUNCH_DISPLAY=dpi-scaled" "$D/cyberdeckzero-desktop.service"'
check "cyberdeckzero-console: valid sh" 'sh -n "$D/cyberdeckzero-console"'

# --- applaunch-console against a fake /sys/class/vtconsole and a fake systemctl
SYS="$TMP/sys"
fake_vt() {  # fake_vt BIND: vtcon0 = dummy, vtcon1 = the framebuffer console with that bind value
    rm -rf "$SYS"; mkdir -p "$SYS/class/vtconsole/vtcon0" "$SYS/class/vtconsole/vtcon1"
    echo "(S) dummy device" > "$SYS/class/vtconsole/vtcon0/name"; echo 1 > "$SYS/class/vtconsole/vtcon0/bind"
    echo "(M) frame buffer device" > "$SYS/class/vtconsole/vtcon1/name"; echo "$1" > "$SYS/class/vtconsole/vtcon1/bind"
}
SCTL="$TMP/fake-systemctl"
SCTL_LOG="$TMP/fake-systemctl.log"
cat > "$SCTL" <<'EOF'
#!/bin/sh
# fake systemctl: FAKE_STATE = is-system-running answer, FAKE_JOBS = list-jobs lines; logs the other calls
case "$1" in
    is-system-running) echo "${FAKE_STATE:-running}"; [ "${FAKE_STATE:-running}" = running ] ;;
    list-jobs) [ -z "${FAKE_JOBS:-}" ] || printf '%s\n' "$FAKE_JOBS" ;;
    *) printf '%s\n' "$*" >> "$FAKE_SCTL_LOG" ;;
esac
EOF
chmod +x "$SCTL"
con() { OUT=$(APPLAUNCH_SYSFS="$SYS" APPLAUNCH_SYSTEMCTL="$SCTL" FAKE_SCTL_LOG="$SCTL_LOG" sh "$D/cyberdeckzero-console" "$@" 2>&1); RC=$?; }
vt() { cat "$SYS/class/vtconsole/vtcon1/bind"; }
fake_vt 1
con unbind
check "console unbind: the framebuffer console only" '[ $RC = 0 ] && [ "$(vt)" = 0 ] && [ "$(cat "$SYS/class/vtconsole/vtcon0/bind")" = 1 ] && has "vtcon1) unbound"'
con unbind
check "console unbind twice: idempotent" '[ $RC = 0 ] && [ "$(vt)" = 0 ] && has "already unbound"'
con status
check "console status: unbound" '[ $RC = 0 ] && has "unbound (vtcon1)"'
con bind
check "console bind: bound again" '[ $RC = 0 ] && [ "$(vt)" = 1 ] && has "vtcon1) bound"'
rm -rf "$SYS"; mkdir -p "$SYS/class/vtconsole/vtcon0"; echo "(S) dummy device" > "$SYS/class/vtconsole/vtcon0/name"
con unbind
check "console without a framebuffer console: nothing to do, exit 0" '[ $RC = 0 ] && has "no framebuffer console"'
con status
check "console status without one: absent" 'has "absent"'
con
check "console without a command: usage, exit 2" '[ $RC = 2 ]'
# restore-desktop: a normal exit gives the desktop back; a shutdown does not
fake_vt 0; rm -f "$SCTL_LOG"
con restore-desktop lightdm.service
check "restore-desktop (running): console bound, display manager started without blocking" '[ $RC = 0 ] && [ "$(vt)" = 1 ] && [ "$(cat "$SCTL_LOG")" = "--no-block start lightdm.service" ]'
fake_vt 0; rm -f "$SCTL_LOG"
FAKE_STATE=stopping; export FAKE_STATE
con restore-desktop lightdm.service
unset FAKE_STATE
check "restore-desktop during a poweroff/reboot (stopping): console bound, no desktop" '[ $RC = 0 ] && [ "$(vt)" = 1 ] && [ ! -e "$SCTL_LOG" ] && has "shutting down"'
for J in "  91 poweroff.target start waiting" "  92 reboot.target  start waiting" "  93 halt.target start waiting" "  94 kexec.target start waiting"; do
    fake_vt 0; rm -f "$SCTL_LOG"
    FAKE_JOBS="$J"; export FAKE_JOBS
    con restore-desktop lightdm.service
    unset FAKE_JOBS
    check "restore-desktop with a queued job ($(echo $J | cut -d' ' -f2)): no desktop" '[ $RC = 0 ] && [ ! -e "$SCTL_LOG" ]'
done
fake_vt 0; rm -f "$SCTL_LOG"
FAKE_JOBS="  95 apt-daily.service start waiting"; export FAKE_JOBS
FAKE_STATE=degraded; export FAKE_STATE
con restore-desktop lightdm.service
unset FAKE_JOBS FAKE_STATE
check "restore-desktop (degraded, other jobs): the desktop comes back" '[ $RC = 0 ] && [ "$(cat "$SCTL_LOG")" = "--no-block start lightdm.service" ]'
con restore-desktop
check "restore-desktop without a unit: exit 2" '[ $RC = 2 ]'

# --- uninstall.sh plan for the units and files, both generations (a half-migrated board has some of each)
R=$(root pi3a-luckfox35)
mkdir -p "$R/etc/systemd/system" "$R/usr/share/applications" "$R/usr/share/APPLaunch/bin" "$R/etc/polkit-1/rules.d" \
         "$R/usr/share/APPLaunch/share/images" "$R/usr/share/APPLaunch/adb" "$R/usr/share/doc/cyberdeckzero-launcher"
for f in applaunch-console.service applaunch-desktop.service cyberdeckzero-console.service cyberdeckzero-desktop.service; do
    : > "$R/etc/systemd/system/$f"
done
: > "$R/usr/share/applications/applaunch.desktop"; : > "$R/etc/polkit-1/rules.d/52-applaunch-desktop.rules"
: > "$R/usr/share/applications/cyberdeckzero.desktop"; : > "$R/etc/polkit-1/rules.d/52-cyberdeckzero-desktop.rules"
: > "$R/usr/share/APPLaunch/bin/applaunch-console"; : > "$R/usr/share/APPLaunch/bin/cyberdeckzero-console"
: > "$R/usr/share/APPLaunch/bin/cyberdeckzero"; ln -s cyberdeckzero "$R/usr/share/APPLaunch/bin/$OLDBIN"
: > "$R/usr/share/APPLaunch/share/images/logo_lcd.png"; : > "$R/usr/share/APPLaunch/adb/cardputer-adb"
: > "$R/usr/share/doc/cyberdeckzero-launcher/LICENSE"
OUT=$(HOME="$TMP/home" UNINSTALL_ROOT="$R" sh "$UNINSTALL" --dry-run --no-boot-config 2>&1); RC=$?
check "uninstall dry-run: plan names both generations" '[ $RC = 0 ] && has "user service cyberdeckzero.service / APPLaunch.service" && has "cyberdeckzero-console.service / applaunch-console.service" && has "cyberdeckzero-desktop.service / applaunch-desktop.service" && has "52-cyberdeckzero-desktop, 52-applaunch-desktop"'
for u in applaunch-desktop.service cyberdeckzero-desktop.service; do
    check "uninstall dry-run: $u stopped (desktop back) and removed" 'has "systemctl stop $u" && has "rm -f $R/etc/systemd/system/$u"'
done
for u in applaunch-console.service cyberdeckzero-console.service; do
    check "uninstall dry-run: $u disabled, stopped (console back) and removed" 'has "systemctl disable --now $u" && has "rm -f $R/etc/systemd/system/$u"'
done
check "uninstall dry-run: console bound again with the new helper, both helper names removed" 'has "$R/usr/share/APPLaunch/bin/cyberdeckzero-console bind" && has "rm -f $R/usr/share/APPLaunch/bin/cyberdeckzero-console" && has "rm -f $R/usr/share/APPLaunch/bin/applaunch-console"'
check "uninstall dry-run: both menu entries and polkit rules removed" 'has "rm -f $R/usr/share/applications/applaunch.desktop" && has "rm -f $R/etc/polkit-1/rules.d/52-applaunch-desktop.rules" && has "rm -f $R/usr/share/applications/cyberdeckzero.desktop" && has "rm -f $R/etc/polkit-1/rules.d/52-cyberdeckzero-desktop.rules"'
check "uninstall dry-run: the binary and its earlier name (a link) removed" 'has "rm -f $R/usr/share/APPLaunch/bin/cyberdeckzero" && has "rm -f $R/usr/share/APPLaunch/bin/$OLDBIN"'
check "uninstall dry-run: retired files and the licences removed" 'has "rm -f $R/usr/share/APPLaunch/share/images/logo_lcd.png" && has "rm -f $R/usr/share/APPLaunch/adb/cardputer-adb" && has "rm -rf $R/usr/share/doc/cyberdeckzero-launcher"'
check "uninstall dry-run: units handled before the launcher files" '[ "$(printf "%s\n" "$OUT" | grep -n "cyberdeckzero-console bind" | head -n 1 | cut -d: -f1)" -lt "$(printf "%s\n" "$OUT" | grep -n "rm -f $R/usr/share/APPLaunch/bin/cyberdeckzero-console" | head -n 1 | cut -d: -f1)" ]'
check "uninstall dry-run: still nothing changed" '[ -f "$R/etc/systemd/system/applaunch-desktop.service" ] && [ -f "$R/usr/share/applications/applaunch.desktop" ] && [ -L "$R/usr/share/APPLaunch/bin/$OLDBIN" ] && [ -f "$R/usr/share/doc/cyberdeckzero-launcher/LICENSE" ]'
# the old helper only: it binds the console (a board that was never migrated)
rm -f "$R/usr/share/APPLaunch/bin/cyberdeckzero-console"
OUT=$(HOME="$TMP/home" UNINSTALL_ROOT="$R" sh "$UNINSTALL" --dry-run --no-boot-config 2>&1); RC=$?
check "uninstall dry-run, never migrated: the earlier helper binds the console" '[ $RC = 0 ] && has "$R/usr/share/APPLaunch/bin/applaunch-console bind"'

# --- migration from the names of v0.6.0 and before
U=$(id -un)
UHOME=$(getent passwd "$U" | cut -d: -f6)
# old_install ROOT appliance|desktop HOME: what an install of v0.6.0 left (HOME: the user's home, without ROOT)
old_install() {
    r=$1; a="$1/usr/share/APPLaunch"
    mkdir -p "$a/bin" "$a/share/images" "$a/adb" "$a/applications" "$r/etc/systemd/system"
    printf 'old launcher\n' > "$a/bin/$OLDBIN"; chmod 755 "$a/bin/$OLDBIN"
    printf '#!/bin/sh\n' > "$a/bin/applaunch-console"; chmod 755 "$a/bin/applaunch-console"
    for f in logo_lcd.png launcher_brand_logo.png carousel_zero_wordmark.png; do echo old > "$a/share/images/$f"; done
    echo old > "$a/adb/cardputer-adb"; echo old > "$a/adb/cardputer-adb-gadget"
    echo app > "$a/applications/some-app.desktop"                       # an installed app's entry: kept
    if [ "$2" = appliance ]; then
        h="$r$3/.config/systemd/user"
        mkdir -p "$h/default.target.wants" "$r/etc/systemd/system/multi-user.target.wants"
        echo "[Unit]" > "$h/APPLaunch.service"
        ln -s ../APPLaunch.service "$h/default.target.wants/APPLaunch.service"
        echo "[Unit]" > "$r/etc/systemd/system/applaunch-console.service"
        ln -s ../applaunch-console.service "$r/etc/systemd/system/multi-user.target.wants/applaunch-console.service"
    else
        mkdir -p "$r/etc/polkit-1/rules.d" "$r/usr/share/applications"
        echo "[Unit]" > "$r/etc/systemd/system/applaunch-desktop.service"
        echo "rule" > "$r/etc/polkit-1/rules.d/52-applaunch-desktop.rules"
        echo "[Desktop Entry]" > "$r/usr/share/applications/applaunch.desktop"
    fi
}

# dry run over an earlier install: every step listed, found or not, nothing changed
R=$(root pi3a-luckfox35)
old_install "$R" appliance /home/tester
run --dry-run --user tester
check "migration dry-run: exit 0, the old user service found and planned" '[ $RC = 0 ] && has "user service APPLaunch.service of tester: found: stopped, disabled and removed"'
check "migration dry-run: done before the new units" 'has "done before the new units are enabled, so two launchers never"'
check "migration dry-run: old console unit found, desktop unit, rule and menu entry not found" 'has "applaunch-console.service: found: disabled and stopped" && has "applaunch-desktop.service: not found" && has "/etc/polkit-1/rules.d/52-applaunch-desktop.rules: not found" && has "/usr/share/applications/applaunch.desktop: not found"'
check "migration dry-run: earlier binary and helper become links" 'has "/usr/share/APPLaunch/bin/$OLDBIN: found: replaced by a link to cyberdeckzero" && has "/usr/share/APPLaunch/bin/applaunch-console: found: replaced by a link to cyberdeckzero-console"'
check "migration dry-run: retired files listed" 'has "/usr/share/APPLaunch/share/images/logo_lcd.png" && has "/usr/share/APPLaunch/share/images/carousel_zero_wordmark.png" && has "/usr/share/APPLaunch/adb/cardputer-adb-gadget"'
check "migration dry-run: board.conf and the user data kept, licences planned" 'has "/etc/applaunch/board.conf, the user" && has "/usr/share/doc/cyberdeckzero-launcher"'
check "migration dry-run: nothing changed" '[ -f "$R/home/tester/.config/systemd/user/APPLaunch.service" ] && [ -f "$R/etc/systemd/system/applaunch-console.service" ] && [ ! -L "$R/usr/share/APPLaunch/bin/$OLDBIN" ] && [ -f "$R/usr/share/APPLaunch/share/images/logo_lcd.png" ] && [ ! -e "$R/etc/applaunch" ]'
R=$(root zero2w)
run --dry-run --user tester
check "migration dry-run on a fresh board: nothing found, links still planned" '[ $RC = 0 ] && has "user service APPLaunch.service of tester: not found" && has "applaunch-console.service: not found" && has "$OLDBIN: not found: created as a link to cyberdeckzero" && has "adb/): none found"'
R=$(root hackberry-cm5)
desktop "$R" lightdm.service lightdm labwc
old_install "$R" desktop /home/tester
run --dry-run --user tester
check "migration dry-run, desktop board: the old desktop unit, rule and menu entry found" '[ $RC = 0 ] && has "applaunch-desktop.service: found: stopped if it runs (the earlier launcher quits, the desktop comes back), removed" && has "/etc/polkit-1/rules.d/52-applaunch-desktop.rules: found: removed" && has "/usr/share/applications/applaunch.desktop: found: removed"'

# --- a whole install into a fake root: a bundle as build.sh assembles it, stand-ins for the system commands
BUN="$TMP/bundle"
rm -rf "$BUN"; mkdir -p "$BUN/payload/bin" "$BUN/payload/lib" "$BUN/payload/etc" "$BUN/payload/share/share/images" \
    "$BUN/payload/share/share/font" "$BUN/payload/doc/LICENSES" "$BUN/payload/doc/fonts"
cp "$D/install.sh" "$D/uninstall.sh" "$D/default-sources.sh" "$BUN/"
printf '#!/bin/sh\necho launcher\n' > "$BUN/payload/bin/cyberdeckzero"
cp "$D/cyberdeckzero-console" "$BUN/payload/bin/"
chmod 755 "$BUN/payload/bin/cyberdeckzero" "$BUN/payload/bin/cyberdeckzero-console"
: > "$BUN/payload/lib/libapplaunch_vfb.so"
for f in cyberdeckzero.service launcher-ntp-default.service waveshare-pwm-backlight.dts 90-backlight-unblank.rules \
         91-applaunch-vkbd.rules 93-applaunch-rfkill.rules 50-networkmanager-netdev.rules 51-launcher-time-power.rules \
         cyberdeckzero-console.service cyberdeckzero-desktop.service 52-cyberdeckzero-desktop.rules cyberdeckzero.desktop; do
    cp "$D/$f" "$BUN/payload/etc/"
done
chmod 755 "$BUN/payload/etc/"*      # as in the real bundle (built from a Windows checkout: drvfs files are rwx)
echo png > "$BUN/payload/share/share/images/setting_100.png"
echo ofl > "$BUN/payload/share/share/font/JetBrainsMono-OFL.txt"
( cd "$BUN/payload/share" && find . -type f | sed 's|^\./||' | LC_ALL=C sort ) > "$BUN/payload/share.manifest"
echo "MIT License" > "$BUN/payload/doc/LICENSE"; echo mit > "$BUN/payload/doc/LICENSES/LVGL-MIT.txt"
echo ofl > "$BUN/payload/doc/fonts/JetBrainsMono-OFL.txt"

FAKEBIN="$TMP/fakebin"; CALLS="$TMP/calls.log"
mkdir -p "$FAKEBIN"
cat > "$FAKEBIN/systemctl" <<'EOF'
#!/bin/sh
# stand-in systemctl: logs the call; is-active answers from FAKE_ACTIVE (unit names); "cat" knows no unit
printf 'systemctl %s\n' "$*" >> "$FAKE_CALLS"
case " $* " in
    *" is-active "*) for a in "$@"; do [ -n "$a" ] || continue; case " ${FAKE_ACTIVE:-} " in *" $a "*) exit 0 ;; esac; done; exit 3 ;;
    *" cat "*) exit 1 ;;
esac
exit 0
EOF
cat > "$FAKEBIN/runuser" <<'EOF'
#!/bin/sh
# stand-in runuser (runuser -u USER -- env XDG_RUNTIME_DIR=... CMD): logs the call; is-active from FAKE_USER_ACTIVE
printf 'runuser %s\n' "$*" >> "$FAKE_CALLS"
case " $* " in
    *" is-active "*) for a in "$@"; do [ -n "$a" ] || continue; case " ${FAKE_USER_ACTIVE:-} " in *" $a "*) exit 0 ;; esac; done; exit 3 ;;
esac
exit 0
EOF
cat > "$FAKEBIN/sudo" <<'EOF'
#!/bin/sh
# stand-in sudo: logs the call and runs it as the caller
printf 'sudo %s\n' "$*" >> "$FAKE_CALLS"
[ "$1" = -v ] && exit 0
exec "$@"
EOF
printf '#!/bin/sh\necho "${FAKE_UNAME_M:-aarch64}"\n' > "$FAKEBIN/uname"
printf '#!/bin/sh\nprintf "apt-get %%s\\n" "$*" >> "$FAKE_CALLS"\nexit "${FAKE_APT_RC:-1}"\n' > "$FAKEBIN/apt-get"
cat > "$FAKEBIN/dpkg" <<'EOF'
#!/bin/sh
# stand-in dpkg: logs the call; "-s PKG" answers "not installed" for the packages in FAKE_DPKG_MISSING
printf 'dpkg %s\n' "$*" >> "$FAKE_CALLS"
if [ "$1" = -s ]; then case " ${FAKE_DPKG_MISSING:-} " in *" $2 "*) exit 1 ;; esac; fi
exit 0
EOF
cat > "$FAKEBIN/pkaction" <<'EOF'
#!/bin/sh
# stand-in pkaction: polkit answers unless FAKE_POLKIT=missing
printf 'pkaction %s\n' "$*" >> "$FAKE_CALLS"
[ "${FAKE_POLKIT:-ok}" = ok ]
EOF
for c in loginctl udevadm usermod chown dtc groupadd; do
    printf '#!/bin/sh\nprintf "%s %%s\\n" "$*" >> "$FAKE_CALLS"\nexit 0\n' "$c" > "$FAKEBIN/$c"
done
chmod 755 "$FAKEBIN"/*
# full ROOT [options]: install.sh of the fake bundle into ROOT for the user U; output $OUT, exit code $RC, calls in $CALLS
full() {
    r=$1; shift
    : > "$CALLS"
    OUT=$(PATH="$FAKEBIN:$PATH" FAKE_CALLS="$CALLS" SYSROOT="$r" INSTALL_FAKE_SYSTEM=1 \
          sh "$BUN/install.sh" --user "$U" --no-boot-config --no-default-source "$@" 2>&1); RC=$?
}
# line of the first call matching $1 in $CALLS (0 if none)
at() { n=$(grep -n -F -- "$1" "$CALLS" | head -n 1 | cut -d: -f1); echo "${n:-0}"; }
called() { grep -q -F -- "$1" "$CALLS"; }
# every file and link under $1 with its checksum or target (no times)
snapshot() { ( cd "$1" && find . \( -type f -o -type l \) | LC_ALL=C sort | while IFS= read -r f; do
    if [ -L "$f" ]; then echo "$f -> $(readlink "$f")"; else echo "$f $(cksum < "$f")"; fi; done ); }

OUT=$(SYSROOT="$TMP/none" INSTALL_FAKE_SYSTEM=0 sh "$INSTALL" 2>&1); RC=$?
check "SYSROOT with INSTALL_FAKE_SYSTEM other than 1: still refused" '[ $RC = 2 ]'

# fresh appliance board (Pi 3A+): the new names only
R=$(root pi3a-luckfox35)
A="$R/usr/share/APPLaunch"; H="$R$UHOME/.config/systemd/user"
full "$R"
check "fake install, fresh: exit 0" '[ $RC = 0 ]'
check "fake install, fresh: binary, helper and their earlier names as links" '[ -x "$A/bin/cyberdeckzero" ] && [ ! -L "$A/bin/cyberdeckzero" ] && [ "$(readlink "$A/bin/$OLDBIN")" = cyberdeckzero ] && [ -x "$A/bin/cyberdeckzero-console" ] && [ "$(readlink "$A/bin/applaunch-console")" = cyberdeckzero-console ]'
check "fake install, fresh: user unit and console unit by the new names, none of the old" '[ -f "$H/cyberdeckzero.service" ] && [ -f "$R/etc/systemd/system/cyberdeckzero-console.service" ] && [ ! -e "$H/APPLaunch.service" ] && [ ! -e "$R/etc/systemd/system/applaunch-console.service" ]'
check "fake install, fresh: units enabled and started by the new names" 'called "systemctl enable --now cyberdeckzero-console.service" && called "systemctl --user enable cyberdeckzero.service" && called "systemctl --user restart cyberdeckzero.service"'
check "fake install, fresh: nothing to migrate" 'has "nothing to migrate" && ! called "APPLaunch.service"'
check "fake install, fresh: licences in /usr/share/doc/cyberdeckzero-launcher" '[ -f "$R/usr/share/doc/cyberdeckzero-launcher/LICENSE" ] && [ -f "$R/usr/share/doc/cyberdeckzero-launcher/LICENSES/LVGL-MIT.txt" ] && [ -f "$R/usr/share/doc/cyberdeckzero-launcher/fonts/JetBrainsMono-OFL.txt" ]'
check "fake install, fresh: board profile written, assets copied" '[ -f "$R/etc/applaunch/board.conf" ] && [ -f "$A/share/images/setting_100.png" ]'
check "fake install, fresh: no apt-get, nothing written outside the fake root" '! called "apt-get" && [ ! -e /usr/share/doc/cyberdeckzero-launcher ] && [ ! -e /etc/systemd/system/cyberdeckzero-console.service ]'
check "fake install, fresh: the user unit is 644 (systemd: not marked executable), no unit, rule or profile executable" '[ "$(stat -c %a "$H/cyberdeckzero.service")" = 644 ] && [ -z "$(find "$R/etc" "$H" -type f -perm /111)" ]'
check "fake install, fresh: a Pi's packages (dtc, no polkitd or bluez), bluetooth.service left alone, polkit checked" 'called "dpkg -s device-tree-compiler" && ! called "dpkg -s polkitd" && ! called "dpkg -s bluez" && ! called "enable --now bluetooth.service" && called "pkaction --action-id org.freedesktop.login1.reboot" && has "polkit: running"'

# appliance board installed with v0.6.0 (Pi 3A+): migrated
R=$(root pi3a-luckfox35)
A="$R/usr/share/APPLaunch"; H="$R$UHOME/.config/systemd/user"
SYSROOT="$R" sh "$INSTALL" --board-only > /dev/null 2>&1
# board.conf as v0.6.0 wrote it: the same values, the earlier wording of the header
sed -i '2s/.*/# Loaded by APPLaunch.service (EnvironmentFile). Edit freely: install.sh then leaves it alone unless --force./' "$R/etc/applaunch/board.conf"
CONF_SUM=$(cksum < "$R/etc/applaunch/board.conf")
old_install "$R" appliance "$UHOME"
full "$R"
check "fake migration: exit 0" '[ $RC = 0 ]'
check "fake migration: old user service stopped and disabled through the user's systemd" 'called "systemctl --user disable --now APPLaunch.service"'
check "fake migration: ... before the new user service is enabled" '[ "$(at "disable --now APPLaunch.service")" -lt "$(at "systemctl --user enable cyberdeckzero.service")" ] && [ "$(at "disable --now APPLaunch.service")" -gt 0 ]'
check "fake migration: old console unit disabled before the new one is enabled" '[ "$(at "systemctl disable --now applaunch-console.service")" -gt 0 ] && [ "$(at "systemctl disable --now applaunch-console.service")" -lt "$(at "systemctl enable --now cyberdeckzero-console.service")" ]'
check "fake migration: old units and their enablement links removed" '[ ! -e "$H/APPLaunch.service" ] && [ ! -L "$H/default.target.wants/APPLaunch.service" ] && [ ! -e "$R/etc/systemd/system/applaunch-console.service" ] && [ ! -L "$R/etc/systemd/system/multi-user.target.wants/applaunch-console.service" ]'
check "fake migration: new units installed" '[ -f "$H/cyberdeckzero.service" ] && [ -f "$R/etc/systemd/system/cyberdeckzero-console.service" ]'
check "fake migration: earlier binary and helper are links to the new files now" '[ "$(readlink "$A/bin/$OLDBIN")" = cyberdeckzero ] && [ "$(readlink "$A/bin/applaunch-console")" = cyberdeckzero-console ] && [ "$(cat "$A/bin/$OLDBIN")" = "$(cat "$A/bin/cyberdeckzero")" ]'
check "fake migration: retired files and the empty adb folder removed" '[ ! -e "$A/share/images/logo_lcd.png" ] && [ ! -e "$A/share/images/launcher_brand_logo.png" ] && [ ! -e "$A/share/images/carousel_zero_wordmark.png" ] && [ ! -e "$A/adb" ]'
check "fake migration: an installed app and board.conf are kept as they are" '[ "$(cat "$A/applications/some-app.desktop")" = app ] && [ "$(cksum < "$R/etc/applaunch/board.conf")" = "$CONF_SUM" ] && [ ! -e "$R/etc/applaunch/board.conf.bak" ]'
check "fake migration: the steps are reported" 'has "== Migrating an earlier install" && has "user service APPLaunch.service of $U: stopped, disabled and removed" && has "applaunch-console.service disabled and removed" && has "removed /usr/share/APPLaunch/share/images/logo_lcd.png"'
SNAP1=$(snapshot "$R")
full "$R"
check "fake migration, second run: exit 0, nothing to migrate, nothing of the old names called" '[ $RC = 0 ] && has "nothing to migrate" && ! called "APPLaunch.service" && ! called "applaunch-console.service"'
SNAP2=$(snapshot "$R")
if [ "$SNAP1" = "$SNAP2" ]; then ok "fake migration, second run: the same files, links and contents"; else
    ko "fake migration, second run: the same files, links and contents" "$(printf '%s\n' "$SNAP1" > "$TMP/s1"; printf '%s\n' "$SNAP2" | diff "$TMP/s1" - )"; fi

# the earlier user service still running after its stop (it did not stop): the new one is not started on top of it
R=$(root pi3a-luckfox35)
old_install "$R" appliance "$UHOME"
FAKE_USER_ACTIVE=APPLaunch.service; export FAKE_USER_ACTIVE
full "$R"
unset FAKE_USER_ACTIVE
check "fake migration, old service will not stop: new one enabled, not started, warned" '[ $RC = 0 ] && has "WARNING: APPLaunch.service is still running" && called "systemctl --user enable cyberdeckzero.service" && ! called "restart cyberdeckzero.service"'

# desktop board installed with v0.6.0 (HackBerry), its launcher running during the install
R=$(root hackberry-cm5)
A="$R/usr/share/APPLaunch"
desktop "$R" lightdm.service lightdm labwc
old_install "$R" desktop "$UHOME"
FAKE_ACTIVE=applaunch-desktop.service; export FAKE_ACTIVE
full "$R"
unset FAKE_ACTIVE
check "fake desktop migration: exit 0" '[ $RC = 0 ]'
check "fake desktop migration: the running earlier launcher stopped (desktop back), its unit, rule and menu entry removed" 'called "systemctl stop applaunch-desktop.service" && [ ! -e "$R/etc/systemd/system/applaunch-desktop.service" ] && [ ! -e "$R/etc/polkit-1/rules.d/52-applaunch-desktop.rules" ] && [ ! -e "$R/usr/share/applications/applaunch.desktop" ]'
check "fake desktop migration: new unit, rule and menu entry, filled in" 'grep -qx "User=$U" "$R/etc/systemd/system/cyberdeckzero-desktop.service" && grep -q "\"cyberdeckzero-desktop.service\"" "$R/etc/polkit-1/rules.d/52-cyberdeckzero-desktop.rules" && grep -qx "Name=CyberdeckZero Launcher" "$R/usr/share/applications/cyberdeckzero.desktop"'
check "fake desktop migration: no user service, no console unit, not started" '[ ! -e "$R$UHOME/.config/systemd/user/cyberdeckzero.service" ] && [ ! -e "$R/etc/systemd/system/cyberdeckzero-console.service" ] && has "not started now" && ! called "start cyberdeckzero-desktop.service"'
check "fake desktop migration: the earlier helper name links to the new one (ExecStopPost= of a loaded old unit)" '[ "$(readlink "$A/bin/applaunch-console")" = cyberdeckzero-console ]'
check "HackBerry desktop install: no variant switch, no user service touched, no kbd asked for (a Pi), the session named at the end" '! has "variant: switching" && ! has "== Switching" && ! called "systemctl --user" && ! called "dpkg -s kbd" && ! called "getty@tty1" && has "session: desktop (display manager lightdm.service enabled; running: labwc+lightdm)" && has "run install.sh again"'
SNAP1=$(snapshot "$R")
full "$R"
check "fake desktop migration, second run: nothing to migrate, same tree" '[ $RC = 0 ] && has "nothing to migrate" && [ "$(snapshot "$R")" = "$SNAP1" ]'

# a whole uninstall of a half-migrated board (both generations there): everything of both goes, user data stays
R=$(root pi3a-luckfox35)
A="$R/usr/share/APPLaunch"; H="$R$UHOME/.config/systemd/user"
full "$R"
old_install "$R" appliance "$UHOME"
rm -f "$A/bin/$OLDBIN"; printf 'old launcher\n' > "$A/bin/$OLDBIN"         # an earlier binary file, not a link
mkdir -p "$R/etc/polkit-1/rules.d" "$R/usr/share/applications"
: > "$R/etc/polkit-1/rules.d/52-applaunch-desktop.rules"; : > "$R/usr/share/applications/applaunch.desktop"
: > "$CALLS"
OUT=$(PATH="$FAKEBIN:$PATH" FAKE_CALLS="$CALLS" HOME="$R$UHOME" UNINSTALL_ROOT="$R" APPLAUNCH_SYSFS="$TMP/no-sys" \
      sh "$BUN/uninstall.sh" --yes --no-boot-config 2>&1); RC=$?
check "fake uninstall, half-migrated: exit 0" '[ $RC = 0 ]'
check "fake uninstall: both user services stopped and removed" 'called "systemctl --user stop cyberdeckzero.service" && called "systemctl --user stop APPLaunch.service" && [ ! -e "$H/cyberdeckzero.service" ] && [ ! -e "$H/APPLaunch.service" ] && [ ! -L "$H/default.target.wants/APPLaunch.service" ]'
check "fake uninstall: both console units, rules and menu entries removed" '[ -z "$(ls "$R/etc/systemd/system" | grep -e cyberdeckzero -e applaunch)" ] && [ ! -e "$R/etc/systemd/system/multi-user.target.wants/applaunch-console.service" ] && [ -z "$(ls "$R/etc/polkit-1/rules.d")" ] && [ -z "$(ls "$R/usr/share/applications" 2>/dev/null)" ]'
check "fake uninstall: binaries, helpers (both names), retired files and licences gone" '[ ! -e "$A/bin/cyberdeckzero" ] && [ ! -e "$A/bin/$OLDBIN" ] && [ ! -e "$A/bin/cyberdeckzero-console" ] && [ ! -e "$A/bin/applaunch-console" ] && [ ! -e "$A/share/images/logo_lcd.png" ] && [ ! -e "$A/adb" ] && [ ! -e "$R/usr/share/doc/cyberdeckzero-launcher" ]'
check "fake uninstall: the installed app and the user data stay, the profile goes" '[ -f "$A/applications/some-app.desktop" ] && [ ! -e "$R/etc/applaunch/board.conf" ]'

# the DSI Pi 3A+ installed with v0.6.0, its owner's hand-written profile, a real boot configuration: config.txt is not
# touched, cmdline.txt gets the two console arguments only (no fbcon=), the profile is kept
R=$(root pi3a-wsdsi43)
mkdir -p "$R/boot/firmware/overlays" "$R/etc/applaunch"
printf '[all]\ndtoverlay=vc4-kms-v3d\ndtoverlay=vc4-kms-dsi-waveshare-800x480\n' > "$R/boot/firmware/config.txt"
CMD0='console=serial0,115200 console=tty1 root=PARTUUID=1234-02 rootfstype=ext4 fsck.repair=yes rootwait'
printf '%s\n' "$CMD0" > "$R/boot/firmware/cmdline.txt"
printf '# written by hand\nAPPLAUNCH_BOARD=pi3a-wsdsi43\nAPPLAUNCH_FB=/dev/fb0\nAPPLAUNCH_LOGICAL=800x480\n' > "$R/etc/applaunch/board.conf"
CFG_SUM=$(cksum < "$R/boot/firmware/config.txt"); HAND=$(cksum < "$R/etc/applaunch/board.conf")
old_install "$R" appliance "$UHOME"
fullboot() { : > "$CALLS"; OUT=$(PATH="$FAKEBIN:$PATH" FAKE_CALLS="$CALLS" SYSROOT="$R" INSTALL_FAKE_SYSTEM=1 \
    sh "$BUN/install.sh" --user "$U" --no-default-source 2>&1); RC=$?; }
fullboot
check "fake DSI install: exit 0, board pi3a-wsdsi43, migrated" '[ $RC = 0 ] && has "profile pi3a-wsdsi43" && has "user service APPLaunch.service of $U: stopped, disabled and removed"'
check "fake DSI install: config.txt untouched (no backup, no overlay, no dtc)" '[ "$(cksum < "$R/boot/firmware/config.txt")" = "$CFG_SUM" ] && [ ! -e "$R/boot/firmware/config.txt.bak-applaunch" ] && [ ! -e "$R/boot/firmware/overlays/waveshare-pwm-backlight.dtbo" ] && ! called "dtc " && has "pi3a-wsdsi43: config.txt left as it is"'
check "fake DSI install: cmdline.txt gets the two console arguments only, backup first" '[ "$(cat "$R/boot/firmware/cmdline.txt")" = "$CMD0 vt.global_cursor_default=0 consoleblank=0" ] && [ "$(cat "$R/boot/firmware/cmdline.txt.bak-applaunch")" = "$CMD0" ] && ! grep -q fbcon "$R/boot/firmware/cmdline.txt"'
check "fake DSI install: the hand-written profile kept, no gpio rule, reboot for the console settings only" '[ "$(cksum < "$R/etc/applaunch/board.conf")" = "$HAND" ] && [ ! -e "$R/etc/applaunch/board.conf.bak" ] && has "edited by hand: kept" && [ ! -e "$R/etc/udev/rules.d/92-applaunch-backlight-gpio.rules" ] && has "reboot now so the console settings apply" && ! has "PWM backlight and"'
fullboot
check "fake DSI install, second run: boot files unchanged, nothing to migrate" '[ $RC = 0 ] && [ "$(cat "$R/boot/firmware/cmdline.txt")" = "$CMD0 vt.global_cursor_default=0 consoleblank=0" ] && [ "$(cksum < "$R/boot/firmware/config.txt")" = "$CFG_SUM" ] && has "nothing to migrate" && ! has "reboot now"'

# --- PC (amd64, report 028): the Panasonic FZ-M1 tablet as measured in x-a: DMI, the i915 framebuffer 1280x800, the
# eDP connector with an EDID, intel_backlight (and an acpi_video0), a disconnected HDMI port; generated here (an EDID is
# binary). pc_root NAME [PRODUCT] [CHASSIS]: the fake root; edid FILE W_MM H_MM W_CM H_CM: a 128-byte EDID blob.
edid() {
    LC_ALL=C awk -v w="$2" -v h="$3" -v wc="$4" -v hc="$5" 'BEGIN {
        for (i = 0; i < 128; i++) b[i] = 0
        for (i = 1; i < 7; i++) b[i] = 255
        b[21] = wc; b[22] = hc
        if (w > 0) { b[54] = 29; b[55] = 28 }
        b[66] = w % 256; b[67] = h % 256; b[68] = int(w / 256) * 16 + int(h / 256)
        for (i = 0; i < 128; i++) printf "%c", b[i] }' > "$1"
}
pc_root() {
    d="$TMP/$1"; rm -rf "$d"
    mkdir -p "$d/sys/class/dmi/id" "$d/sys/class/graphics/fb0" "$d/sys/class/drm/card1-eDP-1" \
        "$d/sys/class/drm/card1-HDMI-A-1" "$d/sys/class/backlight/acpi_video0" "$d/sys/class/backlight/intel_backlight" \
        "$d/proc/1" "$d/boot/grub" "$d/etc/default" "$d/etc/netplan"
    echo "Panasonic Corporation" > "$d/sys/class/dmi/id/sys_vendor"
    echo "${2:-FZM1-2}" > "$d/sys/class/dmi/id/product_name"
    echo "${3:-30}" > "$d/sys/class/dmi/id/chassis_type"
    echo i915drmfb > "$d/sys/class/graphics/fb0/name"
    echo 1280,800 > "$d/sys/class/graphics/fb0/virtual_size"
    echo 32 > "$d/sys/class/graphics/fb0/bits_per_pixel"
    echo connected > "$d/sys/class/drm/card1-eDP-1/status"
    echo 1280x800 > "$d/sys/class/drm/card1-eDP-1/modes"
    edid "$d/sys/class/drm/card1-eDP-1/edid" 152 94 15 9
    echo disconnected > "$d/sys/class/drm/card1-HDMI-A-1/status"
    : > "$d/sys/class/drm/card1-HDMI-A-1/edid"
    echo 937 > "$d/sys/class/backlight/intel_backlight/max_brightness"
    echo 400 > "$d/sys/class/backlight/intel_backlight/brightness"
    echo 15 > "$d/sys/class/backlight/acpi_video0/max_brightness"
    echo systemd > "$d/proc/1/comm"
    printf 'menuentry "Armbian" {\n\tlinux /boot/vmlinuz root=UUID=1234 quiet\n}\n' > "$d/boot/grub/grub.cfg"
    printf 'GRUB_CMDLINE_LINUX_DEFAULT="quiet"\n' > "$d/etc/default/grub"
    printf 'network:\n  version: 2\n  renderer: networkd\n  wifis:\n    wlan0:\n      dhcp4: true\n' > "$d/etc/netplan/20-wifi.yaml"
    echo "$d"
}
pcrun() { OUT=$(INSTALL_MACHINE=x86_64 SYSROOT="$R" sh "$INSTALL" "$@" 2>&1); RC=$?; }
R=$(pc_root pc)
check "pc fixture: the EDID blob is 128 bytes with the EDID header" '[ "$(wc -c < "$R/sys/class/drm/card1-eDP-1/edid" | tr -d " ")" = 128 ] && [ "$(od -An -tx1 -N8 "$R/sys/class/drm/card1-eDP-1/edid" | tr -d " \n")" = 00ffffffffffff00 ]'
pcrun --dry-run
check "pc dry-run: exit 0, board pc-fzm1-2, the DMI facts" '[ $RC = 0 ] && has "board: pc-fzm1-2" && has "dmi: Panasonic Corporation / FZM1-2, chassis type 30, x86_64"'
check "pc dry-run: appliance session, the user service and the console unit (fbcon vtcon1 unbound)" 'has "session: appliance" && has "cyberdeckzero-console.service frees the panel"'
check "pc dry-run: no boot configuration (no config.txt, GRUB, kernel command line), no gpio rule" 'has "boot config: none on a PC (GRUB and the kernel command line are left alone)" && ! has "PWM backlight" && ! has "cmdline" && ! has "92-applaunch-backlight-gpio"'
check "pc dry-run: the intel_backlight levels rule planned" 'has "92-applaunch-backlight-sysfs.rules (video group may set the brightness of intel_backlight)"'
check "pc dry-run: the PC's packages (polkitd, bluez; no dtc), bluetooth.service, NetworkManager left to the owner" 'has "packages (PC, when missing): libinput10 libxkbcommon0 libfreetype6 curl polkitd bluez; bluetooth.service enabled" && has "network-manager is" && ! has "device-tree-compiler"'
check "pc dry-run: nothing written" '[ ! -e "$R/etc/applaunch" ] && [ ! -e "$R/etc/udev" ]'
pcrun --board-only
P="$R/etc/applaunch/board.conf"
EXPECT='APPLAUNCH_BOARD=pc-fzm1-2
APPLAUNCH_DISPLAY=dpi-scaled
APPLAUNCH_FB=/dev/fb0
APPLAUNCH_LOGICAL=1280x800
APPLAUNCH_ROTATE=0
APPLAUNCH_PANEL_MM=152x94
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/intel_backlight
APPLAUNCH_KBD_IGNORE=bus:0011
APPLAUNCH_HIDE_TILES=app_Store
LV_LINUX_FBDEV_DEVICE=/dev/fb0
APPLAUNCH_TOUCH_SWAP_XY=0
APPLAUNCH_TOUCH_INVERT_X=0
APPLAUNCH_TOUCH_INVERT_Y=0'
GOT=$(grep -v '^#' "$P" 2>/dev/null)
if [ "$GOT" = "$EXPECT" ]; then ok "pc profile: exact keys and values (EDID 152x94 mm, i8042 ignored for presence)"; else ko "pc profile: exact keys and values (EDID 152x94 mm, i8042 ignored for presence)" "$GOT"; fi
check "pc profile: the mm source named" 'grep -qx "# panel size in mm, from the EDID of eDP-1: the layout.s density" "$P"'
# shellcheck disable=SC1090
GOT=$( ( set -a; . "$P"; echo "$APPLAUNCH_DISPLAY $APPLAUNCH_FB $APPLAUNCH_LOGICAL $APPLAUNCH_PANEL_MM $APPLAUNCH_KBD_IGNORE" ) )
check "pc profile: valid sh syntax" '[ "$GOT" = "dpi-scaled /dev/fb0 1280x800 152x94 bus:0011" ]'
check "pc profile: only KEY=VALUE or comment lines" '! grep -v -E "^(#.*|[A-Z_][A-Z0-9_]*=[^ ]*)$" "$P" >/dev/null'
BLR="$R/etc/udev/rules.d/92-applaunch-backlight-sysfs.rules"
check "pc board-only: intel_backlight rule (video group), no gpio rule" '[ -f "$BLR" ] && grep -q "KERNEL==\"intel_backlight\"" "$BLR" && [ ! -e "$R/etc/udev/rules.d/92-applaunch-backlight-gpio.rules" ]'
SUM1=$(cksum < "$P")
pcrun --board-only
check "pc second run: idempotent" '[ $RC = 0 ] && has "already up to date" && [ "$(cksum < "$P")" = "$SUM1" ]'
# the size when the EDID's detailed timing has none: the basic block (cm), else the known model, else nothing
R=$(pc_root pc-cm); edid "$R/sys/class/drm/card1-eDP-1/edid" 0 0 15 9
pcrun --dry-run
check "pc, EDID with the size in cm only: 150x90" 'has "APPLAUNCH_PANEL_MM=150x90"'
R=$(pc_root pc-noedid); : > "$R/sys/class/drm/card1-eDP-1/edid"
pcrun --dry-run
check "pc FZM1-2 without an EDID size: the known 150x93" 'has "APPLAUNCH_PANEL_MM=150x93" && has "from the known size of the FZM1-2"'
R=$(pc_root pc-generic "Some Laptop 14" 10); edid "$R/sys/class/drm/card1-eDP-1/edid" 0 0 0 0
pcrun --dry-run
check "pc laptop, unknown size: no panel size, its keyboard counts, slug of the product" '[ $RC = 0 ] && has "board: pc-some-laptop-14" && ! has "APPLAUNCH_PANEL_MM=" && has "panel size unknown" && ! has "APPLAUNCH_KBD_IGNORE"'
R=$(pc_root pc-detach "Tablet X" 32)
pcrun --dry-run
check "pc detachable (chassis 32): its built-in keyboard device does not count" 'has "APPLAUNCH_KBD_IGNORE=bus:0011"'
R=$(pc_root pc-nobl); rm -rf "$R/sys/class/backlight"
pcrun --dry-run
check "pc without a backlight device: no APPLAUNCH_BACKLIGHT, no rule" '[ $RC = 0 ] && ! has "APPLAUNCH_BACKLIGHT=" && ! has "92-applaunch-backlight-sysfs"'
R=$(pc_root pc-acpi); rm -rf "$R/sys/class/backlight/intel_backlight"
pcrun --dry-run
check "pc without intel_backlight: the first backlight there" 'has "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/acpi_video0"'
R=$(pc_root pc-nofb); rm -rf "$R/sys/class/graphics/fb0"
pcrun --dry-run
check "pc without a framebuffer: unknown, said why, would stop, no boot configuration" '[ $RC = 0 ] && has "board: unknown" && has "PC (x86_64) without a framebuffer" && has "would stop here" && has "boot config: none on a PC"'
R=$(pc_root pc-arm)
OUT=$(INSTALL_MACHINE=aarch64 SYSROOT="$R" sh "$INSTALL" --dry-run 2>&1); RC=$?
check "the PC's sys tree on an aarch64 system without a device tree: no pc profile" '[ $RC = 0 ] && has "board: unknown" && ! has "pc-fzm1-2"'

# a whole fake install of the amd64 bundle on the tablet (x86_64): a fake launcher with an x86-64 ELF header
elf() {  # elf FILE E_MACHINE: a fake binary whose ELF header names that machine (62 x86-64, 183 aarch64)
    LC_ALL=C awk -v m="$2" 'BEGIN { printf "%c%c%c%c%c%c%c", 127, 69, 76, 70, 2, 1, 1
        for (i = 7; i < 18; i++) printf "%c", 0
        printf "%c%c", m, 0; printf "\necho launcher\n" }' > "$1"
    chmod 755 "$1"
}
BUN64="$TMP/bundle-amd64"; rm -rf "$BUN64"; cp -R "$BUN" "$BUN64"; elf "$BUN64/payload/bin/cyberdeckzero" 62
BUNARM="$TMP/bundle-arm64"; rm -rf "$BUNARM"; cp -R "$BUN" "$BUNARM"; elf "$BUNARM/payload/bin/cyberdeckzero" 183
pcfull() {  # pcfull ROOT BUNDLE MACHINE [options]: install.sh as a real run would go (boot configuration on) into ROOT
    r=$1; bun=$2; m=$3; shift 3
    : > "$CALLS"
    OUT=$(PATH="$FAKEBIN:$PATH" FAKE_CALLS="$CALLS" FAKE_UNAME_M="$m" SYSROOT="$r" INSTALL_FAKE_SYSTEM=1 \
          sh "$bun/install.sh" --user "$U" --no-default-source "$@" 2>&1); RC=$?
}
R=$(pc_root pc-full)
BOOTSUM=$(cat "$R/boot/grub/grub.cfg" "$R/etc/default/grub" | cksum)
pcfull "$R" "$BUN64" x86_64
check "fake PC install: exit 0, profile pc-fzm1-2" '[ $RC = 0 ] && has "profile pc-fzm1-2" && grep -qx "APPLAUNCH_BOARD=pc-fzm1-2" "$R/etc/applaunch/board.conf"'
check "fake PC install: no boot file touched (GRUB, /etc/default/grub), no config.txt or cmdline.txt made, no dtc" '[ "$(cat "$R/boot/grub/grub.cfg" "$R/etc/default/grub" | cksum)" = "$BOOTSUM" ] && [ ! -e "$R/boot/firmware" ] && [ -z "$(find "$R/boot" -name "*.txt*")" ] && ! called "dtc " && has "PC: GRUB and the kernel command line are left as they are"'
check "fake PC install: no device-tree-compiler asked for" '! called "dpkg -s device-tree-compiler"'
check "fake PC install: user service, linger, console unit, getty@tty1 masked (as on the Pi appliance boards)" '[ -f "$R$UHOME/.config/systemd/user/cyberdeckzero.service" ] && called "loginctl enable-linger $U" && called "systemctl enable --now cyberdeckzero-console.service" && called "systemctl mask getty@tty1.service"'
check "fake PC install: the backlight rule, the groups" '[ -f "$R/etc/udev/rules.d/92-applaunch-backlight-sysfs.rules" ] && called "usermod -aG input,video,netdev $U"'
check "fake PC install: polkitd and bluez checked, bluetooth.service enabled and started, polkit answers" 'called "dpkg -s polkitd" && called "dpkg -s bluez" && called "systemctl enable --now bluetooth.service" && has "bluetooth.service enabled and started" && has "polkit: running" && ! has "WARNING: no polkit"'
check "fake PC install: the user unit is 644, no unit, rule or profile executable" '[ "$(stat -c %a "$R$UHOME/.config/systemd/user/cyberdeckzero.service")" = 644 ] && [ -z "$(find "$R/etc" "$R$UHOME/.config" -type f -perm /111)" ]'
check "fake PC install: network-manager never installed" '! called "apt-get install" && ! grep -q "network-manager" "$CALLS"'
if getent group bluetooth >/dev/null 2>&1; then
    check "fake PC install: the user joins the bluetooth group (BlueZ's D-Bus policy)" 'called "usermod -aG bluetooth $U"'
else
    check "fake PC install: no bluetooth group, no usermod for it" '! called "usermod -aG bluetooth"'
fi
if id -nG "$U" | tr " " "\n" | grep -qx sudo; then
    check "fake PC install: the user is in the sudo group: no warning about the power / clock rule" '! has "is not in the sudo group"'
else
    check "fake PC install: the user is not in the sudo group: warned (the power / clock rule needs it)" 'has "is not in the sudo group"'
fi
if ! command -v nmcli >/dev/null 2>&1; then
    check "fake PC install: NetworkManager missing: the notice names it, Settings > Wi-Fi, the netplan recipe and the roll-back" 'has "NOTICE: network-manager is not installed: Settings > Wi-Fi needs NetworkManager" && has "on the console" && has "sudo apt install network-manager" && has "echo '"'"'network: {version: 2, renderer: NetworkManager}'"'"' | sudo tee /etc/netplan/90-applaunch-networkmanager.yaml" && has "sudo netplan apply" && has "Roll back" && has "sudo rm /etc/netplan/90-applaunch-networkmanager.yaml"'
    check "fake PC install: the recipe reboots after netplan apply (netplan-wpa-<interface> holds the Wi-Fi), the check, the roll-back reboots too" 'has "          sudo reboot" && has "sudo systemctl stop netplan-wpa-<interface>.service" && has "nmcli -t device status" && has ":connected\", not \":unavailable\"" && has "An ssh session over Wi-Fi drops" && [ "$(printf "%s\n" "$OUT" | grep -c "^          sudo reboot$")" = 2 ]'
fi
# the packages missing: apt-get installs polkitd and bluez (never network-manager)
R=$(pc_root pc-missing)
FAKE_DPKG_MISSING="polkitd bluez"; FAKE_APT_RC=0; export FAKE_DPKG_MISSING FAKE_APT_RC
pcfull "$R" "$BUN64" x86_64
unset FAKE_DPKG_MISSING FAKE_APT_RC
check "fake PC install, polkitd and bluez missing: apt-get installs exactly them" '[ $RC = 0 ] && called "apt-get update" && called "apt-get install -y --no-install-recommends polkitd bluez"'
# polkit does not answer: the install goes on, warned twice (after the rules, at the end)
R=$(pc_root pc-nopolkit)
FAKE_POLKIT=missing; export FAKE_POLKIT
pcfull "$R" "$BUN64" x86_64
unset FAKE_POLKIT
check "fake PC install without polkit: exit 0, Reboot / Shut down / the clock named in a WARNING, the fix given" '[ $RC = 0 ] && has "WARNING: polkit is not available" && has "WARNING: no polkit: Settings > Reboot / Shut down and the clock settings will not work. Fix: sudo apt install polkitd"'
# NetworkManager there (an nmcli on PATH): no notice
NMBIN="$TMP/fake-nm"; mkdir -p "$NMBIN"; printf '#!/bin/sh\nexit 0\n' > "$NMBIN/nmcli"; cp "$NMBIN/nmcli" "$NMBIN/bluetoothctl"; chmod 755 "$NMBIN"/*
R=$(pc_root pc-nm)
OPATH=$PATH; PATH="$NMBIN:$PATH"
pcfull "$R" "$BUN64" x86_64
PATH=$OPATH
check "fake PC install with NetworkManager: no notice" '[ $RC = 0 ] && ! has "NOTICE: network-manager"'
R=$(pc_root pc-wrongarch)
pcfull "$R" "$BUNARM" x86_64
check "the arm64 bundle on a PC: refused before any change, the right bundle named" '[ $RC = 1 ] && has "this bundle is for arm64, this system is amd64" && has "cyberdeckzero-launcher-<version>-amd64.tar.gz" && [ ! -e "$R/usr/share/APPLaunch" ] && [ ! -e "$R/etc/applaunch" ]'
R=$(root pi3a-luckfox35)
pcfull "$R" "$BUN64" aarch64
check "the amd64 bundle on a Pi: refused" '[ $RC = 1 ] && has "this bundle is for amd64, this system is arm64" && [ ! -e "$R/usr/share/APPLaunch" ]'
pcfull "$R" "$BUNARM" aarch64 --no-boot-config
check "the arm64 bundle on a Pi: accepted as before" '[ $RC = 0 ] && has "profile pi3a-luckfox35"'

# --- switching between the variants: a desktop installed later on a board installed as an appliance, then install.sh
# again, and back. The FZ-M1 tablet (pc profile, amd64) installed as an appliance, then XFCE as armbian-config sets it up
# (lightdm enabled through display-manager.service, lightdm and Xorg running, graphical.target).
R=$(pc_root pc-switch)
H="$R$UHOME/.config/systemd/user"; P="$R/etc/applaunch/board.conf"; DU="$R/etc/systemd/system/cyberdeckzero-desktop.service"
pcfull "$R" "$BUN64" x86_64
check "switch: the appliance install first (user service, console unit, no switch)" '[ $RC = 0 ] && has "session: appliance (no display manager enabled or running: the launcher starts at boot)" && [ -f "$H/cyberdeckzero.service" ] && [ -f "$R/etc/systemd/system/cyberdeckzero-console.service" ] && ! has "variant: switching" && ! has "Switched from"'
# the enablement links systemd would have made (the stand-in systemctl makes none)
mkdir -p "$H/default.target.wants" "$R/etc/systemd/system/multi-user.target.wants"
ln -sf ../cyberdeckzero.service "$H/default.target.wants/cyberdeckzero.service"
ln -sf ../cyberdeckzero-console.service "$R/etc/systemd/system/multi-user.target.wants/cyberdeckzero-console.service"
CONF_SUM=$(cksum < "$P"); BOOTSUM=$(cat "$R/boot/grub/grub.cfg" "$R/etc/default/grub" | cksum)
desktop "$R" lightdm.service lightdm Xorg xfce4-session
ln -sf /lib/systemd/system/graphical.target "$R/etc/systemd/system/default.target"
pcrun --dry-run --user "$U"
check "switch dry-run: desktop session and its evidence" '[ $RC = 0 ] && has "session: desktop (display manager lightdm.service enabled; running: Xorg+lightdm)" && ! has "WARNING: the system boots"'
check "switch dry-run: the switch planned, what it stops, disables and removes, what it keeps" 'has "switch from the appliance variant to the desktop variant" && has "user service cyberdeckzero.service of $U: stopped if it runs (the launcher on the panel quits now" && has "the launcher and lightdm.service never both start at boot" && has "cyberdeckzero-console.service: disabled and stopped (the text console is bound again), removed" && has "kept as they are: lingering of $U, getty@tty1" && has "sudo systemctl start lightdm.service"'
check "switch dry-run: the appliance files found, by path" 'has "      $UHOME/.config/systemd/user/cyberdeckzero.service" && has "      $UHOME/.config/systemd/user/default.target.wants/cyberdeckzero.service" && has "      /etc/systemd/system/cyberdeckzero-console.service" && has "      /etc/systemd/system/multi-user.target.wants/cyberdeckzero-console.service"'
check "switch dry-run: then the desktop variant, kbd for chvt, the unit for lightdm" 'has "/etc/systemd/system/cyberdeckzero-desktop.service for $U, not enabled" && has "polkitd bluez kbd (chvt, for the desktop unit)" && [ -n "$(unit_text | grep -x "Conflicts=lightdm.service")" ] && has "later: if you install or remove a desktop (a display manager), run install.sh again"'
check "switch dry-run: nothing changed" '[ -f "$H/cyberdeckzero.service" ] && [ -f "$R/etc/systemd/system/cyberdeckzero-console.service" ] && [ ! -e "$DU" ] && [ "$(cksum < "$P")" = "$CONF_SUM" ]'
# the launcher runs on the panel while the switch happens
FAKE_USER_ACTIVE=cyberdeckzero.service; export FAKE_USER_ACTIVE
pcfull "$R" "$BUN64" x86_64
unset FAKE_USER_ACTIVE
check "switch to desktop: exit 0, the switch announced and done as a step" '[ $RC = 0 ] && has "variant: switching from the appliance variant to the desktop variant (a display manager is there now)" && has "== Switching from the appliance variant to the desktop variant"'
check "switch to desktop: the running launcher is said to stop, then stopped and disabled through the user's systemd" 'has "the launcher is running on the panel (cyberdeckzero.service): it is stopped now" && called "systemctl --user disable --now cyberdeckzero.service"'
check "switch to desktop: user service and its enablement link removed, lingering left as it is" '[ ! -e "$H/cyberdeckzero.service" ] && [ ! -L "$H/default.target.wants/cyberdeckzero.service" ] && ! called "loginctl" && has "lingering"'
check "switch to desktop: console unit disabled and stopped, its file and link removed, not enabled again" 'called "systemctl disable --now cyberdeckzero-console.service" && [ ! -e "$R/etc/systemd/system/cyberdeckzero-console.service" ] && [ ! -L "$R/etc/systemd/system/multi-user.target.wants/cyberdeckzero-console.service" ] && ! called "enable --now cyberdeckzero-console.service"'
check "switch to desktop: the appliance units go before the services are installed" '[ "$(at "systemctl --user disable --now cyberdeckzero.service")" -gt 0 ] && [ "$(at "disable --now cyberdeckzero-console.service")" -lt "$(at "systemctl enable launcher-ntp-default.service")" ] && [ "$(at "systemctl --user disable --now cyberdeckzero.service")" -lt "$(at "systemctl enable launcher-ntp-default.service")" ]'
check "switch to desktop: desktop unit for the user and lightdm, menu entry, polkit rule" 'grep -qx "User=$U" "$DU" && grep -qx "Conflicts=lightdm.service" "$DU" && grep -qx "After=lightdm.service" "$DU" && grep -qx "ExecStopPost=+/usr/share/APPLaunch/bin/cyberdeckzero-console restore-desktop lightdm.service" "$DU" && grep -qx "Name=CyberdeckZero Launcher" "$R/usr/share/applications/cyberdeckzero.desktop" && grep -q "subject.user == \"$U\"" "$R/etc/polkit-1/rules.d/52-cyberdeckzero-desktop.rules"'
# the unit's own environment against the pc profile: no APPLAUNCH_BOARD of its own, no key with another value
CLASH=$(sed -n 's/^Environment=\([A-Z_][A-Z0-9_]*\)=\(.*\)$/\1 \2/p' "$DU" | while read -r k v; do
    b=$(sed -n "s/^$k=//p" "$P"); [ -z "$b" ] || [ "$b" = "$v" ] || echo "$k"; done)
check "switch to desktop: the unit fits the pc profile (no APPLAUNCH_BOARD, no clashing value, the profile loaded, the binary by path)" '! grep -q "APPLAUNCH_BOARD" "$DU" && [ -z "$CLASH" ] && grep -qx "EnvironmentFile=-/etc/applaunch/board.conf" "$DU" && grep -qx "ExecStart=/usr/bin/stdbuf -oL -eL /usr/share/APPLaunch/bin/cyberdeckzero" "$DU" && grep -qx "APPLAUNCH_KBD_IGNORE=bus:0011" "$P" && grep -qx "APPLAUNCH_DISPLAY=dpi-scaled" "$P"'
check "switch to desktop: board.conf unchanged (no .bak), no boot file touched" '[ "$(cksum < "$P")" = "$CONF_SUM" ] && [ ! -e "$P.bak" ] && [ "$(cat "$R/boot/grub/grub.cfg" "$R/etc/default/grub" | cksum)" = "$BOOTSUM" ] && [ ! -e "$R/boot/firmware" ]'
check "switch to desktop: kbd checked (chvt), nothing started, getty@tty1 left alone" 'called "dpkg -s kbd" && ! called "start cyberdeckzero-desktop.service" && ! called "restart cyberdeckzero.service" && ! called "getty@tty1" && has "not started now (it would stop the desktop)"'
check "switch to desktop: the end names the session, the switch, how the desktop comes up, the menu, and to run install.sh again" 'has "session: desktop (display manager lightdm.service enabled; running: Xorg+lightdm)" && has "Switched from the appliance variant: the launcher no longer starts at boot, lightdm.service does." && has "The desktop comes up at the next boot (sudo reboot), or at once with: sudo systemctl start lightdm.service" && has "its open programs are closed" && has "If you install or remove a desktop (a display manager) later, run install.sh again"'
SNAP1=$(snapshot "$R")
FAKE_ACTIVE=lightdm.service; export FAKE_ACTIVE
pcfull "$R" "$BUN64" x86_64
unset FAKE_ACTIVE
check "switch to desktop, second run: no switch, no appliance unit called, the same tree" '[ $RC = 0 ] && ! has "variant: switching" && ! has "== Switching" && ! called "cyberdeckzero.service" && ! called "cyberdeckzero-console.service" && ! called "systemctl --user" && [ "$(snapshot "$R")" = "$SNAP1" ]'
check "switch to desktop, second run: the desktop running, no start hint" '! has "The desktop comes up at the next boot" && ! has "Switched from"'
# the desktop removed again as armbian-config's remove does it: lightdm purged (display-manager.service may be left
# pointing nowhere), multi-user.target, nothing of the desktop running: back to the appliance variant
rm -f "$R/lib/systemd/system/lightdm.service"; rm -rf "$R"/proc/7[0-9][0-9]
ln -sf /lib/systemd/system/multi-user.target "$R/etc/systemd/system/default.target"
pcrun --dry-run --user "$U"
check "switch back dry-run: appliance session (the link left by the removed lightdm named), the desktop files listed, nothing changed" '[ $RC = 0 ] && has "session: appliance (display-manager.service points to lightdm.service, which is not installed (a removed desktop): the launcher starts at boot)" && has "switch from the desktop variant to the appliance variant" && has "      /etc/systemd/system/cyberdeckzero-desktop.service" && has "      /usr/share/applications/cyberdeckzero.desktop" && has "      /etc/polkit-1/rules.d/52-cyberdeckzero-desktop.rules" && [ -f "$DU" ] && ! has "WARNING"'
# lightdm still installed and enabled but the system boots to multi-user.target: it never starts, the same switch
echo "[Unit]" > "$R/lib/systemd/system/lightdm.service"
pcrun --dry-run --user "$U"
check "switch back dry-run, lightdm enabled but multi-user.target: appliance, said why and how to get the desktop" '[ $RC = 0 ] && has "session: appliance (display manager lightdm.service enabled, but the system boots to multi-user.target, where it does not start: the launcher starts at boot (for the desktop variant: sudo systemctl set-default graphical.target, then install.sh again))" && has "switch from the desktop variant to the appliance variant"'
rm -f "$R/lib/systemd/system/lightdm.service"
pcfull "$R" "$BUN64" x86_64
check "switch to appliance: exit 0, announced, the desktop unit, menu entry and polkit rule removed" '[ $RC = 0 ] && has "variant: switching from the desktop variant to the appliance variant (no display manager any more)" && has "== Switching from the desktop variant to the appliance variant" && [ ! -e "$DU" ] && [ ! -e "$R/usr/share/applications/cyberdeckzero.desktop" ] && [ ! -e "$R/etc/polkit-1/rules.d/52-cyberdeckzero-desktop.rules" ]'
check "switch to appliance: user service enabled and started, lingering, console unit, getty@tty1 masked" '[ -f "$H/cyberdeckzero.service" ] && [ -f "$R/etc/systemd/system/cyberdeckzero-console.service" ] && called "systemctl --user enable cyberdeckzero.service" && called "systemctl --user restart cyberdeckzero.service" && called "loginctl enable-linger $U" && called "systemctl enable --now cyberdeckzero-console.service" && called "systemctl mask getty@tty1.service"'
check "switch to appliance: the desktop files go before the console unit is enabled; the menu launcher was not running: not stopped" '[ "$(at "is-active --quiet cyberdeckzero-desktop.service")" -gt 0 ] && [ "$(at "is-active --quiet cyberdeckzero-desktop.service")" -lt "$(at "enable --now cyberdeckzero-console.service")" ] && ! called "stop cyberdeckzero-desktop.service"'
check "switch to appliance: board.conf and the boot files unchanged, no kbd asked for" '[ "$(cksum < "$P")" = "$CONF_SUM" ] && [ ! -e "$P.bak" ] && [ "$(cat "$R/boot/grub/grub.cfg" "$R/etc/default/grub" | cksum)" = "$BOOTSUM" ] && ! called "dpkg -s kbd"'
check "switch to appliance: the end names the session and the switch, and to run install.sh again" 'has "session: appliance (display-manager.service points to lightdm.service, which is not installed" && has "Switched from the desktop variant: the menu entry is gone, the launcher starts at boot." && has "If you install or remove a desktop (a display manager) later, run install.sh again"'
SNAP1=$(snapshot "$R")
pcfull "$R" "$BUN64" x86_64
check "switch to appliance, second run: no switch, the desktop unit not looked at, the same tree" '[ $RC = 0 ] && ! has "variant: switching" && ! has "== Switching" && ! called "cyberdeckzero-desktop.service" && [ "$(snapshot "$R")" = "$SNAP1" ]'
# back to the appliance while the launcher started from the menu still runs: left running, the user service waits for
# the next boot (no second launcher now, no display manager started by its exit)
R=$(pc_root pc-switch-running); DU="$R/etc/systemd/system/cyberdeckzero-desktop.service"
desktop "$R" lightdm.service lightdm Xorg
pcfull "$R" "$BUN64" x86_64
check "desktop install from the start on the tablet: no switch, no user service, no console unit" '[ $RC = 0 ] && ! has "variant: switching" && [ -f "$DU" ] && [ ! -e "$R$UHOME/.config/systemd/user/cyberdeckzero.service" ] && [ ! -e "$R/etc/systemd/system/cyberdeckzero-console.service" ] && ! called "systemctl --user"'
# (armbian-config's disable: "systemctl disable display-manager", the alias link goes, graphical.target stays)
rm -f "$R/etc/systemd/system/display-manager.service"; rm -rf "$R"/proc/7[0-9][0-9]
FAKE_ACTIVE=cyberdeckzero-desktop.service; export FAKE_ACTIVE
pcfull "$R" "$BUN64" x86_64
unset FAKE_ACTIVE
check "switch to appliance with the menu launcher running: not stopped, said so, its files removed" '[ $RC = 0 ] && ! called "systemctl stop cyberdeckzero-desktop.service" && has "is running: it is left running, not stopped" && [ ! -e "$DU" ] && [ ! -e "$R/usr/share/applications/cyberdeckzero.desktop" ]'
check "switch to appliance with the menu launcher running: user service enabled, not started, reboot asked" 'called "systemctl --user enable cyberdeckzero.service" && ! called "restart cyberdeckzero.service" && has "next boot: sudo reboot"'
# the boot path of the desktop variant: the display manager must be enabled and the system must boot to graphical.target
R=$(pc_root pc-multiuser)
desktop "$R" lightdm.service lightdm
ln -sf /lib/systemd/system/multi-user.target "$R/etc/systemd/system/default.target"
pcrun --dry-run --user "$U"
check "desktop booting to multi-user.target: warned, the fix given" '[ $RC = 0 ] && has "WARNING: the system boots to multi-user.target (systemctl get-default): lightdm.service does not start at boot" && has "sudo systemctl set-default graphical.target"'
ln -sf /usr/lib/systemd/system/graphical.target "$R/etc/systemd/system/default.target"
pcrun --dry-run --user "$U"
check "desktop booting to graphical.target: no warning" '[ $RC = 0 ] && ! has "WARNING: the system boots" && ! has "no display manager is enabled for boot"'
R=$(pc_root pc-noalias)
desktop "$R" - lightdm Xorg
pcrun --dry-run --user "$U"
check "desktop running without display-manager.service: warned" 'has "session: desktop (running: Xorg+lightdm)" && has "WARNING: no display manager is enabled for boot"'
# the deck installed as an appliance, then a desktop: still refused, before anything (no switch, nothing changed)
R=$(root zero2w)
full "$R"
desktop "$R" lightdm.service lightdm
SNAP1=$(snapshot "$R")
full "$R"
check "deck appliance with a desktop now: refused, nothing switched, nothing changed" '[ $RC = 1 ] && has "has no desktop variant" && ! has "variant: switching" && ! called "disable" && [ "$(snapshot "$R")" = "$SNAP1" ]'
run --dry-run --user "$U"
check "deck appliance with a desktop now, dry run: no switch planned" '[ $RC = 0 ] && has "has no desktop variant: install.sh would stop here" && ! has "switch from the appliance"'

# --- default app source: install.sh's plan and option
R=$(root zero2w)
run --dry-run
check "install dry-run: default app source planned" 'has "default app source: OSRdesign/cyberdeck-zero-apps added for the user once"'
run --dry-run --no-default-source
check "install dry-run --no-default-source: skipped" '[ $RC = 0 ] && has "default app source: skipped (--no-default-source)"'
check "install.sh runs default-sources.sh as the user" 'grep -q "runuser -u \"\$TARGET_USER\" -- env HOME=\"\$TARGET_HOME\" sh \"\$HERE/default-sources.sh\"" "$INSTALL"'
check "build.sh puts default-sources.sh in the bundle" 'grep -q "\$HERE/default-sources.sh" "$HERE/../build.sh"'

# --- default-sources.sh against a temp state folder (M5APPSTORE_STATE_DIR) and a fake Store backend
SEED="$HERE/../default-sources.sh"
SRC_URL=https://raw.githubusercontent.com/OSRdesign/cyberdeck-zero-apps/main/registry.json
FAKE="$TMP/fake-appstore"
LOG="$TMP/fake-appstore.log"
cat > "$FAKE" <<'EOF'
#!/bin/sh
# fake Store backend: logs its arguments; FAKE_FAIL=1 answers as for an unreachable registry
printf '%s\n' "$*" >> "$FAKE_LOG"
if [ "${FAKE_FAIL:-0}" = 1 ]; then printf 'ERROR\tregistry unavailable\t%s\n' "$2"; exit 1; fi
printf '2026-10-11 INFO sync\nREGISTRY\tUPDATED\t%s\t%s\tok\t9\t%s\n' "$2" "$3" "$5"
EOF
chmod +x "$FAKE"
# run default-sources.sh; output in $OUT, exit code in $RC; state folder $S
seed() { OUT=$(HOME="$TMP/home" APPLAUNCH_ARCH="${SEED_ARCH:-arm64}" M5APPSTORE_STATE_DIR="$S" APPLAUNCH_APPSTORE_BIN="${BIN:-$FAKE}" FAKE_LOG="$LOG" \
                FAKE_FAIL="${SYNC_FAIL:-0}" sh "$SEED" "$@" 2>&1); RC=$?; }
calls() { if [ -f "$LOG" ]; then wc -l < "$LOG" | tr -d ' '; else echo 0; fi; }
json_ok() { if command -v python3 >/dev/null 2>&1; then python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$1"; else true; fi; }
HUB='    {
      "builtin": true,
      "enabled": true,
      "name": "CardputerZero Hub",
      "region": "default",
      "url": "https://cardputerzero.github.io/generated/registry.json"
    }'
HUB_ONLY="{
  \"active_region\": \"default\",
  \"region\": \"auto\",
  \"registries\": [
$HUB
  ]
}"
EXPECT_SEED="{
  \"active_region\": \"default\",
  \"region\": \"auto\",
  \"registries\": [
$HUB,
    {
      \"enabled\": true,
      \"name\": \"OSRdesign/cyberdeck-zero-apps\",
      \"url\": \"$SRC_URL\"
    }
  ]
}"

# fresh install: no state folder at all
S="$TMP/state-fresh"; rm -f "$LOG"
seed --dry-run
check "seed dry-run: plan and JSON, nothing written" '[ $RC = 0 ] && has "would add OSRdesign/cyberdeck-zero-apps" && has "$SRC_URL" && [ ! -e "$S" ] && [ "$(calls)" = 0 ]'
seed
check "seed fresh: exit 0, added and synced" '[ $RC = 0 ] && has "added OSRdesign/cyberdeck-zero-apps" && has "synced: 9 apps (ok)"'
check "seed fresh: the Store format exactly" '[ "$(cat "$S/registries.json")" = "$EXPECT_SEED" ]'
check "seed fresh: valid JSON" 'json_ok "$S/registries.json"'
check "seed fresh: one sync of that source only" '[ "$(cat "$LOG")" = "--edit-registry $SRC_URL $SRC_URL --registry-name OSRdesign/cyberdeck-zero-apps" ]'
check "seed fresh: marker written, no backup" '[ -f "$S/applaunch-default-source" ] && [ ! -e "$S/registries.json.bak-applaunch" ]'
SUM=$(cksum < "$S/registries.json")
seed
check "seed second run: decided already, nothing changed, no sync" '[ $RC = 0 ] && has "already decided" && [ "$(cksum < "$S/registries.json")" = "$SUM" ] && [ "$(calls)" = 1 ]'
# the user removed the source afterwards (the Store then keeps the hub only): a re-run does not add it back
printf '%s\n' "$HUB_ONLY" > "$S/registries.json"
seed
check "seed after the user removed it: not added back" 'has "already decided" && [ "$(cat "$S/registries.json")" = "$HUB_ONLY" ]'

# a PC (amd64): the source is registry-amd64.json and the arm64-only hubs are not seeded (X8, X13)
AMD_URL=https://raw.githubusercontent.com/OSRdesign/cyberdeck-zero-apps/main/registry-amd64.json
EXPECT_AMD="{
  \"active_region\": \"default\",
  \"region\": \"auto\",
  \"registries\": [
    {
      \"enabled\": true,
      \"name\": \"OSRdesign/cyberdeck-zero-apps\",
      \"url\": \"$AMD_URL\"
    }
  ]
}"
S="$TMP/state-amd64"; rm -f "$LOG"; SEED_ARCH=amd64
seed
check "seed amd64: registry-amd64.json, no hub, synced" '[ $RC = 0 ] && has "added OSRdesign/cyberdeck-zero-apps" && has "synced: 9 apps (ok)" && grep -q "registry-amd64.json" "$S/registries.json" && ! grep -q "cardputer" "$S/registries.json"'
check "seed amd64: the Store format exactly, one sync of the amd64 registry" '[ "$(cat "$S/registries.json")" = "$EXPECT_AMD" ] && json_ok "$S/registries.json" && [ "$(cat "$LOG")" = "--edit-registry $AMD_URL $AMD_URL --registry-name OSRdesign/cyberdeck-zero-apps" ]'
SEED_ARCH=arm64

# existing install whose list holds the built-in hub only (region China): seeded, region kept, old file backed up
S="$TMP/state-hub"; rm -f "$LOG"; mkdir -p "$S"
printf '%s\n' "$HUB_ONLY" | sed -e 's/"active_region": "default"/"active_region": "CN"/' -e 's/"region": "auto"/"region": "CN"/' \
    -e 's/"name": "CardputerZero Hub"/"name": "CardputerZero Hub CN"/' -e 's/"region": "default"/"region": "CN"/' \
    -e 's|https://cardputerzero.github.io/generated/registry.json|https://cardputer-zero-repo.oss-cn-shenzhen.aliyuncs.com/packages/cn/registry.json|' \
    > "$S/registries.json"
BEFORE=$(cat "$S/registries.json")
seed --no-sync
check "seed hub only: added, not synced (--no-sync)" '[ $RC = 0 ] && has "added OSRdesign" && has "not synced (--no-sync)" && [ "$(calls)" = 0 ]'
check "seed hub only: CN region and hub kept" 'grep -q "^  \"region\": \"CN\",$" "$S/registries.json" && grep -q "^  \"active_region\": \"CN\",$" "$S/registries.json" && grep -q "Hub CN" "$S/registries.json" && grep -q "$SRC_URL" "$S/registries.json"'
check "seed hub only: valid JSON, backup of the old file" 'json_ok "$S/registries.json" && [ "$(cat "$S/registries.json.bak-applaunch")" = "$BEFORE" ]'

# the user's own list (another source): never changed, decision recorded, no sync
S="$TMP/state-own"; rm -f "$LOG"; mkdir -p "$S"
cat > "$S/registries.json" <<EOF
{
  "active_region": "default",
  "region": "auto",
  "registries": [
$HUB,
    {
      "enabled": false,
      "name": "me/my-apps",
      "url": "https://raw.githubusercontent.com/me/my-apps/main/registry.json"
    }
  ]
}
EOF
SUM=$(cksum < "$S/registries.json")
seed
check "seed own list: kept as is, no sync" '[ $RC = 0 ] && has "own sources (1) are kept" && [ "$(cksum < "$S/registries.json")" = "$SUM" ] && [ "$(calls)" = 0 ] && [ -f "$S/applaunch-default-source" ]'

# the source is already there (added by hand, disabled): left alone
S="$TMP/state-ours"; rm -f "$LOG"; mkdir -p "$S"
printf '{"registries":[{"url":"%s","name":"OSRdesign/cyberdeck-zero-apps","enabled":false}]}\n' "$SRC_URL" > "$S/registries.json"
SUM=$(cksum < "$S/registries.json")
seed
check "seed already listed: left alone" '[ $RC = 0 ] && has "already in the list" && [ "$(cksum < "$S/registries.json")" = "$SUM" ] && [ "$(calls)" = 0 ]'

# sync failures never fail the install; the list stays written
S="$TMP/state-offline"; rm -f "$LOG"
SYNC_FAIL=1; seed; SYNC_FAIL=0
check "seed offline: exit 0, reason shown, list written" '[ $RC = 0 ] && has "could not sync it now (registry unavailable)" && [ "$(cat "$S/registries.json")" = "$EXPECT_SEED" ]'
S="$TMP/state-nostore"; rm -f "$LOG"
BIN="$TMP/no-such-appstore"; seed; BIN=""
check "seed without the Store: list written, not synced" '[ $RC = 0 ] && has "is not installed: not synced" && [ -f "$S/registries.json" ]'
seed --bogus
check "seed unknown option: exit 2" '[ $RC = 2 ]'

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ]
