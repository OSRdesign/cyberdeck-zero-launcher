#!/bin/sh
# Install CyberdeckZero Launcher on a Raspberry Pi Zero 2W with the Waveshare 2.8" DPI LCD (the deck), on another
# known board (see README.md, "Boards"), or on a PC (x86_64, the amd64 bundle: the "pc" profile, README.md, "PCs"):
# the board is detected and its profile written to /etc/applaunch/board.conf.
# A board that boots to a desktop (a display manager such as lightdm is enabled or running) gets the desktop
# variant: the launcher is started from the applications menu and gives the desktop back when it exits (README.md,
# "Desktop boards"); the others run it at boot as the user service cyberdeckzero.service.
#
# Run on the Pi, from the unpacked bundle (see build.sh):   sudo ./install.sh [options]
# It is idempotent: running it again updates the files and leaves everything else as it is. Over an earlier install
# (v0.6.0 and before: APPLaunch.service, applaunch-console.service, applaunch-desktop.service) it migrates to the
# new names first: the old units are stopped, disabled and removed before the new ones are enabled (--dry-run lists it).
#
# Options:
#   --keyboard-name NAME   Bluetooth keyboard name as shown in `bluetoothctl devices` (default: "M4 Keyboard")
#   --user NAME            user that runs the launcher (default: the user who ran sudo)
#   --no-boot-config       do not touch /boot/firmware/config.txt and cmdline.txt (no PWM backlight)
#   --board NAME           use this board profile instead of detecting it (zero2w, pi3a-luckfox35,
#                          pi3a-wsdsi43, pi5-hyperpixel4, hackberry-cm5)
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
#   - pairing the Bluetooth keyboard (bluetoothctl);
#   - on a PC: installing NetworkManager and BlueZ (Settings > Wi-Fi and Bluetooth need them; it says so when they
#     are missing). A PC's boot configuration (GRUB, kernel command line) is never changed.
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
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
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
        --board) [ $# -ge 2 ] || { echo "--board needs a name (zero2w, pi3a-luckfox35, pi3a-wsdsi43, pi5-hyperpixel4, hackberry-cm5)" >&2; exit 2; }; BOARD_ARG="$2"; shift 2 ;;
        --board-only) BOARD_ONLY=1; shift ;;
        --dry-run) DRY=1; shift ;;
        --force) FORCE=1; shift ;;
        --no-default-source) DEFAULT_SOURCE=0; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# SYSROOT is a test hook: board and desktop detection read $SYSROOT/proc, $SYSROOT/sys and $SYSROOT/etc, and the
# profile and the backlight udev rule are written under $SYSROOT/etc. Leave it unset on a real board.
# INSTALL_FAKE_SYSTEM=1 (test hook, with SYSROOT) allows a whole install into $SYSROOT: every system path and the
# user's home are taken under it, and the test puts stand-ins for systemctl, runuser, loginctl, udevadm, usermod,
# dpkg, chown and uname first on PATH (tests/test-install-board.sh). Never set it on a real board.
# INSTALL_GLIBC_VERSION is a test hook too: it replaces the glibc version read from the system; INSTALL_MACHINE
# replaces "uname -m" (aarch64, x86_64) for the board detection.
SYSROOT=${SYSROOT:-}
if [ -n "$SYSROOT" ] && [ "$DRY" = 0 ] && [ "$BOARD_ONLY" = 0 ] && [ "${INSTALL_FAKE_SYSTEM:-0}" != 1 ]; then
    echo "SYSROOT is a test hook: use it only with --dry-run or --board-only" >&2; exit 2
fi
PROFILE_DIR="$SYSROOT/etc/applaunch"
PROFILE="$PROFILE_DIR/board.conf"
GPIO_BL_RULE="92-applaunch-backlight-gpio.rules"

# ---------------------------------------------------------------------------------------------------------
# Names (report 025). Since v0.7.0 the launcher's binary, units, polkit rule and menu entry carry the product name;
# the paths (/usr/share/APPLaunch, /etc/applaunch), the APPLAUNCH_* variables, the other rules and the Store keep
# their technical names (NAMING.md). The names of v0.6.0 and before are known here for the migration.
# ---------------------------------------------------------------------------------------------------------
LAUNCHER_BIN=cyberdeckzero                       # /usr/share/APPLaunch/bin/cyberdeckzero
CONSOLE_HELPER=cyberdeckzero-console             # /usr/share/APPLaunch/bin/cyberdeckzero-console
USER_UNIT=cyberdeckzero.service                  # appliance boards: systemd user service
CONSOLE_UNIT=cyberdeckzero-console.service       # appliance boards: frees the panel from the text console
DESKTOP_UNIT=cyberdeckzero-desktop.service       # desktop boards: started from the menu entry
DESKTOP_RULES=52-cyberdeckzero-desktop.rules     # desktop boards: the user may start and stop DESKTOP_UNIT
DESKTOP_ENTRY_NAME=cyberdeckzero.desktop         # desktop boards: "CyberdeckZero Launcher" in the menu
DOC_NAME=cyberdeckzero-launcher                  # /usr/share/doc/cyberdeckzero-launcher: licences and notices
# branding-allow-begin: the binary name of v0.6.0 and before (kept as a link for one release, then removed)
OLD_LAUNCHER_BIN=M5CardputerZero-APPLaunch
# branding-allow-end
OLD_CONSOLE_HELPER=applaunch-console             # kept as a link to CONSOLE_HELPER for one release
OLD_USER_UNIT=APPLaunch.service
OLD_CONSOLE_UNIT=applaunch-console.service
OLD_DESKTOP_UNIT=applaunch-desktop.service
OLD_DESKTOP_RULES=52-applaunch-desktop.rules
OLD_DESKTOP_ENTRY_NAME=applaunch.desktop
# Files that earlier bundles put in /usr/share/APPLaunch and this one no longer ships (install.sh copies the bundle
# over the folder and never deletes by wildcard: installed apps live there too): the stock start-up logo, two unused
# images, the stock USB ADB gadget helpers.
RETIRED_FILES="share/images/logo_lcd.png share/images/launcher_brand_logo.png share/images/carousel_zero_wordmark.png
adb/cardputer-adb adb/cardputer-adb-gadget"
RETIRED_DIRS="adb"                               # removed when empty afterwards

