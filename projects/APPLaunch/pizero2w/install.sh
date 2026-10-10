#!/bin/sh
# Install the launcher port on a Raspberry Pi Zero 2W with the Waveshare 2.8" DPI LCD (the deck), or on another
# known board (see README.md, "Boards"): the board is detected and its profile written to /etc/applaunch/board.conf.
#
# Run on the Pi, from the unpacked bundle (see build.sh):   sudo ./install.sh [options]
# It is idempotent: running it again updates the files and leaves everything else as it is.
#
# Options:
#   --keyboard-name NAME   Bluetooth keyboard name as shown in `bluetoothctl devices` (default: "M4 Keyboard")
#   --user NAME            user that runs the launcher (default: the user who ran sudo)
#   --no-boot-config       do not touch /boot/firmware/config.txt and cmdline.txt (no PWM backlight)
#   --board NAME           use this board profile instead of detecting it (zero2w, pi3a-luckfox35)
#   --board-only           only detect the board and write/update /etc/applaunch/board.conf, then stop
#   --dry-run              print the detected board and what would be done, change nothing
#   --force                continue on an unknown board (no profile is written); replace a hand-edited
#                          /etc/applaunch/board.conf (the old one is kept as board.conf.bak)
#
# To remove everything this script adds, run ./uninstall.sh (see README.md, "Uninstall").
#
# Prerequisites that this script does NOT do:
#   - Waveshare DPI panel overlays in config.txt (see config.txt.snippet and the Waveshare wiki);
#   - pairing the Bluetooth keyboard (bluetoothctl).
set -eu

KEYBOARD_NAME="M4 Keyboard"
TARGET_USER=""
BOOT_CONFIG=1
FORCE=0
DRY=0
BOARD_ONLY=0
BOARD_ARG=""
for arg in "$@"; do
    case "$arg" in
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        --dry-run) DRY=1 ;;
    esac
done
# Become root first, while "$@" still holds the options (the loop below consumes them). A dry run and the
# SYSROOT test hook need no root.
if [ "$DRY" = 0 ] && [ -z "${SYSROOT:-}" ] && [ "$(id -u)" -ne 0 ]; then
    exec sudo -E sh "$0" "$@"
fi
while [ $# -gt 0 ]; do
    case "$1" in
        --keyboard-name) [ $# -ge 2 ] || { echo "--keyboard-name needs a name" >&2; exit 2; }; KEYBOARD_NAME="$2"; shift 2 ;;
        --user) [ $# -ge 2 ] || { echo "--user needs a name" >&2; exit 2; }; TARGET_USER="$2"; shift 2 ;;
        --no-boot-config) BOOT_CONFIG=0; shift ;;
        --board) [ $# -ge 2 ] || { echo "--board needs a name (zero2w, pi3a-luckfox35)" >&2; exit 2; }; BOARD_ARG="$2"; shift 2 ;;
        --board-only) BOARD_ONLY=1; shift ;;
        --dry-run) DRY=1; shift ;;
        --force) FORCE=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# SYSROOT is a test hook: board detection reads $SYSROOT/proc and $SYSROOT/sys, and the profile and the
# backlight udev rule are written under $SYSROOT/etc. Leave it unset on a real board.
# INSTALL_GLIBC_VERSION is a test hook too: it replaces the glibc version read from the system.
SYSROOT=${SYSROOT:-}
if [ -n "$SYSROOT" ] && [ "$DRY" = 0 ] && [ "$BOARD_ONLY" = 0 ]; then
    echo "SYSROOT is a test hook: use it only with --dry-run or --board-only" >&2; exit 2
fi
PROFILE_DIR="$SYSROOT/etc/applaunch"
PROFILE="$PROFILE_DIR/board.conf"
GPIO_BL_RULE="92-applaunch-backlight-gpio.rules"

