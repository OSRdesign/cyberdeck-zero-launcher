#!/bin/sh
# Read-only hardware probe: tells us what the launcher has to deal with on this board.
#
# Run on the board as a normal user (no sudo needed):   sh probe-board.sh
# It prints one Markdown report and also saves it as ./board-probe-<hostname>-<date>.md
# (or in $HOME / /tmp if the current directory is not writable). Paste that file back.
#
# Options:
#   --selftest       check the /proc/bus/input/devices bitmap decoder against a built-in sample, then exit
#   --decode FILE    decode a saved copy of /proc/bus/input/devices (Markdown table), then exit
#
# It changes nothing on the system, never uses sudo, and does not collect secrets: no Wi-Fi
# credentials, no ssh keys, no NetworkManager connection files, no environment dump. MAC addresses
# are masked. Every command is optional: a missing tool or a refused file is noted and skipped.

LC_ALL=C
export LC_ALL
VERSION=1

have() { command -v "$1" >/dev/null 2>&1; }
# read a sysfs/procfs value on one line (device-tree strings are NUL separated)
rd() { [ -r "$1" ] && tr '\0' ' ' < "$1" 2>/dev/null | head -n 1 | sed 's/ *$//'; }
TO=""
have timeout && TO="timeout 10"

# Word size of the kernel bitmaps in /proc/bus/input/devices (a C "long" of the kernel, not of userland).
case "$(uname -m 2>/dev/null)" in
    *64*) WB=64 ;;
    *) WB=32 ;;
esac
WB=${PROBE_WORD_BITS:-$WB}