# System paths ($SYSROOT is empty on a real board)
APP_ROOT="$SYSROOT/usr/share/APPLaunch"
SYSTEMD_DIR="$SYSROOT/etc/systemd/system"
POLKIT_DIR="$SYSROOT/etc/polkit-1/rules.d"
UDEV_DIR="$SYSROOT/etc/udev/rules.d"
MENU_DIR="$SYSROOT/usr/share/applications"
DESKTOP_ENTRY="$MENU_DIR/$DESKTOP_ENTRY_NAME"
DOC_DIR="$SYSROOT/usr/share/doc/$DOC_NAME"

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
# a PC: x86_64 and DMI (see "PC" below)
MACHINE=${INSTALL_MACHINE:-$(uname -m)}
DMI="$SYSROOT/sys/class/dmi/id"
DMI_VENDOR=$(rd "$DMI/sys_vendor" || true)
DMI_PRODUCT=$(rd "$DMI/product_name" || true)
DMI_CHASSIS=$(rd "$DMI/chassis_type" || true)

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
    if [ -n "$DMI_VENDOR$DMI_PRODUCT" ]; then
        echo "  dmi: ${DMI_VENDOR:-?} / ${DMI_PRODUCT:-?}, chassis type ${DMI_CHASSIS:-?}, $MACHINE"
    fi
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

# BOARD: zero2w | pi3a-luckfox35 | pi3a-wsdsi43 | pi5-hyperpixel4 | hackberry-cm5 | pc-<model> | "" (unknown).
# PANEL_FB: the panel's framebuffer of a board with a profile (not the deck).
# BOARD_NOTE: why a known model was left unknown (two panels at once), printed with the facts.
BOARD=""
PANEL_FB=""
BOARD_NOTE=""
KNOWN_BOARDS="zero2w, pi3a-luckfox35, pi3a-wsdsi43, pi5-hyperpixel4, hackberry-cm5"
# The panel of each profiled board: fbdev name prefix, buffer size (virtual_size), DRM connector and its mode.
# The RP1 DPI fbdev ("drm-rp1-dpidrmf") is the panel's native buffer: portrait 480x800 for the HyperPixel 4 (the
# launcher pre-rotates its 800x480 canvas 90 degrees), square 720x720 for the HackBerry (report 021, addenda 3, 4, 7).
# The Pi 3A+ takes one of two panels: the Luckfox 3.5" SPI panel (its own panel-mipi-dbi fbdev, portrait 320x480) or
# the Waveshare 4.3" DSI panel (the KMS fbdev "vc4drmfb", 800x480 landscape). The detection reads only the model, the
# DRM connector and the framebuffer, never config.txt: it does not depend on the overlay's name (the panel wants
# dtoverlay=vc4-kms-dsi-waveshare-800x480; the generic vc4-kms-dsi-7inch also drives it, but it flickers).
panel_spec() {
    case "$1" in
        pi3a-luckfox35) echo "panel-mipi-dbi 320,480 SPI-* 320x480" ;;
        pi3a-wsdsi43) echo "vc4drmfb 800,480 DSI-* 800x480" ;;
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
        pi3a-luckfox35|pi3a-wsdsi43|pi5-hyperpixel4|hackberry-cm5)
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
        *) echo "unknown board \"$BOARD_ARG\" (known: $KNOWN_BOARDS)" >&2; exit 2 ;;
    esac
else
    case "$MODEL" in
        *"Zero 2"*) BOARD=zero2w ;;
        *"Raspberry Pi 3 Model A Plus"*)
            # one panel or the other; both connected at once is not guessed
            SPI_FB=""; DSI_FB=""
            if detect_panel pi3a-luckfox35; then SPI_FB=$PANEL_FB; fi
            if detect_panel pi3a-wsdsi43; then DSI_FB=$PANEL_FB; fi
            if [ -n "$SPI_FB" ] && [ -n "$DSI_FB" ]; then
                PANEL_FB=""
                BOARD_NOTE="Pi 3A+ with two panels connected (SPI $SPI_FB: pi3a-luckfox35, DSI $DSI_FB: pi3a-wsdsi43): choose one with --board."
            elif [ -n "$SPI_FB" ]; then BOARD=pi3a-luckfox35; PANEL_FB=$SPI_FB
            elif [ -n "$DSI_FB" ]; then BOARD=pi3a-wsdsi43; PANEL_FB=$DSI_FB
            fi ;;
        *"Raspberry Pi 5"*) detect_panel pi5-hyperpixel4 && BOARD=pi5-hyperpixel4 ;;
        *"Compute Module 5"*) detect_panel hackberry-cm5 && BOARD=hackberry-cm5 ;;
    esac
fi

