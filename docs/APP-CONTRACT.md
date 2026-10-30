# App contract (version 1)

What an app started by CyberdeckZero Launcher (project and install folder `APPLaunch`) can learn about the screen, the window it gets and
the keyboards, so it does not have to hard-code 320x170 or 640x480. This page is the normative reference; the C
header `ext_components/cp0_lvgl/include/cp0_app_contract.h` says the same in short. Background and decisions:
`docs/dev/reports/023-p5-app-contract-plan.md`, task 014 P5.

## 1. Purpose and the three layers

The launcher runs on panels from 480x320 to 1280x720, rotated or not, at 16 or 32 bpp. An app needs to know which
mode it runs in, how many pixels it draws, how dense they are (a 7 mm button is 79 px on the deck at 2x and 44 px
on the Pi 3A+ with its SPI panel), and whether it should show its own on-screen keyboard.

| Layer | What | Promise |
|---|---|---|
| **Contract** | environment variables, `screen.state`, `input.state`, `status.state`, `board.state`, `launcher.sock`, `.desktop` keys | stable within contract 1 (section 8) |
| **SDK** | C files an app copies (section 9) | source, versioned with the launcher tree; no ABI promise |
| **Internal** | the virtual framebuffer file (VFB2) between the launcher and its `LD_PRELOAD` shim | may change in any release; apps never read it |

## 2. Who writes, who reads

| Item | Written by | When | Read by |
|---|---|---|---|
| per-launch environment | the launcher, on the app's command line only | each start of an app | that app |
| panel environment (`APPLAUNCH_SCREEN_*`) | the launcher, process-wide at display creation | once | every child process |
| `screen.state` | **only** the launcher (atomic rename) | at display creation, and again after **every** app exits (`gen + 1`) | any app |
| `input.state` | **only** the launcher (atomic rename) | keyboard list change, Settings > System > On-screen keyboard or Pointer navigation change | any app |
| `status.state` | **only** the launcher (atomic rename, from a thread of its own) | when the Wi-Fi, Ethernet, Bluetooth, battery, volume, mute, brightness or SSH indicator value changes (checked every 2 s; volume and brightness at least every 10 s, at once after a change by the launcher) | any app |
| `board.state` | **only** the launcher (atomic rename, from a thread of its own) | at start, again after the first sound-server probe, then when a fact changes (sound re-probed every 30 s) | any app |
| `launcher.sock` | the launcher serves it (a thread of its own, mode 0600) | while the launcher runs | any process of the same user: apps, scripts, ssh (section 14) |
| `.desktop` keys | the app's package | install | the launcher |

The state files live in one directory: `$APPLAUNCH_STATE_DIR`, else `$XDG_RUNTIME_DIR/applaunch/`, else the files
`/tmp/applaunch-screen.state`, `/tmp/applaunch-input.state` and `/tmp/applaunch-status.state` (`cp0_app_state_path()`
implements this order).

An app built on `cp0_lvgl` never writes `screen.state`: its display backend sees `APPLAUNCH_APP_MODE` and skips the
publication (an older app binary may still overwrite the file while it runs; the launcher rewrites it when the app
exits).

## 3. Environment

### Per launch (set by the launcher for the app only)

| Variable | compat (default) | fullscreen (`X-Fullscreen=true`) |
|---|---|---|
| `APPLAUNCH_CONTRACT` | `1` | `1` |
| `APPLAUNCH_APP_MODE` | `compat` | `fullscreen` |
| `APPLAUNCH_WINDOW=x,y,w,h` | where the scaled picture sits on the screen, e.g. deck `0,0,640,340`, Pi 3A+ SPI `80,25,320,170` | `0,0,W,H` |
| `APPLAUNCH_CANVAS=WxH` | `320x170` (the pixels the app draws) | the logical screen, e.g. `640x480` |
| `APPLAUNCH_CANVAS_SCALE` | screen pixels per canvas pixel: `2` or `1` | `1` |
| `APPLAUNCH_STATE_DIR` | the state directory (absent without `XDG_RUNTIME_DIR`) | same |
| `APPLAUNCH_PDA` | `0` | `1` for an app run upright on a launcher with the PDA writing zone (PDA mode on, report 044: `X-Portrait` that fits, see "Portrait launcher" below): its own display backend builds the band; `0` in every other case (turned, no writing zone), whatever the launcher runs with |
| `APPLAUNCH_LIFECYCLE=1`, `APPLAUNCH_CONTROL_FD=<n>` | never | only for an app with `X-Background` and only from a launcher with multitasking on: the lifecycle control socket (section 13) |

All coordinates are **logical**: the canvas as the launcher shows it (landscape on every board but the portrait PDA
profile, below), before the panel's rotation. Compat apps also get the launcher's
virtual-framebuffer setup (`APPLAUNCH_VFB`, `LD_PRELOAD`, `LV_LINUX_FBDEV_DEVICE=/dev/fb0`, `APPLAUNCH_FB=/dev/fb0`,
`APPLAUNCH_ROTATE=0`, the virtual keyboard in `LV_LINUX_KEYBOARD_DEVICE`): these are plumbing, not contract. A
terminal app (`Terminal=true`) runs inside the launcher's terminal page and gets no per-launch variables.

### Portrait launcher (the PDA profile)

