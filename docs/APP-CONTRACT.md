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
| **Contract** | environment variables, `screen.state`, `input.state`, `.desktop` keys | stable within contract 1 (section 8) |
| **SDK** | C files an app copies (section 9) | source, versioned with the launcher tree; no ABI promise |
| **Internal** | the virtual framebuffer file (VFB2) between the launcher and its `LD_PRELOAD` shim | may change in any release; apps never read it |

## 2. Who writes, who reads

| Item | Written by | When | Read by |
|---|---|---|---|
| per-launch environment | the launcher, on the app's command line only | each start of an app | that app |
| panel environment (`APPLAUNCH_SCREEN_*`) | the launcher, process-wide at display creation | once | every child process |
| `screen.state` | **only** the launcher (atomic rename) | at display creation, and again after **every** app exits (`gen + 1`) | any app |
| `input.state` | **only** the launcher (atomic rename) | keyboard list change, Settings > System > On-screen keyboard or Pointer navigation change | any app |
| `.desktop` keys | the app's package | install | the launcher |

The state files live in one directory: `$APPLAUNCH_STATE_DIR`, else `$XDG_RUNTIME_DIR/applaunch/`, else the files
`/tmp/applaunch-screen.state` and `/tmp/applaunch-input.state` (`cp0_app_state_path()` implements this order).

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
| `APPLAUNCH_LIFECYCLE=1`, `APPLAUNCH_CONTROL_FD=<n>` | never | only for an app with `X-Background` and only from a launcher with multitasking on: the lifecycle control socket (section 13, draft) |

All coordinates are **logical**: landscape, before the panel's rotation. Compat apps also get the launcher's
virtual-framebuffer setup (`APPLAUNCH_VFB`, `LD_PRELOAD`, `LV_LINUX_FBDEV_DEVICE=/dev/fb0`, `APPLAUNCH_FB=/dev/fb0`,
`APPLAUNCH_ROTATE=0`, the virtual keyboard in `LV_LINUX_KEYBOARD_DEVICE`): these are plumbing, not contract. A
terminal app (`Terminal=true`) runs inside the launcher's terminal page and gets no per-launch variables.

### Panel (process-wide; always the panel, never the app's canvas)