# ---------------------------------------------------------------------------------------------------------
# glibc floor: the launcher and every app package are built against Debian 13 (they need GLIBC_2.38).
# ---------------------------------------------------------------------------------------------------------
glibc_version() {
    if [ -n "${INSTALL_GLIBC_VERSION:-}" ]; then echo "$INSTALL_GLIBC_VERSION"; return; fi
    v=$(getconf GNU_LIBC_VERSION 2>/dev/null | sed -n 's/^glibc \([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')
    [ -n "$v" ] || v=$(ldd --version 2>/dev/null | head -n 1 | grep -o '[0-9][0-9]*\.[0-9][0-9]*' | tail -n 1)
    echo "$v"
}
GLIBC=$(glibc_version)
GLIBC_OK=0
case "$GLIBC" in
    [0-9]*.[0-9]*)
        GMAJ=${GLIBC%%.*}; GMIN=${GLIBC#*.}; GMIN=${GMIN%%.*}
        if [ "$GMAJ" -gt 2 ] || { [ "$GMAJ" -eq 2 ] && [ "$GMIN" -ge 38 ]; }; then GLIBC_OK=1; fi ;;
esac
if [ "$GLIBC_OK" != 1 ]; then
    echo "this system has glibc ${GLIBC:-unknown}; the launcher and its apps need glibc 2.38 or newer" >&2
    echo "(Debian 13 trixie / Raspberry Pi OS trixie). Nothing was changed." >&2
    exit 1
fi

# ---------------------------------------------------------------------------------------------------------
# Board detection: device-tree model, connected DRM connectors, framebuffers. Read only.
# ---------------------------------------------------------------------------------------------------------
rd() { [ -r "$1" ] && tr -d '\0' < "$1" 2>/dev/null | head -n 1; }
MODEL=$(rd "$SYSROOT/proc/device-tree/model" || true)

# the framebuffer device whose fbdev name starts with $1 and whose virtual_size is $2 (e.g. 320,480)
find_fb() {
    for d in "$SYSROOT"/sys/class/graphics/fb[0-9]*; do
        [ -d "$d" ] || continue
        n=$(rd "$d/name" || true)
        s=$(rd "$d/virtual_size" || true)
        case "$n" in "$1"*) [ "$s" = "$2" ] && { echo "/dev/${d##*/}"; return 0; } ;; esac
    done
    return 1
}
# true if a connected DRM connector matching card*-$1 lists the mode $2
drm_connected() {
    for d in "$SYSROOT"/sys/class/drm/card[0-9]*-$1; do
        [ -d "$d" ] || continue
        [ "$(rd "$d/status" || true)" = connected ] || continue
        grep -qx "$2" "$d/modes" 2>/dev/null && return 0
    done
    return 1
}
print_facts() {
    echo "  model: ${MODEL:-unknown}"
    found=0
    for d in "$SYSROOT"/sys/class/drm/card[0-9]*-*; do
        [ -e "$d/status" ] || continue
        found=1
        echo "  drm ${d##*/}: $(rd "$d/status" || echo '?')$(m=$(rd "$d/modes" || true); [ -n "$m" ] && echo ", first mode $m")"
    done
    [ "$found" = 1 ] || echo "  drm: no connector"
    found=0
    for d in "$SYSROOT"/sys/class/graphics/fb[0-9]*; do
        [ -d "$d" ] || continue
        found=1
        echo "  /dev/${d##*/}: name \"$(rd "$d/name" || true)\", size $(rd "$d/virtual_size" || true), $(rd "$d/bits_per_pixel" || true) bpp"
    done
    [ "$found" = 1 ] || echo "  framebuffer: none"
    bl=""
    for d in "$SYSROOT"/sys/class/backlight/*; do [ -e "$d" ] && bl="$bl ${d##*/}"; done
    echo "  backlight:${bl:- none}"
}

# BOARD: zero2w | pi3a-luckfox35 | "" (unknown). PANEL_FB: the panel's framebuffer for pi3a-luckfox35.
BOARD=""
PANEL_FB=""
detect_pi3a_luckfox35() {
    PANEL_FB=$(find_fb panel-mipi-dbi 320,480 || true)
    [ -n "$PANEL_FB" ] && drm_connected 'SPI-*' 320x480
}
if [ -n "$BOARD_ARG" ]; then
    case "$BOARD_ARG" in
        zero2w) BOARD=zero2w ;;
        pi3a-luckfox35)
            BOARD=pi3a-luckfox35
            PANEL_FB=$(find_fb panel-mipi-dbi 320,480 || true)
            if [ -z "$PANEL_FB" ]; then
                echo "--board pi3a-luckfox35: no 320x480 \"panel-mipi-dbi\" framebuffer found (is the panel overlay loaded?)." >&2
                print_facts >&2
                echo "Nothing was changed." >&2
                exit 1
            fi ;;
        *) echo "unknown board \"$BOARD_ARG\" (known: zero2w, pi3a-luckfox35)" >&2; exit 2 ;;
    esac
