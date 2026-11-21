# Board tests: launcher-probe (report 031, phase P0)

The owner runs these on real boards; the Controller sets up nothing. About 3 minutes per board. The probe only reads:
it changes nothing on the board and needs no `sudo`. Background: `tools/boards/README.md` section "launcher-probe",
`docs/dev/reports/031-smarter-installer-screen-test.md` (sections 2, 10 and 11).

## What to run, on each board

Copy `tools/boards/launcher-probe` and, for the last section of the report, the `projects/APPLaunch/pizero2w/install.sh`
of the same checkout (or unpack a release bundle and use its `install.sh`). Then, as the normal user:

```
sh launcher-probe --installer ./install.sh --out ~/probe-BOARD.txt
```

Take BOARD from this list: **pi52, pi4b, palmpi, zero8, deck, pi3a, pi5, hackberry**. (pi52 is the Pi 5 with the 7"
DSI panel, pi5 the Pi 5 with the HyperPixel 4", palmpi the Pi 3A+ with the Freenove 5", pi3a the Pi 3A+ with the
Waveshare 4.3".) The touch ranges need read access to `/dev/input`: add the user to the `input` group, or run
`sudo sh launcher-probe ...` once (it still only reads). If `python3` is missing the touch ranges and the pixel layout
say `unavailable`: that is a result, note it.

Put every output file into ONE folder (for example `C:\CLAUDE\zero7\board-tests-current\probe\`), named
`probe-BOARD.txt`, and tell the Controller. Look each file over for anything that identifies you before sending (it
redacts MACs, IPs, host and user names and serials, but read it).

## What to look for (note the answer per board)

1. **Runs to the end**: the last line is `==== END OF REPORT ====`, 11 sections, no shell error text. An `unavailable:`
   line is fine if its reason is true for that board.
2. **Board and session (sections 1, 2)**: model and SoC right; the launcher unit names that exist are the ones you
   expect (`cyberdeckzero.service` on the appliance boards, `cyberdeckzero-desktop.service` on pi52, pi5, hackberry; no
   `APPLaunch.service` left anywhere).
3. **Connectors (3)**: the panel's connector is `connected` with the mode you expect; HDMI connectors disconnected
   unless a monitor is plugged in. For an HDMI monitor, the EDID size in mm is plausible.
4. **Framebuffer (4)**: size, bpp and pixel layout match the board table (RGB565 on pi4b, palmpi, pi3a; 32 bpp on the
   Pi 5 boards and hackberry). **Open question (report 031, false positive 3):** on pi4b and palmpi, what is
   `driver mm`? 154x86 means the 7-inch overlay's size leaks through; `unknown` means the launcher falls back to its
   default density.
5. **Panel driver (5)**: the device-tree panel node and its `compatible`, `width-mm`, `height-mm` on the DSI boards. On
   pi3a (Waveshare 4.3", forced overlay) and palmpi: which `compatible` does the node show?
6. **Boot configuration (6)**: the display lines of `config.txt` are the ones you know. On pi4b, pi52 and palmpi: does
   `display_auto_detect=1` show, and what does the device-tree node say the kernel really loaded?
7. **Input (7)**: the touch device is `kind: touchscreen`; ranges (pi5 0..799 x 0..479 on a 480x800 buffer, hackberry
   0..719, zero8 0..319 x 0..479) and contact count (deck 5, pi3a DSI 10, hackberry 2). Does any touch driver show a
   non-zero `res` (report 031 unknown)?
8. **Backlight (8)**: pi3a and palmpi `10-0045` with levels; zero8 `backlight_gpio` or `backlight_pwm`; hackberry none.
   `writable by this user` is yes only where the installer's udev rule gave the video group access.
9. **What install.sh would pick (9)**: the board name equals the profile you installed (pi4b-dsi on pi4b, pi3a-wsdsi43
   on pi3a, and so on). Note a board where it picks wrongly: palmpi is known to pick the 4.3" profile.
10. **Installed files (10)**: `board.conf` and `install.conf` are what the board has; `board.state` and `screen.state`
    show only while the launcher runs.

Anything that looks wrong, or any identifying text that was not redacted: send the file as it is and say which line.
