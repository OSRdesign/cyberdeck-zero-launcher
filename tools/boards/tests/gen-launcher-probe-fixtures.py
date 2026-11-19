#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Writes the fixture trees for test-launcher-probe.sh (fake /proc, /sys, /boot, /etc below a PROBE_ROOT).

    python3 tools/boards/tests/gen-launcher-probe-fixtures.py            # rewrites tools/boards/tests/fixtures/

The trees are committed; this generator exists so that they can be rebuilt and reviewed (EDID blobs and kernel
capability bitmaps are not something to type by hand). Files are written with LF line endings.

Provenance of each tree (V = taken from the installer's recorded fixtures or from a measured board fact in the
repository, R = reconstructed from the profile facts and report 031, not recorded on the board):
  pi4b-dsi-portrait   R  a 480x800 DSI panel on a Pi 4 B (the owner's pi4b has an 800x480 panel; this one is the
                         portrait case) with a Goodix touch
  pi3a-wsdsi43-forced R+V Pi 3A+, Waveshare 4.3" DSI, forced vc4-kms-dsi-waveshare-800x480 (the installer fixture's
                         connector, framebuffer and backlight are V; the device-tree node and the touch ranges are R)
  hackberry-720       R+V HackBerry CM5 720x720 DPI, desktop (labwc), installed launcher (the installer fixture's
                         connector and framebuffer are V; ranges 0..719 and 2 contacts are report 021 / 031 facts)
  pc-hdmi-edid        R  an x86 PC with a 27 inch monitor over HDMI (a synthetic EDID with a serial number to be redacted)
  unknown-empty       -  nothing at all
"""
import os
import shutil
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "fixtures")


def put(root, path, data, binary=False):
    p = os.path.join(root, path)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    if isinstance(data, str):
        data = data.encode()
    with open(p, "wb") as f:
        f.write(data)


def txt(root, path, text):
    put(root, path, text if text.endswith("\n") else text + "\n")


def dts(root, path, *strings):
    put(root, path, b"".join(s.encode() + b"\0" for s in strings))


def u32(root, path, v):
    put(root, path, struct.pack(">I", v))


def bitmap(bits, wb=64):
    """A kernel capability bitmap string: hex words of wb bits, most significant first, only the top word unpadded."""
    if not bits:
        return "0"
    nw = max(bits) // wb + 1
    words = []
    for w in range(nw - 1, -1, -1):
        v = 0
        for b in bits:
            if b // wb == w:
                v |= 1 << (b % wb)
        words.append(v)
    out = ["%x" % words[0]] + ["%0*x" % (wb // 4, x) for x in words[1:]]
    return " ".join(out)


def edid(vendor, product, serial, week, year, w_cm, h_cm, hact, vact, pclk_khz, w_mm, h_mm, name, sn_text):
    b = bytearray(128)
    b[0:8] = bytes([0, 255, 255, 255, 255, 255, 255, 0])
    v = ((ord(vendor[0]) - 64) << 10) | ((ord(vendor[1]) - 64) << 5) | (ord(vendor[2]) - 64)
    b[8], b[9] = v >> 8, v & 255
    b[10], b[11] = product & 255, product >> 8
    b[12:16] = struct.pack("<I", serial)
    b[16], b[17], b[18], b[19] = week, year - 1990, 1, 4
    b[21], b[22] = w_cm, h_cm
    d = 54
    pc = pclk_khz // 10
    b[d], b[d + 1] = pc & 255, pc >> 8
    b[d + 2] = hact & 255
    b[d + 4] = (hact >> 8) << 4
    b[d + 5] = vact & 255
    b[d + 7] = (vact >> 8) << 4
    b[d + 12], b[d + 13] = w_mm & 255, h_mm & 255
    b[d + 14] = ((w_mm >> 8) << 4) | (h_mm >> 8)
    for off, tag, text in ((72, 0xFC, name), (90, 0xFF, sn_text)):
        b[off + 3] = tag
        t = (text + "\n").ljust(13)[:13].encode()
        b[off + 5:off + 18] = t
    b[126] = 0
    b[127] = (256 - sum(b[:127]) % 256) % 256
    return bytes(b)


def absinfo(codes):
    """The text the probe's python helper prints for EVIOCGABS (and the fixture file has the same text)."""
    names = {0: "X", 1: "Y", 24: "PRESSURE", 47: "MT_SLOT", 48: "MT_TOUCH_MAJOR", 53: "MT_POSITION_X", 54: "MT_POSITION_Y", 57: "MT_TRACKING_ID", 58: "MT_PRESSURE"}
    lines = []
    slot_max = None
    for code, (mn, mx, fuzz, flat, res) in sorted(codes.items()):
        lines.append("ABS %-15s min %d max %d fuzz %d flat %d res %d" % (names.get(code, code), mn, mx, fuzz, flat, res))
        if code == 47:
            slot_max = mx
    if slot_max is not None:
        lines.append("slots (ABS_MT_SLOT max + 1): %d" % (slot_max + 1))
    return "\n".join(lines)


