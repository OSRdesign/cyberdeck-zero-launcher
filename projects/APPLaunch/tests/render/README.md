# Render harness (headless PC screenshots of the launcher)

Renders the native shell (home grid, status strip, stock-app toolbar, Calculator, screensaver, toasts) and
the native Settings pages to PNG at any logical screen size, on the PC, with no board and no
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
| `run.sh` | build, render every scene at every size, compare with `golden/` (exit 1 on any changed pixel), then `header` |
| `run.sh render [scene ...]` | render into `out/<WxH>/<shot>.png`, one contact sheet per scene in `out/sheets/<scene>.png` |
| `run.sh compare` | compare `out/` with `golden/`; diff images in `out/diff/<WxH>/` (changed pixels red). Shots of a review-only scene (listed in `out/<WxH>/review.list`) need no golden |
| `run.sh header` | every page header cut by a scene's `header` command against the home grid's: the status strip rectangle pixel-identical (`render-harness compare`) and the title label at the same origin |
| `run.sh golden` | copy `out/640x480` and `out/480x320` into `golden/` (a deliberate baseline change, commit it) |
| `run.sh validate` | compare the harness with real device captures (see Fidelity) |
| `run.sh twice` | render everything twice and check the PNGs are byte-identical |
| `run.sh contract` | app contract 1 (`docs/APP-CONTRACT.md`): builds the host `tools/dev/app-contract-probe`, renders the `metrics` scene, then runs the probe in `--report` mode with the per-launch environment the launcher's builder wrote (`out/<WxH>/metrics.{compat,fullscreen}.env`) against `out/<WxH>/runtime/applaunch` (plus an `input.state` fixture, `vkb=force`), and checks size, class, density, rotation, canvas, window, scale and the keyboard rule against the layout service (`out/<WxH>/metrics.facts`); also the degrade cases (no environment, nothing). Reports: `out/<WxH>/contract.*.txt` |

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
| 640x480 | `deck` (no board.conf) | 640x480 XRGB8888, no rotation, no physical size (11.3 px/mm default), compat 2x at (0,0) |
| 480x320 | `pi3a` (board.conf of the Pi 3A+: `APPLAUNCH_BOARD`, rotate 90, compat scale 1, `APPLAUNCH_PANEL_MM=79x49`, gpio backlight) | 320x480 RGB565, turned 90 deg clockwise, compat 1x at (80,25) |
| others | `generic` | WxH XRGB8888, no rotation, reports the physical size of the size's density (`var.width/height` in mm), compat window placed by the layout service (`cp0_ui_metrics`) |

`XDG_RUNTIME_DIR` is `out/<WxH>/runtime`, so the `screen.state` the display manager publishes lands there.

Touch uses `APPLAUNCH_TOUCH_ORIENT=buffer` everywhere (exact integer mapping); this is the only profile
setting that differs from the deck (which uses the legacy swap/invert mapping).

## Scene scripts (`scenes/*.scene`)

One command per line, `#` comments, `"quoted words"`. A line prefixed `@480x320` (or `@640x480,480x320`)
runs at those sizes only.