A board profile may show the launcher in portrait (the PDA profile, report 038; `pi3a-wsdsi43-portrait`: the Pi 3A+
4.3" DSI panel turned to 480x800). Its logical canvas is then taller than wide and `APPLAUNCH_SCREEN_CLASS` is
`portrait`. No app built before this gets a portrait canvas, so contract 1 holds (keys and variables are only added):

| App | What it gets on a portrait launcher |
|---|---|
| fullscreen, **without** `X-Portrait` (every app today) | runs **turned**: full screen on the panel seen in landscape (480x800 -> `800x480`), the user turns the device. On its command line, after the variables above (window `0,0,800,480`, canvas `800x480`, scale `1`), the display variables of that canvas, which replace the inherited board profile's and the launcher's: `APPLAUNCH_ROTATE` and `APPLAUNCH_SCREEN_ROTATE` (a quarter turn back from the launcher's: `90` -> `0`, `270` -> `180`), `APPLAUNCH_LOGICAL=800x480`, `APPLAUNCH_SCREEN_W=800`, `APPLAUNCH_SCREEN_H=480`, `APPLAUNCH_SCREEN_CLASS` of that canvas (`wide`). A `cp0_lvgl` app's display backend and touch (`buffer` mapping) follow by themselves; nothing to change in the app. `X-MinSize` is checked against that landscape canvas |
| fullscreen **with** `X-Portrait=WxH` that fits the portrait canvas | the portrait canvas as it is (the variables above, e.g. canvas `480x800`), no turning; with the writing zone (PDA mode on) also `APPLAUNCH_PDA=1`: the app draws its own band (below) |
| fullscreen with an `X-Portrait` larger than the portrait canvas | turned, as without it |
| compat (320x170), a terminal app (`Terminal=true`) | as on every board: a terminal app runs in the launcher's terminal page; `X-MinSize` against the portrait canvas |
| compat (320x170), not a terminal | **not listed** (see below) |

A portrait screen shows only responsive fullscreen apps (turned, or native with `X-Portrait`) and terminal apps. A
compat app that is not a terminal is a fixed 320x170 page, too small at the top of a tall screen, so a portrait launcher
hides its tile (`desktop_fit()`: `Hidden`, whatever its `X-MinSize`); a landscape or square launcher is not affected.
The built-in fixed-pixel tiles (IP Panel, Snake, Tank, Store) are hidden by default through the board profile
(`APPLAUNCH_HIDE_TILES`), and Settings > Launcher can turn them on.

`screen.state` and the process-wide `APPLAUNCH_SCREEN_*` keep describing the panel as the launcher shows it (portrait,
`rot=90`); only a turned app's own command line says otherwise, so a turned app takes the environment first (as
`cp0_app_env_read()` does) and does not use `screen.state`'s `w`, `h` and `rot` for its canvas. A turned app hidden
behind the launcher's switcher (section 13) shows its dimmed frame sideways.

**The writing zone (report 041, phase S3; report 042).** The PDA profile (`APPLAUNCH_PDA=1` in its board profile) has a
band at the bottom of the portrait panel: a key strip (Keyb, Tab, Ctrl, Alt, the arrows, Enter) on top of the writing
zone where the user writes with a pen (the Strokes recogniser). With Settings > System > On-screen keyboard on
Handwriting + keyboard (offered on this profile only, and its default) it behaves like the fallback on-screen keyboard:
it shows while one of the launcher's own text fields (its terminal pages included) wants input, by the keyboard's own
rule (section 5), and is hidden otherwise; the strip's Keyb key puts the standard on-screen keyboard in its place (for
the following fields too) until that keyboard's close cross brings the band back. With Auto or On the band never shows
(the standard keyboard, as on every board); the launcher's pages get the whole panel and leave the band's rows free while it
shows, as for the keyboard. The launcher never draws its band over a fullscreen app: an app run turned owns the whole
panel in landscape and sees none of it. While the launcher is in front and the band shows, `input.state` says `vkb=zone`
(section 5): the zone is the text input and no on-screen keyboard is shown; otherwise (the standard keyboard chosen with
Keyb included) it says the usual rule. `screen.state` and the `APPLAUNCH_SCREEN_*` variables keep describing the whole
panel.

**The band in an app (report 044, phase S4).** An app run upright (`X-Portrait` that fits) on a launcher with the
writing zone gets `APPLAUNCH_PDA=1`; its own `cp0_lvgl` display backend then builds the same band at the bottom of its
portrait canvas (the tables and the user's trained glyphs are read from disk when the app starts). There is no opt-in key: every
such app follows the user's Settings > System > On-screen keyboard, which `input.state` says with `hand=` (section 5):
with `hand=1` (Handwriting + keyboard) the band shows where the app would show its own on-screen keyboard (a text field
wants input and `cp0_app_osk_allowed()` says yes) and leaves its rows free through the keyboard's inset; the strip's
Keyb puts the app's standard keyboard in its place until that keyboard's cross; its keys reach the app exactly as the
on-screen keyboard's (`cp0_keyboard_inject_key()`). With `hand=0` (Auto, On) the app shows its standard keyboard, as
before. The rule is `cp0_lvgl`'s, shared with the launcher (`cp0::ui::zone_host`, `include/cp0_zone_host.hpp`; the app
binds it with `hand` from `input.state`); an app built before it builds the band but never shows it. An app run turned,
a compat app and every app of a launcher without the writing zone get `APPLAUNCH_PDA=0`. The variables reserved for
this phase by earlier versions of this document (`APPLAUNCH_ZONE`, `screen.state` `orient=` and `zone=`) are not used:
the backend computes the band from the canvas (`cp0_zone_layout()`).

### Panel (process-wide; always the panel, never the app's canvas)

| Variable | Meaning |
|---|---|
| `APPLAUNCH_SCREEN_W`, `APPLAUNCH_SCREEN_H` | logical panel size as the launcher shows it (landscape, or portrait on the PDA profile), px |
| `APPLAUNCH_SCREEN_ROTATE` | clockwise pre-rotation of the logical picture into the framebuffer: `0`, `90`, `180`, `270` |
| `APPLAUNCH_SCREEN_PPMM` | density, px per mm x100 (`1130` = 11.3 px/mm) |
| `APPLAUNCH_SCREEN_CLASS` | `compact`, `standard`, `wide`, `square`, `large`, `portrait` (rules in `cp0_ui_metrics.h`) |
| `APPLAUNCH_SCREEN_FB` | the framebuffer device the launcher draws to (`""` = unknown) |
| `APPLAUNCH_SCREEN_TOUCH` | the touch device the launcher reads (`""` = none) |

**Density of the canvas = `APPLAUNCH_SCREEN_PPMM / APPLAUNCH_CANVAS_SCALE`.** A compat app on the deck draws at
5.65 px/mm (its 51-pixel square is 102 screen pixels, 9 mm).

The board's touch settings stay in the environment for a fullscreen app that reads the touch device itself:
`APPLAUNCH_TOUCH_ORIENT` (`buffer`: the device axes follow the framebuffer, map through `APPLAUNCH_SCREEN_ROTATE`;
`legacy`: use `APPLAUNCH_TOUCH_SWAP_XY`, `APPLAUNCH_TOUCH_INVERT_X`, `APPLAUNCH_TOUCH_INVERT_Y`), `APPLAUNCH_TOUCH_DEV`
(the node or `auto`). `cp0_fbo_touch_to_logical()` (SDK) does the `buffer` mapping. These come from the board's
`board.conf` and are documented here, but their values are the board's, not computed by the launcher.

## 4. `screen.state`

One `key=value` per line, LF, ASCII. Example (the deck; device paths vary by board):

```
v=1
w=640
h=480
rot=0
ppmm=1130
density=default
class=standard
compat=0,0,640,340
scale=2
toolbar=140
fb=/dev/fb0
bpp=32
touch=/dev/input/event0
space=8,16,20
radius=8,18,28
header=56
row=79
target=102
dialog_max_w=560
text=32,28,24,18
kb_inset=0
gen=33
```

| Key | Meaning |
|---|---|
| `v` | file version, `1`. A reader treats any other value as "no file". |
| `w`, `h`, `rot`, `ppmm` | the panel, as the `APPLAUNCH_SCREEN_*` variables |
| `density` | where `ppmm` came from: `panel_mm` (board.conf `APPLAUNCH_PANEL_MM`), `fbdev` (the driver's mm), `default` (11.3 px/mm assumed) |
| `class` | the screen class |
| `compat=x,y,w,h`, `scale` | the compat window and its integer scale |
| `toolbar` | the compat toolbar's height (full width, at the bottom) |
| `fb`, `bpp`, `touch` | the panel's framebuffer device, its depth in bits, the touch device (`""` / `0` = unknown) |
| `touch_slots` | (multitasking, section 13) how many contacts the touch screen tracks: `0` no touch screen, `1` single touch (or a panel without slots), `N` multi-touch slots (the deck 5, the Pi 3A+ DSI 10); written after `touch=` by a launcher with multitasking on only (section 13); absent = unknown |
| `space=s,m,l`, `radius=s,m,l`, `header`, `row`, `target`, `dialog_max_w` | the launcher's design tokens in **screen** px (row 7 mm, target 7 mm; the compact list row, `CP0_TOK_LIST_ROW_H`, is 5 mm and is not in `screen.state`; the toolbar, the native page header's Esc (`cp0_ui_toolbar_key_px`) and the on-screen keyboard keep their own 9 mm sizes) |
| `text=title,heading,body,caption` | text sizes in px |
| `kb_inset` | reserved, always `0` in contract 1 |
| `gen` | increases on every write; re-read when it changes |

