#!/bin/sh
# Install CyberdeckZero Launcher on a Raspberry Pi Zero 2 W with the Waveshare 2.8" DPI LCD (the deck), on another
# known board (see README.md, "Boards"), or on a PC (x86_64, the amd64 bundle: the "pc" profile, README.md, "Install on a PC (amd64)"):
# the board is detected and its profile written to /etc/applaunch/board.conf.
# A board that boots to a desktop (a display manager such as lightdm is enabled or running) gets the desktop
# variant: the launcher is started from the applications menu and gives the desktop back when it exits (README.md,
# "Desktop boards"); the others run it at boot as the user service cyberdeckzero.service. Install or remove a desktop
# later and run this script again: it switches between the two variants (the other one's units are taken off first).
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
#   --pwm-backlight        Pi 5 + HyperPixel 4 (pi5-hyperpixel4) only: also turn its on/off backlight into a PWM one
#                          with brightness levels (overlay hyperpixel4-pwm-backlight, a config.txt line, a reboot;
#                          README.md, "Pi 5 + HyperPixel 4: brightness levels"). Never done without this option
#   --board NAME           use this board profile instead of detecting it (zero2w, pi3a-luckfox35,
#                          pi3a-wsdsi43, pi3a-wsdsi43-portrait (PDA: never detected), pi4b-dsi, pi5-hyperpixel4,
#                          pi5-dsi7, hackberry-cm5, generic). generic: a screen that has no profile (untested hardware):
#                          the profile is written from what is detected (the panel's framebuffer and size, rotation,
#                          touch by capability, the backlight); README.md, "My screen is not listed"
#   --screen-size INCHES   the screen's diagonal in inches (1.5 to 15, e.g. 2.8, 3.5, 4.3, 5, 7). Panels that look the same to
#                          software (the 4.3", 5" and 7" DSI ones) cannot be told apart by the profile: this sets the
#                          profile's panel size (APPLAUNCH_PANEL_MM, the layout's density) from the diagonal and the
#                          resolution, and nothing else. Not for the deck (zero2w, no profile). Without it the profile
#                          keeps its own size; in a terminal, a first install asks
#   --panel-mm WxH         the panel's visible area in mm, exactly (wins over --screen-size)
#   --board-only           only detect the board and write/update /etc/applaunch/board.conf, then stop
#   --dry-run              print the detected board, the session and what would be done, change nothing
#   --plan                 the dry run as key=value lines for a program (Settings > Updates' helper,
#                          cyberdeckzero-update: board, session, variant switch, migration, board profile action)
#   --force                continue on an unknown board (no profile is written); replace a hand-edited
#                          /etc/applaunch/board.conf (the old one is kept as board.conf.bak)
#   --no-default-source    do not offer the apps repo (OSRdesign/cyberdeck-zero-apps) in Settings > Apps
#                          (default: added once for the user if they have no source yet, see default-sources.sh)
#   --spi-panel waveshare35f  Armbian on an Allwinner H616 / H618 board (Orange Pi Zero 2W): set up the Waveshare 3.5" RPi LCD (F)
#                          on the 40-pin header (its init file, the driver module, a DKMS build of drm_mipi_dbi for the
#                          SoC's DMA limit, the overlay in /boot/armbianEnv.txt) and write the pi3a-luckfox35 profile;
#                          then reboot (README.md, "Orange Pi Zero 2W: the 3.5" SPI panel")
#   --spi-panel luckfox35  a Raspberry Pi (Pi 5, Zero 2 W, Pi 3A+, ...; waveshare35f is the same panel and also accepted
#                          there): set up the Luckfox 3.5" RPi LCD (CTP) / Waveshare 3.5" RPi LCD (F) (ST7796S + GT911) in
#                          /boot/firmware/config.txt (a marked block in [all], the init file in /lib/firmware) and write the
#                          pi3a-luckfox35 profile, also on a desktop board (the option is explicit); then reboot (README.md,
#                          "Raspberry Pi: the 3.5" SPI panel"). ./uninstall.sh takes the block off again
#   --soft-pwm-backlight   with --spi-panel waveshare35f: brightness levels by software PWM on header pin 12 (DKMS module pwm-gpio)
#                          instead of the on/off backlight (not on a Raspberry Pi)
#   --network-manager      Debian / Ubuntu with netplan and no NetworkManager renderer yet: install NetworkManager and
#                          polkitd (Settings > Wi-Fi) and hand the network to it at the next boot; then reboot
#   --yes                  with --network-manager: go ahead when the network goes over Wi-Fi (asked on a terminal,
#                          refused without a terminal)
#
# Each install records its options (keyboard name, --board, --screen-size, --panel-mm, --no-boot-config, --pwm-backlight, --no-default-source) in
# /etc/applaunch/install.conf: Settings > Updates installs a newer release by running this script again with them.
# To remove everything this script adds, run ./uninstall.sh (see README.md, "Uninstall").
#
# Prerequisites that this script does NOT do:
#   - the panel's overlay in config.txt (the deck: config.txt.snippet; other boards: README.md, "Boards"; the 3.5" SPI panel: --spi-panel);
#   - pairing the Bluetooth keyboard (bluetoothctl);
#   - on a PC: installing NetworkManager unless --network-manager (Settings > Wi-Fi needs it, but it takes the network over
#     from netplan / systemd-networkd: the end of the install prints a recipe to run on the console). polkitd and BlueZ are installed
#     on a PC (reboot / shutdown / the clock, and Settings > Bluetooth, need them). A PC's boot configuration (GRUB,
#     kernel command line) is never changed.
set -eu

KEYBOARD_NAME="M4 Keyboard"
TARGET_USER=""
BOOT_CONFIG=1
PWM_BACKLIGHT=0
FORCE=0
DRY=0
BOARD_ONLY=0
BOARD_ARG=""
SIZE_ARG=""
MM_ARG=""
DEFAULT_SOURCE=1
PLAN=0
SPI_PANEL=""        # --spi-panel NAME (waveshare35f, luckfox35: a Raspberry Pi only)
SOFT_PWM=0          # --soft-pwm-backlight
NM_OPT=0            # --network-manager
YES=0               # --yes
# --screen-size and --panel-mm values: a diagonal 1.5 to 15 inches (digits and one point), WxH in mm (20 to 500 each)
valid_size() {
    case "$1" in ""|*[!0-9.]*|.*|*.|*.*.*) return 1 ;; esac
    awk -v d="$1" 'BEGIN { exit !(d + 0 >= 1.5 && d + 0 <= 15) }'
}
valid_mm() {
    case "$1" in [0-9]*x[0-9]*) ;; *) return 1 ;; esac
    vw=${1%x*}; vh=${1#*x}
    case "$vw$vh" in *[!0-9]*) return 1 ;; esac
    [ "$vw" -ge 20 ] && [ "$vw" -le 500 ] && [ "$vh" -ge 20 ] && [ "$vh" -le 500 ]
}
for arg in "$@"; do
    case "$arg" in
        -h|--help) sed -n '2,61p' "$0"; exit 0 ;;
        --dry-run|--plan) DRY=1 ;;
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
        --pwm-backlight) PWM_BACKLIGHT=1; shift ;;
        --board) [ $# -ge 2 ] || { echo "--board needs a name (zero2w, pi3a-luckfox35, pi3a-wsdsi43, pi3a-wsdsi43-portrait, pi4b-dsi, pi5-hyperpixel4, pi5-dsi7, hackberry-cm5, generic)" >&2; exit 2; }; BOARD_ARG="$2"; shift 2 ;;
        --screen-size) [ $# -ge 2 ] || { echo "--screen-size needs the diagonal in inches (1.5 to 15)" >&2; exit 2; }
                 valid_size "$2" || { echo "--screen-size: \"$2\" is not a diagonal in inches between 1.5 and 15 (for example 2.8, 3.5, 4.3, 5, 7)" >&2; exit 2; }
                 SIZE_ARG="$2"; shift 2 ;;
        --panel-mm) [ $# -ge 2 ] || { echo "--panel-mm needs WxH in mm (for example 108x65)" >&2; exit 2; }
                 valid_mm "$2" || { echo "--panel-mm: \"$2\" is not WxH in mm (20 to 500 each, for example 108x65)" >&2; exit 2; }
                 MM_ARG="$2"; shift 2 ;;
        --board-only) BOARD_ONLY=1; shift ;;
        --dry-run) DRY=1; shift ;;
        --plan) DRY=1; PLAN=1; shift ;;
        --force) FORCE=1; shift ;;
        --no-default-source) DEFAULT_SOURCE=0; shift ;;
        --spi-panel) [ $# -ge 2 ] || { echo "--spi-panel needs the panel's name (waveshare35f, luckfox35)" >&2; exit 2; }
                 case "$2" in
                     waveshare35f|luckfox35) ;;
                     *) echo "--spi-panel: unknown panel \"$2\" (known: waveshare35f, the Waveshare 3.5\" RPi LCD (F); luckfox35, the same panel family on a Raspberry Pi)" >&2; exit 2 ;;
                 esac
                 SPI_PANEL="$2"; shift 2 ;;
        --soft-pwm-backlight) SOFT_PWM=1; shift ;;
        --network-manager) NM_OPT=1; shift ;;
        --yes) YES=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
# the new options' combinations (the board itself is checked after the detection)
if [ "$SOFT_PWM" = 1 ] && [ -z "$SPI_PANEL" ]; then
    echo "--soft-pwm-backlight goes with --spi-panel waveshare35f (Armbian)" >&2; exit 2
fi
if [ -n "$SPI_PANEL" ]; then
    if [ "$BOARD_ONLY" = 1 ]; then
        echo "--spi-panel sets up the panel's driver and boot files: a whole install (or --dry-run), not --board-only" >&2; exit 2
    fi
    if [ "$BOOT_CONFIG" = 0 ]; then
        echo "--spi-panel adds its overlay to the boot configuration (/boot/armbianEnv.txt, or config.txt on a Raspberry Pi): it does not go with --no-boot-config" >&2; exit 2
    fi
    case "$BOARD_ARG" in
        ""|pi3a-luckfox35) ;;
        *) echo "--spi-panel $SPI_PANEL writes the pi3a-luckfox35 profile: leave --board out (or give --board pi3a-luckfox35)" >&2; exit 2 ;;
    esac
fi
if [ "$NM_OPT" = 1 ] && [ "$BOARD_ONLY" = 1 ]; then
    echo "--network-manager installs packages and changes the network set-up: a whole install (or --dry-run), not --board-only" >&2; exit 2
fi

# SYSROOT is a test hook: board and desktop detection read $SYSROOT/proc, $SYSROOT/sys and $SYSROOT/etc, and the
# profile and the backlight udev rule are written under $SYSROOT/etc. Leave it unset on a real board.
# INSTALL_FAKE_SYSTEM=1 (test hook, with SYSROOT) allows a whole install into $SYSROOT: every system path and the
# user's home are taken under it, and the test puts stand-ins for systemctl, runuser, loginctl, udevadm, usermod,
# dpkg, chown, uname and pkaction first on PATH (tests/test-install-board.sh). Never set it on a real board.
# INSTALL_GLIBC_VERSION is a test hook too: it replaces the glibc version read from the system; INSTALL_MACHINE
# replaces "uname -m" (aarch64, x86_64) for the board detection, INSTALL_KERNEL_RELEASE "uname -r" for --spi-panel.
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
CTL_BIN=cyberdeckzero-ctl                        # /usr/share/APPLaunch/bin/cyberdeckzero-ctl, linked in /usr/local/bin
UPDATE_HELPER=cyberdeckzero-update               # /usr/share/APPLaunch/bin/cyberdeckzero-update: Settings > Updates
ANCHOR_PKG=cyberdeckzero-runtime                 # the anchor package (report 047 D13): "Runtime anchor package" below
USER_UNIT=cyberdeckzero.service                  # appliance boards: systemd user service
CONSOLE_UNIT=cyberdeckzero-console.service       # appliance boards: frees the panel from the text console
DESKTOP_UNIT=cyberdeckzero-desktop.service       # desktop boards: started from the menu entry
DESKTOP_RULES=52-cyberdeckzero-desktop.rules     # desktop boards: the user may start and stop DESKTOP_UNIT
UDISKS_RULES=53-cyberdeckzero-udisks.rules       # every variant: the user may mount removable drives (udisksctl)
POWERKEY_DROPIN=50-cyberdeckzero-powerkey.conf   # Panasonic FZ-M1 only: logind drop-in, power key = standby (see pk_state)
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
# over the folder and never deletes by wildcard: installed apps live there too): the upstream start-up logo, two unused
# images, the upstream USB ADB gadget helpers, and since report 029 an empty placeholder library, an unused desktop
# template and the icons of the removed upstream ZClaw app; since the Store front end went from the home grid, its tile
# icons and the 15 images of its screens (the Store program itself stays: it is Settings > Apps' back end), and the
# icons of the removed 320x170 built-in apps Snake, Tank and IP Panel; and two large fonts nothing loads any more
# (Noto Serif CJK, the thin Alibaba PuHuiTi: report 048 section 3.3); and the unused, unlicensed icon font svgfont.ttf
# (removed from the bundle in 0.10.2, #164).
RETIRED_FILES="share/images/logo_lcd.png share/images/launcher_brand_logo.png share/images/carousel_zero_wordmark.png
adb/cardputer-adb adb/cardputer-adb-gadget lib/nihao.so applications/vim.desktop.temple share/images/claw_100.png
share/images/claw_80.png share/images/zclaw_avatar_16.png share/images/zclaw_send_button_18.png share/images/zclaw_sparkles_10.png
share/images/appstore.png share/images/appstore-shortcut-install.png share/images/appstore-shortcut-upgrade.png
share/images/appstore-shortcut-delete.png share/images/appstore-shortcut-back.png
share/images/appstore-shortcut-sharecode.png share/images/appstore-shortcut-search.png
share/images/appstore-shortcut-screenshots.png share/images/appstore-shortcut-settings.png
share/images/appstore-shortcut-detail.png share/images/store_arrow_down.png share/images/store_arrow_up.png
share/images/store_arrow_right.png share/images/store_arrow_left.png share/images/store_wordmark.png
share/images/store_100.png share/images/store_80.png
share/images/game_100.png share/images/game_80.png share/images/ip_panel_100.png share/images/ip_panel_80.png
share/images/tank.png share/images/tank_100.png share/images/tank_80.png
share/font/NotoSerifCJK-Regular.ttc share/font/AlibabaPuHuiTi-3-35-Thin.ttf share/font/svgfont.ttf"
RETIRED_DIRS="adb"                               # removed when empty afterwards

# System paths ($SYSROOT is empty on a real board)
APP_ROOT="$SYSROOT/usr/share/APPLaunch"
SYSTEMD_DIR="$SYSROOT/etc/systemd/system"
LOGIND_DIR="$SYSROOT/etc/systemd/logind.conf.d"
POLKIT_DIR="$SYSROOT/etc/polkit-1/rules.d"
UDEV_DIR="$SYSROOT/etc/udev/rules.d"
MENU_DIR="$SYSROOT/usr/share/applications"
DESKTOP_ENTRY="$MENU_DIR/$DESKTOP_ENTRY_NAME"
DOC_DIR="$SYSROOT/usr/share/doc/$DOC_NAME"
BOOT="$SYSROOT/boot/firmware"
[ -d "$BOOT" ] || BOOT="$SYSROOT/boot"

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
# A Raspberry Pi's device tree names it in "compatible" (raspberrypi,... and brcm,bcm...); a model string alone is not
# enough: an Orange Pi Zero 2W also says "Zero 2W". Without a compatible file (the test fixtures), the model has to
# start with "Raspberry Pi".
COMPAT=$(rd "$SYSROOT/proc/device-tree/compatible" || true)
IS_RPI=0
case "$COMPAT" in
    *raspberrypi*|*brcm,bcm*) IS_RPI=1 ;;
    "") case "$MODEL" in "Raspberry Pi"*) IS_RPI=1 ;; esac ;;
esac
# a PC: x86_64 and DMI (see "PC" below)
MACHINE=${INSTALL_MACHINE:-$(uname -m)}
DMI="$SYSROOT/sys/class/dmi/id"
DMI_VENDOR=$(rd "$DMI/sys_vendor" || true)
DMI_PRODUCT=$(rd "$DMI/product_name" || true)
DMI_CHASSIS=$(rd "$DMI/chassis_type" || true)

# ---------------------------------------------------------------------------------------------------------
# --spi-panel waveshare35f: the Waveshare 3.5" RPi LCD (F) (ST7796S 320x480 on SPI, Goodix GT911 touch on I2C, the
# backlight on header pin 12) plugged on the 40-pin header of an Allwinner H616 / H618 board with Armbian (tested on an
# Orange Pi Zero 2W, Armbian 26.11, kernel 6.18). A Raspberry Pi sets the same panel up in config.txt (--spi-panel
# luckfox35: "--spi-panel on a Raspberry Pi" below), not by the steps listed here. What is set up (the board's hand-made set-up of 2026-11-21, made repeatable):
#   - /lib/firmware/st7796s.bin, the panel's init sequence (Waveshare's file, checked by its sha256);
#   - /etc/modules-load.d/spi35.conf: panel-mipi-dbi (and pwm-gpio.conf with --soft-pwm-backlight);
#   - /boot/overlay-user/spi35-st7796s.dtbo, compiled with dtc -@ from the bundle's .dts (the on/off gpio-backlight, or with
#     --soft-pwm-backlight the software PWM backlight of the DKMS module pwm-gpio: the Armbian kernel has CONFIG_PWM_GPIO off,
#     and header pin 12 (PI1) has no hardware PWM on the H618);
#   - always the DKMS module drm-mipi-dbi-chunk: the kernel's own drm_mipi_dbi.c with each SPI transfer capped at 64 KiB
#     (chunk.patch). The H618's SPI DMA cannot reach RAM above 4 GB, so a 4 GB board bounces through swiotlb, whose segments
#     are 256 KiB at most; the 307200-byte frame then fails ("swiotlb buffer is full", "Failed to update display -5"). The
#     source must match the running kernel: it is fetched for its upstream tag from git.kernel.org (plain file, https), and
#     only when the module is not installed for this kernel yet, so a re-run needs no network. Never mem= on the kernel
#     command line (the earlier workaround: it cost 1 GB of RAM);
#   - user_overlays=spi35-st7796s in /boot/armbianEnv.txt (a dated backup first; the other user overlays kept).
# Every step that can fail (packages, headers, download, patch, dtc, DKMS build) runs before any file is put in place; a
# failure removes the DKMS modules this run added and stops the install with nothing of the panel changed.
SPI_IMPLIED=0      # 1: the pi3a-luckfox35 profile comes from --spi-panel (no --board given)
SPI_KREL=""        # the running kernel's release, 6.18.64-current-sunxi64
SPI_TAG=""         # its upstream tag, v6.18.64 (v6.18 for x.y.0)
SPI_HDR=""         # its headers package, linux-headers-current-sunxi64
SPI_PIN_NOTE=""    # a warning on an H616 / H618 board other than the Orange Pi Zero 2W
SPI_RPI=0          # 1: --spi-panel on a Raspberry Pi (config.txt, below), not the Armbian set-up
spi_refuse() {
    echo "--spi-panel $SPI_PANEL: $1" >&2
    echo "Nothing was changed." >&2
    exit 1
}
if [ -n "$SPI_PANEL" ]; then
    SPI_COMPAT=""
    [ ! -r "$SYSROOT/proc/device-tree/compatible" ] || SPI_COMPAT=$(tr '\0' ' ' < "$SYSROOT/proc/device-tree/compatible" | sed 's/ *$//')
    if [ "$IS_RPI" = 1 ]; then
        # a Raspberry Pi: the panel goes into config.txt (see "--spi-panel on a Raspberry Pi" below), no DKMS, no kernel checks
        SPI_RPI=1
        [ -f "$BOOT/config.txt" ] || spi_refuse "only on a Raspberry Pi with ${BOOT#"$SYSROOT"}/config.txt (not found: the panel's overlay goes there)."
        [ "$SOFT_PWM" = 0 ] || spi_refuse "--soft-pwm-backlight is for Armbian (waveshare35f); on a Raspberry Pi the panel's PWM backlight is the overlay's own."
        if [ -z "$BOARD_ARG" ]; then BOARD_ARG=pi3a-luckfox35; SPI_IMPLIED=1; fi
    else
        [ "$SPI_PANEL" != luckfox35 ] || spi_refuse "luckfox35 is the name for a Raspberry Pi with /boot/firmware/config.txt; on Armbian (Allwinner H616 / H618) use waveshare35f."
        case " $SPI_COMPAT " in
            *" allwinner,sun50i-h616 "*|*" allwinner,sun50i-h618 "*) ;;
            *) spi_refuse "for Allwinner H616 / H618 boards with Armbian only (this device tree: ${SPI_COMPAT:-none})." ;;
        esac
        if [ ! -f "$SYSROOT/boot/armbianEnv.txt" ]; then
            spi_refuse "no /boot/armbianEnv.txt: not an Armbian boot (the panel's overlay is loaded from there)."
        fi
        SPI_KREL=${INSTALL_KERNEL_RELEASE:-$(uname -r)}
        spi_kver=${SPI_KREL%%-*}
        spi_flavour=${SPI_KREL#*-}
        case "$spi_kver" in
            [0-9]*.[0-9]*) case "$spi_kver" in *[!0-9.]*|*..*|*.) spi_kver="" ;; esac ;;
            *) spi_kver="" ;;
        esac
        case "$spi_flavour" in ""|"$SPI_KREL"|*[!A-Za-z0-9.+-]*) spi_flavour="" ;; esac
        if [ -z "$spi_kver" ] || [ -z "$spi_flavour" ]; then
            spi_refuse "the kernel release \"$SPI_KREL\" is not of the form <version>-<flavour> (6.18.64-current-sunxi64): its upstream source and headers package cannot be told."
        fi
        spi_mm=$(printf '%s\n' "$spi_kver" | cut -d. -f1-2)
        spi_patch=$(printf '%s\n' "$spi_kver" | cut -d. -f3)
        if [ -z "$spi_patch" ] || [ "$spi_patch" = 0 ]; then SPI_TAG="v$spi_mm"; else SPI_TAG="v$spi_mm.$spi_patch"; fi
        SPI_HDR="linux-headers-$spi_flavour"
        if [ -z "$BOARD_ARG" ]; then BOARD_ARG=pi3a-luckfox35; SPI_IMPLIED=1; fi
        # the overlay's pins are the Orange Pi Zero 2W's 40-pin header (wired as on a Raspberry Pi): said on other H616 / H618 boards
        case " $SPI_COMPAT " in
            *" xunlong,orangepi-zero2w "*) ;;
            *) SPI_PIN_NOTE="WARNING: the overlay's pins are those of the Orange Pi Zero 2W's 40-pin header (SPI1 on PH5-PH8, I2C1 on PI7/PI8, pin 12 = PI1); this board (${SPI_COMPAT}) is untested: check its header before rebooting" ;;
        esac
    fi
