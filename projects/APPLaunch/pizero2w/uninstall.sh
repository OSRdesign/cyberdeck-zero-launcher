#!/bin/sh
# Uninstall CyberdeckZero Launcher from a Raspberry Pi board: the counterpart of install.sh.
#
# Run on the Pi as the deck user (sudo is used only for the system parts):   ./uninstall.sh [options]
# It is idempotent: running it again only reports what is already gone. It never writes or logs a password
# (sudo asks for it itself) and never uses pkill: the launcher is stopped through systemd. It knows the names of this
# release (cyberdeckzero.service, ...) and those of v0.6.0 and before (APPLaunch.service, ...), so it also cleans a
# board that was never migrated, or only half.
#
# By default the user's data is kept: ~/.config/cardputerzero, the Settings > Apps sources and records
# (~/.local/share/cardputerzero-appstore), the download cache, and every installed app package with its data.
#
# Options:
#   --dry-run          print every action, change nothing
#   --yes              do not ask for confirmation
#   --purge-config     also remove ~/.config/cardputerzero, ~/.local/share/cardputerzero-appstore,
#                      ~/.cache/cardputerzero-appstore (settings, app sources, records, cache), and Settings > Updates'
#                      ~/.local/state/cyberdeckzero and ~/.cache/cyberdeckzero (its state, release list and downloads)
#   --remove-apps      also remove the app packages installed from the launcher (listed first, taken from
#                      ~/.local/share/cardputerzero-appstore/installed.json; skipped if there is no record).
#                      With --purge-config the packages are purged (their config files go too)
#   --restore-config   put back /boot/firmware/config.txt from config.txt.bak-before-pwm instead of removing
#                      the two PWM lines (this also drops any other change made to config.txt since)
#   --no-boot-config   do not touch config.txt, cmdline.txt and the overlay (counterpart of install.sh)
#   --disable-linger   also run "loginctl disable-linger" (install.sh enabled it; it may have been on before)
#   --user NAME        user that ran install.sh (default: the current user, or the one who ran sudo)
#   --payload DIR      bundle payload directory, to know which files install.sh copied into /usr/share/APPLaunch
#                      (default: payload/ next to this script)
#   -h, --help         this text
set -eu