The tokens are what the launcher itself uses on that panel, so an app that takes them looks like the launcher. When
the file is missing, `cp0_app_screen_compute()` gives the same numbers from a size and a density.

## 5. `input.state`

```
v=1
keyboards=1
last_seen=1791523200
bt_keyboards=1
kb_gone=0
bt_gone=0
vkb=auto
pointer=off
hand=0
gen=4
```

| Key | Meaning |
|---|---|
| `keyboards` | real keyboards present now. A device with volume, mute or brightness keys but no letters (a tablet's buttons, a headset) never counts (section 14) |
| `bt_keyboards` | of them, Bluetooth |
| `last_seen` | unix seconds of the last time `keyboards > 0` (0 = never since boot) |
| `kb_gone`, `bt_gone` | **monotonic** seconds (`CLOCK_MONOTONIC`, same in every process of the boot) when the keyboard / Bluetooth keyboard count last dropped to 0; `0` = never |
| `vkb` | the on-screen keyboard rule in force: `auto` (Settings > System > On-screen keyboard Auto, and Handwriting + keyboard while its band is not shown), `off` (`APPLAUNCH_OSK=off`, or a desktop session owns the screen; the setting has no Off since report 042, a stored Off reads as Auto), `force` (Settings > System > On-screen keyboard On: shown even with a physical keyboard; `APPLAUNCH_OSK=force`, development and the HackBerry), `zone` (the PDA profile's writing zone is shown as the text input while the launcher is in front: no on-screen keyboard; report 041, 042) |
| `pointer` | Settings > System > Pointer navigation: `on` or `off` (the default). See section 10b |
| `hand` | `1` while Settings > System > On-screen keyboard is Handwriting + keyboard on a launcher with the PDA writing zone, whoever is in front; else `0` (report 044). An app run with `APPLAUNCH_PDA=1` shows its own band instead of its keyboard while it is `1` (section 3, "The band in an app"); `vkb` stays `auto` then, so an app without the band shows its keyboard |
| `gen` | increases on every write |

A file without `vkb` (an older launcher) means `auto`; a file without `pointer` means `off`; a file without `hand` means
`0`. A reader that does not know `zone` (an SDK copy from before report 041) keeps `auto` for it; a fullscreen app never
sees `zone` while it is in front.

## 5b. `status.state`

The values the launcher's own status strip shows, for the strip of an app (report 032). Added within contract 1 (keys
and files are only added, section 8): `v` stays 1 and `APPLAUNCH_CONTRACT` stays 1. An older launcher does not write it.

```
v=1
gen=12
pid=801
wifi=1
wifi_pct=72
eth=0
bt=connected
battery=64
battery_state=Discharging
volume=40
mute=0
brightness_pct=70
ssh=1
ssh_act=0
```

| Key | Meaning |
|---|---|
| `pid` | the launcher process that wrote it. The file is trusted only while `kill(pid, 0)` succeeds (`EPERM` counts as alive): a launcher that crashed or was stopped leaves a file nobody should believe |
| `wifi` | `1` = connected |
| `wifi_pct` | signal, 0..100 (the bars' rule is the launcher's: connected shows at least one bar) |
| `eth` | `1` = a wired link is connected |
| `bt` | `off` (adapter not powered in BlueZ, or no Bluetooth service), `on` (powered), `connected` (powered and a device connected): exactly what the launcher's Bluetooth glyph shows. sysfs cannot tell "powered" |
| `battery` | charge 0..100, `-1` = no battery |
| `battery_state` | `Charging`, `Discharging`, `Full`, `Not charging` or `Unknown` (sysfs spelling). Only `Charging` colours the strip |
| `volume` | the launcher's output volume 0..100, `-1` = the board has no sound output (report 035; additive, older launchers do not write it: the reader gives `-1` too) |
| `mute` | `1` = muted |
| `brightness_pct` | the backlight as a percent of its maximum, `-1` = no backlight to drive. Snapped to the launcher's 10 steps; while an app is dimmed by section 14 `dim allow` it shows the dimmed level |
| `gen` | increases on every write (a restarted launcher continues from the old file) |
| `ssh` | incoming SSH connections to the board's sshd (the SSH indicator, 2026-10-30): established TCP connections whose local port is one sshd listens on, each seen in two consecutive samples (so a port scan never shows); `ssh localhost` and connections still at the password prompt count. `0` = none, Settings > System > SSH indicator is Off (the launcher then does not look), or an older launcher (the reader gives `0` too). A count only: peer addresses never appear in this file |
| `ssh_act` | `1` = bytes moved on one of them since the launcher's previous sample, 2 s earlier (typing, output, a transfer, an SSH keepalive); `0` otherwise and whenever `ssh` is `0` |

`ssh` and `ssh_act` drive the strip's SSH indicator: the terminal glyph (U+F120) in the strip's leftmost slot while `ssh`
is above 0, the Bluetooth "connected" blue, alternating with white on the strip's 1 s clock tick while `ssh_act` is 1;
on a board with a battery the percentage is left out while it shows (the level glyph stays).
`cp0::ui::StatusStrip` with the default source draws it by itself (an app built against a toolkit from before it shows
nothing until rebuilt). The slot is always reserved in `cp0::ui::status_strip_left()`, so a page header's controls never
move when a session starts or ends. A non-LVGL app that draws its own bar sets `ssh`, `ssh_active` and, for the blink,
`ssh_bright` in `cp0_statusbar_state_t` (`cp0_ui_status_set_ssh()` and `cp0_ui_status_ssh_tick()` in
`cp0_ui_status_strip.h` are the rules); a zeroed state draws nothing.

`volume`, `mute` and `brightness_pct` are for an app that draws its own level bar: the launcher draws nothing over a fullscreen app (section 10), so the user who presses a volume key in your app only hears the change unless the app shows it. Poll the file with the rest of the status; do not call `launcher.sock` for it.

The launcher rewrites the file only when a shown value changes. Read it with `cp0_app_status_read(NULL, &status)`
(SDK, section 9): it returns `0` for a good feed, and `-1` when the file is missing, is not `v=1`, has no `pid` or its
`pid` is gone. On `-1` the app must fall back to its own sources: for the strip, `cp0_statusbar_read_state()` (sysfs and
procfs; it finds the Wi-Fi interface under any name: `wlan0`, `wlp2s0`, `wlx...`). The strip of an app built on
`cp0_lvgl` (`cp0::ui::StatusStrip` without a source) does this by itself, and takes the battery from the feed when the
feed has one. A non-LVGL app that draws its own bar reads the feed the same way.

## 5c. `board.state`

The facts of the board that the launcher already works out for its own Settings (report 035). Added within contract 1: `v` stays 1. An older launcher does not write it.

```
v=1
gen=2
pid=801
launcher=0.8.6
board=hackberry-cm5
arch=aarch64
ram_mb=1800
battery=1
battery_source=gauge:max17048
backlight=levels
backlight_steps=10
sound=1
sound_kind=bluetooth
sound_guess=1
requests=hello,caps,toast,beep,volume,brightness,dim
```

| Key | Meaning |
|---|---|
| `pid`, `gen` | as `status.state` (the file is trusted only while the launcher process is alive) |
| `launcher` | the launcher version |
| `board` | `APPLAUNCH_BOARD` of the board profile. Absent on the deck and on a board without a profile |
| `arch`, `ram_mb` | `uname -m` and MemTotal in MB: the app picks its own memory class |
| `battery` | `0` none, `1` present, `2` not set up (the profile says there should be one: `battery_note`) |
| `battery_source` | `power_supply:<name>` (sysfs) or `gauge:max17048` (a fuel gauge read over i2c) |
| `backlight` | `none`, `onoff` (a gpio backlight: on or off only) or `levels` |
| `backlight_steps` | the steps the launcher offers: `10`, `2` for on / off, `0` for none |
| `sound` | `0` no sound output, `1` present, `2` not set up (the profile says how to set it up: `sound_note`) |
| `sound_kind` | `alsa` (a local card with a playback volume), `server` (a PulseAudio / PipeWire sink), `bluetooth` |
| `sound_guess` | `1` when `sound_kind=bluetooth` is a **guess** from the sink's name (`bluez_output.*`); the launcher cannot ask the sink what it is |
| `requests` | the verbs `launcher.sock` answers **on this board** (section 14): `volume` only with sound, `brightness` only with a backlight, `dim` only on a battery board with brightness levels. `beep` is always listed: without sound it answers `err nosound` |
| `battery_note`, `backlight_note`, `sound_note` | the board profile's hint text (`use the button on the case`, `pair XWF-M18 in Bluetooth`), for display only |

**A fact the launcher does not know is an absent key, never `0`.** Right after the launcher starts, `sound` is absent on a board without a local sound card until the first sound-server probe has run (a moment later; the file is rewritten). The sound output can change while the launcher runs (a Bluetooth speaker connects): it is looked at again every 30 s. The physical buttons of a board (which key a button sends) are **not** published: the launcher has no source for them.

Read it with `cp0_app_board_read(NULL, &board)` (SDK, section 9): `0` for a good file, `-1` when it is missing, not `v=1`, has no `pid` or its `pid` is gone. On `-1` assume nothing and hide what depends on it (an older launcher: no sound option, no beeps). `cp0_app_board_has_request(&board, "beep")` looks in `requests`.

## 6. `.desktop` keys

| Key | Meaning |
|---|---|
| `X-Fullscreen=true` | fullscreen mode: the launcher stops drawing, the app owns the whole panel until it exits (the hold-Esc watchdog still applies). Without it the app is a compat app: 320x170 in the scaled window, toolbar below. |
| `X-Background=run\|freeze` | (section 13) the app may be hidden and resumed instead of owning the panel until it exits; `freeze` = stopped while hidden, `run` = keeps working while hidden. Absent or another value: today's behaviour. Only with `X-Fullscreen=true`, only under a launcher with multitasking on; an older launcher ignores it. Declare it only in a build that answers the protocol (section 13, "The helper"). |
| `X-Permissions=...` | **reserved**, not read by any launcher yet (report 035, D3). A later launcher may let an app declare the launcher requests it uses (`beep`, `brightness`, ...) and refuse the rest with `err denied`. Today every request of section 14 is open to every process of the user, rate-limited and logged; do not rely on it being absent. |
| `X-MinSize=WxH` | the logical screen size the app needs (`x` or `X`, each 1..65535). On a smaller screen (either dimension) the tile is **hidden**, like `Hidden=true`: the app is not registered, so it does **not appear in Settings > Apps either**. Malformed = no restriction. A launcher without layout metrics (the SDL simulator) shows it. It is the **landscape** minimum: on a portrait launcher (section 3, "Portrait launcher") a fullscreen app is checked against the panel seen in landscape, the canvas it runs turned on. |
| `X-Portrait=WxH` | (added with the PDA profile, report 038; same syntax as `X-MinSize`) the smallest canvas the app supports on a portrait launcher. A fullscreen app that declares it and fits gets the portrait canvas there; absent, malformed or larger than the portrait canvas: the app is landscape only and runs turned. Ignored on a landscape launcher and by an older launcher. Declare it only in a build whose layout passes on the portrait canvases (480x640, 480x800). |

**An older launcher ignores `X-MinSize`** (and every contract variable), so an app must still start on a screen that
is too small and say so ("needs a 640x480 screen") instead of drawing off the edge.

## 7. Failure modes and degrade rules

Rule: **degrade to the app's own probe, never crash.**

| Situation | What the app sees | What it does |
|---|---|---|
| started without the launcher (ssh, a script) or by an older launcher | no `APPLAUNCH_CONTRACT` (`cp0_app_env_read()` returns -1); maybe `screen.state` | use `screen.state` if present (`derived.source=state` in the probe), else the framebuffer's `FBIOGET_VSCREENINFO` size and 11.3 px/mm; tokens from `cp0_app_screen_compute()` |
| no state file, or `v` other than 1 | the readers return -1 | the same fallback |
| a key missing (older launcher) | the field is 0 / `""` | use its own default for that value |
| unknown keys or variables | - | ignore them |
| a torn file | impossible: the launcher writes a temporary file and renames it | - |
| the launcher crashed | stale files | acceptable: the screen does not change at runtime |
| a change while the app runs | `gen` changes | inotify the directory (`IN_MOVED_TO`) or stat at most once a second, re-read on a new `gen` |
| an app run as root (`keep_root`) | the same environment | - |

## 8. Stability promise

- Within contract 1 the names, units and meanings of every variable and key above never change.
- Keys and variables are only **added**. Readers must ignore what they do not know.
- A breaking change raises `v=` and `APPLAUNCH_CONTRACT` together; the launcher then keeps writing the old version's
  keys for at least one release.
- The SDK files (section 9) are source: they follow the launcher tree and may change shape; copy the version you
  build against. `sdk_version.txt` is not tied to the contract version.
- The virtual framebuffer (VFB2: a 64 KiB header, then the pixels) is **internal** between the launcher and its
  shim, which update together. An app only ever uses the ordinary fbdev calls (`open`, `FBIOGET_VSCREENINFO`,
  `FBIOGET_FSCREENINFO`, `FBIOPAN_DISPLAY`, `mmap`) on `/dev/fb0`; in compat mode they report a 320x170, 32 bpp
  framebuffer with physical size -1 mm (the density is in the environment, not in the fbdev ioctls).

## 9. SDK files (copy-ready)

All in `ext_components/cp0_lvgl`, MIT, plain C99 + POSIX, **no LVGL** and no cp0 runtime:

| Files | What | Needs |
|---|---|---|
| `include/cp0_app_contract.h`, `src/cp0_app_contract.c` | `cp0_app_env_read()`, `cp0_app_state_path()`, `cp0_app_screen_read()` / `_parse()` / `_compute()`, `cp0_app_input_read()` / `_parse()` (with `pointer_nav` and `hand`), `cp0_app_status_read()` / `_load()` / `_parse()` (section 5b; `cp0_app_status_read()` needs POSIX `kill`), `cp0_app_board_read()` / `_load()` / `_parse()` / `_has_request()` (section 5c), `cp0_app_osk_allowed()` / `cp0_app_osk_rule()`, `cp0_app_monotonic_s()` | `cp0_ui_metrics.{h,c}` |
| `include/cp0_app_client.h`, `src/cp0_app_client.c` | the app side of `launcher.sock` (section 14): `cp0_app_client_call()`, `cp0_app_beep()`, `cp0_app_toast()`. Never blocks longer than the timeout (default 200 ms), no service = `-1` | libc + POSIX sockets |
| `include/cp0_ui_metrics.h`, `src/cp0_ui_metrics.c` | the layout service: class rules, density, tokens, text sizes, compat window (`cp0_ui_metrics_compute()`) | libc only |
| `src/cp0/cp0_fb_output.h`, `src/cp0/cp0_fb_output.c` | drawing into a mapped framebuffer with the panel's rotation and 16/32 bpp; touch mapping (`cp0_fbo_setup()`, `cp0_fbo_blit*()`, `cp0_fbo_pack()`, `cp0_fbo_touch_to_logical()`) | libc only |
| `include/cp0_app_lifecycle.h`, `src/cp0_app_lifecycle.c` | the app side of the lifecycle protocol (section 13): `cp0_app_lifecycle_init()` from the environment, `_service()` (non-blocking, answers `ack <n>`), `_send()` (`home`, `switcher`), the key rule `_key_drop()` | libc + POSIX sockets |

Build: compile the `.c` files with your app, `-I` the folders of the headers. Example of a complete user:
`tools/dev/app-contract-probe/build.sh`.

**Not copy-in: the LVGL widget set.** An LVGL app links `cp0_lvgl` (statically, from a pinned launcher commit) and gets
the `cp0::ui` widgets (`include/cp0_ui_widgets.hpp`: Page, Roller, CompactList, InfoTable, TextBlock, TextField, Dialog,
the on-screen keyboard; Roller or CompactList: `docs/cp0_lvgl.en.md`, "Lists") and the launcher's **status strip and title**: `cp0::ui::StatusStrip::add(header)` (clock pill, Wi-Fi bars,
Bluetooth glyph, laid out from the tokens at every size, updated by its own timers, freed with its parent),
`cp0::ui::status_strip_left()` and `cp0::ui::add_title()`. They are part of the component, not files to copy: an app
never keeps its own copy of the strip, the widgets or `cp0_statusbar.[ch]`, and never draws its own clock or icons
(API: `docs/cp0_lvgl.en.md`, "Status strip and title"). Only non-LVGL apps (a framebuffer shim, a headless renderer)
may still copy `cp0_statusbar.[ch]`; `docs/dev/deck/check_statusbar_drift.py` checks those copies.

