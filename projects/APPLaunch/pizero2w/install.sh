#!/bin/sh
# Install the launcher port on a Raspberry Pi Zero 2W with the Waveshare 2.8" DPI LCD (the deck), or on another
# known board (see README.md, "Boards"): the board is detected and its profile written to /etc/applaunch/board.conf.
# A board that boots to a desktop (a display manager such as lightdm is enabled or running) gets the desktop
# variant: the launcher is started from the applications menu and gives the desktop back when it exits (README.md,
# "Desktop boards"); the others run it at boot as the user service APPLaunch.service.
#
# Run on the Pi, from the unpacked bundle (see build.sh):   sudo ./install.sh [options]
# It is idempotent: running it again updates the files and leaves everything else as it is.
#
# Options:
#   --keyboard-name NAME   Bluetooth keyboard name as shown in `bluetoothctl devices` (default: "M4 Keyboard")
#   --user NAME            user that runs the launcher (default: the user who ran sudo)
#   --no-boot-config       do not touch /boot/firmware/config.txt and cmdline.txt (no PWM backlight)
#   --board NAME           use this board profile instead of detecting it (zero2w, pi3a-luckfox35,
#                          pi5-hyperpixel4, hackberry-cm5)
#   --board-only           only detect the board and write/update /etc/applaunch/board.conf, then stop
#   --dry-run              print the detected board, the session and what would be done, change nothing
#   --force                continue on an unknown board (no profile is written); replace a hand-edited
#                          /etc/applaunch/board.conf (the old one is kept as board.conf.bak)
#   --no-default-source    do not offer the apps repo (OSRdesign/cyberdeck-zero-apps) in Settings > Apps
#                          (default: added once for the user if they have no source yet, see default-sources.sh)
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
DEFAULT_SOURCE=1
for arg in "$@"; do
    case "$arg" in
        -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
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
        --board) [ $# -ge 2 ] || { echo "--board needs a name (zero2w, pi3a-luckfox35, pi5-hyperpixel4, hackberry-cm5)" >&2; exit 2; }; BOARD_ARG="$2"; shift 2 ;;
        --board-only) BOARD_ONLY=1; shift ;;
        --dry-run) DRY=1; shift ;;
        --force) FORCE=1; shift ;;
        --no-default-source) DEFAULT_SOURCE=0; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# SYSROOT is a test hook: board and desktop detection read $SYSROOT/proc, $SYSROOT/sys and $SYSROOT/etc, and the
# profile and the backlight udev rule are written under $SYSROOT/etc. Leave it unset on a real board.
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

# BOARD: zero2w | pi3a-luckfox35 | pi5-hyperpixel4 | hackberry-cm5 | "" (unknown).
# PANEL_FB: the panel's framebuffer of a board with a profile (not the deck).
BOARD=""
PANEL_FB=""
# The panel of each profiled board: fbdev name prefix, buffer size (virtual_size), DRM connector and its mode.
# The RP1 DPI fbdev ("drm-rp1-dpidrmf") is the panel's native buffer: portrait 480x800 for the HyperPixel 4 (the
# launcher pre-rotates its 800x480 canvas 90 degrees), square 720x720 for the HackBerry (report 021, addenda 3, 4, 7).
panel_spec() {
    case "$1" in
        pi3a-luckfox35) echo "panel-mipi-dbi 320,480 SPI-* 320x480" ;;
        pi5-hyperpixel4) echo "drm-rp1-dpi 480,800 DPI-* 480x800" ;;
        hackberry-cm5) echo "drm-rp1-dpi 720,720 DPI-* 720x720" ;;
    esac
}
# detect_panel BOARD: sets PANEL_FB; true when the framebuffer and the connected DRM connector are there
detect_panel() {
    spec=$(panel_spec "$1")
    set -f                                  # split the spec, but do not expand its connector pattern here
    # shellcheck disable=SC2086
    set -- $spec
    set +f
    PANEL_FB=$(find_fb "$1" "$2" || true)
    [ -n "$PANEL_FB" ] && drm_connected "$3" "$4"
}
if [ -n "$BOARD_ARG" ]; then
    case "$BOARD_ARG" in
        zero2w) BOARD=zero2w ;;
        pi3a-luckfox35|pi5-hyperpixel4|hackberry-cm5)
            BOARD=$BOARD_ARG
            detect_panel "$BOARD" || true
            if [ -z "$PANEL_FB" ]; then
                spec=$(panel_spec "$BOARD")
                fbname=${spec%% *}; fbsize=${spec#* }; fbsize=${fbsize%% *}
                echo "--board $BOARD: no ${fbsize%,*}x${fbsize#*,} \"$fbname\" framebuffer found (is the panel overlay loaded?)." >&2
                print_facts >&2
                echo "Nothing was changed." >&2
                exit 1
            fi ;;
        *) echo "unknown board \"$BOARD_ARG\" (known: zero2w, pi3a-luckfox35, pi5-hyperpixel4, hackberry-cm5)" >&2; exit 2 ;;
    esac