| Command | Meaning |
|---|---|
| `title TEXT`, `sizes WxH ...` | sheet title; sizes to render (default the five above) |
| `review` | the scene's shots are for review only: no golden is expected (nor made by `run.sh golden`) at 640x480 / 480x320 |
| `profile deck\|pi3a\|generic`, `env KEY VALUE`, `ppmm N` | before the first drawing command. `ppmm` (px/mm x100): the fake framebuffer reports the matching size in mm (default per size for `generic`: 800x480 920, 720x720 1000, 1280x720 1160, others 1130; with an explicit `ppmm` on every profile). `APPLAUNCH_PANEL_MM` (`env`) wins over it |
| `apps Name Name=icon.png Name=fixture:file.png ...` | home tiles (built-in icons by name); `Settings`, `Calculator`, `SSH` and `CLI` (the terminal page, on the fake PTY) open the real pages |
| `clock HH:MM [YYYY-MM-DD]`, `wifi off\|PCT`, `bt off\|on\|connected`, `config KEY VALUE` | fixtures (the status strip polls every 2 s: `wait 2100` after a change) |
| `home` | build the grid and show it (`native_ui::refresh_apps` + `show_home`) |
| `launch NAME` | tap-equivalent launch of a tile |
| `stockapp` | stock app: toolbar chrome + a flat 320x170 placeholder picture through the real external blit (no process) |
| `ribbon` | the hold-Esc ribbon over a stock app, as `external_poll()` shows it |
| `key NAME [press\|release]`, `type TEXT` | keys through the real queue (UP DOWN LEFT RIGHT ENTER ESC TAB SPACE BACKSPACE 0-9 ...) |
| `tap X Y`, `drag X1 Y1 X2 Y2 [STEPS]`, `touch X Y` + `release` | touch through the display manager's touch path (logical coordinates) |
| `tapkey LABEL`, `tapfield LABEL` | a tap (touch path) on the on-screen keyboard key labelled LABEL (`SHIFT`, `ENTER`, `BACKSPACE`, `LEFT`/`UP`/`DOWN`/`RIGHT` for the symbol keys, `SPACE` for the space bar; a label with `#` in quotes: `"#+="`), or on the text field under the field label LABEL |
| `ptyout TEXT`, `ptyprompt TEXT`, `ptyexit`, `ptylog NAME` | the fake PTY of the terminal pages (`src/harness_services.cpp`, no process): what the program prints next (`\n` = CR LF, `\e` = Esc); the prompt the fake shell prints after Enter and ^C; the program exits; what the terminal did with the PTY (open, every write as bytes, resize, close) as `out/<WxH>/NAME.txt`. Writes are echoed as a cooked tty and a shell would (^C: "^C" and a new prompt, ^D: "exit" and the end) |
| `listdemo compact\|comfortable ROWS [readonly] [separators] [wrap] [kinds] [empty TEXT...]`, `listdemo update` | a `cp0::ui::CompactList` page (Page scaffold, status strip, the list in the page body, an input group on the keypad) with demo rows: Compact = a system monitor (sections, value tones, long labels), Comfortable = 3 serial ports or a 12-row settings menu; `kinds`: a device page with every row affordance (Edit, Open, Info with a Normal and a Bad value, Action idle, one Unspecified, a long label and a long value beside a chevron, an Action running with a value last; Compact adds a section first); an activation writes "Activated LABEL" into the title. `update` refreshes the values in place (`set_rows`, same rows); `value\|row INDEX normal\|ok\|warn\|bad\|muted TEXT` sets one row's value and tone through `set_value` / `update_row`; `kind\|setkind INDEX unspecified\|info\|open\|edit\|action\|action-running` sets one row's affordance through `update_row` (`kind`) or the live refresh `set_rows` with the other rows unchanged (`setkind`) |
| `expect_color TEXT RRGGBB` | fail the scene unless a visible label shows TEXT with text colour RRGGBB and pixels of that colour are drawn in its box |
| `expect_chevron LABEL yes\|no` | fail the scene unless the list row of the visible label LABEL shows a row-affordance chevron (`yes`: grey with pixels drawn, right of the row's texts and inside the row, its pixels centred on the row's middle within 1.5 px, a font larger than the label's, the row at the list's item height) or none (`no`) |
| `expect_dot LABEL idle\|running\|no` | fail the scene unless the list row of the visible label LABEL shows the Action dot (`idle`: white, `running`: the accent blue; a circle of the list's dot size with its colour drawn, its pixels centred on the row's middle and in the chevron's slot within 1.5 px, right of the row's texts and inside the row, the row's value ending where a chevron row's value ends, the row at the list's item height) or none (`no`) |
| `expect_same SHOT` | fail the scene unless the screen now is pixel-identical to the shot SHOT taken earlier in the scene at this size |
| `keyboards N` | physical keyboards the on-screen keyboard policy sees (default 0; `harness_stubs.cpp` replaces the presence watcher) |
| `ntp on\|off`, `timeset ok\|fail` | Date & Time: Network Time as NtpGet reports it (the Network Time toggle sets it too), whether Set Manually > Save? works (`harness_stubs.cpp`) |
| `tz ZONE`, `tzset ok\|fail\|denied` | Date & Time > Time zone: the zone in use (default `Etc/UTC`), what a change answers (done, failed, refused by PolicyKit: the page then tries the sudo path, which the harness does not have). The list is `fixtures/timezones.txt` (a real `timedatectl list-timezones`); a zone set becomes the process's `TZ` when the page reports the change, so the clocks follow it (`harness_stubs.cpp`) |
| `toast TEXT` (`\n` = new line) / `toast off`, `screensaver` (+ `wait`) | launcher toast (persistent) / hide it; arm the clock screensaver |
| `osd level TITLE PCT`, `osd mute on`/`off`, `loading TEXT` / `loading off`, `compatpage` | the media OSD of the brightness / volume keys (it hides itself after 1000 ms: a shot settles 700 ms after the command), the "Loading..." overlay, a compat page (the compat window is default, a flat placeholder screen, no external app) |
| `wifinet SSID IP`, `expect_text [end] TEXT` | the connected network the Wi-Fi fixture reports; fail the scene unless a visible label shows TEXT whole (`end`: a label whose text ends with TEXT) in a box wide enough for it (not cut with dots), see `settings_native_wifi_status` |
| `wait MS`, `shot NAME` | advance the fake clock; settle 500 ms and save `out/<WxH>/NAME.png` |
| `header NAME [TITLE ...]` | the home grid's status strip rectangle (from its leftmost drawn pixel, the Bluetooth icon, `cp0::ui::status_strip_left()`, to the right edge, `bar_h` high) cut out as `out/<WxH>/header/NAME.png`, and the origin of the visible label whose text is one of TITLE as `NAME.txt`; `run.sh header` checks them against `header home` taken on the home grid. The home grid has no title (rebranding plan 025): without TITLE the origin written is the shell's title origin (`shell.title_x/title_y`) and the command fails if a label stands there |
| `switcher CARD...`, `switcher select ITEM`, `switcher off` | multitasking P-d: the switcher screen (`multitask/switcher_view.cpp`) with the cards named `Name=front\|running\|paused\|nr` (front: the app it was opened from; the icon is the tile's of that name); `select card0..3\|home\|closeall` moves the selection |
| `sheet NAME`, `sheet off` | the legacy sheet "Close NAME?": the screen shown now stands for the frozen app's frame (snapshot, written back dimmed to 38 %), only the sheet's box is drawn (the native clip); the command fails unless every pixel outside the box is the dimmed frame and the box is drawn |
| `dots NAME...` | these tiles' apps are left open (`native_ui::set_resident_apps`; tiles have the Exec line `tile:NAME`) |
| `tapitem ITEM [edge]`, `expect_event TEXT`, `expect_targets` | a tap on `card0..3`, `close0..3` (an X), `home`, `closeall`, `keep`, `sheetclose` (`edge`: 2 px inside the corner of its touch target farthest from what is drawn); fail unless the last thing the switcher / sheet reported is TEXT (`pick NAME`, `close NAME`, `home`, `close-all`, `cancel`, `keep`, `sheet-close`); fail unless every touch target shown is at least 7 mm both ways and the cards and the bar stay inside the panel, apart |
| `metrics NAME` | the layout service's numbers for the size (class, density and its source, tokens, text sizes, compat window and toolbar, home layout, `APPLAUNCH_SCREEN_*`, the per-launch environment of both modes, `screen.state`) as `out/<WxH>/NAME.txt`; for `run.sh contract` also `NAME.compat.env` / `NAME.fullscreen.env` (the panel variables + `app_launch_env::export_for_current()`, as `export ...;` lines) and `NAME.facts` (size, class, ppmm, rot, compat window, scale) |

Scenes today: `home`, `home_grid` (computed grid at 800x480, 720x720, 1280x720, 1024x600 with 12 apps; no golden),  `home_status` (offline, weak + BT on, full + BT connected,
focus moved, scrolled), `toolbar` (stock app, hold-Esc ribbon), `calculator` (empty, typing, result),
`screensaver`, `toast` (one and two lines), `settings` (root, Screen section, root on System, System
section). Settings is the native host (full screen on the native display, the default since task 014 P3e; its
goldens were re-shot on 2026-10-11 from the compat ones).
`settings_native` (task 014 P2a: root, Screen section, DarkTime list, Brightness
slider one step darker or the Pi 3A+ on/off list, at all five sizes) and `settings_native_touch` (tap, tap to open,
drag, the header's Esc button, at 480x320 and 800x480) are review-only scenes
(no goldens); their headers are checked against the home grid by
`run.sh header`.
`osk` (task 014 P2b, `APPLAUNCH_OSK=force`: Settings > System > On-screen keyboard, then the development page
Keyboard test: letters, Shift armed, 123, Ctrl armed, number pad, password masked, Esc dismisses, a tap on the field
brings the keyboard back, at all five sizes) and `osk_policy` (`APPLAUNCH_OSK=auto` at 640x480 and 800x480: setting
Off, back to Auto through its choice list, a physical keyboard appears) are review-only scenes too.
`settings_native_info` (task 014 P3a, a release build: Storage, Licenses as opened / scrolled / dragged, About, User,
Date & Time Info), `settings_native_wifi` (task 014 P3b, `APPLAUNCH_OSK=force`: the network list, the password dialog
with the keyboard, wrong password, connected, the connected network's actions and Details) and
`settings_native_wifi_dialog` (a physical keyboard: the dialog with its buttons, Join Hidden Network, Disconnect, Wi-Fi
off) are review-only too. Their Wi-Fi service is `src/harness_services.cpp`: `Fixtures::wifi_networks` (a connected
saved network, a saved one, open ones, a new secured one, a weak one; no real names), Scan, Connect / ConnectHidden (a
new secured network accepts only `Fixtures::wifi_password`, a fake; a hidden one only `wifi_hidden_ssid`),
ProfileForget / ProfileExists, ProfileDisconnectActive, RadioSetEnabled; the OS info service answers
NetworkDefaultInfoRead. A connection keeps the scene's `wifi PCT` signal so the status strip stays comparable.
`metrics` writes `out/<WxH>/metrics.txt` at the five sizes and 1024x600 (text only, no golden).
`settings_native_launcher` (task 014 P3e: Settings > Launcher with the Store toggle first, switched off, reopened) is
review-only too.
`settings_native_timezone` (review-only, at 320x170, 480x320, 640x480 and 1280x800): Date & Time > Time zone from
Etc/UTC, the row with its chevron, the regions, America's zones, a failed and a refused change, the change done (row and
clocks), the picker reopened on the zone, Esc up one level, Network Time switched off without touching the zone, Info
with the new offset, the home grid's clock, then Europe > Paris.
`ssh_native` (task 014 P4, review-only, `APPLAUNCH_OSK=force`): the native SSH form (labels beside the fields) with the
keyboard up on Host, Port (number pad) and User, the whole form with the keyboard down, the "Host is required" error
(keyboard down, then up with the error under the field), the help page (`key F1`) and back, at the five sizes. The harness links the SSH page and its terminal (a tile named `SSH` opens it); the terminal is never shot.
`compact_list` (goldens at 640x480 and 480x320; also 800x480 and 720x720): `CompactList`, 40 compact rows (top with the
scrollbar flashed, 10 items down, a live refresh, End, idle without scrollbar, a drag), 3 comfortable rows (all fit;
tap selects, tap again activates), 12 comfortable rows (top, 6 down, End), read-only with separators, the empty state.
`compact_list_tones` (review-only): value tones changed in place (`set_value`, `update_row`; Normal / Bad to Muted and
back, on a plain row and the selected one), each checked with `expect_color`.
`compact_list_affordance` (review-only, the five sizes): the row affordances in both densities (`listdemo ... kinds`),
each kind (Action idle and running included) on a plain row and on the selected one, checked with `expect_chevron`,
`expect_dot` and `expect_color`; the dot switched between idle and running through `set_rows` and `update_row` and
back, `expect_same` against the shot before (no rebuild: selection and scroll kept); then an untagged list tagged with
five kinds and set back to Unspecified, `expect_same` against the shot taken before (the legacy look is
pixel-identical).
`multitask_switcher`, `multitask_sheet`, `multitask_home` (task 015-pd, goldens at 640x480; also 800x480, 720x720,
1280x720, 1024x600; the switcher does not exist under 640x480): the switcher with 3 apps and a free slot, the keyboard
selection, a Not responding card, 4 apps, 1 app with a long name; the legacy sheet over a dimmed frame; the home grid
with the dots and the one-time hint, the dots gone again (pixel-identical).
`overlays` (task 014 P4, review-only): the toast at home / over Settings / over the Calculator, the hold-Esc ribbon, the media OSD and the loading overlay at home and over a compat page, the compat ribbon over a stock app, at the five sizes.
`terminal_osk` (task 014 P4b, review-only, `APPLAUNCH_OSK=auto` with `keyboards N`): the CLI terminal on the fake PTY with a physical keyboard (whole grid), the on-screen keyboard up when the last keyboard goes (grid above it, prompt on its last row, PTY resized), the 123 and #+= layers while typing, Ctrl armed, Ctrl+C, a physical keyboard back (grid and lines restored), Ctrl+D and "Press any key to exit", then the SSH form connecting through the keyboard's Enter and the SSH session, at the five sizes; `ptylog` writes the bytes every key sent and the PTY sizes (`out/<WxH>/pty.txt`).

## Goldens and fidelity

`golden/640x480` and `golden/480x320` are the v0.5.0 baseline. `compare` fails on **any** changed pixel.
Goldens compare harness to harness. Against the devices (`run.sh validate`, captures in `reference/`):

- Deck home 640x480 vs `/dev/fb0` capture: **identical** (0 pixels). The 2048 tile icon is an installed
  Store app whose PNG is not in this repository: `fixtures/2048_128.png` was cut from that capture.
- Pi 3A+ home 480x320 vs `/dev/fb1` capture (RGB565, de-rotated): 16 pixels are the console cursor
  blinking at the top left (not drawn by the launcher, ignored), 25 pixels in the clock text differ by at
  most 1 LSB of RGB565 (channel delta 9). Everything else identical.
- Settings: no device comparison any more (the one against `docs/screenshots/settings.png`, a 2026-10-03 deck capture
  of the compat Settings, was dropped with the flip to the native Settings, task 014 P3e).

Caveats: the device blends with NEON and the host does not; the device's FreeType (Debian trixie) and
the host's (Ubuntu 24.04) may rasterise a glyph differently. Expect at most 1-LSB differences against
device screenshots, mostly in scaled FreeType text. `deck.py shot` stays the device truth.

## Not covered

- Pages that need live services beyond the fixtures (Bluetooth scan/pairing, Apps sources, sudo prompt): add
  fixtures in `src/harness_services.cpp` as P3 scenes need them. Storage reads the host's mounts: on a PC it shows
  "SD card information unavailable.".
- Stock apps (only a placeholder picture), real programs in the terminal pages (the terminal runs on a fake PTY that
  echoes keys; no shell, ssh, nano), the hold-Esc controller (not linked), games, touch feel and speed. The harness
  proves layout, not behaviour on the board.
- Changes to `builtin_app_registry.cpp`'s table must be copied to `src/harness_stubs.cpp`; changes to the
  device LVGL config to `config/lvgl_config.h`.