Minimal reader:

```c
#include "cp0_app_contract.h"
#include <stdio.h>

int main(void)
{
    cp0_app_env_t env;
    cp0_app_screen_t scr;
    cp0_app_input_t in;
    const int contract = cp0_app_env_read(&env) == 0;   /* started by a contract-1 launcher */
    const int have_file = cp0_app_screen_read(NULL, &scr) == 0;

    int canvas_w = 320, canvas_h = 170, scale = 1, ppmm = CP0_UI_DEFAULT_PPMM_X100;
    if (contract) {
        canvas_w = env.canvas_w, canvas_h = env.canvas_h, scale = env.canvas_scale;
        if (env.screen_ppmm_x100 > 0) ppmm = env.screen_ppmm_x100;
    } else if (have_file) {                               /* no contract: the screen as the launcher saw it */
        canvas_w = scr.w, canvas_h = scr.h, ppmm = scr.ppmm_x100;
    }                                                      /* else: FBIOGET_VSCREENINFO of your framebuffer */
    const int canvas_ppmm = (ppmm + scale / 2) / scale;
    const int button_px = cp0_ui_mm_px(canvas_ppmm, CP0_UI_TARGET_MM_X10, 1); /* a 7 mm button, in canvas pixels */

    const int osk = cp0_app_input_read(NULL, &in) == 0 && cp0_app_osk_allowed(&in, cp0_app_monotonic_s());
    printf("%s %dx%d, 7 mm = %d px, own keyboard %s\n", contract ? cp0_app_mode_name(env.mode) : "no contract",
           canvas_w, canvas_h, button_px, osk ? "allowed" : "not now");
    return 0;
}
```

