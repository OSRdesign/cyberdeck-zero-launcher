# Render harness (headless PC screenshots of the launcher)

Renders the native shell (home grid, status strip, stock-app toolbar, Calculator, screensaver, toasts) and
the compat-hosted Settings pages to PNG at any logical screen size, on the PC, with no board and no
SDL. Goldens at 640x480 (deck) and 480x320 (Pi 3A+) catch any changed pixel, so the responsive-shell
work (task 014, P1b onwards) can be checked without a person at a board.

## Run

Linux or WSL with gcc, g++, make, pkg-config, FreeType and libpng headers. LVGL 9.5 is taken from
`SDK/github_source/lvgl/lvgl_9_5` (downloaded by the first scons build of the launcher).

```sh
# WSL, from the Windows checkout (same rsync as the launcher build, under the build lock)
flock /tmp/wsl-build.lock rsync -a --exclude build/ --exclude dist/ --exclude .git/ \
    /mnt/c/CLAUDE/zero7/launcher/projects/ ~/zero7/launcher/projects/
flock /tmp/wsl-build.lock rsync -a --exclude build/ /mnt/c/CLAUDE/zero7/launcher/ext_components/ \
    ~/zero7/launcher/ext_components/
cd ~/zero7/launcher/projects/APPLaunch/tests/render
flock /tmp/wsl-build.lock bash run.sh      # build + render all scenes + compare with golden/
```

| Command | What it does |
|---|---|
| `run.sh` | build, render every scene at every size, compare with `golden/` (exit 1 on any changed pixel) |
| `run.sh render [scene ...]` | render into `out/<WxH>/<shot>.png`, one contact sheet per scene in `out/sheets/<scene>.png` |
| `run.sh compare` | compare `out/` with `golden/`; diff images in `out/diff/<WxH>/` (changed pixels red) |
| `run.sh golden` | copy `out/640x480` and `out/480x320` into `golden/` (a deliberate baseline change, commit it) |
| `run.sh validate` | compare the harness with real device captures (see Fidelity) |
| `run.sh twice` | render everything twice and check the PNGs are byte-identical |

First build about 1-2 minutes (all of LVGL incl. ThorVG), all scenes at all sizes about 5 s.
The binary also has `render-harness compare a.png b.png [--diff d.png] [--tolerance N] [--ignore x,y,w,h]`,
`sheet` and `crop` modes.

Contact sheets: a row per shot, a column per size (480x320, 640x480, 800x480, 720x720, 1280x720), at
1:1 with a grey frame around each screen. One image shows a whole scene at every size.

## How it works

The launcher code is compiled **unchanged** and runs as on the deck. No product file has a harness hook.