# Decoder for /proc/bus/input/devices. Bitmaps are hex words of WB bits, space separated, most
# significant word first, leading zero words and leading zero digits omitted. So bit n lives in word
# int(n/WB) counted from the RIGHT, and inside that word in hex digit int((n%WB)/4) counted from the right.
# MODE=table prints a Markdown table, MODE=list prints node|name|keyboard|touch|bus, MODE=unit tests bit().
# shellcheck disable=SC2016
DECODE_AWK='
function hv(c) { return index("0123456789abcdef", tolower(c)) - 1 }
function bit(bm, n,   nw, w, b, word, pos) {
    if (bm == "") return 0
    nw = split(bm, ws, " "); w = int(n / WB); b = n % WB
    if (w >= nw) return 0
    word = ws[nw - w]; pos = length(word) - int(b / 4)
    if (pos < 1) return 0
    return int(hv(substr(word, pos, 1)) / (2 ^ (b % 4))) % 2
}
function busname(b) {
    b = tolower(b)
    if (b == "0003") return "usb"; if (b == "0005") return "bluetooth"; if (b == "0006") return "VIRTUAL"
    if (b == "0018") return "i2c"; if (b == "0019") return "host"; if (b == "001c") return "spi"
    return b
}
function yn(v) { return v ? "yes" : "-" }
function reset() { name = ""; bus = ""; hnd = ""; node = ""; ev = ""; key = ""; abs = ""; rel = ""; prop = "" }
function val(s) { sub(/^[^=]*=/, "", s); sub(/[ \t\r]+$/, "", s); return s }
function flush(   ka, kz, ke, ks, mtx, ax, kbd, touch, virt) {
    if (name == "" && ev == "") return
    ka = bit(key, 30); kz = bit(key, 44); ke = bit(key, 28); ks = bit(key, 57)
    mtx = bit(abs, 53); ax = bit(abs, 0); virt = (tolower(bus) == "0006")
    kbd = ka && kz && ke && ks && !virt && name !~ /^applaunch-/
    touch = (mtx || ax) && bit(ev, 3) && (bit(prop, 1) || bit(key, 330)) && name !~ /^applaunch-/
    if (node == "") node = "(none)"
    gsub(/\|/, "/", name)
    if (MODE == "list") print node "|" name "|" (kbd ? 1 : 0) "|" (touch ? 1 : 0) "|" busname(bus)
    else printf "| %s | %s | %s | %s | %s | %s%s%s%s | %s | %s | %s | %s | %s | %s |\n", node, name, busname(bus), hnd, ev, \
        (ka ? "A" : "."), (kz ? "Z" : "."), (ke ? "E" : "."), (ks ? "S" : "."), yn(mtx), yn(ax), yn(bit(ev, 2)), \
        yn(bit(prop, 1)), (touch ? "**TOUCH**" : "-"), (kbd ? "**KEYBOARD**" : "-")
    reset()
}
BEGIN {
    reset()
    if (MODE == "unit") {
        # bit 28 set, 30 not; multi-word: bit 330 = word 5 bit 10 (64-bit); "1 0" has bit 64 only
        WB = 64; f = 0
        if (bit("10000ffc", 28) != 1) f++; if (bit("10000ffc", 30) != 0) f++; if (bit("10000ffc", 2) != 1) f++
        if (bit("400 0 0 0 0 0", 330) != 1) f++; if (bit("400 0 0 0 0 0", 10) != 0) f++
        if (bit("1 0", 64) != 1) f++; if (bit("1 0", 0) != 0) f++; if (bit("265800000000003", 53) != 1) f++
        WB = 32; if (bit("2658000 3", 53) != 1) f++; if (bit("2658000 3", 0) != 1) f++; if (bit("2658000 3", 2) != 0) f++
        print (f == 0 ? "unit bit checks: PASS" : "unit bit checks: FAIL (" f ")"); exit (f != 0)
    }
    if (MODE != "list") {
        print "| node | name | bus | handlers | EV | keys A/Z/Enter/Space | ABS_MT_X | ABS_X | REL | DIRECT | touch? | keyboard? |"
        print "|---|---|---|---|---|---|---|---|---|---|---|---|"
    }
}
/^I:/ { if (match($0, /Bus=[0-9a-fA-F]+/)) bus = substr($0, RSTART + 4, RLENGTH - 4) }
/^N: Name=/ { name = val($0); gsub(/"/, "", name) }
/^H: Handlers=/ { hnd = val($0); n = split(hnd, hs, " "); for (i = 1; i <= n; i++) if (hs[i] ~ /^event/) node = hs[i] }
/^B: EV=/ { ev = val($0) }
/^B: KEY=/ { key = val($0) }
/^B: ABS=/ { abs = val($0) }
/^B: REL=/ { rel = val($0) }
/^B: PROP=/ { prop = val($0) }
/^[ \t\r]*$/ { flush() }
END { flush() }
'
decode() { awk -v WB="$WB" -v MODE="$1" "$DECODE_AWK"; }

# Desktop session detection. match_procs "NAMES": reads process names (one per line, as in
# /proc/PID/comm) on stdin and prints the NAMES found, joined with "+", in NAMES order. comm is cut
# to 15 characters, so a 15-character prefix of a longer name also matches; "wvkbd" matches wvkbd-*.
DESKTOP_NAMES="labwc wayfire sway weston Xorg Xwayland lightdm sddm gdm gdm3"
OSK_NAMES="squeekboard onboard wvkbd matchbox-keyboard"
match_procs() {
    awk -v names="$1" '
    { sub(/[ 	]+$/, ""); c = $0
      for (i = 1; i <= n; i++) if (c == a[i] || (length(c) == 15 && index(a[i], c) == 1) || (a[i] == "wvkbd" && index(c, "wvkbd") == 1)) hit[i] = 1 }
    BEGIN { n = split(names, a, " ") }
    END { out = ""; for (i = 1; i <= n; i++) if (hit[i]) out = out (out == "" ? "" : "+") a[i]; print out }'
}
proc_names() { cat /proc/[0-9]*/comm 2>/dev/null; }

sample_devices() {
    cat <<'EOF'
I: Bus=0003 Vendor=046d Product=c31c Version=0110
N: Name="Logitech USB Keyboard"
P: Phys=usb-3f980000.usb-1.2/input0
H: Handlers=sysrq kbd leds event0
B: PROP=0
B: EV=120013
B: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe
B: MSC=10
B: LED=7

I: Bus=0018 Vendor=0416 Product=038f Version=0100
N: Name="Goodix Capacitive TouchScreen"
P: Phys=input/ts
H: Handlers=mouse0 event5
B: PROP=2
B: EV=b
B: KEY=400 0 0 0 0 0
B: ABS=265800000000003

I: Bus=0003 Vendor=0bda Product=2838 Version=0100
N: Name="Realtek RTL2832U reference design"
P: Phys=usb-xhci-hcd.1-1/ir0
H: Handlers=kbd event6
B: PROP=0
B: EV=100013
B: KEY=100000000 0 0 0 1a000000000000 10000ffc
B: MSC=10

I: Bus=0006 Vendor=0000 Product=0000 Version=0000
N: Name="applaunch-vkbd"
P: Phys=
H: Handlers=sysrq kbd event9
B: PROP=0
B: EV=100003
B: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe
EOF
}

selftest() {
    rc=0
    awk -v MODE=unit "$DECODE_AWK" </dev/null || rc=1
    got=$(sample_devices | awk -v WB=64 -v MODE=list "$DECODE_AWK")
    want='event0|Logitech USB Keyboard|1|0|usb
event5|Goodix Capacitive TouchScreen|0|1|i2c
event6|Realtek RTL2832U reference design|0|0|usb
event9|applaunch-vkbd|0|0|VIRTUAL'
    if [ "$got" = "$want" ]; then echo "sample decode (keyboard, Goodix, IR receiver, applaunch-vkbd): PASS"
    else rc=1; printf 'sample decode: FAIL\n--- got\n%s\n--- want\n%s\n' "$got" "$want"; fi
    # the same Goodix block as a 32-bit kernel prints it
    got32=$(sample_devices | sed 's/^B: ABS=265800000000003/B: ABS=2658000 3/; s/^B: KEY=400 0 0 0 0 0$/B: KEY=400 0 0 0 0 0 0 0 0 0 0/' \
        | awk -v WB=32 -v MODE=list "$DECODE_AWK" | grep Goodix)
    if [ "$got32" = "event5|Goodix Capacitive TouchScreen|0|1|i2c" ]; then echo "32-bit word decode: PASS"
    else rc=1; echo "32-bit word decode: FAIL ($got32)"; fi
    sample_devices | awk -v WB=64 -v MODE=table "$DECODE_AWK"
    gotd=$(printf 'systemd
lightdm
Xwayland
labwc
swaybg
matchbox-keyboa
wvkbd-mobintl
bash
' | match_procs "$DESKTOP_NAMES $OSK_NAMES")
    if [ "$gotd" = "labwc+Xwayland+lightdm+wvkbd+matchbox-keyboard" ]; then echo "desktop/OSK process match (fake list): PASS"
    else rc=1; echo "desktop/OSK process match: FAIL ($gotd)"; fi
    gotn=$(printf 'systemd
sshd
' | match_procs "$DESKTOP_NAMES")
    if [ -z "$gotn" ]; then echo "desktop match on a headless list: PASS"; else rc=1; echo "headless match: FAIL ($gotn)"; fi
    [ $rc = 0 ] && echo "SELFTEST PASS" || echo "SELFTEST FAIL"
    return $rc
}

case "${1:-}" in
    --selftest) selftest; exit $? ;;
    --decode) decode table < "${2:?--decode needs a file}"; exit 0 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    "") ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
esac

# --- report helpers ------------------------------------------------------------------------------
h2() { printf '\n## %s\n' "$*"; }
# cmd TITLE command [args]: run a command if it exists and show its output in a code block
cmd() {
    title=$1; shift
    printf '\n### %s\n\n```\n' "$title"
    if have "$1"; then
        # shellcheck disable=SC2086
        $TO "$@" 2>&1 | head -n 80
    else
        echo "($1 not installed)"
    fi
    printf '```\n'
}
block_start() { printf '\n### %s\n\n```\n' "$1"; }
block_end() { printf '```\n'; }
mask_macs() { sed 's/[0-9A-Fa-f][0-9A-Fa-f]\(:[0-9A-Fa-f][0-9A-Fa-f]\)\{5\}/xx:xx:xx:xx:xx:xx/g'; }

report() {
    HOST=$(hostname 2>/dev/null || rd /etc/hostname)
    printf '# Board probe: %s\n\n' "${HOST:-unknown}"
    printf 'probe-board.sh v%s, %s, user %s\n' "$VERSION" "$(date '+%Y-%m-%d %H:%M %Z' 2>/dev/null)" "$(id -un 2>/dev/null)"

    h2 "Identity"
    MODEL=$(rd /proc/device-tree/model)
    [ -n "$MODEL" ] || MODEL=$(rd /sys/class/dmi/id/product_name)
    block_start "model, OS, kernel"
    echo "model:       ${MODEL:-unknown (no /proc/device-tree)}"
    echo "compatible:  $(rd /proc/device-tree/compatible)"
    grep -E '^(Hardware|Revision|Model)[[:space:]]*:' /proc/cpuinfo 2>/dev/null
    CPU=$(grep -m 1 -E '^(model name|CPU part)[[:space:]]*:' /proc/cpuinfo 2>/dev/null | sed 's/.*: *//')
    echo "cpu:         ${CPU:-?} (nproc $(nproc 2>/dev/null || echo ?))"
    have lscpu && lscpu 2>/dev/null | grep -E '^(Model name|Architecture|CPU max MHz)'
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        OS=$(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-?}")
        grep -E '^(PRETTY_NAME|ID|ID_LIKE|VERSION_ID|VERSION_CODENAME)=' /etc/os-release
    fi
    [ -r /etc/armbian-release ] && grep -E '^(BOARD|BOARD_NAME|BRANCH|VERSION|LINUXFAMILY)=' /etc/armbian-release
    echo "uname:       $(uname -a 2>/dev/null)"
    ARCH=$(dpkg --print-architecture 2>/dev/null || echo '?')
    echo "dpkg arch:   $ARCH  (kernel bitmap words: $WB bit)"
    GLIBC=$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')
    [ -n "$GLIBC" ] || GLIBC=$(ldd --version 2>/dev/null | head -n 1 | grep -i -E 'glibc|gnu libc' | awk '{print $NF}')
    GLIBC_OK=$(echo "$GLIBC" | awk -F. '{ if ($1 == "") print "UNKNOWN"; else if ($1 > 2 || ($1 == 2 && $2 >= 38)) print "OK"; else print "NOT OK" }')
    echo "ldd:         $(ldd --version 2>/dev/null | head -n 1)"
    echo "glibc:       ${GLIBC:-?}  -> launcher needs >= 2.38: $GLIBC_OK"
    PAGE=$(getconf PAGESIZE 2>/dev/null || getconf PAGE_SIZE 2>/dev/null)
    echo "page size:   ${PAGE:-?}"
    if [ -r /proc/config.gz ]; then
        (zcat /proc/config.gz 2>/dev/null || gzip -dc /proc/config.gz 2>/dev/null) | grep -E '^CONFIG_(ARM64_(4K|16K|64K)_PAGES|PAGE_SIZE_[0-9]+KB)='
    else
        echo "/proc/config.gz: not present"
    fi
    grep -E '^(MemTotal|MemAvailable|SwapTotal|CmaTotal|CmaFree):' /proc/meminfo 2>/dev/null
    MEM=$(awk '/^MemTotal:/ {printf "%d MB", $2/1024}' /proc/meminfo 2>/dev/null)
    have vcgencmd && { vcgencmd get_mem arm 2>&1; vcgencmd get_mem gpu 2>&1; }
    df -h / 2>/dev/null | tail -n 1
    block_end

    h2 "Boot configuration"
    BOOTCFG=""
    for f in /boot/firmware/config.txt /boot/config.txt /boot/armbianEnv.txt /boot/firmware/armbianEnv.txt; do
        [ -r "$f" ] || continue
        BOOTCFG="$BOOTCFG $f"
        block_start "$f (non-comment lines)"
        grep -v -E '^[[:space:]]*(#|$)' "$f" | head -n 120
        block_end
    done
    [ -n "$BOOTCFG" ] || echo "no config.txt / armbianEnv.txt found (or not readable)"
    block_start "overlays named in the boot config"
    # shellcheck disable=SC2086
    [ -n "$BOOTCFG" ] && grep -h -E '^[[:space:]]*(dtoverlay|overlays|user_overlays|overlay_prefix)=' $BOOTCFG
    block_end
    block_start "/proc/cmdline"
    rd /proc/cmdline
    block_end

    h2 "Display"
    block_start "framebuffers"
    cat /proc/fb 2>/dev/null || echo "/proc/fb not readable"
    ls -l /dev/fb* /dev/dri 2>&1
    block_end
    echo
    echo "| fb | name | virtual_size | bits_per_pixel | stride | rotate |"
    echo "|---|---|---|---|---|---|"
    FB_SUM=""
    for d in /sys/class/graphics/fb[0-9]*; do
        [ -d "$d" ] || continue
        n=${d##*/}
        printf '| /dev/%s | %s | %s | %s | %s | %s |\n' "$n" "$(rd "$d/name")" "$(rd "$d/virtual_size")" \
            "$(rd "$d/bits_per_pixel")" "$(rd "$d/stride")" "$(rd "$d/rotate")"
        FB_SUM="$FB_SUM /dev/$n=$(rd "$d/name"):$(rd "$d/virtual_size" | tr , x)@$(rd "$d/bits_per_pixel")bpp"
    done
    if have fbset; then
        for f in /dev/fb[0-9]*; do [ -e "$f" ] && cmd "fbset -i -fb $f" fbset -i -fb "$f"; done
    fi
    echo
    echo "| DRM connector | status | enabled | first mode | dpms |"
    echo "|---|---|---|---|---|"
    DRM_SUM=""
    for c in /sys/class/drm/card*-*; do
        [ -d "$c" ] || continue
        st=$(rd "$c/status")
        mode=$(head -n 1 "$c/modes" 2>/dev/null)
        printf '| %s | %s | %s | %s | %s |\n' "${c##*/}" "$st" "$(rd "$c/enabled")" "$mode" "$(rd "$c/dpms")"
        [ "$st" = connected ] && DRM_SUM="$DRM_SUM ${c##*/}(${mode:-no mode})"
    done
    block_start "DRM cards and drivers"
    for c in /sys/class/drm/card[0-9]*; do
        case "${c##*/}" in *-*) continue ;; esac
        [ -e "$c" ] || continue
        drv=$(readlink "$c/device/driver" 2>/dev/null)
        echo "${c##*/}: driver ${drv##*/}"
    done
    have lsmod && lsmod 2>/dev/null | awk 'NR > 1 {print $1}' | grep -E 'vc4|v3d|drm|rp1|fb|sun4i|sunxi|panel|tinydrm|fbtft|mipi|dbi|hdmi|dsi|dpi' | tr '\n' ' '
    echo
    block_end

    h2 "Backlight and PWM"
    block_start "/sys/class/backlight, pwmchips"
    BL_SUM=""
    for b in /sys/class/backlight/*; do
        [ -e "$b" ] || continue
        max=$(rd "$b/max_brightness")
        echo "${b##*/}: type=$(rd "$b/type") max_brightness=$max brightness=$(rd "$b/brightness") actual=$(rd "$b/actual_brightness") bl_power=$(rd "$b/bl_power")"
        if [ "${max:-0}" -le 1 ] 2>/dev/null; then BL_SUM="$BL_SUM ${b##*/}(on/off only)"
        else BL_SUM="$BL_SUM ${b##*/}(dimmable 0..$max, $(rd "$b/type"))"; fi
    done
    [ -n "$BL_SUM" ] || echo "no backlight class device"
    for p in /sys/class/pwm/pwmchip*; do [ -e "$p" ] && echo "${p##*/}: npwm=$(rd "$p/npwm")"; done
    if have pinctrl; then pinctrl get 18 2>&1 | head -n 2
    elif have raspi-gpio; then raspi-gpio get 18 2>&1 | head -n 2; fi
    block_end

    h2 "Input"
    echo
    if [ -r /proc/bus/input/devices ]; then
        decode table < /proc/bus/input/devices
        INPUT_LIST=$(decode list < /proc/bus/input/devices)
    else
        echo "/proc/bus/input/devices not readable"
        INPUT_LIST=""
    fi
    TOUCH_SUM=$(echo "$INPUT_LIST" | awk -F'|' '$4 == 1 {printf " %s(%s, %s)", $1, $2, $5}')
    KBD_SUM=$(echo "$INPUT_LIST" | awk -F'|' '$3 == 1 {printf " %s(%s, %s)", $1, $2, $5}')
    block_start "/proc/bus/input/devices (raw)"
    cat /proc/bus/input/devices 2>&1 | grep -E '^(I|N|P|H|B): ' | mask_macs | head -n 200
    block_end
    if have udevadm; then
        block_start "udev properties per event node"
        for ev in /dev/input/event*; do
            [ -e "$ev" ] || continue
            props=$(udevadm info -q property -n "$ev" 2>/dev/null | grep -E '^(ID_INPUT[A-Z_]*|LIBINPUT_[A-Z_]+|ID_PATH)=' | tr '\n' ' ')
            echo "${ev##*/}: $props"
        done
        block_end
    fi
    cmd "libinput list-devices (often needs root; an error here is fine)" libinput list-devices
    block_start "uinput and permissions"
    if [ -e /dev/uinput ]; then ls -l /dev/uinput; else echo "/dev/uinput: missing"; fi
    id 2>/dev/null
    nr=0; nt=0
    for ev in /dev/input/event*; do [ -e "$ev" ] || continue; nt=$((nt + 1)); [ -r "$ev" ] && nr=$((nr + 1)); done
    echo "readable /dev/input/event*: $nr of $nt"
    for f in /dev/fb[0-9]*; do
        [ -e "$f" ] || continue
        if [ -r "$f" ] && [ -w "$f" ]; then echo "$f: read+write OK"; else echo "$f: NO read/write access"; fi
    done
    block_end

    h2 "Desktop session"
    block_start "compositor / display manager / on-screen keyboard processes, default target"
    PROCS=$(proc_names)
    DESK=$(echo "$PROCS" | match_procs "$DESKTOP_NAMES")
    OSK=$(echo "$PROCS" | match_procs "$OSK_NAMES")
    TARGET=""
    have systemctl && TARGET=$($TO systemctl get-default 2>/dev/null)
    echo "desktop processes:  ${DESK:-none}"
    echo "on-screen keyboard: ${OSK:-none}"
    echo "default target:     ${TARGET:-?}"
    if [ -n "$DESK" ]; then
        echo "A desktop owns the display (DRM master): /dev/fb* writes may not show."
        echo "For fb-test-pattern.py run over ssh: sudo systemctl stop lightdm (before), sudo systemctl start lightdm (after)."
    fi
    block_end

    h2 "Radios and network"
    cmd "rfkill list" rfkill list
    block_start "bluetooth"
    ls /sys/class/bluetooth 2>/dev/null || echo "no /sys/class/bluetooth"
    if have hciconfig; then hciconfig 2>&1 | head -n 12
    elif have bluetoothctl && [ -n "$TO" ]; then timeout 5 bluetoothctl show </dev/null 2>&1 | head -n 12; fi
    block_end
    block_start "network interfaces (MAC masked)"
    if have ip; then ip -br link 2>&1; else ls /sys/class/net; fi
    have nmcli && nmcli -t -f DEVICE,TYPE,STATE dev 2>&1
    block_end

    h2 "Audio, power, health"
    cmd "aplay -l" aplay -l
    block_start "power supply, rtc, uptime, thermal"
    for p in /sys/class/power_supply/*; do [ -e "$p" ] && echo "${p##*/}: type=$(rd "$p/type") capacity=$(rd "$p/capacity")"; done
    ls /dev/rtc* 2>/dev/null || echo "no /dev/rtc*"
    uptime 2>/dev/null
    for t in /sys/class/thermal/thermal_zone*; do
        [ -e "$t" ] && echo "${t##*/}: $(rd "$t/type") $(rd "$t/temp") mC"
    done
    if have vcgencmd; then vcgencmd get_throttled 2>&1; vcgencmd measure_temp 2>&1; fi
    block_end

    h2 "Kernel log (display, touch, backlight)"
    block_start "journalctl -k -b or dmesg, filtered, last 60 lines"
    klog=$(journalctl -k -b --no-pager 2>/dev/null)
    [ -n "$klog" ] || klog=$(dmesg 2>/dev/null)
    if [ -n "$klog" ]; then
        echo "$klog" | grep -i -E 'drm|fbdev|fb[0-9]+:|framebuffer|dpi|dsi|hdmi|goodix|edt_ft|ft5x|ili9|touch|ads7846|backlight|pwm|panel' | tail -n 60 | mask_macs
    else
        echo "kernel log not readable by this user (that is fine)"
    fi
    block_end

    h2 "Launcher"
    block_start "APPLaunch install and service"
    if [ -d /usr/share/APPLaunch ]; then
        LAUNCHER="installed"
        ls /usr/share/APPLaunch/bin 2>&1
        if have systemctl; then
            echo "enabled: $(systemctl --user is-enabled APPLaunch.service 2>&1)  active: $(systemctl --user is-active APPLaunch.service 2>&1)"
            $TO systemctl --user show APPLaunch.service -p Environment 2>&1 | tr ' ' '\n' | grep -E '^(Environment=)?(APPLAUNCH|LV_)'
            $TO systemctl --user --no-pager status APPLaunch.service 2>&1 | head -n 6
            journalctl --user -u APPLaunch.service -b --no-pager 2>/dev/null | grep '\[dpi\]' | tail -n 15
        fi
    else
        LAUNCHER="not installed"
        echo "/usr/share/APPLaunch: not present"
    fi
    block_end

    h2 "SUMMARY"
    echo
    echo '```'
    echo "board:      ${MODEL:-unknown}"
    echo "os:         ${OS:-?}, kernel $(uname -r 2>/dev/null), $(uname -m 2>/dev/null), dpkg $ARCH"
    echo "glibc:      ${GLIBC:-?} -> $GLIBC_OK (needs >= 2.38)"
    echo "page size:  ${PAGE:-?}$([ "${PAGE:-4096}" != 4096 ] && echo '  (not 4K: vfb header offset issue)')"
    echo "ram:        ${MEM:-?}, cpus $(nproc 2>/dev/null || echo ?)"
    echo "fbdev:      ${FB_SUM:- none}"
    echo "drm conn.:  ${DRM_SUM:- none connected}"
    echo "touch:      ${TOUCH_SUM:- none}"
    echo "keyboards:  ${KBD_SUM:- none}"
    echo "backlight:  ${BL_SUM:- none (no /sys/class/backlight)}"
    echo "uinput:     $([ -e /dev/uinput ] && echo present || echo missing)"
    BT=$(for b in /sys/class/bluetooth/*; do [ -e "$b" ] && printf '%s ' "${b##*/}"; done)
    echo "bluetooth:  ${BT:-none}"
    echo "boot cfg:   ${BOOTCFG:- none found}"
    echo "launcher:   ${LAUNCHER:-?}"
    echo "desktop:    ${DESK:-none} (${TARGET:-?})$([ -n "$DESK" ] && echo '  -> fbdev tests need lightdm stopped')"
    echo "osk:        ${OSK:-none}"
    echo '```'
}

STAMP=$(date +%Y%m%d-%H%M 2>/dev/null || echo now)
NAME="board-probe-$(hostname 2>/dev/null || echo board)-$STAMP.md"
OUT=""
for dir in . "$HOME" /tmp; do
    if [ -n "$dir" ] && [ -w "$dir" ]; then OUT="$dir/$NAME"; break; fi
done
if [ -n "$OUT" ]; then
    report 2>/dev/null | mask_macs | tee "$OUT"
    printf '\n(report saved to %s)\n' "$OUT" >&2
else
    report 2>/dev/null | mask_macs
    echo "(no writable directory: report not saved, copy it from the screen)" >&2
fi
exit 0