## 10. On-screen keyboard in a fullscreen app

The launcher never draws over a fullscreen app (it has stopped drawing). An app with text fields draws **its own**
keyboard, and only when the shared rule says so: `cp0_app_osk_allowed(&input, cp0_app_monotonic_s())` returns 1 when
`vkb=force`, or `vkb=auto` with no physical keyboard, not within 600 s of a Bluetooth keyboard going away (it is
probably asleep) and not within 2 s of any keyboard going away. `vkb=off` and `vkb=zone` never. The launcher's own keyboard uses
exactly this function, so the app and the launcher agree. Re-read `input.state` when its `gen` changes (a keyboard
connects, the user switches the setting). The keyboard must never be the only way in: the policy is "physical
keyboard first, fallback keyboard only when none is connected, never summoned by hand". An app started with
`APPLAUNCH_PDA=1` shows the writing zone's band instead of its keyboard by the same rule while `hand=1` (section 3, "The
band in an app").

## 10b. Pointer navigation

With `pointer=on`, a mouse, trackball, trackpoint or the HackBerry's optical pad drives the keyboard UI: motion is
turned into arrow keys at a rate that follows the push (one axis at a time, a dead zone for rest noise), the wheel into
Up / Down, a click (button released within 0.6 s, without moving) into Enter, and a held click (0.6 s without moving)
into **Ctrl+O, the options key** (`KEY_O` with `KBD_MOD_CTRL`; Mesh Hop's chat options use it). A pointer is a device
with `REL_X`, `REL_Y` and `BTN_LEFT` and no absolute axis or `BTN_TOUCH` (never a touch screen or a touchpad). The keys
arrive like a physical keyboard's (press + release, `keysym` 0), so an app needs no code for them; an app that has an
options menu should open it on Ctrl+O. On the HackBerry's pad a quick tap on the surface (which its firmware reports as
a short click, 0.16-0.19 s) does not select: only the pad's physical button clicks (a press of 0.26 s or more; a
per-device default, `APPLAUNCH_POINTER_MIN_CLICK_MS` changes it for every pointer, `0` turns it off). Other mice click
as before. While a text field edits (`KBD_INPUT_CONTEXT_TEXT`) Left / Right and Ctrl+O are
not sent (the caret, a terminal's control character); Up / Down and Enter are.

An app that links `cp0_lvgl` and reads every keyboard (`cp0_keyboard_set_read_all_keyboards(1)`, as
`apps/_common/platform.cpp` does) gets this from its own keyboard backend, which follows `pointer=` (re-read when the file
changes, at most once a second). It needs a `cp0_lvgl` that has it: rebuild against a launcher commit with
`cp0_pointer_nav.h`. The launcher stops its own pointer reading while an app runs. `APPLAUNCH_POINTER_NAV=1|0` in the
environment forces it on or off for a test; the tuning variables are in `ext_components/cp0_lvgl/include/cp0_pointer_nav.h`.

## 11. Wrapped (compat) apps in v1

A compat app learns everything above: its mode, the window, the scale, the class, the density, the keyboard rule.
It still **draws 320x170**: the virtual framebuffer is fixed at that size and the launcher scales it into the window.
So it can choose sizes in millimetres (`canvas density = SCREEN_PPMM / CANVAS_SCALE`) and adapt its layout to the
class, but it cannot use more pixels. A canvas that fills the window (`X-Canvas=fill`) and a launcher-drawn keyboard
over a compat app are future work (task 014 P5-4, F1 phase 2), not part of contract 1. An app that needs the whole
panel today uses `X-Fullscreen=true` and draws through `cp0_fb_output`.

## 12. Testing with the probe

`tools/dev/app-contract-probe` (see its README) is a small app that prints every fact above (`--report`, one
`key=value` per line) or draws a test pattern: the canvas border, a 1 mm grid and a 9 mm square, in compat or
fullscreen mode. Three `.desktop` variants (compat, fullscreen, fullscreen with `X-MinSize=640x480`) install by hand
into `/usr/share/APPLaunch/applications/`.

On the PC, `projects/APPLaunch/tests/render/run.sh contract` (a step of `tools/dev/check-all.sh`) runs the host probe
against the render harness at six screen sizes with the environment the launcher's own builder produces, and checks
every fact against the layout service. The board tests are in `docs/dev/tests/launcher-0.5.0-keyboard-tests.md`,
section L.

## 13. Lifecycle: hidden and resumed (multitasking)

**Status: contract 1, additive** (no `v=` change, section 8; final since multitasking phase P-e; **shipped in launcher
v0.8.0**). The launcher side is
task 015 (`docs/dev/tasks/015-multitasking-pb.md`, `-pc.md`, `-pd.md`). Multitasking is **on by default** on logical
canvases whose short side is 480 or more (640x480, the size the switcher was designed for, and up; since the PDA
profile also the portrait 480x640 and 480x800) and off below them (480x320, the 320x480 SPI panel turned to 480x320,
the 320x170 board: the launcher behaves as before multitasking). `APPLAUNCH_MULTITASK=0`
(or `false`, `off`, `no`) in the launcher's environment turns it off, `=1` (`true`, `on`, `yes`) forces it on, also
on a smaller canvas; with it off nothing of this section happens. The app side is the toolkit's helper (below, phase
P-e), which every native app of the apps repository gets through `apps/_common`. Design and owner decisions:
`docs/dev/reports/026-multitasking-study.md`. An app that declares nothing behaves exactly as before, under any
launcher; so does an app that declares `X-Background` when the launcher gives it no control socket (an older
launcher, multitasking off, started by hand).