def input_node(root, n, name, bus, vendor, product, ev, key, abs_=(), rel=(), prop=(), ranges=None):
    d = "sys/class/input/event%d/device" % n
    txt(root, d + "/name", name)
    txt(root, d + "/id/bustype", "%04x" % bus)
    txt(root, d + "/id/vendor", "%04x" % vendor)
    txt(root, d + "/id/product", "%04x" % product)
    txt(root, d + "/properties", bitmap(prop))
    txt(root, d + "/capabilities/ev", bitmap(ev))
    txt(root, d + "/capabilities/key", bitmap(key))
    txt(root, d + "/capabilities/abs", bitmap(abs_))
    txt(root, d + "/capabilities/rel", bitmap(rel))
    if ranges:
        txt(root, "probe-fixtures/event%d.absinfo" % n, absinfo(ranges))


KEYS = list(range(1, 249))
MT = [0, 1, 47, 48, 53, 54, 57]
EV_SYN, EV_KEY, EV_REL, EV_ABS = 0, 1, 2, 3


def env(root, **kw):
    txt(root, "../probe.env", "\n".join("%s=%s" % kv for kv in kw.items()))


def drm(root, card, name, status, modes, enabled="enabled", edid_bytes=None):
    base = "sys/class/drm/%s-%s" % (card, name)
    txt(root, base + "/status", status)
    txt(root, base + "/enabled", enabled if status == "connected" else "disabled")
    txt(root, base + "/dpms", "On" if status == "connected" else "Off")
    put(root, base + "/modes", "".join(m + "\n" for m in modes))
    if edid_bytes is not None:
        put(root, base + "/edid", edid_bytes)


def fb(root, n, name, size, bpp, stride, vinfo):
    base = "sys/class/graphics/fb%d" % n
    txt(root, base + "/name", name)
    txt(root, base + "/virtual_size", "%d,%d" % size)
    txt(root, base + "/bits_per_pixel", str(bpp))
    txt(root, base + "/stride", str(stride))
    txt(root, base + "/rotate", "0")
    txt(root, "probe-fixtures/fb%d.vinfo" % n, vinfo)


def vinfo(w, h, bpp, red, green, blue, transp, mm):
    return "vinfo: visible %dx%d virtual %dx%d bpp %d red %d/%d green %d/%d blue %d/%d transp %d/%d, driver mm %s (width x height)" % (
        w, h, w, h, bpp, red[0], red[1], green[0], green[1], blue[0], blue[1], transp[0], transp[1], mm)


RGB565 = ((11, 5), (5, 6), (0, 5), (0, 0))
XRGB = ((16, 8), (8, 8), (0, 8), (24, 8))


def os_release(root, pretty, vid):
    txt(root, "etc/os-release", 'PRETTY_NAME="%s"\nNAME="Debian GNU/Linux"\nVERSION_ID="%s"\nID=debian\n' % (pretty, vid))


def pi_common(root, model, compat, soc, mem_kb, kernel):
    put(root, "proc/device-tree/model", model.encode() + b"\0")
    dts(root, "proc/device-tree/compatible", *compat, soc)
    txt(root, "proc/sys/kernel/osrelease", kernel)
    txt(root, "proc/cpuinfo", "processor\t: 0\nprocessor\t: 1\nprocessor\t: 2\nprocessor\t: 3\nmodel name\t: ARMv8 Processor rev 4 (v8l)\n"
        "Hardware\t: BCM2835\nSerial\t\t: 10000000deadbeef\nModel\t\t: %s\n" % model)
    txt(root, "proc/meminfo", "MemTotal:       %d kB\nMemFree:         100000 kB\n" % mem_kb)
    dts(root, "proc/device-tree/serial-number", "10000000deadbeef")
    txt(root, "etc/hostname", "probehost")
    os_release(root, "Debian GNU/Linux 13 (trixie)", "13")