DRY=0
YES=0
PURGE_CONFIG=0
REMOVE_APPS=0
RESTORE_CONFIG=0
BOOT_CONFIG=1
DISABLE_LINGER=0
TARGET_USER=""
PAYLOAD_ARG=""
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY=1; shift ;;
        --yes|-y) YES=1; shift ;;
        --purge-config) PURGE_CONFIG=1; shift ;;
        --remove-apps) REMOVE_APPS=1; shift ;;
        --restore-config) RESTORE_CONFIG=1; shift ;;
        --no-boot-config) BOOT_CONFIG=0; shift ;;
        --disable-linger) DISABLE_LINGER=1; shift ;;
        --user) [ $# -ge 2 ] || { echo "--user needs a name" >&2; exit 2; }; TARGET_USER="$2"; shift 2 ;;
        --payload) [ $# -ge 2 ] || { echo "--payload needs a directory" >&2; exit 2; }; PAYLOAD_ARG="$2"; shift 2 ;;
        -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# UNINSTALL_ROOT is a test hook: it prefixes every system path (/etc, /usr/share/APPLaunch, /boot). Leave it unset.
ROOT=${UNINSTALL_ROOT:-}
APP_ROOT="$ROOT/usr/share/APPLaunch"
BOOT="$ROOT/boot/firmware"
[ -d "$BOOT" ] || BOOT="$ROOT/boot"
UDEV_DIR="$ROOT/etc/udev/rules.d"
POLKIT_DIR="$ROOT/etc/polkit-1/rules.d"
SYSTEMD_DIR="$ROOT/etc/systemd/system"
MENU_DIR="$ROOT/usr/share/applications"
DOC_DIR="$ROOT/usr/share/doc/cyberdeckzero-launcher"
PROFILE_DIR="$ROOT/etc/applaunch"
PROFILE="$PROFILE_DIR/board.conf"
INSTALL_CONF="$PROFILE_DIR/install.conf"
# Settings > Updates (report 047 P2): the update helper's status and last backup, its work folder, its logs, its lock
UPDATE_STATE_DIR="$ROOT/var/lib/cyberdeckzero-update"
UPDATE_WORK_DIR="$ROOT/var/cache/cyberdeckzero-update"
UPDATE_RUN_DIR="$ROOT/run/cyberdeckzero-update"
UPDATE_LOG_DIR="$ROOT/var/log/cyberdeckzero"

# The names of this release, then those of v0.6.0 and before (as in install.sh, "Names")
LAUNCHER_BIN=cyberdeckzero
CONSOLE_HELPER=cyberdeckzero-console
CTL_BIN=cyberdeckzero-ctl
UPDATE_HELPER=cyberdeckzero-update
USER_UNIT=cyberdeckzero.service
CONSOLE_UNIT=cyberdeckzero-console.service
DESKTOP_UNIT=cyberdeckzero-desktop.service
DESKTOP_RULES=52-cyberdeckzero-desktop.rules
DESKTOP_ENTRY_NAME=cyberdeckzero.desktop
# branding-allow-begin: the binary name of v0.6.0 and before (a link to the new binary after a migration)
OLD_LAUNCHER_BIN=M5CardputerZero-APPLaunch
# branding-allow-end
OLD_CONSOLE_HELPER=applaunch-console
OLD_USER_UNIT=APPLaunch.service
OLD_CONSOLE_UNIT=applaunch-console.service
OLD_DESKTOP_UNIT=applaunch-desktop.service
OLD_DESKTOP_RULES=52-applaunch-desktop.rules
OLD_DESKTOP_ENTRY_NAME=applaunch.desktop
# Files of earlier bundles that the current one no longer ships (install.sh: RETIRED_FILES): not in its manifest
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
share/images/tank.png share/images/tank_100.png share/images/tank_80.png"

HERE=$(cd "$(dirname "$0")" && pwd)

SELF_UID=$(id -u)
if [ -z "$TARGET_USER" ]; then
    if [ "$SELF_UID" -eq 0 ]; then TARGET_USER=${SUDO_USER:-}; else TARGET_USER=$(id -un); fi
fi
[ -n "$TARGET_USER" ] && id "$TARGET_USER" >/dev/null 2>&1 || { echo "cannot determine the user: pass --user NAME" >&2; exit 1; }
TARGET_UID=$(id -u "$TARGET_USER")
if [ "$TARGET_USER" = "$(id -un)" ] && [ -n "${HOME:-}" ]; then
    TARGET_HOME=$HOME
else
    TARGET_HOME=$(getent passwd "$TARGET_USER" | cut -d: -f6)
fi
[ -n "$TARGET_HOME" ] || { echo "cannot find the home directory of $TARGET_USER" >&2; exit 1; }

CONF_DIR="$TARGET_HOME/.config/cardputerzero"
STATE_DIR=${M5APPSTORE_STATE_DIR:-$TARGET_HOME/.local/share/cardputerzero-appstore}
CACHE_DIR=${M5APPSTORE_CACHE_DIR:-$TARGET_HOME/.cache/cardputerzero-appstore}
# Settings > Updates' own files (update.state; the release list and the downloaded bundles)
UPD_STATE_DIR="$TARGET_HOME/.local/state/cyberdeckzero"
UPD_CACHE_DIR="$TARGET_HOME/.cache/cyberdeckzero"
UNIT_DIR="$TARGET_HOME/.config/systemd/user"
WP_DROPIN="$TARGET_HOME/.config/wireplumber/wireplumber.conf.d/50-cyberdeckzero-bluez-no-seat.conf"
CONFIG="$BOOT/config.txt"
CMDLINE="$BOOT/cmdline.txt"
OVERLAY="$BOOT/overlays/waveshare-pwm-backlight.dtbo"
PWM_LINE='dtoverlay=pwm,pin=18,func=2'
BL_LINE='dtoverlay=waveshare-pwm-backlight'
# the Pi 5 + HyperPixel 4's PWM backlight (install.sh --pwm-backlight): one line and its overlay
HP_OVERLAY="$BOOT/overlays/hyperpixel4-pwm-backlight.dtbo"
HP_LINE='dtoverlay=hyperpixel4-pwm-backlight'
STAMP=$(date +%Y%m%d-%H%M%S)

say() { printf '\n== %s\n' "$*"; }
note() { printf '  %s\n' "$*"; }

# sudo only for the system parts, and not at all when already root
SUDO=""
if [ "$SELF_UID" -ne 0 ]; then SUDO="sudo"; fi

# Run a command, or only print it with --dry-run.
act() {
    if [ "$DRY" = 1 ]; then printf '  [dry-run] %s\n' "$*"; else printf '  %s\n' "$*"; "$@"; fi
}
# Same, but allowed to fail.
act_try() { act "$@" || note "(failed or nothing to do, continuing)"; }

# A command in the target user's session (the user service lives on the user bus).
as_user() {
    if [ "$SELF_UID" -eq 0 ]; then
        runuser -u "$TARGET_USER" -- env XDG_RUNTIME_DIR="/run/user/$TARGET_UID" "$@"
    else
        env XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$TARGET_UID}" "$@"
    fi
}

exists() { [ -e "$1" ] || [ -L "$1" ]; }

# Remove one system file if it is there.
rm_sys() {
    if exists "$1"; then act $SUDO rm -f "$1"; CHANGED_SYS=1; else note "already gone: $1"; fi
}

CHANGED_SYS=0
NEED_REBOOT=0

# ---------------------------------------------------------------------------------------------------------
# What install.sh copied into /usr/share/APPLaunch/ (payload/share): from the manifest build.sh writes, else
# from the payload directory. Installed apps share this directory, so nothing is removed by a wildcard.
# ---------------------------------------------------------------------------------------------------------
PAYLOAD=${PAYLOAD_ARG:-$HERE/payload}
MANIFEST=""
for cand in "$PAYLOAD/share.manifest" "$HERE/share.manifest"; do
    if [ -f "$cand" ]; then MANIFEST=$cand; break; fi
done
ASSETS_TMP=$(mktemp)
trap 'rm -f "$ASSETS_TMP" "${EDIT_TMP:-}"' EXIT
ASSETS_SOURCE="none"
if [ -n "$MANIFEST" ]; then
    grep -v '^[[:space:]]*$' "$MANIFEST" > "$ASSETS_TMP" || true
    ASSETS_SOURCE="manifest $MANIFEST"