fi

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

# BOARD: zero2w | pi3a-luckfox35 | pi3a-wsdsi43 | pi3a-wsdsi43-portrait | pi4b-dsi | pi5-hyperpixel4 | pi5-dsi7 | hackberry-cm5
# | pc-<model> | "" (unknown). pi3a-wsdsi43-portrait (the PDA profile, report 038) is the DSI panel turned to portrait:
# chosen only with --board, never detected (the same panel is pi3a-wsdsi43 when detected).
# PANEL_FB: the panel's framebuffer of a board with a profile (not the deck).
# BOARD_NOTE: why a known model was left unknown (two panels at once), printed with the facts.
BOARD=""
PANEL_FB=""
BOARD_NOTE=""
KNOWN_BOARDS="zero2w, pi3a-luckfox35, pi3a-wsdsi43, pi3a-wsdsi43-portrait, pi4b-dsi, pi5-hyperpixel4, pi5-dsi7, hackberry-cm5, generic"
# The panel of each profiled board: fbdev name prefix, buffer size (virtual_size), DRM connector and its mode.
# The RP1 DPI fbdev ("drm-rp1-dpidrmf") is the panel's native buffer: portrait 480x800 for the HyperPixel 4 (the
# launcher pre-rotates its 800x480 canvas 90 degrees), square 720x720 for the HackBerry (report 021, addenda 3, 4, 7).
# The Pi 3A+ takes one of two panels: the Luckfox 3.5" SPI panel (its own panel-mipi-dbi fbdev, portrait 320x480) or
# the Waveshare 4.3" DSI panel (the KMS fbdev "vc4drmfb", 800x480 landscape). The detection reads only the model, the
# DRM connector and the framebuffer, never config.txt: it does not depend on the overlay's name (the panel wants
# dtoverlay=vc4-kms-dsi-waveshare-800x480; the generic vc4-kms-dsi-7inch also drives it, but it flickers).
# The Pi 4 Model B takes the Freenove 5" DSI panel, same family: the same KMS fbdev 800x480 on a DSI connector (card1
# there); its auto-detection also wants the panel's backlight regulator at i2c 10-0045 (rpi_panel_attiny_regulator).
# The Pi 5 takes the same family of DSI panel (the official Raspberry Pi 7" Touch Display) on the RP1's DSI port: the
# fbdev is "drm-rp1-dsidrmf" 800x480 32 bpp (the HyperPixel's is "drm-rp1-dpidrmf", so the two never overlap); its
# auto-detection wants the 10-0045 regulator as well.
panel_spec() {
    case "$1" in
        pi3a-luckfox35) echo "panel-mipi-dbi 320,480 SPI-* 320x480" ;;
        pi3a-wsdsi43|pi3a-wsdsi43-portrait|pi4b-dsi) echo "vc4drmfb 800,480 DSI-* 800x480" ;;
        pi5-hyperpixel4) echo "drm-rp1-dpi 480,800 DPI-* 480x800" ;;
        pi5-dsi7) echo "drm-rp1-dsi 800,480 DSI-* 800x480" ;;
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
# --spi-panel before the reboot: the panel's framebuffer does not exist yet. It takes the next number after the framebuffers
# there now: /dev/fb0 when there is none (an H618 board without an HDMI monitor at boot: sun4i-drm makes no fbdev until a
# monitor is connected, so the panel is the only framebuffer), else the one after them (an HDMI monitor connected at boot:
# sun4i-drm's fbdev is built in and comes first). install.sh run again after the reboot finds the panel itself.
SPI_FB_PREDICTED=0
spi_predict_fb() {
    n=0
    for d in "$SYSROOT"/sys/class/graphics/fb[0-9]*; do
        [ -d "$d" ] || continue
        i=${d##*/fb}
        case "$i" in ""|*[!0-9]*) continue ;; esac
        [ "$i" -lt "$n" ] || n=$((i + 1))
    done
    echo "/dev/fb$n"
}
# The generic profile (a screen that has no profile of its own: untested hardware): the panel is the one connected internal
# DRM connector (DSI, DPI, SPI, eDP, LVDS) whose first mode is the size of a framebuffer (either orientation); with LONE=1
# (--board generic) the only framebuffer there is will do when there is no such connector. Sets GEN_FB, GEN_NW, GEN_NH
# (the buffer's size) and, when found, GEN_LW, GEN_LH (the landscape canvas) and GEN_ROT (90 when the buffer is portrait);
# else GEN_WHY says why not. True when exactly one panel was found.
GEN_FB=""; GEN_NW=""; GEN_NH=""; GEN_LW=""; GEN_LH=""; GEN_ROT=0; GEN_WHY=""; GEN_AUTO=0
generic_detect() {
    GEN_FB=""; GEN_WHY=""
    gd_list=""; gd_n=0; gd_s=""
    for c in "$SYSROOT"/sys/class/drm/card[0-9]*-*; do
        [ -d "$c" ] || continue
        case "${c##*/}" in *-DSI-*|*-DPI-*|*-SPI-*|*-eDP-*|*-LVDS-*) ;; *) continue ;; esac
        [ "$(rd "$c/status" || true)" = connected ] || continue
        m=$(rd "$c/modes" || true)
        case "$m" in [0-9]*x[0-9]*) ;; *) continue ;; esac
        mw=${m%%x*}; mh=${m#*x}; mh=${mh%%[!0-9]*}
        for f in "$SYSROOT"/sys/class/graphics/fb[0-9]*; do
            [ -d "$f" ] || continue
            s=$(rd "$f/virtual_size" || true)
            if [ "$s" = "$mw,$mh" ] || [ "$s" = "$mh,$mw" ]; then
                case " $gd_list " in
                    *" /dev/${f##*/} "*) ;;
                    *) gd_list="$gd_list /dev/${f##*/}"; gd_n=$((gd_n + 1)); gd_s=$s ;;
                esac
                break
            fi
        done
    done
    if [ "$gd_n" = 0 ] && [ "${1:-0}" = 1 ]; then
        for f in "$SYSROOT"/sys/class/graphics/fb[0-9]*; do
            [ -d "$f" ] || continue
            s=$(rd "$f/virtual_size" || true)
            case "$s" in [0-9]*,[0-9]*) gd_list="$gd_list /dev/${f##*/}"; gd_n=$((gd_n + 1)); gd_s=$s ;; esac
        done
    fi
    if [ "$gd_n" = 0 ]; then
        GEN_WHY="no panel found (a connected DSI, DPI, SPI, eDP or LVDS connector with a framebuffer of its size): is the screen's overlay or driver loaded?"
        return 1
    elif [ "$gd_n" != 1 ]; then
        GEN_WHY="more than one panel found ($gd_list): this installer does not guess which one is the launcher's screen"
        return 1
    fi
    GEN_FB=${gd_list# }; GEN_NW=${gd_s%,*}; GEN_NH=${gd_s#*,}
    if [ "$GEN_NW" -lt "$GEN_NH" ]; then GEN_LW=$GEN_NH; GEN_LH=$GEN_NW; GEN_ROT=90
    else GEN_LW=$GEN_NW; GEN_LH=$GEN_NH; GEN_ROT=0; fi
    return 0
}
if [ -n "$BOARD_ARG" ]; then
    case "$BOARD_ARG" in
        generic) ;;                         # after the PC check below
        zero2w) BOARD=zero2w ;;
        pi3a-luckfox35|pi3a-wsdsi43|pi3a-wsdsi43-portrait|pi4b-dsi|pi5-hyperpixel4|pi5-dsi7|hackberry-cm5)
            BOARD=$BOARD_ARG
            detect_panel "$BOARD" || true
            if [ -z "$PANEL_FB" ] && [ -n "$SPI_PANEL" ]; then
                # the panel comes with the overlay at the next boot: its framebuffer is foreseen (spi_predict_fb)
                PANEL_FB=$(spi_predict_fb); SPI_FB_PREDICTED=1
            elif [ -z "$PANEL_FB" ]; then
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
        *"Zero 2"*)
            # The deck (no SPI panel: its Waveshare 2.8" is a DPI panel) stays zero2w; a Zero 2 W with a connected SPI connector
            # at 320x480 and a panel-mipi-dbi framebuffer (the 3.5" panel of zero8) is the SPI panel's board, as on a Pi 5 below.
            if [ "$IS_RPI" = 1 ]; then
                BOARD=zero2w
                # a DSI or DPI connector connected as well is a second panel: not guessed, the deck stays
                z_other=0
                for d in "$SYSROOT"/sys/class/drm/card[0-9]*-*; do
                    case "${d##*/}" in *-DSI-*|*-DPI-*) ;; *) continue ;; esac
                    if [ "$(rd "$d/status" || true)" = connected ]; then z_other=1; fi
                done
                if [ "$z_other" = 0 ] && detect_panel pi3a-luckfox35; then
                    BOARD=pi3a-luckfox35
                else
                    PANEL_FB=""
                fi
            fi ;;
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
        *"Raspberry Pi 4 Model B"*)
            if detect_panel pi4b-dsi && [ -d "$SYSROOT/sys/class/backlight/10-0045" ]; then BOARD=pi4b-dsi; else PANEL_FB=""; fi ;;
        *"Raspberry Pi 5"*)
            # the HyperPixel 4 (DPI fbdev) first; else the 7" DSI panel, which also wants its 10-0045 regulator
            if detect_panel pi5-hyperpixel4; then BOARD=pi5-hyperpixel4
            elif detect_panel pi5-dsi7 && [ -d "$SYSROOT/sys/class/backlight/10-0045" ]; then BOARD=pi5-dsi7
            else PANEL_FB=""; fi ;;
        *"Compute Module 5"*) detect_panel hackberry-cm5 && BOARD=hackberry-cm5 ;;
    esac
    # The 3.5" SPI panel on any other Raspberry Pi (a Pi 5, a Pi 4B without its DSI panel, ...): left over by the cases above.
    # The Zero 2 W (deck unless an SPI panel is connected, see its case above) and the Pi 3A+ have their own cases. A DSI or
    # DPI connector connected as well is a second panel: not guessed (HDMI is fine).
    if [ -z "$BOARD" ] && [ "$IS_RPI" = 1 ]; then
        case "$MODEL" in
            *"Zero 2"*|*"Raspberry Pi 3 Model A Plus"*) ;;
            *)
                if detect_panel pi3a-luckfox35; then
                    SPI_OTHER=""; SPI_SEEN=$PANEL_FB
                    for d in "$SYSROOT"/sys/class/drm/card[0-9]*-*; do
                        case "${d##*/}" in *-DSI-*|*-DPI-*) ;; *) continue ;; esac
                        if [ "$(rd "$d/status" || true)" = connected ]; then SPI_OTHER="$SPI_OTHER ${d##*/}"; fi
                    done
                    if [ -n "$SPI_OTHER" ]; then
                        PANEL_FB=""
                        BOARD_NOTE="SPI panel ($SPI_SEEN: pi3a-luckfox35) and another panel connected (${SPI_OTHER# }): choose one with --board."
                    else
                        BOARD=pi3a-luckfox35
                    fi
                else
                    PANEL_FB=""
                fi ;;
        esac
    fi
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
# The Panasonic FZ-M1 (DMI FZM1-2* / FZ-M1*) alone gets the power-key drop-in; no other PC, no Pi.
pc_is_fzm1() {
    case "$DMI_PRODUCT" in FZM1-2*|FZ-M1*) return 0 ;; esac
    return 1
}
# The drop-in: Armbian's logind default powers the tablet off on a short press of the power key; here a short press
# suspends (standby) and a long press powers off. The same two keys the owner has applied by hand since v0.8.7.
pk_text() {
    echo "# Written by the CyberdeckZero launcher's install.sh (Panasonic FZ-M1 only): a short press of the power key"
    echo "# suspends (standby), a long press powers off. Armbian's logind default powers off on a short press."
    echo "# Remove this file (or run uninstall.sh) to go back to the default. Read by: systemctl kill -s HUP systemd-logind"
    echo "[Login]"
    echo "HandlePowerKey=suspend"
    echo "HandlePowerKeyLongPress=poweroff"
}
# pk_keys FILE: the settings of a logind file, without comments, blank lines, spaces and CRs (what logind acts on)
pk_keys() { tr -d '\r' < "$1" 2>/dev/null | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' -e '/^#/d' -e '/^;/d' -e '/^$/d'; }
# pk_state: absent, same (the file there has our settings, whatever its comments) or different (edited: never replaced
# without --force)
pk_state() {
    f="$LOGIND_DIR/$POWERKEY_DROPIN"
    if [ ! -e "$f" ]; then echo absent
    elif [ "$(pk_keys "$f")" = "$(pk_text | pk_keys /dev/stdin)" ]; then echo same
    else echo different; fi
}
IS_PC=0
PC_CONN=""; PC_MM=""; PC_MM_FROM=""; PC_LOGICAL=""
# the packages of a PC: the launcher's libraries, polkitd (logind and timedated ask polkit before a reboot, a shutdown or
# a clock change from the launcher's user service; Debian 13 and Ubuntu 24.04+ name it polkitd), BlueZ and alsa-utils
# (amixer: Settings > Speaker finds the sound card's volume control with it, and sets the volume when no sound server
# runs, report 030; the Pi images have it)
PC_PKGS="libinput10 libxkbcommon0 libfreetype6 curl polkitd bluez alsa-utils"
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
# The generic profile: with --board generic (not on a PC: it has its own profile), and on an unknown board installed with
# --force when exactly one panel is found (then install.conf records board=generic: the updates keep it).
if [ "$BOARD_ARG" = generic ]; then
    if [ "$IS_PC" = 1 ]; then
        echo "--board generic is not for a PC (it has its own profile: leave --board out)." >&2; exit 2
    fi
    if ! generic_detect 1; then
        echo "--board generic: $GEN_WHY" >&2
        print_facts >&2
        echo "Nothing was changed." >&2
        exit 1
    fi
    BOARD="generic-${GEN_LW}x${GEN_LH}"; PANEL_FB=$GEN_FB
elif [ -z "$BOARD" ] && [ "$FORCE" = 1 ] && [ "$IS_PC" = 0 ] && [ -z "$BOARD_NOTE" ] && generic_detect 0; then
    BOARD="generic-${GEN_LW}x${GEN_LH}"; PANEL_FB=$GEN_FB; GEN_AUTO=1
fi
# On an unknown board without --force: when a panel is found, say that the generic profile exists
generic_hint() {
    if [ "$IS_PC" = 0 ] && [ -z "$BOARD_NOTE" ] && generic_detect 0; then
        echo "A panel was found ($GEN_FB, ${GEN_NW}x${GEN_NH}): --board generic (with --screen-size INCHES) writes a generic profile for it"
        echo "(untested screen; README.md, \"My screen is not listed\")."
    fi
}
# The deck's PWM backlight overlay goes into config.txt on the deck and, as before, on an unknown board installed with
# --force, except when the model is one of the other boards' (Pi 3A+, Pi 4 B, Pi 5, CM5) and only its panel was not
# recognised: those panels bring their own backlight (and overlay), so config.txt stays as it is there.
DECK_BOOT=0
if [ "$BOARD" = zero2w ]; then
    DECK_BOOT=1
elif [ -z "$BOARD" ] && [ "$IS_PC" = 0 ]; then
    case "$MODEL" in
        *"Raspberry Pi 3 Model A Plus"*|*"Raspberry Pi 4 Model B"*|*"Raspberry Pi 5"*|*"Compute Module 5"*) ;;
        *) if [ "$IS_RPI" = 1 ]; then DECK_BOOT=1; fi ;;
    esac
fi
# The boot configuration (config.txt, cmdline.txt) exists on Raspberry Pi firmware boards only. Another board (an Orange Pi
# with Armbian: /boot/armbianEnv.txt and boot.cmd) is left alone, and the install goes on: the step is optional.
BOOT_FW=1
BOOT_FW_WHY=""
if [ "$IS_PC" = 0 ]; then
    if [ "$IS_RPI" = 0 ]; then
        BOOT_FW=0
        if [ -f "$BOOT/config.txt" ]; then BOOT_FW_WHY="not a Raspberry Pi firmware board: the device tree is not a Raspberry Pi's"
        else BOOT_FW_WHY="not a Raspberry Pi firmware board: ${BOOT#"$SYSROOT"}/config.txt not found"; fi
    fi
fi
# The Pi 5 + HyperPixel 4's own PWM backlight overlay (brightness levels, report 030 c-4): only when asked with
# --pwm-backlight, only on that board, on the appliance and the desktop variant alike; --no-boot-config wins.
HP_PWM=0
HP_PWM_NOTE=""
HP_DTS=hyperpixel4-pwm-backlight.dts
HP_DTBO=hyperpixel4-pwm-backlight.dtbo
HP_LINE=dtoverlay=hyperpixel4-pwm-backlight
# The PWM channel of GPIO18 (header pin 12) on a model whose PWM0 has no other user (no headphone jack): 0 on a Zero 2 W
# (the overlay's default channel), 2 on a Pi 5 (GPIO18 is RP1 PWM0 channel 2: the overlay's default channel 0 binds the wrong
# one, dtparam=backlight-pwm-chan=2 is required), nothing on the others (the Pi 3A+'s PWM0 is the headphone jack's). There the
# 3.5" SPI panel (pi3a-luckfox35) gets its PWM backlight by default (LF_SETUP below)
lf_pwm_chan() {
    [ "$IS_RPI" = 1 ] || return 0
    case "$MODEL" in *"Zero 2"*) echo 0 ;; *"Raspberry Pi 5"*) echo 2 ;; esac
}
lf_pwm_free() { [ -n "$(lf_pwm_chan)" ]; }
LF_CHAN=""      # the dtparam=backlight-pwm-chan= value to write, only where it is not the default (a Pi 5: 2)
[ "$(lf_pwm_chan)" != 2 ] || LF_CHAN=2
if [ "$PWM_BACKLIGHT" = 1 ]; then
    if [ "$BOARD" = pi3a-luckfox35 ] && lf_pwm_free; then
        HP_PWM_NOTE="--pwm-backlight: not needed on pi3a-luckfox35 on a Zero 2 W or a Pi 5 (its panel's PWM backlight needs no option): ignored"
    elif [ "$BOARD" != pi5-hyperpixel4 ]; then
        HP_PWM_NOTE="--pwm-backlight is for the Pi 5 + HyperPixel 4 (pi5-hyperpixel4) only: ignored on ${BOARD:-this board}"
    elif [ "$BOOT_CONFIG" = 0 ]; then
        HP_PWM_NOTE="--pwm-backlight: skipped (--no-boot-config)"
    else
        HP_PWM=1
    fi
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
# The default target the system boots to: the desktop variant leaves the boot to the display manager, which starts
# only with graphical.target. Read only: the default.target link (systemctl set-default writes the one in /etc).
boot_target() {
    for f in "$SYSROOT/etc/systemd/system/default.target" "$SYSROOT/usr/lib/systemd/system/default.target" \
             "$SYSROOT/lib/systemd/system/default.target"; do
        if [ -L "$f" ]; then t=$(readlink "$f" 2>/dev/null || true); echo "${t##*/}"; return 0; fi
    done
}
BOOT_TARGET=$(boot_target || true)
# true when the unit $1 that display-manager.service points to ($2) is installed: a desktop removed with its display
# manager (armbian-config's remove purges the packages) can leave the link behind, pointing nowhere
dm_unit_installed() {
    case "$2" in /*) [ -e "$SYSROOT$2" ] && return 0 ;; esac
    for d in /etc/systemd/system /run/systemd/system /usr/lib/systemd/system /lib/systemd/system; do
        [ -e "$SYSROOT$d/$1" ] && return 0
    done
    return 1
}
# SESSION: appliance | desktop. DM_UNIT: the display manager's unit (desktop only). SESSION_WHY: the evidence.
SESSION=appliance
DM_UNIT=""
SESSION_WHY=""
DM_LINK="$SYSROOT/etc/systemd/system/display-manager.service"
DM_ENABLED=0                                    # 1: display-manager.service is there and names an installed unit
[ ! -e "$DM_LINK" ] || [ -L "$DM_LINK" ] || DM_ENABLED=1
DESK_PROCS=$(cat "$SYSROOT"/proc/[0-9]*/comm 2>/dev/null | match_procs "$DESKTOP_NAMES")
if [ -L "$DM_LINK" ]; then
    # the display manager enabled for boot (Debian: an alias symlink to its unit)
    dm_target=$(readlink "$DM_LINK" 2>/dev/null || true)
    DM_UNIT=${dm_target##*/}
    case "$DM_UNIT" in
        *?.service)
            if dm_unit_installed "$DM_UNIT" "$dm_target"; then DM_ENABLED=1; SESSION_WHY="display manager $DM_UNIT enabled"
            else SESSION_WHY="display-manager.service points to $DM_UNIT, which is not installed (a removed desktop)"; DM_UNIT=""; fi ;;
        *) DM_UNIT="" ;;
    esac