def pi4b_dsi_portrait():
    r = os.path.join(FIX, "pi4b-dsi-portrait", "root")
    env(r, PROBE_MACHINE="aarch64", PROBE_USER="pilot", PROBE_DATE="2026-11-19", NOTE="reconstructed")
    pi_common(r, "Raspberry Pi 4 Model B Rev 1.5", ["raspberrypi,4-model-b"], "brcm,bcm2711", 3884000, "6.12.47+rpt-rpi-v8")
    drm(r, "card1", "DSI-1", "connected", ["480x800"])
    drm(r, "card1", "HDMI-A-1", "disconnected", [])
    drm(r, "card1", "HDMI-A-2", "disconnected", [])
    drm(r, "card1", "Writeback-1", "unknown", [])
    fb(r, 0, "vc4drmfb", (480, 800), 16, 960, vinfo(480, 800, 16, *RGB565, "unknown"))
    # panel node, touch node, backlight regulator (names reconstructed)
    dts(r, "proc/device-tree/soc/dsi@7e700000/panel@0/compatible", "waveshare,4.0inch-dsi")
    u32(r, "proc/device-tree/soc/dsi@7e700000/panel@0/width-mm", 52)
    u32(r, "proc/device-tree/soc/dsi@7e700000/panel@0/height-mm", 86)
    dts(r, "proc/device-tree/soc/i2c@7e205000/touch@14/compatible", "goodix,gt911")
    u32(r, "proc/device-tree/soc/i2c@7e205000/touch@14/reg", 20)
    dts(r, "proc/device-tree/soc/i2c@7e205000/display-regulator@45/compatible", "raspberrypi,7inch-touchscreen-panel-regulator")
    txt(r, "boot/firmware/config.txt", "# For more options and information see\n# http://rptl.io/configtxt\ndtparam=audio=on\ncamera_auto_detect=1\n"
        "display_auto_detect=0\nauto_initramfs=1\ndtoverlay=vc4-kms-v3d\nmax_framebuffers=2\ndisable_fw_kms_setup=1\narm_64bit=1\n"
        "disable_overscan=1\n[cm4]\notg_mode=1\n[all]\ndtoverlay=vc4-kms-dsi-waveshare-panel,4_0_inch\ndtparam=i2c_arm=on\n")
    txt(r, "boot/firmware/cmdline.txt", "console=serial0,115200 console=tty1 root=PARTUUID=aaaaaaaa-02 rootfstype=ext4 fsck.repair=yes rootwait cfg80211.ieee80211_regdom=NL video=DSI-1:480x800@60")
    put(r, "boot/firmware/overlays/vc4-kms-dsi-waveshare-panel-overlay.dtbo", b"x")
    put(r, "boot/firmware/overlays/vc4-kms-dsi-7inch.dtbo", b"x")
    put(r, "boot/firmware/overlays/vc4-kms-v3d-overlay.dtbo", b"x")
    put(r, "boot/firmware/overlays/hyperpixel4.dtbo", b"x")
    txt(r, "sys/class/backlight/10-0045/brightness", "255")
    txt(r, "sys/class/backlight/10-0045/actual_brightness", "255")
    txt(r, "sys/class/backlight/10-0045/max_brightness", "255")
    txt(r, "sys/class/backlight/10-0045/type", "raw")
    txt(r, "sys/class/backlight/10-0045/bl_power", "0")
    input_node(r, 0, "Goodix Capacitive TouchScreen", 0x18, 0x0416, 0x0038, [EV_SYN, EV_KEY, EV_ABS], [330], MT, prop=[1],
               ranges={0: (0, 479, 0, 0, 0), 1: (0, 799, 0, 0, 0), 47: (0, 4, 0, 0, 0), 48: (0, 255, 0, 0, 0), 53: (0, 479, 0, 0, 0), 54: (0, 799, 0, 0, 0), 57: (0, 65535, 0, 0, 0)})
    txt(r, "probe-fixtures/kernel.log",
        "[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x410fd083]\n"
        "[    3.512345] vc4-drm gpu: bound fe600000.dsi (ops vc4_dsi_ops)\n"
        "[    3.612345] panel-simple-dsi 10.panel@0: probing panel waveshare,4.0inch-dsi\n"
        "[    3.712345] Goodix-TS 1-0014: ID 911, version: 1060\n"
        "[    4.012345] [drm] Initialized vc4 0.0.0 for gpu on minor 1\n"
        "[    4.112345] Console: switching to colour frame buffer device 60x50\n"
        "[    4.212345] vc4-drm gpu: [drm] fb0: vc4drmfb frame buffer device\n"
        "[    9.512345] vc4-drm gpu: hdmi hotplug seen by host probehost, mac aa:bb:cc:dd:ee:ff gateway 192.168.1.1 user pilot\n")