# ---------------------------------------------------------------------------------------------------------
# PC (x86_64 without a device tree, the amd64 bundle; report 028): the model from DMI, the panel the DRM driver's
# framebuffer (i915drmfb on the FZ-M1), its size in mm from the connected connector's EDID, else a table of known
# models, else nothing (the launcher's default density: large controls). Landscape, no rotation, touch by capability.
# ---------------------------------------------------------------------------------------------------------
pc_slug() { printf '%s' "$1" | tr 'A-Z' 'a-z' | sed -e 's/[^a-z0-9][^a-z0-9]*/-/g' -e 's/^-//' -e 's/-$//' | cut -c1-32; }
# the panel's framebuffer: the DRM driver's fbdev (name ending in "drmfb"), else the first one
pc_fb() {
    first=""
    for d in "$SYSROOT"/sys/class/graphics/fb[0-9]*; do
        [ -d "$d" ] || continue
        [ -n "$first" ] || first="/dev/${d##*/}"
        case "$(rd "$d/name" || true)" in *drmfb) echo "/dev/${d##*/}"; return 0 ;; esac
    done
    [ -z "$first" ] || echo "$first"
}
# the connected DRM connector of the panel: an internal one first (eDP, LVDS, DSI), else any connected one
pc_connector() {
    for kind in eDP LVDS DSI ""; do
        for d in "$SYSROOT"/sys/class/drm/card[0-9]*-"$kind"*; do
            [ -d "$d" ] && [ "$(rd "$d/status" || true)" = connected ] && { echo "$d"; return 0; }
        done
    done
    return 0
}
# WxH in mm from an EDID blob: the first detailed timing's image size (mm), else the basic block's (cm); nothing when
# the blob is no EDID or gives no plausible size
edid_mm() {
    [ -r "$1" ] || return 0
    # shellcheck disable=SC2046
    set -- $(od -An -tu1 -v -N128 "$1" 2>/dev/null)
    [ $# -eq 128 ] && [ "$1 $2 $3 $4 $5 $6 $7 $8" = "0 255 255 255 255 255 255 0" ] || return 0
    w=0; h=0
    if [ "${55}" != 0 ] || [ "${56}" != 0 ]; then      # bytes 54-55: pixel clock of a detailed timing descriptor
        w=$(( ${67} + ${69} / 16 * 256 )); h=$(( ${68} + ${69} % 16 * 256 ))    # bytes 66-68
    fi
    if [ "$w" -lt 20 ] || [ "$h" -lt 20 ]; then w=$(( ${22} * 10 )); h=$(( ${23} * 10 )); fi   # bytes 21-22, cm
    if [ "$w" -ge 20 ] && [ "$h" -ge 20 ] && [ "$w" -le 2000 ] && [ "$h" -le 2000 ]; then echo "${w}x${h}"; fi
}
# Known PCs: panel size in mm (measured on the board) when the EDID has none; tablets whose built-in keyboard device
# is always there (its keys stay usable, it only does not count as "a keyboard is connected")
pc_known_mm() {
    case "$DMI_PRODUCT" in
        FZM1-2*|FZ-M1*) echo 150x93 ;;      # Panasonic FZ-M1 (mk2): 7" 1280x800 (report 028, x-a, from the touch panel)
    esac
}
pc_is_tablet() {
    case "$DMI_CHASSIS" in 30|32) return 0 ;; esac        # SMBIOS chassis: 30 tablet, 32 detachable
    case "$DMI_PRODUCT" in FZM1-2*|FZ-M1*) return 0 ;; esac
    return 1
}
IS_PC=0
PC_CONN=""; PC_MM=""; PC_MM_FROM=""; PC_LOGICAL=""
if [ -z "$MODEL" ] && [ "$MACHINE" = x86_64 ]; then
    IS_PC=1
    if [ -z "$BOARD_ARG" ]; then
        PANEL_FB=$(pc_fb || true)
        PC_CONN=$(pc_connector || true)
        if [ -n "$PC_CONN" ]; then PC_MM=$(edid_mm "$PC_CONN/edid" || true); fi
        conn=${PC_CONN##*/}
        if [ -n "$PC_MM" ]; then PC_MM_FROM="the EDID of ${conn#card*-}"
        else PC_MM=$(pc_known_mm); [ -z "$PC_MM" ] || PC_MM_FROM="the known size of the $DMI_PRODUCT"; fi
        if [ -n "$PANEL_FB" ]; then
            s=$(rd "$SYSROOT/sys/class/graphics/${PANEL_FB#/dev/}/virtual_size" || true)
            case "$s" in [0-9]*,[0-9]*) PC_LOGICAL="${s%,*}x${s#*,}" ;; esac
            slug=$(pc_slug "$DMI_PRODUCT")
            BOARD="pc-${slug:-generic}"
        else
            BOARD_NOTE="PC (x86_64) without a framebuffer: the launcher draws on /dev/fb0, the DRM driver's fbdev emulation."
        fi
    fi
fi
# The deck's PWM backlight overlay goes into config.txt on the deck and, as before, on an unknown board installed with
# --force, except when the model is one of the other boards' (Pi 3A+, Pi 5, CM5) and only its panel was not
# recognised: those panels bring their own backlight (and overlay), so config.txt stays as it is there.
DECK_BOOT=0
if [ "$BOARD" = zero2w ]; then
    DECK_BOOT=1
elif [ -z "$BOARD" ] && [ "$IS_PC" = 0 ]; then
    case "$MODEL" in
        *"Raspberry Pi 3 Model A Plus"*|*"Raspberry Pi 5"*|*"Compute Module 5"*) ;;
        *) DECK_BOOT=1 ;;
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
# the deck's environment lives in its user service (it has no profile): no desktop variant for it
DESKTOP_REFUSED=0
if [ "$SESSION" = desktop ] && [ "$BOARD" = zero2w ]; then DESKTOP_REFUSED=1; fi

# The profile for the board ("" = no profile file: the deck keeps the service's own environment).
# It is a systemd EnvironmentFile: its values override the Environment= lines of cyberdeckzero.service,
# so the deck's legacy touch axes and fb0 are neutralised here instead of being removed from the service.
# The body below is checksummed into the file (profile_state): changing its text, comments included, rewrites the
# profile of every board at the next install (the old one kept as board.conf.bak). Its comments still name the user
# service of v0.6.0 for that reason.
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
        # Pi 3A+ with the Waveshare 4.3" DSI panel: the values of the hand-written profile that works on the board
        # (2026-10-13). The KMS framebuffer is already landscape 800x480: no rotation; touch (ft5x06) by capability,
        # in buffer orientation. The backlight is the overlay's i2c regulator (sysfs brightness 0..255).
        pi3a-wsdsi43)
            bl=$(dsi_backlight)
            cat <<EOF
APPLAUNCH_BOARD=pi3a-wsdsi43
APPLAUNCH_FB=$PANEL_FB
APPLAUNCH_LOGICAL=800x480
APPLAUNCH_ROTATE=0
APPLAUNCH_COMPAT_SCALE=2
# landscape visible area in mm (800x480 at 0.117 mm pitch; to be confirmed with a ruler): the layout's density
APPLAUNCH_PANEL_MM=94x56
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
EOF
            if [ -n "$bl" ]; then echo "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/$bl"; fi
            cat <<EOF
LV_LINUX_FBDEV_DEVICE=$PANEL_FB
# the deck's legacy touch axes (set in its user service) must not apply here: neutral values
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
        # A PC (amd64): the DRM driver's framebuffer at its own size, landscape (no rotation), the dpi-scaled backend
        # (also set by the units: a launcher started by hand with this file gets it too), touch by capability in buffer
        # orientation, the first backlight with levels (intel_backlight first).
        pc-*)
            bl=$(pc_backlight)
            echo "APPLAUNCH_BOARD=$BOARD"
            echo "APPLAUNCH_DISPLAY=dpi-scaled"
            echo "APPLAUNCH_FB=$PANEL_FB"
            if [ -n "$PC_LOGICAL" ]; then echo "APPLAUNCH_LOGICAL=$PC_LOGICAL"; fi
            echo "APPLAUNCH_ROTATE=0"
            if [ -n "$PC_MM" ]; then
                echo "# panel size in mm, from $PC_MM_FROM: the layout's density"
                echo "APPLAUNCH_PANEL_MM=$PC_MM"
            else
                echo "# panel size unknown (no EDID size, not a known model): the launcher's default density (large controls)"
            fi
            cat <<EOF
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
EOF
            if [ -n "$bl" ]; then echo "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/$bl"; fi
            if pc_is_tablet; then
                echo "# the tablet's built-in keyboard device (i8042, bus 0011) is always there: it does not count as a"
                echo "# connected keyboard (the on-screen keyboard shows without a USB or Bluetooth one); its keys still work"
                echo "APPLAUNCH_KBD_IGNORE=bus:0011"
            fi
            cat <<EOF
