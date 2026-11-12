# App design QA: the scene set

The headless renders an app ships for [`app-design-qa`](../agents/app-design-qa.md), which checks them against
[`app-design.md`](app-design.md) without a board. `apps-dev` writes the scenes and renders them with the app's headless
harness (apps repo
[`apps/_common/headless/README.md`](https://github.com/OSRdesign/cyberdeck-zero-apps/blob/main/apps/_common/headless/README.md):
the scene commands and the layout checks); the QA only reads the result.

## Sizes

One shot of every page at every size the app supports. The harness gives each size the launcher's density, except the
small panels, which are rendered at their real density in a scene of their own (`ppmm` holds for a whole scene).

The 3.5" panel is 79x49 mm (`APPLAUNCH_PANEL_MM`, `tools/boards/boards.json`); the layout service resolves it to
6.3 px/mm in both orientations (`cp0_ui_density_resolve`, the mean of the two axes). The apps harness turns `ppmm N`
into whole millimetres, so `ppmm 630` renders at 630, while the older `ppmm 608` and `610` render at 606 and 611:
accepted (480x320 is a pinned preset whose tokens do not move, and the 320x480 Esc is 44 px at all three), but use
630 in new scenes. Other values (667, 800) stand for no board: do not use them for QA shots. Without a `ppmm` line the
apps harness renders 480x320, 320x480 and 480x480 at the deck's 11.3 px/mm (`default_ppmm` in
`apps/_common/headless/headless_main.cpp`).