def pi3a_wsdsi43():
    r = os.path.join(FIX, "pi3a-wsdsi43-forced", "root")
    env(r, PROBE_MACHINE="aarch64", PROBE_USER="pilot", PROBE_DATE="2026-11-19", NOTE="reconstructed")
    pi_common(r, "Raspberry Pi 3 Model A Plus Rev 1.1", ["raspberrypi,3-model-a-plus"], "brcm,bcm2837", 427000, "6.12.47+rpt-rpi-v8")
    drm(r, "card0", "DSI-1", "connected", ["800x480"])
    drm(r, "card0", "HDMI-A-1", "disconnected", [])
    drm(r, "card0", "Writeback-1", "unknown", [])
    fb(r, 0, "vc4drmfb", (800, 480), 16, 1600, vinfo(800, 480, 16, *RGB565, "154x86"))
    # the overlay makes the kernel treat the panel like the official 7" one (report 031 section 2: 154x86 mm, or none)
    dts(r, "proc/device-tree/soc/dsi@7e700000/panel@0/compatible", "raspberrypi,7inch-dsi")
    dts(r, "proc/device-tree/soc/i2c@7e205000/touchscreen@38/compatible", "edt,edt-ft5406")
    u32(r, "proc/device-tree/soc/i2c@7e205000/touchscreen@38/reg", 56)
    dts(r, "proc/device-tree/soc/i2c@7e205000/regulator@45/compatible", "raspberrypi,7inch-touchscreen-panel-regulator")
    txt(r, "boot/firmware/config.txt", "dtparam=audio=on\ncamera_auto_detect=1\ndisplay_auto_detect=0\nauto_initramfs=1\ndtoverlay=vc4-kms-v3d\n"
        "max_framebuffers=2\ndisable_fw_kms_setup=1\narm_64bit=1\ndisable_overscan=1\n[all]\ndtoverlay=vc4-kms-dsi-waveshare-800x480\n")
    txt(r, "boot/firmware/cmdline.txt", "console=serial0,115200 console=tty1 root=PARTUUID=bbbbbbbb-02 rootfstype=ext4 rootwait consoleblank=0")
    put(r, "boot/firmware/overlays/vc4-kms-dsi-waveshare-800x480.dtbo", b"x")
    put(r, "boot/firmware/overlays/vc4-kms-dsi-7inch.dtbo", b"x")
    txt(r, "sys/class/backlight/10-0045/brightness", "255")
    txt(r, "sys/class/backlight/10-0045/actual_brightness", "255")
    txt(r, "sys/class/backlight/10-0045/max_brightness", "255")
    txt(r, "sys/class/backlight/10-0045/type", "raw")
    txt(r, "sys/class/backlight/10-0045/bl_power", "0")
    input_node(r, 0, "generic ft5x06 (79)", 0x18, 0x0000, 0x0000, [EV_SYN, EV_KEY, EV_ABS], [330], MT, prop=[1],
               ranges={0: (0, 799, 0, 0, 0), 1: (0, 479, 0, 0, 0), 47: (0, 9, 0, 0, 0), 53: (0, 799, 0, 0, 0), 54: (0, 479, 0, 0, 0), 57: (0, 65535, 0, 0, 0)})
    txt(r, "probe-fixtures/kernel.log",
        "[    3.100000] panel-simple-dsi 10.panel@0: probing panel raspberrypi,7inch-dsi\n"
        "[    3.300000] edt_ft5x06 1-0038: touchscreen probe\n"
        "[    3.900000] vc4-drm soc:gpu: [drm] fb0: vc4drmfb frame buffer device\n")
    # an installed launcher
    txt(r, "etc/applaunch/board.conf", "# Board profile for the launcher, written by install.sh (board: pi3a-wsdsi43).\n"
        "APPLAUNCH_BOARD=pi3a-wsdsi43\nAPPLAUNCH_FB=/dev/fb0\nAPPLAUNCH_LOGICAL=800x480\nAPPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/10-0045\n")
    txt(r, "etc/applaunch/install.conf", "board=pi3a-wsdsi43\nscreen_size=4.3\nkeyboard_name=Fixture Keyboard\nwifi_psk=never-print-this\n")
    put(r, "home/pilot/.config/systemd/user/cyberdeckzero.service", "[Unit]\n")
    put(r, "etc/systemd/system/cyberdeckzero-console.service", "[Unit]\n")
    put(r, "usr/share/APPLaunch/bin/cyberdeckzero", "x")
    txt(r, "run/user/1000/applaunch/board.state", "version=0.10.5\nboard=pi3a-wsdsi43\nbacklight=levels\nip=192.168.1.77\n")
    txt(r, "run/user/1000/applaunch/screen.state", "canvas=800x480\ntouch_slots=10\n")
    put(r, "proc/1/comm", "systemd\n")
    put(r, "proc/200/comm", "cyberdeckzero\n")


