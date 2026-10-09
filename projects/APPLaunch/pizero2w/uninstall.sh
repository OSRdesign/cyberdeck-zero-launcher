#!/bin/sh
# Uninstall the launcher port from a Raspberry Pi Zero 2W: the counterpart of install.sh.
#
# Run on the Pi as the deck user (sudo is used only for the system parts):   ./uninstall.sh [options]
# It is idempotent: running it again only reports what is already gone. It never writes or logs a password
# (sudo asks for it itself) and never uses pkill: the launcher is stopped through systemd.
#
# By default the user's data is kept: ~/.config/cardputerzero, the Settings > Apps sources and records
# (~/.local/share/cardputerzero-appstore), the download cache, and every installed app package with its data.
#
# Options:
#   --dry-run          print every action, change nothing
#   --yes              do not ask for confirmation
#   --purge-config     also remove ~/.config/cardputerzero, ~/.local/share/cardputerzero-appstore and
#                      ~/.cache/cardputerzero-appstore (settings, app sources, records, cache)
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
        -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
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
UNIT_DIR="$TARGET_HOME/.config/systemd/user"
CONFIG="$BOOT/config.txt"
CMDLINE="$BOOT/cmdline.txt"
OVERLAY="$BOOT/overlays/waveshare-pwm-backlight.dtbo"
PWM_LINE='dtoverlay=pwm,pin=18,func=2'
BL_LINE='dtoverlay=waveshare-pwm-backlight'
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
if grep -q 'APPLaunch.service' /proc/self/cgroup 2>/dev/null && [ "$DRY" = 0 ]; then
    echo "this shell runs inside APPLaunch.service (the launcher's terminal): stopping the service would kill the" >&2
    echo "uninstall half way. Run it from an ssh session or a text console instead." >&2
    exit 1
fi

say "Uninstall plan (user: $TARGET_USER, home: $TARGET_HOME)"
[ "$DRY" = 1 ] && echo "DRY RUN: nothing will be changed."
echo "Will remove:"
echo "  - user service APPLaunch.service (stop, disable, delete, reset-failed)"
echo "  - system service launcher-ntp-default.service"
echo "  - $APP_ROOT/bin/M5CardputerZero-APPLaunch, M5CardputerZero-AppStore, lib/libapplaunch_vfb.so"
case "$ASSETS_SOURCE" in
    none) echo "  - launcher assets in $APP_ROOT: NOT removed (no payload/share.manifest next to this script; use --payload DIR)" ;;
    *) echo "  - $(wc -l < "$ASSETS_TMP" | tr -d ' ') launcher asset files in $APP_ROOT (from $ASSETS_SOURCE)" ;;
esac
echo "  - udev rules 90-backlight-unblank, 91-applaunch-vkbd, 99-bt-keyboard"
echo "  - polkit rules 50-networkmanager-netdev, 51-launcher-time-power"
if [ "$BOOT_CONFIG" = 1 ]; then
    if [ "$RESTORE_OK" = 1 ]; then
        echo "  - $CONFIG restored from $CONFIG.bak-before-pwm (a timestamped backup of the current file is made first)"
    elif [ "$CFG_REMOVE_BL" = 1 ]; then
        echo "  - config.txt lines: $BL_LINE$( [ "$CFG_REMOVE_PWM" = 1 ] && echo " and $PWM_LINE")"
    fi
    [ "$DTBO_REMOVE" = 1 ] && echo "  - $OVERLAY"
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
fi
echo "Will keep:"
[ "$PURGE_CONFIG" = 1 ] || echo "  - $CONF_DIR, $STATE_DIR, $CACHE_DIR"
if [ "$REMOVE_APPS" = 1 ] && [ -s "$APPS_TMP" ]; then :; else echo "  - installed app packages and their data (/usr/share/APPLaunch/applications, /opt/..., apps' own files)"; fi
echo "  - packages install.sh added with apt (libinput10 libxkbcommon0 libfreetype6 curl device-tree-compiler)"
echo "  - group membership (input, video, netdev) of $TARGET_USER"
[ "$DISABLE_LINGER" = 1 ] || echo "  - lingering (add --disable-linger to turn it off)"
echo "  - the *.bak-applaunch backups next to config.txt and cmdline.txt, $CONFIG.bak-before-pwm"
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
if as_user systemctl --user cat APPLaunch.service >/dev/null 2>&1 || exists "$UNIT_DIR/APPLaunch.service"; then
    act_try as_user systemctl --user stop APPLaunch.service
    act_try as_user systemctl --user disable APPLaunch.service
else
    note "APPLaunch.service is not installed"