else
    case "$MODEL" in
        *"Zero 2"*) BOARD=zero2w ;;
        *"Raspberry Pi 3 Model A Plus"*) detect_panel pi3a-luckfox35 && BOARD=pi3a-luckfox35 ;;
        *"Raspberry Pi 5"*) detect_panel pi5-hyperpixel4 && BOARD=pi5-hyperpixel4 ;;
        *"Compute Module 5"*) detect_panel hackberry-cm5 && BOARD=hackberry-cm5 ;;
    esac
fi

# ---------------------------------------------------------------------------------------------------------
# Session: a board that boots to a desktop (a display manager is enabled, or a desktop process runs) gets the
# desktop variant; the others are appliance boards (the launcher starts at boot). Read only.
# DESKTOP_NAMES are the process names of tools/board-probe/probe-board.sh (comm is cut to 15 characters).
# ---------------------------------------------------------------------------------------------------------
DESKTOP_NAMES="labwc wayfire sway weston Xorg Xwayland lightdm sddm gdm gdm3"
DM_NAMES="lightdm sddm gdm gdm3"
match_procs() {
    awk -v names="$1" '
    BEGIN { n = split(names, a, " ") }
    { sub(/[ \t]+$/, ""); c = $0
      for (i = 1; i <= n; i++) if (c == a[i] || (length(c) == 15 && index(a[i], c) == 1)) hit[i] = 1 }
    END { out = ""; for (i = 1; i <= n; i++) if (hit[i]) out = out (out == "" ? "" : "+") a[i]; print out }'
}
# SESSION: appliance | desktop. DM_UNIT: the display manager's unit (desktop only). SESSION_WHY: the evidence.
SESSION=appliance
DM_UNIT=""
SESSION_WHY=""
DM_LINK="$SYSROOT/etc/systemd/system/display-manager.service"
DESK_PROCS=$(cat "$SYSROOT"/proc/[0-9]*/comm 2>/dev/null | match_procs "$DESKTOP_NAMES")
if [ -L "$DM_LINK" ]; then
    # the display manager enabled for boot (Debian: an alias symlink to its unit)
    DM_UNIT=$(readlink "$DM_LINK" 2>/dev/null || true)
    DM_UNIT=${DM_UNIT##*/}
    case "$DM_UNIT" in *?.service) SESSION_WHY="display manager $DM_UNIT enabled" ;; *) DM_UNIT="" ;; esac
fi
if [ -z "$DM_UNIT" ]; then
    for n in $DM_NAMES; do
        case "+$DESK_PROCS+" in *"+$n+"*) DM_UNIT="$n.service"; break ;; esac
    done
fi
if [ -n "$DM_UNIT" ] || [ -n "$DESK_PROCS" ] || [ -e "$DM_LINK" ]; then
    SESSION=desktop
    if [ -z "$DM_UNIT" ]; then DM_UNIT=lightdm.service; SESSION_WHY="no display manager unit found: $DM_UNIT assumed"; fi
    [ -z "$DESK_PROCS" ] || SESSION_WHY="${SESSION_WHY:+$SESSION_WHY; }running: $DESK_PROCS"
fi
# the deck's environment lives in APPLaunch.service (it has no profile): no desktop variant for it
DESKTOP_REFUSED=0
if [ "$SESSION" = desktop ] && [ "$BOARD" = zero2w ]; then DESKTOP_REFUSED=1; fi

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
        # The two desktop boards: the values board.py's test-start used on them (tools/boards/boards.json, test_env).
        # Touch is already in buffer orientation (report 021, addendum 8). No APPLAUNCH_PANEL_MM yet: the panels'
        # size in mm is still to be measured (task 014), the layout service uses its default density meanwhile.
        pi5-hyperpixel4) cat <<EOF