elif [ -d "$PAYLOAD/share" ]; then
    ( cd "$PAYLOAD/share" && find . -type f | sed 's|^\./||' ) > "$ASSETS_TMP"
    ASSETS_SOURCE="payload $PAYLOAD/share"
fi
# refuse anything that is not a plain relative path
case $(grep -c -E '(^/|(^|/)\.\.(/|$))' "$ASSETS_TMP" || true) in
    0) ;;
    *) echo "the file list has absolute or .. paths: refusing ($ASSETS_SOURCE)" >&2; exit 1 ;;
esac

# ---------------------------------------------------------------------------------------------------------
# Apps installed from the launcher: the launcher's own record (installed.json). Only packages that dpkg says
# are installed AND that own files under /usr/share/APPLaunch are listed.
# ---------------------------------------------------------------------------------------------------------
APPS_TMP=$(mktemp)
trap 'rm -f "$ASSETS_TMP" "$APPS_TMP" "${EDIT_TMP:-}"' EXIT
APPS_NOTE=""
find_apps() {
    rec="$STATE_DIR/installed.json"
    if [ ! -r "$rec" ]; then APPS_NOTE="no record ($rec is missing)"; return; fi
    grep -o '"package"[[:space:]]*:[[:space:]]*"[^"]*"' "$rec" 2>/dev/null \
        | sed 's/.*:[[:space:]]*"\(.*\)"$/\1/' | grep -E '^[a-z0-9][a-z0-9+.-]+$' | sort -u > "$APPS_TMP.all" || true
    : > "$APPS_TMP"
    while IFS= read -r pkg; do
        [ -n "$pkg" ] || continue
        status=$(dpkg-query -W -f='${db:Status-Abbrev}' "$pkg" 2>/dev/null || true)
        case "$status" in i*) ;; *) continue ;; esac
        if dpkg-query -L "$pkg" 2>/dev/null | grep -q '^/usr/share/APPLaunch/'; then echo "$pkg" >> "$APPS_TMP"; fi
    done < "$APPS_TMP.all"
    rm -f "$APPS_TMP.all"
    if [ ! -s "$APPS_TMP" ]; then APPS_NOTE="the record lists no installed app package"; fi
}
find_apps

# ---------------------------------------------------------------------------------------------------------
# Boot configuration: decide what the installer owns. Evidence is the backups install.sh (or the manual PWM
# change before it) made: a line that is already in the pre-install backup was not written by install.sh.
# ---------------------------------------------------------------------------------------------------------
CONFIG_REF=""
if [ -f "$CONFIG.bak-before-pwm" ]; then CONFIG_REF="$CONFIG.bak-before-pwm"
elif [ -f "$CONFIG.bak-applaunch" ]; then CONFIG_REF="$CONFIG.bak-applaunch"; fi
CMD_REF=""
[ -f "$CMDLINE.bak-applaunch" ] && CMD_REF="$CMDLINE.bak-applaunch"

CFG_REMOVE_BL=0
CFG_REMOVE_PWM=0
CFG_NOTE=""
if [ "$BOOT_CONFIG" = 1 ] && [ -f "$CONFIG" ]; then
    if grep -qxF "$BL_LINE" "$CONFIG"; then
        if [ -n "$CONFIG_REF" ] && grep -qxF "$BL_LINE" "$CONFIG_REF"; then
            CFG_NOTE="$BL_LINE is also in $CONFIG_REF: not written by install.sh, kept"
        else
            CFG_REMOVE_BL=1
            [ -n "$CONFIG_REF" ] || CFG_NOTE="no config.txt backup to compare with: removing the two PWM lines by exact match"
            # the pwm line only goes together with the backlight line it was added for
            if grep -qxF "$PWM_LINE" "$CONFIG"; then CFG_REMOVE_PWM=1; fi
        fi
    fi
fi
CFG_REMOVE_HP=0
if [ "$BOOT_CONFIG" = 1 ] && [ -f "$CONFIG" ] && grep -qxF "$HP_LINE" "$CONFIG"; then
    if [ -n "$CONFIG_REF" ] && grep -qxF "$HP_LINE" "$CONFIG_REF"; then
        CFG_NOTE="${CFG_NOTE:+$CFG_NOTE; }$HP_LINE is also in $CONFIG_REF: not written by install.sh, kept"
    else
        CFG_REMOVE_HP=1
    fi
fi
# the config.txt lines to remove, for the plan
CFG_LINES=""
[ "$CFG_REMOVE_BL" = 0 ] || CFG_LINES="$BL_LINE$( [ "$CFG_REMOVE_PWM" = 1 ] && echo " and $PWM_LINE")"
[ "$CFG_REMOVE_HP" = 0 ] || CFG_LINES="${CFG_LINES:+$CFG_LINES, }$HP_LINE"
CMD_TOKENS=""
CMD_NOTE=""
if [ "$BOOT_CONFIG" = 1 ] && [ -f "$CMDLINE" ]; then
    for tok in vt.global_cursor_default=0 consoleblank=0; do
        grep -q -- "$tok" "$CMDLINE" || continue
        if [ -z "$CMD_REF" ]; then
            CMD_NOTE="no $CMDLINE.bak-applaunch to tell what install.sh added: cmdline.txt left alone"
            CMD_TOKENS=""
            break
        elif grep -q -- "$tok" "$CMD_REF"; then
            CMD_NOTE="${CMD_NOTE:+$CMD_NOTE; }$tok was already there before install.sh: kept"
        else
            CMD_TOKENS="$CMD_TOKENS $tok"
        fi
    done