### Declaring it

| `.desktop` (with `X-Fullscreen=true`) | Hidden, the app is ... | For |
|---|---|---|
| `X-Background=freeze` | **stopped** (`SIGSTOP` of its process group after it answered `hide`): no CPU, memory kept | most apps: the cheap default |
| `X-Background=run` | **running**, without drawing and ignoring input | work that must go on: logging a serial port, decoding radio, sampling |
| absent or anything else | never hidden: it owns the panel until it exits (today) | wrapped (compat) apps, old builds |

One instance per app: tapping the tile of a hidden app resumes it. At most 4 apps are resident (the app in front
included); a 5th launch closes the least recently used *frozen* app. While an app is hidden the launcher reads
MemAvailable every 2 s: under 64 MB it closes the least recently used frozen app, under 32 MB a running one too.
**The launcher may close a hidden app at any time**: save what matters on `close` (and better, as you go).

### What the app receives

`APPLAUNCH_LIFECYCLE=1` and `APPLAUNCH_CONTROL_FD=<n>`: one end of a `SOCK_SEQPACKET` Unix socket, inherited
(close-on-exec cleared). Set `FD_CLOEXEC` on it yourself before starting helpers. Without these variables (the key
is absent, the launcher is older, or multitasking is off) the app keeps today's behaviour; the key alone means
nothing.

### Messages

ASCII datagrams of at most 64 bytes, one message each; a trailing newline is tolerated; unknown messages are ignored
by both sides. `<n>` is a decimal number that the launcher increases with every message to that app.