else
    case "$MODEL" in
        *"Zero 2"*) BOARD=zero2w ;;
        *"Raspberry Pi 3 Model A Plus"*) detect_pi3a_luckfox35 && BOARD=pi3a-luckfox35 ;;
    esac
fi

# The profile for the board ("" = no profile file: the deck keeps the service's own environment).
# It is a systemd EnvironmentFile: its values override the Environment= lines of APPLaunch.service,
# so the deck's legacy touch axes and fb0 are neutralised here instead of being removed from the service.
profile_body() {
    case "$BOARD" in
        pi3a-luckfox35) cat <<EOF
APPLAUNCH_BOARD=pi3a-luckfox35
APPLAUNCH_FB=$PANEL_FB
APPLAUNCH_LOGICAL=480x320
APPLAUNCH_ROTATE=90
APPLAUNCH_COMPAT_SCALE=1
# landscape panel size in mm (the overlay's width-mm=49 height-mm=79, portrait): the layout's density
APPLAUNCH_PANEL_MM=79x49
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
APPLAUNCH_BACKLIGHT=gpio:/sys/class/backlight/backlight_gpio
# same device for the code that still reads the LVGL name (the service sets it to /dev/fb0)
LV_LINUX_FBDEV_DEVICE=$PANEL_FB
# the deck's legacy touch axes (set in APPLaunch.service) must not apply here: neutral values
APPLAUNCH_TOUCH_SWAP_XY=0
APPLAUNCH_TOUCH_INVERT_X=0
APPLAUNCH_TOUCH_INVERT_Y=0
EOF
        ;;
    esac
}
body_sum() { cksum | cut -d' ' -f1-2; }
# The first lines say who wrote the file and a checksum of the rest, so a hand edit can be told apart.
profile_content() {
    body=$(profile_body)
    printf '# Board profile for the launcher, written by install.sh (board: %s).\n' "$BOARD"
    printf '# Loaded by APPLaunch.service (EnvironmentFile). Edit freely: install.sh then leaves it alone unless --force.\n'
    printf '# applaunch-profile-sum: %s\n' "$(printf '%s\n' "$body" | body_sum)"
    printf '%s\n' "$body"
}
# state of the file in place: none | ours (generated, unedited) | edited
profile_state() {
    [ -e "$PROFILE" ] || { echo none; return; }
    want=$(sed -n 's/^# applaunch-profile-sum: //p' "$PROFILE" | head -n 1)
    have=$(sed '1,3d' "$PROFILE" | body_sum)
    { [ -n "$want" ] && [ "$want" = "$have" ]; } && echo ours || echo edited
}
# PROFILE_ACTION: what apply_profile will do, as one word, plus PROFILE_MSG for the user
plan_profile() {
    st=$(profile_state)
    if [ -z "$BOARD" ]; then
        PROFILE_ACTION=keep; PROFILE_MSG="unknown board: no profile written"
        [ "$st" = none ] || PROFILE_MSG="$PROFILE_MSG (the existing $PROFILE is left alone)"
    elif [ "$BOARD" = zero2w ]; then
        case "$st" in
            none) PROFILE_ACTION=keep; PROFILE_MSG="zero2w: no profile file (the service's own environment applies)" ;;
            ours) PROFILE_ACTION=remove; PROFILE_MSG="zero2w: stale generated $PROFILE moved to board.conf.bak" ;;
            edited) if [ "$FORCE" = 1 ]; then PROFILE_ACTION=remove; PROFILE_MSG="zero2w: hand-edited $PROFILE moved to board.conf.bak (--force)"
                    else PROFILE_ACTION=keep; PROFILE_MSG="zero2w: $PROFILE was edited by hand: kept (it overrides the service; --force moves it away)"; fi ;;
        esac
    else
        if [ "$st" = none ]; then PROFILE_ACTION=write; PROFILE_MSG="$BOARD: write $PROFILE"
        elif [ "$(profile_content)" = "$(cat "$PROFILE")" ]; then PROFILE_ACTION=keep; PROFILE_MSG="$BOARD: $PROFILE already up to date"
        elif [ "$st" = ours ]; then PROFILE_ACTION=replace; PROFILE_MSG="$BOARD: update $PROFILE (old one kept as board.conf.bak)"
        elif [ "$FORCE" = 1 ]; then PROFILE_ACTION=replace; PROFILE_MSG="$BOARD: replace hand-edited $PROFILE (--force; old one kept as board.conf.bak)"
        else PROFILE_ACTION=keep; PROFILE_MSG="$BOARD: $PROFILE was edited by hand: kept (use --force to replace it)"; fi
    fi
}
apply_profile() {
    case "$PROFILE_ACTION" in
        write) mkdir -p "$PROFILE_DIR"; profile_content > "$PROFILE.tmp"; chmod 644 "$PROFILE.tmp"; mv "$PROFILE.tmp" "$PROFILE" ;;
        replace) cp -p "$PROFILE" "$PROFILE.bak"; profile_content > "$PROFILE.tmp"; chmod 644 "$PROFILE.tmp"; mv "$PROFILE.tmp" "$PROFILE" ;;
        remove) mv "$PROFILE" "$PROFILE.bak" ;;
        keep) ;;
    esac
    echo "$PROFILE_MSG"
}
# The on/off GPIO backlight of non-PWM boards (the deck has the PWM one, see 90-backlight-unblank.rules)
GPIO_BL=0
if [ "$BOARD" != zero2w ] && [ -d "$SYSROOT/sys/class/backlight/backlight_gpio" ]; then GPIO_BL=1; fi
gpio_bl_rule() {
    cat <<'EOF'
# On/off GPIO backlight (boards without a PWM backlight): let the video group switch it.
# "change" too, so that "udevadm trigger" at install time applies it without a reboot.
SUBSYSTEM=="backlight", KERNEL=="backlight_gpio", ACTION=="add|change", RUN+="/bin/sh -c 'chgrp video /sys/class/backlight/%k/brightness /sys/class/backlight/%k/bl_power; chmod g+w /sys/class/backlight/%k/brightness /sys/class/backlight/%k/bl_power'"
EOF
}