# the Store tile starts hidden (the Store's hub has arm64 packages only); Settings > Launcher can show it
APPLAUNCH_HIDE_TILES=app_Store
LV_LINUX_FBDEV_DEVICE=$PANEL_FB
# the deck's legacy touch axes (set in its user service) must not apply here: neutral values
APPLAUNCH_TOUCH_SWAP_XY=0
APPLAUNCH_TOUCH_INVERT_X=0
APPLAUNCH_TOUCH_INVERT_Y=0
EOF
        ;;
    esac
}
# The PC's backlight with levels: intel_backlight, else the first one there; none: nothing
pc_backlight() {
    if [ -d "$SYSROOT/sys/class/backlight/intel_backlight" ]; then echo intel_backlight; return 0; fi
    for d in "$SYSROOT"/sys/class/backlight/*; do
        [ -e "$d" ] && { echo "${d##*/}"; return 0; }
    done
    return 0
}
# The DSI panel's backlight device: the DSI overlay's regulator at i2c 10-0045 (seen on the Pi 3A+ with both
# vc4-kms-dsi-waveshare-800x480 and vc4-kms-dsi-7inch), else
# the only backlight device there is; none or several: nothing (no APPLAUNCH_BACKLIGHT line, the launcher's default)
dsi_backlight() {
    if [ -d "$SYSROOT/sys/class/backlight/10-0045" ]; then echo 10-0045; return 0; fi
    n=0; one=""
    for d in "$SYSROOT"/sys/class/backlight/*; do
        [ -e "$d" ] || continue
        n=$((n + 1)); one=${d##*/}
    done
    if [ "$n" = 1 ]; then echo "$one"; fi
}
body_sum() { cksum | cut -d' ' -f1-2; }
# The first lines say who wrote the file and a checksum of the rest, so a hand edit can be told apart.
profile_content() {
    body=$(profile_body)
    printf '# Board profile for the launcher, written by install.sh (board: %s).\n' "$BOARD"
    printf '# Loaded by the launcher unit (EnvironmentFile). Edit freely: install.sh then leaves it alone unless --force.\n'
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
        # generated, unedited, same values: an earlier install's wording of the header lines is no reason to rewrite it
        elif [ "$st" = ours ] && [ "$(sed '1,3d' "$PROFILE")" = "$(profile_body)" ]; then
            PROFILE_ACTION=keep; PROFILE_MSG="$BOARD: $PROFILE already up to date"
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
# The levels backlight of the profile (APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/<name>: the Pi 3A+ DSI
# regulator, the PC's intel_backlight): its brightness file is root's only, the launcher runs as the user.
SYSFS_BL_RULE="92-applaunch-backlight-sysfs.rules"
SYSFS_BL=$(profile_body | sed -n 's|^APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/\([A-Za-z0-9._:-]*\)$|\1|p')
sysfs_bl_rule() {
    cat <<EOF
# Backlight with brightness levels named in the board profile (APPLAUNCH_BACKLIGHT=sysfs:...): let the video group
# set it. "change" too, so that "udevadm trigger" at install time applies it without a reboot.
SUBSYSTEM=="backlight", KERNEL=="$SYSFS_BL", ACTION=="add|change", RUN+="/bin/sh -c 'chgrp video /sys/class/backlight/%k/brightness; chmod g+w /sys/class/backlight/%k/brightness'"
EOF
}

# ---------------------------------------------------------------------------------------------------------
# Desktop variant files: templates in the bundle (payload/etc/, or next to this script in the source tree) with
# @USER@ (the launcher's user) and @DM@ (the display manager's unit) filled in.
# ---------------------------------------------------------------------------------------------------------
HERE=$(cd "$(dirname "$0")" && pwd)
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

# ---------------------------------------------------------------------------------------------------------
# Migration from the names of v0.6.0 and before (see "Names" above). What is there is looked up under $SYSROOT.
# ---------------------------------------------------------------------------------------------------------
present() { [ -e "$1" ] || [ -L "$1" ]; }
# the earlier user service of the user whose home is $1 ($SYSROOT included): its unit file or its enablement link
old_user_unit_present() {
    present "$1/.config/systemd/user/$OLD_USER_UNIT" || present "$1/.config/systemd/user/default.target.wants/$OLD_USER_UNIT"
}
# the retired files still in /usr/share/APPLaunch, one relative path per line
retired_present() {
    for f in $RETIRED_FILES; do
        if present "$APP_ROOT/$f"; then echo "$f"; fi
    done
}
# The dry run's account of the migration: every item, found or not, and what the install does with it.
# $1: the user, $2: the user's home ($SYSROOT included).
print_migration() {
    echo "migration from the names of v0.6.0 and before (done before the new units are enabled, so two launchers never"
    echo "share the screen; /etc/applaunch/board.conf, the user's settings, app sources and installed apps are kept):"
    if old_user_unit_present "$2"; then
        echo "  - user service $OLD_USER_UNIT of $1: found: stopped, disabled and removed (systemctl --user as $1)"
    else
        echo "  - user service $OLD_USER_UNIT of $1: not found"
    fi
    if present "$SYSTEMD_DIR/$OLD_CONSOLE_UNIT"; then
        echo "  - $OLD_CONSOLE_UNIT: found: disabled and stopped (the text console is bound again until $CONSOLE_UNIT"
        echo "    frees the panel), removed"
    else
        echo "  - $OLD_CONSOLE_UNIT: not found"
    fi
    if present "$SYSTEMD_DIR/$OLD_DESKTOP_UNIT"; then
        echo "  - $OLD_DESKTOP_UNIT: found: stopped if it runs (the earlier launcher quits, the desktop comes back), removed"
    else
        echo "  - $OLD_DESKTOP_UNIT: not found"
    fi
    for f in "$POLKIT_DIR/$OLD_DESKTOP_RULES" "$MENU_DIR/$OLD_DESKTOP_ENTRY_NAME"; do
        if present "$f"; then echo "  - ${f#"$SYSROOT"}: found: removed"; else echo "  - ${f#"$SYSROOT"}: not found"; fi
    done
    for pair in "$OLD_LAUNCHER_BIN:$LAUNCHER_BIN" "$OLD_CONSOLE_HELPER:$CONSOLE_HELPER"; do
        old=${pair%%:*}; new=${pair#*:}
        if [ -L "$APP_ROOT/bin/$old" ]; then state="already a link to $new"
        elif present "$APP_ROOT/bin/$old"; then state="found: replaced by a link to $new"
        else state="not found: created as a link to $new"; fi
        echo "  - ${APP_ROOT#"$SYSROOT"}/bin/$old: $state (the earlier name keeps working for one release)"
    done
    gone=$(retired_present)
    if [ -n "$gone" ]; then
        echo "  - files of earlier bundles that this one does not ship: found: removed:"
        printf '%s\n' "$gone" | sed "s|^|      ${APP_ROOT#"$SYSROOT"}/|"
    else
        echo "  - files of earlier bundles that this one does not ship (logo_lcd.png, two unused images, adb/): none found"
    fi
}

plan_profile
if [ "$DRY" = 1 ]; then
    echo "DRY RUN: nothing will be changed."
    echo "glibc: $GLIBC (ok)"
    echo "detected:"
    print_facts
    echo "board: ${BOARD:-unknown}${BOARD_ARG:+ (forced with --board)}"
    if [ -z "$BOARD" ]; then
        if [ -n "$BOARD_NOTE" ]; then echo "$BOARD_NOTE"; else echo "This board is not known to install.sh. Run tools/board-probe/probe-board.sh on it and send the report."; fi
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
    if [ -n "$SYSFS_BL" ]; then echo "udev: install /etc/udev/rules.d/$SYSFS_BL_RULE (video group may set the brightness of $SYSFS_BL)"; fi
    if [ "$IS_PC" = 1 ]; then
        echo "boot config: none on a PC (GRUB and the kernel command line are left alone)"
    elif [ "$SESSION" = desktop ]; then
        echo "boot config: none on a desktop board (config.txt and cmdline.txt left alone)"
    elif [ "$DECK_BOOT" = 1 ]; then
        [ "$BOOT_CONFIG" = 1 ] && echo "boot config: PWM backlight overlay + cmdline cursor/consoleblank" || echo "boot config: skipped (--no-boot-config)"
    else
        [ "$BOOT_CONFIG" = 1 ] && echo "boot config: cmdline cursor/consoleblank only (the deck's PWM backlight overlay is not for this board)" || echo "boot config: skipped (--no-boot-config)"
    fi
    if [ "$BOARD_ONLY" = 0 ]; then
        TARGET_USER=${TARGET_USER:-${SUDO_USER:-$(id -un 2>/dev/null || echo USER)}}
        DRY_HOME=$(getent passwd "$TARGET_USER" 2>/dev/null | cut -d: -f6 || true)
        print_migration "$TARGET_USER" "$SYSROOT${DRY_HOME:-/home/$TARGET_USER}"
        echo "launcher files: /usr/share/APPLaunch/bin/$LAUNCHER_BIN and $CONSOLE_HELPER, the libraries and assets of the"
        echo "  bundle; licences and notices in /usr/share/doc/$DOC_NAME (replaced as a whole)"
    fi
    if [ "$BOARD_ONLY" = 0 ] && [ "$SESSION" = desktop ]; then
        echo "then: packages, launcher files, udev and polkit rules, Network Time service, as for the deck; the desktop variant:"
        echo "  - no user service, no lingering, getty@tty1 left as it is, no boot configuration change"
        echo "  - /etc/systemd/system/$DESKTOP_UNIT for $TARGET_USER, not enabled (nothing starts at boot): starting it stops"
        echo "    $DM_UNIT; when the launcher exits the desktop comes back, except during a poweroff or reboot"
        echo "  - /usr/share/applications/$DESKTOP_ENTRY_NAME: \"CyberdeckZero Launcher\" in the applications menu"
        echo "  - /etc/polkit-1/rules.d/$DESKTOP_RULES: $TARGET_USER may start and stop $DESKTOP_UNIT without a password"
        echo "  - the launcher is not started now (it would stop the desktop): start it from the menu"
        echo "--- /etc/systemd/system/$DESKTOP_UNIT"
        render "$DESKTOP_UNIT"
        echo "--- /etc/polkit-1/rules.d/$DESKTOP_RULES"
        render "$DESKTOP_RULES"
    elif [ "$BOARD_ONLY" = 0 ]; then
        echo "then: packages, launcher files, udev/polkit rules, services, user service (as for the deck): $USER_UNIT"
        echo "  for $TARGET_USER (lingering on), enabled and started"
        echo "console: $CONSOLE_UNIT frees the panel from the text console once the boot is done (the boot lines still show)"
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
        if [ -n "$BOARD_NOTE" ]; then echo "$BOARD_NOTE"; else echo "This board is not known to install.sh. Run tools/board-probe/probe-board.sh on it and send the report."; fi
    fi
    apply_profile
    if [ "$GPIO_BL" = 1 ]; then
        mkdir -p "$UDEV_DIR"
        gpio_bl_rule > "$UDEV_DIR/$GPIO_BL_RULE"
        echo "installed $UDEV_DIR/$GPIO_BL_RULE"
        [ -n "$SYSROOT" ] || { udevadm control --reload; udevadm trigger --subsystem-match=backlight || true; }
    fi
    if [ -n "$SYSFS_BL" ]; then
        mkdir -p "$UDEV_DIR"
        sysfs_bl_rule > "$UDEV_DIR/$SYSFS_BL_RULE"
        echo "installed $UDEV_DIR/$SYSFS_BL_RULE"
        [ -n "$SYSROOT" ] || { udevadm control --reload; udevadm trigger --subsystem-match=backlight || true; }
    fi
    exit 0
fi

PAYLOAD="$HERE/payload"
# arm64 or amd64 from the e_machine field of the launcher's ELF header (183 = aarch64, 62 = x86-64); "" if unreadable
bundle_arch() {
    f="$PAYLOAD/bin/$LAUNCHER_BIN"
    [ "$(od -An -tx1 -N4 "$f" 2>/dev/null | tr -d ' \n')" = 7f454c46 ] || return 0
    case "$(od -An -tu1 -j18 -N2 "$f" | tr -s ' \n' ' ')" in
        " 183 0 ") echo arm64 ;;
        " 62 0 ") echo amd64 ;;
    esac
}
BOOT="$SYSROOT/boot/firmware"
[ -d "$BOOT" ] || BOOT="$SYSROOT/boot"