def hackberry():
    r = os.path.join(FIX, "hackberry-720", "root")
    env(r, PROBE_MACHINE="aarch64", PROBE_USER="hbuser", PROBE_DATE="2026-11-19", NOTE="reconstructed")
    pi_common(r, "Raspberry Pi Compute Module 5 Rev 1.0", ["raspberrypi,5-compute-module", "raspberrypi,5-model-b"], "brcm,bcm2712", 8040000, "6.12.47+rpt-rpi-2712")
    drm(r, "card0", "DPI-1", "connected", ["720x720"])
    drm(r, "card0", "HDMI-A-1", "disconnected", [])
    drm(r, "card0", "HDMI-A-2", "disconnected", [])
    fb(r, 0, "drm-rp1-dpidrmf", (720, 720), 32, 2880, vinfo(720, 720, 32, *XRGB, "unknown"))
    dts(r, "proc/device-tree/panel/compatible", "hackberry,720x720-dpi")
    dts(r, "proc/device-tree/axi/pcie@1000120000/rp1/i2c@74000/touchscreen@38/compatible", "edt,edt-ft5406")
    txt(r, "boot/firmware/config.txt", "dtparam=audio=on\ncamera_auto_detect=1\ndisplay_auto_detect=0\nauto_initramfs=1\ndtoverlay=vc4-kms-v3d\n"
        "max_framebuffers=2\narm_64bit=1\n[all]\ndtoverlay=hackberrypicm5\ndtparam=rotate=0\n")
    txt(r, "boot/firmware/cmdline.txt", "console=tty1 root=PARTUUID=cccccccc-02 rootfstype=ext4 rootwait quiet splash")
    put(r, "boot/firmware/overlays/hackberrypicm5.dtbo", b"x")
    input_node(r, 0, "EP0110M09", 0x18, 0x2a8f, 0x0110, [EV_SYN, EV_KEY, EV_ABS], [330], MT, prop=[1],
               ranges={0: (0, 719, 0, 0, 0), 1: (0, 719, 0, 0, 0), 47: (0, 1, 0, 0, 0), 53: (0, 719, 0, 0, 0), 54: (0, 719, 0, 0, 0), 57: (0, 65535, 0, 0, 0)})
    input_node(r, 1, "HackBerry Keyboard", 0x03, 0x1209, 0xb001, [EV_SYN, EV_KEY], KEYS, [], [], ranges=None)
    input_node(r, 2, "HackBerry Trackpoint", 0x03, 0x1209, 0xb002, [EV_SYN, EV_KEY, EV_REL], [272, 273, 274], [], [0, 1, 8])
    put(r, "proc/1/comm", "systemd\n")
    put(r, "proc/300/comm", "labwc\n")
    put(r, "proc/301/comm", "lightdm\n")
    put(r, "etc/systemd/system/display-manager.service", "[Unit]\n")  # a symlink in a real system; a file survives Windows checkouts
    put(r, "etc/systemd/system/cyberdeckzero-desktop.service", "[Unit]\n")
    put(r, "home/hbuser/.config/systemd/user/cyberdeckzero.service", "[Unit]\n")
    put(r, "usr/share/APPLaunch/bin/cyberdeckzero", "x")
    txt(r, "etc/applaunch/board.conf", "APPLAUNCH_BOARD=hackberry-cm5\nAPPLAUNCH_FB=/dev/fb0\nAPPLAUNCH_LOGICAL=720x720\n")
    txt(r, "etc/applaunch/install.conf", "board=hackberry-cm5\nvariant=desktop\n")
    txt(r, "probe-fixtures/kernel.log",
        "[    4.000000] vc4-drm axi:gpu: [drm] fb0: drm-rp1-dpidrmf frame buffer device\n"
        "[    4.200000] edt_ft5x06 6-0038: touchscreen probe failed\n")


