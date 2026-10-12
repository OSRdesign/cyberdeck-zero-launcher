# app-contract-probe

A small test app for the launcher's **app contract v1** ([`docs/APP-CONTRACT.md`](../../../docs/APP-CONTRACT.md),
task 014 P5). It reads everything the contract offers an app and either prints it or draws it, so a person (or the
render harness) can check that the launcher tells its apps the truth.

Plain C99, no LVGL. It is built only from `probe.c`, `font5x7.h` and the contract SDK files of
`ext_components/cp0_lvgl` (the same files an app copies): `cp0_app_contract.{h,c}`, `cp0_ui_metrics.{h,c}`,
`cp0_fb_output.{h,c}`.

## Build

Linux or WSL (`gcc`; `gcc-aarch64-linux-gnu` for the boards):

```sh
sh tools/dev/app-contract-probe/build.sh            # both
sh tools/dev/app-contract-probe/build.sh host       # build/host/app-contract-probe
sh tools/dev/app-contract-probe/build.sh aarch64    # build/aarch64/app-contract-probe (Pi Zero 2 W, 3A+, HackBerry)
```

It is not part of `tools/dev/wsl-build.sh` (the Pi bundle) nor of the release: it is a development tool.

## Modes

| Command | What it does |
|---|---|
| `app-contract-probe --report [--no-fb]` | Prints one `key=value` line per fact and exits 0 (see below). `--no-fb` skips opening the framebuffer. |
| `app-contract-probe [--tag NAME] [--fb DEV] [--timeout S]` | Draws the test pattern on the framebuffer and waits for **Esc** or **Q** (or the timeout, default 300 s, `0` = none). Also prints the report on stderr and writes it to `/tmp/app-contract-probe-<tag>.txt`. Re-reads `screen.state` / `input.state` once a second and redraws when a `gen` or the keyboard rule changes. |
| `app-contract-probe --ppm FILE [--ppm-bpp 16\|32]` | Draws the same pattern into memory as the panel would hold it (fullscreen: turned by the panel's rotation) and writes a PPM picture. For checks on a PC. |

**Which framebuffer.** Compat (`APPLAUNCH_APP_MODE=compat`): `APPLAUNCH_FB` (the launcher sets `/dev/fb0`), which the
launcher's `LD_PRELOAD` shim turns into the 320x170 virtual framebuffer; drawn upright. Fullscreen or no contract:
`APPLAUNCH_SCREEN_FB`, else `fb=` of `screen.state`, else `APPLAUNCH_FB`, else `/dev/fb0`; drawn through
`cp0_fb_output` with the panel's clockwise rotation (`APPLAUNCH_SCREEN_ROTATE`) and its depth (16 or 32 bpp, the
16 bpp channel layout read from the driver). `--fb DEV` overrides.

**The pattern.** On the canvas the app owns: a 1 px white then red border on its edge, a 1 mm grid (every 10th line
brighter), a **9 mm orange square** at the bottom right whose top-left corner is on a grid line (it covers 9 x 9 grid
cells), and the report summary (about 1.4 mm high letters). The density used is the canvas density,
`APPLAUNCH_SCREEN_PPMM / APPLAUNCH_CANVAS_SCALE`: in a compat window shown at 2x the square is 51 canvas pixels,
102 screen pixels, 9 mm on the glass.

**Keys.** Compat: the launcher's virtual keyboard (`LV_LINUX_KEYBOARD_DEVICE`), which carries the physical keys and
the toolbar. Fullscreen or by hand: that device too, plus every `/dev/input/event*` that has an Esc key (the user
must be allowed to read them, as for any fullscreen app), plus the terminal when stdin is one. With no keyboard it
leaves on the timeout; in compat the launcher's hold-Esc still ends it.

## The report

Prefixes: `probe.*`; `env.*` (the variables as found, raw); `screen.*` (`screen.state`, only when `screen.ok=1`);
`input.*` (`input.state`, only when `input.ok=1`); `fb.*` (the fbdev ioctls); `derived.*` (what an app goes by after
the degrade rules: `derived.source=contract|state|fbdev|none`); `summary=` (one line: mode, canvas, window, scale,
class, density). Deck, compat Store-style app:

```
env.ok=1
env.mode=compat
env.window=0,0,640,340
env.canvas=320x170
env.canvas_scale=2
...
derived.class=standard
derived.canvas_ppmm=565
derived.square_9mm_px=51
derived.osk_allowed=0
summary=compat 320x170 0,0,640,340 scale 2 standard 1130
```

Readers must ignore keys they do not know; the probe adds keys only (`probe.version=1`).

## Install on a board (by hand)

The Store and Settings > Apps are not used. The launcher lists every `*.desktop` file of
`/usr/share/APPLaunch/applications/` (it watches the folder: the tiles appear without a restart). `Exec` must be an
absolute path to an executable with no shell characters, and no two entries may have the same `Exec`, which is why
the three variants pass a different `--tag`. No `Icon`: the launcher's default tile icon.

| File | Mode | Tile |
|---|---|---|
| `desktop/app-contract-probe-compat.desktop` | compat: the 320x170 window with the toolbar | `Probe compat` |
| `desktop/app-contract-probe-full.desktop` | `X-Fullscreen=true` | `Probe full` |
| `desktop/app-contract-probe-min.desktop` | `X-Fullscreen=true`, `X-MinSize=640x480`: hidden on a smaller screen (and then not in Settings > Apps either) | `Probe min` |

From the PC (WSL), with `<board>` the board's ssh name (for example the user's ssh alias or `user@ip`):

```sh
cd tools/dev/app-contract-probe && sh build.sh aarch64
scp build/aarch64/app-contract-probe desktop/*.desktop <board>:/tmp/
ssh -t <board> 'sudo install -m 0755 /tmp/app-contract-probe /usr/share/APPLaunch/bin/ &&
  sudo install -m 0644 /tmp/app-contract-probe-*.desktop /usr/share/APPLaunch/applications/'
```

Remove:

```sh
ssh -t <board> 'sudo rm -f /usr/share/APPLaunch/bin/app-contract-probe /usr/share/APPLaunch/applications/app-contract-probe-*.desktop'
```

On the board, without the launcher: `XDG_RUNTIME_DIR=/run/user/$(id -u) /usr/share/APPLaunch/bin/app-contract-probe --report`
shows what an app started some other way finds (`env.ok=0`, `derived.source=state`).

## Checked by

`projects/APPLaunch/tests/render/run.sh contract` (also a step of `tools/dev/check-all.sh`): the host build reads,
at six screen sizes, the environment the launcher's own builder (`app_launch_env`) produces and the `screen.state` the
display manager published in the harness, and every fact must equal the layout service's numbers.