fi
if [ -z "$DM_UNIT" ]; then
    for n in $DM_NAMES; do
        case "+$DESK_PROCS+" in *"+$n+"*) DM_UNIT="$n.service"; break ;; esac
    done
fi
if [ -n "$DM_UNIT" ] || [ -n "$DESK_PROCS" ] || [ "$DM_ENABLED" = 1 ]; then
    SESSION=desktop
    if [ -z "$DM_UNIT" ]; then DM_UNIT=lightdm.service; SESSION_WHY="no display manager unit found: $DM_UNIT assumed"; fi
    [ -z "$DESK_PROCS" ] || SESSION_WHY="${SESSION_WHY:+$SESSION_WHY; }running: $DESK_PROCS"
fi
# A display manager enabled for boot that never starts (the system boots to multi-user.target: armbian-config's remove
# sets it, so does a hand-made appliance) and does not run now: the launcher is what starts at boot
if [ "$SESSION" = desktop ] && [ -z "$DESK_PROCS" ] && [ -n "$BOOT_TARGET" ] && [ "$BOOT_TARGET" != graphical.target ]; then
    SESSION=appliance
    SESSION_WHY="display manager $DM_UNIT enabled, but the system boots to $BOOT_TARGET, where it does not start: the launcher starts at boot (for the desktop variant: sudo systemctl set-default graphical.target, then install.sh again)"
fi
if [ "$SESSION" = appliance ]; then
    SESSION_WHY="${SESSION_WHY:-no display manager enabled or running}"
    case "$SESSION_WHY" in *"the launcher starts at boot"*) ;; *) SESSION_WHY="$SESSION_WHY: the launcher starts at boot" ;; esac
fi
# the deck's environment lives in its user service (it has no profile): no desktop variant for it
DESKTOP_REFUSED=0
if [ "$SESSION" = desktop ] && [ "$BOARD" = zero2w ]; then DESKTOP_REFUSED=1; fi
# The desktop variant leaves the boot to the display manager: say so when it may not start at boot (found running only)
DESKTOP_BOOT_WARN=""
if [ "$SESSION" = desktop ] && [ "$DESKTOP_REFUSED" = 0 ]; then
    if [ "$DM_ENABLED" = 0 ]; then
        DESKTOP_BOOT_WARN="no display manager is enabled for boot (no /etc/systemd/system/display-manager.service): the desktop may not come up at boot, and the launcher no longer starts at boot either. Check: systemctl status display-manager"
    elif [ -n "$BOOT_TARGET" ] && [ "$BOOT_TARGET" != graphical.target ]; then
        DESKTOP_BOOT_WARN="the system boots to $BOOT_TARGET (systemctl get-default): $DM_UNIT does not start at boot, and the launcher no longer does either. Fix: sudo systemctl set-default graphical.target"
    fi
fi
# Desktop variant: starting the launcher stops the display manager, and WirePlumber 0.5 then drops its Bluetooth
# monitor (seat monitoring: no active seat), so BlueZ has no A2DP endpoint and a Bluetooth speaker fails to connect
# ("a2dp-sink profile connect failed: Protocol not available"). A per-user drop-in turns seat monitoring off.
WP_DROPIN_REL=".config/wireplumber/wireplumber.conf.d/50-cyberdeckzero-bluez-no-seat.conf"
wireplumber_installed() {
    if [ -n "$SYSROOT" ]; then [ -x "$SYSROOT/usr/bin/wireplumber" ]
    else command -v wireplumber >/dev/null 2>&1 || [ -x /usr/bin/wireplumber ]; fi
}
wp_dropin_content() {
    echo "# CyberdeckZero: the launcher stops the display manager, which makes WirePlumber drop Bluetooth audio (seat monitoring)"
    echo "wireplumber.profiles = {"
    echo "  main = {"
    echo "    monitor.bluez.seat-monitoring = disabled"
    echo "  }"
    echo "}"
}
WP_DROPIN=0
if [ "$SESSION" = desktop ] && [ "$DESKTOP_REFUSED" = 0 ] && wireplumber_installed; then WP_DROPIN=1; fi

# The profile for the board ("" = no profile file: the deck keeps the service's own environment).
# It is a systemd EnvironmentFile: its values override the Environment= lines of cyberdeckzero.service,
# so the deck's legacy touch axes and fb0 are neutralised here instead of being removed from the service.
# The body below is checksummed into the file (profile_state): changing its text, comments included, rewrites the
# profile of every board at the next install (the old one kept as board.conf.bak). Its comments still name the user
# service of v0.6.0 for that reason.
# A backlight with levels (an APPLAUNCH_BACKLIGHT=sysfs: line) comes with its idle dim level written out: the
# launcher's default 10 %, pi4b-dsi and pi5-dsi7 their own 20 (in their bodies). On/off and no backlight: no dim line (nothing to dim to).
dim_lines() {
    echo "# idle dim level, percent of the backlight: 1..100 (the launcher's default is 10)"
    echo "APPLAUNCH_DIM_PERCENT=$1"
}
# The screen size (--screen-size, --panel-mm, the install question; resolved below, before the first profile_body): when it
# changes the profile's own PANEL_MM (SCREEN_APPLY=1) the panel-dependent lines are replaced; nothing else changes. The old
# size lines of the profile go (its comments too) and "APPLAUNCH_PANEL_MM=" is put before the touch lines, where the DSI
# profiles have it. With no change the body is the profile's own, byte for byte.
SCREEN_APPLY=0; SCREEN_MM=""; SCREEN_NOTE=""; SCREEN_DIM20=0
profile_body() {
    if [ "$SCREEN_APPLY" = 0 ]; then profile_body_base; return; fi
    sc="/^# landscape visible area in mm/d
/^# landscape panel size in mm/d
/^# panel size in mm, from/d
/^# panel size unknown/d
/^APPLAUNCH_PANEL_MM=/d
s|^APPLAUNCH_TOUCH_DEV=|# panel visible area in mm ($SCREEN_NOTE): the layout's density\\
APPLAUNCH_PANEL_MM=$SCREEN_MM\\
APPLAUNCH_TOUCH_DEV=|"
    if [ "$SCREEN_DIM20" = 1 ]; then
        sc="$sc
s|^APPLAUNCH_DIM_PERCENT=.*|APPLAUNCH_DIM_PERCENT=20|
s|^# idle dim level.*|# idle dim level, percent of the backlight (default 10; 20 for the 5-inch panel, where raw 50/255 is dimmed but readable)|"
    fi
    profile_body_base | sed -e "$sc"
}
profile_body_base() {
    case "$BOARD" in
        # The backlight lines are the only ones that vary: the on/off backlight_gpio, or the overlay's PWM backlight_pwm
        # and its idle dim level when config.txt has it or this run sets it up (LF_PWM). The on/off body is byte for byte
        # the one of v0.9.3 and before, so a board without the PWM backlight keeps its generated profile as it is.
        # On a board that is not a Raspberry Pi (no config.txt) the backlight is what /sys/class/backlight has, or what
        # --spi-panel's overlay brings (LF_BL): levels (sysfs: and the dim level), on/off backlight_gpio, or none (no line).
        pi3a-luckfox35)
            cat <<EOF
APPLAUNCH_BOARD=pi3a-luckfox35
APPLAUNCH_FB=$PANEL_FB
APPLAUNCH_LOGICAL=480x320
APPLAUNCH_ROTATE=90
APPLAUNCH_COMPAT_SCALE=1
# landscape panel size in mm (the overlay's width-mm=49 height-mm=79, portrait): the layout's density
APPLAUNCH_PANEL_MM=79x49
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
EOF
            if [ "$LF_PWM" = 1 ]; then echo "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/backlight_pwm"; dim_lines 10
            elif [ "$IS_RPI" = 0 ]; then
                case "$LF_BL" in
                    "") ;;
                    gpio) echo "APPLAUNCH_BACKLIGHT=gpio:/sys/class/backlight/backlight_gpio" ;;
                    *) echo "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/$LF_BL"; dim_lines 10 ;;
                esac
            else echo "APPLAUNCH_BACKLIGHT=gpio:/sys/class/backlight/backlight_gpio"; fi
            cat <<EOF
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
            if [ -n "$bl" ]; then echo "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/$bl"; dim_lines 10; fi
            cat <<EOF
LV_LINUX_FBDEV_DEVICE=$PANEL_FB
# the deck's legacy touch axes (set in its user service) must not apply here: neutral values
APPLAUNCH_TOUCH_SWAP_XY=0
APPLAUNCH_TOUCH_INVERT_X=0
APPLAUNCH_TOUCH_INVERT_Y=0
EOF
        ;;
        # The PDA profile (report 038, phases S2 and S3): the same DSI panel turned to portrait, 480x800, with the writing
        # zone (report 041). Only with --board.
        # No APPLAUNCH_COMPAT_SCALE: 320x170 fits 1x only on a 480 px wide canvas. Landscape fullscreen apps run turned
        # (a quarter turn back from APPLAUNCH_ROTATE, full screen 800x480): the launcher sets that per launch.
        pi3a-wsdsi43-portrait)
            bl=$(dsi_backlight)
            cat <<EOF
APPLAUNCH_BOARD=pi3a-wsdsi43-portrait
APPLAUNCH_FB=$PANEL_FB
APPLAUNCH_LOGICAL=480x800
# 90: the panel's right edge (as it sits in landscape) at the top; 270: its left edge at the top (edit by hand)
APPLAUNCH_ROTATE=90
# landscape visible area in mm, as pi3a-wsdsi43 (the launcher turns it to the portrait canvas): the layout's density
APPLAUNCH_PANEL_MM=94x56
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
EOF
            if [ -n "$bl" ]; then echo "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/$bl"; dim_lines 10; fi
            cat <<EOF
# the fixed-pixel built-in tiles (IP Panel, Snake, Tank: 320x170 pages; the Store) start hidden on the portrait panel;
# Settings > Launcher shows them, and what is saved there wins
APPLAUNCH_HIDE_TILES=app_IP_Panel,app_Game,app_Tank,app_Store
# the writing zone at the bottom (report 041, 042): pen strokes and the key strip, Settings > On-screen keyboard
# "Handwriting + keyboard" (its default on this profile while nothing is stored); 0 = none
APPLAUNCH_PDA=1
LV_LINUX_FBDEV_DEVICE=$PANEL_FB
# the deck's legacy touch axes (set in its user service) must not apply here: neutral values
APPLAUNCH_TOUCH_SWAP_XY=0
APPLAUNCH_TOUCH_INVERT_X=0
APPLAUNCH_TOUCH_INVERT_Y=0
EOF
        ;;
        # Pi 4 Model B with the Freenove 5" DSI panel: the values of the hand-written profile that works on the board
        # (2026-10-25: touch and home grid checked by the owner). Same panel family as the 4.3" one: landscape 800x480
        # KMS framebuffer, no rotation, touch (ft5x06) by capability in buffer orientation, the overlay's i2c regulator
        # as backlight. The idle dim level is raised: 10 % of this panel's 0..255 range is black.
        pi4b-dsi)
            bl=$(dsi_backlight)
            cat <<EOF
APPLAUNCH_BOARD=pi4b-dsi
APPLAUNCH_FB=$PANEL_FB
APPLAUNCH_LOGICAL=800x480
APPLAUNCH_ROTATE=0
APPLAUNCH_COMPAT_SCALE=2
# landscape visible area in mm of the 5" panel (a guess, to be checked with a ruler): the layout's density
APPLAUNCH_PANEL_MM=108x65
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
EOF
            if [ -n "$bl" ]; then echo "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/$bl"; fi
            cat <<EOF
# idle dim level, percent of the backlight (default 10; raw 50/255 is dimmed but readable on this panel)
APPLAUNCH_DIM_PERCENT=20
LV_LINUX_FBDEV_DEVICE=$PANEL_FB
# the deck's legacy touch axes (set in its user service) must not apply here: neutral values
APPLAUNCH_TOUCH_SWAP_XY=0
APPLAUNCH_TOUCH_INVERT_X=0
APPLAUNCH_TOUCH_INVERT_Y=0
EOF
        ;;
        # Pi 5 with the official 7" Touch Display on the RP1 DSI port: the values of the hand-written profile that works
        # on the board (2026-11-11: home grid, touch and brightness checked by the owner). Same family as pi4b-dsi:
        # landscape 800x480 fbdev, no rotation, touch (ft5x06 at i2c 10-0038) by capability in buffer orientation, the
        # overlay's i2c regulator as backlight. Panel size: the display's 154x86 mm active area.
        pi5-dsi7)
            bl=$(dsi_backlight)
            cat <<EOF
APPLAUNCH_BOARD=pi5-dsi7
APPLAUNCH_FB=$PANEL_FB
APPLAUNCH_LOGICAL=800x480
APPLAUNCH_ROTATE=0
APPLAUNCH_COMPAT_SCALE=2
# landscape visible area in mm of the 7" panel (the official display's active area): the layout's density
APPLAUNCH_PANEL_MM=154x86
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
EOF
            if [ -n "$bl" ]; then echo "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/$bl"; fi
            cat <<EOF
# idle dim level, percent of the backlight (default 10; raw 50/255 is dimmed but readable on this panel family)
APPLAUNCH_DIM_PERCENT=20
LV_LINUX_FBDEV_DEVICE=$PANEL_FB
# the deck's legacy touch axes (set in its user service) must not apply here: neutral values
APPLAUNCH_TOUCH_SWAP_XY=0
APPLAUNCH_TOUCH_INVERT_X=0
APPLAUNCH_TOUCH_INVERT_Y=0
EOF
        ;;
        # A screen that has no profile (--board generic, or --force on an unknown board with one panel): what was detected.
        # The canvas is the landscape size of the framebuffer (a portrait buffer is turned: ROTATE=90, as the HyperPixel 4
        # profile does), the compat scale 1 below 640 px wide (the Luckfox 3.5" profile) else 2 (the DSI profiles), touch by
        # capability in buffer orientation, the backlight as the DSI profiles find it (an on/off GPIO one as the Luckfox
        # profile names it). No panel size unless --screen-size or --panel-mm gives one (see profile_body): the launcher's
        # default density then.
        generic-*)
            bl=$(dsi_backlight)
            echo "APPLAUNCH_BOARD=$BOARD"
            echo "APPLAUNCH_FB=$PANEL_FB"
            echo "APPLAUNCH_LOGICAL=${GEN_LW}x${GEN_LH}"
            echo "APPLAUNCH_ROTATE=$GEN_ROT"
            if [ "$GEN_LW" -lt 640 ]; then echo "APPLAUNCH_COMPAT_SCALE=1"; else echo "APPLAUNCH_COMPAT_SCALE=2"; fi
            echo "# panel size unknown (no --screen-size, no --panel-mm): the launcher's default density (large controls)"
            cat <<EOF
APPLAUNCH_TOUCH_DEV=auto
APPLAUNCH_TOUCH_ORIENT=buffer
EOF
            case "$bl" in
                "") ;;
                backlight_gpio) echo "APPLAUNCH_BACKLIGHT=gpio:/sys/class/backlight/backlight_gpio" ;;
                *) echo "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/$bl"; dim_lines 10 ;;
            esac
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
APPLAUNCH_GAUGE=max17048:auto:0x36
APPLAUNCH_HINT_BACKLIGHT=button
APPLAUNCH_HINT_SOUND=bluetooth:XWF-M18-M28-M38
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
            if [ -n "$bl" ]; then echo "APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/$bl"; dim_lines 10; fi
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
# The board's own sound card (not HDMI) in /proc/asound/cards: its line ("0 [PCH ]: HDA-Intel - HDA Intel PCH"), or
# nothing. With one, the user joins the audio group: the launcher reads and sets its volume with amixer when no sound
# server holds the card (Settings > Speaker, report 030).
local_sound_card() {
    [ -r "$SYSROOT/proc/asound/cards" ] || return 0
    grep -E '^ *[0-9]+ \[' "$SYSROOT/proc/asound/cards" | grep -iv hdmi | head -n 1 | sed 's/^ *//' || true
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
# vc4-kms-dsi-waveshare-800x480 and vc4-kms-dsi-7inch, and on the Pi 4 B with the Freenove 5" panel, and on the Pi 5 with the
# 7" Touch Display), else
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
# The generic profile is for an untested screen: what was assumed, and what to do
generic_note() {
    case "$BOARD" in generic-*) ;; *) return 0 ;; esac
    echo "generic profile: untested screen: please send tools/board-probe/probe-board.sh output so that a profile can be added."
    echo "  assumed: framebuffer $PANEL_FB (${GEN_NW}x${GEN_NH}), canvas ${GEN_LW}x${GEN_LH}, rotation $GEN_ROT, touch by capability,"
    echo "  $(profile_body | sed -n 's/^APPLAUNCH_BACKLIGHT=/backlight /p' | head -n 1 | grep . || echo 'no backlight control')"
    if [ -n "$PANEL_IN_USE" ]; then echo "  panel size $PANEL_IN_USE mm ($SCREEN_NOTE)"
    else echo "  no panel size (the launcher's default density, large controls): --screen-size INCHES sets it"; fi
    echo "  check the home grid, touch and rotation; edit /etc/applaunch/board.conf by hand if needed (README.md, \"My screen is not listed\")"
}
# The 4.3" / 5" / 7" 800x480 DSI panels look the same to software (same fbdev, same connector mode): the profile assumes
# one size. Said on every run that was not given a size (--screen-size / --panel-mm, or one recorded in install.conf).
size_guess_note() {
    case "$BOARD" in pi3a-wsdsi43|pi3a-wsdsi43-portrait|pi4b-dsi|pi5-dsi7) ;; *) return 0 ;; esac
    [ -z "$SCREEN_NOTE" ] && [ -n "$PANEL_IN_USE" ] || return 0
    case "$BOARD" in pi3a-wsdsi43*) sg_eg="5 or 7" ;; pi4b-dsi) sg_eg="4.3 or 7" ;; *) sg_eg="4.3 or 5" ;; esac
    echo "screen size 4.3\"/5\"/7\" cannot be detected: assuming $(profile_diag)\" ($PANEL_IN_USE mm); for another size run again with --screen-size INCHES (e.g. $sg_eg)"
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
    generic_note
    size_guess_note
}
# The 3.5" SPI panel's backlight in config.txt (pi3a-luckfox35): the parameters of the one dtoverlay=mipi-dbi-spi line,
# on that line and on the dtparam= lines after it, up to the next dtoverlay= line or [section] (a dtparam applies to the
# last overlay loaded). "lf_config state FILE" prints pwm (backlight-pwm, no backlight-gpio), gpio (one
# backlight-gpio=18, no backlight-pwm) or nothing (no such overlay, several, another pin, both, no file);
# "lf_config line FILE" the number of the line with backlight-gpio=18; "lf_config convert FILE" the file with
# backlight-gpio=18 taken off its line (a dtparam= line left empty goes) and dtparam=backlight-pwm on the next line (and, on a
# Pi 5, dtparam=backlight-pwm-chan=2 after it). A dtparam=backlight-pwm-chan= line is not a backlight-pwm parameter for the state.
lf_config() {
    { [ -f "$2" ] && [ -r "$2" ]; } || return 0
    awk -v mode="$1" -v chan="${LF_CHAN:-}" '
    { line[NR] = $0 }
    END {
        ov = 0; n = 0; bg = 0; nbg = 0; npwm = 0
        for (i = 1; i <= NR; i++) if (line[i] ~ /^dtoverlay=mipi-dbi-spi(,|$)/) { ov = i; n++ }
        if (n == 1) {
            for (i = ov; i <= NR; i++) {
                if (i > ov && line[i] ~ /^(dtoverlay=|\[)/) break
                if (i > ov && line[i] !~ /^dtparam=/) continue
                k = split(substr(line[i], index(line[i], "=") + 1), it, ",")
                for (j = (i == ov ? 2 : 1); j <= k; j++) {
                    if (it[j] ~ /^backlight-gpio=/) { nbg++; if (it[j] == "backlight-gpio=18") bg = i }
                    else if (it[j] ~ /^backlight-pwm(=(on|true|yes|1))?$/) npwm++
                }
            }
        }
        st = ""
        if (npwm > 0 && nbg == 0) st = "pwm"
        else if (npwm == 0 && nbg == 1 && bg > 0) st = "gpio"
        if (mode == "state") { if (st != "") print st; exit }
        if (mode == "line") { if (st == "gpio") print bg; exit }
        for (i = 1; i <= NR; i++) {
            if (st != "gpio" || i != bg) { print line[i]; continue }
            p = substr(line[i], 1, index(line[i], "=")); k = split(substr(line[i], length(p) + 1), it, ","); out = ""
            for (j = 1; j <= k; j++) if (it[j] != "backlight-gpio=18" && it[j] != "") out = out (out == "" ? "" : ",") it[j]
            if (out != "") print p out
            print "dtparam=backlight-pwm"
            if (chan != "") print "dtparam=backlight-pwm-chan=" chan
        }
    }' "$2"
}
# The 3.5" SPI panel's PWM backlight (pi3a-luckfox35): the stock mipi-dbi-spi overlay's own backlight-pwm parameter in
# place of its on/off backlight-gpio=18, a kernel pwm-backlight on PWM0 / GPIO18 with 16 levels at
# /sys/class/backlight/backlight_pwm; the profile names it as sysfs: (the 10 brightness steps, the idle dim, the start-up
# restore). Set up by default on a Zero 2 W (the Waveshare 3.5" RPi LCD (F), tested by hand on zero8), whose PWM0 has no
# other user; a Pi 3A+ keeps its on/off backlight (its PWM0 is the headphone jack's, which that parameter turns off).
# config.txt is changed only by a whole install on an appliance board without --no-boot-config that writes the profile
# too: with a hand-edited board.conf (kept) config.txt is kept as well, --force writes both. The profile names
# backlight_pwm when config.txt has it already (an earlier run or a hand edit, on any model) or this run sets it up.
LF_CFG=""      # pwm | gpio | "": config.txt's panel backlight (lf_config state)
LF_PWM=0       # 1: the profile names the PWM backlight
LF_SETUP=0     # 1: this run turns backlight-gpio=18 into dtparam=backlight-pwm in config.txt (then a reboot)
LF_NOTE=""     # why not, on a Zero 2 W
LF_WARN=""     # a Pi 5 whose config.txt has backlight-pwm but not its channel 2
SPI_BEGIN="# >>> cyberdeckzero spi panel"      # --spi-panel on a Raspberry Pi: the block install.sh adds to config.txt
SPI_END="# <<< cyberdeckzero spi panel"
SPI_RPI_MODE=""  # --spi-panel on a Raspberry Pi: block (ours there already), hand (a panel overlay not made by install.sh), new
if [ "$SPI_RPI" = 1 ]; then
    if grep -qxF "$SPI_BEGIN" "$BOOT/config.txt"; then SPI_RPI_MODE=block
    elif grep -q '^dtoverlay=mipi-dbi-spi\(,\|$\)' "$BOOT/config.txt"; then SPI_RPI_MODE=hand
    else SPI_RPI_MODE=new; fi
fi
if [ "$BOARD" = pi3a-luckfox35 ]; then
    LF_CFG=$(lf_config state "$BOOT/config.txt")
    if [ "$LF_CFG" = pwm ]; then
        LF_PWM=1
        if [ "$LF_CHAN" = 2 ] && ! grep -q '^dtparam=.*backlight-pwm-chan=' "$BOOT/config.txt"; then
            LF_WARN="Pi 5: config.txt has dtparam=backlight-pwm but no dtparam=backlight-pwm-chan=2: GPIO18 is RP1 PWM0 channel 2, the overlay's default channel 0 binds the wrong one (no working brightness); add the line after dtparam=backlight-pwm and reboot"
        fi
    elif [ "$SPI_RPI" = 1 ]; then
        # --spi-panel: this run writes the block (new), or keeps what is there; never converts a hand-made backlight
        if [ "$SPI_RPI_MODE" = new ]; then
            if lf_pwm_free; then LF_PWM=1; else LF_NOTE="on/off backlight (a PWM backlight needs a model whose PWM0 has no other user: the Pi 3A+'s is the headphone jack's)"; fi
        elif [ "$LF_CFG" = gpio ]; then
            LF_NOTE="the panel's overlay in config.txt is kept as it is (on/off backlight)"
        fi
    elif lf_pwm_free; then
        if [ "$LF_CFG" != gpio ]; then
            LF_NOTE="config.txt has no dtoverlay=mipi-dbi-spi with backlight-gpio=18 to change: left as it is (on/off backlight)"
        elif [ "$BOARD_ONLY" = 1 ]; then
            LF_NOTE="--board-only: config.txt left as it is (on/off backlight; a whole install sets up the PWM one)"
        elif [ "$SESSION" = desktop ]; then
            LF_NOTE="desktop board: config.txt left as it is (on/off backlight); install.sh --spi-panel luckfox35 changes config.txt for the PWM one (README.md)"
        elif [ "$BOOT_CONFIG" = 0 ]; then
            LF_NOTE="skipped (--no-boot-config): on/off backlight"
        elif [ "$(profile_state)" = edited ] && [ "$FORCE" = 0 ]; then
            LF_NOTE="board.conf was edited by hand: config.txt left as it is too (--force writes both)"
        else
            LF_PWM=1; LF_SETUP=1
        fi
    fi
fi
# The 3.5" SPI panel's backlight on a board that is not a Raspberry Pi (pi3a-luckfox35 with no config.txt, where LF_CFG and
# LF_PWM do not apply): LF_BL is a backlight device with brightness levels (levels_backlight: sysfs: in the profile, the dim
# level and the sysfs udev rule), "gpio" for the on/off backlight_gpio (the gpio: form and its rule), or "" (no backlight:
# no line, the launcher shows no brightness control). With --spi-panel it is what the overlay brings at the next boot.
# A backlight with levels: max_brightness above 1 (an on/off one has 1); backlight_pwm first, else the only one; none or
# several others: nothing
levels_backlight() {
    lb_n=0; lb_one=""; lb_pwm=0
    for d in "$SYSROOT"/sys/class/backlight/*; do
        [ -e "$d" ] || continue
        m=$(rd "$d/max_brightness" || true)
        case "$m" in ""|*[!0-9]*) continue ;; esac
        [ "$m" -gt 1 ] || continue
        lb_n=$((lb_n + 1)); lb_one=${d##*/}
        [ "$lb_one" != backlight_pwm ] || lb_pwm=1
    done
    if [ "$lb_pwm" = 1 ]; then echo backlight_pwm; elif [ "$lb_n" = 1 ]; then echo "$lb_one"; fi
}
LF_BL=""
if [ "$BOARD" = pi3a-luckfox35 ] && [ "$IS_RPI" = 0 ]; then
    if [ -n "$SPI_PANEL" ]; then
        if [ "$SOFT_PWM" = 1 ]; then LF_BL=backlight_pwm; else LF_BL=gpio; fi
    else
        LF_BL=$(levels_backlight)
        if [ -z "$LF_BL" ] && [ -d "$SYSROOT/sys/class/backlight/backlight_gpio" ]; then LF_BL=gpio; fi
    fi
