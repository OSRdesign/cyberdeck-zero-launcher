# Render harness (headless PC screenshots of the launcher)

Renders the native shell (home grid, status strip, stock-app toolbar, Calculator, screensaver, toasts) and
the native Settings pages to PNG at any logical screen size, on the PC, with no board and no
SDL. Goldens at 640x480 (deck) and 480x320 (Pi 3A+), and at 480x640 and 480x800 for the PDA scenes (`pda_*`, profile
`pda`, report 038 S2), catch any changed pixel, so the responsive-shell
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
| `run.sh golden` | copy `out/640x480`, `out/480x320`, `out/480x640` and `out/480x800` into `golden/` (a deliberate baseline change, commit it) |
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
| Board capabilities (`cp0_capabilities.c`, report 030) | real, on fake trees and fake programs under `out/<WxH>/runtime` (`power_supply`, `backlight`, `asound`, `fakebin`): scene commands `battery`, `backlight`, `sound` |

Each size runs in its own forked process (the display manager is process-wide). Board profiles per size:

| Size | Profile | Framebuffer the display manager sees |
|---|---|---|
| 640x480 | `deck` (no board.conf) | 640x480 XRGB8888, no rotation, no physical size (11.3 px/mm default), compat 2x at (0,0) |
| 480x320 | `pi3a` (board.conf of the Pi 3A+: `APPLAUNCH_BOARD`, rotate 90, compat scale 1, `APPLAUNCH_PANEL_MM=79x49`, gpio backlight) | 320x480 RGB565, turned 90 deg clockwise, compat 1x at (80,25) |
| others | `generic` | WxH XRGB8888, no rotation, reports the physical size of the size's density (`var.width/height` in mm), compat window placed by the layout service (`cp0_ui_metrics`) |
| 480x640, 480x800 with `profile pda` (the PDA scenes) | `pda` (report 038 S2: the board.conf of `pi3a-wsdsi43-portrait`: `APPLAUNCH_BOARD`, rotate 90, `APPLAUNCH_PDA=1` (the writing zone, report 041; the `pda_*` scenes set it to 0); `APPLAUNCH_PANEL_MM` 94x56 at 480x800, the 4.3" DSI, and 71x53 at 480x640, a 3.5" 640x480 panel) | the landscape panel (800x480 / 640x480) RGB565, turned 90 deg clockwise to the portrait canvas, compat 1x; the native display is the whole panel, the band (the key strip and the writing zone, 318 / 240 px) a third display drawn over its bottom while text input is wanted (report 042) |

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
| `profile deck\|pi3a\|pda\|generic`, `env KEY VALUE`, `ppmm N` | before the first drawing command. `ppmm` (px/mm x100): the fake framebuffer reports the matching size in mm (default per size for `generic`: 800x480 920, 720x720 1000, 1280x720 1160, others 1130; with an explicit `ppmm` on every profile). `APPLAUNCH_PANEL_MM` (`env`) wins over it. `env APPLAUNCH_ROTATE N` with a rotation of the same quarter as the profile's (the `pda` profile's 90 -> 270: PDA mode On rotate right, report 043) also turns the harness's own view: the shots and the touch go through that rotation, as on a panel held the other way |
| `apps Name Name=icon.png Name=fixture:file.png Name=none Name=stale ...` | home tiles (built-in icons by name; another name has no icon, as a .desktop entry with no `Icon=` line); `Settings`, `Calculator`, `SSH` and `CLI` (the terminal page, on the fake PTY) open the real pages. `none` / `stale`: no icon, with zeros / a small image header after the empty path's terminator (what a recycled heap chunk can leave there, `harness_launch.cpp` `plant_empty_icon`; scene `home_icon_missing`) |
| `clock HH:MM [YYYY-MM-DD]`, `wifi off\|PCT`, `bt off\|on\|connected`, `config KEY VALUE` | fixtures (the status strip polls every 2 s: `wait 2100` after a change) |
| `battery off\|mains\|PCT discharging\|charging\|full\|notcharging` | the fake `power_supply` tree the launcher reads (`APPLAUNCH_POWER_SUPPLY_ROOT` = `out/<WxH>/runtime/power_supply`, set at boot, emptied before every scene: never the host's battery): nothing (the Pi boards, the default), an AC adapter alone, or the FZ-M1's `ADP1` + `BAT1` (energy units: 26.64 of 33.3 Wh, 7.412 V, 6.54 W discharging, 12 W charging). Status strip: `wait 2100` after a change; Settings > Battery's row follows the tree when Settings opens |
| `ssh_status off\|N [idle\|active]` | after `home` (the launcher reads Settings > System > SSH indicator there): the SSH indicator's input (2026-10-30). Writes a fake `/proc/net/tcp` under `out/<WxH>/runtime/sshroot` (sshd listening on 22, N = 1..8 incoming sessions from 192.168.68.12; `active`: bytes in their send queue, the watch's fallback, as a fake tree has no sockets for netlink) and takes two samples 2 s apart with the launcher's own watch and setting (`ssh_indicator::Sampler`, `launcher_ssh::enabled()`), published as the status feed thread does (the harness has no feed thread). Never the host's own sessions. The strips read it at their next refresh: `wait 2100` |
| `backlight levels\|onoff\|none` | before the first drawing command (detected once per launcher run). The fake `/sys/class/backlight` the launcher detects its backlight in when the profile has no `APPLAUNCH_BACKLIGHT` (`APPLAUNCH_BACKLIGHT_ROOT` = `out/<WxH>/runtime/backlight`, report 030): `levels` (the default) the deck's PWM `backlight` (max = the fixture's 255: today's default path), `onoff` the Pi 5 HyperPixel's `gpio-backlight` (max 1: Brightness On / Off), `none` no device (the HackBerry: no Brightness row) |
| `sound off\|hdmi\|card\|bluetooth` | what the launcher finds as Settings opens (report 030; `APPLAUNCH_ASOUND_ROOT` = `out/<WxH>/runtime/asound`, `APPLAUNCH_AMIXER` / `APPLAUNCH_PACTL` = fake programs in `out/<WxH>/runtime/fakebin`, never the host's): `off` (the default) nothing, `hdmi` vc4-hdmi cards only, `card` the tablet's HDA Intel PCH with Master, `bluetooth` no card but a sound server with a Bluetooth speaker's sink. The audio service's VolumeRead / VolumeWrite answer the fixture volume (50 at start) |
| `home` | build the grid and show it (`native_ui::refresh_apps` + `show_home`) |
| `launch NAME` | tap-equivalent launch of a tile |
| `stockapp` | stock app: toolbar chrome + a flat 320x170 placeholder picture through the real external blit (no process) |
| `ribbon` | the hold-Esc ribbon over a stock app, as `external_poll()` shows it |
| `key NAME [press\|release]`, `type TEXT` | keys through the real queue (UP DOWN LEFT RIGHT ENTER ESC TAB SPACE BACKSPACE 0-9 ...); `key CTRL+NAME` sends the key with `KBD_MOD_CTRL`, and with `CTRL+` a letter A-Z is a key name too (`key CTRL+O`: the options key, what a keyboard's Ctrl+O and the pointer's held click send; no text) |
| `tap X Y`, `drag X1 Y1 X2 Y2 [STEPS]`, `touch X Y` + `release` | touch through the display manager's touch path (logical coordinates) |
| `tapkey LABEL`, `tapfield LABEL` | a tap (touch path) on the on-screen keyboard key labelled LABEL (`SHIFT`, `ENTER`, `BACKSPACE`, `LEFT`/`UP`/`DOWN`/`RIGHT` for the symbol keys, `CLOSE` for the cross that brings the PDA writing zone back, `SPACE` for the space bar; a label with `#` in quotes: `"#+="`), or on the text field under the field label LABEL |
| `ptyout TEXT`, `ptyprompt TEXT`, `ptyexit`, `ptylog NAME` | the fake PTY of the terminal pages (`src/harness_services.cpp`, no process): what the program prints next (`\n` = CR LF, `\e` = Esc); the prompt the fake shell prints after Enter and ^C; the program exits; what the terminal did with the PTY (open, every write as bytes, resize, close) as `out/<WxH>/NAME.txt`. Writes are echoed as a cooked tty and a shell would (^C: "^C" and a new prompt, ^D: "exit" and the end) |
| `listdemo compact\|comfortable ROWS [readonly] [separators] [wrap] [kinds] [empty TEXT...]`, `listdemo update` | a `cp0::ui::CompactList` page (Page scaffold, status strip, the list in the page body, an input group on the keypad) with demo rows: Compact = a system monitor (sections, value tones, long labels), Comfortable = 3 serial ports or a 12-row settings menu; `kinds`: a device page with every row affordance (Edit, Open, Info with a Normal and a Bad value, Action idle, one Unspecified, a long label and a long value beside a chevron, an Action running with a value last; Compact adds a section first); an activation writes "Activated LABEL" into the title. `update` refreshes the values in place (`set_rows`, same rows); `value\|row INDEX normal\|ok\|warn\|bad\|muted TEXT` sets one row's value and tone through `set_value` / `update_row`; `kind\|setkind INDEX unspecified\|info\|open\|edit\|action\|action-running` sets one row's affordance through `update_row` (`kind`) or the live refresh `set_rows` with the other rows unchanged (`setkind`) |
| `listtitle PART...` | the list demo page's title path (`Page::set_title`), one word per part; a quoted word may itself be a breadcrumb (`"Net Toolbox > Diagnosis"`) |
| `expect_rows LABEL...` | fail the scene unless the list holding the visible row label LABEL (the first one: a native Settings roller, its rows scrolled out of view included) shows exactly these rows in this order |
| `expect_color TEXT RRGGBB` | fail the scene unless a visible label shows TEXT with text colour RRGGBB and pixels of that colour are drawn in its box |
| `expect_chevron LABEL yes\|no` | fail the scene unless the list row of the visible label LABEL shows a row-affordance chevron (`yes`: grey with pixels drawn, right of the row's texts and inside the row, its pixels centred on the row's middle within 1.5 px, a font larger than the label's, the row at the list's item height) or none (`no`) |
| `expect_dot LABEL idle\|running\|no` | fail the scene unless the list row of the visible label LABEL shows the Action dot (`idle`: white, `running`: the accent blue; a circle of the list's dot size with its colour drawn, its pixels centred on the row's middle and in the chevron's slot within 1.5 px, right of the row's texts and inside the row, the row's value ending where a chevron row's value ends, the row at the list's item height) or none (`no`) |
| `expect_same SHOT` | fail the scene unless the screen now is pixel-identical to the shot SHOT taken earlier in the scene at this size |
| `expect_tiles NAME...` | fail the scene unless the home grid's tiles, in the grid's child order (the order drawn: a tile carried in move mode at its current place, study 046 P2), are exactly these; a tile's name is its own label |
| `grid_scroll_mark NAME`, `expect_grid_scroll_same NAME` | remember the home grid's scroll (the grid holding the tile NAME); fail unless it is still the same (a press and the 3 s hold on a tile do not scroll the grid) |
| `expect_grid_view NAME` | fail the scene unless the home grid's view is inside its content (no blank space under the last row nor above the first; a grid shorter than the view starts at its first row) and the tile NAME is wholly inside the grid's area (study 046 P2: the view follows a carried tile by the least scroll) |
| `keyboards N` | physical keyboards the on-screen keyboard policy sees (default 0; `harness_stubs.cpp` replaces the presence watcher); the home grid's move-mode caption follows it too (no keyboard: the touch-only line) |
| `configfull on\|off` | the config service's `SetManyAndSave` refuses every write, as the real store does when its 64 entries are taken (`harness_services.cpp`; the home grid's move then shows "Cannot save the order") |
| `ntp on\|off`, `timeset ok\|fail` | Date & Time: Network Time as NtpGet reports it (the Network Time toggle sets it too), whether Set Manually > Save? works (`harness_stubs.cpp`) |
| `tz ZONE\|none`, `tzset ok\|fail\|denied`, `tzlist on\|off` | Date & Time > Time zone: the zone in use (default `Etc/UTC`; `none`: it cannot be read), what a change answers (done, failed, refused by PolicyKit: the page then tries the sudo path, which the harness does not have), the list (`fixtures/timezones.txt`, a real `timedatectl list-timezones`; `off`: none, as without timedated and zoneinfo files). A zone set becomes the process's `TZ` when the page reports the change, so the clocks follow it (`harness_stubs.cpp`) |
| `toast TEXT` (`\n` = new line) / `toast off`, `screensaver` (+ `wait`) | launcher toast (persistent) / hide it; arm the clock screensaver |
| `osd level TITLE PCT`, `osd mute on`/`off`, `loading TEXT` / `loading off`, `compatpage` | the media OSD of the brightness / volume keys (it hides itself after 1000 ms: a shot settles 700 ms after the command), the "Loading..." overlay, a compat page (the compat window is default, a flat placeholder screen, no external app) |
| `wifinet SSID IP`, `expect_text [end] TEXT` | the connected network the Wi-Fi fixture reports; fail the scene unless a visible label (of the native screen, else of its top layer: the sudo prompt) shows TEXT whole (`end`: a label whose text ends with TEXT) in a box wide enough for it (not cut with dots), see `settings_native_wifi_status` |
| `sudoprompt open\|busy\|wrong\|close` | the responsive sudo password prompt (`cp0_sudo_prompt_ui`, on the native display's top layer) without the sudo coordinator: open = SHOW_PROMPT (an input group around its field), busy = START_WORKER, wrong = SHOW_AUTH_ERROR, close = DESTROY_PROMPT. With `env HARNESS_SUDO prompt` a sudo request of the launcher (Settings > Apps' privileged step; `env HARNESS_APPS_PACKAGE job` makes the fake Store backend prepare one) opens the same prompt on the next cycle, and Cancel / Esc ends that request as cancelled; nothing ever runs |
| `wait MS`, `shot NAME` | advance the fake clock; settle 500 ms and save `out/<WxH>/NAME.png` |
| `panelshot NAME` | settle 500 ms and save the framebuffer as the panel shows it (its own orientation and size, no rotation undone) as `out/<WxH>/NAME.png`: which edge of the panel the picture's top is at (report 043). Not in the contact sheet |
| `header NAME [TITLE ...]` | the home grid's status strip rectangle (from its leftmost drawn pixel, the SSH indicator's reserved slot since 2026-10-30, `cp0::ui::status_strip_left()`, to the right edge, `bar_h` high) cut out as `out/<WxH>/header/NAME.png`, and the origin of the visible label whose text is one of TITLE as `NAME.txt`; `run.sh header` checks them against `header home` taken on the home grid. The home grid has no title (rebranding plan 025): without TITLE the origin written is the shell's title origin (`shell.title_x/title_y`) and the command fails if a label stands there |
| `switcher CARD...`, `switcher select ITEM`, `switcher off` | multitasking P-d: the switcher screen (`multitask/switcher_view.cpp`) with the cards named `Name=front\|running\|paused\|nr` (front: the app it was opened from; the icon is the tile's of that name); `select card0..3\|home\|closeall` moves the selection |
| `sheet NAME`, `sheet off` | the legacy sheet "Close NAME?": the screen shown now stands for the frozen app's frame (snapshot, written back dimmed to 38 %), only the sheet's box is drawn (the native clip); the command fails unless every pixel outside the box is the dimmed frame and the box is drawn |
| `dots NAME...` | these tiles' apps are left open (`native_ui::set_resident_apps`; tiles have the Exec line `tile:NAME`) |
| `tapitem ITEM [edge]`, `expect_event TEXT`, `expect_targets` | a tap on `card0..3`, `close0..3` (an X), `home`, `closeall`, `keep`, `sheetclose` (`edge`: 2 px inside the corner of its touch target farthest from what is drawn); fail unless the last thing the switcher / sheet reported is TEXT (`pick NAME`, `close NAME`, `home`, `close-all`, `cancel`, `keep`, `sheet-close`); fail unless every touch target shown is at least 7 mm both ways and the cards and the bar stay inside the panel, apart |
| `stroke letters\|digits X,Y ... [hold]` | the writing zone (report 041, profile `pda`): a pen stroke through the touch path (the router, the recogniser with the shipped tables, the ink, what it types); points in px from the box's top-left corner, a frame every 1 mm, each read at once, 10 ms of the fake clock apart; one point is a tap; then the lift and 400 ms (the heal window, the result, the fade), unless `hold` (the pen stays down: the ink of a stroke in progress; `release` lifts it) |
| `zonekey NAME [hold MS]` | a tap (or a press held MS) through the touch path on a strip key (`Keyb Tab Ctrl Alt Left Up Down Right Enter`, the band shown) |
| `expect_zone shown\|hidden`, `expect_osk shown\|hidden`, `expect_vkb auto\|off\|force\|zone`, `expect_hand 0\|1`, `expect_mode 0\|2\|3` | fail unless the writing zone's band is shown / hidden; unless the standard on-screen keyboard is shown / hidden; unless the on-screen keyboard rule launcher_osk last published (input.state `vkb=`) is that one; unless launcher_osk last published that input.state `hand=` (report 044: Handwriting + keyboard with the writing zone); unless Settings > On-screen keyboard reads as that value (`launcher_osk::mode()`: 0 Auto, 2 On, 3 Handwriting + keyboard; a stored 1, the old Off, reads as 0) |
| `tapbutton TEXT` | a tap (touch path) on the centre of the object holding the first visible label TEXT on the native screen (a native page's button, a cell of the Strokes training picker, a home tile) |
| `touchbutton TEXT` (+ `wait`, `release`) | as `tapbutton`, but the finger stays down until `release`: a hold on a home tile (`touchbutton Calculator`, `wait 3200`, `release` picks it up, study 046 P2) |
| `strokesuser`, `expect_file NAME [lines N\|has TEXT]` | before the first drawing command: the writing zone's user overlay is `out/<WxH>/runtime/config/strokes-user.txt`, empty at the start (Strokes training saves it there, its capture log `strokes-training.txt` next to it; report 042); fail unless that directory has the file NAME (with N lines starting `# training`, or containing TEXT) |
| `turnedapp on\|off` | a landscape fullscreen app run turned takes the panel (`display_handoff::enter_app_front`, the app in front for the keyboard rule) and draws a landscape test frame over the whole panel at rotate 0 as its own backend would (no zone, no strip); `off`: the launcher takes the panel back and draws everything again |
| `metrics NAME` | the layout service's numbers for the size (class, density and its source, tokens, text sizes, compat window and toolbar, home layout, `APPLAUNCH_SCREEN_*`, the per-launch environment of both modes, `screen.state`) as `out/<WxH>/NAME.txt`; for `run.sh contract` also `NAME.compat.env` / `NAME.fullscreen.env` (the panel variables + `app_launch_env::export_for_current()`, as `export ...;` lines) and `NAME.facts` (size, class, ppmm, rot, compat window, scale) |

PDA scenes (report 038 S2, profile `pda`, goldens at 480x640 and 480x800): `pda_home` (home grid, a compat app in its
1x window, the terminal with the fallback keyboard), `pda_settings` (titles whole beside the narrow strip, Screen,
DarkTime, System, the keyboard test page), `pda_switcher` (the switcher on the portrait panels, targets, taps, name
wrap), `pda_titles` (report 040: the 18 px narrow title with the longest titles whole, Date & Time, Set Manually,
On-screen keyboard and Keyboard test (their page titles "Screen keyboard" and "Key test" since the status strip's
reserved SSH slot, 2026-10-30: `SettingEntry::page_title`), and a breadcrumb, as a path and as one string, showing its
last part only);
`pda_switcher_dense` (review only: 480x640 at the deck's 11.3 px/mm, the name starting under the X). The
`metrics` scene also renders 480x640 and 480x800 with that profile and writes `metrics.turned.env` there (what a
landscape fullscreen app run turned gets); `run.sh contract` checks it with the probe. The `pda_*` scenes run without the
writing zone (`env APPLAUNCH_PDA 0`): the portrait launcher of S2, kept as its regression.

PDA mode scenes (report 043, phase S7b; golden at 480x800, also rendered at 800x1280, the FZ-M1 tablet turned to
portrait, board `pc-fzm1-2`, 150x93 mm): `pda_mode` (Settings > Screen with the PDA mode row and its value, the list of
Off / On rotate left / On rotate right with the check on the profile's side, the restart question, Esc: nothing changes;
a panelshot of the home grid at rotate 90), `pda_mode_right` (`env APPLAUNCH_ROTATE 270`: the panelshot with the
picture the other way up, the list checked on On rotate right, the Keyboard test written with strokes and tapped on the
band and the standard keyboard through the touch path at rotate 270, a landscape app run turned at rotate 180 and its
panelshot).

Tablet scenes (report 043, review only, 800x1280 on the profile `pda` with `APPLAUNCH_BOARD pc-fzm1-2` and 150x93 mm; the pictures of `docs/PDA-PROFILE.md` "Large screens: the tablet"): `pda_tablet` (the home grid, Settings > System > Keyboard test with the band), `pda_tablet_terminal` (the terminal, the key strip, Ctrl armed); the PDA mode picture is `pda_mode`'s `pda_mode_choice`.

Writing zone scenes (report 041, 042, profile `pda` with the zone, goldens at 480x640 and 480x800): `zone_home` (the
home grid: no text field, the band hidden, the grid on the whole panel), `zone_text` (Settings with the band hidden, then
System > Keyboard test: its field focused shows the band; written with pen strokes: the mode line after the shift
gesture and after the extended prefix, "Lit £1" typed, a stroke in progress, an accent; Esc ends the editing and the
band hides), `zone_strip` (the terminal: the band shown, Ctrl armed, a "c" stroke sending Ctrl+C, a pound sign reaching
the terminal as text; Keyb: the terminal's standard keyboard with its close cross in the band's place, s, u, Enter
reaching the PTY as keys, the cross bringing the band back), `zone_keyboard` (Settings > On-screen keyboard on the PDA
profile: Handwriting + keyboard by default, Keyb on the Keyboard test kept for the next field (the number pad with the
cross), the cross; the choice list with its three values; On: the standard keyboard only, shown with a physical
keyboard; Auto: the standard keyboard, no band, hidden with a physical keyboard), `zone_old_off` (review only: a stored
Off reads as Auto on the PDA profile too), `zone_turned` (the Keyboard test with the band, then a landscape app run turned: the whole panel
its, no zone; the keyboard rule auto while it is in front, zone and the band again after), `zone_switcher` (the switcher,
its targets; the screensaver), `zone_sudo` (golden at 480x800 only, also rendered at 800x1280 with the tablet's board and
panel size and checked there by its `expect_*` lines: Settings > Apps > Available, an install asks for the sudo password;
the prompt's Password field on the top layer gets the band like any field, the dialog above it, two letters written and
shown with the eye, Keyb and a tapped key, the cross, Enter (Authenticating..., the band hides), a wrong password (the
band again), Esc: Cancelled). The `metrics` scene's text adds the band at those sizes. `strokes_training` (report 042: Settings > System > Strokes training with the band pinned: the first glyph b with its guide, three attempts judged (recognised, read as another glyph, refused) as thumbnails with the result line, the glyph done and the next one, the glyph picker with its marks, a picked glyph trained, the Reset all dialog, the re-test, the summary; the overlay and the capture log checked with `expect_file`).

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
brings the keyboard back, at all five sizes) and `osk_policy` (`APPLAUNCH_OSK=auto` at 640x480 and 800x480: a stored
Off, gone since report 042, reads as Auto, a physical keyboard appears, On through the choice list keeps the keyboard
shown) are review-only scenes too.
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
`home_order` (study 046 P1, review-only, at 640x480, 480x800 and 720x720): a stored home order (`config home_order`
before `apps`; the harness gives each tile the board's id, `harness_launch.cpp` `tile_id`) checked with `expect_same`
against the same tiles listed in that order, then Settings > Launcher's "Reset app order" row, its question and Reset.
`home_move` (study 046 P2, review-only, at 640x480, 480x800 on the profile `pda`, 720x720 and 1024x600): move mode on the
home grid. A hold let go after its bar showed (no launch), the 3 s hold mid-fill and the tile picked up (outline, move
badge, the caption in the status line), Up / Left and Enter (saved; a new app list keeps it), Ctrl+O with a swipe that
scrolls the grid and a tap that places, Ctrl+O / Right / Down / Esc (`expect_same` with the grid before), a full config
store ("Cannot save the order"), the touch-only caption and a tap on the picked tile, a slow swipe in normal mode, the
screen dimming over a carried tile, and a short tap that still launches; the order is checked with `expect_tiles`.
`home_move_rows` (study 046 P2, review-only, at 640x480 and 480x800 on the profile `pda`): 20 tiles, the first one
carried with the arrows to the last row and back, then halfway and placed by a tap; `expect_grid_view` checks that the
view follows by the least scroll (no row snapped to the top, no blank space past the content).
`settings_native_timezone` (review-only, at 320x170, 480x320, 640x480 and 1280x800): Date & Time > Time zone from
Etc/UTC, the row with its chevron, the regions, America's zones, a failed and a refused change, the change done (row and
clocks), the picker reopened on the zone, Esc up one level, Network Time switched off without touching the zone, Info
with the new offset, the home grid's clock, then Europe > Paris. `settings_native_timezone_missing` (review-only,
480x320 and 640x480): the same page without timedated (zone Unknown, no list: only UTC, a failed change).
`settings_rows_deck`, `settings_rows_deck_dev`, `settings_rows_pi3a`, `settings_rows_hackberry`,
`settings_rows_hackberry_dev`, `settings_rows_pc` (review-only): the same Settings options on every board. Each pins
the root menu and System with `expect_rows` (and the About page's lines: a "Board" line only with a board profile) for
the deck without a board profile, pi3a-wsdsi43, hackberry-cm5 in its desktop session, pc-fzm1-2, release and
development, at 480x320 to 1280x800 (`docs/dev/native-settings-page-howto.md` section 5).
`ssh_status` (the SSH indicator, 2026-10-30; goldens at 640x480 and 480x800 (`pda` profile), also 720x720 and
1024x600): no session (the slot empty, `header` against the home grid), idle (steady blue), active in both blink phases
(the third tick back to the first, `expect_same`), two sessions (the same glyph), the session gone, the Settings root
with a session (the glyph right of the Esc), Settings > System > SSH indicator On / its choice page / Off (the glyph gone,
sessions not looked for) / On again, and the clock screensaver over a session with traffic (it covers the strip; a tap
wakes it, the glyph as before; that it does not blink behind the clock is not visible in a shot, the unit test
`test_ssh_indicator_model.cpp` checks that rule). `ssh_status_battery` (same sizes and goldens): a battery board (`battery` before `home`), "85%" without a
session, the glyph in its place idle and active, charging, the session gone ("85%" back), the Settings root without and
with a session (the Esc where "100%" puts it).
`battery_status`, `settings_battery`, `settings_battery_none` (decision X12, review-only, at 320x170 to 1280x800; the
tablet's density at 1280x800): the home strip with a battery discharging, charging, full, low with Bluetooth off, gone;
Settings with the Battery row and its page (discharging, charging, full, the battery removed while it is open); no
Battery row and an unchanged strip with an AC adapter alone or nothing.
`settings_speaker` (report 030 c-1, review-only, the five sizes, `sound card`): Speaker after Screen in the root
menu, its slider at 50 %, Right to 60 %, Left twice to 40 %. `settings_speaker_none` (review-only, `sound hdmi`): no
Speaker row. `settings_speaker_bluetooth` (review-only, 720x720, the HackBerry's profile, `sound bluetooth`,
`backlight none`): Speaker from the Bluetooth sink, Screen with DarkTime alone. `settings_backlight_onoff`
(review-only, `backlight onoff`): Screen > Brightness as On / "Off 10 s". `settings_capability_hints` (review-only,
`APPLAUNCH_HINT_BACKLIGHT=button`, `APPLAUNCH_HINT_SOUND=bluetooth:...`, no backlight, HDMI only): the Info rows
"Speaker: pair ... in Bluetooth" and "Brightness: use the button on the case".
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

- Pages that need live services beyond the fixtures (Bluetooth scan/pairing, Apps sources, sudo itself: only its
  prompt is drawn, `sudoprompt`): add
  fixtures in `src/harness_services.cpp` as P3 scenes need them. Storage reads the host's mounts: on a PC it shows
  "SD card information unavailable.".
- Stock apps (only a placeholder picture), real programs in the terminal pages (the terminal runs on a fake PTY that
  echoes keys; no shell, ssh, nano), the hold-Esc controller (not linked), games, touch feel and speed. The harness
  proves layout, not behaviour on the board.
- Changes to `builtin_app_registry.cpp`'s table must be copied to `src/harness_stubs.cpp`; changes to the
  device LVGL config to `config/lvgl_config.h`.