[ -d "$PAYLOAD" ] || { echo "payload/ not found next to install.sh (unpack the whole bundle)" >&2; exit 1; }
TARGET_USER=${TARGET_USER:-${SUDO_USER:-}}
[ -n "$TARGET_USER" ] && id "$TARGET_USER" >/dev/null 2>&1 || { echo "cannot determine the user: pass --user NAME" >&2; exit 1; }
TARGET_UID=$(id -u "$TARGET_USER")
TARGET_HOME=$(getent passwd "$TARGET_USER" | cut -d: -f6)
[ -n "$TARGET_HOME" ] || { echo "cannot find the home directory of $TARGET_USER" >&2; exit 1; }
TARGET_HOME="$SYSROOT$TARGET_HOME"
UNIT_DIR="$TARGET_HOME/.config/systemd/user"
# a command in the user's systemd (the user service lives on the user bus, reachable through the runtime dir)
as_user() { runuser -u "$TARGET_USER" -- env XDG_RUNTIME_DIR="/run/user/$TARGET_UID" "$@"; }

say() { printf '\n== %s\n' "$*"; }

# The migration itself (see print_migration): before the new units are installed and enabled, so the earlier
# launcher is stopped first and two launchers never share the screen. Idempotent: nothing to do the second time.
OLD_STILL_ACTIVE=0
migrate_old() {
    found=0
    if old_user_unit_present "$TARGET_HOME"; then
        found=1
        # through the user's systemd (lingering keeps it running at boot); without a reachable user manager the
        # service cannot be running, and its files go anyway
        as_user systemctl --user disable --now "$OLD_USER_UNIT" \
            || echo "(the user's systemd did not answer: $OLD_USER_UNIT is not running; its files are removed)"
        rm -f "$UNIT_DIR/$OLD_USER_UNIT" "$UNIT_DIR/default.target.wants/$OLD_USER_UNIT"
        as_user systemctl --user daemon-reload 2>/dev/null || true
        as_user systemctl --user reset-failed "$OLD_USER_UNIT" 2>/dev/null || true
        echo "user service $OLD_USER_UNIT of $TARGET_USER: stopped, disabled and removed"
        if as_user systemctl --user is-active --quiet "$OLD_USER_UNIT" 2>/dev/null; then
            OLD_STILL_ACTIVE=1
            echo "WARNING: $OLD_USER_UNIT is still running: the new launcher is not started now (reboot when done)" >&2
        fi
    fi
    if present "$SYSTEMD_DIR/$OLD_DESKTOP_UNIT"; then
        found=1
        if systemctl is-active --quiet "$OLD_DESKTOP_UNIT"; then
            # its ExecStopPost= gives the desktop back (its helper's earlier name is a link to the new helper)
            systemctl stop "$OLD_DESKTOP_UNIT" || true
            echo "the earlier launcher ($OLD_DESKTOP_UNIT) was running: stopped, the desktop comes back"
        fi
        rm -f "$SYSTEMD_DIR/$OLD_DESKTOP_UNIT"
        echo "$OLD_DESKTOP_UNIT removed"
    fi
    if present "$SYSTEMD_DIR/$OLD_CONSOLE_UNIT"; then
        found=1
        # its ExecStop= binds the text console again; $CONSOLE_UNIT frees the panel again right after
        systemctl disable --now "$OLD_CONSOLE_UNIT" || true
        rm -f "$SYSTEMD_DIR/$OLD_CONSOLE_UNIT" "$SYSTEMD_DIR/multi-user.target.wants/$OLD_CONSOLE_UNIT"
        echo "$OLD_CONSOLE_UNIT disabled and removed"
    fi
    for f in "$POLKIT_DIR/$OLD_DESKTOP_RULES" "$MENU_DIR/$OLD_DESKTOP_ENTRY_NAME"; do
        if present "$f"; then found=1; rm -f "$f"; echo "removed ${f#"$SYSROOT"}"; fi
    done
    if [ "$found" = 1 ]; then
        systemctl daemon-reload
        systemctl reset-failed "$OLD_DESKTOP_UNIT" "$OLD_CONSOLE_UNIT" 2>/dev/null || true
    else
        echo "nothing to migrate (no unit, rule or menu entry of the earlier names)"
    fi
}
# Files earlier bundles installed and this one does not ship (RETIRED_FILES), then their folders when empty
remove_retired() {
    for f in $(retired_present); do
        rm -f "$APP_ROOT/$f"
        echo "removed ${APP_ROOT#"$SYSROOT"}/$f (no longer in the bundle)"
    done
    for d in $RETIRED_DIRS; do
        if [ -d "$APP_ROOT/$d" ] && [ -z "$(ls -A "$APP_ROOT/$d" 2>/dev/null)" ]; then rmdir "$APP_ROOT/$d"; fi
    done
}