fi
# The on/off GPIO backlight of non-PWM boards (the deck has the PWM one, see 90-backlight-unblank.rules)
GPIO_BL=0
if [ "$BOARD" != zero2w ] && [ -d "$SYSROOT/sys/class/backlight/backlight_gpio" ]; then GPIO_BL=1; fi
if [ "$LF_BL" = gpio ]; then GPIO_BL=1; fi
# --spi-panel on a Raspberry Pi with the on/off backlight (a Pi 3A+): the device does not exist before the reboot, its rule comes now
if [ "$SPI_RPI_MODE" = new ] && [ "$LF_PWM" = 0 ]; then GPIO_BL=1; fi
gpio_bl_rule() {
    cat <<'EOF'
# On/off GPIO backlight (boards without a PWM backlight): let the video group switch it.
# "change" too, so that "udevadm trigger" at install time applies it without a reboot.
SUBSYSTEM=="backlight", KERNEL=="backlight_gpio", ACTION=="add|change", RUN+="/bin/sh -c 'chgrp video /sys/class/backlight/%k/brightness /sys/class/backlight/%k/bl_power; chmod g+w /sys/class/backlight/%k/brightness /sys/class/backlight/%k/bl_power'"
EOF
}
# ---------------------------------------------------------------------------------------------------------------
# The screen's size (--screen-size INCHES, --panel-mm WxH, the question below): the panels of one family look the same to
# software (the Waveshare 4.3", the Freenove 5" and the official 7" DSI panels: same ft5x06 touch, same 10-0045 regulator,
# same 800x480 fbdev), and a screen that is not listed has no profile: the profile only holds one size. The size sets
# APPLAUNCH_PANEL_MM (the layout's density) and, for 5" on the DSI regulator's backlight, the idle dim level 20 as pi4b-dsi
# has it; the board name, framebuffer, rotation and the rest stay the profile's.
#   mm from the diagonal d and the landscape canvas (w, h): w * d * 25.4 / sqrt(w^2 + h^2) by h * d * 25.4 / sqrt(...),
#   each rounded to the nearest mm, except for the sizes the profiles already know (their values were measured or taken
#   from the panel's data sheet and differ by a mm or two from the formula): 4.3, 5 and 7 inch at 800x480 (94x56, 108x65,
#   154x86: the 7" official panel's pixels are not square) and 3.5 inch at 480x320 (79x49, the Luckfox overlay's size).
#   When the result is the profile's own size the profile is not touched (byte for byte).
# A size given before (install.conf: screen_size, panel_mm) is kept by a re-run without options. The deck (zero2w) and an
# unknown board have no profile: the options are refused there.
# The question (a terminal: stdin and stdout; a first install: no screen_size= in install.conf; no option given; a profile
# written; not a PC, whose size comes from its EDID; not --dry-run, --plan, --board-only) is never asked from scripts,
# Settings > Updates or CI. APPLAUNCH_INSTALL_ASSUME_TTY=1 is a test hook, honoured only together with SYSROOT.
# ---------------------------------------------------------------------------------------------------------------
INSTALL_CONF="$PROFILE_DIR/install.conf"
size_is() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a + 0 == b + 0) }'; }
# screen_mm D W H: "WxH" in mm of a D inch diagonal on a W by H canvas (landscape: the larger side first)
screen_mm() {
    awk -v d="$1" -v w="$2" -v h="$3" 'BEGIN {
        if (w < h) { t = w; w = h; h = t }
        k = sprintf("%gx%g@%g", w, h, d + 0)
        if (k == "800x480@4.3") print "94x56"
        else if (k == "800x480@5") print "108x65"
        else if (k == "800x480@7") print "154x86"
        else if (k == "480x320@3.5") print "79x49"
        else { n = sqrt(w * w + h * h); printf "%dx%d\n", int(d * 25.4 * w / n + 0.5), int(d * 25.4 * h / n + 0.5) }
    }'
}
# the diagonal of the profile's own panel, for the question
profile_diag() {
    case "$BOARD" in
        pi3a-wsdsi43|pi3a-wsdsi43-portrait) echo 4.3 ;;
        pi4b-dsi) echo 5 ;;
        pi5-dsi7) echo 7 ;;
        pi3a-luckfox35) echo 3.5 ;;
    esac
}
SIZE_KEY=0; SIZE_REC=""; MM_REC=""
if [ -f "$INSTALL_CONF" ]; then
    if grep -q '^screen_size=' "$INSTALL_CONF"; then SIZE_KEY=1; SIZE_REC=$(sed -n 's/^screen_size=//p' "$INSTALL_CONF" | tail -n 1); fi
    MM_REC=$(sed -n 's/^panel_mm=//p' "$INSTALL_CONF" | tail -n 1)
fi
SCREEN_OWN_MM=""; SCREEN_LOGICAL=""
case "$BOARD" in
    ""|zero2w)
        if [ -n "$SIZE_ARG$MM_ARG" ]; then
            echo "--screen-size and --panel-mm set the panel size of a board profile; this board (${BOARD:-unknown}) has no board profile." >&2
            if [ "$BOARD" = zero2w ]; then
                echo "The deck's 2.8\" panel size is built in. For another screen use --board generic (README.md, \"My screen is not listed\")," >&2
                echo "or set APPLAUNCH_PANEL_MM=WxH in a hand-edited /etc/applaunch/board.conf." >&2
            fi
            exit 2
        fi ;;
    *)
        if [ -z "$SIZE_ARG$MM_ARG" ]; then
            valid_size "$SIZE_REC" && SIZE_ARG=$SIZE_REC
            valid_mm "$MM_REC" && MM_ARG=$MM_REC
        fi
        base=$(profile_body_base)
        SCREEN_OWN_MM=$(printf '%s\n' "$base" | sed -n 's/^APPLAUNCH_PANEL_MM=//p' | head -n 1)
        SCREEN_LOGICAL=$(printf '%s\n' "$base" | sed -n 's/^APPLAUNCH_LOGICAL=//p' | head -n 1)
        if [ -z "$SIZE_ARG$MM_ARG" ] && [ "$SIZE_KEY" = 0 ] && [ "$IS_PC" = 0 ] && [ "$DRY" = 0 ] && [ "$BOARD_ONLY" = 0 ] \
                && { { [ "${APPLAUNCH_INSTALL_ASSUME_TTY:-0}" = 1 ] && [ -n "$SYSROOT" ]; } || { [ -t 0 ] && [ -t 1 ]; }; }; then
            own_diag=$(profile_diag)
            tries=0
            while [ "$tries" -lt 3 ]; do
                printf "What is the screen's diagonal in inches? (for example 2.8, 3.5, 4.3, 5, 7; Enter = %s) " "${own_diag:-unknown}"
                ans=""
                read -r ans || { echo; echo "no answer: the profile's own size stays"; break; }
                ans=${ans%\"}
                if [ -z "$ans" ]; then break; fi
                if valid_size "$ans"; then SIZE_ARG=$ans; break; fi
                tries=$((tries + 1))
                echo "\"$ans\" is not a diagonal in inches between 1.5 and 15."
                [ "$tries" -lt 3 ] || echo "no usable answer: the profile's own size stays (--screen-size INCHES sets it later)"
            done
        fi
        if [ -n "$MM_ARG" ]; then
            SCREEN_MM=$MM_ARG; SCREEN_NOTE="--panel-mm $MM_ARG"
        elif [ -n "$SIZE_ARG" ]; then
            case "$SCREEN_LOGICAL" in
                [0-9]*x[0-9]*) SCREEN_MM=$(screen_mm "$SIZE_ARG" "${SCREEN_LOGICAL%x*}" "${SCREEN_LOGICAL#*x}"); SCREEN_NOTE="--screen-size $SIZE_ARG" ;;
                *) echo "--screen-size: the canvas size of this screen is not known here: give --panel-mm WxH instead." >&2; exit 2 ;;
            esac
        fi
        if [ -n "$SIZE_ARG" ] && size_is "$SIZE_ARG" 5 \
                && printf '%s\n' "$base" | grep -qx 'APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/10-0045'; then
            own_dim=$(printf '%s\n' "$base" | sed -n 's/^APPLAUNCH_DIM_PERCENT=//p' | head -n 1)
            { [ -z "$own_dim" ] || [ "$own_dim" = 20 ]; } || SCREEN_DIM20=1
        fi
        if [ -n "$SCREEN_MM" ] && { [ "$SCREEN_MM" != "$SCREEN_OWN_MM" ] || [ "$SCREEN_DIM20" = 1 ]; }; then SCREEN_APPLY=1; fi ;;
esac
PANEL_IN_USE=${SCREEN_MM:-$SCREEN_OWN_MM}

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
SOUND_CARD=$(local_sound_card)

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
        ""|-*|*[!A-Za-z0-9._-]*) echo "unusable user name \"$1\" for the polkit rules and the desktop unit" >&2; exit 1 ;;
    esac
}
# how the board was chosen, for the board: line
board_how() {
    if [ "$SPI_IMPLIED" = 1 ]; then echo " (the profile of --spi-panel $SPI_PANEL)"
    elif [ -n "$BOARD_ARG" ]; then echo " (forced with --board)"; fi
}