fi
DTBO_REMOVE=0
if [ "$BOOT_CONFIG" = 1 ] && exists "$OVERLAY"; then
    # keep the overlay file if config.txt still loads it (a line that is not ours)
    if [ "$CFG_REMOVE_BL" = 1 ] || ! { [ -f "$CONFIG" ] && grep -qxF "$BL_LINE" "$CONFIG"; } || [ "$RESTORE_CONFIG" = 1 ]; then
        DTBO_REMOVE=1
    fi
fi
HP_DTBO_REMOVE=0
if [ "$BOOT_CONFIG" = 1 ] && exists "$HP_OVERLAY"; then
    if [ "$CFG_REMOVE_HP" = 1 ] || ! { [ -f "$CONFIG" ] && grep -qxF "$HP_LINE" "$CONFIG"; } || [ "$RESTORE_CONFIG" = 1 ]; then
        HP_DTBO_REMOVE=1
    fi
fi
RESTORE_OK=0
if [ "$RESTORE_CONFIG" = 1 ]; then
    if [ "$BOOT_CONFIG" = 0 ]; then
        echo "--restore-config and --no-boot-config contradict each other" >&2; exit 2
    elif [ -f "$CONFIG.bak-before-pwm" ]; then
        RESTORE_OK=1
    else
        echo "--restore-config: $CONFIG.bak-before-pwm does not exist" >&2; exit 1
    fi
fi

# getty@tty1: restore only if it is masked now (install.sh masked it)
GETTY_STATE=$(systemctl is-enabled getty@tty1.service 2>/dev/null || true)

# ---------------------------------------------------------------------------------------------------------
# Plan and confirmation
# ---------------------------------------------------------------------------------------------------------
if grep -q -e "$USER_UNIT" -e "$DESKTOP_UNIT" -e "$OLD_USER_UNIT" -e "$OLD_DESKTOP_UNIT" /proc/self/cgroup 2>/dev/null \
        && [ "$DRY" = 0 ]; then
    echo "this shell runs inside the launcher's service (the launcher's terminal): stopping the service would kill the" >&2
    echo "uninstall half way. Run it from an ssh session or a text console instead." >&2
    exit 1
fi

say "Uninstall plan (user: $TARGET_USER, home: $TARGET_HOME)"
[ "$DRY" = 1 ] && echo "DRY RUN: nothing will be changed."
echo "Will remove (the names of this release, then those of v0.6.0 and before, whichever are there):"
echo "  - user service $USER_UNIT / $OLD_USER_UNIT (stop, disable, delete, reset-failed)"
echo "  - system service $CONSOLE_UNIT / $OLD_CONSOLE_UNIT (appliance boards: the panel goes back to the text"
echo "    console)"
echo "  - system service $DESKTOP_UNIT / $OLD_DESKTOP_UNIT (desktop boards: stopped first if it runs, so the"
echo "    desktop comes back) and the menu entry ${MENU_DIR#"$ROOT"}/$DESKTOP_ENTRY_NAME / $OLD_DESKTOP_ENTRY_NAME"
echo "  - system service launcher-ntp-default.service"
if exists "$WP_DROPIN"; then echo "  - $WP_DROPIN (desktop boards: WirePlumber Bluetooth audio drop-in)"; fi
echo "  - $APP_ROOT/bin/$LAUNCHER_BIN, $OLD_LAUNCHER_BIN, M5CardputerZero-AppStore, $CONSOLE_HELPER, $OLD_CONSOLE_HELPER,"
echo "    $CTL_BIN (and its link $ROOT/usr/local/bin/$CTL_BIN), $UPDATE_HELPER, lib/libapplaunch_vfb.so"
echo "  - Settings > Updates' helper files: $UPDATE_STATE_DIR (its status and last backup), $UPDATE_WORK_DIR,"
echo "    $UPDATE_RUN_DIR, $UPDATE_LOG_DIR/update-*.log"
case "$ASSETS_SOURCE" in
    none) echo "  - launcher assets in $APP_ROOT: NOT removed (no payload/share.manifest next to this script; use --payload DIR)" ;;
    *) echo "  - $(wc -l < "$ASSETS_TMP" | tr -d ' ') launcher asset files in $APP_ROOT (from $ASSETS_SOURCE)" ;;
esac
echo "  - files of earlier bundles no longer shipped: $(echo $RETIRED_FILES | sed 's/ /, /g') (those there)"
echo "  - licences and notices $DOC_DIR"
echo "  - udev rules 90-backlight-unblank, 91-applaunch-vkbd, 92-applaunch-backlight-gpio, 92-applaunch-backlight-sysfs, 93-applaunch-rfkill, 99-bt-keyboard"
echo "  - board profile $PROFILE and the install options $INSTALL_CONF (and $PROFILE_DIR if it is then empty)"
echo "  - polkit rules 50-networkmanager-netdev, 51-launcher-time-power, ${DESKTOP_RULES%.rules}, ${OLD_DESKTOP_RULES%.rules}"
if [ "$BOOT_CONFIG" = 1 ]; then
    if [ "$RESTORE_OK" = 1 ]; then
        echo "  - $CONFIG restored from $CONFIG.bak-before-pwm (a timestamped backup of the current file is made first)"
    elif [ -n "$CFG_LINES" ]; then
        echo "  - config.txt lines: $CFG_LINES"
    fi
    [ "$DTBO_REMOVE" = 1 ] && echo "  - $OVERLAY"
    [ "$HP_DTBO_REMOVE" = 1 ] && echo "  - $HP_OVERLAY"
    [ -n "$CMD_TOKENS" ] && echo "  - cmdline.txt arguments:$CMD_TOKENS"