say "Checking the board"
echo "board: ${MODEL:-unknown} -> profile ${BOARD:-none (unknown board)}   user: $TARGET_USER   glibc: $GLIBC"
if [ -z "$BOARD" ]; then
    echo "detected:"; print_facts
    if [ -n "$BOARD_NOTE" ]; then echo "$BOARD_NOTE"; else echo "This board is not known to install.sh. Run tools/board-probe/probe-board.sh on it and send the report."; fi
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
# The bundle's architecture (its launcher's ELF header) must be the system's: aarch64 = arm64, x86_64 = amd64
case "$MACHINE" in
    aarch64) SYS_ARCH=arm64 ;;
    x86_64) SYS_ARCH=amd64 ;;
    *) echo "a 64-bit OS is required: aarch64 (Raspberry Pi) or x86_64 (PC), this one is $MACHINE" >&2; exit 1 ;;
esac
BUNDLE_ARCH=$(bundle_arch)
if [ -n "$BUNDLE_ARCH" ] && [ "$BUNDLE_ARCH" != "$SYS_ARCH" ]; then
    echo "this bundle is for $BUNDLE_ARCH, this system is $SYS_ARCH ($MACHINE): use cyberdeckzero-launcher-<version>-$SYS_ARCH.tar.gz. Nothing was changed." >&2
    exit 1