| Piece | In the harness |
|---|---|
| LVGL 9.5 | SDK copy, device LVGL config (`config/lvgl_config.h` = the deck build's Kconfig output, NEON off, see the file), launcher's FreeType override (`main/lvgl`) |
| Display manager `cp0_lvgl_dpi_scaled.c` + `cp0_fb_output.c` | real. Its `open`/`ioctl`/`mmap` references are renamed with `objcopy --redefine-sym` (Makefile) to `src/harness_fb.c`, which serves a memory framebuffer of the profile's geometry and a pipe as the touch device. Native flush, compat 2x/1x blit, rotation, RGB565, external blit and overlay rect all run as on the board |
| Native shell, Calculator, screensaver, toast, Settings (all pages), AppPage top bar, status bar renderer | real (`main/ui`, `cp0_lvgl/src`) |
| Keyboard | real queue + real LVGL bridge (`cp0_keyboard_lvgl_input.c`); `src/harness_keyboard.c` replaces only the libinput/xkb reader thread (same injection code as `cp0_keyboard_inject`) |
| Services (`cp0_signal_*`) | the callback lists are real (`commount.cpp`); `src/harness_services.cpp` answers config, filesystem paths, Wi-Fi status, Bluetooth status, backlight, local time from fixtures, in the documented wire formats. An unknown command answers -1 and logs `no fixture` in the scene log |
| Launcher object (`class Launch`) | `src/harness_launch.cpp` instead of `launch.cpp`: the scene names the tiles; launching a page app runs the real `app::launch` lambda (`begin_page`, loading overlay, page constructor, `cp0_lvgl_start_app_page`) |
| Time | LVGL tick: fake clock advanced in 5 ms steps; wall clock (`time`, `gettimeofday`, `clock_gettime(REALTIME)`): the scene's `clock` |
| Platform C calls left after `--gc-sections` | `src/harness_stubs.cpp` (no battery, no sudo, `cp0_time_str` from the scene clock, a copy of the built-in app registry table) |

Each size runs in its own forked process (the display manager is process-wide). Board profiles per size:

| Size | Profile | Framebuffer the display manager sees |
|---|---|---|
| 640x480 | `deck` (no board.conf) | 640x480 XRGB8888, no rotation, compat 2x at (0,0) |
| 480x320 | `pi3a` (board.conf of the Pi 3A+: `APPLAUNCH_BOARD`, rotate 90, compat scale 1, gpio backlight) | 320x480 RGB565, turned 90 deg clockwise, compat 1x at (80,25) |
| others | `generic` | WxH XRGB8888, no rotation, compat scale = largest that fits |

Touch uses `APPLAUNCH_TOUCH_ORIENT=buffer` everywhere (exact integer mapping); this is the only profile
setting that differs from the deck (which uses the legacy swap/invert mapping).

## Scene scripts (`scenes/*.scene`)

One command per line, `#` comments, `"quoted words"`. A line prefixed `@480x320` (or `@640x480,480x320`)
runs at those sizes only.

| Command | Meaning |
|---|---|
| `title TEXT`, `sizes WxH ...` | sheet title; sizes to render (default the five above) |
| `profile deck\|pi3a\|generic`, `env KEY VALUE`, `ppmm N` | before the first drawing command. `ppmm` (px/mm x100) is only logged: v0.5.0 has no density input yet (P1b) |
| `apps Name Name=icon.png Name=fixture:file.png ...` | home tiles (built-in icons by name); `Settings` and `Calculator` open the real pages |
| `clock HH:MM [YYYY-MM-DD]`, `wifi off\|PCT`, `bt off\|on\|connected`, `config KEY VALUE` | fixtures (the status strip polls every 2 s: `wait 2100` after a change) |
| `home` | build the grid and show it (`native_ui::refresh_apps` + `show_home`) |
| `launch NAME` | tap-equivalent launch of a tile |
| `stockapp` | stock app: toolbar chrome + a flat 320x170 placeholder picture through the real external blit (no process) |
| `ribbon` | the hold-Esc ribbon over a stock app, as `external_poll()` shows it |
| `key NAME [press\|release]`, `type TEXT` | keys through the real queue (UP DOWN LEFT RIGHT ENTER ESC TAB SPACE BACKSPACE 0-9 ...) |
| `tap X Y`, `drag X1 Y1 X2 Y2 [STEPS]`, `touch X Y` + `release` | touch through the display manager's touch path (logical coordinates) |
| `toast TEXT` (`\n` = new line), `screensaver` (+ `wait`) | launcher toast; arm the clock screensaver |
| `wait MS`, `shot NAME` | advance the fake clock; settle 500 ms and save `out/<WxH>/NAME.png` |

Scenes today (18 shots, 90 PNGs): `home`, `home_status` (offline, weak + BT on, full + BT connected,
focus moved, scrolled), `toolbar` (stock app, hold-Esc ribbon), `calculator` (empty, typing, result),
`screensaver`, `toast` (one and two lines), `settings` (root, Screen section, root on System, System
section). Settings runs where it runs on the device: in the compat window, scaled by the display manager.

## Goldens and fidelity

`golden/640x480` and `golden/480x320` are the v0.5.0 baseline. `compare` fails on **any** changed pixel.
Goldens compare harness to harness. Against the devices (`run.sh validate`, captures in `reference/`):

- Deck home 640x480 vs `/dev/fb0` capture: **identical** (0 pixels). The 2048 tile icon is an installed
  Store app whose PNG is not in this repository: `fixtures/2048_128.png` was cut from that capture.
- Pi 3A+ home 480x320 vs `/dev/fb1` capture (RGB565, de-rotated): 16 pixels are the console cursor
  blinking at the top left (not drawn by the launcher, ignored), 25 pixels in the clock text differ by at
  most 1 LSB of RGB565 (channel delta 9). Everything else identical.
- Settings root 640x480 vs `docs/screenshots/settings.png` (deck, 2026-10-03): identical outside the
  clock/Wi-Fi strip (other time and signal in that capture).

Caveats: the device blends with NEON and the host does not; the device's FreeType (Debian trixie) and
the host's (Ubuntu 24.04) may rasterise a glyph differently. Expect at most 1-LSB differences against
device screenshots, mostly in scaled FreeType text. `deck.py shot` stays the device truth.

## Not covered

- Pages that need live services beyond the fixtures (Wi-Fi network list, Bluetooth scan/pairing, Apps
  sources, sudo prompt, Date & Time info, Storage): add fixtures in `src/harness_services.cpp` as P2-P3
  scenes need them.
- Stock apps (only a placeholder picture), the terminal pages, SSH, games, the loading overlay, the media
  OSD, touch feel and speed. The harness proves layout, not behaviour on the board.
- Changes to `builtin_app_registry.cpp`'s table must be copied to `src/harness_stubs.cpp`; changes to the
  device LVGL config to `config/lvgl_config.h`.