| Size | Class | Stands for | When | Scene |
| --- | --- | --- | --- | --- |
| 640x480 | Standard | the deck, the reference size (11.3 px/mm) | always | `qa` |
| 800x480 | Wide | the 800x480 DSI panels, HyperPixel 4 | always | `qa` |
| 480x480 | Square | a small square panel | always | `qa` |
| 720x720 | Square | the HackBerry | always | `qa` |
| 480x320 | Compact | the 3.5" panels (Pi 3A+, zero8), landscape | always; under a declared `X-MinSize` only the `notice` scene | `qa-small` (`ppmm 630`) |
| 320x480 | Portrait, narrow | the 3.5" panels in PDA mode | `X-Portrait=320x480` | `qa-small` (`ppmm 630`) |
| 480x800, 480x640 | Portrait | the 4.3" DSI and 3.5" panels in PDA mode | any `X-Portrait` | `qa` |
| 1280x720, 1024x600, 800x1280 | Large, Wide, Portrait | the FZ-M1 tablet class | optional (the harness's default set has the first two) | `qa` |

## What to render

| Scene file (`<app>/tests/scenes/`) | Content | When |
| --- | --- | --- |
| `qa.scene` | every page once (keys or taps to reach it), each dialog, a busy state (none when nothing makes the user wait: say so in the scene's `title`) and an error state, at the `qa` sizes above; `expect esc` after the first shot | always |
| `qa-small.scene` | the same walk at 480x320 (and 320x480), `ppmm 630` | always (only the notice when `X-MinSize` excludes 480x320) |
| `qa-empty.scene`, `qa-cjk.scene` | the empty state (no data, no files) and a CJK sample string in every place outside text shows (D-55), at the `qa` sizes and, where the app runs there, 480x320 and 320x480 (a second scene with `ppmm 630`) | with an empty state; with outside text |
| `nocontract.scene` | started by hand: the app starts without crashing (the headless `.log` stays empty, D-25) | always |
| `notice.scene` | the "needs WxH" notice under the declared minimum | with `X-MinSize` |
| `keyboard.scene` | a text field with `keyboards 0` (keyboard shown), then `keyboards 1` (hidden) | with a text field |
| `portrait-band.scene` | a text field at 480x800 and 480x640 with the writing band (`band`, `expect band shown`) | with `X-Portrait` of 480 px wide or more and a text field |
| `lifecycle.scene` | hide, keys while hidden, show, close (`expect nowrite`, `expect repaint`, `expect quit`) | with `X-Background` |

An app with many pages may split `qa.scene` into `qa-<part>.scene` files (all start with `qa`). An app that already has
scenes for these states (a dialog, an error) may hand them over instead of repeating them, as long as their sizes match
the table: list them on the `extra=` line of `qa-info.txt` (below) and copy them too.

## Names

- Shots: `NN-<page>[-<state>]`, two digits in walk order, lower case and hyphens: `01-home`, `02-settings`,
  `03-settings-picker`, `04-dialog-delete`, `05-busy`, `06-error-nodevice`. The same names at every size, so a page is
  found at each size by its name.
- The harness writes `out/<scene>/<WxH>/<shot>.png`, `out/<scene>/<WxH>/report.txt` (each layout check, PASS or FAIL),
  `out/<scene>/<WxH>.log` (the app's stderr) and `out/sheets/<scene>.png` (one contact sheet per scene).

## Rendering

In WSL, from the apps worktree (the launcher tree as `apps/_common/build_app.sh` describes: `LAUNCHER_SRC`, `LAUNCHER_TREE`):

```
wsl -e sh /mnt/c/CLAUDE/zero7/<apps worktree>/apps/<id>/build.sh host
```

It builds the harness, runs the unit tests and every scene into `~/cdk-build/<id>/host/out/`. One scene by hand, after a
build: `~/cdk-build/<id>/host/<id>-headless --scene apps/<id>/tests/scenes/qa.scene --out out --resources
<launcher tree>/projects/APPLaunch/APPLaunch`. A frozen header clock makes two renders identical (optional; Cassette's
`docs/freeze-clock.c` and `docs/MAKING-SCREENSHOTS.md` show how).

## Hand-off to the QA

Copy, keeping the tree, the folders `qa*`, `nocontract`, `notice`, `keyboard`, `portrait-band`, `lifecycle` (those that
exist), the `extra=` folders and `sheets/` from `~/cdk-build/<id>/host/out/` into a folder outside both repositories
(for example `C:\CLAUDE\zero7\pkg-out-<id>\qa\`). The folder must hold:

- the tree `<scene>/<WxH>/` with every `<shot>.png` and `report.txt`, and the `<scene>/<WxH>.log` files;
- `sheets/` (one contact sheet per scene);
- `qa-info.txt` (below);
- `deb-info.txt` once a `.deb` is built (always at a publish): the output of `dpkg-deb -I <deb>` then `dpkg-deb -c
  <deb>`, for example `wsl -e sh -c 'dpkg-deb -I <deb>; dpkg-deb -c <deb>' > deb-info.txt` (D-74).

A flattened folder (shots renamed into one directory) or one without `report.txt` or `qa-info.txt` is a D-01 FAIL.

```
app=<id>
version=<app.json version>
apps_commit=<commit the scenes were rendered from>
launcher_ref=<apps/_common/LAUNCHER_REF>
extra=<other scene folders handed over, space separated, or empty>
controls=<optional, for controls the app draws itself: "<size> <name> <drawn WxH> hit <WxH>" separated by ";">
```

The `controls=` line (e.g. `640x480 queue-button 56x56 hit 79x79; 480x320 queue-button 42x42 hit 44x44`) helps D-33
when the harness reports `buttons min 0`. The app's owner rulings and waivers are in
`apps/<id>/docs/decisions.md` in the worktree (the QA reads it there). The hand-off report names the folder; the
Controller passes it to `app-design-qa` with the app id and the worktree.

## A `qa.scene` to start from

```
title <App>: design QA walk (every page)
sizes 640x480 800x480 480x480 720x720
# with X-Portrait: add 480x800 480x640
wait 600
shot 01-home
expect esc
key down
key enter
wait 300
shot 02-settings
key esc
wait 300
key esc
expect quit
```

`qa-small.scene` is the same walk with `sizes 480x320` (and `320x480`) and a `ppmm 630` line.