fi
# The migration stops the earlier launcher's unit: run from a terminal inside it (the launcher's own terminal app),
# that stop would end this script half way, the old unit gone and the new one not yet there.
if [ -z "$SYSROOT" ] && grep -q -e "$OLD_USER_UNIT" -e "$OLD_DESKTOP_UNIT" /proc/self/cgroup 2>/dev/null; then
    echo "this shell runs inside the earlier launcher's service ($OLD_USER_UNIT or $OLD_DESKTOP_UNIT): the migration" >&2
    echo "stops it, which would end the install half way. Run it from an ssh session or a text console. Nothing was changed." >&2
    exit 1
fi

say "Installing packages"
PKGS="libinput10 libxkbcommon0 libfreetype6 curl"
[ "$IS_PC" = 1 ] || PKGS="$PKGS device-tree-compiler"   # the deck's backlight overlay; a PC needs no dtc
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
# Settings > Wi-Fi and Bluetooth drive NetworkManager and BlueZ: not installed here (moving a PC's network from netplan
# or networkd to NetworkManager can cut an ssh session: do it on the console)
NOTE_PKGS=""
command -v nmcli >/dev/null 2>&1 || NOTE_PKGS="network-manager"
command -v bluetoothctl >/dev/null 2>&1 || NOTE_PKGS="${NOTE_PKGS:+$NOTE_PKGS }bluez"
case "$NOTE_PKGS" in
    "network-manager bluez") NOTE_WHAT="Settings > Wi-Fi and Bluetooth" ;;
    network-manager) NOTE_WHAT="Settings > Wi-Fi" ;;
    *) NOTE_WHAT="Settings > Bluetooth" ;;
esac
if [ -n "$NOTE_PKGS" ]; then
    echo "NOTICE: $NOTE_PKGS not installed: $NOTE_WHAT will not work. Install it yourself (sudo apt install"
    echo "        $NOTE_PKGS), from the console when NetworkManager is to take the network over."
fi

say "Installing the launcher into ${APP_ROOT#"$SYSROOT"}"
mkdir -p "$APP_ROOT/bin" "$APP_ROOT/lib" "$APP_ROOT/applications"
cp -a "$PAYLOAD/share/." "$APP_ROOT/"                       # images, fonts, audio, ... (does not delete installed apps)
install -m 755 "$PAYLOAD/bin/$LAUNCHER_BIN" "$APP_ROOT/bin/$LAUNCHER_BIN"
if [ -f "$PAYLOAD/bin/M5CardputerZero-AppStore" ]; then
    install -m 755 "$PAYLOAD/bin/M5CardputerZero-AppStore" "$APP_ROOT/bin/M5CardputerZero-AppStore"
fi
install -m 644 "$PAYLOAD/lib/libapplaunch_vfb.so" "$APP_ROOT/lib/libapplaunch_vfb.so"
# the text console helper of the console unit (appliance boards) and the desktop unit (desktop boards)
install -m 755 "$PAYLOAD/bin/$CONSOLE_HELPER" "$APP_ROOT/bin/$CONSOLE_HELPER"
# The earlier names stay for one release, as links: a unit of an earlier install that is still loaded (its ExecStop=,
# ExecStopPost=), or a script that starts the launcher by its old path, gets the new files.
ln -sfn "$LAUNCHER_BIN" "$APP_ROOT/bin/$OLD_LAUNCHER_BIN"
ln -sfn "$CONSOLE_HELPER" "$APP_ROOT/bin/$OLD_CONSOLE_HELPER"
echo "$LAUNCHER_BIN and $CONSOLE_HELPER installed; the earlier names $OLD_LAUNCHER_BIN and $OLD_CONSOLE_HELPER link to them"
remove_retired
# a PC distribution may lack a library the Pi images have: name the missing ones (the packages differ per distribution)
if [ "$IS_PC" = 1 ] && command -v ldd >/dev/null 2>&1; then
    for b in "$APP_ROOT/bin/$LAUNCHER_BIN" "$APP_ROOT/bin/M5CardputerZero-AppStore"; do
        [ -f "$b" ] || continue
        miss=$(ldd "$b" 2>/dev/null | sed -n 's/^[[:space:]]*\([^ ]*\) => not found.*/\1/p' | tr '\n' ' ')
        [ -z "$miss" ] || echo "WARNING: ${b##*/} needs libraries that are missing: $miss(apt-file search <name>)" >&2
    done
fi
chown -R "$TARGET_USER:$TARGET_USER" "$APP_ROOT/applications"
chmod -R a+rX "$APP_ROOT"

say "Installing the licences and notices into ${DOC_DIR#"$SYSROOT"}"
if [ -d "$PAYLOAD/doc" ]; then
    # the folder is this launcher's alone: replaced as a whole, so a licence file dropped from the bundle goes too
    rm -rf "$DOC_DIR"
    mkdir -p "$DOC_DIR"
    cp -R "$PAYLOAD/doc/." "$DOC_DIR/"
    chmod -R u=rwX,go=rX "$DOC_DIR"
    echo "$(find "$DOC_DIR" -type f | wc -l | tr -d ' ') files (LICENSE, LICENSES/, the components' and fonts' licences)"
else
    echo "this bundle has no payload/doc: skipped"
fi

say "Installing udev rules (keyboard: \"$KEYBOARD_NAME\")"
mkdir -p "$UDEV_DIR"
install -m 644 "$PAYLOAD/etc/90-backlight-unblank.rules" "$UDEV_DIR/90-backlight-unblank.rules"
install -m 644 "$PAYLOAD/etc/91-applaunch-vkbd.rules" "$UDEV_DIR/91-applaunch-vkbd.rules"
install -m 644 "$PAYLOAD/etc/93-applaunch-rfkill.rules" "$UDEV_DIR/93-applaunch-rfkill.rules"
cat > "$UDEV_DIR/99-bt-keyboard.rules" <<EOF
# Stable name for the Bluetooth keyboard (it gets a new event node each time it reconnects).
SUBSYSTEM=="input", KERNEL=="event*", ATTRS{name}=="$KEYBOARD_NAME", SYMLINK+="input/bt-keyboard"
EOF
if [ "$GPIO_BL" = 1 ]; then
    gpio_bl_rule > "$UDEV_DIR/$GPIO_BL_RULE"
    echo "on/off GPIO backlight: /etc/udev/rules.d/$GPIO_BL_RULE"