# ---------------------------------------------------------------------------------------------------------
# --spi-panel (what and why: above the board detection): the bundle's files (payload/panels/<panel>/, or panels/<panel>/
# next to this script in the source tree), the dry run's plan and the set-up itself (spi_setup, a whole install only)
# ---------------------------------------------------------------------------------------------------------
SPI_OVL=spi35-st7796s
SPI_FW_SHA=17204e39cce35fba857ad2dff14243e1d3a958c4dac00283f8df9b7ad5147cc7
SPI_DTS=spi35-waveshare35f-gpio-backlight.dts
SPI_DIR=waveshare35f      # the bundle's folder of the panel's files (luckfox35 on a Raspberry Pi is the same panel family)
[ "$SOFT_PWM" = 0 ] || SPI_DTS=spi35-waveshare35f-softpwm.dts
# DKMS needs the compiler, make and the C library headers (the headers package builds its scripts at install) and patch
SPI_PKGS="dkms gcc make libc6-dev patch $SPI_HDR"
SPI_URL=""
[ -z "$SPI_TAG" ] || SPI_URL="https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain/drivers/gpu/drm/drm_mipi_dbi.c?h=$SPI_TAG"
SPI_ENV="$SYSROOT/boot/armbianEnv.txt"
SPI_DTBO="$SYSROOT/boot/overlay-user/$SPI_OVL.dtbo"
SPI_FW="$SYSROOT/lib/firmware/st7796s.bin"
SPI_CHANGED=0; SPI_REBOOT=0; SPI_DKMS_NEW=""; SPI_WORK=""
spi_src() {
    if [ -d "$HERE/payload/panels/$SPI_DIR" ]; then echo "$HERE/payload/panels/$SPI_DIR"; else echo "$HERE/panels/$SPI_DIR"; fi
}
# the panel's files in the bundle: nothing when they are all there and the init file is Waveshare's, else what is wrong
spi_payload_problem() {
    sp=$(spi_src)
    sp_need="st7796s.bin $SPI_DTS dkms-drm-mipi-dbi-chunk/Makefile dkms-drm-mipi-dbi-chunk/dkms.conf dkms-drm-mipi-dbi-chunk/chunk.patch"
    [ "$SOFT_PWM" = 0 ] || sp_need="$sp_need dkms-pwm-gpio/Makefile dkms-pwm-gpio/dkms.conf dkms-pwm-gpio/pwm-gpio.c"
    [ "$SPI_RPI" = 0 ] || sp_need="st7796s.bin"      # a Raspberry Pi: the init file only
    for f in $sp_need; do
        [ -f "$sp/$f" ] || { echo "the bundle has no panels/$SPI_DIR/$f (unpack the whole bundle)"; return 0; }
    done
    sp_sum=$(sha256sum < "$sp/st7796s.bin" | cut -d' ' -f1)
    [ "$sp_sum" = "$SPI_FW_SHA" ] || echo "panels/$SPI_DIR/st7796s.bin is not Waveshare's init file (sha256 $sp_sum, expected $SPI_FW_SHA)"
}
# true when the DKMS module NAME/VERSION (kernel module MODULE) is installed for the running kernel
spi_mod_installed() {
    ls "$SYSROOT/lib/modules/$SPI_KREL/updates/dkms/$3".ko* > /dev/null 2>&1 && return 0
    [ -z "$SYSROOT" ] && command -v dkms > /dev/null 2>&1 && dkms status "$1/$2" -k "$SPI_KREL" 2>/dev/null | grep -q ': installed'
}
# the user_overlays= list of armbianEnv.txt: its last such line (U-Boot's env import keeps the last one)
spi_env_overlays() { sed -n 's/^user_overlays=//p' "$1" | tail -n 1; }
spi_env_has() { case " $(spi_env_overlays "$1" | tr '\t' ' ') " in *" $SPI_OVL "*) return 0 ;; esac; return 1; }
# armbianEnv.txt with SPI_OVL added to its last user_overlays= line (the names there kept), or a new line at the end
spi_env_add() {
    awk -v n="$SPI_OVL" '{ line[NR] = $0; if ($0 ~ /^user_overlays=/) last = NR }
    END {
        for (i = 1; i <= NR; i++) {
            if (i == last) { v = substr(line[i], 15); sub(/[ \t]+$/, "", v); line[i] = "user_overlays=" (v == "" ? n : v " " n) }
            print line[i]
        }
        if (!last) print "user_overlays=" n
    }' "$1"
}
# the profile's framebuffer, and why that one
spi_fb_note() {
    if [ "$SPI_FB_PREDICTED" = 0 ]; then echo "$PANEL_FB (the panel's framebuffer, found: the panel works already)"
    elif [ "$PANEL_FB" = /dev/fb0 ]; then echo "/dev/fb0 (no framebuffer now: the panel will be the only one after the reboot)"
    else echo "$PANEL_FB (foreseen: the framebuffers there now, an HDMI monitor's at boot, come first; boot without that monitor and the panel is /dev/fb0: then run install.sh again, it finds the panel and updates the profile)"
    fi
}
spi_plan() {
    echo "SPI panel (--spi-panel $SPI_PANEL): the Waveshare 3.5\" RPi LCD (F), ST7796S 320x480 and Goodix GT911 touch, on the 40-pin header"
    sp_p=$(spi_payload_problem)
    [ -z "$sp_p" ] || echo "  WARNING: $sp_p: the install would stop here"
    [ -z "$SPI_PIN_NOTE" ] || echo "  $SPI_PIN_NOTE"
    echo "  kernel $SPI_KREL (upstream $SPI_TAG); packages, when missing: $SPI_PKGS (and the headers must be the running kernel's)"
    if [ -f "$SPI_FW" ] && [ "$(sha256sum < "$SPI_FW" | cut -d' ' -f1)" = "$SPI_FW_SHA" ]; then sp_s="already there"; else sp_s="installed"; fi
    echo "  /lib/firmware/st7796s.bin: the panel's init sequence (Waveshare's file, sha256 $(printf '%.8s' "$SPI_FW_SHA")...): $sp_s"
    echo "  /etc/modules-load.d/spi35.conf: panel-mipi-dbi$( [ "$SOFT_PWM" = 0 ] || echo '; /etc/modules-load.d/pwm-gpio.conf: pwm-gpio')"
    if [ "$SOFT_PWM" = 1 ]; then sp_b="10 brightness levels: software PWM on header pin 12 (DKMS module pwm-gpio), /sys/class/backlight/backlight_pwm"
    else sp_b="on/off: gpio-backlight on header pin 12, /sys/class/backlight/backlight_gpio (--soft-pwm-backlight: 10 levels)"; fi
    echo "  /boot/overlay-user/$SPI_OVL.dtbo: dtc -@ of $SPI_DTS (backlight $sp_b)"
    if spi_mod_installed drm-mipi-dbi-chunk 1.0 drm_mipi_dbi; then
        echo "  DKMS drm-mipi-dbi-chunk/1.0 (drm_mipi_dbi, SPI transfers of at most 64 KiB): installed for $SPI_KREL already: kept (no download)"
    else
        echo "  DKMS drm-mipi-dbi-chunk/1.0 (drm_mipi_dbi, SPI transfers of at most 64 KiB: the H618's DMA cannot reach RAM above"
        echo "    4 GB and swiotlb takes 256 KiB at most): drm_mipi_dbi.c of $SPI_TAG downloaded from"
        echo "    $SPI_URL"
        echo "    and chunk.patch applied, then built and installed for $SPI_KREL (DKMS rebuilds it for kernel updates of the"
        echo "    same $spi_mm series; for another series run install.sh --spi-panel again)"
    fi
    if [ "$SOFT_PWM" = 1 ]; then
        if spi_mod_installed pwm-gpio 1.0 pwm-gpio; then echo "  DKMS pwm-gpio/1.0 (software PWM): installed for $SPI_KREL already: kept"
        else echo "  DKMS pwm-gpio/1.0 (software PWM, Linux 6.18 drivers/pwm/pwm-gpio.c from the bundle): built and installed for $SPI_KREL"; fi
    fi
    if spi_env_has "$SPI_ENV"; then
        echo "  /boot/armbianEnv.txt: user_overlays already loads $SPI_OVL: left as it is"
    else
        echo "  /boot/armbianEnv.txt: user_overlays=$(spi_env_add "$SPI_ENV" | sed -n 's/^user_overlays=//p' | tail -n 1) (armbianEnv.txt.bak-$(date +%Y%m%d) first)"
    fi
    echo "  no mem= kernel argument (the DKMS module makes it unnecessary)"
    if grep -q '^extraargs=.*mem=' "$SPI_ENV" 2>/dev/null; then
        echo "  NOTE: armbianEnv.txt has a mem= kernel argument (extraargs): no longer needed with the module; left as it is (remove it by hand for the RAM)"
    fi
    echo "  profile: pi3a-luckfox35, APPLAUNCH_FB=$(spi_fb_note)"
    echo "  a failing step (packages, the headers of $SPI_KREL, the download, the patch, dtc, a DKMS build) stops the install"
    echo "  before any file of the panel is put in place; then: reboot needed (the launcher is not started before it)"
}
# A failure of the set-up: the DKMS modules this run added go again, nothing of the panel is in place
spi_fail() {
    echo "ERROR: --spi-panel $SPI_PANEL: $1" >&2
    for m in $SPI_DKMS_NEW; do
        dkms remove "$m" --all > /dev/null 2>&1 || true
        rm -rf "$SYSROOT/usr/src/${m%/*}-${m#*/}"
        echo "  DKMS module $m taken off again" >&2
    done
    [ -z "$SPI_WORK" ] || rm -rf "$SPI_WORK"
    echo "Nothing of the panel set-up was put in place (packages apt installed stay); the launcher was not installed." >&2
    echo "Fix the cause and run install.sh again." >&2
    exit 1
}
# spi_put SOURCE DEST: SOURCE in place as DEST (644) when it differs
spi_put() {
    if [ -f "$2" ] && cmp -s "$1" "$2"; then echo "${2#"$SYSROOT"}: already up to date"; return 0; fi
    mkdir -p "${2%/*}"
    install -m 644 "$1" "$2"
    echo "${2#"$SYSROOT"}: installed"
    SPI_CHANGED=1
}
# spi_dkms NAME VERSION SOURCE_DIR: the module's source in /usr/src/NAME-VERSION (an earlier one of that name removed first),
# added, built and installed for the running kernel; a failure undoes this run's DKMS modules and stops
spi_dkms() {
    if [ -d "$SYSROOT/var/lib/dkms/$1/$2" ]; then dkms remove "$1/$2" --all > /dev/null 2>&1 || true; fi
    rm -rf "$SYSROOT/usr/src/$1-$2"
    mkdir -p "$SYSROOT/usr/src/$1-$2"
    cp "$3"/* "$SYSROOT/usr/src/$1-$2/"
    SPI_DKMS_NEW="$SPI_DKMS_NEW $1/$2"
    echo "DKMS $1/$2: building for $SPI_KREL (a minute or more on a small board) ..."
    if dkms add "$1/$2" > "$SPI_WORK/dkms.log" 2>&1 && dkms build "$1/$2" -k "$SPI_KREL" >> "$SPI_WORK/dkms.log" 2>&1 \
            && dkms install "$1/$2" -k "$SPI_KREL" >> "$SPI_WORK/dkms.log" 2>&1; then
        echo "DKMS $1/$2: installed for $SPI_KREL (/usr/src/$1-$2; DKMS rebuilds it for kernel updates)"
        SPI_CHANGED=1
        return 0
    fi
    tail -n 15 "$SPI_WORK/dkms.log" | sed 's/^/  | /' >&2
    spi_fail "the DKMS module $1/$2 did not build or install for $SPI_KREL (full log: /var/lib/dkms/$1/$2/build/make.log)"
}
# The set-up itself (a whole install, after the launcher's packages: curl and dtc are there)
spi_setup() {
    SPI_WORK=$(mktemp -d)
    sp=$(spi_src)
    [ -z "$SPI_PIN_NOTE" ] || echo "$SPI_PIN_NOTE" >&2
    sp_missing=""
    for pkg in $SPI_PKGS; do dpkg -s "$pkg" > /dev/null 2>&1 || sp_missing="$sp_missing $pkg"; done
    if [ -n "$sp_missing" ]; then
        echo "packages:$sp_missing"
        apt-get update || spi_fail "apt-get update failed (no network?)"
        # shellcheck disable=SC2086
        apt-get install -y --no-install-recommends $sp_missing || spi_fail "apt could not install$sp_missing"
    else
        echo "packages: all there ($SPI_PKGS)"
    fi
    if [ ! -d "$SYSROOT/lib/modules/$SPI_KREL/build" ]; then
        spi_fail "no kernel headers for the running kernel $SPI_KREL (/lib/modules/$SPI_KREL/build): $SPI_HDR is another version's. Update the kernel (sudo apt update && sudo apt full-upgrade), reboot, and run install.sh again"
    fi
    dtc -@ -I dts -O dtb -o "$SPI_WORK/$SPI_OVL.dtbo" "$sp/$SPI_DTS" || spi_fail "dtc could not compile $SPI_DTS"
    [ -s "$SPI_WORK/$SPI_OVL.dtbo" ] || spi_fail "dtc wrote no overlay from $SPI_DTS"
    if spi_mod_installed drm-mipi-dbi-chunk 1.0 drm_mipi_dbi; then
        echo "DKMS drm-mipi-dbi-chunk/1.0: installed for $SPI_KREL already (no download)"
    else
        mkdir "$SPI_WORK/chunk"
        echo "drm_mipi_dbi.c of $SPI_TAG: $SPI_URL"
        curl -fsSL --retry 2 --max-time 120 -o "$SPI_WORK/chunk/drm_mipi_dbi.c" "$SPI_URL" \
            || spi_fail "the download of drm_mipi_dbi.c of $SPI_TAG failed ($SPI_URL): no network, or no such upstream tag"
        grep -q 'size_t max_chunk = spi_max_transfer_size(spi);' "$SPI_WORK/chunk/drm_mipi_dbi.c" \
            || spi_fail "the downloaded drm_mipi_dbi.c of $SPI_TAG has no 'max_chunk = spi_max_transfer_size(spi)' line: chunk.patch does not fit this kernel"
        if ! patch --batch --forward --no-backup-if-mismatch "$SPI_WORK/chunk/drm_mipi_dbi.c" \
                "$sp/dkms-drm-mipi-dbi-chunk/chunk.patch" > "$SPI_WORK/patch.log" 2>&1; then
            sed 's/^/  | /' "$SPI_WORK/patch.log" >&2
            spi_fail "chunk.patch does not apply to drm_mipi_dbi.c of $SPI_TAG"
        fi
        grep -q 'min_t(size_t, spi_max_transfer_size(spi), 65536)' "$SPI_WORK/chunk/drm_mipi_dbi.c" \
            || spi_fail "chunk.patch left no 64 KiB cap in drm_mipi_dbi.c of $SPI_TAG"
        rm -f "$SPI_WORK/chunk/"*.orig "$SPI_WORK/chunk/"*.rej
        cp "$sp/dkms-drm-mipi-dbi-chunk/Makefile" "$sp/dkms-drm-mipi-dbi-chunk/dkms.conf" "$SPI_WORK/chunk/"
        # this source fits this kernel series only: DKMS skips another one (the kernel's own module then loads)
        { echo "# install.sh: drm_mipi_dbi.c of $SPI_TAG with chunk.patch; built for the kernel series $spi_mm only"
          printf 'BUILD_EXCLUSIVE_KERNEL="^%s\\."\n' "$(printf '%s' "$spi_mm" | sed 's/\./\\./g')"; } >> "$SPI_WORK/chunk/dkms.conf"
        spi_dkms drm-mipi-dbi-chunk 1.0 "$SPI_WORK/chunk"
    fi
    if [ "$SOFT_PWM" = 1 ]; then
        if spi_mod_installed pwm-gpio 1.0 pwm-gpio; then echo "DKMS pwm-gpio/1.0: installed for $SPI_KREL already"
        else spi_dkms pwm-gpio 1.0 "$sp/dkms-pwm-gpio"; fi
    fi
    # everything that can fail went well: the files
    spi_put "$sp/st7796s.bin" "$SPI_FW"
    printf 'panel-mipi-dbi\n' > "$SPI_WORK/spi35.conf"
    spi_put "$SPI_WORK/spi35.conf" "$SYSROOT/etc/modules-load.d/spi35.conf"
    if [ "$SOFT_PWM" = 1 ]; then
        printf 'pwm-gpio\n' > "$SPI_WORK/pwm-gpio.conf"
        spi_put "$SPI_WORK/pwm-gpio.conf" "$SYSROOT/etc/modules-load.d/pwm-gpio.conf"
    fi
    spi_put "$SPI_WORK/$SPI_OVL.dtbo" "$SPI_DTBO"
    if spi_env_has "$SPI_ENV"; then
        echo "/boot/armbianEnv.txt: user_overlays already loads $SPI_OVL"
    else
        sp_bak="$SPI_ENV.bak-$(date +%Y%m%d)"
        [ -e "$sp_bak" ] || cp -p "$SPI_ENV" "$sp_bak"
        spi_env_add "$SPI_ENV" > "$SPI_ENV.applaunch-new"
        if spi_env_has "$SPI_ENV.applaunch-new"; then
            chmod 644 "$SPI_ENV.applaunch-new"
            mv "$SPI_ENV.applaunch-new" "$SPI_ENV"
            echo "/boot/armbianEnv.txt: user_overlays=$(spi_env_overlays "$SPI_ENV") (the earlier file is ${sp_bak##*/})"
            SPI_CHANGED=1
        else
            rm -f "$SPI_ENV.applaunch-new"
            spi_fail "/boot/armbianEnv.txt could not be changed (the files above are in place; add user_overlays=$SPI_OVL by hand)"
        fi
    fi
    if grep -q '^extraargs=.*mem=' "$SPI_ENV" 2>/dev/null; then
        echo "NOTE: armbianEnv.txt has a mem= kernel argument (extraargs): no longer needed with the DKMS module; left as it is"
        echo "      (remove it by hand to get the RAM back)"
    fi
    rm -rf "$SPI_WORK"; SPI_WORK=""
    if [ "$SPI_CHANGED" = 1 ]; then
        SPI_REBOOT=1
        echo "the panel comes with the overlay at the next boot: REBOOT NEEDED (the launcher is not started before it)"
    else
        echo "the panel's set-up was in place already: nothing changed"
    fi
}

# ---------------------------------------------------------------------------------------------------------
# --spi-panel on a Raspberry Pi (luckfox35, or waveshare35f: the same panel family, the Luckfox 3.5" RPi LCD (CTP) and the
# Waveshare 3.5" RPi LCD (F): ST7796S 320x480 on SPI0 CE0, DC GPIO22, RST GPIO27, backlight GPIO18, Goodix GT911 touch on
# I2C 0x5d). Done by hand first on a Pi 5 (Raspberry Pi OS trixie, kernel 6.18) and made repeatable here:
#   - /lib/firmware/st7796s.bin, the panel's init sequence (Waveshare's file, checked by its sha256; byte-identical to Luckfox's);
#   - a block in config.txt's [all] section between SPI_BEGIN and SPI_END (the markers are what uninstall.sh looks for: a
#     hand-made block is never touched): dtparam=spi=on, dtparam=i2c_arm=on (each only if config.txt has no such line),
#     dtoverlay=mipi-dbi-spi,spi0-0,speed=48000000 with its compatible / width / height / reset-gpio / dc-gpio parameters,
#     the backlight, dtoverlay=goodix,addr=0x5d (unless config.txt has a goodix overlay);
#   - the backlight by model: PWM (dtparam=backlight-pwm) on a Zero 2 W (PWM0 channel 0, the overlay's default) and on a Pi 5
#     (dtparam=backlight-pwm-chan=2: GPIO18 is RP1 PWM0 channel 2, the default channel 0 binds the wrong one); on/off
#     (backlight-gpio=18) elsewhere, a Pi 3A+'s PWM0 being the headphone jack's;
#   - the pi3a-luckfox35 profile (the rest of the install), on a desktop board too: the option is explicit.
# A config.txt that has the panel's mipi-dbi-spi overlay already (no marker: made by hand) is left as it is, as is one with
# the marked block (a second run adds nothing). config.txt.bak-applaunch-spipanel is made once, before the first change.
# No DKMS and no kernel checks: the kernel's drm_mipi_dbi and the Pi firmware's overlays are enough.
# ---------------------------------------------------------------------------------------------------------
SPI_RPI_BAK="$BOOT/config.txt.bak-applaunch-spipanel"
SPI_FW_ADDED=0       # 1: this run created /lib/firmware/st7796s.bin (recorded in install.conf for uninstall.sh)
# the lines of the block for this model and this config.txt (without the markers)
spi_rpi_lines() {
    sr_cfg="$BOOT/config.txt"
    sr_last=$(grep '^\[.*\][[:space:]]*$' "$sr_cfg" | tail -n 1 | tr -d '[:space:]' || true)
    if [ -n "$sr_last" ] && [ "$sr_last" != "[all]" ]; then echo "[all]"; fi
    grep -qx 'dtparam=spi=on' "$sr_cfg" || echo "dtparam=spi=on"
    grep -qx 'dtparam=i2c_arm=on' "$sr_cfg" || echo "dtparam=i2c_arm=on"
    echo "dtoverlay=mipi-dbi-spi,spi0-0,speed=48000000"
    printf '%s\n' 'dtparam=compatible=st7796s\0panel-mipi-dbi-spi'
    echo "dtparam=width=320,height=480,width-mm=49,height-mm=79"
    if lf_pwm_free; then
        echo "dtparam=reset-gpio=27,dc-gpio=22"
        echo "dtparam=backlight-pwm"
        [ -z "$LF_CHAN" ] || echo "dtparam=backlight-pwm-chan=$LF_CHAN"
    else
        echo "dtparam=reset-gpio=27,dc-gpio=22,backlight-gpio=18"
    fi
    grep -q '^dtoverlay=goodix\(,\|$\)' "$sr_cfg" || echo "dtoverlay=goodix,addr=0x5d"
}
spi_rpi_plan() {
    echo "SPI panel (--spi-panel $SPI_PANEL): the Luckfox 3.5\" RPi LCD (CTP) / Waveshare 3.5\" RPi LCD (F), ST7796S 320x480 and Goodix GT911 touch, on the 40-pin header"
    sp_p=$(spi_payload_problem)
    [ -z "$sp_p" ] || echo "  WARNING: $sp_p: the install would stop here"
    echo "  Raspberry Pi: ${MODEL:-unknown}; the panel is set up in ${BOOT#"$SYSROOT"}/config.txt, also on a desktop board (the option is explicit)"
    if [ -f "$SPI_FW" ] && [ "$(sha256sum < "$SPI_FW" | cut -d' ' -f1)" = "$SPI_FW_SHA" ]; then sp_s="already there"; else sp_s="installed"; fi
    echo "  /lib/firmware/st7796s.bin: the panel's init sequence (Waveshare's file, sha256 $(printf '%.8s' "$SPI_FW_SHA")...): $sp_s"
    case "$SPI_RPI_MODE" in
        block) echo "  config.txt: the block between \"$SPI_BEGIN\" and \"$SPI_END\" is there already: left as it is" ;;
        hand) echo "  config.txt: has a dtoverlay=mipi-dbi-spi line (not made by install.sh): left as it is, nothing added" ;;
        *)
            echo "  config.txt: appended to the [all] section (config.txt.bak-applaunch-spipanel first, once):"
            echo "    $SPI_BEGIN"
            spi_rpi_lines | sed 's/^/    /'
            echo "    $SPI_END" ;;
    esac
    if [ "$SPI_RPI_MODE" = new ]; then
        if lf_pwm_free; then echo "  backlight: PWM, 16 levels at /sys/class/backlight/backlight_pwm$( [ -z "$LF_CHAN" ] || echo " (Pi 5: GPIO18 is RP1 PWM0 channel $LF_CHAN)")"
        else echo "  backlight: on/off, /sys/class/backlight/backlight_gpio (this model's PWM0 is the headphone jack's)"; fi
    fi
    echo "  profile: pi3a-luckfox35, APPLAUNCH_FB=$(spi_fb_note)"
    echo "  then: reboot needed when config.txt or the init file changed (the launcher is started after it); ./uninstall.sh takes the block off again"
}
spi_rpi_setup() {
    sp=$(spi_src)
    cfg="$BOOT/config.txt"
    [ -e "$SPI_FW" ] || SPI_FW_ADDED=1
    spi_put "$sp/st7796s.bin" "$SPI_FW"
    case "$SPI_RPI_MODE" in
        block) echo "${cfg#"$SYSROOT"}: the block of --spi-panel is there already: left as it is" ;;
        hand) echo "${cfg#"$SYSROOT"}: has a dtoverlay=mipi-dbi-spi line that install.sh did not write: left as it is, nothing added" ;;
        *)
            [ -e "$SPI_RPI_BAK" ] || cp -p "$cfg" "$SPI_RPI_BAK"
            { cat "$cfg"
              # a file without a final newline: one before the block
              [ -z "$(tail -c 1 "$cfg")" ] || echo
              echo "$SPI_BEGIN"
              echo "# added by install.sh --spi-panel (README.md, \"Raspberry Pi: the 3.5\" SPI panel\"); ./uninstall.sh removes this block"
              spi_rpi_lines
              echo "$SPI_END"; } > "$cfg.applaunch-new"
            if grep -qxF "$SPI_BEGIN" "$cfg.applaunch-new" && grep -qxF "$SPI_END" "$cfg.applaunch-new" \
                    && [ "$(grep -c '^dtoverlay=mipi-dbi-spi' "$cfg.applaunch-new")" = 1 ]; then
                cat "$cfg.applaunch-new" > "$cfg"
                rm -f "$cfg.applaunch-new"
                echo "${cfg#"$SYSROOT"}: the panel's block appended to [all] (the original is ${SPI_RPI_BAK##*/})"
                SPI_CHANGED=1
            else
                rm -f "$cfg.applaunch-new"
                echo "ERROR: --spi-panel $SPI_PANEL: config.txt could not be changed (nothing written)" >&2
                exit 1
            fi ;;
    esac
    if [ "$SPI_CHANGED" = 1 ]; then
        SPI_REBOOT=1
        echo "the panel comes with its overlay at the next boot: REBOOT NEEDED (the launcher is not started before it)"
    else
        echo "the panel's set-up was in place already: nothing changed"
    fi
}