plan_profile
if [ "$DRY" = 1 ]; then
    echo "DRY RUN: nothing will be changed."
    echo "glibc: $GLIBC (ok)"
    echo "detected:"
    print_facts
    echo "board: ${BOARD:-unknown}${BOARD_ARG:+ (forced with --board)}"
    if [ -z "$BOARD" ]; then
        echo "This board is not known to install.sh. Run tools/board-probe/probe-board.sh on it and send the report."
        [ "$FORCE" = 1 ] || echo "install.sh would stop here (--force installs without a profile)."
    fi
    echo "profile: $PROFILE_MSG"
    case "$PROFILE_ACTION" in write|replace) echo "--- $PROFILE"; profile_content ;; esac
    if [ "$GPIO_BL" = 1 ]; then echo "udev: install /etc/udev/rules.d/$GPIO_BL_RULE (video group may switch backlight_gpio)"; fi
    if [ "$BOARD" = zero2w ] || [ -z "$BOARD" ]; then
        [ "$BOOT_CONFIG" = 1 ] && echo "boot config: PWM backlight overlay + cmdline cursor/consoleblank" || echo "boot config: skipped (--no-boot-config)"
    else
        [ "$BOOT_CONFIG" = 1 ] && echo "boot config: cmdline cursor/consoleblank only (the deck's PWM backlight overlay is not for this board)" || echo "boot config: skipped (--no-boot-config)"
    fi
    [ "$BOARD_ONLY" = 1 ] || echo "then: packages, launcher files, udev/polkit rules, services, user service (as for the deck)"
    exit 0
fi

if [ "$BOARD_ONLY" = 1 ]; then
    echo "board: ${BOARD:-unknown}${BOARD_ARG:+ (forced with --board)}"
    if [ -z "$BOARD" ]; then
        echo "detected:"; print_facts
        echo "This board is not known to install.sh. Run tools/board-probe/probe-board.sh on it and send the report."
    fi
    apply_profile
    if [ "$GPIO_BL" = 1 ]; then
        mkdir -p "$SYSROOT/etc/udev/rules.d"
        gpio_bl_rule > "$SYSROOT/etc/udev/rules.d/$GPIO_BL_RULE"
        echo "installed $SYSROOT/etc/udev/rules.d/$GPIO_BL_RULE"
        [ -n "$SYSROOT" ] || { udevadm control --reload; udevadm trigger --subsystem-match=backlight || true; }
    fi
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
PAYLOAD="$HERE/payload"
APP_ROOT=/usr/share/APPLaunch
BOOT=/boot/firmware
[ -d "$BOOT" ] || BOOT=/boot

