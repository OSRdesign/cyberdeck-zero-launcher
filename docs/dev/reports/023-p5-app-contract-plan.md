# 023 - P5 app contract: investigation and plan

Status: plan written 2026-10-12 on branch `p5-app-contract` (= main 684392b), Controller decisions at the end. Task 014 P5
(decision D10). Evidence: file:line, **V** = verified in code, **I** = inferred (needs a board).

## 1. What exists today vs what the study promised

| Item | Promised (022:438-455, 020:77, 014:104) | Today (V) | Gap |
|---|---|---|---|
| Env | `APPLAUNCH_SCREEN_W/H/ROTATE/PPMM/CLASS` + resolved fb and touch | the 5 SCREEN vars, process-global `setenv` at display creation (`cp0_ui_state.c:101-110`, called `cp0_lvgl_dpi_scaled.c:895`), inherited by every child | no resolved fb/touch; no mode/window/canvas; a compat child sees the 640x480 panel values while it draws 320x170 |
| Per-launch env (compat) | - | `run_external` `native_ui.cpp:670-687`: exports `APPLAUNCH_VFB`, `LV_LINUX_FBDEV_DEVICE=/dev/fb0`, `APPLAUNCH_FB=/dev/fb0`, `APPLAUNCH_ROTATE=0`, vkbd keyboard, `LD_PRELOAD`; unsets `LOGICAL/COMPAT_SCALE/PANEL_MM` (the P3e neutral env, 683d4f7); keeps `APPLAUNCH_DISPLAY`, `APPLAUNCH_TOUCH_*`, `APPLAUNCH_SCREEN_*` | nothing says "compat" |
| Per-launch env (fullscreen) | - | none: whole launcher env (`launch.cpp:204-206` -> `cp0_external_app_runner.cpp:97-104`, `sh -c`) | usable but undocumented |
| `screen.state` | `v w h rot ppmm class compat scale toolbar kb_inset gen`, apps watch `gen` | exists: `cp0_ui_metrics.c:433-441`, path `cp0_ui_metrics.h:182-189`, plus `density=`; written **once**; `kb_inset=0` literal (`:438`) | no app reader (only `cp0_ui_state_parse_gen` `:465-480`; `cp0_ui_metrics_from_env()` never written); no tokens, no fb |
| `cp0_ui_metrics.h` | published, versioned with `sdk_version.txt` | marked "Internal for now (D10)" (`:23`); signature differs from the study (`w,h,rot,ppmm,src,compat_cap`); `sdk_version.txt` = v0.0.7 | not published |
| `input.state` | `v=1 keyboards=N vkb=auto\|off gen=G` | `v=1 keyboards last_seen gen` (`cp0_input_state.h:5-9`), written on list changes only (`input_presence.cpp:63-95`) | no `vkb=`; the gone times the OSK policy uses stay in-process (`input_presence.cpp:36-61`); Settings `set_mode` does not republish (`launcher_osk.cpp:77-83`) |
| OSK for apps | `vkb=`/`kb_inset=` (P5) | in-process only (`cp0::ui::osk`; terminal uses it, P4b); the fullscreen path blocks the UI by design (`launch.cpp:195-221`, enforced by `tests/test_external_framebuffer_ownership.py`) | the launcher can never draw over a fullscreen app; compat overlay = F1 phase 2, not done |
| VFB | VFB2: 64 KiB header, `host_w host_h scale ppmm_x100 canvas_mode` | VFB1, 4096-byte header (`applaunch_vfb.h:19-23`), fixed 320x170, 2 pages (`native_ui.cpp:556-573`); shim maps pixels at +4096 (`applaunch_vfb_shim.c:163-165`), reports mm = -1 (`:197`) | 16 KB-page kernels; no fields |
| `.desktop` | `X-Canvas`, `X-Fullscreen`, `X-MinSize` | `Name Icon Exec Terminal Sysplause X-Fullscreen Hidden/NoDisplay Type` (`desktop_entry.cpp:78-85`) | `X-Canvas`, `X-MinSize` missing |

**Findings**
- **F1 (V code, I runtime) - `screen.state` gets clobbered.** Any cp0_lvgl app run in the vfb inherits
  `APPLAUNCH_DISPLAY=dpi-scaled` (`cp0_lvgl_freambuffer.c:57-59`); its backend opens the vfb (320x170) and calls
  `cp0_ui_metrics_publish` unconditionally (`cp0_lvgl_dpi_scaled.c:895`), overwriting the launcher's file with
  `w=320 h=170`; the launcher never rewrites it. Hits the Store (links cp0_lvgl, `projects/AppStore/main/SConstruct:28`),
  LanScan / Wi-Fi Survey if rebuilt after P1b (built against the launcher tree, `lanscan/build/build.sh`).
