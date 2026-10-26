# 043 - PDA mode on large screens, landscape PDA and a runtime orientation setting (study S7)

Status: study, 2026-10-27. Worktree `launcher-s7`, branch `pda-s7-study` from `github/main` e07a75f (v0.8.8 + #101). No
product code changed or committed; the renders were made with the real launcher code in the render harness, plus one
study-only patch for the landscape band (section 8: `043-pda-large-screens/experiment/`). No board touched.
Sources read: `cp0_zone.[ch]`, `cp0_zone_ui.cpp`, `cp0_ui_metrics.c`, `cp0_ui_osk.c`, `cp0/cp0_lvgl_dpi_scaled.c`,
`cp0_fb_output.h`, `main/src/main.cpp`, `main/ui/{launcher_osk,launcher_zone,desktop_entry,desktop_app_loader,
app_launch_env,launch}.*`, `multitask/switcher_model.*`, the Settings pages, `pizero2w/install.sh`, both systemd units,
`tools/boards/boards.json`, reports 028, 038-042, `docs/PDA-PROFILE.md`.

## Summary

1. **Portrait PDA already runs on the tablet with no code change** (board.conf `APPLAUNCH_LOGICAL=800x1280`, `APPLAUNCH_ROTATE=90`, `APPLAUNCH_PDA=1`): home, Settings, band, terminal, training, switcher, turned apps all render; only the band is wrong (a third of 1280 px = 48 mm boxes).
2. **Landscape PDA is refused by one test** (`cp0_zone_layout()`: portrait only); the backend, touch router, band UI, keyboard inset and policy are orientation-free. A study patch lifting it renders correctly at 1280x800, 1024x600, 800x480 and 720x720.
3. **Band height rule for every panel:** the writing zone 31 mm (the 480x800 reference: boxes 29 mm), capped at 32 mm in portrait (480 px panels unchanged) and, in landscape, at the on-screen keyboard's own share of the screen.
4. **Landscape layout recommended:** one row at the bottom, a 3x3 key block (9 mm keys, d-pad arrows) left of the two boxes: 1280x800 keeps 534 of 800 px above a 266 px band with 29 mm boxes; 800x480 gets 26 mm boxes in half the screen (the keyboard takes the same); 720x720 falls back to the strip over the boxes.
5. **Compatible** = short side at least 480 px and the band fits (boxes at least 20 mm, 240 px left above it): the tablet both ways, 1024x600 both, the 800x480 DSI panels both, the HackBerry landscape only; deck and Luckfox none.
6. **Runtime setting:** Settings > Screen > PDA mode = Off / Portrait / Landscape (compatible values only), stored in config.json; at start the launcher derives `APPLAUNCH_ROTATE` / `_LOGICAL` / `_PDA` from board.conf before the display opens; a change restarts the launcher (exit status 76, restarted by both units).
7. **The install profile only sets the default** (pi3a-wsdsi43-portrait = Portrait); a stored choice wins over board.conf, which also ends report 040's "a plain install.sh reverts the PDA to landscape".
8. **Rotation is the existing CPU output stage** (90 on a landscape framebuffer: Pi 3A+ DSI, tablet; 0 on a portrait one: HyperPixel 4) and touch follows it; the tablet's desktop variant stops lightdm and draws on fb0 itself, nothing X11-specific is needed.
9. **Must fix with landscape:** apps inherit `APPLAUNCH_PDA=1`; once the core accepts landscape, an app built on a newer pin would draw its own band, so the launcher must export `APPLAUNCH_PDA=0` per launch.
10. **Phases:** S7a band rules (renders and unit tests only), S7b runtime setting + restart (units touched), S7c owner tests on the tablet and the Pi 3A+; unknowns: is the tablet's pen a separate input device, CPU rotation speed at 1280x800x32.

The owner decisions are in section 7, each with a recommendation.

## 1. Facts used

| Fact | Value | Source |
| --- | --- | --- |
| Tablet panel | FZ-M1 mk2, 7" 1280x800 16:10, owner: about 151x94 mm, 8.5 px/mm (215 ppi) | coordinator, 2026-10-27 |
| What the launcher knows on the tablet | `APPLAUNCH_PANEL_MM` written by install.sh: the EDID size when the connector has one (report 028: 152x94), else the known-model table `150x93` (`install.sh` `pc_known_mm`); boards.json's test profile says 150x93. Both give 8.4-8.6 px/mm, within 2 % of the owner's figure | `install.sh:311-350`, `boards.json` fzm1 |
| Density in the renders | 1280x800 and 800x1280 at `150x93` = 8.57 px/mm (the panel size is written landscape and turned with the canvas, `cp0_ui_density_resolve()`, `cp0_ui_metrics.c:165`) | harness logs |
| Pi 3A+ 4.3" DSI | 94x56 mm, 8.54 px/mm; framebuffer 800x480 (landscape) | `install.sh` profiles |
| Tablet framebuffer | i915drmfb `/dev/fb0` 1280x800 XRGB8888; the desktop unit stops lightdm (`Conflicts=`) and the launcher draws on fb0 with the dpi-scaled backend, sized by board.conf (`APPLAUNCH_LOGICAL` from `virtual_size` at install) | `cyberdeckzero-desktop.service`, report 028 |
| Tablet touch | Sharp 07EA0004, direct, 2 contacts reported (no 3-finger gesture), picked by capability (`touch_score()`: direct 4 + multi-touch 2 + BTN_TOUCH 1) | report 028, `cp0_lvgl_dpi_scaled.c:337` |
| Owner's box size feedback | pda.4 boxes 160 px = 19 mm on 480x800: "too small"; pda.5 boxes 251 px = 29 mm: kept | report 041 section 12 |
| The on-screen keyboard's height | 330 px at 1280x800 (41 %), 236 px at 800x480 (49 %); class caps 45 % Large / 50 % Wide and Square / 62.5 % Standard (`cp0_ui_osk.c:57-66`) | renders `1280x800_land_osk`, `800x480_land_osk` |
| Apps' pin | `apps/_common/LAUNCHER_REF` = d0cb456 (v0.8.5): no app has the writing zone code yet | apps repo |

## 2. (a) What is gated on what today

| # | Rule | Trigger (file) | 1280x800 landscape | 800x1280 portrait |
| --- | --- | --- | --- | --- |
| 1 | Writing zone exists | `APPLAUNCH_PDA=1` **and** `cp0_zone_layout()` accepts the canvas: portrait only (`h > w`), app area at least 300 px (`cp0_zone.c:31,53`; `cp0_lvgl_dpi_scaled.c:1292`) | **refused**: log "has no room for the writing zone (portrait only)" | yes |
| 2 | Band height | `write_h = max(6 pad, min(h/3, h - row_h - 400))` (`cp0_zone.c:45-48`, `cp0_zone.h:55-60`) | n/a | **427 px = 50 mm** zone, boxes 411 px = 48 mm, band 478 (37 % of the panel) |
| 3 | Handwriting + keyboard value, its default, Strokes training row | the zone exists (`launcher_zone::active()`: `launcher_osk.cpp:49-58`, `settings_strokes_page.cpp:916`) | absent (render `1280x800_land_system`: Auto) | present |
| 4 | `input.state vkb=zone` | band shown and the launcher in front (`launcher_osk.cpp` `publish_vkb`) | n/a | holds |
| 5 | Narrow status strip (72 %) | computed canvas under 600 px wide (`cp0_ui_metrics.c:63-64,405`) | no | no (800 wide): full strip, as intended |
| 6 | Narrow page header (18 px title, last breadcrumb part) | computed, under 600 wide, deck shell (`cp0_ui_narrow_header()`, `:353`) | no | no: 32 px titles, "System > Keyboard test" fits (render) |
| 7 | Multitasking switcher | both sides of the panel at least 480 (`switcher_model.cpp:94`, `kSwitcherMinSide`) | yes | yes |
| 8 | Fixed-pixel apps hidden | `desktop_fit()`: a non-fullscreen, non-terminal app on any portrait canvas (`desktop_entry.cpp:99-101`) | shown (compat window 4x) | hidden, although the 2x window (640x340) fits at 800 wide |
| 9 | Built-in fixed-pixel tiles hidden | `APPLAUNCH_HIDE_TILES` text in the profile only (`install.sh:565`; pc profiles hide the Store only, `:658`) | Store only | **not hidden**: a portrait tablet would show IP Panel, Snake, Tank unless the rule moves into code |
| 10 | Landscape apps run turned / `X-Portrait` | `desktop_fit()` on a portrait canvas; `turned_rotation()` = a quarter turn back (`desktop_entry.cpp:102-109`, `app_launch_env.hpp:56-63`) | not used: apps native, no turn | turned to 1280x800 at rotate 0 = the framebuffer's own orientation: no CPU turn for apps (render `800x1280_por_turned`) |
| 11 | Full-height native draw buffer | `g.lh > g.lw` (`cp0_lvgl_dpi_scaled.c:1340`) | 96 lines (as today) | 800x1280x4 = 4.1 MB (fine on 3.8 GB) |
| 12 | Number pad full width | `h > w` (`cp0_ui_osk.c:122`) | letter-key width | a third of the row (about 250 px keys) |
| 13 | Image cache 1 MB | every build (`*config_defaults.mk:81`), no gate | 28 tiles, 80 px icons: fits | same |
| 14 | Band hidden behind the switcher | `launcher_zone::screen_changed()` (`multitask_ui.cpp:353`) | holds | holds |
| 15 | Density | `APPLAUNCH_PANEL_MM` written landscape, turned with the canvas (`cp0_ui_metrics.c:165`) | 8.57 px/mm | 8.57 px/mm |
| 16 | Touch mapping | `APPLAUNCH_TOUCH_ORIENT=buffer`: raw axes normalised, turned back through `APPLAUNCH_ROTATE` (pc and Pi profiles set it) | holds | holds (no extra setting per rotation) |
| 17 | Zone in an app's process | apps inherit `APPLAUNCH_PDA=1`; harmless only because the core refuses landscape (report 041 D11) | **breaks once rule 1 accepts landscape** (with a pin past v0.8.8) | turned apps are landscape: same |
| 18 | `screen.state` of a turned app | still the launcher's portrait panel (report 040 item 2, S4) | n/a | holds (open) |
| 19 | Classes | portrait canvas = class Portrait (Standard numbers, keyboard cap 45 %); 1280x800 = Large | Large, header 64 | Portrait, header 56 |

Home grid tile counts (one page, band hidden on the home grid at every size): 1280x800 7x4 = 28, 800x1280 4x6 = 24,
1024x600 7x4 = 28, 600x1024 4x7 = 28, 800x480 4x2 = 8, 720x720 3x3 = 9, 480x800 and 480x640 3 columns (report 042).

## 3. (b) Compatible resolutions and the band geometry

### 3.1 Renders of today's code

- **Portrait on the tablet (800x1280) and on a 7" 1024x600 turned (600x1024):** everything works with the
  `pi3a-wsdsi43-portrait` values plus the panel size: the home grid (4x6), Settings, the Keyboard test written with
  strokes, the terminal with Ctrl armed, Keyb to the standard keyboard, Strokes training, the switcher, a landscape
  app turned. The band is too tall: zone 427 px (50 mm), boxes 527x411 + 249x411 px (61x48 + 29x48 mm).
  `800x1280_por_text.png`, `800x1280_por_term.png`, `800x1280_por_train.png`, `800x1280_por_turned.png`,
  `800x1280_por_home.png`, `800x1280_por_switcher.png`, `600x1024_por_text.png`, `sheet_por_text.png`.
- **Landscape with `APPLAUNCH_PDA=1`:** no zone at 1280x800, 1024x600, 800x480, 720x720; the Keyboard test gets the
  standard keyboard; Settings offers Auto / On only. `1280x800_land_osk.png`, `800x480_land_osk.png`,
  `1280x800_land_system.png`.

### 3.2 The experiment (study patch, three layouts)

| Layout | What it is |
| --- | --- |
| stack | today's portrait layout allowed in landscape: the key strip over two boxes, full width (68 / 32 %) |
| centre | the strip and the boxes centred; letters box 1.6 x its height wide, digits 0.8 x (the portrait reference's shape) |
| side | no strip row: a 3x3 key block (Keyb Up Tab / Left Down Right / Ctrl Alt Enter) left of the two boxes, centred; when the panel is too narrow the boxes give up width |

Height rule tried: target zone 31 mm (the reference's 267 px at 8.54 px/mm); landscape capped at the keyboard's class
cap minus the strip row; portrait `min(third, 32 mm)`.

| Panel (px/mm) | Layout | Band (px) | Boxes (px, mm) | Above the band | Keys |
| --- | --- | --- | --- | --- | --- |
| 1280x800 tablet (8.57) | stack | 319 | 850x246 + 400x246 (99x29 + 47x29 mm) | 481 | 5 mm strip |
| | centre | 319 | 393x246 + 196x246 (46x29 + 23x29) | 481 | 5 mm strip |
| | **side** | **266** | 393x246 + 196x246 (46x29 + 23x29) | **534** | 101x78 px (12x9 mm) |
| 1024x600 7" (6.60) | stack / centre | 246 | 680 or 302 x 189 (189 px = 29 mm) | 354 | 5 mm strip |
| | side | 205 | 302x189 + 151x189 (46x29 + 23x29) | 395 | 78x60 (12x9 mm) |
| 800x480 DSI (8.54) | stack / centre | 240 | 173 px high = **20 mm** | 240 | 5 mm strip |
| | side | 240 | 320x224 + 161x224 (37x26 + 19x26) | 240 | 93x72 (11x8 mm) |
| 720x720 HackBerry (10.0) | stack | 360 | 473x286 + 223x286 (47x29 + 22x29) | 360 | 5 mm strip |
| | side | 310 | 207x294 + 104x294: letters box 21 mm wide, too narrow | 410 | 123x95 |
| 800x1280 tablet portrait | today | 478 | 527x411 + 249x411 (48 mm high) | 802 | 5 mm strip |
| | 32 mm cap, stack / centre | 325 | 258 px = 30 mm high | 955 | 5 mm strip |
| 600x1024 portrait (6.66) | today / 32 mm cap | 382 / 254 | 325 / 197 px = 49 / 30 mm | 642 / 770 | |
| 480x800 reference | 32 mm cap | 318 (unchanged) | 310x251 + 146x251 (unchanged) | 482 | |
| 480x640 | 32 mm cap | 240 (unchanged) | 171 px (unchanged) | 400 | |

Renders: `sheet_lstack_text.png`, `sheet_lcentre_text.png`, `sheet_lside_text.png` (each: the four landscape sizes, the
text written and a stroke in progress), `1280x800_lstack_text.png`, `1280x800_lcentre_text.png`,
`1280x800_lside_text.png`, `1280x800_lside_ink.png`, `1280x800_lside_term.png` (Ctrl armed),
`1280x800_lside_keyb.png` (Keyb: the standard keyboard, 330 px, with its cross), `1280x800_lside_train.png`,
`1280x800_lside_switcher.png`, `1280x800_land_home.png`, `1024x600_lside_text.png`, `1024x600_lcentre_text.png`,
`800x480_lstack_text.png`, `800x480_lside_text.png`, `720x720_lstack_text.png`, `720x720_lside_text.png`,
`sheet_pcap_centre.png` (portrait with the cap at 800x1280, 600x1024, 480x800, 480x640), `800x1280_pcap_stack_text.png`,
`800x1280_pcap_centre_text.png`, `600x1024_pcap_centre_text.png`. All viewed; nothing cut or overlapping except the side
layout at 720x720 (narrow boxes, as the table says).

### 3.3 Proposed rules

**Band height (every panel, the density from `APPLAUNCH_PANEL_MM` / `cp0_ui_mm_px()`):**

- writing zone target **31 mm** (`CP0_ZONE_WRITE_MM_X10 310`): boxes about 29 mm high, the size the owner kept;
- **portrait:** `min(third, 32 mm, h - row_h - 400)` (today's rule plus the 32 mm cap): the 480x800 and 480x640 numbers
  do not move (267 and 187 px), so their goldens stay identical; 800x1280 gets 274 px (30 mm boxes)
  instead of 427;
- **landscape and square:** `min(31 mm, keyboard cap - strip row)`: the band never takes more of the screen than the
  on-screen keyboard is allowed to (45 % Large, 50 % Wide / Square, 62.5 % Standard);
- the owner asked whether landscape wants fewer mm: on the tablet it does not (the full 29 mm boxes take 266-319 px,
  less than the keyboard's 330); only 800x480 gets fewer (26 mm with the side layout, 20 mm with a strip row).

**Compatible (the values Settings offers):**

- **Portrait:** the portrait canvas is at least 480 px wide and the existing rule leaves the zone at least 6 pad high
  (in practice at least 480x640). Tablet 800x1280, 600x1024, the 4.3" / 5" DSI 480x800, a HyperPixel 4 480x800 (rotate
  0) qualify; the Luckfox 320x480 and the deck do not; a square panel never.
- **Landscape (square included):** short side at least 480 px (the multitasking rule), boxes at least **20 mm** high
  and at least **240 px** of the screen above the band. Tablet, 1024x600, 800x480 (the Pi 3A+ and Pi 4 B DSI panels; the
  HyperPixel 4 once its panel size is in its profile) and 720x720 qualify; the deck 640x480 (11.3 px/mm: 180 px left) and 480x320 do not.
- The check is the layout itself: Settings computes the metrics of each candidate canvas and calls `cp0_zone_layout()`
  on it (pure C, no display needed), so the rule lives in one place.

**Band layout:** portrait keeps today's strip over the boxes (centred boxes are an option where the panel is wider than
the reference: `800x1280_pcap_centre_text.png`); landscape uses the side layout when the key block and both boxes fit
at 1.6 / 0.8 x their height (1280x800, 1024x600, 800x480 with narrower boxes), else the strip over the boxes (720x720).

## 4. (c) The runtime setting

### 4.1 Values

**Settings > Screen > PDA mode**: Off / Portrait / Landscape. Only the compatible values are listed; the row is absent
when neither PDA value is compatible (the deck, the Luckfox), which keeps the deck's Settings unchanged. On a square
panel the landscape value could read "On". Changing it shows one confirmation ("The launcher restarts to turn the
screen. Open apps are closed.") and restarts the launcher.

The alternative, Orientation (Auto / Portrait / Landscape) plus a PDA on/off switch, adds a portrait launcher without the
band (today's `APPLAUNCH_PDA=0` portrait, kept as a regression only) and an "Auto" that needs the tablet's accelerometer
and a restart per turn of the device. Not recommended now.

### 4.2 What happens at start (no live re-layout)

Every page, the home grid, the compat window, the band display and the published `screen.state` are built for one
canvas when the display opens (`cp0_dpi_scaled_create()`, from the environment). A live switch would rebuild all of
them; a restart is a few seconds and reuses what exists. The launcher's `main()` (before `cp0_lvgl_run()`, so before the
display opens) does:

1. read the stored `pda_mode` (config.json, `~/.config/cardputerzero/`, the same store as `osk_mode`; absent = the
   profile's default, i.e. leave the environment as board.conf set it);
2. take the board's base canvas from board.conf (`APPLAUNCH_LOGICAL`, `APPLAUNCH_ROTATE`; a portrait base, such as the
   PDA profile's 480x800 rotate 90, is brought back to landscape with `turned_rotation()`: 800x480 rotate 0);
3. set `APPLAUNCH_LOGICAL`, `APPLAUNCH_ROTATE` and `APPLAUNCH_PDA` with `setenv()`: Off = the landscape base, no zone;
   Landscape = the landscape base with `APPLAUNCH_PDA=1`; Portrait = the base turned a quarter (`rot + 90`, logical
   swapped) with `APPLAUNCH_PDA=1`; on a portrait canvas also add the fixed-pixel built-in tiles to the default
   `APPLAUNCH_HIDE_TILES` (rule 9 moves from the profile text into code);
4. keep the originals as `APPLAUNCH_BOARD_LOGICAL` / `APPLAUNCH_BOARD_ROTATE` for Settings (which values are
   compatible) and for the log line.

`APPLAUNCH_PANEL_MM` (written landscape), `APPLAUNCH_TOUCH_ORIENT=buffer` and `APPLAUNCH_COMPAT_SCALE` (lowered by the
layout service when it does not fit) need no change. Apps inherit the derived values, so a fullscreen app sees the same
panel the launcher draws; turned apps keep their per-launch overrides.

| Board (profile) | Framebuffer | Base canvas | Portrait | Landscape |
| --- | --- | --- | --- | --- |
| Pi 3A+ 4.3" DSI (`pi3a-wsdsi43`) | 800x480 RGB565 | 800x480 rot 0 | 480x800 rot 90 (today's PDA profile) | 800x480 rot 0 + band (side: 26 mm boxes) |
| Pi 3A+ DSI portrait (`pi3a-wsdsi43-portrait`) | 800x480 | 480x800 rot 90, so landscape base 800x480 rot 0 | same as today | as the row above |
| Pi 4 B Freenove 5" (`pi4b-dsi`) | 800x480 | 800x480 rot 0 | 480x800 rot 90 | 800x480 + band |
| Pi 5 HyperPixel 4 (`pi5-hyperpixel4`) | 480x800 (portrait) | 800x480 rot 90 | 480x800 **rot 0**: no CPU turn | 800x480 rot 90 + band (its profile has no panel size: at the 11.3 px/mm default the boxes compute as 19.8 mm, refused, until it is measured, about 9.3 px/mm) |
| HackBerry CM5 | 720x720 | 720x720 rot 0 | not offered (square) | 720x720 + band (strip layout) |
| FZ-M1 tablet (`pc-fzm1-2`) | 1280x800 XRGB8888 | 1280x800 rot 0 | 800x1280 rot 90 | 1280x800 + band (side) |
| Pi 3A+ Luckfox 3.5" | 320x480 | 480x320 rot 90 | no (320 wide) | no (128 px left) |
| Deck (no profile) | 640x480 | 640x480 | no | no |

### 4.3 The install profile and the stored choice

- install.sh keeps writing board.conf as today; `pi3a-wsdsi43-portrait` keeps its values (`APPLAUNCH_PDA=1`, 480x800
  rot 90), so a board installed with it starts in Portrait while nothing is stored: the profile is the default.
- Once the user picks a value it is stored in the user's config.json, which install.sh never touches, and it wins over
  board.conf's geometry. A later plain `install.sh` that rewrites the portrait profile back to landscape (report 040
  item 3) then changes nothing visible.
- A `--board ...-portrait` profile for the tablet is not needed: the pc profile plus the setting cover it.

### 4.4 Restart

| Unit | Today | Change |
| --- | --- | --- |
| `cyberdeckzero.service` (appliance, user unit) | `Restart=on-failure`: any non-zero exit restarts it | none needed; add `RestartForceExitStatus=76` to make it explicit |
| `cyberdeckzero-desktop.service` (desktop boards, the tablet) | `Restart=no`; `ExecStopPost=` gives the screen back to lightdm | `RestartForceExitStatus=76`, and `cyberdeckzero-console restore-desktop` returns at once when `$EXIT_STATUS` is 76 (systemd passes it to `ExecStopPost=`), else lightdm would start and stop the restart through `Conflicts=` |

In the launcher: a `launcher_session::request_restart()` next to the existing `request_quit()` (the same orderly end of
the main loop: teardown, then `main()` returns 76). Open apps die with the unit's processes, hence the confirmation.
An in-process re-exec (`execv` of itself) would avoid the unit change but needs every descriptor close-on-exec (a
touch device still grabbed by the old descriptor would starve the new one) and the multitasking children handled: more
risk for no gain. Install tests: the unit texts, the helper's 76 path (its `APPLAUNCH_SYSTEMCTL` test hook exists).

### 4.5 Rotation, touch and pen

- **Physically landscape framebuffer** (Pi 3A+ DSI, Pi 4 B DSI, the tablet): Portrait = rotate 90 (or 270, the other side
  up), done by the CPU in the output stage (`cp0_fb_output.h`: every blit, both depths). Measured on the Pi 3A+ in
  S2/S3 at 480x800 RGB565; not measured at 1280x800x32 on the Atom/Core m5 (about 4 MB per full frame; partial redraws
  are smaller). Turned landscape apps on such a panel run at rotate 0, the panel's own orientation: no turn for them.
- **Physically portrait framebuffer** (HyperPixel 4, Luckfox): Portrait = rotate 0, no CPU turn; their landscape base is
  the turned one.
- **Touch** follows the rotation in buffer mode (normalised raw axes, turned back through `APPLAUNCH_ROTATE`): no new
  setting per orientation. The tablet profile already has `APPLAUNCH_TOUCH_ORIENT=buffer` and `TOUCH_DEV=auto`.
- **Pen (unknown, I):** if the tablet's pen is a passive or capacitive stylus it is the touch device and works as on the
  Pi. If it is an active digitizer it is a second evdev device (`BTN_TOOL_PEN`, single-touch ABS_X/Y, direct, score 5);
  the reader opens one device, the touch screen scores 7 and wins, so the pen would be ignored. Probe first
  (`cat /proc/bus/input/devices` on the tablet, pen near the screen); a second device in the touch reader is a separate
  medium-size change.
- **Desktop variant on the tablet:** the launcher does not run under X: starting `cyberdeckzero-desktop.service` stops
  lightdm, the launcher opens fb0 itself, sized from board.conf (`APPLAUNCH_LOGICAL=1280x800`, `APPLAUNCH_PANEL_MM`),
  and the derivation of 4.2 applies unchanged. XFCE's own rotation is not involved; the desktop comes back landscape
  after Quit to desktop whatever the PDA mode.

## 5. (d) Landscape PDA design

- **Where the band goes:** at the bottom, the whole width as one display (the backend's band display needs no change):
  1280x800 and 1024x600 the side layout (key block left, boxes right of it, the group centred); 800x480 the side layout
  with narrower boxes (37 and 19 mm wide, 26 mm high); 720x720 the strip over the boxes (29 mm high). A band beside the
  app area (a vertical column) would need a horizontal inset in every page and a narrower native display: not proposed.
- **Strip and keys:** the same keys and state machine (Keyb, Tab, Ctrl, Alt, the arrows, Enter; sticky modifiers;
  held arrows); in the side layout they are 9 mm keys in a d-pad block, finger size, where the strip's are 5 mm.
- **Keyboard policy:** unchanged from portrait (report 042): the band shows where the fallback keyboard would (a text
  field, the terminal, no physical keyboard counted; the tablet's built-in AT keyboard is ignored by
  `APPLAUNCH_KBD_IGNORE=bus:0011`, so the band shows unless a USB or Bluetooth keyboard is there), Keyb puts the standard
  keyboard in its place with the close cross, Settings > On-screen keyboard offers Handwriting + keyboard (its default
  in PDA mode), Strokes training is listed.
- **Multitasking and home grid:** the switcher exists at every compatible size (short side at least 480) and hides the
  band while it is up (`1280x800_lside_switcher.png`); tile counts are the canvas's own (section 2). The tablet has no
  three-finger gesture (2 contacts): the clock long-press, Alt+Tab and the Windows key double tap remain the routes.
- **Apps in landscape PDA:** fullscreen apps are native (`desktop_fit()` sees no portrait canvas): no turned logic, no
  `X-Portrait`, no CPU turn. Compat apps run in their window (4x at 1280x800). An app in front keeps the standard
  keyboard rule (`vkb=auto`): the band is the launcher's until S4. The inherited `APPLAUNCH_PDA=1` must become
  `APPLAUNCH_PDA=0` in the per-launch environment (`app_launch_env`), or the backend must skip the zone when
  `APPLAUNCH_APP_MODE` is set; today no app has the zone code (pin v0.8.5), so this costs nothing to do first.

## 6. (e) Effort, risk and phases

| Phase | Content | Size (estimate) | Risk | Tested by |
| --- | --- | --- | --- | --- |
| **P0 (optional, now)** | owner hand-edits the tablet's `/etc/applaunch/board.conf` (`APPLAUNCH_LOGICAL=800x1280`, `APPLAUNCH_ROTATE=90`, `APPLAUNCH_PDA=1`, keep a copy) on v0.8.8 amd64 and restarts the launcher: touch and pen in portrait, repaint speed, writing feel (boxes 48 mm, too tall but usable); `cat /proc/bus/input/devices` with the pen | none (no code) | low (one file, copy kept) | the tablet |
| **S7a** band rules | `cp0_zone_layout()`: the 32 mm portrait cap, landscape and square with the keyboard cap, the side layout and its hit test (`keys_r`); `APPLAUNCH_PDA=0` per launch; harness profile for a landscape PDA panel (32 bpp, the tablet's mm) and goldens at 1280x800, 1024x600, 800x480, 720x720, 800x1280 (the 480x800 / 480x640 goldens must stay identical) | about 250 lines + tests + 30 goldens; 0.5 M tokens | low: landscape needs `APPLAUNCH_PDA=1`, which no landscape profile sets | renders, `test_zone.c`, contract probe |
| **S7b** runtime setting | the pure derivation (unit-tested table of section 4.2), its call in `main()`, Settings > Screen > PDA mode (compatible values, confirmation), `request_restart()` and exit 76, both units and the console helper, install tests, tiles hidden in code on portrait, docs (`PDA-PROFILE.md`, APP-CONTRACT section 3) | about 450 lines; 1 M tokens | medium: every install gets new unit text; the desktop variant's give-back path changes | unit tests, Settings renders (row, dialog), `test-install-board.sh`; the restart itself only on boards |
| **S7c** owner tests | tablet: Portrait and Landscape from Settings in the desktop variant (restart without the desktop flashing back, Quit to desktop after), touch and pen in both, writing at 29 mm, turned apps in portrait, repaint speed; Pi 3A+: Off / Portrait / Landscape on the DSI (Landscape: 26 mm boxes in half the screen) | 45 min | | boards |
| S7d (optional) | compat apps shown on portrait where the 2x window fits (800x1280); HyperPixel 4 panel size measured (no `APPLAUNCH_PANEL_MM` in its profile: the 11.3 px/mm default would size the band wrong); a second input device for an active pen if P0 finds one | small each; the pen medium | | renders / boards |

Order: P0 any time; S7a before S7b (S7b's compatibility check calls S7a's layout); S7c after both. Renders cover all
of S7a and the Settings pages of S7b; the restart, the units, rotation speed, touch / pen mapping and writing feel need
the boards.

## 7. Owner decisions

| # | Question | Options | Recommendation |
| --- | --- | --- | --- |
| D1 | Shape of the setting | A: one row PDA mode = Off / Portrait / Landscape in Settings > Screen. B: Orientation Auto / Portrait / Landscape + a PDA switch | **A** |
| D2 | How a change applies | A: restart the launcher after a confirmation (open apps close). B: live re-layout | **A** (B rebuilds every page and display: several times the work) |
| D3 | Landscape band layout | A: side: a 3x3 key block beside the boxes, one row, falling back to the strip over the boxes where it does not fit (720x720). B: centre: the strip over centred boxes. C: full-width stack (99 mm wide letters box on the tablet) | **A** (most room above the band, 9 mm keys) |
| D4 | Writing box height | A: about 29 mm wherever there is room (zone 31 mm), portrait capped at 32 mm, landscape at the keyboard's share. B: 25 mm in landscape. C: keep a third of the panel (48 mm on the tablet) | **A** (480 px panels unchanged) |
| D5 | Compatibility floor | A: boxes at least 20 mm and 240 px above the band. B: 24 mm (800x480 only with the side layout) | **A** |
| D6 | Portrait side up on a landscape panel | A: rotate 90 (as the Pi 3A+ PDA profile), 270 by hand in board.conf. B: a fourth value "Portrait, other side" | **A**, B when the tablet test asks for it |
| D7 | Fixed-pixel (320x170) apps on large portrait canvases | A: shown where the 2x window fits (800x1280). B: hidden on every portrait canvas (today) | **A**, in S7d |
| D8 | Side of the key block | A: left of the boxes. B: right. C: a mirror setting | **A** |
| D9 | `pi3a-wsdsi43-portrait` profile | A: keep it as the "Portrait by default" install choice. B: retire it in favour of the setting | **A** |
| D10 | The tablet test before code | A: P0 now (hand-edited board.conf, the pen probe). B: wait for S7a/S7b | **A** |

## 8. Renders and how to reproduce

All images in `043-pda-large-screens/` were made by the render harness (`projects/APPLaunch/tests/render`) on this
branch, synced to the WSL tree `~/zero7/launcher-s7`. The scenes and the study patch are in
`043-pda-large-screens/experiment/`: copy the `.scene` files into `tests/render/scenes/`, apply
`zone-layout-experiment.patch` (`git apply --ignore-whitespace`: the checkout's line endings may differ from the
patch's), then `bash run.sh render s7_lside_text ...`. The patch changes nothing unless `APPLAUNCH_S7_LANDSCAPE=1`,
`APPLAUNCH_S7_BAND` or `APPLAUNCH_S7_WRITE_MM_X10` is set (the scenes set them), so the portrait renders without them
are today's code. It is a study tool, not the S7a implementation (no tests, constants in place, env switches).

| Scene | Sizes | Shows |
| --- | --- | --- |
| `s7_por_home`, `_text`, `_term`, `_train`, `_switch` | 800x1280, 600x1024 (profile `pda`, panel mm 150x93 / 154x90) | portrait PDA on large panels, today's code |
| `s7_land_today` | 1280x800, 1024x600, 800x480, 720x720 | `APPLAUNCH_PDA=1` refused in landscape; the standard keyboard |
| `s7_lstack_*`, `s7_lcentre_*`, `s7_lside_*` | the four landscape sizes | the three landscape layouts (patch), text, ink, terminal, Keyb, training, home, switcher |
| `s7_pcap_stack`, `s7_pcap_centre` | 800x1280, 600x1024, 480x800, 480x640 | the 32 mm portrait cap; 480x800 and 480x640 unchanged |

Note: the harness's `pda` profile draws RGB565 (the Pi's depth); the tablet is XRGB8888. Layout is the same; S7a should
add a 32 bpp profile for the tablet's goldens.

## 9. Risks and open points

1. **The tablet's pen** may be a separate digitizer the launcher does not read (section 4.5): probe in P0.
2. **CPU rotation at 1280x800x32** in portrait is not measured (the Pi 3A+ figure is for 480x800x16).
3. **Unit change for the restart** reaches every install; a wrong `ExecStopPost=` path on the desktop variant would
   leave a tablet without its desktop after a restart: covered by install tests, then the S7c test.
4. **Apps inheriting `APPLAUNCH_PDA=1`** (section 5): must ship with S7a, before any app pin moves past it.
5. **480x640** keeps its 19 mm boxes (the cap of report 041); the owner found 19 mm too small on 480x800. Not changed by
   this study.
6. **The 1 MB image cache** was sized for the Pi boards; on the tablet's 28-tile pages it is enough for the 80 px icons
   seen in the renders; worth a check on the board, cheap to raise on amd64 only.
7. **Strokes accuracy** with a real stylus is still the open point of reports 041 / 042; a bigger panel does not change
   the recogniser (strokes are judged by shape, not size).