[ -d "$PAYLOAD" ] || { echo "payload/ not found next to install.sh (unpack the whole bundle)" >&2; exit 1; }
TARGET_USER=${TARGET_USER:-${SUDO_USER:-}}
[ -n "$TARGET_USER" ] && id "$TARGET_USER" >/dev/null 2>&1 || { echo "cannot determine the user: pass --user NAME" >&2; exit 1; }
TARGET_UID=$(id -u "$TARGET_USER")
TARGET_HOME=$(getent passwd "$TARGET_USER" | cut -d: -f6)

say() { printf '\n== %s\n' "$*"; }

say "Checking the board"
echo "board: ${MODEL:-unknown} -> profile ${BOARD:-none (unknown board)}   user: $TARGET_USER   glibc: $GLIBC"
if [ -z "$BOARD" ]; then
    echo "detected:"; print_facts
    echo "This board is not known to install.sh. Run tools/board-probe/probe-board.sh on it and send the report."
    [ "$FORCE" = 1 ] || { echo "stopping here (use --force to install anyway, without a board profile)" >&2; exit 1; }
fi
[ "$(uname -m)" = aarch64 ] || { echo "a 64-bit (aarch64) OS is required" >&2; exit 1; }

say "Installing packages"
PKGS="libinput10 libxkbcommon0 libfreetype6 curl device-tree-compiler"
MISSING=""
for pkg in $PKGS; do
    dpkg -s "$pkg" >/dev/null 2>&1 || MISSING="$MISSING $pkg"
done
if [ -n "$MISSING" ]; then
    apt-get update
    # shellcheck disable=SC2086
    apt-get install -y --no-install-recommends $MISSING
else
    echo "all packages already present"
fi

say "Installing the launcher into $APP_ROOT"
mkdir -p "$APP_ROOT/bin" "$APP_ROOT/lib" "$APP_ROOT/applications"
cp -a "$PAYLOAD/share/." "$APP_ROOT/"                       # images, fonts, audio, ... (does not delete installed apps)
install -m 755 "$PAYLOAD/bin/M5CardputerZero-APPLaunch" "$APP_ROOT/bin/M5CardputerZero-APPLaunch"
if [ -f "$PAYLOAD/bin/M5CardputerZero-AppStore" ]; then
    install -m 755 "$PAYLOAD/bin/M5CardputerZero-AppStore" "$APP_ROOT/bin/M5CardputerZero-AppStore"
fi
install -m 644 "$PAYLOAD/lib/libapplaunch_vfb.so" "$APP_ROOT/lib/libapplaunch_vfb.so"
chown -R "$TARGET_USER:$TARGET_USER" "$APP_ROOT/applications"
chmod -R a+rX "$APP_ROOT"

say "Installing udev rules (keyboard: \"$KEYBOARD_NAME\")"
install -m 644 "$PAYLOAD/etc/90-backlight-unblank.rules" /etc/udev/rules.d/90-backlight-unblank.rules
install -m 644 "$PAYLOAD/etc/91-applaunch-vkbd.rules" /etc/udev/rules.d/91-applaunch-vkbd.rules
cat > /etc/udev/rules.d/99-bt-keyboard.rules <<EOF
# Stable name for the Bluetooth keyboard (it gets a new event node each time it reconnects).
SUBSYSTEM=="input", KERNEL=="event*", ATTRS{name}=="$KEYBOARD_NAME", SYMLINK+="input/bt-keyboard"
EOF
if [ "$GPIO_BL" = 1 ]; then
    gpio_bl_rule > "/etc/udev/rules.d/$GPIO_BL_RULE"
    echo "on/off GPIO backlight: /etc/udev/rules.d/$GPIO_BL_RULE"
fi
udevadm control --reload
udevadm trigger --subsystem-match=input --subsystem-match=misc --subsystem-match=backlight || true
usermod -aG input,video,netdev "$TARGET_USER"

say "Installing PolicyKit rules (Wi-Fi, time settings, reboot/shutdown from the launcher)"
mkdir -p /etc/polkit-1/rules.d
install -m 644 "$PAYLOAD/etc/50-networkmanager-netdev.rules" /etc/polkit-1/rules.d/50-networkmanager-netdev.rules
install -m 644 "$PAYLOAD/etc/51-launcher-time-power.rules" /etc/polkit-1/rules.d/51-launcher-time-power.rules