APPLAUNCH_BOARD=pi5-hyperpixel4
APPLAUNCH_FB=$PANEL_FB
APPLAUNCH_LOGICAL=800x480
APPLAUNCH_ROTATE=90
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
LV_LINUX_FBDEV_DEVICE=$PANEL_FB
EOF
        ;;
        hackberry-cm5) cat <<EOF
APPLAUNCH_BOARD=hackberry-cm5
APPLAUNCH_FB=$PANEL_FB
APPLAUNCH_LOGICAL=720x720
APPLAUNCH_ROTATE=0
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
LV_LINUX_FBDEV_DEVICE=$PANEL_FB
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

# ---------------------------------------------------------------------------------------------------------
# Desktop variant files: templates in the bundle (payload/etc/, or next to this script in the source tree) with
# @USER@ (the launcher's user) and @DM@ (the display manager's unit) filled in.
# ---------------------------------------------------------------------------------------------------------
HERE=$(cd "$(dirname "$0")" && pwd)
DESKTOP_UNIT=applaunch-desktop.service
DESKTOP_RULES=52-applaunch-desktop.rules
DESKTOP_ENTRY=/usr/share/applications/applaunch.desktop
bundle_file() {
    if [ -f "$HERE/payload/etc/$1" ]; then echo "$HERE/payload/etc/$1"; else echo "$HERE/$1"; fi
}
render() {
    sed -e "s|@USER@|$TARGET_USER|g" -e "s|@DM@|$DM_UNIT|g" "$(bundle_file "$1")"
}
# a user name goes into a unit file and a polkit rule: plain names only
check_user_name() {
    case "$1" in
        ""|-*|*[!A-Za-z0-9._-]*) echo "unusable user name \"$1\" for the desktop unit and its polkit rule" >&2; exit 1 ;;
    esac
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
    if [ "$SESSION" = desktop ]; then
        echo "session: desktop ($SESSION_WHY)"
        [ "$DESKTOP_REFUSED" = 0 ] || echo "The deck profile (zero2w) has no desktop variant: install.sh would stop here."
    else
        echo "session: appliance (no display manager: the launcher starts at boot)"
    fi
    echo "profile: $PROFILE_MSG"
    case "$PROFILE_ACTION" in write|replace) echo "--- $PROFILE"; profile_content ;; esac
    if [ "$GPIO_BL" = 1 ]; then echo "udev: install /etc/udev/rules.d/$GPIO_BL_RULE (video group may switch backlight_gpio)"; fi
    if [ "$SESSION" = desktop ]; then
        echo "boot config: none on a desktop board (config.txt and cmdline.txt left alone)"
    elif [ "$BOARD" = zero2w ] || [ -z "$BOARD" ]; then
        [ "$BOOT_CONFIG" = 1 ] && echo "boot config: PWM backlight overlay + cmdline cursor/consoleblank" || echo "boot config: skipped (--no-boot-config)"
    else
        [ "$BOOT_CONFIG" = 1 ] && echo "boot config: cmdline cursor/consoleblank only (the deck's PWM backlight overlay is not for this board)" || echo "boot config: skipped (--no-boot-config)"
    fi
    if [ "$BOARD_ONLY" = 0 ] && [ "$SESSION" = desktop ]; then
        TARGET_USER=${TARGET_USER:-${SUDO_USER:-$(id -un 2>/dev/null || echo USER)}}
        echo "then: packages, launcher files, udev and polkit rules, Network Time service, as for the deck; the desktop variant:"
        echo "  - no user service, no lingering, getty@tty1 left as it is, no boot configuration change"
        echo "  - /etc/systemd/system/$DESKTOP_UNIT for $TARGET_USER, not enabled (nothing starts at boot): starting it stops"
        echo "    $DM_UNIT; when the launcher exits the desktop comes back, except during a poweroff or reboot"
        echo "  - $DESKTOP_ENTRY: \"CardputerZero Launcher\" in the applications menu"
        echo "  - /etc/polkit-1/rules.d/$DESKTOP_RULES: $TARGET_USER may start and stop $DESKTOP_UNIT without a password"
        echo "  - the launcher is not started now (it would stop the desktop): start it from the menu"
        echo "--- /etc/systemd/system/$DESKTOP_UNIT"
        render "$DESKTOP_UNIT"
        echo "--- /etc/polkit-1/rules.d/$DESKTOP_RULES"
        render "$DESKTOP_RULES"
    elif [ "$BOARD_ONLY" = 0 ]; then
        echo "then: packages, launcher files, udev/polkit rules, services, user service (as for the deck)"
        echo "console: applaunch-console.service frees the panel from the text console once the boot is done (the boot lines still show)"
    fi
    if [ "$BOARD_ONLY" = 0 ]; then
        if [ "$DEFAULT_SOURCE" = 1 ]; then
            echo "default app source: OSRdesign/cyberdeck-zero-apps added for the user once, only if they have no source yet, then synced (default-sources.sh)"
        else
            echo "default app source: skipped (--no-default-source)"
        fi
    fi
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
if [ "$SESSION" = desktop ]; then
    echo "session: desktop ($SESSION_WHY): the launcher is started from the applications menu"
    if [ "$DESKTOP_REFUSED" = 1 ]; then
        echo "The deck profile (zero2w) has no desktop variant (its settings live in the user service). Nothing was changed." >&2
        exit 1
    fi
    check_user_name "$TARGET_USER"
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
# the text console helper of applaunch-console.service (appliance) and applaunch-desktop.service (desktop)
install -m 755 "$PAYLOAD/bin/applaunch-console" "$APP_ROOT/bin/applaunch-console"
chown -R "$TARGET_USER:$TARGET_USER" "$APP_ROOT/applications"
chmod -R a+rX "$APP_ROOT"