| Direction | Message | Meaning |
|---|---|---|
| launcher -> app | `hide <n>` | finish the current frame, **stop writing the framebuffer**, cancel a press in progress (LVGL: `lv_indev_reset`), ignore input, then answer `ack <n>`, **within 300 ms**. A `freeze` app is stopped right after the answer. |
| launcher -> app | `show <n>` | the launcher has stopped drawing, put the app's last frame back and emptied its input queues; see below, then answer `ack <n>` |
| launcher -> app | `close <n>` | save and quit. `SIGTERM` follows 3 s later, `SIGKILL` 3 s after that |
| app -> launcher | `ack <n>` | `hide` or `show` done |
| app -> launcher | `home` | leave to the launcher (an app's own "minimise" control). Esc at the app's root still **quits** (decision D5) |
| app -> launcher | `switcher` | open the switcher: the app is hidden as for `home`, then the launcher shows its switcher screen (on screens whose short side is under 480: the same as `home`) |

### Obligations

- **Answer `hide` in time.** No `ack <n>` within 300 ms: the launcher stops the whole process group with `SIGSTOP`
  and shows the app as *not responding*. It can still be resumed (its last frame is restored, its input emptied, the
  group continued) or closed. A `run` app that cannot stop drawing must not declare `run`.
- **No framebuffer write while hidden.** The launcher draws the home grid or the switcher over your last frame.
- **Alt+Tab and Super are the launcher's.** You see Alt go down and the Tab with it: do not act on Tab with Alt held
  (the helper drops it), and do not give Super or three fingers held still a meaning of your own. Super tapped alone
  hides the app (home) at once; from launcher 0.8.7 a **second tap within 0.45 s** (press to press), or Super **held
  0.6 s** with no other key pressed meanwhile, opens the switcher (below, "Gestures"). Super+another key is none of
  these: a chord the launcher leaves alone.
- **On `show`:** read and drop whatever input is still pending (until `EAGAIN`), **treat every key as released and
  reset the modifiers** (re-read `EVIOCGKEY` if you track key state: the Alt+Tab that hid you showed you Alt going down
  but never up), reset the pointer state, repaint the whole screen, resume input, answer `ack <n>`.
- **Frozen means stopped.** `SIGSTOP` cannot be caught; `SIGCONT` can. Timers jump, network peers may drop you (in
  the P-a measurements viz1090's link to readsb dropped after about 15 s frozen): reconnect after `show`.
- **Exclusive hardware** (a serial port, an RTL-SDR): declare `run` and keep using it, or release it on `hide` and
  open it again on `show`; a frozen app still holds what it opened.
- **Background cost.** A hidden `run` app does its own work only: no rendering, low-rate timers. Its
  `oom_score_adj` is raised to 700 while hidden (900 when frozen) and set back to its start value on `show`, so under
  memory pressure the kernel kills hidden apps first.
- The launcher also protects you from stale input on its own (it grabs the touch screen and the keyboards while it
  is in front with apps hidden, and empties your evdev queues before every `show`), but an app must not rely on it:
  another launcher, or a board with a stricter `ptrace_scope`, may not.
- **Save state on exit, and better as you go.** `close`, the memory rule, `SIGTERM` (Esc held 3 s) and the kernel's
  OOM killer can all end a hidden app; only the first two give it time.

### The helper (SDK, phase P-e)

`ext_components/cp0_lvgl` has the app side, in two layers:

| Files | For | What it does |
|---|---|---|
| `include/cp0_app_lifecycle.h`, `src/cp0_app_lifecycle.c` | any app (copy-ready, section 9: plain C, no LVGL) | reads the environment (the variable pair, an open socket), makes the socket non-blocking and close-on-exec; `cp0_app_lifecycle_service()` reads every waiting message without blocking, calls the app's `hide` / `show` / `close` function, then answers `ack <n>`; a `hide` while hidden or a `show` while visible is only answered; the launcher's end going away ends the lifecycle (and closes a hidden app: nobody can show it again); `cp0_app_lifecycle_send()` for `home` / `switcher`; the key rule below |
| `include/cp0_app_lifecycle_lvgl.h`, `src/cp0_app_lifecycle_lvgl.c`, `src/cp0/cp0_app_lifecycle_device.cpp` | an app on `cp0_lvgl` | `cp0_app_lifecycle_device_start(on_close)` after the display exists: the service runs from `cp0_lvgl_run()`'s loop hook (no thread, no timer of its own: the loop turns at least every indev read period, 33 ms, which bounds the answer). **hide**: `cp0_display_suspend()` (no framebuffer write from its return; the touch screen reads released), invalidation off on every display (nothing is rendered while hidden; widgets may still change), `lv_indev_reset()` (the press in progress ends without a click), the keyboards let go (`keyboard_pause()`: libinput releases the keys still held, so Alt comes back up), the key queue emptied. **show**: the keyboards opened again, the touch screen's queue dropped and its contacts read back (`cp0_display_touch_resync()`: a finger still down presses nothing until lifted), the key queue emptied, the indevs reset, invalidation on again, every screen and layer invalidated, `cp0_display_resume()`: a full repaint. **close**: `on_close()`, the app's normal exit. **G2** in the app: one finger held 500 ms on its status strip sends `switcher`. A key filter drops every key while hidden, Tab with Alt held (the launcher's Alt+Tab must not also move the app's focus), and after a `show` the release or repeat of a key not pressed since (the Alt of the Alt+Tab that hid the app). |

The native apps of the apps repository start it in `apps/_common/platform.cpp` and quit on `close` exactly as on Esc at
their root (`cdk::request_quit`); `cdk::hidden()` tells a `run` app to skip its own rendering work. An app opts in with
the `.desktop` key alone once it is built against a `cp0_lvgl` with the helper (and the apps' guidelines say what it
must not do while hidden). Esc at an app's root still quits (D5); nothing in the helper adds a "minimise" control, but
`cp0_app_lifecycle_lvgl_send("home")` is there for an app that wants one.

### Gestures (multitasking phase P-c)

- **Three fingers held still for 1 s anywhere** (G1) and **a long-press on the status strip** (G2) open the
  switcher (phase P-d): the app in front is hidden (`hide`), as Super does. An app without `X-Background` is frozen
  instead (`SIGSTOP` of its group) behind a "Close <app>?" sheet: Keep continues it as a `show` would (last frame put
  back, input queues emptied first; no message, it has no socket), Close ends it as Esc held 3 s does (after a
  `SIGCONT`). On screens whose short side is under 480 the gestures hide a lifecycle app and leave others alone. While an app
  is in front the launcher reads the touch screen **passively** (a second file descriptor, never `EVIOCGRAB`): the app
  still gets every touch, including the three fingers. When G1 fires the fingers are still down: `hide` arrives in
  the middle of a press; cancel it (no click) as the `hide` obligation says, and ignore the touches until `show`.
- **Super double tap** (launcher 0.8.7 and later): two taps of Super (a tablet's Windows button, or the Super key of
  any keyboard) whose presses are at most 0.45 s apart open the switcher. The first tap acts at once as a single tap
  (the app is hidden, home); the second opens the switcher with that app first, marked *In front* (Esc goes back to
  it). A third tap within 0.45 s starts nothing; a double tap while the switcher is up leaves it as Esc does. The
  FZ-M1's Windows button reports press and release at the same instant, so on that tablet the double tap is the way.
- **Super held 0.6 s** (a keyboard that reports how long a key is held) does what G1 does too: the app is hidden and
  the switcher opens, an app without `X-Background` gets the sheet. Any other key pressed while Super is down cancels
  the hold and the tap, and that key is not taken from the app (Super+L; the FZ-M1's rotation-lock button, which sends
  Super+F14). The launcher reads Super passively, as every key: the app sees it.
- Do not give three or more fingers a meaning of your own (a four-finger contact cancels G1, but three fingers held
  still are the launcher's). One- and two-finger gestures (pinch, pan, two-thumb typing) stay the app's.
- G2 in an app is the app's own: a long-press on its status strip sends `switcher` (an app on `cp0_lvgl` with the
  helper does it for the strip of its active screen, `cp0::ui::StatusStrip`; an app that draws by itself checks its
  own strip's rectangle with `cp0_gesture_g2.h`).
- `touch_slots` in `screen.state` (section 4) says whether the panel can do G1 at all.

### Testing

`tools/multitasking-spike/` has `fbhapp`, a fake fullscreen app that speaks this protocol (its README; `--helper`
makes it use the helper above), and `desktop/` files that install it as `freeze`, `run`, slow-to-answer and legacy
test apps, and `mtplay`, a scripted uinput touch screen (three-finger holds and the patterns that must not fire). On
the PC: `ext_components/cp0_lvgl/tests/test_app_lifecycle.c` (the helper), `projects/APPLaunch/tests/multitask/`
(the real supervisor with `fbhapp`, with and without `--helper`) and, in the apps repository, each app's
`tests/scenes/lifecycle.scene` (the headless harness plays the launcher). The board test protocols are
`docs/dev/tests/multitasking-pb-protocol.md`, `-pc-`, `-pd-` and `multitasking-pe-protocol.md` (the real apps).

## 14. Requests to the launcher (`launcher.sock`)

An app is a separate process; the launcher owns the volume, the backlight, the sounds and the notices. An app asks it through one Unix stream socket, `$XDG_RUNTIME_DIR/applaunch/launcher.sock` (mode 0600, the launcher's user). One request line, one reply line, UTF-8, at most 254 bytes plus the newline. It is served by a thread of its own, so it works for apps launched in every mode (compat, fullscreen with or without multitasking, a terminal app) and from a shell or ssh as the same user. There is **no `/tmp` fallback**: without `XDG_RUNTIME_DIR` there is no service. Added within contract 1 (section 8); the protocol has its own level (`hello` says `1`), and verbs and reply fields are only added.

```
> hello 1                      < ok 1 launcher=0.8.6 app=mesh-hop verbs=hello,caps,toast,beep,volume,brightness,dim
> caps                         < ok sound=1 backlight=levels dim=1 verbs=...
> beep [notify|done|warn]      < ok | err nosound
> toast <text>                 < ok queued | err limit
> volume get|up|down|mute|unmute   < ok 40 muted=0 | err nosound
> volume set 55                < ok 55 muted=0
> brightness get               < ok 70 kind=levels
> brightness set 30            < ok 30 kind=levels | err absent (no backlight, or on / off only)
> dim allow|deny               < ok | err absent
```

Replies are `ok ...` or `err <word>`: `unknown` (no such verb: an older launcher, or a typo), `bad` (arguments), `nosound`, `absent` (the board has no such thing), `limit` (rate limit), `busy` (the backend failed or the server is full), `denied` (reserved, section 6 `X-Permissions`). `app=` in the `hello` reply is the name of the calling process (`/proc/<pid>/comm`, from the kernel's peer credentials; the request cannot name itself).

| Verb | What the launcher does |
|---|---|
| `beep` | plays a short built-in sound (`notify` by default, `done`, `warn`) **at the volume in force**, whatever the launcher's own interface sounds are (those stay off; there is no setting for app beeps). A board without a sound output answers `err nosound` and does nothing; board.state `sound=0` says so beforehand. A muted or very quiet volume is silent: that is the user's choice |
| `toast` | puts the text (printable UTF-8, cut at 160 characters) on the launcher's own toast. The launcher draws nothing over a fullscreen app, so the toast waits and shows when the launcher is in front (after the app exits or is hidden); at most 4 wait, the oldest goes. No badge, no sound |
| `volume` | reads or changes the output volume exactly like the volume keys (the same backend, saved the same way). `set` un-mutes. Apps that do not play media have no business here; `get` is for a level bar, but `status.state` already has it |
| `brightness` | `get` reads the percent; `set` snaps to the launcher's steps and saves like the key. Levels backlights only |
| `dim` | `allow` lets the launcher dim the backlight (to 10%) when nothing was touched for the user's *Dark time* (Settings > Screen; *Never* = never) while this app is in front, on a battery board with brightness levels (`requests` has `dim`); any key or touch brings the light back. Today the panel never dims while an app is in front. The permission is **for the app in front and ends when the launcher takes the panel back** (exit, hidden, Alt+Tab): send `dim allow` again after a resume. Touch is seen only on the supervisor's path (multitasking on); a mouse or trackpoint is not seen, so an app that is used with a pointer only should not allow it. `deny` withdraws it |

Limits (all per client process, answered `err limit`): `beep` and `toast` one per second, `volume`, `brightness` and `dim` ten per second, the rest fifty. Every request is logged by the launcher with the process name (`[req] mesh-hop: beep done`). Eight connections at most (more get `err busy`), an idle one is closed after 30 s, a line over the limit gets `err bad` and the connection is closed. The launcher never executes anything a client names: verbs are looked up in a fixed list, arguments are numbers or a few fixed words, the toast text is displayed and nothing else. **Permissions are an agreement, not a wall:** every client is the same user, who can already run `pactl` or write the backlight; the point is that the launcher keeps owning volume, brightness and sounds.

### The volume, brightness and mute keys inside an app

The launcher reads the keyboards while a fullscreen app is in front and acts on `KEY_VOLUMEUP`, `KEY_VOLUMEDOWN`, `KEY_MUTE`, `KEY_BRIGHTNESSUP` and `KEY_BRIGHTNESSDOWN` itself, as it does on its own pages (the same step, the same saved level; no rebuild of the app is needed). **These keys are the launcher's, like Alt+Tab: an app must not act on them too**, or the step doubles. The keys still reach the app (the launcher only reads them). Nothing is drawn on screen: the user hears the change, and the app may show the level from `status.state` (section 5b). On a board without the hardware (`sound=0`, `backlight=none`) they do nothing, as on the launcher's pages. A compat app runs inside the launcher's window, whose key handling already covers them.

**Media-key devices** (launcher 0.8.7 and later). Volume and brightness buttons are often not on a keyboard device. The launcher also reads every input device that is **not a keyboard** (it lacks A, Z, Enter or Space) but has one of the five keys above: a tablet's buttons (on the FZ-M1 "Panasonic Laptop Support" and "Intel Virtual Buttons"), the ACPI "Video Bus" (brightness keys), a keyboard's "Consumer Control" node, a Bluetooth headset's or remote's AVRCP node. The rule, by capability only:

- From such a device the launcher takes **only the five keys**; every other key of it (Esc, Enter, arrows, letters, power, F14, Super) is ignored: it cannot type, navigate or close an app. The Windows key of a tablet is read from its keyboard device (the FZ-M1's i8042 keyboard), not from a buttons device that declares it too (Intel Virtual Buttons). It works on the launcher's pages and while any app is in front, exactly like a keyboard's volume keys.
- It **never counts as a keyboard**: `input.state` (`keyboards=`, `vkb=`) and the on-screen keyboard rule are unchanged by it.
- It is **never grabbed** (such a device may also carry the power button, which the system must keep seeing) and never forwarded to a wrapped (compat) app; an app that opens the device itself sees its keys, as for a keyboard.
- One press reported by two devices counts **once** (the FZ-M1 declares its volume buttons on two devices; Super may come from two keyboards): for these five keys and Super a press from a second device while the first holds the key, or less than 80 ms after the first's press, is dropped with its release.
- HDMI CEC and infrared receivers (the kernel's remote-control devices, `/rc/` in their sysfs path) are not read: their remotes are not the board's buttons.
- `APPLAUNCH_KBD_IGNORE` applies: a `drop:` entry (`drop:Video Bus`, `drop:bus:0019`) keeps a device from being read; a plain entry changes nothing (it is never counted anyway). `APPLAUNCH_KEYBOARD_SCAN=0` reads neither. Devices that come and go (a headset connecting) are picked up like keyboards.

**Reserved:** `KEY_F14`. The FZ-M1's rotation-lock button sends Super+F14 (in the BIOS's "Rotation lock" mode); a later launcher may give it a meaning. Today the launcher does nothing with it and it reaches the page or the app like any key; do not give it a meaning of your own.

### Degrade rules

| Situation | `board.state` | `launcher.sock` | The app |
|---|---|---|---|
| launcher 0.8.6 or later | good | answers | uses both |
| an older launcher | missing | `ENOENT`: `-1` at once | hides sound options, skips beeps and toasts silently, shows no error |
| the launcher crashed or stopped | `pid` gone: `-1` | `ECONNREFUSED`: `-1` at once | as an older launcher |
| started over ssh, launcher running | readable | answers (the process name is `ssh`'s child) | works |
| started over ssh, no launcher | missing | missing | as an older launcher |
| multitasking off | same | same: the server does not depend on the supervisor | a toast shows after the app exits |
| the app is frozen while hidden | - | it cannot send | only `X-Background=run` apps notify in the background |

The client (`cp0_app_client.h`) returns `-1` for every "no service" case within its timeout (200 ms by default) and prints nothing; **never call it on a thread that must not stall** (the LVGL thread of a toolkit app): use a worker, and remember a failure for a few seconds instead of retrying per event.

### The command line: `cyberdeckzero-ctl`

`cyberdeckzero-ctl` (installed in `/usr/share/APPLaunch/bin`, linked in `/usr/local/bin`) is the same client for terminal apps, scripts and the board test protocols: `cyberdeckzero-ctl hello`, `beep done`, `toast "2 new messages"`, `volume set 30`, `brightness get`, `dim allow`, `board` (prints `board.state`). It prints the reply; exit status 0 ok, 1 refused (`err ...`), 2 usage, 3 no launcher service.