fi
# a dead user bus must not leave the unit behind
if exists "$UNIT_DIR/APPLaunch.service"; then act rm -f "$UNIT_DIR/APPLaunch.service"; else note "already gone: $UNIT_DIR/APPLaunch.service"; fi
if exists "$UNIT_DIR/default.target.wants/APPLaunch.service"; then act rm -f "$UNIT_DIR/default.target.wants/APPLaunch.service"; fi
act_try as_user systemctl --user daemon-reload
act_try as_user systemctl --user reset-failed APPLaunch.service
for d in "$UNIT_DIR/default.target.wants" "$UNIT_DIR"; do
    if [ -d "$d" ] && [ -z "$(ls -A "$d" 2>/dev/null)" ]; then act rmdir "$d"; fi
done

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
for f in 90-backlight-unblank.rules 91-applaunch-vkbd.rules 99-bt-keyboard.rules; do
    exists "$UDEV_DIR/$f" && UDEV_CHANGED=1
    rm_sys "$UDEV_DIR/$f"
done
[ "$UDEV_CHANGED" = 1 ] && act_try $SUDO udevadm control --reload
for f in 50-networkmanager-netdev.rules 51-launcher-time-power.rules; do rm_sys "$POLKIT_DIR/$f"; done

say "Removing the launcher files in $APP_ROOT"
for f in bin/M5CardputerZero-APPLaunch bin/M5CardputerZero-AppStore lib/libapplaunch_vfb.so; do rm_sys "$APP_ROOT/$f"; done
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
    elif [ "$CFG_REMOVE_BL" = 1 ]; then
        act $SUDO cp -p "$CONFIG" "$CONFIG.bak-uninstall-$STAMP"
        note "backup: $CONFIG.bak-uninstall-$STAMP"
        EDIT_TMP=$(mktemp)
        grep -vxF -e "$BL_LINE" "$CONFIG" > "$EDIT_TMP" || true
        if [ "$CFG_REMOVE_PWM" = 1 ]; then
            grep -vxF -e "$PWM_LINE" "$EDIT_TMP" > "$EDIT_TMP.2" || true
            mv "$EDIT_TMP.2" "$EDIT_TMP"
        fi
        note "config.txt lines removed:"
        grep -nxF -e "$BL_LINE" -e "$PWM_LINE" "$CONFIG" | while IFS= read -r l; do
            case "$l" in *"$PWM_LINE") [ "$CFG_REMOVE_PWM" = 1 ] || continue ;; esac
            printf '    - %s\n' "$l"
        done
        act $SUDO cp "$EDIT_TMP" "$CONFIG"
        rm -f "$EDIT_TMP"
        NEED_REBOOT=1
    else
        note "config.txt: no line of install.sh to remove${CFG_NOTE:+ ($CFG_NOTE)}"
    fi
    if [ "$DTBO_REMOVE" = 1 ]; then rm_sys "$OVERLAY"; NEED_REBOOT=1; else note "overlay file: nothing to remove"; fi
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
    for d in "$CONF_DIR" "$STATE_DIR" "$CACHE_DIR"; do
        if [ -d "$d" ]; then
            # these paths are fixed names under the user's home, never a wildcard
            case "$d" in */cardputerzero|*/cardputerzero-appstore) act rm -rf "$d" ;; *) note "not removing unexpected path $d" ;; esac
        else
            note "already gone: $d"
        fi
    done
fi

# ---------------------------------------------------------------------------------------------------------
say "Done$( [ "$DRY" = 1 ] && echo ' (dry run: nothing was changed)')"
echo "What remains:"
for p in "$CONF_DIR" "$STATE_DIR" "$CACHE_DIR"; do [ -d "$p" ] && echo "  kept (user data): $p"; done
if [ -d "$APP_ROOT" ]; then
    echo "  $APP_ROOT: $(find "$APP_ROOT" -type f 2>/dev/null | wc -l | tr -d ' ') files left (installed apps and their entries)"
fi
for b in "$CONFIG.bak-applaunch" "$CMDLINE.bak-applaunch" "$CONFIG.bak-before-pwm"; do [ -f "$b" ] && echo "  backup kept: $b"; done
for b in "$BOOT"/config.txt.bak-uninstall-* "$BOOT"/cmdline.txt.bak-uninstall-*; do [ -f "$b" ] && echo "  backup made now: $b"; done
echo "  apt packages added by install.sh, and the groups input/video/netdev of $TARGET_USER (not recorded by install.sh)"
if [ "$DISABLE_LINGER" = 0 ]; then echo "  lingering stays on for $TARGET_USER (loginctl disable-linger $TARGET_USER to undo)"; fi
echo "Follow-ups:"
if [ "$NEED_REBOOT" = 1 ]; then
    echo "  - reboot so the overlay and console settings take effect:  sudo reboot"
fi
if [ "$GETTY_STATE" = masked ]; then
    echo "  - the login console on tty1 comes back at the next boot (or now: sudo systemctl start getty@tty1)"
fi
echo "  - the Waveshare gpio-backlight (on/off) takes over again after the reboot; the PWM brightness setting is gone"
echo "  - /dev/uinput and the bt-keyboard name go back to the system defaults after a reboot"
echo "  - to install again: sudo ./install.sh from the bundle"