| Variable | Meaning |
|---|---|
| `APPLAUNCH_SCREEN_W`, `APPLAUNCH_SCREEN_H` | logical (landscape) panel size, px |
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
| `touch_slots` | (draft, multitasking, section 13) how many contacts the touch screen tracks: `0` no touch screen, `1` single touch (or a panel without slots), `N` multi-touch slots (the deck 5, the Pi 3A+ DSI 10); written after `touch=` by a launcher running with `APPLAUNCH_MULTITASK=1` only; absent = unknown |
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
gen=4
```

| Key | Meaning |
|---|---|
| `keyboards` | real keyboards present now |
| `bt_keyboards` | of them, Bluetooth |
| `last_seen` | unix seconds of the last time `keyboards > 0` (0 = never since boot) |
| `kb_gone`, `bt_gone` | **monotonic** seconds (`CLOCK_MONOTONIC`, same in every process of the boot) when the keyboard / Bluetooth keyboard count last dropped to 0; `0` = never |
| `vkb` | the on-screen keyboard rule in force: `auto`, `off` (Settings > System > On-screen keyboard Off, or a desktop session owns the screen), `force` (`APPLAUNCH_OSK=force`, development and the HackBerry) |
| `pointer` | Settings > System > Pointer navigation: `on` or `off` (the default). See section 10b |
| `gen` | increases on every write |

A file without `vkb` (an older launcher) means `auto`; a file without `pointer` means `off`.

## 6. `.desktop` keys

| Key | Meaning |
|---|---|
| `X-Fullscreen=true` | fullscreen mode: the launcher stops drawing, the app owns the whole panel until it exits (the hold-Esc watchdog still applies). Without it the app is a compat app: 320x170 in the scaled window, toolbar below. |
| `X-Background=run\|freeze` | **draft (section 13)**: the app may be hidden and resumed instead of owning the panel until it exits; `freeze` = stopped while hidden, `run` = keeps working while hidden. Absent or another value: today's behaviour. Only with `X-Fullscreen=true`, only under a launcher with multitasking on; an older launcher ignores it. |
| `X-MinSize=WxH` | the logical screen size the app needs (`x` or `X`, each 1..65535). On a smaller screen (either dimension) the tile is **hidden**, like `Hidden=true`: the app is not registered, so it does **not appear in Settings > Apps either**. Malformed = no restriction. A launcher without layout metrics (stock CardputerZero, the SDL simulator) shows it. |

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
| `include/cp0_app_contract.h`, `src/cp0_app_contract.c` | `cp0_app_env_read()`, `cp0_app_state_path()`, `cp0_app_screen_read()` / `_parse()` / `_compute()`, `cp0_app_input_read()` / `_parse()` (with `pointer_nav`), `cp0_app_osk_allowed()` / `cp0_app_osk_rule()`, `cp0_app_monotonic_s()` | `cp0_ui_metrics.{h,c}` |
| `include/cp0_ui_metrics.h`, `src/cp0_ui_metrics.c` | the layout service: class rules, density, tokens, text sizes, compat window (`cp0_ui_metrics_compute()`) | libc only |
| `src/cp0/cp0_fb_output.h`, `src/cp0/cp0_fb_output.c` | drawing into a mapped framebuffer with the panel's rotation and 16/32 bpp; touch mapping (`cp0_fbo_setup()`, `cp0_fbo_blit*()`, `cp0_fbo_pack()`, `cp0_fbo_touch_to_logical()`) | libc only |

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
probably asleep) and not within 2 s of any keyboard going away. `vkb=off` never. The launcher's own keyboard uses
exactly this function, so the app and the launcher agree. Re-read `input.state` when its `gen` changes (a keyboard
connects, the user switches the setting). The keyboard must never be the only way in: the policy is "physical
keyboard first, fallback keyboard only when none is connected, never summoned by hand".

## 10b. Pointer navigation

With `pointer=on`, a mouse, trackball, trackpoint or the HackBerry's optical pad drives the keyboard UI: motion is
turned into arrow keys at a rate that follows the push (one axis at a time, a dead zone for rest noise), the wheel into
Up / Down, a click (button released within 0.6 s, without moving) into Enter, and a held click (0.6 s without moving)
into **Ctrl+O, the options key** (`KEY_O` with `KBD_MOD_CTRL`; Mesh Hop's chat options use it). A pointer is a device
with `REL_X`, `REL_Y` and `BTN_LEFT` and no absolute axis or `BTN_TOUCH` (never a touch screen or a touchpad). The keys
arrive like a physical keyboard's (press + release, `keysym` 0), so an app needs no code for them; an app that has an
options menu should open it on Ctrl+O. While a text field edits (`KBD_INPUT_CONTEXT_TEXT`) Left / Right and Ctrl+O are
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

## 13. Lifecycle: hidden and resumed (draft, multitasking)

**Status: draft, additive to contract 1** (no `v=` change, section 8). The launcher side is task 015
(`docs/dev/tasks/015-multitasking-pb.md`, multitasking phase P-b) and runs only with `APPLAUNCH_MULTITASK=1` in the
launcher's environment; the shared app side (`apps/_common`, the viz1090 shim, Mesh Hop) is phase P-e. Design and
owner decisions: `docs/dev/reports/026-multitasking-study.md`. An app that declares nothing behaves exactly as
before, under any launcher.

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
| app -> launcher | `switcher` | open the switcher (multitasking P-d; until then the same as `home`) |

### Obligations

- **Answer `hide` in time.** No `ack <n>` within 300 ms: the launcher stops the whole process group with `SIGSTOP`
  and shows the app as *not responding*. It can still be resumed (its last frame is restored, its input emptied, the
  group continued) or closed. A `run` app that cannot stop drawing must not declare `run`.
- **No framebuffer write while hidden.** The launcher draws the home grid (later the switcher) over your last frame.
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

### Gestures (draft, multitasking phase P-c)

- **Three fingers held still for 1 s anywhere** (G1) and **a long-press on the status strip** (G2) open the
  switcher; until it exists (phase P-d) they hide the app in front, as Super does (a legacy app stays). While an app
  is in front the launcher reads the touch screen **passively** (a second file descriptor, never `EVIOCGRAB`): the app
  still gets every touch, including the three fingers. When G1 fires the fingers are still down: `hide` arrives in
  the middle of a press; cancel it (no click) as the `hide` obligation says, and ignore the touches until `show`.
- Do not give three or more fingers a meaning of your own (a four-finger contact cancels G1, but three fingers held
  still are the launcher's). One- and two-finger gestures (pinch, pan, two-thumb typing) stay the app's.
- G2 in an app is the app's own: a long-press on its status strip sends `switcher` (the shared `cp0_lvgl` strip will
  do it in phase P-e).
- `touch_slots` in `screen.state` (section 4) says whether the panel can do G1 at all.

### Testing

`tools/multitasking-spike/` has `fbhapp`, a fake fullscreen app that speaks this protocol (its README), and
`desktop/` files that install it as `freeze`, `run`, slow-to-answer and legacy test apps, and `mtplay`, a scripted
uinput touch screen (three-finger holds and the patterns that must not fire). The board test protocols are
`docs/dev/tests/multitasking-pb-protocol.md` and `multitasking-pc-protocol.md`.
