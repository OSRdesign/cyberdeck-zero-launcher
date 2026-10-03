#!/bin/sh
# Install the launcher port on a Raspberry Pi Zero 2W with the Waveshare 2.8" DPI LCD.
#
# Run on the Pi, from the unpacked bundle (see build.sh):   sudo ./install.sh [options]
# It is idempotent: running it again updates the files and leaves everything else as it is.
#
# Options:
#   --keyboard-name NAME   Bluetooth keyboard name as shown in `bluetoothctl devices` (default: "M4 Keyboard")
#   --user NAME            user that runs the launcher (default: the user who ran sudo)
#   --no-boot-config       do not touch /boot/firmware/config.txt and cmdline.txt (no PWM backlight)
#   --force                continue even if this does not look like a Pi Zero 2W
#
# Prerequisites that this script does NOT do:
#   - Waveshare DPI panel overlays in config.txt (see config.txt.snippet and the Waveshare wiki);
#   - pairing the Bluetooth keyboard (bluetoothctl).
set -eu

KEYBOARD_NAME="M4 Keyboard"
TARGET_USER=""
BOOT_CONFIG=1
FORCE=0
while [ $# -gt 0 ]; do
    case "$1" in
        --keyboard-name) KEYBOARD_NAME="$2"; shift 2 ;;
        --user) TARGET_USER="$2"; shift 2 ;;
        --no-boot-config) BOOT_CONFIG=0; shift ;;
        --force) FORCE=1; shift ;;
        -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    exec sudo -E sh "$0" "$@"
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
MODEL=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || true)
echo "board: ${MODEL:-unknown}   user: $TARGET_USER"
case "$MODEL" in
    *"Zero 2"*) ;;
    *) [ "$FORCE" = 1 ] || { echo "this does not look like a Pi Zero 2W (use --force to continue)" >&2; exit 1; } ;;
esac
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

if [ "$BOOT_CONFIG" = 1 ]; then
    say "Boot configuration (PWM backlight, hidden console cursor)"
    CONFIG="$BOOT/config.txt"
    CMDLINE="$BOOT/cmdline.txt"
    [ -f "$CONFIG.bak-applaunch" ] || cp "$CONFIG" "$CONFIG.bak-applaunch"
    [ -f "$CMDLINE.bak-applaunch" ] || cp "$CMDLINE" "$CMDLINE.bak-applaunch"
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
    echo "Boot configuration changed: reboot now so the PWM backlight and console settings apply:  sudo reboot"
fi