# ---------------------------------------------------------------------------------------------------------
# --network-manager: Settings > Wi-Fi needs NetworkManager, which Armbian and Ubuntu server images do not have (netplan
# with systemd-networkd). The console steps install.sh printed before (README.md, "Install on a PC (amd64)"), done here:
# network-manager and polkitd (apt, no recommends), /etc/netplan copied to /etc/netplan.bak-applaunch (once), the netplan
# file 90-applaunch-networkmanager.yaml (renderer NetworkManager, 600) and "netplan generate". No "netplan apply": the
# network switches at the next boot, where the netplan Wi-Fi entries come up under NetworkManager by themselves (tested on
# the Orange Pi Zero 2W, 2026-11-21); netplan's own Wi-Fi supplicant would hold the interface until then anyway. Done at
# the end of the install, once the launcher is in place. An ssh session over Wi-Fi drops at that reboot: when the default
# route is over Wi-Fi, a terminal is asked first and a run without one needs --yes.
# ---------------------------------------------------------------------------------------------------------
NM_YAML=90-applaunch-networkmanager.yaml
NM_SETUP=0; NM_NOTE=""; NM_IF=""; NM_WIFI=0; NM_REBOOT=0
nm_installed() {
    if [ -n "$SYSROOT" ]; then [ -x "$SYSROOT/usr/bin/nmcli" ]; else command -v nmcli > /dev/null 2>&1; fi
}
nm_renderer_set() { grep -qs 'renderer:[[:space:]]*"\{0,1\}NetworkManager' "$SYSROOT"/etc/netplan/*.yaml; }
nm_refuse() {
    echo "--network-manager: $1" >&2
    echo "Nothing was changed." >&2
    exit 1
}
if [ "$NM_OPT" = 1 ]; then
    if nm_installed && { [ ! -d "$SYSROOT/etc/netplan" ] || nm_renderer_set; }; then
        NM_NOTE="NetworkManager is installed and has the network already: --network-manager has nothing to do"
    elif [ ! -f "$SYSROOT/etc/debian_version" ]; then
        nm_refuse "for Debian and Ubuntu systems only (no /etc/debian_version)."
    elif [ ! -d "$SYSROOT/etc/netplan" ]; then
        nm_refuse "this system has no /etc/netplan: install.sh switches netplan systems only (Armbian, Ubuntu). By hand: sudo apt install network-manager, and let it manage the Wi-Fi interface (no ifupdown, networkd or wpa_supplicant of its own for it), then reboot."
    else
        NM_SETUP=1
    fi
    NM_IF=$(awk 'NR > 1 && $2 == "00000000" { print $1; exit }' "$SYSROOT/proc/net/route" 2>/dev/null || true)
    case "$NM_IF" in wl*) NM_WIFI=1 ;; esac
    if [ -n "$NM_IF" ] && [ -d "$SYSROOT/sys/class/net/$NM_IF/wireless" ]; then NM_WIFI=1; fi
fi
nm_plan() {
    if [ "$NM_SETUP" = 0 ]; then echo "NetworkManager (--network-manager): $NM_NOTE"; return 0; fi
    echo "NetworkManager (--network-manager), at the end of the install: apt install --no-install-recommends network-manager"
    echo "  polkitd (when missing; polkitd with the launcher's packages); /etc/netplan copied to /etc/netplan.bak-applaunch$( [ ! -e "$SYSROOT/etc/netplan.bak-applaunch" ] || echo ' (there already: kept)');"
    echo "  /etc/netplan/$NM_YAML: 'network: {version: 2, renderer: NetworkManager}' (600); netplan generate"
    echo "  (no netplan apply): NetworkManager has the network from the next boot, the netplan Wi-Fi entries come up under it; reboot needed"
    if [ "$NM_WIFI" = 1 ]; then
        echo "  the default route is over Wi-Fi ($NM_IF): an ssh session over it drops at that reboot; asked first on a terminal,"
        echo "  without a terminal only with --yes$( [ "$YES" = 0 ] || echo ' (given)')"
    fi
}
# The confirmation (a whole install, before anything changes): the default route over Wi-Fi and no --yes
nm_confirm() {
    { [ "$NM_SETUP" = 1 ] && [ "$NM_WIFI" = 1 ] && [ "$YES" = 0 ]; } || return 0
    if { [ "${APPLAUNCH_INSTALL_ASSUME_TTY:-0}" = 1 ] && [ -n "$SYSROOT" ]; } || { [ -t 0 ] && [ -t 1 ]; }; then
        echo "--network-manager: the network goes over Wi-Fi ($NM_IF). NetworkManager takes it over at the next boot: an ssh"
        echo "session over Wi-Fi drops then, and the Wi-Fi comes back under NetworkManager (the netplan entries) by itself."
        printf "Go on? [y/N] "
        nm_ans=""
        read -r nm_ans || nm_ans=""
        case "$nm_ans" in y|Y|yes|Yes|YES) ;; *) nm_refuse "not confirmed." ;; esac
    else
        nm_refuse "the default route is over Wi-Fi ($NM_IF) and this is not a terminal: add --yes to go ahead (an ssh session over Wi-Fi drops at the reboot that follows)."
    fi
}
nm_setup() {
    nm_missing=""
    for pkg in network-manager polkitd; do dpkg -s "$pkg" > /dev/null 2>&1 || nm_missing="$nm_missing $pkg"; done
    if [ -n "$nm_missing" ]; then
        # shellcheck disable=SC2086
        if ! { apt-get update && apt-get install -y --no-install-recommends $nm_missing; }; then
            echo "WARNING: apt could not install$nm_missing: the network set-up is left as it was (netplan untouched)" >&2
            return 0
        fi
        echo "installed:$nm_missing"
    else
        echo "network-manager and polkitd: there already"
    fi
    if [ -e "$SYSROOT/etc/netplan.bak-applaunch" ]; then
        echo "/etc/netplan.bak-applaunch: there already (kept)"
    else
        cp -a "$SYSROOT/etc/netplan" "$SYSROOT/etc/netplan.bak-applaunch"
        echo "/etc/netplan copied to /etc/netplan.bak-applaunch"
    fi
    printf 'network: {version: 2, renderer: NetworkManager}\n' > "$SYSROOT/etc/netplan/$NM_YAML.tmp"
    chmod 600 "$SYSROOT/etc/netplan/$NM_YAML.tmp"
    mv "$SYSROOT/etc/netplan/$NM_YAML.tmp" "$SYSROOT/etc/netplan/$NM_YAML"
    echo "/etc/netplan/$NM_YAML: renderer NetworkManager (600)"
    if netplan generate; then
        echo "netplan generate: done (the network switches at the next boot)"
    else
        echo "WARNING: netplan generate failed: check the files in /etc/netplan (the backup is /etc/netplan.bak-applaunch)" >&2
    fi
    NM_REBOOT=1
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
        echo "  - files of earlier bundles that this one does not ship (logo_lcd.png, unused images and placeholders, adb/): none found"
    fi
}

# ---------------------------------------------------------------------------------------------------------
# Switch between the two variants (README.md, "Switching between appliance and desktop"): a board installed as an
# appliance that has a display manager now gets the desktop variant, and the other way round, at the next install.
# The other variant's files are taken off first, so the launcher and the display manager never both start at boot;
# /etc/applaunch/board.conf, the user's settings, app sources and installed apps are kept. Looked up under $SYSROOT.
# ---------------------------------------------------------------------------------------------------------
# the appliance variant's files of the user whose home is $1 ($SYSROOT included), one per line, without $SYSROOT
appliance_files() {
    for f in "$1/.config/systemd/user/$USER_UNIT" "$1/.config/systemd/user/default.target.wants/$USER_UNIT" \
             "$SYSTEMD_DIR/$CONSOLE_UNIT" "$SYSTEMD_DIR/multi-user.target.wants/$CONSOLE_UNIT"; do
        if present "$f"; then echo "${f#"$SYSROOT"}"; fi
    done
}
# the desktop variant's files, one per line, without $SYSROOT
desktop_files() {
    for f in "$SYSTEMD_DIR/$DESKTOP_UNIT" "$DESKTOP_ENTRY" "$POLKIT_DIR/$DESKTOP_RULES"; do
        if present "$f"; then echo "${f#"$SYSROOT"}"; fi
    done
}
# SWITCH: to-desktop | to-appliance | "" (none); SWITCH_FOUND: the other variant's files. $1: the user's home.
plan_switch() {
    SWITCH=""; SWITCH_FOUND=""
    if [ "$SESSION" = desktop ] && [ "$DESKTOP_REFUSED" = 0 ]; then
        SWITCH_FOUND=$(appliance_files "$1")
        [ -z "$SWITCH_FOUND" ] || SWITCH=to-desktop
    elif [ "$SESSION" = appliance ]; then
        SWITCH_FOUND=$(desktop_files)
        [ -z "$SWITCH_FOUND" ] || SWITCH=to-appliance
    fi
}
# The dry run's account of the switch. $1: the user.
print_switch() {
    case "$SWITCH" in
        to-desktop)
            echo "switch from the appliance variant to the desktop variant (a display manager is there now; done before the"
            echo "desktop files are installed; /etc/applaunch/board.conf, the user's settings, app sources and installed apps are kept):"
            echo "  - user service $USER_UNIT of $1: stopped if it runs (the launcher on the panel quits now, the install says"
            echo "    so when it does), disabled and removed: the launcher and $DM_UNIT never both start at boot"
            echo "  - $CONSOLE_UNIT: disabled and stopped (the text console is bound again), removed: the desktop unit"
            echo "    frees the panel itself while the launcher runs"
            echo "  - kept as they are: lingering of $1, getty@tty1 (the desktop owns its own VT)"
            echo "  - the appliance files found:"
            printf '%s\n' "$SWITCH_FOUND" | sed 's/^/      /'
            echo "  - the desktop comes up at the next boot (sudo reboot), or at once with: sudo systemctl start $DM_UNIT"
            ;;
        to-appliance)
            echo "switch from the desktop variant to the appliance variant (no display manager any more; done before the"
            echo "appliance units are enabled; /etc/applaunch/board.conf, the user's settings, app sources and installed apps are kept):"
            echo "  - $DESKTOP_UNIT, the menu entry and the polkit rule: removed (a launcher started from the menu that"
            echo "    still runs is left running, not stopped: the appliance launcher then starts at the next boot)"
            echo "  - the desktop files found:"
            printf '%s\n' "$SWITCH_FOUND" | sed 's/^/      /'
            ;;
    esac
}

plan_profile
# --plan: the dry run for a program (cyberdeckzero-update, Settings > Updates, report 047 P2): one key=value per line and
# nothing else. The helper stops an update on a variant switch, a migration, an unknown board, desktop_refused=1 or a
# profile_action of replace or remove: those need this script run by hand.
if [ "$PLAN" = 1 ]; then
    TARGET_USER=${TARGET_USER:-${SUDO_USER:-$(id -un 2>/dev/null || echo USER)}}
    PLAN_HOME=$(getent passwd "$TARGET_USER" 2>/dev/null | cut -d: -f6 || true)
    PLAN_HOME="$SYSROOT${PLAN_HOME:-/home/$TARGET_USER}"
    plan_switch "$PLAN_HOME"
    MIGRATION=0
    if old_user_unit_present "$PLAN_HOME" || present "$SYSTEMD_DIR/$OLD_CONSOLE_UNIT" || present "$SYSTEMD_DIR/$OLD_DESKTOP_UNIT" \
            || present "$POLKIT_DIR/$OLD_DESKTOP_RULES" || present "$MENU_DIR/$OLD_DESKTOP_ENTRY_NAME"; then
        MIGRATION=1
    fi
    echo "plan=1"
    echo "glibc=$GLIBC"
    echo "machine=$MACHINE"
    echo "pc=$IS_PC"
    echo "board=$BOARD"
    echo "board_forced=$( [ -n "$BOARD_ARG" ] && echo 1 || echo 0 )"
    echo "screen_size=$SIZE_ARG"
    echo "panel_mm=$MM_ARG"
    echo "panel_in_use=$PANEL_IN_USE"
    echo "session=$SESSION"
    echo "desktop_refused=$DESKTOP_REFUSED"
    echo "switch=$SWITCH"
    echo "migration=$MIGRATION"
    echo "profile_action=$PROFILE_ACTION"
    echo "profile_state=$(profile_state)"
    echo "user=$TARGET_USER"
    echo "udisks_rule=$UDISKS_RULES"
    exit 0
fi
if [ "$DRY" = 1 ]; then
    echo "DRY RUN: nothing will be changed."
    echo "glibc: $GLIBC (ok)"
    echo "detected:"
    print_facts
    echo "board: ${BOARD:-unknown}$(board_how)"
    if [ -z "$BOARD" ]; then
        if [ -n "$BOARD_NOTE" ]; then echo "$BOARD_NOTE"; else echo "This board is not known to install.sh. Run tools/board-probe/probe-board.sh on it and send the report."; fi
        generic_hint
        if [ "$FORCE" = 1 ]; then echo "unknown board: using generic settings (--force: no board profile)."
        else echo "install.sh would stop here (--force installs without a profile)."; fi
    elif [ "$BOARD" != zero2w ]; then
        if [ -n "$SCREEN_NOTE" ]; then echo "screen: panel size $PANEL_IN_USE mm ($SCREEN_NOTE)"
        elif [ -n "$PANEL_IN_USE" ]; then echo "screen: panel size $PANEL_IN_USE mm (the profile's own; --screen-size INCHES or --panel-mm WxH sets another)"
        else echo "screen: panel size not set (the launcher's default density; --screen-size INCHES or --panel-mm WxH sets it)"; fi
    fi
    echo "session: $SESSION ($SESSION_WHY)"
    if [ "$SESSION" = desktop ]; then
        [ "$DESKTOP_REFUSED" = 0 ] || echo "The deck profile (zero2w) has no desktop variant: install.sh would stop here."
    fi
    [ -z "$DESKTOP_BOOT_WARN" ] || echo "WARNING: $DESKTOP_BOOT_WARN"
    echo "profile: $PROFILE_MSG"
    generic_note
    size_guess_note
    case "$PROFILE_ACTION" in write|replace) echo "--- $PROFILE"; profile_content ;; esac
    if [ "$GPIO_BL" = 1 ]; then echo "udev: install /etc/udev/rules.d/$GPIO_BL_RULE (video group may switch backlight_gpio)"; fi
    if [ -n "$SYSFS_BL" ]; then echo "udev: install /etc/udev/rules.d/$SYSFS_BL_RULE (video group may set the brightness of $SYSFS_BL)"; fi
    if [ "$BOARD_ONLY" = 0 ] && [ -n "$SOUND_CARD" ]; then echo "audio group: the user joins it (sound card: $SOUND_CARD)"; fi
    if [ "$BOARD_ONLY" = 0 ] && [ "$BOARD" = hackberry-cm5 ]; then echo "i2c group: the user joins it, if the system has one (battery gauge)"; fi
    if [ "$BOARD_ONLY" = 0 ] && [ "$WP_DROPIN" = 1 ]; then echo "wireplumber: ~/$WP_DROPIN_REL for the user (Bluetooth audio while the launcher runs; applies after the next login or \`systemctl --user restart wireplumber\`)"; fi
    if [ "$IS_PC" = 1 ]; then
        echo "boot config: none on a PC (GRUB and the kernel command line are left alone)"
    elif [ "$SESSION" = desktop ] && [ "$HP_PWM" = 1 ]; then
        echo "boot config: the PWM backlight overlay only (--pwm-backlight, below); cmdline.txt left alone on a desktop board"
    elif [ "$SESSION" = desktop ]; then
        echo "boot config: none on a desktop board (config.txt and cmdline.txt left alone$( [ "$SPI_RPI" = 0 ] || echo ' except the SPI panel block of --spi-panel, below'))"
    elif [ "$BOOT_FW" = 0 ] && [ "$BOOT_CONFIG" = 1 ]; then
        echo "boot configuration: skipped ($BOOT_FW_WHY); nothing changed"
        [ -z "$SPI_PANEL" ] || echo "  (config.txt and cmdline.txt only: the SPI panel's overlay goes into /boot/armbianEnv.txt, below)"
    elif [ "$DECK_BOOT" = 1 ]; then
        [ "$BOOT_CONFIG" = 1 ] && echo "boot config: PWM backlight overlay + cmdline cursor/consoleblank" || echo "boot config: skipped (--no-boot-config)"
    elif [ "$HP_PWM" = 1 ]; then
        echo "boot config: the HyperPixel's PWM backlight overlay (--pwm-backlight, below) + cmdline cursor/consoleblank"
    elif [ "$LF_SETUP" = 1 ]; then
        echo "boot config: the panel's PWM backlight in config.txt (below) + cmdline cursor/consoleblank"
    else
        [ "$BOOT_CONFIG" = 1 ] && echo "boot config: cmdline cursor/consoleblank only (the deck's PWM backlight overlay is not for this board)" || echo "boot config: skipped (--no-boot-config)"
    fi
    if [ "$HP_PWM" = 1 ]; then
        echo "PWM backlight (--pwm-backlight): /boot/firmware/overlays/$HP_DTBO built with dtc, the line $HP_LINE"
        echo "  after the vc4-kms-dpi-hyperpixel4 line of config.txt (config.txt.bak-applaunch first); then reboot"
    elif [ -n "$HP_PWM_NOTE" ]; then
        echo "$HP_PWM_NOTE"
    fi
    if [ "$LF_SETUP" = 1 ]; then
        echo "PWM backlight (3.5\" SPI panel): in config.txt, backlight-gpio=18 comes off line $(lf_config line "$BOOT/config.txt") and dtparam=backlight-pwm"
        echo "  goes after it (config.txt.bak-applaunch first; the mipi-dbi-spi overlay's PWM0 on GPIO18, 16 levels at"
        echo "  /sys/class/backlight/backlight_pwm in the profile); then reboot"
        [ -z "$LF_CHAN" ] || echo "  Pi 5: dtparam=backlight-pwm-chan=$LF_CHAN goes after it too (GPIO18 is RP1 PWM0 channel $LF_CHAN; the default channel 0 binds the wrong one)"
    elif [ "$LF_PWM" = 1 ] && [ "$SPI_RPI_MODE" != new ]; then
        echo "PWM backlight (3.5\" SPI panel): config.txt has it already (dtparam=backlight-pwm): left as it is"
    elif [ -n "$LF_NOTE" ]; then
        echo "PWM backlight (3.5\" SPI panel): $LF_NOTE"
    fi
    [ -z "$LF_WARN" ] || echo "WARNING: $LF_WARN"
    if [ "$SPI_RPI" = 1 ]; then spi_rpi_plan; elif [ -n "$SPI_PANEL" ]; then spi_plan; fi
    [ "$NM_OPT" = 0 ] || nm_plan
    if [ "$BOARD_ONLY" = 0 ]; then
        TARGET_USER=${TARGET_USER:-${SUDO_USER:-$(id -un 2>/dev/null || echo USER)}}
        DRY_HOME=$(getent passwd "$TARGET_USER" 2>/dev/null | cut -d: -f6 || true)
        print_migration "$TARGET_USER" "$SYSROOT${DRY_HOME:-/home/$TARGET_USER}"
        plan_switch "$SYSROOT${DRY_HOME:-/home/$TARGET_USER}"
        print_switch "$TARGET_USER"
        echo "launcher files: /usr/share/APPLaunch/bin/$LAUNCHER_BIN and $CONSOLE_HELPER, the libraries and assets of the"
        echo "  bundle; licences and notices in /usr/share/doc/$DOC_NAME (replaced as a whole)"
        echo "install options: /etc/applaunch/install.conf (the keyboard name, --board, --screen-size, --panel-mm, the boot configuration, the default"
        echo "  source: Settings > Updates runs install.sh again with them)"
        echo "anchor package: $ANCHOR_PKG (built here with dpkg-deb, no files; Depends: the packages of the launcher's"
        echo "  libraries and of the programs it runs on this board, so apt autoremove never takes them away)"
        echo "removable drives: /etc/polkit-1/rules.d/$UDISKS_RULES: $TARGET_USER (only) may mount removable drives through udisks"
        echo "  (udisksctl mount -b, org.freedesktop.udisks2.filesystem-mount and -mount-other-seat) without a password, also from"
        echo "  the launcher's background session; fixed disks, encryption and everything else stay as they are"
        if [ "$IS_PC" = 1 ] && pc_is_fzm1; then
            case "$(pk_state)" in
                absent) echo "power key (Panasonic FZ-M1): /etc/systemd/logind.conf.d/$POWERKEY_DROPIN will be written: a short press suspends"
                    echo "  (HandlePowerKey=suspend), a long press powers off (HandlePowerKeyLongPress=poweroff); logind reloads its settings"
                    echo "  (systemctl kill -s HUP systemd-logind, never a restart: ssh and desktop sessions stay)" ;;
                same) echo "power key (Panasonic FZ-M1): /etc/systemd/logind.conf.d/$POWERKEY_DROPIN: already there, with these settings: left as it is" ;;
                *) if [ "$FORCE" = 1 ]; then
                       echo "power key (Panasonic FZ-M1): /etc/systemd/logind.conf.d/$POWERKEY_DROPIN is there with other settings: replaced (--force), the old one kept as $POWERKEY_DROPIN.bak"
                   else
                       echo "power key (Panasonic FZ-M1): /etc/systemd/logind.conf.d/$POWERKEY_DROPIN is there with other settings: kept as it is (--force replaces it, the old one kept as $POWERKEY_DROPIN.bak)"
                   fi ;;
            esac
        fi
    fi
    if [ "$BOARD_ONLY" = 0 ] && [ "$SESSION" = desktop ]; then
        if [ "$IS_PC" = 1 ]; then
            echo "packages (PC, when missing): $PC_PKGS kbd (chvt, for the desktop unit); bluetooth.service enabled and"
            if [ "$NM_SETUP" = 1 ]; then echo "  started; network-manager with --network-manager (above)"
            else echo "  started; network-manager is not installed by install.sh"; fi
        fi
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
        if [ "$IS_PC" = 1 ]; then
            echo "packages (PC, when missing): $PC_PKGS; bluetooth.service enabled and started; network-manager is"
            if [ "$NM_SETUP" = 1 ]; then echo "  installed with --network-manager (above)"
            else echo "  not installed (it takes the network over: the end of the install prints a recipe for the console)"; fi
        fi
        echo "then: packages, launcher files, udev/polkit rules, services, user service (as for the deck): $USER_UNIT"
        if [ -n "$SPI_PANEL" ]; then echo "  for $TARGET_USER (lingering on), enabled; started at the reboot when the panel's set-up changed (else now)"
        else echo "  for $TARGET_USER (lingering on), enabled and started"; fi
        echo "console: $CONSOLE_UNIT frees the panel from the text console once the boot is done (the boot lines still show)"
    fi
    if [ "$BOARD_ONLY" = 0 ]; then
        if [ "$DEFAULT_SOURCE" = 1 ]; then
            echo "default app source: OSRdesign/cyberdeck-zero-apps added for the user once, only if they have no source yet, then synced (default-sources.sh)"
        else
            echo "default app source: skipped (--no-default-source)"
        fi
        echo "later: if you install or remove a desktop (a display manager), run install.sh again: it switches the variant"
    fi
    exit 0
fi

if [ "$BOARD_ONLY" = 1 ]; then
    echo "board: ${BOARD:-unknown}$(board_how)"
    if [ -z "$BOARD" ]; then
        echo "detected:"; print_facts
        if [ -n "$BOARD_NOTE" ]; then echo "$BOARD_NOTE"; else echo "This board is not known to install.sh. Run tools/board-probe/probe-board.sh on it and send the report."; fi
        generic_hint
    fi
    apply_profile
    if [ "$GPIO_BL" = 1 ]; then
        mkdir -p "$UDEV_DIR"
        gpio_bl_rule > "$UDEV_DIR/$GPIO_BL_RULE"; chmod 644 "$UDEV_DIR/$GPIO_BL_RULE"
        echo "installed $UDEV_DIR/$GPIO_BL_RULE"
        [ -n "$SYSROOT" ] || { udevadm control --reload; udevadm trigger --subsystem-match=backlight || true; }
    fi
    if [ -n "$SYSFS_BL" ]; then
        mkdir -p "$UDEV_DIR"
        sysfs_bl_rule > "$UDEV_DIR/$SYSFS_BL_RULE"; chmod 644 "$UDEV_DIR/$SYSFS_BL_RULE"
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

[ -d "$PAYLOAD" ] || { echo "payload/ not found next to install.sh (unpack the whole bundle)" >&2; exit 1; }
TARGET_USER=${TARGET_USER:-${SUDO_USER:-}}
[ -n "$TARGET_USER" ] && id "$TARGET_USER" >/dev/null 2>&1 || { echo "cannot determine the user: pass --user NAME" >&2; exit 1; }
TARGET_UID=$(id -u "$TARGET_USER")
TARGET_HOME=$(getent passwd "$TARGET_USER" | cut -d: -f6)
[ -n "$TARGET_HOME" ] || { echo "cannot find the home directory of $TARGET_USER" >&2; exit 1; }
check_user_name "$TARGET_USER"      # it goes into the polkit rule of removable drives (and the desktop unit)
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

# The switch itself (see plan_switch, print_switch): before the new variant's units are installed or enabled.
# Idempotent: once the other variant's files are gone, plan_switch finds nothing and this is not called again.
SWITCH_KEPT_RUNNING=0
switch_to_desktop() {
    if present "$UNIT_DIR/$USER_UNIT" || present "$UNIT_DIR/default.target.wants/$USER_UNIT"; then
        # said before it happens: the launcher on the panel quits now
        if as_user systemctl --user is-active --quiet "$USER_UNIT" 2>/dev/null; then
            echo "the launcher is running on the panel ($USER_UNIT): it is stopped now (the desktop variant starts it from the"
            echo "applications menu)"
        fi
        as_user systemctl --user disable --now "$USER_UNIT" \
            || echo "(the user's systemd did not answer: $USER_UNIT is not running; its files are removed)"
        rm -f "$UNIT_DIR/$USER_UNIT" "$UNIT_DIR/default.target.wants/$USER_UNIT"
        as_user systemctl --user daemon-reload 2>/dev/null || true
        as_user systemctl --user reset-failed "$USER_UNIT" 2>/dev/null || true
        echo "user service $USER_UNIT of $TARGET_USER: stopped, disabled and removed (it no longer starts at boot; lingering"
        echo "stays on, harmless: loginctl disable-linger $TARGET_USER turns it off)"
    fi
    if present "$SYSTEMD_DIR/$CONSOLE_UNIT" || present "$SYSTEMD_DIR/multi-user.target.wants/$CONSOLE_UNIT"; then
        # its ExecStop= binds the text console again; the desktop unit unbinds it while the launcher runs
        systemctl disable --now "$CONSOLE_UNIT" || true
        rm -f "$SYSTEMD_DIR/$CONSOLE_UNIT" "$SYSTEMD_DIR/multi-user.target.wants/$CONSOLE_UNIT"
        echo "$CONSOLE_UNIT disabled, stopped (the text console is bound again) and removed: the desktop unit frees the"
        echo "panel itself while the launcher runs"
    fi
    systemctl daemon-reload
    systemctl reset-failed "$CONSOLE_UNIT" 2>/dev/null || true
    echo "kept: ${PROFILE#"$SYSROOT"}, the user's settings, app sources and installed apps, lingering, getty@tty1 as it is"
}
switch_to_appliance() {
    if present "$SYSTEMD_DIR/$DESKTOP_UNIT"; then
        if systemctl is-active --quiet "$DESKTOP_UNIT"; then
            # not stopped: its ExecStopPost= would start the display manager again if its unit is still there
            SWITCH_KEPT_RUNNING=1
            echo "the launcher started from the menu ($DESKTOP_UNIT) is running: it is left running, not stopped; the"
            echo "appliance launcher starts at the next boot (reboot when the install is done, do not quit it to the desktop)"
        fi
        rm -f "$SYSTEMD_DIR/$DESKTOP_UNIT"
        echo "$DESKTOP_UNIT removed"
    fi
    for f in "$DESKTOP_ENTRY" "$POLKIT_DIR/$DESKTOP_RULES"; do
        if present "$f"; then rm -f "$f"; echo "removed ${f#"$SYSROOT"}"; fi
    done
    systemctl daemon-reload
    [ "$SWITCH_KEPT_RUNNING" = 1 ] || systemctl reset-failed "$DESKTOP_UNIT" 2>/dev/null || true
    echo "kept: ${PROFILE#"$SYSROOT"}, the user's settings, app sources and installed apps"
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

# The anchor package (report 047 P4, decision D13): cyberdeckzero-runtime, a package with no files (Architecture: all)
# built here with dpkg-deb and installed with dpkg. Its Depends are the packages the launcher needs on THIS board, found
# at install time: the packages this script installs (PKGS), ca-certificates (curl's https), the dpkg owners of the
# libraries the launcher's binaries load (ldd) and of the programs it runs. apt then never removes them (autoremove, from
# Settings > Updates or from a terminal), and a plan that would remove one also removes the anchor: "Remv
# cyberdeckzero-runtime" is the red flag (Settings > Updates refuses any removal). Never a version in Depends: no hold, no
# pin, security updates keep coming (report 047 section 7.4.2). Rebuilt only when its version or its Depends change
# (a second run is a no-op); uninstall.sh purges it. A board without dpkg-deb, or a failure here, only loses the
# protection: the launcher works as before.
ANCHOR_TOOLS="curl sudo systemctl systemd-run systemd-inhibit timedatectl nmcli bluetoothctl rfkill amixer pactl chvt dtc"
# the version of this bundle (build.sh writes VERSION next to install.sh) as a Debian version; 0 when unknown
anchor_version() {
    v=$(head -n 1 "$HERE/VERSION" 2>/dev/null | tr -d '\r') || v=""
    case "$v" in
        ""|*[!A-Za-z0-9.+~-]*) echo 0 ;;
        [0-9]*) echo "$v" ;;
        *) echo "0~$v" ;;
    esac
}
# paths on stdin -> the names of the packages that own them ("libc6:arm64: /usr/lib/..." and "a, b: /path" lines)
anchor_owners() {
    xargs -r dpkg-query -S 2>/dev/null | awk -F': /' 'NF > 1 && $1 !~ /^diversion/ {
        n = split($1, names, ", "); for (i = 1; i <= n; i++) { sub(/:.*/, "", names[i]); print names[i] } }'
}
# a path as given, as resolved, and with /lib/ as /usr/lib/ (dpkg records the merged-/usr paths on Debian 13)
anchor_paths() {
    while IFS= read -r p; do
        [ -n "$p" ] || continue
        r=$(readlink -f "$p" 2>/dev/null) || r=""
        for q in "$p" $r; do
            echo "$q"
            case "$q" in /lib/*|/bin/*|/sbin/*) echo "/usr$q" ;; esac
        done
    done
}
# the Depends of the anchor: sorted, unique, installed now ("ii"), joined with ", "
anchor_depends() {
    {
        for p in $PKGS ca-certificates; do echo "$p"; done
        for b in "$APP_ROOT/bin/$LAUNCHER_BIN" "$APP_ROOT/bin/M5CardputerZero-AppStore" "$APP_ROOT/bin/$CTL_BIN" \
                 "$APP_ROOT/lib/libapplaunch_vfb.so"; do
            [ -f "$b" ] || continue
            ldd "$b" 2>/dev/null | sed -n 's/^.*=> \(\/[^ ]*\).*$/\1/p'
        done | anchor_paths | sort -u | anchor_owners
        for t in $ANCHOR_TOOLS; do command -v "$t" 2>/dev/null || true; done | anchor_paths | sort -u | anchor_owners
    } | grep -E '^[a-z0-9][a-z0-9+.-]+$' | grep -vx "$ANCHOR_PKG" | LC_ALL=C sort -u \
      | xargs -r dpkg-query -W -f='${Package} ${db:Status-Abbrev}\n' 2>/dev/null | awk '$2 == "ii" { print $1 }' \
      | LC_ALL=C sort -u | awk 'BEGIN { ORS = "" } { print (NR > 1 ? ", " : "") $0 } END { if (NR) print "\n" }'
}
runtime_anchor() {
    if ! command -v dpkg-deb >/dev/null 2>&1 || ! command -v dpkg-query >/dev/null 2>&1; then
        echo "no dpkg-deb or dpkg-query: no anchor package (apt does not know which packages the launcher needs)"
        return 0
    fi
    deps=$(anchor_depends)
    if [ -z "$deps" ]; then
        echo "none of the launcher's packages was found installed: no anchor package"
        return 0
    fi
    ver=$(anchor_version)
    count=$(printf '%s\n' "$deps" | tr ',' '\n' | grep -c .)
    current=$(dpkg-query -W -f='${db:Status-Abbrev}|${Version}|${Depends}' "$ANCHOR_PKG" 2>/dev/null || true)
    if [ "$current" = "ii |$ver|$deps" ]; then
        echo "$ANCHOR_PKG $ver already up to date ($count packages kept for the launcher)"
        return 0
    fi
    work=$(mktemp -d) || return 1
    mkdir -p "$work/root/DEBIAN"
    {
        echo "Package: $ANCHOR_PKG"
        echo "Version: $ver"
        echo "Architecture: all"
        echo "Maintainer: CyberdeckZero Launcher install.sh <root@localhost>"
        echo "Section: misc"
        echo "Priority: optional"
        echo "Depends: $deps"
        echo "Description: packages the CyberdeckZero Launcher needs on this board"
        echo " Built on this board by the launcher's install.sh; it installs no file. Its Depends are"
        echo " the packages of the libraries the launcher loads and of the programs it runs, so apt"
        echo " never removes them. uninstall.sh removes it (the packages themselves stay)."
    } > "$work/root/DEBIAN/control"
    chmod 755 "$work/root" "$work/root/DEBIAN"
    chmod 644 "$work/root/DEBIAN/control"
    if dpkg-deb --root-owner-group --build "$work/root" "$work/$ANCHOR_PKG.deb" > "$work/log" 2>&1 \
            && dpkg -i "$work/$ANCHOR_PKG.deb" >> "$work/log" 2>&1; then
        echo "$ANCHOR_PKG $ver installed: $count packages kept for the launcher ($deps)"
    else
        echo "WARNING: the anchor package $ANCHOR_PKG could not be installed (apt does not know which packages the" >&2
        echo "         launcher needs; everything else works):" >&2
        tail -n 5 "$work/log" | sed 's/^/         /' >&2
        # never leave it half installed: apt would refuse every later operation until it is fixed
        case "$(dpkg-query -W -f='${db:Status-Abbrev}' "$ANCHOR_PKG" 2>/dev/null || true)" in
            ""|"un "|"ii ") ;;
            *) dpkg --remove "$ANCHOR_PKG" > /dev/null 2>&1 || true ;;
        esac
    fi
    rm -rf "$work"
    return 0
}

say "Checking the board"
echo "board: ${MODEL:-unknown} -> profile ${BOARD:-none (unknown board)}   user: $TARGET_USER   glibc: $GLIBC"
if [ -z "$BOARD" ]; then
    echo "detected:"; print_facts
    if [ -n "$BOARD_NOTE" ]; then echo "$BOARD_NOTE"; else echo "This board is not known to install.sh. Run tools/board-probe/probe-board.sh on it and send the report."; fi
    generic_hint
    [ "$FORCE" = 1 ] || { echo "stopping here (use --force to install anyway, without a board profile)" >&2; exit 1; }
    echo "unknown board: using generic settings (--force: no board profile)"
fi
if [ "$SESSION" = desktop ]; then
    echo "session: desktop ($SESSION_WHY): the launcher is started from the applications menu"
    if [ "$DESKTOP_REFUSED" = 1 ]; then
        echo "The deck profile (zero2w) has no desktop variant (its settings live in the user service). Nothing was changed." >&2
        exit 1
    fi
    check_user_name "$TARGET_USER"
else
    echo "session: appliance ($SESSION_WHY)"
fi
plan_switch "$TARGET_HOME"
case "$SWITCH" in
    to-desktop) echo "variant: switching from the appliance variant to the desktop variant (a display manager is there now)" ;;
    to-appliance) echo "variant: switching from the desktop variant to the appliance variant (no display manager any more)" ;;
esac
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
# The same for the switch to the desktop variant: it stops the appliance launcher (its Terminal app runs inside it)
if [ -z "$SYSROOT" ] && [ "$SWITCH" = to-desktop ] && grep -q -F "/$USER_UNIT" /proc/self/cgroup 2>/dev/null; then
    echo "this shell runs inside the launcher's service ($USER_UNIT, its Terminal app): the switch to the desktop variant" >&2
    echo "stops it, which would end the install half way. Run it from an ssh session or a terminal on the desktop." >&2
    echo "Nothing was changed." >&2
    exit 1
fi
# --spi-panel: the bundle's panel files first; --network-manager over Wi-Fi: asked (or --yes) before anything changes
if [ -n "$SPI_PANEL" ]; then
    SPI_PROBLEM=$(spi_payload_problem)
    [ -z "$SPI_PROBLEM" ] || spi_refuse "$SPI_PROBLEM."
fi
nm_confirm

say "Installing packages"
if [ "$IS_PC" = 1 ]; then
    PKGS=$PC_PKGS        # no dtc on a PC; polkitd and BlueZ, which the Pi images have already
    # the desktop unit's chvt (a minimal Ubuntu or Armbian image may lack kbd; Raspberry Pi OS desktop has it)
    if [ "$SESSION" = desktop ]; then PKGS="$PKGS kbd"; fi
else
    PKGS="libinput10 libxkbcommon0 libfreetype6 curl device-tree-compiler"
    # --network-manager: polkitd now (the polkit check below), network-manager at the end (it takes the network over)
    if [ "$NM_SETUP" = 1 ]; then PKGS="$PKGS polkitd"; fi
fi
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
if [ "$IS_PC" = 1 ]; then
    # BlueZ's daemon for Settings > Bluetooth (its package enables it on most distributions; an Armbian image may not)
    if systemctl enable --now bluetooth.service >/dev/null 2>&1; then
        echo "bluetooth.service enabled and started"
    else
        echo "WARNING: bluetooth.service could not be started: Settings > Bluetooth will not work (systemctl status bluetooth)" >&2
    fi
fi
# NetworkManager (Settings > Wi-Fi) and, on a Pi image without it, BlueZ: not installed here; the end of the install
# says what to do (NetworkManager takes the network over from netplan / networkd: on the console)
NEED_NM=0
command -v nmcli >/dev/null 2>&1 || NEED_NM=1
NEED_BLUEZ=0
command -v bluetoothctl >/dev/null 2>&1 || NEED_BLUEZ=1

# The SPI panel before the launcher's files: a failed step stops here, with nothing of the panel or the launcher in place
if [ "$SPI_RPI" = 1 ]; then
    say "SPI panel (--spi-panel $SPI_PANEL: 3.5\" RPi LCD (ST7796S, GT911), config.txt)"
    spi_rpi_setup
elif [ -n "$SPI_PANEL" ]; then
    say "SPI panel (--spi-panel $SPI_PANEL: Waveshare 3.5\" RPi LCD (F)$( [ "$SOFT_PWM" = 0 ] || echo ', software PWM backlight'))"
    spi_setup
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
# Settings > Updates' root helper (report 047 P2): only ever started by the launcher, through the sudo password and
# systemd-run; a bundle without it installs without (Settings > Updates then offers no install)
if [ -f "$PAYLOAD/bin/$UPDATE_HELPER" ]; then
    install -m 755 "$PAYLOAD/bin/$UPDATE_HELPER" "$APP_ROOT/bin/$UPDATE_HELPER"
fi
# the command-line client of launcher.sock (report 035), on the PATH of every user; a bundle without it installs without
if [ -f "$PAYLOAD/bin/$CTL_BIN" ]; then
    install -m 755 "$PAYLOAD/bin/$CTL_BIN" "$APP_ROOT/bin/$CTL_BIN"
    mkdir -p "$SYSROOT/usr/local/bin"
    ln -sfn "/usr/share/APPLaunch/bin/$CTL_BIN" "$SYSROOT/usr/local/bin/$CTL_BIN"
    echo "$CTL_BIN installed (/usr/local/bin/$CTL_BIN)"
fi
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

say "Runtime anchor package (apt keeps what the launcher needs)"
runtime_anchor || echo "WARNING: the anchor package step failed (everything else works)" >&2

say "Installing udev rules (keyboard: \"$KEYBOARD_NAME\")"
mkdir -p "$UDEV_DIR"
install -m 644 "$PAYLOAD/etc/90-backlight-unblank.rules" "$UDEV_DIR/90-backlight-unblank.rules"
install -m 644 "$PAYLOAD/etc/91-applaunch-vkbd.rules" "$UDEV_DIR/91-applaunch-vkbd.rules"
install -m 644 "$PAYLOAD/etc/93-applaunch-rfkill.rules" "$UDEV_DIR/93-applaunch-rfkill.rules"
cat > "$UDEV_DIR/99-bt-keyboard.rules" <<EOF
# Stable name for the Bluetooth keyboard (it gets a new event node each time it reconnects).
SUBSYSTEM=="input", KERNEL=="event*", ATTRS{name}=="$KEYBOARD_NAME", SYMLINK+="input/bt-keyboard"
EOF
chmod 644 "$UDEV_DIR/99-bt-keyboard.rules"
if [ "$GPIO_BL" = 1 ]; then
    gpio_bl_rule > "$UDEV_DIR/$GPIO_BL_RULE"
    chmod 644 "$UDEV_DIR/$GPIO_BL_RULE"
    echo "on/off GPIO backlight: /etc/udev/rules.d/$GPIO_BL_RULE"
fi
if [ -n "$SYSFS_BL" ]; then
    sysfs_bl_rule > "$UDEV_DIR/$SYSFS_BL_RULE"
    chmod 644 "$UDEV_DIR/$SYSFS_BL_RULE"
    echo "backlight levels ($SYSFS_BL): /etc/udev/rules.d/$SYSFS_BL_RULE"
fi
udevadm control --reload || echo "WARNING: udevadm could not reload the rules (they apply after the next boot)" >&2
udevadm trigger --subsystem-match=input --subsystem-match=misc --subsystem-match=backlight || true
# netdev (the polkit rule for NetworkManager) is a Debian group that other distributions may not have
if ! getent group netdev >/dev/null 2>&1; then groupadd -f netdev && echo "group netdev created (50-networkmanager-netdev.rules)"; fi
usermod -aG input,video,netdev "$TARGET_USER"
# BlueZ's D-Bus policy lets the bluetooth group (made by the bluez package) talk to it from a background session
if getent group bluetooth >/dev/null 2>&1; then usermod -aG bluetooth "$TARGET_USER"; fi
# A sound card of the board's own (not HDMI): the audio group opens /dev/snd for amixer when no sound server holds the
# card (Settings > Speaker, report 030)
if [ -n "$SOUND_CARD" ] && getent group audio >/dev/null 2>&1; then
    usermod -aG audio "$TARGET_USER"
    echo "audio group: $TARGET_USER (sound card: $SOUND_CARD)"
fi
# The HackBerry's battery gauge (APPLAUNCH_GAUGE in its profile) is read over /dev/i2c-N, which the i2c group opens
# (Raspberry Pi OS's udev rule); a user made by the image is in it already, a user added later is not
if [ "$BOARD" = hackberry-cm5 ] && getent group i2c >/dev/null 2>&1; then
    usermod -aG i2c "$TARGET_USER"
    echo "i2c group: $TARGET_USER (battery gauge)"
fi

say "Installing PolicyKit rules (Wi-Fi, time settings, reboot/shutdown from the launcher)"
mkdir -p "$POLKIT_DIR"
install -m 644 "$PAYLOAD/etc/50-networkmanager-netdev.rules" "$POLKIT_DIR/50-networkmanager-netdev.rules"
install -m 644 "$PAYLOAD/etc/51-launcher-time-power.rules" "$POLKIT_DIR/51-launcher-time-power.rules"
# removable drives: the user service is a background session, which polkit's allow_active does not cover; the user
# (not everybody) may mount removable media through udisks. Written whole and moved into place: a rerun, or another
# --user, rewrites the same file
render "$UDISKS_RULES" > "$POLKIT_DIR/$UDISKS_RULES.tmp"
chmod 644 "$POLKIT_DIR/$UDISKS_RULES.tmp"
mv "$POLKIT_DIR/$UDISKS_RULES.tmp" "$POLKIT_DIR/$UDISKS_RULES"
echo "udisks: /etc/polkit-1/rules.d/$UDISKS_RULES ($TARGET_USER may mount removable drives without a password)"
# Reboot, shutdown and the clock from Settings go through logind / timedated, which ask polkit: without polkitd they
# refuse the launcher's user service, and the rules above never apply
POLKIT_OK=0
if command -v pkaction >/dev/null 2>&1 && pkaction --action-id org.freedesktop.login1.reboot >/dev/null 2>&1; then
    POLKIT_OK=1
    echo "polkit: running (Settings > Reboot / Shut down and the date and time settings can work)"
    # 51-launcher-time-power.rules allows the sudo group only
    if ! id -nG "$TARGET_USER" 2>/dev/null | tr ' ' '\n' | grep -qx sudo; then
        echo "WARNING: $TARGET_USER is not in the sudo group: 51-launcher-time-power.rules (reboot, shutdown, clock from" >&2
        echo "         Settings) does not apply to it. Fix: sudo usermod -aG sudo $TARGET_USER" >&2
    fi
else
    echo "WARNING: polkit is not available (no polkitd, or it does not answer): Settings > Reboot, Shut down and the" >&2
    echo "         date and time settings will be refused. Install it: sudo apt install polkitd" >&2
fi
# The Panasonic FZ-M1's power key (logind drop-in). PK_CREATED=1 (recorded in install.conf) says this installer wrote the
# file, so that uninstall.sh removes only a file it made; a hand-made one is never removed, a hand edit never replaced
# without --force (the old file is then kept as .bak). logind is told to re-read its settings (SIGHUP, which is what
# "systemctl reload" does); never restarted: an ssh or desktop session must survive.
PK_CREATED=0
if [ -f "$INSTALL_CONF" ] && [ "$(sed -n 's/^powerkey_dropin=//p' "$INSTALL_CONF" | tail -n 1)" = created ]; then PK_CREATED=1; fi
if [ "$IS_PC" = 1 ] && pc_is_fzm1; then
    PK_FILE="$LOGIND_DIR/$POWERKEY_DROPIN"
    PK_WRITE=0
    case "$(pk_state)" in
        absent) PK_WRITE=1; PK_MSG="written" ;;
        same) echo "power key: /etc/systemd/logind.conf.d/$POWERKEY_DROPIN: already there (short press suspends, long press powers off): left as it is" ;;
        *) if [ "$FORCE" = 1 ]; then
               cp -p "$PK_FILE" "$PK_FILE.bak"
               PK_WRITE=1; PK_MSG="replaced (--force; the old one kept as $POWERKEY_DROPIN.bak)"
           else
               echo "power key: /etc/systemd/logind.conf.d/$POWERKEY_DROPIN is there with other settings: kept as it is (--force replaces it, the old one kept as $POWERKEY_DROPIN.bak)"
           fi ;;
    esac
    if [ "$PK_WRITE" = 1 ]; then
        mkdir -p "$LOGIND_DIR"
        pk_text > "$PK_FILE.tmp"
        chmod 644 "$PK_FILE.tmp"
        mv "$PK_FILE.tmp" "$PK_FILE"
        PK_CREATED=1
        echo "power key: /etc/systemd/logind.conf.d/$POWERKEY_DROPIN $PK_MSG (Panasonic FZ-M1: a short press suspends, a long press powers off)"
        if systemctl kill -s HUP systemd-logind >/dev/null 2>&1; then
            echo "power key: logind told to re-read its settings (not restarted)"
        else
            echo "WARNING: logind could not be told to re-read its settings: the power key changes at the next boot" >&2
        fi
    fi
fi
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
# Then the other variant's files, before this variant's units are installed or enabled (see plan_switch)
case "$SWITCH" in
    to-desktop) say "Switching from the appliance variant to the desktop variant"; switch_to_desktop ;;
    to-appliance) say "Switching from the desktop variant to the appliance variant"; switch_to_appliance ;;
esac

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
    if [ "$WP_DROPIN" = 1 ]; then
        # not restarted here: a running desktop session must not lose its sound
        WP_FILE="$TARGET_HOME/$WP_DROPIN_REL"
        WP_NEW_CONFIG=0; [ -d "$TARGET_HOME/.config" ] || WP_NEW_CONFIG=1
        if [ -f "$WP_FILE" ] && [ "$(cat "$WP_FILE")" = "$(wp_dropin_content)" ]; then
            WP_STATE="already up to date"
        else
            mkdir -p "${WP_FILE%/*}"
            wp_dropin_content > "$WP_FILE.tmp"
            chmod 644 "$WP_FILE.tmp"
            mv "$WP_FILE.tmp" "$WP_FILE"
            WP_STATE="written"
        fi
        chown -R "$TARGET_USER:$TARGET_USER" "$TARGET_HOME/.config/wireplumber"
        [ "$WP_NEW_CONFIG" = 0 ] || chown "$TARGET_USER:$TARGET_USER" "$TARGET_HOME/.config"
        echo "wireplumber: ~/$WP_DROPIN_REL of $TARGET_USER $WP_STATE (Bluetooth audio while the launcher runs; applies after the next login or \`systemctl --user restart wireplumber\`)"
    fi
    # (an appliance install's user service, which must not start a second launcher at boot under the desktop, is
    # gone already: switch_to_desktop)
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
    # 644: the bundle's copy may carry an executable bit (systemd then warns "is marked executable")
    install -m 644 "$PAYLOAD/etc/$USER_UNIT" "$UNIT_DIR/$USER_UNIT"
    chown -R "$TARGET_USER:$TARGET_USER" "$TARGET_HOME/.config"
    loginctl enable-linger "$TARGET_USER"
fi

say "Board profile"
apply_profile
[ -z "$LF_WARN" ] || echo "WARNING: $LF_WARN" >&2

# The options of this run, for Settings > Updates (report 047 P2): its helper runs a newer release's install.sh with
# them, so the keyboard link, a forced board profile (the PDA one is never detected) and the boot configuration stay as
# they are. Rewritten by every run, never by --dry-run or --board-only.
say "Install options"
INSTALL_CONF="$PROFILE_DIR/install.conf"
mkdir -p "$PROFILE_DIR"
PREV_SPI=""; PREV_SOFT=""; PREV_FW=0
if [ -f "$INSTALL_CONF" ]; then
    PREV_SPI=$(sed -n 's/^spi_panel=//p' "$INSTALL_CONF" | tail -n 1)
    PREV_SOFT=$(sed -n 's/^soft_pwm_backlight=//p' "$INSTALL_CONF" | tail -n 1)
    PREV_FW=$(sed -n 's/^spi_fw_added=//p' "$INSTALL_CONF" | tail -n 1)
    case "$PREV_SPI" in waveshare35f|luckfox35) ;; *) PREV_SPI="" ;; esac
    case "$PREV_FW" in 1) ;; *) PREV_FW=0 ;; esac
    case "$PREV_SOFT" in 1) ;; *) PREV_SOFT=0 ;; esac
fi
{
    echo "# The options of the last install.sh run, written by install.sh. Settings > Updates runs install.sh of a newer"
    echo "# release again with them (cyberdeckzero-update). board= is empty when the board was detected; screen_size= (inches)
# and panel_mm= (WxH) when no screen size was given (--screen-size, --panel-mm, the install question): the profile's own."
    echo "keyboard_name=$KEYBOARD_NAME"
    echo "board=${BOARD_ARG:-$( [ "$GEN_AUTO" = 1 ] && echo generic)}"
    echo "screen_size=$SIZE_ARG"
    echo "panel_mm=$MM_ARG"
    echo "boot_config=$BOOT_CONFIG"
    echo "pwm_backlight=$PWM_BACKLIGHT"
    echo "default_source=$DEFAULT_SOURCE"
    echo "user=$TARGET_USER"
    # the FZ-M1's power-key drop-in, when this installer wrote it (uninstall.sh removes only such a file)
    if [ "${PK_CREATED:-0}" = 1 ]; then echo "powerkey_dropin=created"; fi
    # --spi-panel is done once (its files stay; DKMS rebuilds its modules): recorded for information and kept by later runs,
    # never run again by Settings > Updates, which keeps the profile through board= above
    if [ -n "$SPI_PANEL" ]; then
        echo "spi_panel=$SPI_PANEL"
        echo "soft_pwm_backlight=$SOFT_PWM"
        # a Raspberry Pi: the init file was put there by this install (uninstall.sh removes it only then, and unchanged)
        if [ "$SPI_RPI" = 1 ] && { [ "$SPI_FW_ADDED" = 1 ] || [ "$PREV_FW" = 1 ]; }; then echo "spi_fw_added=1"; fi
    elif [ -n "$PREV_SPI" ]; then
        echo "spi_panel=$PREV_SPI"
        echo "soft_pwm_backlight=${PREV_SOFT:-0}"
        [ "$PREV_FW" = 0 ] || echo "spi_fw_added=1"
    fi
} > "$INSTALL_CONF.tmp"
chmod 644 "$INSTALL_CONF.tmp"
mv "$INSTALL_CONF.tmp" "$INSTALL_CONF"
echo "${INSTALL_CONF#"$SYSROOT"}: keyboard \"$KEYBOARD_NAME\"${BOARD_ARG:+, --board $BOARD_ARG}${SIZE_ARG:+, --screen-size $SIZE_ARG}${MM_ARG:+, --panel-mm $MM_ARG}$( [ "$BOOT_CONFIG" = 0 ] && echo ', --no-boot-config')$( [ "$PWM_BACKLIGHT" = 1 ] && echo ', --pwm-backlight')$( [ "$DEFAULT_SOURCE" = 0 ] && echo ', --no-default-source')${SPI_PANEL:+ (spi_panel=$SPI_PANEL recorded for information)}"

# The PWM backlight overlay is for the deck's Waveshare panel (DECK_BOOT, see the board detection); a board with its
# own profile keeps its config.txt and only gets the cmdline arguments (never an fbcon= argument).
# A desktop board keeps its boot configuration as it is (its desktop owns the console settings).
if [ "$IS_PC" = 1 ]; then
    say "Boot configuration"
    echo "PC: GRUB and the kernel command line are left as they are (the boot lines show, as on the deck)"
elif [ "$SESSION" = desktop ]; then
    say "Boot configuration"
    echo "desktop board: config.txt and cmdline.txt left as they are$( [ "$SPI_RPI" = 0 ] || echo ' (except the SPI panel block of --spi-panel, above)')"
elif [ "$BOOT_CONFIG" = 1 ] && [ "$BOOT_FW" = 1 ] && { [ ! -f "$BOOT/config.txt" ] || [ ! -f "$BOOT/cmdline.txt" ]; }; then
    say "Boot configuration"
    echo "boot configuration: skipped (${BOOT#"$SYSROOT"}/config.txt or cmdline.txt not found); nothing changed"
elif [ "$BOOT_CONFIG" = 1 ] && [ "$BOOT_FW" = 0 ]; then
    say "Boot configuration"
    echo "boot configuration: skipped ($BOOT_FW_WHY); nothing changed"
    [ -z "$SPI_PANEL" ] || echo "(the SPI panel's overlay in /boot/armbianEnv.txt: the SPI panel step above)"
elif [ "$BOOT_CONFIG" = 1 ]; then
    say "Boot configuration ($( { [ "$DECK_BOOT" = 1 ] || [ "$LF_SETUP" = 1 ]; } && echo 'PWM backlight, ' )hidden console cursor)"
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
    elif [ "$LF_SETUP" = 1 ]; then
        # the 3.5" SPI panel's PWM backlight (LF_SETUP): the profile written above names backlight_pwm already
        [ -f "$CONFIG.bak-applaunch" ] || cp "$CONFIG" "$CONFIG.bak-applaunch"
        LF_LINE=$(lf_config line "$CONFIG")
        if lf_config convert "$CONFIG" > "$CONFIG.applaunch-new" && [ "$(lf_config state "$CONFIG.applaunch-new")" = pwm ]; then
            mv "$CONFIG.applaunch-new" "$CONFIG"
            echo "config.txt: backlight-gpio=18 taken off line $LF_LINE, dtparam=backlight-pwm$( [ -z "$LF_CHAN" ] || echo " and dtparam=backlight-pwm-chan=$LF_CHAN") added after it (the original is"
            echo "config.txt.bak-applaunch): the panel's PWM backlight with 16 levels from the next boot. REBOOT for it: until"
            echo "then Settings > Screen > Brightness cannot set the panel"
            REBOOT=1
        else
            rm -f "$CONFIG.applaunch-new"
            echo "WARNING: config.txt could not be changed, but board.conf names the PWM backlight: run install.sh again" >&2
        fi
    elif [ "$LF_PWM" = 1 ] && [ "$SPI_RPI_MODE" = new ]; then
        echo "$BOARD: the panel's PWM backlight was set up by --spi-panel (SPI panel step above)"
    elif [ "$LF_PWM" = 1 ]; then
        echo "$BOARD: config.txt already has the panel's PWM backlight (dtparam=backlight-pwm): left as it is"
    elif [ -n "$LF_NOTE" ]; then
        echo "$BOARD: PWM backlight: $LF_NOTE"
    elif [ "$HP_PWM" = 0 ]; then
        echo "${BOARD:-this board}: config.txt left as it is (the PWM backlight overlay is for the deck's Waveshare panel)"
    fi
    for arg in vt.global_cursor_default=0 consoleblank=0; do
        if ! grep -q "$arg" "$CMDLINE"; then
            sed -i "1s|\$| $arg|" "$CMDLINE"
            REBOOT=1
        fi
    done
fi
# Pi 5 + HyperPixel 4, only with --pwm-backlight (HP_PWM, see the board detection): the overlay turns the HyperPixel
# overlay's gpio-backlight node into a pwm-backlight (RP1 PWM0 channel 3 on GPIO19), so its line must come after
# the HyperPixel one. The board profile needs no line: the launcher finds "backlight" with levels by itself (report 030),
# and Raspberry Pi OS's 60-backlight.rules lets the video group set its brightness.
if [ "$HP_PWM" = 1 ]; then
    say "PWM backlight (Pi 5 + HyperPixel 4, --pwm-backlight)"
    CONFIG="$BOOT/config.txt"
    if ! grep -qE '^dtoverlay=vc4-kms-dpi-hyperpixel4(,|$)' "$CONFIG" 2>/dev/null; then
        echo "WARNING: no dtoverlay=vc4-kms-dpi-hyperpixel4 line in $CONFIG: PWM backlight not set up (it changes the backlight that overlay adds)"
    else
        [ -f "$CONFIG.bak-applaunch" ] || cp "$CONFIG" "$CONFIG.bak-applaunch"
        mkdir -p "$BOOT/overlays"
        dtc -@ -I dts -O dtb -o "$BOOT/overlays/$HP_DTBO" "$PAYLOAD/etc/$HP_DTS"
        if grep -qxF "$HP_LINE" "$CONFIG"; then
            echo "config.txt already loads it ($HP_LINE); overlay rebuilt"
        else
            LAST=$(grep -nE '^dtoverlay=vc4-kms-dpi-hyperpixel4(,|$)' "$CONFIG" | tail -1 | cut -d: -f1)
            sed -i "${LAST}a $HP_LINE" "$CONFIG"
            echo "config.txt: $HP_LINE added after line $LAST (the original is config.txt.bak-applaunch)"
            REBOOT=1
        fi
    fi
elif [ -n "$HP_PWM_NOTE" ]; then
    say "PWM backlight"
    echo "$HP_PWM_NOTE"
fi

# Settings > Apps lists the apps of the user's sources: offer the project's apps repo once, as the user (their own
# file, no password), only if they have no source yet; a list of their own is never changed (default-sources.sh).
say "Default app source (Settings > Apps)"
if [ "$DEFAULT_SOURCE" = 0 ]; then
    echo "skipped (--no-default-source)"
elif [ ! -f "$HERE/default-sources.sh" ]; then
    echo "default-sources.sh is not next to install.sh: skipped (add OSRdesign/cyberdeck-zero-apps in Settings > Apps > Sources)"
else
    # The user cannot always read $HERE: the update helper unpacks the bundle in /var/cache/cyberdeckzero-update (root-only,
    # 0700), a bundle can sit in /root. So root copies the script where the user can read it, runs it, removes it.
    DS_DIR=$(mktemp -d) && chmod 755 "$DS_DIR" && cp "$HERE/default-sources.sh" "$DS_DIR/default-sources.sh" \
        && chmod 644 "$DS_DIR/default-sources.sh" \
        && runuser -u "$TARGET_USER" -- env HOME="$TARGET_HOME" sh "$DS_DIR/default-sources.sh" </dev/null \
        || echo "(the default app source could not be set up: add OSRdesign/cyberdeck-zero-apps in Settings > Apps > Sources)"
    [ -z "${DS_DIR:-}" ] || rm -rf "$DS_DIR"
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
    elif [ "$SWITCH_KEPT_RUNNING" = 1 ]; then
        echo "not started now: the launcher started from the menu ($DESKTOP_UNIT) still runs; $USER_UNIT starts at the"
        echo "next boot: sudo reboot"
    elif [ "$SPI_REBOOT" = 1 ]; then
        echo "not started now: the SPI panel comes with its overlay at the next boot; $USER_UNIT starts then: sudo reboot"
    else
        as_user systemctl --user restart "$USER_UNIT" || echo "(could not start it now; it starts at the next boot)"
    fi
fi

# --network-manager last: the launcher is in place whatever happens to the network
if [ "$NM_SETUP" = 1 ]; then
    say "NetworkManager (--network-manager)"
    nm_setup
fi

say "Done"
echo "session: $SESSION ($SESSION_WHY)"
case "$SWITCH" in
    to-desktop) echo "Switched from the appliance variant: the launcher no longer starts at boot, $DM_UNIT does." ;;
    to-appliance) echo "Switched from the desktop variant: the menu entry is gone, the launcher starts at boot." ;;
esac
if [ "$SESSION" = desktop ]; then
    if [ "$SWITCH" = to-desktop ] && ! systemctl is-active --quiet "$DM_UNIT"; then
        echo "The desktop comes up at the next boot (sudo reboot), or at once with: sudo systemctl start $DM_UNIT"
    fi
    echo "Start the launcher from the desktop: applications menu > CyberdeckZero Launcher (or: systemctl start $DESKTOP_UNIT)."
    echo "The desktop stops while it runs (its open programs are closed); Settings > System > Quit to desktop brings it back."
fi
if [ -n "$DESKTOP_BOOT_WARN" ]; then
    echo "WARNING: $DESKTOP_BOOT_WARN"
fi
echo "If you install or remove a desktop (a display manager) later, run install.sh again: it switches between the"
echo "appliance variant (the launcher at boot) and the desktop variant (the launcher from the applications menu)."
if [ "$POLKIT_OK" = 0 ]; then
    echo "WARNING: no polkit: Settings > Reboot / Shut down and the clock settings will not work. Fix: sudo apt install polkitd"
fi
if [ "$NEED_BLUEZ" = 1 ]; then
    echo "NOTICE: bluez is not installed: Settings > Bluetooth will not work. Fix: sudo apt install bluez"
fi
if [ "$NEED_NM" = 1 ] && [ "$NM_REBOOT" = 0 ]; then
    echo "NOTICE: network-manager is not installed: Settings > Wi-Fi needs NetworkManager. install.sh does not install it"
    echo "        without --network-manager (it does these steps, README.md, \"NetworkManager by install.sh\"),"
    echo "        because it takes the network over (from netplan / systemd-networkd here). Do it on the console, not"
    echo "        from an ssh session over Wi-Fi (the link drops while it switches):"
    if [ -d "$SYSROOT/etc/netplan" ]; then
        cat <<'EOF'
          sudo apt install network-manager
          sudo cp -a /etc/netplan /etc/netplan.bak-applaunch
          echo 'network: {version: 2, renderer: NetworkManager}' | sudo tee /etc/netplan/90-applaunch-networkmanager.yaml
          sudo chmod 600 /etc/netplan/90-applaunch-networkmanager.yaml
          sudo netplan apply
          sudo reboot
        The reboot is needed: netplan's own Wi-Fi supplicant (netplan-wpa-<interface>.service, a wpa_supplicant
        on /run/netplan/wpa-<interface>.conf) keeps running after "netplan apply" and holds the interface, so
        NetworkManager cannot take it ("wpa_supplicant couldn't grab this interface", the device "unavailable",
        Settings > Wi-Fi "Wi-Fi service unavailable"). Without a reboot, on the console:
          sudo systemctl stop netplan-wpa-<interface>.service
        An ssh session over Wi-Fi drops either way. Check after the reboot:
          nmcli -t device status        (the Wi-Fi line ends in ":connected", not ":unavailable")
        then Settings > Wi-Fi. Roll back (on the console, then reboot as well):
          sudo rm /etc/netplan/90-applaunch-networkmanager.yaml
          sudo apt remove network-manager
          sudo netplan apply
          sudo reboot
        Details (a Wi-Fi entry with its own "renderer: networkd", a slow boot): README.md, "Install on a PC (amd64)".
EOF
    else
        echo "          sudo apt install network-manager"
        echo "        and let NetworkManager manage the Wi-Fi interface (no other service for it: ifupdown, networkd,"
        echo "        a wpa_supplicant of its own), then reboot. Check: nmcli -t device status (the Wi-Fi \":connected\")."
    fi
fi
echo "Next: pair the keyboard if you have not yet (bluetoothctl: scan on / pair / trust / connect)."
if [ "${REBOOT:-0}" = 1 ]; then
    if [ "$SESSION" = desktop ]; then
        # only --pwm-backlight changes the boot configuration of a desktop board
        echo "Boot configuration changed: reboot now so the PWM backlight applies:  sudo reboot"
    else
        echo "Boot configuration changed: reboot now so the $( { [ "$DECK_BOOT" = 1 ] || [ "$HP_PWM" = 1 ] || [ "$LF_SETUP" = 1 ]; } && echo 'PWM backlight and ' )console settings apply:  sudo reboot"
    fi
fi
if [ "$SPI_REBOOT" = 1 ]; then
    echo "Reboot needed: the SPI panel, its touch and backlight come with the overlay at the next boot, then the launcher:  sudo reboot"
fi
if [ -n "$NM_NOTE" ]; then echo "$NM_NOTE"; fi
if [ "$NM_REBOOT" = 1 ]; then
    echo "Reboot needed: NetworkManager takes the network over at the next boot (Settings > Wi-Fi then works):  sudo reboot"
    echo "  An ssh session over Wi-Fi drops at the reboot; the Wi-Fi of the netplan files comes back under NetworkManager by"
    echo "  itself. Check: nmcli -t device status (the Wi-Fi line \":connected\"). Roll back: README.md, \"NetworkManager by install.sh\"."
fi