- **F2** a compat child cannot know it draws 320x170 (no mode / canvas; SCREEN vars describe the panel).
- **F3 (I)** compat apps very probably fail on the HackBerry and the Pi 5 today (16 KB pages, 021 addenda 3-4; mmap at
  +4096, 021:433-439). No HackBerry stock-app test found in `docs/dev/tests`.
- **F4** Mesh Hop hard-codes 640x480 `/dev/fb0` (`platform.hpp:23-24`, `platform.cpp:109-110`): broken on the Pi 3A+
  (fb1 320x480 portrait 16 bpp, I). It parses `/proc` for keyboards itself (`core/input_state.cpp:60-79`), no cp0_lvgl
  runtime (`SConstruct:13`).

## 2. Contract v1 proposal

**Principle: three layers.** *Contract* (stable, versioned `APPLAUNCH_CONTRACT=1` / `v=1`): env, `screen.state`,
`input.state`, `.desktop` keys. *SDK* (source files apps copy, like `cp0_statusbar`; versioned by `sdk_version.txt`,
no ABI promise). *VFB2* stays launcher <-> shim internal (apps only see fbdev ioctls).

**Per-launch env** (set only on the child's command line, by one pure builder used by `run_external` and `launch_Exec`):

| Var | compat | fullscreen |
|---|---|---|
| `APPLAUNCH_CONTRACT` | `1` | `1` |
| `APPLAUNCH_APP_MODE` | `compat` | `fullscreen` |
| `APPLAUNCH_WINDOW=x,y,w,h` (screen coords) | compat window, e.g. `0,0,640,340` | `0,0,W,H` |
| `APPLAUNCH_CANVAS=WxH` (pixels the app draws) | `320x170` | `WxH` (logical, landscape) |
| `APPLAUNCH_CANVAS_SCALE` | 2 / 1 | 1 |
| `APPLAUNCH_STATE_DIR` | dir of both state files (removes the `/tmp` fallback guesswork) | same |

Process-global (kept, = **the panel**): the 5 `APPLAUNCH_SCREEN_*` + new `APPLAUNCH_SCREEN_FB` (resolved device, for
fullscreen apps). Not overridden for compat children. Canvas density = PPMM / CANVAS_SCALE.

**`screen.state`** (only the launcher writes): today's keys (one `key=value` per line, atomic rename) + `fb=`, `bpp=`,
tokens `space=s,m,l radius=s,m,l header= row= target= dialog_max_w= text=title,heading,body,caption`; `kb_inset=0`
documented "reserved, always 0 in contract 1". The launcher rewrites it (`gen+1`) after every app exits (heals old
clobbering binaries). Guard: `cp0_ui_metrics_publish` does nothing (no file, no setenv) when `APPLAUNCH_APP_MODE` is set
(fixes F1).

**`input.state`** (`v=1`, additive): `bt_keyboards=N`, `kb_gone=`, `bt_gone=` (monotonic s, 0 = never),
`vkb=auto|off|force` (effective: off under a desktop session; `APPLAUNCH_OSK` override). Rewritten on presence changes
(the watcher thread keeps running during a fullscreen app, I) and on the Settings On-screen keyboard change.

**`.desktop`**: `X-Fullscreen` unchanged; new `X-MinSize=WxH` (logical px): tile hidden when the screen is smaller; no
metrics (stock/SDL) = shown. An older launcher ignores it, so the app must still start and say "screen too small".

**SDK** (new, pure C99, MIT; `include/cp0_app_contract.h`, `src/cp0_app_contract.c`, no LVGL, no cp0 runtime):
`cp0_app_env_read(&env)` (-1 = not started by a contract launcher), `cp0_app_screen_read(dir,&s)` /
`cp0_app_screen_parse()` (unknown keys ignored, `v!=1` = missing), `cp0_app_input_read(dir,&in)`,
`cp0_app_osk_allowed(&in, now_mono)` (the F1 rule, moved so `cp0_ui_osk_policy_allows` calls it, not copied). Plus
`cp0_ui_metrics.{h,c}` published (token compute when the file is missing).

**Failure modes** (rule: degrade to the app's own probe, never crash): no env / no file / bad file -> app reads its fbdev
`VSCREENINFO` and uses defaults (11.3 px/mm); torn reads impossible (rename); readers inotify the dir (`IN_MOVED_TO`) or
stat <= 1 Hz and re-read on `gen`; stale files if the launcher crashed (acceptable: the screen does not change at
runtime); keep-root apps get the same env.

**Stability (contract 1):** names, units and meanings of listed vars/keys never change; keys only added; readers must
ignore unknown keys/vars; a breaking change bumps `v=` and `APPLAUNCH_CONTRACT`, and the launcher keeps the old
version's keys for at least one release.

**VFB2 (internal):** header 64 KiB (`header_bytes=65536`: multiple of 4/16/64 KiB pages), the 9 VFB1 fields at the same
offsets, + `version host_w host_h win_x win_y win_w win_h scale ppmm_x100 canvas_mode kb_request kb_inset gen`; the shim
takes the data offset from `header_bytes`. `VSCREENINFO` keeps mm = -1 on purpose: real mm would change the Store's
density (`cp0_lvgl_dpi_scaled.c:813-814` reads fbdev mm) and so its rendering.

**Included vs deferred**

| Included in v1 | Deferred (reason) |
|---|---|
| per-launch env; F1 guard + rewrite after exit; file reader | `X-Canvas=fill` (vfb as big as the window, launcher-side rotation/16 bpp, virtual touch): needs a CPU measurement on the Zero 2 W and Controller decisions on toolbar/touch -> v1.1 (P5-4) |
| `vkb` + gone times + shared policy rule | launcher-drawn OSK over compat apps (`kb_request`/`kb_inset`) = F1 phase 2, needs Controller design |
| `X-MinSize` (hide) | grey tile / "needs a bigger screen" in Settings > Apps (UI decision) |
| VFB2 64 KiB header + fields (fixes F3) | `X-Responsive` (no launcher behaviour to attach) |
| `APPLAUNCH_SCREEN_FB` | touch export (Q6) |

Honest limit: v1 makes a **fullscreen native app** (Mesh Hop) fully responsive; a **wrapped vfb app** becomes *aware*
(class, density, scale) but still draws 320x170 until `X-Canvas=fill`.

**Questions** (Controller / user): see the "Controller decisions" section below for the questions and their answers.

1. Scope: v1 as above, fill in v1.1 (or add P5-4 now, +0.4-0.6M tokens)?
2. Tokens: publish them in `screen.state` (always match the device's launcher) + ship compute as fallback, or apps recompute only?
3. Env names (`APPLAUNCH_CONTRACT`, `APP_MODE`, `WINDOW`, `CANVAS`, `CANVAS_SCALE`, `STATE_DIR`, `SCREEN_FB`; `SCREEN_*` = panel) OK? `APP_MODE=terminal` for terminal apps?
4. `X-MinSize` (short, like `X-Fullscreen`) or `X-AppLaunch-MinSize`; hide (no design) or grey (Controller design)?
5. Fullscreen portability: also publish `cp0_fb_output.{h,c}` as an SDK helper (pure C, unit-tested, `src/cp0/cp0_fb_output.h:61-110`: rotation, 16 bpp, touch mapping), so Mesh Hop runs on the 3A+/HackBerry without fill mode?
6. Touch for fullscreen apps: export `APPLAUNCH_SCREEN_TOUCH=<path>` and document `APPLAUNCH_TOUCH_*`, or leave it to fill mode?
7. Confirm OSK rule for fullscreen apps: app-drawn, per the shared policy; the launcher never draws over them.
8. Monotonic seconds for the new `input.state` keys (no-RTC boot jump) vs unix like `last_seen`?
9. SDK home `ext_components/cp0_lvgl` (+ copy-ready list in docs); bump `sdk_version.txt` to v0.1.0?
10. VFB2: change the shim in lockstep (`tools/boards/board.py:184` already deploys it with the binary), no versioned `.so` name?

## 3. Launcher changes, tests, risk

| Area | Files | Size |
|---|---|---|
| Reader + policy (new) | `cp0_lvgl/include/cp0_app_contract.h`, `src/cp0_app_contract.c`, `tests/test_app_contract.c`, `tests/run_tests.sh` | ~650 |
| State writer | `cp0_ui_metrics.{h,c}` (keys, format), `src/cp0/cp0_ui_state.c` (guard, `SCREEN_FB`), `cp0_lvgl_dpi_scaled.c` (pass dev/bpp), `tests/test_ui_metrics.c` (exact strings `:581-591`) | ~150 |
| Input state | `cp0_input_state.{h,c}` (keys; parse out of `src/cp0`), `input_presence.{hpp,cpp}` (mutex, `republish()`), `launcher_osk.cpp` (republish on `set_mode`), `tests/test_input_state.c` | ~200 |
| Per-launch env | new `main/ui/app_launch_env.{hpp,cpp}` + `tests/test_app_launch_env.cpp`; `native_ui.cpp:670-687`; `launch.cpp:189-206` (prefix the exec string; ownership test must hold); rewrite `screen.state` in `finish_external` and after `ExecBlocking` | ~250 |
| `X-MinSize` | `desktop_entry.{h,cpp}`, `desktop_app_loader.cpp:287-289` filter, `tests/test_desktop_entry.cpp` | ~80 |
| VFB2 | `applaunch_vfb.h`, `native_ui.cpp:554-575,624-628`, `applaunch_vfb_shim.c:79-99,159-168`; host test `pizero2w/vfb/test_vfb.sh` (shim + existing `fbtest.c`, asserts the 65536 offset) | ~200 |
| Probe sample | `tools/dev/app-contract-probe/` (C, no LVGL: report + test pattern with a 9 mm square; host and aarch64 `build.sh`; 3 `.desktop` variants compat / full / full+`X-MinSize=640x480`; README) | ~350 |
| Harness | `metrics` scene: per-launch env for both modes + new keys; `run.sh contract`: host probe in report mode on `out/<WxH>/runtime`, checked against the metrics at 5 sizes | ~80 |

The harness cannot spawn apps (it simulates `run_external`, `harness_main.cpp:904-909`); the proof is: pure env-builder
and reader unit tests, the probe run on the host against the harness's own state file, the shim + VFB2 on the host with
`fbtest`, and the boards. **Pinned deck / Pi 3A+:** stock apps only gain env vars they ignore; window, toolbar, blit,
goldens unchanged (`run.sh` byte-identical); the Store unchanged (the guard only removes its file write; `VSCREENINFO` mm
untouched); fullscreen apps only gain exported vars. **Risks:** a launcher binary copied without its shim breaks vfb
(the app draws on the real fb; the deploy tool copies both); the new `input_presence` mutex on the watcher thread; old
LanScan / Wi-Fi Survey binaries keep clobbering until rebuilt (the rewrite after exit covers it).

## 4. Docs to write
New `docs/APP-CONTRACT.md` (EN, normative: env, both files, `.desktop` keys, failure modes, stability, SDK file list,
minimal reader example; VFB2 marked internal). Update `HOSTING-APPS.md` ("Responsive apps" + `X-MinSize` beside "Full-
screen apps" `:113-120`), `launcher-project-guide/04-application-model-and-launch-mechanism.md` (EN) and its JA twin,
`APPLaunch-App-packaging-guide-ja.md` section 2.1 (only JA exists in the repo; the English guide is the apps repo README
"Host your own apps"), `checklists/app-packaging.md` item 3, `dev/DEV-GUIDE.md` (one line), `docs/cp0_lvgl.{en,md,ja.md}`
(public interface rule of cp0_lvgl `AGENTS.md`), task 014 P5 "as implemented", the `cp0_ui_metrics.h:23` comment, ROADMAP.

## 5. Tasks

| # | Task | Tier | Depends | Owns | DoD |
|---|---|---|---|---|---|
| P5-1 | Contract core: env per launch, guard + rewrite after exit, state keys, `input.state` keys + republish, reader + policy | savvy-heavy | Q2, Q3, Q8 | cp0_lvgl metrics/state/input-state/contract files, `input_presence`, `launcher_osk`, `app_launch_env`, `launch.cpp`, `native_ui.cpp` env part | unit tests green; `check-all`; goldens byte-identical; ownership test green |
| P5-2 | `X-MinSize` + VFB2 64 KiB header | savvy-medium | P5-1 (native_ui.cpp serial), Q4, Q10 | `desktop_entry*`, `desktop_app_loader.cpp`, `applaunch_vfb.h`, shim, `native_ui.cpp` vfb part | host vfb test green; tests green |
| P5-3 | Probe + harness contract check + docs | savvy-medium, then docs-writer | P5-1, P5-2 | `tools/dev/app-contract-probe/`, harness `metrics` + `run.sh contract`, docs | `run.sh contract` passes at 5 sizes; docs reviewed |
| P5-4 (opt.) | `X-Canvas=fill`, virtual touch, OSK over compat apps (F1 ph. 2) | savvy-heavy + Controller design | P5-2, Q1 | display manager, `native_ui`, shim | separate brief |

**Physical tests** (user, after P5-3; probe deployed by hand):
- *Deck 640x480:* (1) baseline before deploy: open and exit the Store, `cat /run/user/$UID/applaunch/screen.state` ->
  `w=320` (F1). (2) After: home, Settings, Calculator identical; Store, LanScan, Wi-Fi Survey in the 2x window with
  toolbar and hold-Esc. (3) After the Store exits the file says `w=640`. (4) Mesh Hop and viz1090 open and close as
  before. (5) Probe-compat fills the window; its log `compat 320x170 0,0,640,340 scale 2 standard 1130`. (6) Probe-full
  fills the screen; the 9 mm square measures 9 mm with a ruler. (7) `input.state`: BT keyboard off -> `keyboards=0`,
  `bt_gone` set; Settings On-screen keyboard Off -> `vkb=off`. (8) Probe-min shown.
- *Pi 3A+:* (9) 2-4 unchanged; (10) probe-compat 1x at (80,25), probe-full upright in 16 bpp (if Q5 yes); (11) probe-min
  hidden.
- *HackBerry:* (12) baseline: does LanScan open today (F3)? (13) after: LanScan and Wi-Fi Survey open in the 2x window;
  (14) probe-full 720x720 `square`, probe-min shown; (15) `APPLAUNCH_OSK=force` -> `vkb=force`.

**Apps repo follow-ups:** rebuild LanScan / Wi-Fi Survey against the new cp0_lvgl (F1 guard). Mesh Hop: R0 add
`X-MinSize=640x480` now (stopgap); R1 canvas from the contract and layout from tokens (remove `kScreenW/H`); R2
`APPLAUNCH_SCREEN_FB` + rotation / 16 bpp (Q5 helper) + touch mapping; R3 own OSK by `cp0_app_osk_allowed` (020 phase 3),
retire its `/proc` check; R4 drop `X-MinSize` once it works at 480x320. Apps README "320x170 picture" step: point to
`APP-CONTRACT.md`. viz1090's shim can later read `SCREEN_FB` instead of `VIZ_FB`.

## Controller decisions (2026-10-12, final; the user delegated the design to the Controller)

- **Q1** v1 as proposed. `X-Canvas=fill` (P5-4) is NOT in this P5 (a future task).
- **Q2** Publish the tokens in `screen.state` AND ship the compute code as the fallback.
- **Q3** Env names approved as proposed: `APPLAUNCH_CONTRACT`, `APPLAUNCH_APP_MODE`, `APPLAUNCH_WINDOW`,
  `APPLAUNCH_CANVAS`, `APPLAUNCH_CANVAS_SCALE`, `APPLAUNCH_STATE_DIR`, `APPLAUNCH_SCREEN_FB`; `APPLAUNCH_SCREEN_*` = the
  panel. No `APP_MODE=terminal`.
- **Q4** Short key `X-MinSize=WxH`; the tile is HIDDEN when the screen is smaller (no grey state).
- **Q5** Yes: publish `cp0_fb_output.{h,c}` as an SDK helper (documented; already unit-tested).
- **Q6** Yes: export `APPLAUNCH_SCREEN_TOUCH=<path>` (the resolved touch device) and document `APPLAUNCH_TOUCH_*`.
- **Q7** Confirmed: the app draws its own keyboard per `cp0_app_osk_allowed`; the launcher never draws over a fullscreen
  app.
- **Q8** Monotonic seconds for the new `input.state` keys.
- **Q9** SDK home = `ext_components/cp0_lvgl` (+ a copy-ready file list in the docs); do NOT bump `sdk_version.txt` (the
  contract has its own version, `APPLAUNCH_CONTRACT=1` / `v=1`).
- **Q10** VFB2 in lockstep with the launcher; no versioned `.so` name.

Order: P5-1 (contract core) now; P5-2 (`X-MinSize` + VFB2) and P5-3 (probe, harness, docs) after the Controller's review.
