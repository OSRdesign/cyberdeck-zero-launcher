"""Read-only fact finding on the FZ-M1. Credentials from FZM1_HOST / FZM1_USER / FZM1_PW (never printed)."""
import os
import sys
import paramiko

CMDS = [
    "uname -a",
    "cat /etc/os-release; cat /etc/armbian-release 2>/dev/null | head -20",
    "for f in sys_vendor product_name product_version product_family bios_vendor bios_version bios_date board_name; do printf '%s: ' $f; cat /sys/class/dmi/id/$f 2>/dev/null || echo '?'; done",
    "lscpu 2>/dev/null | grep -vi '^flags'; grep -m1 '^flags' /proc/cpuinfo | tr ' ' '\\n' | grep -E '^(sse4_2|avx|avx2|ssse3|lm|aes|movbe|popcnt)$' | tr '\\n' ' '",
    "free -m; swapon --show; cat /proc/meminfo | head -3; zramctl 2>/dev/null",
    "lsblk -o NAME,SIZE,TYPE,MODEL,TRAN,ROTA,MOUNTPOINT; df -h / /boot /boot/efi 2>/dev/null",
    "ls /sys/firmware/efi >/dev/null 2>&1 && echo EFI || echo BIOS; cat /sys/firmware/efi/fw_platform_size 2>/dev/null; ls /boot/efi/EFI/* 2>/dev/null; cat /proc/cmdline",
    "lspci -nnk 2>/dev/null || echo no-lspci",
    "lsusb 2>/dev/null || echo no-lsusb",
    "ls -l /sys/class/drm/; for c in /sys/class/drm/card*-*; do echo $c $(cat $c/status 2>/dev/null) $(cat $c/enabled 2>/dev/null); cat $c/modes 2>/dev/null | head -3; done; ls -l /dev/dri /dev/fb* 2>/dev/null; cat /proc/fb; cat /sys/class/graphics/fb0/virtual_size /sys/class/graphics/fb0/bits_per_pixel /sys/class/graphics/fb0/name 2>/dev/null",
    "cat /sys/module/drm_kms_helper/parameters/fbdev_emulation 2>/dev/null; zcat /proc/config.gz 2>/dev/null | grep -E 'CONFIG_(DRM_FBDEV_EMULATION|FB_DEVICE|DRM_I915|DRM_SIMPLEDRM|FRAMEBUFFER_CONSOLE)=' || grep -hE 'CONFIG_(DRM_FBDEV_EMULATION|FB_DEVICE|DRM_I915|DRM_SIMPLEDRM|FRAMEBUFFER_CONSOLE|HID_MULTITOUCH|HID_SENSOR_HUB|INPUT_UINPUT|SND_SOC_SOF_CHERRYTRAIL|SND_SST_ATOM_HIFI2_PLATFORM_ACPI)=' /boot/config-$(uname -r) 2>/dev/null",
    "dmesg 2>/dev/null | grep -iE 'i915|drm|panel|edp|dsi|backlight|orientation' | head -40 || echo dmesg-restricted; journalctl -k -b --no-pager 2>/dev/null | grep -iE 'i915|panel|dsi|orientation|backlight|firmware' | head -40",
    "for c in /sys/class/drm/card*-*; do [ -r $c/edid ] && echo $c && cat $c/edid | od -An -tx1 -j54 -N18 | head -2; done; ls /sys/class/backlight/; for b in /sys/class/backlight/*; do echo $b $(cat $b/type $b/brightness $b/max_brightness 2>/dev/null); done",
    "cat /proc/bus/input/devices",
    "ls -l /dev/input/by-id /dev/input/by-path 2>/dev/null; id; ls -l /dev/uinput /dev/input/event0",
    "which libinput && libinput list-devices 2>&1 | head -80",
    "ls /sys/bus/iio/devices/ 2>/dev/null; for d in /sys/bus/iio/devices/iio:device*; do echo $d $(cat $d/name); ls $d | head -20 | tr '\\n' ' '; echo; done; ls /sys/bus/hid/devices/ 2>/dev/null",
    "ls /sys/class/power_supply/; for p in /sys/class/power_supply/*; do echo $p; cat $p/uevent 2>/dev/null | head -20; done",
    "ip -br link; iw dev 2>/dev/null; ls /sys/class/net/*/device/driver -l 2>/dev/null; rfkill list 2>/dev/null; ls /sys/class/bluetooth/ 2>/dev/null; hciconfig -a 2>/dev/null | head; bluetoothctl show 2>/dev/null | head -8",
    "cat /proc/asound/cards 2>/dev/null; aplay -l 2>/dev/null | head",
    "lsmod | sort | head -150",
    "dpkg -l | grep -E '^ii' | grep -iE 'xorg|xserver|wayland|weston|cage|labwc|sway|wlroots|mutter|gnome-shell|plasma|lightdm|gdm|sddm|libsdl2|sdl3|libdrm|mesa|libinput|network-manager|bluez|iio-sensor|squeekboard|onboard|wvkbd|firmware' | awk '{print $2, $3}'",
    "dpkg -l | grep -c '^ii'; systemctl list-units --type=service --state=running --no-pager --no-legend | awk '{print $1}' | tr '\\n' ' '; echo; loginctl list-sessions --no-legend 2>/dev/null; echo XDG=$XDG_SESSION_TYPE; systemctl get-default",
    "ldd --version | head -1; gcc --version 2>/dev/null | head -1; python3 --version; dpkg --print-architecture; dpkg --print-foreign-architectures",
    "cat /sys/power/mem_sleep 2>/dev/null; cat /sys/devices/system/cpu/cpuidle/current_driver 2>/dev/null; uptime; cat /proc/loadavg",
]


def main():
    host, user, pw = os.environ.get("FZM1_HOST"), os.environ.get("FZM1_USER"), os.environ.get("FZM1_PW")
    if not (host and user and pw):
        sys.exit("FZM1_HOST / FZM1_USER / FZM1_PW not set")
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(host, username=user, password=pw, timeout=15, look_for_keys=False, allow_agent=False)
    for cmd in CMDS:
        print("=" * 8, cmd[:110])
        _, out, err = c.exec_command(cmd, timeout=60)
        text = out.read().decode(errors="replace") + err.read().decode(errors="replace")
        print(text.replace(pw, "***") if pw else text)
    c.close()


if __name__ == "__main__":
    main()