fi
if [ "$GETTY_STATE" = masked ]; then echo "  - unmask and enable getty@tty1 (login console on tty1 comes back)"; fi
[ "$DISABLE_LINGER" = 1 ] && echo "  - lingering for $TARGET_USER (loginctl disable-linger)"
if [ "$REMOVE_APPS" = 1 ]; then
    if [ -s "$APPS_TMP" ]; then
        echo "  - app packages installed from the launcher ($( [ "$PURGE_CONFIG" = 1 ] && echo purge || echo remove )):"
        sed 's/^/        /' "$APPS_TMP"
    else
        echo "  - app packages: NONE (--remove-apps: $APPS_NOTE; skipped, nothing is guessed)"
    fi
fi
if [ "$PURGE_CONFIG" = 1 ]; then
    echo "  - $CONF_DIR"
    echo "  - $STATE_DIR (app sources and install records)"
    echo "  - $CACHE_DIR"
    echo "  - $UPD_STATE_DIR, $UPD_CACHE_DIR (Settings > Updates: its state, release list and downloads)"
fi
echo "Will keep:"
[ "$PURGE_CONFIG" = 1 ] || echo "  - $CONF_DIR, $STATE_DIR, $CACHE_DIR, $UPD_STATE_DIR, $UPD_CACHE_DIR"
if [ "$REMOVE_APPS" = 1 ] && [ -s "$APPS_TMP" ]; then :; else echo "  - installed app packages and their data (/usr/share/APPLaunch/applications, /opt/..., apps' own files)"; fi
echo "  - packages install.sh added with apt (libinput10 libxkbcommon0 libfreetype6 curl device-tree-compiler)"
echo "  - group membership (input, video, netdev; audio with a sound card; i2c on the HackBerry) of $TARGET_USER"
[ "$DISABLE_LINGER" = 1 ] || echo "  - lingering (add --disable-linger to turn it off)"
echo "  - the *.bak-applaunch backups next to config.txt and cmdline.txt, $CONFIG.bak-before-pwm"
echo "  - $PROFILE.bak if install.sh made one (a replaced or hand-edited profile)"
[ "$BOOT_CONFIG" = 0 ] || [ -z "$CFG_NOTE$CMD_NOTE" ] || { echo "Notes:"; [ -z "$CFG_NOTE" ] || echo "  - $CFG_NOTE"; [ -z "$CMD_NOTE" ] || echo "  - $CMD_NOTE"; }

if [ "$DRY" = 0 ] && [ "$YES" = 0 ]; then
    if [ ! -t 0 ]; then echo "not a terminal: pass --yes to go ahead" >&2; exit 1; fi
    printf '\nProceed? [y/N] '
    read -r answer || answer=n
    case "$answer" in y|Y|yes|YES) ;; *) echo "aborted, nothing changed"; exit 1 ;; esac
fi
if [ "$DRY" = 0 ] && [ -n "$SUDO" ]; then
    $SUDO -v || { echo "sudo is needed for the system parts" >&2; exit 1; }
fi

# ---------------------------------------------------------------------------------------------------------
say "Stopping and removing the user service"
for unit in "$USER_UNIT" "$OLD_USER_UNIT"; do
    if exists "$UNIT_DIR/$unit" || exists "$UNIT_DIR/default.target.wants/$unit" \
            || as_user systemctl --user cat "$unit" >/dev/null 2>&1; then
        act_try as_user systemctl --user stop "$unit"
        act_try as_user systemctl --user disable "$unit"
        # a dead user bus must not leave the unit behind
        if exists "$UNIT_DIR/$unit"; then act rm -f "$UNIT_DIR/$unit"; else note "already gone: $UNIT_DIR/$unit"; fi
        if exists "$UNIT_DIR/default.target.wants/$unit"; then act rm -f "$UNIT_DIR/default.target.wants/$unit"; fi
        act_try as_user systemctl --user daemon-reload
        act_try as_user systemctl --user reset-failed "$unit"
    else
        note "$unit is not installed"
    fi
done
for d in "$UNIT_DIR/default.target.wants" "$UNIT_DIR"; do
    if [ -d "$d" ] && [ -z "$(ls -A "$d" 2>/dev/null)" ]; then act rmdir "$d"; fi
done
# the desktop variant's WirePlumber drop-in (Bluetooth audio while the launcher runs); the folders only if now empty
if exists "$WP_DROPIN"; then
    act rm -f "$WP_DROPIN"
    for d in "${WP_DROPIN%/*}" "$TARGET_HOME/.config/wireplumber"; do
        if [ -d "$d" ] && [ -z "$(ls -A "$d" 2>/dev/null)" ]; then act_try rmdir "$d"; fi
    done
fi