fi
if [ -n "$SYSFS_BL" ]; then
    sysfs_bl_rule > "$UDEV_DIR/$SYSFS_BL_RULE"
    echo "backlight levels ($SYSFS_BL): /etc/udev/rules.d/$SYSFS_BL_RULE"
fi
udevadm control --reload
udevadm trigger --subsystem-match=input --subsystem-match=misc --subsystem-match=backlight || true
# netdev (the polkit rule for NetworkManager) is a Debian group that other distributions may not have
if ! getent group netdev >/dev/null 2>&1; then groupadd -f netdev && echo "group netdev created (50-networkmanager-netdev.rules)"; fi
usermod -aG input,video,netdev "$TARGET_USER"

say "Installing PolicyKit rules (Wi-Fi, time settings, reboot/shutdown from the launcher)"
mkdir -p "$POLKIT_DIR"
install -m 644 "$PAYLOAD/etc/50-networkmanager-netdev.rules" "$POLKIT_DIR/50-networkmanager-netdev.rules"
install -m 644 "$PAYLOAD/etc/51-launcher-time-power.rules" "$POLKIT_DIR/51-launcher-time-power.rules"
if [ "$SESSION" = desktop ]; then
    # the menu entry starts the launcher's unit as the user, without a password
    render "$DESKTOP_RULES" > "$POLKIT_DIR/$DESKTOP_RULES.tmp"
    chmod 644 "$POLKIT_DIR/$DESKTOP_RULES.tmp"
    mv "$POLKIT_DIR/$DESKTOP_RULES.tmp" "$POLKIT_DIR/$DESKTOP_RULES"
    echo "desktop: /etc/polkit-1/rules.d/$DESKTOP_RULES ($TARGET_USER may start and stop $DESKTOP_UNIT)"
fi

# Before any new unit is installed or enabled: the earlier launcher is stopped first.
say "Migrating an earlier install (the names of v0.6.0 and before)"
migrate_old

say "Installing services"
mkdir -p "$SYSTEMD_DIR"
install -m 644 "$PAYLOAD/etc/launcher-ntp-default.service" "$SYSTEMD_DIR/launcher-ntp-default.service"
if [ "$SESSION" = desktop ]; then
    # Desktop variant: no user service, no lingering, getty@tty1 as it is (the desktop owns its own VT). The system
    # unit runs the launcher as the user when started; it is never enabled (no [Install] section).
    render "$DESKTOP_UNIT" > "$SYSTEMD_DIR/$DESKTOP_UNIT.tmp"
    chmod 644 "$SYSTEMD_DIR/$DESKTOP_UNIT.tmp"
    mv "$SYSTEMD_DIR/$DESKTOP_UNIT.tmp" "$SYSTEMD_DIR/$DESKTOP_UNIT"
    systemctl daemon-reload
    systemctl enable launcher-ntp-default.service
    mkdir -p "$MENU_DIR"
    install -m 644 "$PAYLOAD/etc/$DESKTOP_ENTRY_NAME" "$DESKTOP_ENTRY"
    echo "desktop: /etc/systemd/system/$DESKTOP_UNIT (stops $DM_UNIT while the launcher runs) and ${DESKTOP_ENTRY#"$SYSROOT"}"
    # an appliance install of this release must not start a second launcher at boot under the desktop (the earlier
    # names' user service is gone already: migrate_old)
    if [ -f "$UNIT_DIR/$USER_UNIT" ]; then
        as_user systemctl --user disable --now "$USER_UNIT" 2>/dev/null || true
        echo "the user service $USER_UNIT of an earlier install is disabled (the menu entry starts the launcher now)"
    fi
else
    systemctl daemon-reload
    systemctl enable launcher-ntp-default.service
    # the text console must not read the keyboard underneath the launcher
    systemctl disable --now getty@tty1.service 2>/dev/null || true
    systemctl mask getty@tty1.service 2>/dev/null || true
    # nor repaint over it: the panel leaves the text console once the boot is done (the boot lines still show)
    install -m 644 "$PAYLOAD/etc/$CONSOLE_UNIT" "$SYSTEMD_DIR/$CONSOLE_UNIT"
    systemctl daemon-reload
    systemctl enable --now "$CONSOLE_UNIT" || echo "($CONSOLE_UNIT did not start now; it runs at the next boot)"

    mkdir -p "$UNIT_DIR"
    cp "$PAYLOAD/etc/$USER_UNIT" "$UNIT_DIR/$USER_UNIT"
    chown -R "$TARGET_USER:$TARGET_USER" "$TARGET_HOME/.config"
    loginctl enable-linger "$TARGET_USER"
fi

say "Board profile"
apply_profile

# The PWM backlight overlay is for the deck's Waveshare panel (DECK_BOOT, see the board detection); a board with its
# own profile keeps its config.txt and only gets the cmdline arguments (never an fbcon= argument).
# A desktop board keeps its boot configuration as it is (its desktop owns the console settings).
if [ "$IS_PC" = 1 ]; then
    say "Boot configuration"
    echo "PC: GRUB and the kernel command line are left as they are (the boot lines show, as on the deck)"
elif [ "$SESSION" = desktop ]; then
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
        echo "${BOARD:-this board}: config.txt left as it is (the PWM backlight overlay is for the deck's Waveshare panel)"
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
    as_user systemctl --user enable "$USER_UNIT"
    if [ "$OLD_STILL_ACTIVE" = 1 ]; then
        echo "not started now: $OLD_USER_UNIT still runs (see the warning above); $USER_UNIT starts at the next boot"
    else
        as_user systemctl --user restart "$USER_UNIT" || echo "(could not start it now; it starts at the next boot)"
    fi
fi

say "Done"
if [ "$SESSION" = desktop ]; then
    echo "Start the launcher from the desktop: applications menu > CyberdeckZero Launcher (or: systemctl start $DESKTOP_UNIT)."
    echo "The desktop stops while it runs; Settings > System > Quit to desktop brings it back."
fi
echo "Next: pair the keyboard if you have not yet (bluetoothctl: scan on / pair / trust / connect)."
if [ "${REBOOT:-0}" = 1 ]; then
    echo "Boot configuration changed: reboot now so the $( [ "$DECK_BOOT" = 1 ] && echo 'PWM backlight and ' )console settings apply:  sudo reboot"
fi