say "Installing udev rules (keyboard: \"$KEYBOARD_NAME\")"
install -m 644 "$PAYLOAD/etc/90-backlight-unblank.rules" /etc/udev/rules.d/90-backlight-unblank.rules
install -m 644 "$PAYLOAD/etc/91-applaunch-vkbd.rules" /etc/udev/rules.d/91-applaunch-vkbd.rules
install -m 644 "$PAYLOAD/etc/93-applaunch-rfkill.rules" /etc/udev/rules.d/93-applaunch-rfkill.rules
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
if [ "$SESSION" = desktop ]; then
    # the menu entry starts the launcher's unit as the user, without a password
    render "$DESKTOP_RULES" > "/etc/polkit-1/rules.d/$DESKTOP_RULES.tmp"
    chmod 644 "/etc/polkit-1/rules.d/$DESKTOP_RULES.tmp"
    mv "/etc/polkit-1/rules.d/$DESKTOP_RULES.tmp" "/etc/polkit-1/rules.d/$DESKTOP_RULES"
    echo "desktop: /etc/polkit-1/rules.d/$DESKTOP_RULES ($TARGET_USER may start and stop $DESKTOP_UNIT)"
fi

say "Installing services"
install -m 644 "$PAYLOAD/etc/launcher-ntp-default.service" /etc/systemd/system/launcher-ntp-default.service
UNIT_DIR="$TARGET_HOME/.config/systemd/user"
as_user() { runuser -u "$TARGET_USER" -- env XDG_RUNTIME_DIR="/run/user/$TARGET_UID" "$@"; }
if [ "$SESSION" = desktop ]; then
    # Desktop variant: no user service, no lingering, getty@tty1 as it is (the desktop owns its own VT). The system
    # unit runs the launcher as the user when started; it is never enabled (no [Install] section).
    render "$DESKTOP_UNIT" > "/etc/systemd/system/$DESKTOP_UNIT.tmp"
    chmod 644 "/etc/systemd/system/$DESKTOP_UNIT.tmp"
    mv "/etc/systemd/system/$DESKTOP_UNIT.tmp" "/etc/systemd/system/$DESKTOP_UNIT"
    systemctl daemon-reload
    systemctl enable launcher-ntp-default.service
    mkdir -p "${DESKTOP_ENTRY%/*}"
    install -m 644 "$PAYLOAD/etc/applaunch.desktop" "$DESKTOP_ENTRY"
    echo "desktop: /etc/systemd/system/$DESKTOP_UNIT (stops $DM_UNIT while the launcher runs) and $DESKTOP_ENTRY"
    # an earlier appliance install must not start a second launcher at boot under the desktop
    if [ -f "$UNIT_DIR/APPLaunch.service" ]; then
        as_user systemctl --user disable --now APPLaunch.service 2>/dev/null || true
        echo "the user service APPLaunch.service of an earlier install is disabled (the menu entry starts the launcher now)"
    fi