say "Installing services"
install -m 644 "$PAYLOAD/etc/launcher-ntp-default.service" /etc/systemd/system/launcher-ntp-default.service
systemctl daemon-reload
systemctl enable launcher-ntp-default.service
# the text console must not read the keyboard underneath the launcher
systemctl disable --now getty@tty1.service 2>/dev/null || true
systemctl mask getty@tty1.service 2>/dev/null || true

UNIT_DIR="$TARGET_HOME/.config/systemd/user"
mkdir -p "$UNIT_DIR"
cp "$PAYLOAD/etc/APPLaunch.service" "$UNIT_DIR/APPLaunch.service"
chown -R "$TARGET_USER:$TARGET_USER" "$TARGET_HOME/.config"
loginctl enable-linger "$TARGET_USER"

say "Board profile"
apply_profile

# The PWM backlight overlay is for the deck's Waveshare panel (the deck, or an unknown board with --force as
# before); a board with its own profile keeps its boot configuration and only gets the cmdline arguments.
DECK_BOOT=0
if [ "$BOARD" = zero2w ] || [ -z "$BOARD" ]; then DECK_BOOT=1; fi
if [ "$BOOT_CONFIG" = 1 ]; then
    say "Boot configuration ($( [ "$DECK_BOOT" = 1 ] && echo 'PWM backlight, ' )hidden console cursor)"
    CONFIG="$BOOT/config.txt"
    CMDLINE="$BOOT/cmdline.txt"
    [ "$DECK_BOOT" = 0 ] || [ -f "$CONFIG.bak-applaunch" ] || cp "$CONFIG" "$CONFIG.bak-applaunch"
    [ -f "$CMDLINE.bak-applaunch" ] || cp "$CMDLINE" "$CMDLINE.bak-applaunch"
    if [ "$DECK_BOOT" = 1 ]; then
        if ! grep -q '^dtoverlay=waveshare-28dpi' "$CONFIG"; then
            echo "WARNING: no dtoverlay=waveshare-28dpi* line in $CONFIG - add the panel overlays first (config.txt.snippet)"
        fi
        dtc -@ -I dts -O dtb -o "$BOOT/overlays/waveshare-pwm-backlight.dtbo" "$PAYLOAD/etc/waveshare-pwm-backlight.dts"
        if ! grep -q '^dtoverlay=waveshare-pwm-backlight' "$CONFIG"; then
            if grep -q '^dtoverlay=waveshare-28dpi' "$CONFIG"; then
                # after the last Waveshare overlay: the PWM backlight must replace the one it adds
                LAST=$(grep -n '^dtoverlay=waveshare-28dpi' "$CONFIG" | tail -1 | cut -d: -f1)
                sed -i "${LAST}a dtoverlay=pwm,pin=18,func=2\ndtoverlay=waveshare-pwm-backlight" "$CONFIG"
            else
                printf 'dtoverlay=pwm,pin=18,func=2\ndtoverlay=waveshare-pwm-backlight\n' >> "$CONFIG"
            fi
            REBOOT=1
        fi
    else
        echo "$BOARD: config.txt left as it is (the PWM backlight overlay is for the deck's Waveshare panel)"
    fi
    for arg in vt.global_cursor_default=0 consoleblank=0; do
        if ! grep -q "$arg" "$CMDLINE"; then
            sed -i "1s|\$| $arg|" "$CMDLINE"
            REBOOT=1
        fi
    done
fi

say "Starting the launcher"
as_user() { runuser -u "$TARGET_USER" -- env XDG_RUNTIME_DIR="/run/user/$TARGET_UID" "$@"; }
as_user systemctl --user daemon-reload
as_user systemctl --user enable APPLaunch.service
as_user systemctl --user restart APPLaunch.service || echo "(could not start it now; it starts at the next boot)"

say "Done"
echo "Next: pair the keyboard if you have not yet (bluetoothctl: scan on / pair / trust / connect)."
if [ "${REBOOT:-0}" = 1 ]; then
    echo "Boot configuration changed: reboot now so the $( [ "$DECK_BOOT" = 1 ] && echo 'PWM backlight and ' )console settings apply:  sudo reboot"
fi