# ---------------------------------------------------------------------------------------------------------
# The text console and the desktop variant, before the launcher files (their units run the console helper).
say "Removing the text console and desktop units"
UNITS_CHANGED=0
for unit in "$DESKTOP_UNIT" "$OLD_DESKTOP_UNIT"; do
    if exists "$SYSTEMD_DIR/$unit"; then
        # stopping it runs its ExecStopPost=: the text console and the desktop come back
        act_try $SUDO systemctl stop "$unit"
        rm_sys "$SYSTEMD_DIR/$unit"
        UNITS_CHANGED=1
    else
        note "already gone: $unit"
    fi
done
for unit in "$CONSOLE_UNIT" "$OLD_CONSOLE_UNIT"; do
    if exists "$SYSTEMD_DIR/$unit"; then
        # stopping it runs its ExecStop=: the panel goes back to the text console
        act_try $SUDO systemctl disable --now "$unit"
        rm_sys "$SYSTEMD_DIR/$unit"
        if exists "$SYSTEMD_DIR/multi-user.target.wants/$unit"; then rm_sys "$SYSTEMD_DIR/multi-user.target.wants/$unit"; fi
        UNITS_CHANGED=1
    else
        note "already gone: $unit"
    fi
done
if [ "$UNITS_CHANGED" = 1 ]; then
    act_try $SUDO systemctl daemon-reload
    act_try $SUDO systemctl reset-failed "$DESKTOP_UNIT" "$CONSOLE_UNIT" "$OLD_DESKTOP_UNIT" "$OLD_CONSOLE_UNIT"
fi
# whatever the units did: the framebuffer console bound again (idempotent, nothing to do when it is)
for helper in "$CONSOLE_HELPER" "$OLD_CONSOLE_HELPER"; do
    if exists "$APP_ROOT/bin/$helper"; then act_try $SUDO "$APP_ROOT/bin/$helper" bind; break; fi
done
rm_sys "$MENU_DIR/$DESKTOP_ENTRY_NAME"
rm_sys "$MENU_DIR/$OLD_DESKTOP_ENTRY_NAME"

# ---------------------------------------------------------------------------------------------------------
if [ "$REMOVE_APPS" = 1 ] && [ -s "$APPS_TMP" ]; then
    say "Removing app packages installed from the launcher"
    # shellcheck disable=SC2046
    act $SUDO apt-get "$( [ "$PURGE_CONFIG" = 1 ] && echo purge || echo remove )" -y $(tr '\n' ' ' < "$APPS_TMP")
    CHANGED_SYS=1
elif [ "$REMOVE_APPS" = 1 ]; then
    say "App packages"
    note "skipped: $APPS_NOTE"
fi

say "Removing the system service launcher-ntp-default"
if exists "$SYSTEMD_DIR/launcher-ntp-default.service"; then
    act_try $SUDO systemctl disable launcher-ntp-default.service
    rm_sys "$SYSTEMD_DIR/launcher-ntp-default.service"
    act_try $SUDO systemctl daemon-reload
    act_try $SUDO systemctl reset-failed launcher-ntp-default.service
else
    note "already gone: launcher-ntp-default.service"
fi

say "Removing udev and polkit rules"
UDEV_CHANGED=0
for f in 90-backlight-unblank.rules 91-applaunch-vkbd.rules 92-applaunch-backlight-gpio.rules 92-applaunch-backlight-sysfs.rules 93-applaunch-rfkill.rules 99-bt-keyboard.rules; do
    exists "$UDEV_DIR/$f" && UDEV_CHANGED=1
    rm_sys "$UDEV_DIR/$f"
done
[ "$UDEV_CHANGED" = 1 ] && act_try $SUDO udevadm control --reload
for f in 50-networkmanager-netdev.rules 51-launcher-time-power.rules "$DESKTOP_RULES" "$OLD_DESKTOP_RULES"; do
    rm_sys "$POLKIT_DIR/$f"
done

say "Removing the board profile and the install options"
rm_sys "$PROFILE"
rm_sys "$INSTALL_CONF"
if [ -d "$PROFILE_DIR" ] && [ -z "$(ls -A "$PROFILE_DIR" 2>/dev/null)" ]; then act $SUDO rmdir "$PROFILE_DIR"; fi

say "Removing Settings > Updates' helper files"
# fixed folders of the update helper alone (report 047 P2): its status and last backup, its work folder, its lock
for d in "$UPDATE_STATE_DIR" "$UPDATE_WORK_DIR" "$UPDATE_RUN_DIR"; do
    if [ -d "$d" ]; then act $SUDO rm -rf "$d"; CHANGED_SYS=1; else note "already gone: $d"; fi
done
for f in "$UPDATE_LOG_DIR"/update-*.log; do
    if exists "$f"; then rm_sys "$f"; fi
done
if [ -d "$UPDATE_LOG_DIR" ] && [ -z "$(ls -A "$UPDATE_LOG_DIR" 2>/dev/null)" ]; then act $SUDO rmdir "$UPDATE_LOG_DIR"; fi

say "Removing the launcher files in $APP_ROOT"
for f in "bin/$LAUNCHER_BIN" "bin/$OLD_LAUNCHER_BIN" bin/M5CardputerZero-AppStore "bin/$CONSOLE_HELPER" \
         "bin/$OLD_CONSOLE_HELPER" "bin/$CTL_BIN" "bin/$UPDATE_HELPER" lib/libapplaunch_vfb.so; do
    rm_sys "$APP_ROOT/$f"