else
    systemctl daemon-reload
    systemctl enable launcher-ntp-default.service
    # the text console must not read the keyboard underneath the launcher
    systemctl disable --now getty@tty1.service 2>/dev/null || true
    systemctl mask getty@tty1.service 2>/dev/null || true
    # nor repaint over it: the panel leaves the text console once the boot is done (the boot lines still show)
    install -m 644 "$PAYLOAD/etc/applaunch-console.service" /etc/systemd/system/applaunch-console.service
    systemctl daemon-reload
    systemctl enable --now applaunch-console.service || echo "(applaunch-console.service did not start now; it runs at the next boot)"

    mkdir -p "$UNIT_DIR"
    cp "$PAYLOAD/etc/APPLaunch.service" "$UNIT_DIR/APPLaunch.service"
    chown -R "$TARGET_USER:$TARGET_USER" "$TARGET_HOME/.config"
    loginctl enable-linger "$TARGET_USER"
fi

say "Board profile"
apply_profile

# The PWM backlight overlay is for the deck's Waveshare panel (the deck, or an unknown board with --force as
# before); a board with its own profile keeps its boot configuration and only gets the cmdline arguments.
# A desktop board keeps its boot configuration as it is (its desktop owns the console settings).
DECK_BOOT=0
if [ "$BOARD" = zero2w ] || [ -z "$BOARD" ]; then DECK_BOOT=1; fi
if [ "$SESSION" = desktop ]; then
    say "Boot configuration"
    echo "desktop board: config.txt and cmdline.txt left as they are"
elif [ "$BOOT_CONFIG" = 1 ]; then
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

# Settings > Apps lists the apps of the user's sources: offer the project's apps repo once, as the user (their own
# file, no password), only if they have no source yet; a list of their own is never changed (default-sources.sh).
say "Default app source (Settings > Apps)"
if [ "$DEFAULT_SOURCE" = 0 ]; then
    echo "skipped (--no-default-source)"
elif [ ! -f "$HERE/default-sources.sh" ]; then
    echo "default-sources.sh is not next to install.sh: skipped (add OSRdesign/cyberdeck-zero-apps in Settings > Apps > Sources)"
else
    runuser -u "$TARGET_USER" -- env HOME="$TARGET_HOME" sh "$HERE/default-sources.sh" </dev/null \
        || echo "(the default app source could not be set up: add OSRdesign/cyberdeck-zero-apps in Settings > Apps > Sources)"
fi

if [ "$SESSION" = desktop ]; then
    say "Starting the launcher"
    # not now: starting it stops the desktop, maybe under the user's feet
    if systemctl is-active --quiet "$DESKTOP_UNIT"; then
        echo "the launcher is running: quit it (Settings > System > Quit to desktop) and start it again to use the new files"
    else
        echo "not started now (it would stop the desktop)"
    fi
else
    say "Starting the launcher"
    as_user systemctl --user daemon-reload
    as_user systemctl --user enable APPLaunch.service
    as_user systemctl --user restart APPLaunch.service || echo "(could not start it now; it starts at the next boot)"
fi

say "Done"
if [ "$SESSION" = desktop ]; then
    echo "Start the launcher from the desktop: applications menu > CardputerZero Launcher (or: systemctl start $DESKTOP_UNIT)."
    echo "The desktop stops while it runs; Settings > System > Quit to desktop brings it back."
fi
echo "Next: pair the keyboard if you have not yet (bluetoothctl: scan on / pair / trust / connect)."
if [ "${REBOOT:-0}" = 1 ]; then
    echo "Boot configuration changed: reboot now so the $( [ "$DECK_BOOT" = 1 ] && echo 'PWM backlight and ' )console settings apply:  sudo reboot"
fi
