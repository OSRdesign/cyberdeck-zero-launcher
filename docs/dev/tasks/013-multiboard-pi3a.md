# 013 - Multi-board bring-up: Pi 3A+ with the 3.5" 480x320 SPI panel (brief)

Status: approved 2026-10-10 (decisions in 012 section F2). Owner: Controller. Builders: savvy workers via launcher-dev rules (ROLES.md). The user runs all physical tests; Controller reads logs, takes framebuffer screenshots over ssh, deploys. No commit, no push without the user's word. Evidence: `reports/021-multi-sbc-multi-screen.md`.

## Hard rule: the Pi Zero 2 W deck must not change
With no profile file (or `APPLAUNCH_BOARD` unset) behaviour is byte-for-byte today's: fb0, 640x480, 32 bpp, no rotation, compat 2x, current touch env. Every new code path is opt-in through the profile.

## Target board facts (measured)
Pi 3A+ Rev 1.1, Raspberry Pi OS trixie 64-bit, KMS, 425 MB RAM, no desktop. Display: DRM SPI panel ST7796S via mipi-dbi-spi -> fbdev emulation **/dev/fb1 "panel-mipi-dbid", 320x480 portrait, 16 bpp RGB565 (red off 11 len 5, green off 5 len 6, blue off 0 len 5), line_length 640, rotate 0**. User watches in landscape: a pattern drawn upright into the buffer appears turned 90 deg LEFT, so the launcher must pre-rotate its landscape canvas **90 deg clockwise** into the buffer. Touch: `1-005d Goodix Capacitive TouchScreen` /dev/input/event0, ABS_MT_POSITION_X 0..319, Y 0..479, already in BUFFER orientation (user-verified: raw normalised to the buffer lands on the finger). Backlight: only `/sys/class/backlight/backlight_gpio` (on/off). BT + Wi-Fi present. Python/tools: probe in `projects/APPLaunch/tools/board-probe/`.

## Profile = a systemd EnvironmentFile, no new parser in the launcher
`/etc/applaunch/board.conf`, KEY=VALUE lines, loaded by the service with `EnvironmentFile=-/etc/applaunch/board.conf` (leading `-` = optional). The launcher reads these env vars:
```
APPLAUNCH_BOARD=pi3a-luckfox35       # label, logged at start, shown in Settings > About
APPLAUNCH_FB=/dev/fb1                # framebuffer device (default /dev/fb0)
APPLAUNCH_LOGICAL=480x320            # landscape canvas the UI draws on (default 640x480)
APPLAUNCH_ROTATE=90                  # 0|90|180|270 clockwise pre-rotation into the buffer (default 0)
APPLAUNCH_COMPAT_SCALE=1             # 1 or 2: scale of the 320x170 stock-app window (default 2)
APPLAUNCH_TOUCH_DEV=auto             # auto = pick by capability, or /dev/input/eventN
APPLAUNCH_TOUCH_ORIENT=buffer        # buffer = device axes follow the framebuffer (normalise by ABS range, then apply APPLAUNCH_ROTATE); legacy = today's SWAP_XY/INVERT_* env (default legacy)
APPLAUNCH_BACKLIGHT=gpio:/sys/class/backlight/backlight_gpio   # gpio:<dir> on/off | sysfs:<dir> brightness | pwm (default: today's deck behaviour)
```
Bits per pixel, red/green/blue offsets and line_length are read from the framebuffer ioctls, never configured.

## Layout rules at 480x320 (Controller design, approved)
- **Home grid:** status bar 40 px, 3 columns x 2 rows, padding/gap 10 px, tiles about 146x125, icon and text about 72% of today's sizes (fonts one step down), same colours and tile style, same scrolling behaviour.
- **Stock apps and Settings (compat window):** 320x170 at 1x, centred horizontally (80 px black each side) and vertically in the area above the toolbar: 25 px black above, 25 px between window and toolbar. **Toolbar 100 px high at the bottom** (y 220..319), same buttons as today (Esc, arrows, Enter), buttons about 90 px high, same icons/style, evenly spread over 480 px. Apps already draw their own status bar inside the 320x170 window, so no extra native bar.
- The shared Esc-hold ribbon and the touch gesture modes keep working with window offsets (touch inside the window maps to window coordinates).

## Tasks
| # | Task | Tier | Depends on |
|---|------|------|------------|
| T1 | Display backend: configurable fb device, 16 bpp RGB565 and 32 bpp blit, logical canvas size, clockwise rotation in the blit (dirty-rect rotate; avoid per-pixel function calls in the hot loop; RAM budget small), native + compat + external (vfb) blit paths, overlay rect and Esc ribbon kept, touch mapping `buffer` mode (capability detection of the touch device, normalise by ABS range, rotate with APPLAUNCH_ROTATE), brightness gpio on/off. Unit tests for rotation and touch mapping (PC). | savvy-heavy | - |
| T2 | install.sh and packaging: detect the board (DT model + connected DRM connector + fb), write `/etc/applaunch/board.conf` for known boards (zero2w: write nothing or the explicit current values; pi3a-luckfox35: values above), service `EnvironmentFile=-`, refuse on glibc < 2.38 with a clear message, bundle/uninstall updated (uninstall removes the profile), README. Add `libc6 (>= 2.38)` to Depends of every app package in the apps repo build tool (tools/build_deb.py) WITHOUT rebuilding or republishing apps. Never write secrets. | savvy-medium | - (profile keys fixed above) |
| T3 | Native 480x320 layout: home grid, status bar, compat window geometry, toolbar (rules above), selected when APPLAUNCH_LOGICAL is 480x320 and kept identical at 640x480. | savvy-careful | T1 |
| T4 | Settings adaptation on the profile: Brightness degrades to an On/Off toggle when the backlight is gpio on/off; Settings > About shows the board label. | savvy-medium | T1 |
| T5 | Deploy to the Pi 3A+ (Controller): build in WSL (projects/APPLaunch/pizero2w/build.sh --with-store), install over ssh with sudo password on stdin, start the launcher, capture /dev/fb1 to PNG, verify visually; user runs the physical tests. | Controller | T1-T4 |

## Acceptance (Controller, per task)
Diff review against this brief, PC unit tests green, build for the deck profile still produces the same behaviour (no profile: fb0/32 bpp/no rotation), no secrets in files, filemap/docs updated if files added. The user's physical checklist is written to `docs/dev/tests/launcher-0.5.0-multiboard-tests.md` by the Controller after T5.