done
# the link of the request socket's client: only when it still points at ours
if [ -L "$ROOT/usr/local/bin/$CTL_BIN" ] && [ "$(readlink "$ROOT/usr/local/bin/$CTL_BIN")" = "/usr/share/APPLaunch/bin/$CTL_BIN" ]; then
    rm_sys "$ROOT/usr/local/bin/$CTL_BIN"
fi
# files of earlier bundles that the current bundle (and so its manifest) no longer has
for f in $RETIRED_FILES; do
    if exists "$APP_ROOT/$f"; then rm_sys "$APP_ROOT/$f"; fi
done
if [ -d "$APP_ROOT/adb" ] && [ -z "$(ls -A "$APP_ROOT/adb" 2>/dev/null)" ]; then act $SUDO rmdir "$APP_ROOT/adb"; fi
# the licences and notices (a fixed folder of this launcher alone)
if [ -d "$DOC_DIR" ]; then act $SUDO rm -rf "$DOC_DIR"; CHANGED_SYS=1; else note "already gone: $DOC_DIR"; fi
if [ -s "$ASSETS_TMP" ]; then
    # one sudo call for the whole list instead of one per file
    LIST_TMP=$(mktemp)
    while IFS= read -r rel; do
        exists "$APP_ROOT/$rel" && printf '%s\n' "$APP_ROOT/$rel" >> "$LIST_TMP"
    done < "$ASSETS_TMP"
    if [ -s "$LIST_TMP" ]; then
        note "$(wc -l < "$LIST_TMP" | tr -d ' ') asset files (first: $(head -n 1 "$LIST_TMP"))"
        if [ "$DRY" = 1 ]; then
            sed "s/^/  [dry-run] $SUDO xargs rm -f: /" "$LIST_TMP"
        else
            $SUDO xargs rm -f < "$LIST_TMP"
        fi
        CHANGED_SYS=1
    else
        note "launcher asset files already gone"
    fi
    rm -f "$LIST_TMP"
    # now-empty directories, deepest first; a directory that still holds an app's files stays
    DIRS_TMP=$(mktemp)
    sed -n 's|/[^/]*$||p' "$ASSETS_TMP" | awk '{ n=split($0,a,"/"); p=""; for(i=1;i<=n;i++){p=(p==""?a[i]:p "/" a[i]); print p} }' \
        | sort -u | awk '{ print length($0) "\t" $0 }' | sort -rn | cut -f2- > "$DIRS_TMP"
    while IFS= read -r d; do
        if [ -d "$APP_ROOT/$d" ] && [ -z "$(ls -A "$APP_ROOT/$d" 2>/dev/null)" ]; then act $SUDO rmdir "$APP_ROOT/$d"; fi
    done < "$DIRS_TMP"
    rm -f "$DIRS_TMP"
fi
for d in bin lib applications share; do
    if [ -d "$APP_ROOT/$d" ] && [ -z "$(ls -A "$APP_ROOT/$d" 2>/dev/null)" ]; then act $SUDO rmdir "$APP_ROOT/$d"; fi
done
if [ -d "$APP_ROOT" ] && [ -z "$(ls -A "$APP_ROOT" 2>/dev/null)" ]; then act $SUDO rmdir "$APP_ROOT"; fi

# ---------------------------------------------------------------------------------------------------------
if [ "$BOOT_CONFIG" = 1 ]; then
    say "Boot configuration"
    if [ "$RESTORE_OK" = 1 ]; then
        act $SUDO cp -p "$CONFIG" "$CONFIG.bak-uninstall-$STAMP"
        note "backup of the current file: $CONFIG.bak-uninstall-$STAMP"
        act $SUDO cp "$CONFIG.bak-before-pwm" "$CONFIG"
        note "restored $CONFIG from $CONFIG.bak-before-pwm"
        NEED_REBOOT=1
    elif [ -n "$CFG_LINES" ]; then
        act $SUDO cp -p "$CONFIG" "$CONFIG.bak-uninstall-$STAMP"
        note "backup: $CONFIG.bak-uninstall-$STAMP"
        EDIT_TMP=$(mktemp)
        cp "$CONFIG" "$EDIT_TMP"
        for line in "$BL_LINE" "$PWM_LINE" "$HP_LINE"; do
            case "$line" in
                "$BL_LINE") [ "$CFG_REMOVE_BL" = 1 ] || continue ;;
                "$PWM_LINE") [ "$CFG_REMOVE_PWM" = 1 ] || continue ;;
                "$HP_LINE") [ "$CFG_REMOVE_HP" = 1 ] || continue ;;
            esac
            grep -vxF -e "$line" "$EDIT_TMP" > "$EDIT_TMP.2" || true
            mv "$EDIT_TMP.2" "$EDIT_TMP"
        done
        note "config.txt lines removed:"
        grep -nxF -e "$BL_LINE" -e "$PWM_LINE" -e "$HP_LINE" "$CONFIG" | while IFS= read -r l; do
            case "$l" in
                *":$BL_LINE") [ "$CFG_REMOVE_BL" = 1 ] || continue ;;
                *":$PWM_LINE") [ "$CFG_REMOVE_PWM" = 1 ] || continue ;;
                *":$HP_LINE") [ "$CFG_REMOVE_HP" = 1 ] || continue ;;
            esac
            printf '    - %s\n' "$l"
        done
        act $SUDO cp "$EDIT_TMP" "$CONFIG"
        rm -f "$EDIT_TMP"
        NEED_REBOOT=1
    else
        note "config.txt: no line of install.sh to remove${CFG_NOTE:+ ($CFG_NOTE)}"
    fi
    if [ "$DTBO_REMOVE" = 1 ]; then rm_sys "$OVERLAY"; NEED_REBOOT=1; else note "overlay file: nothing to remove"; fi
    if [ "$HP_DTBO_REMOVE" = 1 ]; then rm_sys "$HP_OVERLAY"; NEED_REBOOT=1; fi
    if [ -n "$CMD_TOKENS" ]; then
        act $SUDO cp -p "$CMDLINE" "$CMDLINE.bak-uninstall-$STAMP"
        note "backup: $CMDLINE.bak-uninstall-$STAMP"
        EDIT_TMP=$(mktemp)
        cp "$CMDLINE" "$EDIT_TMP"
        for tok in $CMD_TOKENS; do
            esc=$(printf '%s' "$tok" | sed 's/[.[\*^$/]/\\&/g')
            sed "s/ $esc\\( \\|\$\\)/\\1/g" "$EDIT_TMP" > "$EDIT_TMP.2"
            mv "$EDIT_TMP.2" "$EDIT_TMP"
            note "cmdline.txt argument removed: $tok"
        done
        act $SUDO cp "$EDIT_TMP" "$CMDLINE"
        rm -f "$EDIT_TMP"
        NEED_REBOOT=1
    else
        note "cmdline.txt: nothing to remove${CMD_NOTE:+ ($CMD_NOTE)}"
    fi