def pc_hdmi():
    r = os.path.join(FIX, "pc-hdmi-edid", "root")
    env(r, PROBE_MACHINE="x86_64", PROBE_USER="pcuser", PROBE_DATE="2026-11-19", NOTE="reconstructed")
    txt(r, "proc/sys/kernel/osrelease", "6.12.48+deb13-amd64")
    txt(r, "proc/cpuinfo", "processor\t: 0\nprocessor\t: 1\nmodel name\t: Fixture CPU @ 2.00GHz\n")
    txt(r, "proc/meminfo", "MemTotal:       16000000 kB\n")
    txt(r, "etc/hostname", "probehost")
    os_release(r, "Debian GNU/Linux 13 (trixie)", "13")
    for k, v in (("sys_vendor", "FixtureCorp"), ("product_name", "Fixture Desktop 100"), ("chassis_type", "3"), ("bios_vendor", "FixBIOS"), ("product_serial", "SER-123456")):
        txt(r, "sys/class/dmi/id/" + k, v)
    e = edid("FIX", 0x1234, 0xCAFEBABE, 12, 2023, 60, 34, 1920, 1080, 148500, 598, 336, "FIXTURE-27", "SN4567890")
    drm(r, "card0", "HDMI-A-1", "connected", ["1920x1080", "1920x1080", "1280x720", "1024x768", "800x600", "720x480", "640x480"], edid_bytes=e)
    drm(r, "card0", "eDP-1", "disconnected", [])
    drm(r, "card0", "DP-1", "disconnected", [])
    fb(r, 0, "i915drmfb", (1920, 1080), 32, 7680, vinfo(1920, 1080, 32, *XRGB, "598x336"))
    txt(r, "sys/class/backlight/intel_backlight/brightness", "48000")
    txt(r, "sys/class/backlight/intel_backlight/actual_brightness", "48000")
    txt(r, "sys/class/backlight/intel_backlight/max_brightness", "96000")
    txt(r, "sys/class/backlight/intel_backlight/type", "raw")
    input_node(r, 0, "AT Translated Set 2 keyboard", 0x11, 0x0001, 0x0001, [EV_SYN, EV_KEY], KEYS)
    input_node(r, 1, "Fixture USB Optical Mouse", 0x03, 0x046d, 0xc077, [EV_SYN, EV_KEY, EV_REL], [272, 273, 274], [], [0, 1, 8])
    put(r, "proc/1/comm", "systemd\n")
    put(r, "proc/400/comm", "Xorg\n")
    put(r, "proc/401/comm", "gdm3\n")
    txt(r, "probe-fixtures/kernel.log",
        "[    2.000000] i915 0000:00:02.0: [drm] fb0: i915drmfb frame buffer device\n"
        "[    2.500000] i915 0000:00:02.0: drm display link up addr 10.0.0.42 hw 00:11:22:33:44:55 on probehost\n")


def unknown_empty():
    r = os.path.join(FIX, "unknown-empty", "root")
    os.makedirs(r, exist_ok=True)
    txt(r, ".gitkeep", "")
    env(r, PROBE_MACHINE="riscv64", PROBE_USER="nobody", PROBE_DATE="2026-11-19", NOTE="empty")


if __name__ == "__main__":
    shutil.rmtree(FIX, ignore_errors=True)
    for f in (pi4b_dsi_portrait, pi3a_wsdsi43, hackberry, pc_hdmi, unknown_empty):
        f()
    print("fixtures written to", FIX)
