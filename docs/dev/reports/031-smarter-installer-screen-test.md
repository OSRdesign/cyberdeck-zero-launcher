# 031 - Smarter installer for unknown boards, and testing the screen during installation

Study, 2026-11-18. Research and design only: no product code, no board access, no installs. Number: 031 was reserved
by the ROADMAP exploration idea ("A study first (report 031 when written)") and the reports index lists it as "never
written", so this report takes it. Status: owner decisions open (section 9).

Addendum 2026-11-20 (installer fix, branch installer-armbian, board facts from the Orange Pi Zero 2W probe): the installer
called an Orange Pi Zero 2W (Armbian, no config.txt) the Raspberry Pi "zero2w" and died at the boot configuration. Board
detection now needs a Raspberry Pi device tree (compatible raspberrypi / brcm,bcm), and the boot configuration step is
skipped, with a message, where there is no config.txt and cmdline.txt (section 4.3: Armbian boards use armbianEnv.txt).

Markers: **V** verified (this repository at `github/main` e3f3d382, or a primary source fetched on 2026-11-18, cited),
**M** measured on our boards (report 021 addenda, roadmap rows, the owner's board notes), **I** inferred, **[U]**
unverified (not checked; do not build on it without a probe).

## 0. Summary for the owner

- **The installer already does more than the roadmap idea says.** Since v0.10.4 it detects the board from the
  device-tree model, the DRM connectors and the framebuffers, reads the panel size from EDID on a PC, takes
  `--screen-size` / `--panel-mm` / a size question, and writes a generic profile with `--board generic` (or `--force`
  with one panel). What it cannot do is (1) know whether the picture, rotation, colours and touch are right, (2) change
  the boot configuration safely for an unknown panel, (3) tell apart panels that look the same to software.
- **What software can detect safely** (read-only, no extra package): the board model, every DRM connector and its
  modes, the EDID of HDMI/eDP screens, each framebuffer's size, depth and pixel layout, which panel driver and overlay
  the kernel actually loaded (device tree), the touch device with its ranges and contact count, and the backlight with
  its levels. **What it cannot detect**: the physical size of a DSI panel (the 4.3", 5" and 7" ones report the same,
  and the official-7" overlay even reports 154x86 mm for all of them), how the panel is mounted in the case, the colour
  wiring of the glass, whether backlight 0 is really black, and flicker.
- **Screen test during install (proposal).** Before the profile is written, a small test mode draws one test screen
  (a white border on the edges, a big F and arrows, red/green/blue bars) through the same display path the launcher
  uses, and asks "is the F upright in the top-left and is the white border visible on all four sides?"; then five
  touch targets (four corners and the centre) from which the touch settings are computed; then, where the backlight
  has levels, three brightness steps to check that dim is readable and whether zero is black. Answers come from the
  terminal, a keyboard on the board or a touch hold; no answer in 20 s counts as "no" and nothing is written.
- **Boot changes stay rare and become reversible.** On every Raspberry Pi the firmware has a one-shot "tryboot" mode
  (Raspberry Pi documentation: "All Raspberry Pi models support tryboot"): the proposed config goes into
  `tryboot.txt`, the board reboots into it once, the screen test asks "keep these settings?", and only a "yes" copies it
  over `config.txt`. A black or corrupted screen, a crash, a power cut or no answer all fall back to the old
  `config.txt` at the next boot, by the firmware itself. The Pi 3A+ corruption with `display_auto_detect=1` is the
  worked example: with this protocol it would have reverted by itself. Other boards (Armbian, x86): no automatic boot
  changes, instructions only.
- **Phases.** P0 a read-only probe that prints "what I would write and why" and saves a test fixture (S, about 1.5
  worker-days; most value per cost); P1 the screen test and touch math (M, 3-4); P2 the tryboot protocol (M, 2-3); P3 a
  panel catalog and installer integration (M-L, 3-5). Section 9 lists decisions D1-D13 (recommended option first) and
  one open design question, D14 (how the prompts look on the panel).

## 1. What exists today (baseline)

| Piece | Where | What it does (V unless marked) |
| --- | --- | --- |
| Board detection | `pizero2w/install.sh` 208-390 | `/proc/device-tree/model`; `/sys/class/drm/card*-*/{status,modes}`; `/sys/class/graphics/fb*/{name,virtual_size}`; for the DSI boards also `/sys/class/backlight/10-0045`. Never reads `config.txt` (comment at 276-283) |
| Profiles | `install.sh` `panel_spec()` 284, `profile_body_base()` 662 | `zero2w` (none), `pi3a-luckfox35`, `pi3a-wsdsi43`, `pi3a-wsdsi43-portrait` (never detected), `pi4b-dsi`, `pi5-hyperpixel4`, `pi5-dsi7`, `hackberry-cm5`, `pc-<model>`, `generic-<W>x<H>` |
| PC | `install.sh` 392-468 | DMI model, the `*drmfb` framebuffer, panel mm from the connected connector's EDID (`edid_mm()`, detailed timing then basic block), else a known-model table (FZ-M1: 150x93) |
| Generic profile | `install.sh` 303-349, 469-491, 803-833 | one connected DSI/DPI/SPI/eDP/LVDS connector whose mode matches a framebuffer (either orientation); portrait buffer gives `APPLAUNCH_ROTATE=90`; touch `auto` / `buffer`; the backlight as the DSI profiles find it; refuses with two panels or none |
| Screen size | `install.sh` 1066-1161, decisions 2026-11-15 | `--screen-size` (diagonal 1.5-15), `--panel-mm WxH` (wins), a question at a first install from a terminal; recorded in `install.conf` (`screen_size=`, `panel_mm=`) and replayed by the update helper |
| Boot changes | `install.sh` 1998-2081 | only: the deck's PWM backlight overlay, the 3.5" SPI panel's `backlight-pwm` on a Zero 2 W (automatic, no option), the HyperPixel PWM overlay (`--pwm-backlight`), two cmdline arguments. Backup `config.txt.bak-applaunch` / `cmdline.txt.bak-applaunch`, made **once** (the first-ever copy, never timestamped); the change is printed after it is made, not shown as a diff first; no trial boot. The panel overlay itself is never written ("Prerequisites that this script does NOT do", 46-47) |
| Update path | `cyberdeckzero-update` 78-85, 308-309 | replays `install.conf` options; stops when the plan would replace or remove `board.conf`; never passes `--force` |
| Probe | `tools/board-probe/probe-board.sh` | read-only Markdown report: model, OS, glibc, boot config, framebuffers, DRM connectors and drivers, backlight, input devices (bitmap decoder with `--selftest`), desktop session, kernel log |
| Test pattern | `tools/board-probe/fb-test-pattern.py` | raw framebuffer pattern (1 px border, red frame 20 px in, ticks every 100 px, big F, TOP/RIGHT arrows, R G B W K bars, grey gradient), raw touch marks, a 4-tap `--calibrate` that prints SWAP/INVERT and a fitted libinput matrix. Stale: it and the probe still name `APPLaunch.service` (renamed `cyberdeckzero.service` in v0.7.0) |
| Tests | `pizero2w/tests/test-install-board.sh`, `tests/fixtures/` | SYSROOT fixtures for 8 boards (model, DRM `status`/`modes`, fb `name`/`virtual_size`/`bits_per_pixel`, backlight); no EDID blob, no device-tree node beyond `model`, no input devices. 747 installer tests at v0.10.4 (ROADMAP) |

So the screen test is about half built (the pattern and the calibration math exist as a developer tool), and the
generic profile exists. Missing: a check that the result is right, a safe boot-change path, and a catalog.

## 2. What the installer can detect safely

All sources below are read-only. "Root" means the installer's own privileges (it runs under sudo); the probe runs as a
normal user and notes what it cannot read.

| Source | How to read | Package needed | Gives | Reliability | False positives | Cannot tell |
| --- | --- | --- | --- | --- | --- | --- |
| Board model | `/proc/device-tree/model`, `compatible` (`/sys/firmware/devicetree/base` is the same tree) | none | Pi model, CM5, Armbian boards | high (V, in use) | none seen | carrier board of a CM (the HackBerry is "Compute Module 5") |
| x86 identity | `/sys/class/dmi/id/{sys_vendor,product_name,chassis_type}` | none | PC model, tablet chassis (30, 32) | high (V, in use) | generic names on cheap PCs (I) | the panel |
| DRM connectors | `/sys/class/drm/cardN-<type>-<n>/{status,enabled,dpms,modes,connector_id,edid}` (kernel `drm_sysfs.c`, V) | none | connector type (DSI, DPI, SPI, HDMI-A, eDP, LVDS, Writeback), connected or not, the mode list | high for "connected" and modes (V, in use) | `status` is **writable** (it forces a re-probe): the probe must only read it. A DSI connector shows "connected" whatever panel is behind it (M: 4.3"/5"/7") | physical size, which panel model |
| EDID | the `edid` file of a connector; parse bytes 21-22 (cm) and the first detailed timing (bytes 66-68, mm), vendor and product (bytes 8-11) | none (`install.sh edid_mm()` already does it); `edid-decode` optional (Debian trixie and the Raspberry Pi archive, V) | HDMI and eDP screens: size in mm, name, preferred mode | good on monitors and laptop panels (M: FZ-M1) | cheap HDMI LCDs: missing or wrong size and modes (I; Waveshare says its 7" HDMI (C) may need `hdmi_cvt` set by hand, V) | DSI, DPI and SPI panels have no EDID (I) |
| DRM ioctl tools | `modetest` (package `libdrm-tests`, Debian trixie 2.4.124-2, V), `drm_info` (`drm-info` 2.6.0, V), `kmsprint` (`kms++-utils`: Raspberry Pi archive only, not in Debian trixie, V) | yes, none assumed on a Lite image [U] | connector `width_mm`/`height_mm`, the panel orientation property, CRTC state | good | same as the driver data below | same |
| Framebuffer | `/sys/class/graphics/fbN/{name,virtual_size,bits_per_pixel,stride,rotate}`; `FBIOGET_VSCREENINFO` (pixel bit fields, `width`/`height` in mm); `fbset -i` (package `fbset`) | none (a few lines of C or Python for the ioctl) | the buffer the launcher draws to: size, orientation, depth, RGB565 or XRGB layout | high (V, in use) | the DRM fbdev emulation copies the mm of the **first connected connector** (kernel `drm_fb_helper.c`, V): with HDMI and a panel both connected, the mm can be the monitor's (I). Overlays' `rotate` parameters do not rotate the fbdev buffer (M, report 021 addendum 7) | how the buffer is turned on the glass |
| Panel driver data (device tree) | `/proc/device-tree/**/compatible`, `width-mm`, `height-mm`, `rotation`, touch nodes (`edt-ft5406`, `goodix`), regulator nodes | none (strings and big-endian u32 cells) | which overlay the kernel **really** loaded, including one loaded by `display_auto_detect` (which `config.txt` does not name) | high for "what is loaded" (I) | the official-7" overlay `vc4-kms-dsi-7inch` uses `compatible = "raspberrypi,7inch-dsi"`, and panel-simple gives it 154x86 mm (V, kernel sources rpi-6.18.y), so a Waveshare 5" or 7" (whose wiki tells users to load that overlay, V) or the Freenove 5" reports 154x86. `vc4-kms-dsi-waveshare-800x480` declares `width-mm = <0>` (V): no size | the real panel behind a shared overlay |
| Firmware view | `vcgencmd get_config int` / `str` (package `raspi-utils-core`, Raspberry Pi OS) [U: output format not re-checked] | Pi only | the firmware's parsed `config.txt`, including `display_auto_detect` | good (I) | - | which overlay auto-detect chose [U] |
| Boot files | `/boot/firmware/config.txt`, `cmdline.txt`; Armbian `/boot/armbianEnv.txt` (V, Armbian docs); `/boot/firmware/overlays/*.dtbo` | none | intent; which overlays this image ships | medium: intent, not reality | `display_auto_detect=1` hides the real overlay | whether it worked |
| Kernel log | `journalctl -k -b` or `dmesg` (may need root: `kernel.dmesg_restrict`, I) | none | driver names: `panel-simple`, `panel-mipi-dbi`, `edt_ft5x06`, `goodix`, the attiny regulator, probe errors | medium | text changes between kernels (I) | - |
| Touch | `/proc/bus/input/devices` (capability bitmaps; decoder in `probe-board.sh`), `EVIOCGABS` on the node (min, max, fuzz, resolution), `ABS_MT_SLOT` maximum + 1, `INPUT_PROP_DIRECT`, udev `ID_INPUT_TOUCHSCREEN` | none (read access to `/dev/input/event*`: root or the `input` group) | the touch device, raw ranges, contact count (`touch_slots`), single- or multi-touch, bus | high for presence and slots (V, the launcher already computes `touch_slots`) | ranges need not match the buffer: the Pi 5 HyperPixel reports 0..799 x 0..479 on a 480x800 portrait buffer (M); a dead touch is not a wrong mapping (M: the Pi 3A+ DSI touch dead after a soft reboot; dead with a PiSugar fitted) | swap, inversion, offset relative to the picture; `resolution` is units per mm (kernel `input.h`, V) and gives the touch area in mm when the driver sets it; most I2C panel drivers leave it 0 [U] |
| Backlight | `/sys/class/backlight/*/{type,max_brightness,brightness,actual_brightness,bl_power}` | none | none, on/off (`max_brightness` 1), levels; the device name (`10-0045`, `backlight_gpio`, `backlight_pwm`, `intel_backlight`) | high (V, the launcher's report 030 logic) | - | whether 0 is black (the 4.3" DSI on the Pi 3A+ only dims at 0 and with `bl_power=4`, M), the lowest readable level (the 5" panel is black at 10 %, M) |
| Desktop session | process names (`labwc`, `Xorg`, `lightdm` ...), `systemctl get-default` | none | whether a compositor owns the display | high (V, in use) | - | - |

### Worked false positives

1. **Auto-detect can be wrong and can break the picture (M).** On the Pi 3A+ with the Waveshare 4.3" DSI panel,
   `display_auto_detect=1` in place of `dtoverlay=vc4-kms-dsi-waveshare-800x480` gave vertical streaks: the panel got
   the official 7" timings. The kernel sources show the difference (V): the 7" mode in panel-simple runs a 30 MHz pixel
   clock with a 131-pixel horizontal front porch, the Waveshare overlay 27.777 MHz with 59. The overlay README says of
   the Waveshare overlay: "It tries to look like the Pi 7" display, but won't accept some of the timings." Waveshare's
   own wiki for the 4.3" now gives `vc4-kms-dsi-7inch` first and `vc4-kms-dsi-waveshare-800x480` as a fix-up (V), and
   our README records that the 7" overlay makes this panel flicker. The same auto-detect drives the Freenove 5" on the
   Pi 4 B correctly (repository README). So the same detected facts lead to a good picture on one panel and a broken one
   on another: **only a look at the screen can tell.** A backup of `config.txt` let us recover.
2. **Three panel sizes, one set of facts (V, decisions 2026-11-15).** The 4.3", 5" and 7" DSI panels show the same
   ft5x06 touch at i2c 10-0038, the same 10-0045 regulator, the same 800x480 buffer. palmpi (a Pi 3A+ with the Freenove
   5") was detected as the 4.3" profile (M).
3. **The driver's mm can lie (I, from the sources).** When `APPLAUNCH_PANEL_MM` is absent the launcher takes the
   framebuffer's mm (`cp0_ui_density_resolve()`, `density=fbdev`, V). Through `vc4-kms-dsi-7inch` every 800x480 DSI
   panel reports 154x86, so a generic profile without a size on a 5" panel would get the 7" density, not the default
   density that `generic_note()` announces ("no panel size (the launcher's default density, large controls)"). Not
   measured; P0 checks it on pi4b and palmpi.
4. **Overlay rotation is not buffer rotation (M).** `rotation=90` on `mipi-dbi-spi` and `rotate=90` on the HyperPixel
   overlay leave the fbdev buffer portrait; the launcher must pre-rotate (report 021 addenda 7 and 8).
5. **Two framebuffers (M).** The uConsole showed a DSI panel and an HDMI screen at once: the profile must choose by
   connector, never "fb0" (the generic detection already refuses two panels).
6. **A desktop owns the screen (M).** Under labwc a framebuffer write may not show at all (report 021 addendum 6): a
   test pattern drawn there proves nothing.

**What no software can tell:** the visible size of a DSI/DPI/SPI panel (unless the overlay carries the right mm), the
side that is "up" in the case, a mirrored picture (rare), the colour order of the glass (a DPI or SPI panel can be BGR
while the buffer says RGB: the DPI overlays have an `rgb-order` parameter, V), whether backlight 0 is black, the lowest
readable level, and flicker.

## 3. A screen test during installation

### 3.1 Shape

One test program with two callers: `install.sh` before it commits a profile, and later (optional) Settings > Screen.

- **Where it draws.** Through the same display path the launcher will use, with the candidate profile's values
  (`APPLAUNCH_FB`, `APPLAUNCH_LOGICAL`, `APPLAUNCH_ROTATE`, touch settings): a test that draws by its own code could
  pass while the launcher's own rotation or touch mapping fails. Two ways to get that (decision D2): a test mode of the
  launcher binary (`cyberdeckzero --screen-test <candidate file>`, the real dpi-scaled backend, no Python), or the
  existing `fb-test-pattern.py` grown into the installer tool (Python 3 standard library, raw framebuffer; Python 3 is on
  Raspberry Pi OS Lite (I), on Armbian minimal [U]). The raw tool stays for boards where the launcher cannot start.
- **No compositor needed.** Console boards: draw on the framebuffer (stop `cyberdeckzero.service` first, restart it
  after; the tool must restore the screen on exit and on SIGTERM). Desktop boards: the launcher's desktop variant
  already stops the display manager while it runs, so the test does the same, with a trap that starts it again.
  Running the test from a terminal **inside** the desktop would kill that terminal with the display manager: there the
  test is skipped with a note ("run `sudo ./install.sh --check-screen` from ssh or a text console"). A window on the
  desktop (Wayland or X client) would test the compositor's path, which the launcher does not use, and needs a toolkit:
  not proposed (D12).
- **Two passes.** Pass 1 (only when the rotation is unknown, a generic profile): the pattern is drawn in buffer
  coordinates with rotation 0 and the question is "which corner is the F in, and does it read backwards?" (four
  corners plus "mirrored"); the answer gives `APPLAUNCH_ROTATE` directly. Pass 2: the pattern drawn with the candidate
  profile; question "is the F upright in the top-left, and do you see the white line on all four edges?"
- **Answers, three ways at once.** The terminal (ssh or the console: `y` / `n`, a number for a corner); a keyboard on
  the board read from evdev (Y, N, Enter, Esc, digits, arrows); the touch screen with a gesture that needs no
  calibration: **hold anywhere 2 s = yes**. The question is also drawn on the panel in large text, because when the
  installer runs on the panel's own console the pattern hides the terminal. How the prompt looks on the panel (layout,
  colours, wording on a 3.5" screen) is a design question for the owner and the Controller (D14), not settled here.
- **Timeout and revert.** No answer in 20 s is "no". The test writes nothing itself: it returns its results to the
  caller. `install.sh` writes or changes `board.conf` only after a "yes"; on "no" or a timeout it keeps the file that was
  there (a re-install) or, on a first install of an unknown board, stops as today unless `--board generic` / `--force`
  was given (then it writes the candidate and prints that it is unconfirmed). The backlight is put back to its starting
  level on every exit path.
- **Never by surprise.** Only from a terminal (stdin and stdout), never from the update helper, `--plan`, `--dry-run` or
  a script; `--no-screen-test` skips it; `--check-screen` runs it on demand (D3).

### 3.2 Patterns

| # | Pattern | Detects | Question / input | Result it can set | Keep? |
| --- | --- | --- | --- | --- | --- |
| 1 | 1 px white line on the reported edges, a frame 20 px inside, ticks every 100 px with their coordinate, the reported size printed | cropping or overscan, a wrong mode | "white line on all four edges?" | none: a cropped picture means a wrong mode or overlay, so the test fails and the candidate is not used | **minimum** (same screen as 2) |
| 2 | big F and "TOP-LEFT" in the top-left corner, an arrow pointing up (TOP) and one pointing right (RIGHT) | rotation (0/90/180/270) and mirroring | pass 1: which corner; pass 2: upright yes/no | `APPLAUNCH_ROTATE`; a mirror has no launcher setting: report it and stop | **minimum** |
| 3 | R, G, B, W, K bars with labels, a grey gradient | colour order (BGR panel), depth (visible steps = 16 bpp) | "is the bar labelled R red?" | none in the launcher: a BGR panel needs the overlay's `rgb-order` (DPI) or the panel init (SPI); record it and give the hint. Steps on RGB565 are normal | **minimum as a passive check** (one more yes/no on the same screen) |
| 4 | a ruler: a bar whose length the owner adjusts (arrow keys, `+`/`-`, or dragging) until it matches 50 mm on a real ruler, or the long side of a bank card (ISO/IEC 7810 ID-1, 85.60 x 53.98 mm) on panels wide enough (4.3" and up; a 3.5" panel's 79x49 mm is too small for a card) | px per mm, so the panel's mm | adjust, then Enter | `APPLAUNCH_PANEL_MM` (the other axis from square pixels, or a second vertical bar: the official 7" pixels are 0.1926 x 0.1790 mm, about 8 % apart, V from its 154.08 x 85.92 mm area) | optional: offered when the diagonal answer is "don't know" |
| 5 | touch targets: four corners (10 % in) and the centre, tapped in order; then a free line drawn with the finger, echoed live | axis swap, inversion, offset, scale; dead touch | taps (30 s each) | `APPLAUNCH_TOUCH_ORIENT=buffer` when the mapping is the identity in buffer coordinates, else `legacy` with `APPLAUNCH_TOUCH_SWAP_XY` / `INVERT_X` / `INVERT_Y` (section 3.3) | **minimum** (5 taps); the line is optional |
| 6 | two-finger, then three-finger hold | the contact count | hold | none: the launcher computes `touch_slots` itself (screen.state); the test only confirms it, and says when the three-finger switcher gesture cannot work (HackBerry and FZ-M1 report 2 contacts, M / [U]) | optional |
| 7 | brightness ramp: full, the candidate dim level, the lowest level, zero, back to full, each 2 s, through the same `sysfs:` / `gpio:` logic as the launcher | backlight works; dim readable; zero black | "readable at this level?", "fully dark at zero?" | `APPLAUNCH_DIM_PERCENT` (the lowest readable step, e.g. 20 on the 5" panel) and a "zero is not black" fact (D11) | **recommended** where the backlight has levels; on/off: one on/off check |
| 8 | moving vertical bar, 5 s | flicker, tearing, a wrong pixel clock | "does the picture shake or flicker?" | none: a "yes" fails the candidate | optional; the 4.3" flicker with the 7" overlay was visible from the boot anyway (README) |

**Minimum set for a hobby installer** (owner rule: the main path must work): one combined static screen (patterns 1,
2, 3) with its questions, the five touch taps, and on a levels backlight three brightness steps. About one minute.
The existing diagonal question stays the size input; the ruler is the fallback for "don't know".

### 3.3 Touch math

- Normalise each raw sample by the device's own range: `u = (x - min) / (max - min)`, same for `v` (never assume pixels:
  the HyperPixel case above).
- The five targets have known buffer coordinates (the candidate rotation maps the logical target into the buffer, as
  the launcher does). Fit `[x_buf y_buf] = A [u v 1]` (a 2x3 affine matrix, six unknowns) by least squares over five
  points; the residual is the quality check. The existing `--calibrate` fits four points and prints the same matrix; the
  centre tap adds one degree of freedom that exposes offset and non-linear panels.
- **Snap** the fitted matrix to the nearest of the eight pure orientations (swap or not, invert x or not, invert y or
  not) when the largest residual is under a tolerance (proposal: 5 % of the short side, or 4 mm when the density is
  known). Identity in buffer coordinates gives `APPLAUNCH_TOUCH_ORIENT=buffer`, which is what every board of ours
  measured (report 021 addendum 8); any other snap gives `legacy` with the three SWAP/INVERT values (the launcher's
  convention: normalise, swap, invert, then into buffer pixels, then the display rotation).
- **No snap** (offset or scale, typical of resistive ADS7846 panels): the launcher has no matrix key today
  (`cp0_lvgl_dpi_scaled.c` reads SWAP/INVERT only, V). Options (D9): document the kernel overlay parameters (`ads7846`
  `xmin`/`xmax`/`ymin`/`ymax`, `edt-ft5406` `invx`/`invy`/`swapxy`, V), or add `APPLAUNCH_TOUCH_MATRIX` (six floats in the
  libinput order, documented by libinput as `"a b c d e f"`, `"0 -1 1 1 0 0"` = 90 degrees clockwise, V).
- When `EVIOCGABS` reports a non-zero `resolution`, `(max - min) / resolution` is the touch area in mm: a free cross-check
  of the panel size (I; which drivers set it is [U]).
- Two-finger check: count distinct `ABS_MT_TRACKING_ID`s down at the same time; compare with `ABS_MT_SLOT` max + 1.

## 4. Safety for boot changes

### 4.1 Rules

1. **Two kinds of change.** Kind A, harmless to the picture (the two cmdline arguments of today): timestamped backup,
   shown diff, done. Kind B, anything that can change the display (`dtoverlay`, `dtparam` of a panel overlay,
   `display_auto_detect`, `video=`): only through the trial boot below. Today's kind B edits (deck PWM backlight, the
   3.5" panel's `backlight-pwm` on a Zero 2 W, the HyperPixel PWM overlay) are tested on our boards; whether they move to
   the trial protocol is decision D6.
2. **Timestamped backup before any write**: `config.txt.cyberdeckzero-YYYYmmdd-HHMMSS` (and the same for `cmdline.txt`,
   `armbianEnv.txt`), next to the file, on the FAT partition so that any PC can read it. The existing first-ever copy
   `config.txt.bak-applaunch` stays untouched as "the original".
3. **Show the exact diff** (`diff -u` of the old and the proposed file) and **require a typed `y`** from a terminal.
   Never from the update helper, `--plan`, `--dry-run` or a script; no unattended boot change exists.
4. **Auto-detect and catalog entries are proposals.** `display_auto_detect=1` or an overlay from the catalog becomes
   permanent only after the screen test said "yes" **in the trial boot**.
5. **A recovery note is printed at the end and saved** as `/boot/firmware/CYBERDECKZERO-RECOVERY.txt`: which backup to
   copy over which file, from the board or from a PC with the SD card.

### 4.2 The trial boot on a Raspberry Pi (`tryboot`)

The Raspberry Pi documentation (boot flow, "Fail-safe OS updates (tryboot)", V): "The bootloader/firmware provide a
one-shot flag which, if set, is cleared but causes `tryboot.txt` to be loaded instead of `config.txt` ... Since the
flag is cleared before starting the firmware, a crash or reset will cause the original `config.txt` file to be loaded
on the next reboot." It is set with `sudo reboot '0 tryboot'`. "All Raspberry Pi models support tryboot, however, on
Raspberry Pi 4 Model B revision 1.0 and 1.1 the EEPROM must not be write protected."

Protocol:

1. Write `tryboot.txt` = the whole current `config.txt` with the proposed change, plus `cmdline=cmdline-trial.txt`
   (the `cmdline=` property selects another command-line file, V, autoboot documentation) where `cmdline-trial.txt` is
   `cmdline.txt` plus one marker word, `cyberdeckzero.trial=1`.
2. Enable a one-shot unit `cyberdeckzero-trial.service` (ordered before the launcher units) and reboot with
   `reboot '0 tryboot'`.
3. At the trial boot the unit sees the marker in `/proc/cmdline` (works on every model; the bootloader's own flag at
   `/proc/device-tree/chosen/bootloader/tryboot` is documented for the autoboot flow, and whether the Pi 3A+ and Zero 2 W
   firmware fill it is [U]). It runs the screen test: "keep these settings?", 30 s, with the same three answer channels.
4. **Yes**: copy `tryboot.txt` over `config.txt` through a temporary file, `fsync`, rename (whether a rename on the FAT
   boot partition survives a power cut cleanly is [U]: hence the backup), remove the marker files, disable the unit,
   start the launcher. **No, or no answer**: plain `reboot`: the firmware loads the untouched `config.txt`.
5. **Black or corrupted picture**: the owner cannot answer, the timeout reboots into the old configuration. The install
   output said beforehand: "If the screen stays dark or garbled, wait one minute, or unplug the board: the old settings
   come back by themselves."

The Pi 3A+ case under this protocol: the installer proposes `display_auto_detect=1`, the trial boot shows streaks, the
owner cannot read the prompt (or answers no), the timeout reboots, `config.txt` with
`vc4-kms-dsi-waveshare-800x480` is back. No SD card in a PC, no hand restore.

Limits: `tryboot.txt` replaces `config.txt` as a whole (conditional sections and `include` lines are copied as they
are); a kernel panic during the trial boot needs a power cycle (the flag is already cleared, so that also reverts).

### 4.3 Boards without such a scheme

- **x86 (FZ-M1 and other PCs):** GRUB and the kernel command line are never changed (decision X10). Nothing to do.
- **Armbian on ARM boards** (`/boot/armbianEnv.txt` with U-Boot, V for the file): no firmware trial mode. A boot
  counter could be emulated (a unit early at boot counts boots in a file on `/boot`; if no "yes" clears it within two
  boots it restores the backup and reboots), but when the kernel or U-Boot itself fails no user-space unit runs and the
  SD card must go into another computer. U-Boot's own `bootcount`/`bootlimit` exist in U-Boot, and whether Armbian's
  boot script uses them is [U]. Recommendation: print the proposed lines, the diff and the recovery note; let the owner
  edit (D7).

## 5. Panel catalog

### 5.1 Format

A plain text file in the bundle, `panels.conf`, one stanza per panel, `key=value` lines, read by `awk` (no `jq` on a
Lite image [U]; the same style as `board.conf` and `install.conf`). Proposed keys:

| Key | Meaning |
| --- | --- |
| `id`, `name`, `vendor` | `waveshare-dsi-4.3`, "Waveshare 4.3inch DSI LCD", vendor |
| `iface`, `buffer` | `dsi` / `dpi` / `spi` / `hdmi` / `edp`; native buffer `WxH` |
| `detect` | the facts that select it, ANDed: `model=*Pi 3*`, `conn=DSI`, `mode=800x480`, `fb=vc4drmfb`, `dt=waveshare,4-3-inch-dsi`, `backlight=10-0045`, `touch=edt_ft5x06`, `edid=<vendor:product>` |
| `family` | entries that cannot be told apart share a family (`rpi-dsi-800x480`); a family match asks the size (diagonal question or ruler) |
| `boot` | the exact `config.txt` lines, or `none` when the panel needs nothing; `boot_kind=A` or `B` (section 4.1) |
| `rotate`, `touch` | the usual `APPLAUNCH_ROTATE`; `buffer` or `legacy:swap,invx,invy` |
| `panel_mm`, `diagonal`, `mm_source` | visible area, nominal diagonal, where the mm come from (`datasheet`, `ruler`, `overlay`, `guess`) |
| `backlight`, `dim_percent`, `zero_dark` | `levels:<dir>` / `onoff:<dir>` / `none` / `usb`; lowest readable level; `yes` / `no` |
| `notes` | known problems (flicker with another overlay, auto-detect corruption) |
| `status`, `verified_by`, `verified_on`, `source` | `verified` (run on our hardware, with the board and date) or `docs` (from a primary source, URL) or `unknown` |

**Updates.** The file ships in each release bundle and is installed with the launcher, so Settings > Updates brings
new entries (D8: a separate download from GitHub is possible but adds a second trust path).

**Contributors.** The screen test ends by printing a ready-made stanza with the facts it saw and the answers given
("send this with your probe report"). A contributor adds it by pull request with the probe report attached; it enters
as `status=docs` or `unknown` and becomes `verified` only when someone with the panel ran the screen test on it. A unit
test checks the file (required keys, known `status` values, every `dtoverlay=` name present in a snapshot list of the
kernel's overlay README).

### 5.2 Initial entries

From our own profiles and boards (status `verified`, with the board):

| Entry | Detected by (repository) | Boot lines | Buffer, rotate | mm | Touch | Backlight | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Waveshare 2.8" DPI (the deck, Zero 2 W) | model "Zero 2" | legacy firmware `dpi24`, `waveshare-28dpi-3b-4b`, `-3b`, `-4b`, `display_rotate=1` (`config.txt.snippet`) | 640x480 landscape, 32 bpp, rotate 0 | built in (2.8") | Goodix, the deck's legacy axes | PWM, our `waveshare-pwm-backlight` overlay | no profile (`zero2w`) |
| Luckfox 3.5" ST7796S SPI (Pi 3A+) | `SPI-*` 320x480, `panel-mipi-dbi` fb | `mipi-dbi-spi` with `width=320,height=480,width-mm=49,height-mm=79`, Goodix 0x5d (report 021 addendum 2) | 320x480 portrait, 16 bpp, rotate 90 | 79x49 (the overlay's) | Goodix, buffer | `backlight_gpio` on/off | `fbcon=map:10,rotate:1` on that board |
| Waveshare 3.5" RPi LCD (F) (zero8, Zero 2 W) | as above, `--board pi3a-luckfox35` | Waveshare wiki: `mipi-dbi-spi,speed=48000000`, `compatible=st7796s\0panel-mipi-dbi-spi`, `width-mm=49,height-mm=79`, `goodix,addr=0x5d` (V) | as above | 79x49 | Goodix, buffer | `backlight-pwm` (16 levels) set by install.sh | - |
| Waveshare 4.3" DSI (pi3a-dsi) | `DSI-*` 800x480, `vc4drmfb` | `vc4-kms-v3d`, `vc4-kms-dsi-waveshare-800x480` | 800x480, 16 bpp, rotate 0 | 94x56 ("to be confirmed with a ruler") | ft5x06 10-0038, buffer, 10 slots | `10-0045` levels; **zero is not black** | `display_auto_detect=1` corrupts; `vc4-kms-dsi-7inch` flickers |
| Freenove 5" DSI (pi4b; palmpi) | as above + `10-0045` on a Pi 4 B | `display_auto_detect=1` (Pi 4 B) | 800x480, rotate 0 | 108x65 ("a guess") | ft5x06, buffer | `10-0045`, dim 20 (black at 10 %) | detected as the 4.3" on a Pi 3A+ |
| Official Raspberry Pi 7" Touch Display (pi5-dsi7 / pi5b) | `DSI-*` 800x480, `drm-rp1-dsi` + `10-0045` | `display_auto_detect=1` | 800x480, 32 bpp, rotate 0 | 154x86 (docs: 154.08 x 85.92, V) | ft5x06, buffer | `10-0045`, dim 20 | - |
| Pimoroni HyperPixel 4 (pi5) | `DPI-*` 480x800, `drm-rp1-dpi` | `vc4-kms-dpi-hyperpixel4` (V, README; the board has `rotate=90`) | 480x800 portrait, 32 bpp, rotate 90 | not measured | Goodix, buffer (touch range 0..799 x 0..479) | `gpio-backlight` on/off; levels with our `hyperpixel4-pwm-backlight` | - |
| HackBerry CM5 720x720 | `DPI-*` 720x720 | `hackberrypicm5` (vendor overlay) | 720x720, rotate 0 | not measured | EP0110M09 (edt_ft5x06), buffer, 2 contacts | none (a button) | desktop board |
| Panasonic FZ-M1 mk2 (x86) | DMI `FZM1-2*` | none (GRUB untouched) | 1280x800 eDP, rotate 0 | 150x93 (measured; EDID) | I2C-HID multitouch, buffer, 2 contacts reported [U] | `intel_backlight` | desktop board |

Popular panels, status `docs` (overlay names from the kernel overlay README of `rpi-6.18.y`, V, unless marked):

| Entry | Boot lines | Buffer | mm / size | Source |
| --- | --- | --- | --- | --- |
| Raspberry Pi Touch Display 2, 5" | `vc4-kms-dsi-ili9881-5inch` (Compute Modules; other Pis auto-detect it) | 720x1280 portrait | active area 62 x 110.5 mm | raspberrypi.com documentation (V) |
| Raspberry Pi Touch Display 2, 7" | `vc4-kms-dsi-ili9881-7inch` | 720x1280 portrait | 87 x 154.5 mm | same (V) |
| Raspberry Pi Touch Display 2, 10" | `vc4-kms-dsi-ili79600-10-1inch` (the docs also ask for an OS and EEPROM update first) | 1200x1920 | 135.4 x 216.6 mm | same (V) |
| Waveshare 5" DSI LCD, 7" DSI LCD (800x480) | the wiki says `vc4-kms-dsi-7inch` (`,dsi0` for the other port) | 800x480 | not on the page; the overlay reports 154x86 whatever the size | Waveshare wiki (V) |
| Waveshare 7.9" DSI LCD | `vc4-kms-dsi-waveshare-panel,7_9_inch` | 400x1280 (the wiki says "400x1480" in one line and "400 x 1280" in its title) | [U] | README and Waveshare wiki (V) |
| Waveshare DSI family (2.8" 480x640, 4.0" 480x800, 4.0" C 720x720, 5.0" 720x1280, 7.0" C 1024x600, 7.0" H 1280x720, 8.0" and 10.1" 1280x800, more) | `vc4-kms-dsi-waveshare-panel,<size>` (and the `-v2` overlay's list) | as named | [U] | README (V) |
| Waveshare 2.8" DPI on KMS | `vc4-kms-dpi-2inch8` (a dtbo Waveshare supplies; not in the kernel README) | 480x640 | [U] | Waveshare wiki (V) |
| Pimoroni HyperPixel 4 Square, HyperPixel 2 Round | `vc4-kms-dpi-hyperpixel4sq`, `vc4-kms-dpi-hyperpixel2r` (with `touchscreen-inverted-x/y`, `touchscreen-swapped-x-y`, `rotate`) | [U] | [U] | README (V) |
| Waveshare 5" HDMI LCD (resistive) | `hdmi_cvt 800 480 60 6 0 0 0` (legacy) or `video=HDMI-A-1:800x480M@60`; touch `ads7846` with `xmin`/`xmax`/`ymin`/`ymax` | 800x480 | [U] | Waveshare wiki (V) |
| Waveshare 7" HDMI LCD (C) | EDID, else `hdmi_cvt 1024 600 60 6 0 0 0` | 1024x600 | [U] | Waveshare wiki (V): USB 5-point touch, brightness over USB (no sysfs backlight, I) |
| Generic DPI / DSI panels | `vc4-kms-dpi-generic` / `vc4-kms-dsi-generic` with timings and `width-mm`/`height-mm` (DPI also `rgb-order`) | as set | from the parameters | README (V) |
| Other Luckfox panels | [U] | [U] | [U] | not checked |

## 6. The generic profile

**What a profile holds** (repository): `APPLAUNCH_BOARD`, `APPLAUNCH_FB`, `APPLAUNCH_LOGICAL`, `APPLAUNCH_ROTATE`,
`APPLAUNCH_COMPAT_SCALE`, `APPLAUNCH_PANEL_MM`, `APPLAUNCH_TOUCH_DEV`, `APPLAUNCH_TOUCH_ORIENT`, `APPLAUNCH_BACKLIGHT`,
`APPLAUNCH_DIM_PERCENT`, `LV_LINUX_FBDEV_DEVICE`, the neutral `APPLAUNCH_TOUCH_SWAP_XY/INVERT_X/INVERT_Y`, and per board
`APPLAUNCH_DISPLAY`, `APPLAUNCH_KBD_IGNORE`, `APPLAUNCH_HIDE_TILES`, `APPLAUNCH_GAUGE`, `APPLAUNCH_HINT_*`, `APPLAUNCH_PDA`.

**Derivation, in order:**

1. **Facts** (section 2): the one panel (connector + framebuffer), model, backlight, touch, device-tree panel node, EDID.
2. **Candidate** by today's rules (`generic_detect()`): landscape canvas of the buffer, `ROTATE=90` for a portrait
   buffer, compat scale 1 under 640 px wide else 2, touch `auto` / `buffer`, the backlight found.
3. **Catalog** (P3): a match fills boot lines, rotation, touch, mm, dim, `zero_dark`; a family match asks the size.
4. **Screen test** (P1): confirms or corrects rotation and touch, sets dim, records `zero_dark`.
5. **Recorded** (below).

**The size, one fact with several writers.** Precedence when the profile is written: `--panel-mm` > the ruler of the
screen test > `--screen-size` or the size question > the catalog's mm > EDID mm (HDMI, eDP) > the device-tree mm only
when the panel node is not a shared one (not `raspberrypi,7inch-dsi` behind a non-official panel) > none. At run time
the planned Settings > Screen size (roadmap row, not built) should store its choice per user (`config.json`, as the PDA
mode does) and win over `board.conf`, exactly as "the stored choice wins over `board.conf` and the install profile" for
PDA mode (v0.8.9). The installer and Settings then never write each other's file.

**Recording confirmed values.** Proposal (D10): the screen test's results become recorded install options, like
`screen_size=` today: `rotate=`, `touch=` (`buffer` or `legacy:1,0,1`), `dim_percent=`, `zero_dark=`, `screen_checked=`
(date) in `install.conf`, applied by `install.sh` to the generated profile (the way `SCREEN_APPLY` replaces the size
line), replayed by the update helper. The profile stays "ours" (checksummed), so later profile fixes still arrive, and
a hand-edited `board.conf` still wins.

**What stays refused:** glibc older than 2.38 (existing check); a bundle of the wrong architecture (arm64 vs amd64,
existing check); no framebuffer at all (a DRM-only board such as the Walnut Pi probe: the launcher draws on fbdev);
two panels without `--board` (existing); the deck and the size options (existing); a 32-bit (armhf) system (I: no
bundle); a board where the screen test failed and no `--force` was given.

**The roadmap goal "instead of stopping without --force"** (D13): proceed without `--force` only when exactly one panel
is found, the install runs from a terminal and the screen test passed; otherwise stop as today and print the facts.

## 7. Phases

| Phase | Content | Size (worker-days) | Value |
| --- | --- | --- | --- |
| **P0** read-only probe | `install.sh --probe` (or `probe-board.sh` v2): adds device-tree panel nodes (compatible, `width-mm`/`height-mm`), `EVIOCGABS` ranges, resolution and slots per touch device, fbdev mm and bit fields, EDID decoded (the `edid_mm()` code plus vendor and product) and raw hex, overlays shipped, `vcgencmd get_config` where present; prints "install.sh would write this profile, for these reasons"; `--fixture DIR` saves the files it read as a SYSROOT tree for the installer tests. Fix the stale `APPLaunch.service` names. Never writes | S (1-2) | **highest per cost**: every board the owner (or a user) probes becomes a test fixture and a catalog candidate; answers the open [U]s below |
| **P1** screen test + touch math | the test mode (D2), the minimum set of section 3.2, pass 1 rotation answer, 5-tap fit and snap, brightness steps, the three answer channels, timeout; `install.sh --check-screen`, then by default on a first install of a generic or family profile from a terminal (D3); results recorded in `install.conf` (D10) | M (3-4) | **biggest visible value**: catches a wrong rotation, touch or dim level before the owner meets a broken launcher |
| **P2** safe boot changes | timestamped backups, diff, confirmation, recovery file, the tryboot trial with `cyberdeckzero-trial.service`; Armbian print-only | M (2-3) | needed only once the installer proposes overlays (P3); without P3 it only protects today's three PWM edits |
| **P3** catalog + integration | `panels.conf`, matcher, family size question, contributor stanza, format test, the precedence of section 6, the D13 behaviour, Settings > Screen size coordination | M-L (3-5) | turns "My screen is not listed" into a guided path for the listed panels |

Order: **P0, then P1**; P2 and P3 together, and only if the owner wants the installer to propose boot lines at all
(D6 option "never" makes P2 almost empty).

## 8. Tests without boards

| Part | How to test with recorded fixtures | Data we already have |
| --- | --- | --- |
| Detection, candidate profile | `test-install-board.sh` with SYSROOT trees, as today; add `edid` files, `proc/device-tree` panel nodes (strings, big-endian u32 for mm), `proc/bus/input/devices` | 8 fixture trees; the Goodix sample in `probe-board.sh`; report 021 addenda (connectors, buffers, overlays per board) |
| EDID parsing | 128-byte blobs written by a small generator (header, size bytes, a detailed timing, checksum); a real FZ-M1 blob after P0 | none in the repository |
| Touch ranges and slots | `EVIOCGABS` cannot be faked in sysfs: the probe's helper reads an `absinfo` fixture file under a test hook (as `INSTALL_MACHINE` and `SYSROOT` are hooks today) | ranges: Pi 3A+ SPI 0..319 x 0..479, Pi 5 HyperPixel 0..799 x 0..479, HackBerry 0..719 x 0..719 (report 021); slots: deck 5, Pi 3A+ DSI 10 (APP-CONTRACT), HackBerry 2 |
| Touch math | pure functions: synthetic taps for the eight orientations with noise, with offset and scale (must not snap), degenerate taps (all at one point: refused), the corner-answer to `APPLAUNCH_ROTATE` table | the existing `calibrate()` as a reference |
| Ruler | px/mm and rounding; agreement with `screen_mm()` and its known-size table | `screen_mm()` cases in the installer tests |
| Catalog | format validator; every fixture tree must match the expected entry, a family, or nothing | the two tables of section 5.2 |
| Boot protocol | SYSROOT tests: `tryboot.txt` is the old file plus the change byte for byte, the cmdline marker, backup names, the diff text, promotion on "yes", no change on "no" or timeout, refusal from the update helper, `--plan`, no terminal, a desktop terminal; a fake `reboot` on `PATH` (the installer tests already stand in for `systemctl`, `runuser` and others) | the fake-system harness of `test-install-board.sh` |
| Test screens | if built as a launcher mode: render-harness goldens at 320x480, 480x320, 800x480, 720x720, 1280x800 | the render harness |

**What only a board can prove:** that the firmware honours `tryboot` on each model (above all the Pi 3A+ and Zero 2 W,
whose boot flow is the older one), that the pattern is visible (DRM master, compositor stopped), the panel's real
colour order, flicker, whether backlight 0 is black, real touch hardware (a dead touch looks like no input), a power cut
during the promotion, the order of the trial unit and the launcher at boot, and whether the prompts are readable on a
3.5" panel.

## 9. Decisions for the owner

Recommended option first in each.

| # | Question | Options |
| --- | --- | --- |
| D1 | Where does the screen test run? | (a) `install.sh` calls it before writing `board.conf` (recommended); (b) only at the launcher's first start; (c) (a) now and Settings > Screen > "Check screen" later |
| D2 | What is the test tool? | (a) a test mode of the launcher binary: the real display and touch path, no Python (recommended); (b) `fb-test-pattern.py` extended (exists, Python 3, raw framebuffer) |
| D3 | When does it run by default? | (a) first install from a terminal, generic or family profiles only, `--no-screen-test` skips it (recommended); (b) every first install from a terminal; (c) only with `--check-screen` |
| D4 | Which patterns? | (a) the minimum set: one combined screen, five taps, three brightness steps (recommended); (b) plus the ruler always; (c) all eight |
| D5 | Size input | (a) keep the diagonal question, ruler only for "don't know" (recommended); (b) the ruler replaces the question |
| D6 | Boot changes on a Pi | (a) only through the tryboot trial and only for catalog proposals; today's tested PWM edits unchanged (recommended); (b) the trial for every boot change, today's included; (c) never: print the lines, the owner edits |
| D7 | Boot changes on Armbian and other non-Pi boards | (a) print only (recommended); (b) a boot-counter revert unit |
| D8 | Catalog format and updates | (a) `key=value` stanzas in the bundle, updated with releases (recommended); (b) JSON; (c) fetched separately from GitHub |
| D9 | Touch that is not a pure orientation (resistive panels) | (a) not supported, the kernel overlay parameters documented (recommended for a hobby installer); (b) add `APPLAUNCH_TOUCH_MATRIX` to the launcher |
| D10 | How confirmed values are kept | (a) recorded in `install.conf` and applied to the generated profile on every update (recommended); (b) written into `board.conf` as a hand edit |
| D11 | "Backlight 0 is not black" | (a) a new `board.conf` key, published in `board.state` so apps (Vinyl knock-knock) paint black only there (recommended); (b) keep the per-app workaround |
| D12 | Desktop boards | (a) test only from ssh or a text console, the display manager stopped and restarted by a trap; from a desktop terminal, skipped with a note (recommended); (b) a window on the desktop; (c) never on desktop boards |
| D13 | Unknown board without `--force` | (a) proceed only with one panel, a terminal and a passed screen test, else stop as today (recommended); (b) keep stopping, `--board generic` stays the way in |
| D14 | Look of the on-panel prompts | open: layout, colours and wording for the owner and the Controller to design (not proposed here) |

## 10. Honest unknowns

- Which tools a Raspberry Pi OS Lite image has by default (`kmsprint`, `edid-decode`, `fbset`, `evtest`) and whether
  Armbian minimal has Python 3 [U]. The design needs none of them.
- Whether `/proc/device-tree/chosen/bootloader/tryboot` exists on the Pi 3A+ and Zero 2 W [U] (the design uses its own
  cmdline marker instead), and whether the Pi 4 B of the owner is a revision 1.0/1.1 with a write-protected EEPROM [U].
- That the Freenove 5" on the Pi 4 B reports 154x86 mm through the fbdev (I, from the kernel sources): a P0 probe
  answers it.
- Which overlay `display_auto_detect` loads for each panel [U]; the docs say only "recognised DSI displays".
- Which touch drivers set the `resolution` field [U]; the FZ-M1's two-contact limit is from an Ubuntu bug report (report
  028), not measured.
- Whether a rename on the FAT boot partition is safe against a power cut [U].
- The HyperPixel 4 and HackBerry visible sizes in mm (not measured; profiles have none).

## 11. Real-board probe plan (list only; nothing was run)

1. P0 probe with `--fixture` on: deck, pi3a (4.3" DSI), palmpi (Freenove 5" on a Pi 3A+), pi4b, pi5 (HyperPixel),
   pi5b (7" official), zero8 (3.5" SPI), HackBerry, FZ-M1. Collect: the report, the fixture tree, the EDID blob (FZ-M1,
   any HDMI monitor), the device-tree panel nodes, `EVIOCGABS` and slots per touch device, the fbdev mm.
2. Check the fbdev mm on pi4b and palmpi against 154x86 (section 2, false positive 3).
3. Screen test (P1 build) on pi3a (zero not black), pi4b or palmpi (dim 20), pi5b, zero8 (small prompts), HackBerry
   (desktop board, from ssh), FZ-M1 (x86, desktop).
4. Tryboot, harmless first: a comment line plus the marker on a Pi 5, a Pi 4 B, a Pi 3A+ and a Zero 2 W; check the
   marker at the trial boot, the "no" path and the timeout path, then the "yes" promotion.
5. Tryboot with a bad change, only with the owner present and an SD card reader at hand: `display_auto_detect=1` on
   pi3a (the known corruption), expect the automatic revert.

## Sources

- This repository at `github/main` e3f3d382: `projects/APPLaunch/pizero2w/install.sh`, `README.md` ("Boards", "Screen
  size", "My screen is not listed"), `cyberdeckzero-update`, `config.txt.snippet`, `tests/`; `projects/APPLaunch/tools/board-probe/`;
  `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_dpi_scaled.c`, `src/cp0_ui_metrics.c`; `docs/APP-CONTRACT.md` sections 3, 4,
  5c; `docs/dev/decisions.md` (2026-11-15, 2026-10-21 X10); `docs/dev/ROADMAP.md`; reports 021 (addenda 1-8), 028, 030
  (via the index); `tools/boards/boards.json`.
- Raspberry Pi kernel overlay README, branch `rpi-6.18.y`:
  https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.18.y/arch/arm/boot/dts/overlays/README (V)
- Overlay and driver sources, `rpi-6.18.y`: `vc4-kms-dsi-7inch-overlay.dts`, `vc4-kms-dsi-waveshare-800x480-overlay.dts`,
  `drivers/gpu/drm/panel/panel-simple.c` (`raspberrypi_7inch`), `drivers/gpu/drm/drm_fb_helper.c`,
  `drivers/gpu/drm/drm_sysfs.c` (V)
- Linux `include/uapi/linux/input.h` (`struct input_absinfo`), `Documentation/input/multi-touch-protocol.rst` (V)
- Raspberry Pi documentation (GitHub `raspberrypi/documentation`, master): `computers/raspberry-pi/bootflow-eeprom.adoc`
  (tryboot), `computers/config_txt/autoboot.adoc`, `common.adoc` (`display_auto_detect`), `video.adoc`,
  `accessories/display/display_intro.adoc` (7" Touch Display: viewable area 154.08 x 85.92 mm),
  `accessories/touch-display-2/about.adoc` (sizes, overlays) (V)
- Waveshare wiki: 4.3inch_DSI_LCD, 5inch_DSI_LCD, 7inch_DSI_LCD, 7.9inch_DSI_LCD, 3.5inch_RPi_LCD_(F), 2.8inch_DPI_LCD,
  5inch_HDMI_LCD, 7inch_HDMI_LCD_(C), https://www.waveshare.com/wiki/ (V, fetched 2026-11-18)
- libinput documentation: device configuration via udev (`LIBINPUT_CALIBRATION_MATRIX`), absolute axes (V)
- Debian packages (trixie): edid-decode, libdrm-tests, drm-info, read-edid, fbset, evtest, libinput-tools, input-utils;
  `kms++-utils` only in the Raspberry Pi archive (V, packages.debian.org and archive.raspberrypi.com)
- Armbian documentation, `docs/user-guide/advanced-configuration.md` (`/boot/armbianEnv.txt`) (V)