else
    say "Boot configuration"
    note "skipped (--no-boot-config)"
fi

say "Console on tty1"
if [ "$GETTY_STATE" = masked ]; then
    act_try $SUDO systemctl unmask getty@tty1.service
    act_try $SUDO systemctl enable getty@tty1.service
    CHANGED_SYS=1
else
    note "getty@tty1 is not masked (state: ${GETTY_STATE:-unknown}): nothing to restore"
fi
if [ "$DISABLE_LINGER" = 1 ]; then act_try $SUDO loginctl disable-linger "$TARGET_USER"; fi

# ---------------------------------------------------------------------------------------------------------
if [ "$PURGE_CONFIG" = 1 ]; then
    say "Removing the user's launcher data (--purge-config)"
    for d in "$CONF_DIR" "$STATE_DIR" "$CACHE_DIR" "$UPD_STATE_DIR" "$UPD_CACHE_DIR"; do
        if [ -d "$d" ]; then
            # these paths are fixed names under the user's home, never a wildcard
            case "$d" in */.config/cardputerzero|*/cardputerzero-appstore|*/.local/state/cyberdeckzero|*/.cache/cyberdeckzero) act rm -rf "$d" ;; *) note "not removing unexpected path $d" ;; esac
        else
            note "already gone: $d"
        fi
    done
fi

# ---------------------------------------------------------------------------------------------------------
say "Done$( [ "$DRY" = 1 ] && echo ' (dry run: nothing was changed)')"
echo "What remains:"
for p in "$CONF_DIR" "$STATE_DIR" "$CACHE_DIR" "$UPD_STATE_DIR" "$UPD_CACHE_DIR"; do [ -d "$p" ] && echo "  kept (user data): $p"; done
if [ -d "$APP_ROOT" ]; then
    echo "  $APP_ROOT: $(find "$APP_ROOT" -type f 2>/dev/null | wc -l | tr -d ' ') files left (installed apps and their entries)"
fi
for b in "$CONFIG.bak-applaunch" "$CMDLINE.bak-applaunch" "$CONFIG.bak-before-pwm" "$PROFILE.bak"; do [ -f "$b" ] && echo "  backup kept: $b"; done
for b in "$BOOT"/config.txt.bak-uninstall-* "$BOOT"/cmdline.txt.bak-uninstall-*; do [ -f "$b" ] && echo "  backup made now: $b"; done
echo "  apt packages added by install.sh, and the groups input/video/netdev (audio, i2c) of $TARGET_USER (not recorded by install.sh)"
if [ "$DISABLE_LINGER" = 0 ]; then echo "  lingering stays on for $TARGET_USER (loginctl disable-linger $TARGET_USER to undo)"; fi
echo "Follow-ups:"
if [ "$NEED_REBOOT" = 1 ]; then
    echo "  - reboot so the overlay and console settings take effect:  sudo reboot"
fi
if [ "$GETTY_STATE" = masked ]; then
    echo "  - the login console on tty1 comes back at the next boot (or now: sudo systemctl start getty@tty1)"
fi
echo "  - the Waveshare gpio-backlight (on/off) takes over again after the reboot; the PWM brightness setting is gone"
if [ "$CFG_REMOVE_HP" = 1 ] || [ "$HP_DTBO_REMOVE" = 1 ]; then
    echo "  - the HyperPixel 4's gpio-backlight (on/off) takes over again after the reboot"
fi
echo "  - /dev/uinput and the bt-keyboard name go back to the system defaults after a reboot"
echo "  - to install again: sudo ./install.sh from the bundle"
