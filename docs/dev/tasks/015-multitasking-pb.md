# 015 - Multitasking P-b: launcher core (lifecycle, handoff, input protection)

Status: designed and built 2026-10-20 on branch `multitasking-pb` (from `main` 798381c, which has the P-a spike
tools); verified on the PC (section 12); board tests pending.
Study: [`reports/026-multitasking-study.md`](../reports/026-multitasking-study.md) (sections 1, 2.2-2.3, 4.2, 4.3,
6.4, 7.1, 7.3, 7.4; owner decisions D1-D15 of 2026-10-14). P-a tools and protocol: `tools/multitasking-spike/`,
[`tests/multitasking-pa-protocol.md`](../tests/multitasking-pa-protocol.md). Board protocol of this phase:
[`tests/multitasking-pb-protocol.md`](../tests/multitasking-pb-protocol.md). App side: `docs/APP-CONTRACT.md` section 13.

## 1. Scope

P-b builds the **launcher core** of the hybrid lifecycle (D1): apps that declare `X-Background=run|freeze` in their
`.desktop` file can be hidden and resumed, one instance per app, at most 4 resident (D6), switched with Alt+Tab and
Super (D4). It is **feature-flagged and invisible by default**.

| In P-b | Not in P-b |
|---|---|
| non-blocking fullscreen run ("display suspended" mode) | touch gestures G1 / G2 (P-c) |
| app registry, MRU, one instance, the lifecycle state machine (pure, table-tested) | the switcher grid, the dot on the tile, toasts' final wording, the first-run hint (P-d) |
| control socket + environment, `hide` / `show` / `close` / `ack`, 300 ms deadline -> not responding | `_common`, viz1090 shim, Mesh Hop lifecycle code (P-e, apps repo) |
| hide, show, freeze, thaw, close, close all; framebuffer snapshot / restore on every handoff | terminal sessions kept alive (P-f) |
| input protection: grab (primary) + drain (safety net) | per-app cgroup freezer (D10 "later") |
| `oom_score_adj` tiers; memory rule (cap 4, floors 64 / 32 MB) | the legacy Close / Keep sheet (D12, needs P-c/P-d UI) |
| Alt+Tab and Super keys; Esc rules unchanged | |
| dev entry: env flag, control FIFO, status file, log lines, a text-only stand-in for the switcher (existing toast) | |

## 2. The flag: invisible by default

`APPLAUNCH_MULTITASK=1` in the launcher's environment turns P-b on (read once at start). Anything else, or the
variable absent, keeps **every code path of today**: `Launch::launch_Exec()` takes the blocking fullscreen path
unchanged (`lv_timer_enable(false)`, `ExecBlocking`, the 3.5 s watchdog untouched), no thread, timer, key filter or
file of P-b exists. The flag also needs the Pi display backend (`native_ui::enabled()`); the stock and SDL builds
never enter P-b.

With the flag on:
- **every** `X-Fullscreen=true` app runs through the supervisor (the non-blocking path);
- only apps with `X-Background=run|freeze` can be hidden; an app without it (a *legacy* fullscreen app) behaves as
  today for the user: it owns the panel until it exits, Esc held 3 s kills it, Alt+Tab and Super do nothing in it;
- wrapped (compat, VFB) apps, the terminal and the built-in pages are unchanged (D12: no multitasking).

The owner enables it per run with `tools/boards/board.py test-start <board> --env APPLAUNCH_MULTITASK=1` (staged
launcher, nothing installed), or for the service with a user drop-in (`systemctl --user edit cyberdeckzero`,
`Environment=APPLAUNCH_MULTITASK=1`). No Settings row: a visible switch is a UI decision for P-d.

## 3. Architecture

```
launcher process
 +- UI thread (LVGL)
 |   home grid, pages; launch_Exec(fullscreen) -> multitask_ui::launch_fullscreen()  (returns at once)
 |   bridge (main-loop hook, woken by the supervisor; fallback timer): UI events -> display handoff, home, toasts
 |   key filter (launcher in front): Alt / Tab / Shift / Esc -> the Alt+Tab session (consumed while a session runs)
 |   display_handoff: enter_app_front() = pause every LVGL timer but the bridge + release the pointer
 |                    leave_app_front() = resume them + full repaint (cp0_display_resume)
 +- supervisor thread (multitask::Supervisor, no LVGL; replaces the runner loop for these apps)
 |   poll() loop: control sockets, reaping (subreaper), Esc / Alt / Tab / Super from evdev while an app is in front,
 |   the Esc-hold policy, deadlines, MemAvailable every 2 s, the dev control FIFO
 |   pure models: multitask::Model (registry + state machine + memory rule), SwitchKeys (Alt+Tab / Super session)
 |   effects through ports: processes and sockets (Linux), framebuffer (cp0_display_*), input (grab / drain), UI
 +- guardian process (forked once, "mt-guard"): if the launcher dies, SIGCONT + SIGTERM (+ SIGKILL 3 s later)
     every resident group listed in a shared page (the staged test launcher has no systemd cgroup to do it)

per resident app
 +- process group (SIGSTOP / SIGCONT / SIGTERM / SIGKILL), oom_score_adj per state
 +- control socket (SOCK_SEQPACKET), only for X-Background apps: APPLAUNCH_LIFECYCLE=1 APPLAUNCH_CONTROL_FD=<n>
 +- snapshot of its last frame (one framebuffer page; 1.2 MB on the deck, 0.75 MB at 800x480x16)
```

Files (planned; section 12 lists what was built):

| Where | What |
|---|---|
| `projects/APPLaunch/main/ui/multitask/multitask_model.{hpp,cpp}` | pure: types, config, registry, state machine, MRU, cap, memory rule, derived outputs (front, keyboard, grabs) |
| `.../multitask/switch_keys.{hpp,cpp}` | pure: Alt+Tab / Super / Esc-cancel key session |
| `.../multitask/lifecycle_protocol.{hpp,cpp}` | pure: message format and parsing, environment names |
| `.../multitask/supervisor.{hpp,cpp}` | the effect loop over ports (no LVGL; host-testable) |
| `.../multitask/supervisor_linux.{hpp,cpp}` | Linux ports: spawn with a socketpair, signals, reaping, `/proc` members, drain through `pidfd_getfd`, `oom_score_adj`, MemAvailable, the guardian, the control FIFO |
| `.../multitask/display_handoff.{hpp,cpp}` | LVGL thread: display suspended mode (in the render harness too) |
| `.../multitask/multitask_ui.{hpp,cpp}` | LVGL glue: flag, bridge timer, key filter, launch entry, launcher ports (fb, input, UI) |
| `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_dpi_scaled.c`, `include/cp0_display.h` | `cp0_display_suspend/resume` (flush becomes a no-op, under a framebuffer lock), page snapshot / restore, touch grab |
| `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_keyboard.c`, `include/keyboard_input.h` | `cp0_keyboard_set_grab()`: EVIOCGRAB on the launcher's own keyboard fds |
| `ext_components/cp0_lvgl/src/cp0/cp0_esc_key_watch.{hpp,cpp}` | `Sources::poll(listener)`: every key, not only Esc (Alt+Tab, Super) |
| `projects/APPLaunch/main/ui/desktop_entry.*`, `launcher_app.cpp`, `launch.{h,cpp}`, `desktop_app_loader.cpp` | `X-Background` parsed and carried to the launch; the flagged branch in `launch_Exec` |
| `tools/multitasking-spike/src/fbhapp.c`, `desktop/` | the fake app gains a `work` counter (runs hidden or not) and `h` = send `home`; `.desktop` files to install it as freeze / run / slow / legacy test apps |

## 4. The lifecycle state machine

```
                       launch (tile)                       resume (Alt+Tab, FIFO "switch")
  (none) ------------------------------------> FOREGROUND <---------------------------------------------+
                                                |  hide n  (Alt+Tab, Super, app "home", FIFO "home")   |
                                                v                                                       |
                                             HIDING ---- 300 ms, no "ack n" ----> SIGSTOP, snapshot      |
                                                |                                       |               |
                                       "ack n"  | X-Background=run       =freeze        v               |
                                    snapshot    +-----------------+  SIGSTOP, snapshot NOT_RESPONDING ---+
                                                v                 v                     (frozen)        |
                                         HIDDEN_RUNNING     HIDDEN_FROZEN ----------------------------->+
                                         oom 700            oom 900          restore, drain, SIGCONT, show n
  any state: group empty (exit, kill, OOM) -> GONE
  close (FIFO, cap, memory rule, close all; P-d: X on a card): SIGCONT first, "close n", SIGTERM 3 s later,
         SIGKILL 3 s after that -> CLOSING -> GONE
  Esc held 3 s on the FOREGROUND app: SIGTERM now, SIGKILL 3 s later (today's policy) -> CLOSING -> GONE
  legacy app (no X-Background): FOREGROUND -> GONE only (never hidden)
```

| From | Event | To | Actions, in order |
|---|---|---|---|
| none | launch (new) | FOREGROUND | cap check (LRU frozen app closed, or refused); display handed to the app; spawn |
| FOREGROUND | hide request | HIDING | `hide n`, deadline now + 300 ms |
| HIDING | `ack n` | HIDDEN_RUNNING (run) | snapshot, oom 700, then the pending target (launcher, other app) |
| HIDING | `ack n` | HIDDEN_FROZEN (freeze) | SIGSTOP group, wait stopped, snapshot, oom 900, pending target |
| HIDING | deadline | NOT_RESPONDING | SIGSTOP group, snapshot, oom 900, notice, pending target |
| HIDDEN_*, NOT_RESPONDING | resume | FOREGROUND | display handed to the app, ungrab, restore snapshot, drain, oom back to the start value, SIGCONT (frozen), `show n` |
| any but GONE | group empty | GONE | snapshot freed; if it was in front: launcher in front, `screen.state` republished |
| HIDDEN_FROZEN, NOT_RESPONDING | close | CLOSING | SIGCONT, `close n` (if a socket), SIGTERM at +3 s, SIGKILL at +6 s |
| HIDDEN_RUNNING, FOREGROUND | close | CLOSING | `close n` (else SIGTERM at once), SIGTERM +3 s, SIGKILL +6 s |
| FOREGROUND | Esc held 3 s | CLOSING | SIGTERM (SIGCONT with it), SIGKILL +3 s |
| any resident | launcher shutdown | CLOSING | SIGCONT + SIGTERM now, SIGKILL after 3 s |

**Pending target.** A request while an app is HIDING (a second Alt+Tab, a quick Alt release) is kept as the one
pending target (the last request wins: launcher, an app, or a launch) and runs when the hide ends (ack, deadline or
exit).

**Not responding** is resumable (the snapshot is restored, the input drained, the group continued): the P-a template
ran fine that way. P-d decides whether its card offers more than Close.

**Derived outputs.** After every event the model derives who is in front (the launcher or one app), whether the
launcher's keyboard thread runs, whether the launcher grabs keyboards and touch, and whether the supervisor reads the
keys from evdev; it emits only the changes, and always before the restore / thaw actions of a resume.

## 5. Display suspended mode (study 1.1, risk 5)

Today the UI thread blocks inside `ExecBlocking` with every LVGL timer off. The supervised path keeps the UI thread
in its loop instead:
- `cp0_display_suspend()` (any thread): under the framebuffer lock, every flush of both displays becomes
  `lv_display_flush_ready()` only, and the external blit and window fills do nothing. Once it returns the launcher
  writes no pixel. The supervisor calls it synchronously before a snapshot is restored or an app is spawned.
- `display_handoff::enter_app_front()` (UI thread): pauses every running LVGL timer except the bridge (refresh,
  input, animations, the launcher's own timers: the same quiet as `lv_timer_enable(false)`, without turning the
  main loop into a 1 ms spin), resets the input devices (a press in progress is cancelled, no click), sets
  `LVGL_RUN_FLAGE = 0`. The keyboard thread is paused by the supervisor (`keyboard_pause()`), as the runner did.
- `display_handoff::leave_app_front()` (UI thread): `cp0_display_resume()` (flushes write again; the screens and
  layers are invalidated, the panel is panned back to page 0 if an app panned), timers resumed. Then, as today after
  an app exits: home input group, touch swallowed until the finger lifts, `show_home()` (only when no built-in page
  was open: an Alt+Tab from a page comes back to that page), loading overlay hidden, full refresh,
  `LVGL_RUN_FLAGE = 1`, lock screen foreground.

The 3.5 s Esc watchdog stays armed only for the launcher's own pages, as today. The legacy blocking path is kept
verbatim for the flag off; `tests/test_external_framebuffer_ownership.py` checks both paths' ordering.

## 6. Snapshot and restore

P-a: without a restore a frozen LVGL app does **not** repaint (100 % card residue), so the launcher restores the
app's last frame on **every** handoff back, whatever the app's mode. `cp0_display_fb_snapshot()` copies the visible
page (offset from `FBIOGET_VSCREENINFO`) under the framebuffer lock; `cp0_display_fb_restore()` writes it back to the
same page, pans to it if needed and verifies it byte for byte (logged). Taken after the ack (run), after the stop is
complete (freeze, up to 100 ms of waiting for state `T`) or after the deadline stop (not responding). Measured in
P-a on the deck: snapshot about 15 ms, restore 1.5-2 ms.

## 7. Input protection: grab is primary, drain is the safety net

P-a: an unprotected frozen app acts on what was typed or touched meanwhile. Both protections below need no app
cooperation and work behind one small interface (`InputPort`):
- **GRAB (primary).** While the launcher is in front and at least one app is hidden, the launcher takes
  `EVIOCGRAB` on **its own** file descriptors: the touch screen (`cp0_display_set_touch_grab`) and the keyboards its
  libinput thread opened (`cp0_keyboard_set_grab`; new or re-opened keyboards are grabbed as they open). The
  launcher keeps reading them (home grid, pages, Alt+Tab), hidden apps get nothing. Released before any app comes to
  the front (before the thaw). During an Alt+Tab session started inside an app the keyboard grab waits until Alt is
  released (the supervisor must still see the release on its own descriptors); the touch grab starts at once.
- **DRAIN (safety net).** Before every resume (and so before every thaw) the supervisor empties the app's own evdev
  queues from outside: each `/dev/input/event*` descriptor of every process of the group is duplicated with
  `pidfd_getfd(2)` (same open file), polled and read until empty. It catches what the grab cannot: input made while
  another app was in front, and the moments around a handoff. It needs ptrace access to the app (the app is the
  launcher's descendant; the deck has no Yama; with `ptrace_scope` 2 or 3 it fails, is logged once, and the grab and
  the app's own drain remain).
- `APPLAUNCH_MT_INPUT=grab+drain` (default) `| grab | drain | none` chooses for experiments.

**Stuck modifiers (new, app side).** An app hidden by Alt+Tab saw Alt go down and never sees it go up (grabbed or
drained). The contract asks a lifecycle app to treat every key as released on `show` (or re-read `EVIOCGKEY`).

## 8. Control protocol (contract 1, additive; APP-CONTRACT section 13)

- `.desktop`: `X-Background=run|freeze` (absent or other = `none`, legacy).
- Environment, only for those apps and only from a launcher with the flag on: `APPLAUNCH_LIFECYCLE=1`,
  `APPLAUNCH_CONTROL_FD=<n>` (one end of a `SOCK_SEQPACKET` socketpair, inherited).
- Datagrams, ASCII, at most 64 bytes: launcher -> app `hide <n>`, `show <n>`, `close <n>`; app -> launcher
  `ack <n>` (after hide and after show), `home` (leave to the launcher), `switcher` (P-b: same as `home`; P-d opens the
  switcher). Unknown messages are ignored and logged.
- Deadlines: `ack` to `hide` within 300 ms or SIGSTOP + not responding; `ack` to `show` is only timed (log at 1 s);
  `close`: SIGTERM 3 s later, SIGKILL 3 s after that. A socket closed by the app makes the next hide time out.

## 9. Keys

- **Alt+Tab** (D4; Left or Right Alt). Read by the supervisor from evdev while an app is in front (as the Esc
  watcher always did), by the launcher's key filter while the launcher is in front. Alt down + Tab starts a session:
  the cycle is the resident apps in recently-used order, then Home; the selection starts on the second entry. If an
  app is in front it is hidden at once, the launcher shows the home grid and a one-line text (the existing toast)
  lists the cycle with the selection in brackets: the P-b stand-in for the switcher. Tab moves on, Shift+Tab back,
  **releasing Alt switches** to the selection, Esc with Alt held cancels (back to where the session started). A
  legacy app in front: nothing happens. In the launcher, Tab-with-Alt is consumed only while an app is resident.
- **Super** (Left / Right Meta pressed and released alone) and `KEY_HOMEPAGE` = Home: hides the app in front (a
  lifecycle app). In the launcher Super does nothing (unchanged).
- **Esc** unchanged: short Esc is the app's (D5: at the root it quits); held 3 s kills the app in front (SIGTERM, then
  SIGKILL 3 s later); in launcher pages the 3 s force-home and the 3.5 s watchdog as today.

## 10. Memory

- **Cap** 4 resident apps (FOREGROUND, HIDING, HIDDEN_*, NOT_RESPONDING). A 5th launch closes the least recently
  used frozen app (HIDDEN_FROZEN or NOT_RESPONDING) with a notice; if every hidden app is a `run` app the launch is
  refused with a notice (D6 says "ask first": that dialog is P-d).
- **Floors** while an app is hidden, MemAvailable every 2 s: under 64 MB the LRU frozen app is closed, under 32 MB
  also the LRU `run` app; one close at a time.
- **OOM order:** the app's `oom_score_adj` at spawn is its base (200 for apps of the deck's service; 0 from an ssh
  session, as P-a measured): 700 hidden running, 900 frozen, back to the base on resume (P-a: raising and lowering
  back to the start value work unprivileged, below it is refused).
- viz1090 (117 MB PSS, about 149 % CPU running) declares `run` in P-e only if its shim stops rendering hidden;
  until then it should be `freeze` or legacy (app side, P-e).

## 11. Dev entry (no new visual design)

- Flag `APPLAUNCH_MULTITASK=1`; `APPLAUNCH_MT_INPUT` as above; `APPLAUNCH_MT_CAP` (1..8, default 4) for tests.
- Log lines `[mt] ...` on stderr (journal / `run.log`): every transition with timings (`ack_ms`, `stop_ms`,
  `snapshot_ms`, `restore_ms`, `drained=`), every notice.
- Status file `$XDG_RUNTIME_DIR/applaunch/multitask.status` (debug, not part of the contract): front, residents
  with state, mode, pgid, MRU, `oom_score_adj`, last MemAvailable.
- Control FIFO `$XDG_RUNTIME_DIR/applaunch/multitask.ctl` (0600): `home`, `switch <name|pgid>`,
  `close <name|pgid>`, `close-all`, `status`, `memory <kB>` (a fake MemAvailable reading, to test the floors).
- Test apps: `tools/multitasking-spike/desktop/*.desktop` install the spike's `fbhapp` as freeze, run, slow-ack (not
  responding) and legacy fullscreen apps.

## 12. As built (2026-10-20)

Everything in sections 2-11 is built as described; the differences are listed under "Deviations" below.

### Files

| File | What changed |
|---|---|
| `projects/APPLaunch/main/ui/multitask/multitask_model.{hpp,cpp}` | new, pure: registry, state machine, MRU, cap, memory rule, oom tiers, pending target, derived outputs |
| `.../multitask/switch_keys.{hpp,cpp}` | new, pure: Alt+Tab / Shift / Esc-cancel / Super-tap session |
| `.../multitask/lifecycle_protocol.{hpp,cpp}` | new, pure: datagram format and parser, environment exports |
| `.../multitask/notice_queue.{hpp,cpp}` | new, pure (pb.2): notices one at a time, each at least 2 s |
| `.../multitask/supervisor.{hpp,cpp}` | new: the supervisor thread over ports, the Alt+Tab session controller, the dev FIFO, the status file |
| `.../multitask/supervisor_linux.cpp` | new: spawn with a socketpair, signals, `/proc` members, wait for `T`, reaping, `oom_score_adj`, MemAvailable, drain through `pidfd_getfd`, the guardian |
| `.../multitask/display_handoff.{hpp,cpp}` | new: display suspended mode on the LVGL thread (also compiled into the render harness) |
| `.../multitask/multitask_ui.{hpp,cpp}` | new: flag, launcher ports (cp0_display, keyboard grab, keyboard pause / resume, evdev keyboards with the presence watcher), bridge timer, key filter |
| `projects/APPLaunch/main/ui/launch.{h,cpp}` | `launch_Exec` takes the app's name and `X-Background`; the flagged branch to `launch_fullscreen_supervised`; `mt_app_front`, `mt_launcher_front`; start / stop in `bind_ui` / `~Launch` |
| `projects/APPLaunch/main/ui/launcher_app.cpp`, `desktop_app_loader.cpp`, `desktop_entry.{h,cpp}` | `X-Background` parsed and carried to the launch |
| `ext_components/cp0_lvgl/include/cp0_display.h`, `src/cp0/cp0_lvgl_dpi_scaled.c` | `cp0_display_suspend/resume/suspended`, `cp0_display_fb_page_bytes/snapshot/restore`, `cp0_display_set_touch_grab`; one lock around every framebuffer write |
| `ext_components/cp0_lvgl/include/keyboard_input.h`, `src/cp0/cp0_lvgl_keyboard.c` | `cp0_keyboard_set_grab`: the libinput fds are recorded in `open_restricted` |
| `ext_components/cp0_lvgl/src/cp0/cp0_esc_key_watch.{hpp,cpp}` | `Sources::poll(listener, context)`: every key event, the Esc state as before; `Sources::fds()` (pb.2) to poll them |
| `ext_components/cp0_lvgl/include/cp0_lvgl_app_runner.hpp`, `src/cp0_lvgl_app_runner.cpp` | `cp0_lvgl_set_loop_hook()` (pb.2): a function called at every turn of the main loop (inert unless set) |
| `projects/APPLaunch/tests/test_multitask_model.cpp`, `test_switch_keys.cpp`, `test_lifecycle_protocol.cpp`, `test_notice_queue.cpp` | new pure tests (ASan + UBSan) |
| `projects/APPLaunch/tests/multitask/test_supervisor_host.cpp`, `run_supervisor_host.sh` | new host process tests (ASan + UBSan) |
| `projects/APPLaunch/tests/test_desktop_entry.cpp`, `test_external_framebuffer_ownership.py`, `run_tests.sh` | `X-Background` cases; both fullscreen paths and the hand-overs; registration |
| `ext_components/cp0_lvgl/tests/test_esc_key_watch.cpp` | the key listener |
| `projects/APPLaunch/tests/render/{Makefile, src/harness_main.cpp, src/harness_launch.cpp, scenes/multitask_handoff.scene}` | `handoff` / `expect_fb` commands and the review-only scene |
| `tools/multitasking-spike/src/fbhapp.c`, `desktop/*.desktop`, `README.md` | `work` counter, `h` = `home`, `--no-hup`, no 1 ms polling while hidden; the five test apps (MT NoEsc in pb.2) |
| `docs/APP-CONTRACT.md` | `APPLAUNCH_LIFECYCLE` / `APPLAUNCH_CONTROL_FD` in section 3, `X-Background` in section 6, section 13 (draft) |
| `docs/dev/reports/026-multitasking-study.md` | sections 10 (P-a results) and 11 (P-b status) |
| `docs/dev/tests/multitasking-pb-protocol.md` | the board protocol |

### Tests (PC, WSL)

| Suite | Before (main 798381c) | After |
|---|---|---|
| `cp0-unit` (`ext_components/cp0_lvgl/tests/run_tests.sh`) | 106 test programs, PASS | 106 programs (`test_esc_key_watch` has one more case), PASS |
| `app-unit` (`projects/APPLaunch/tests/run_tests.sh`) | 53 compiled tests + 17 Python, PASS | 57 compiled tests (+ model 112 checks, keys 71, protocol 32, notice queue 29, under ASan + UBSan) + 17 Python (the ownership test rewritten) + the host process suite (106 checks, ASan + UBSan, with the loop-cost bound), PASS (pb.1: 56 tests, model 102, host 98) |
| render harness | 40 scenes, 64 goldens (32 at 640x480, 32 at 480x320), 1459 screenshots | 41 scenes (+ `multitask_handoff`, review-only), **64 goldens byte-identical**, 1469 screenshots (+10: the new scene, 2 shots at 5 sizes); `compare`, `twice`, `header`, `contract` PASS |
| `branding` | PASS | PASS |
| P-a spike tests (`tools/multitasking-spike/run_tests.sh`, not in check-all) | 20 Python OK; host e2e 37 of 38 in 3 of 3 runs on this PC | 20 Python OK; host e2e 37 of 38 (passed 38 once in 3 runs). The failing case, fbhandoff's own guardian line `ended_group=1`, is timing-dependent and fails on `main` too: when fbhandoff is killed, the kernel sends SIGHUP + SIGCONT to the newly orphaned stopped group and the fake app quits before fbhandoff's guardian looks (the group is gone either way) |
| launcher build (`tools/dev/wsl-build.sh`, aarch64) | OK | OK, no new warning |

The host process suite runs the real supervisor and Linux port with the fake app on a file-backed framebuffer and
FIFOs as input nodes: hide and ack (0-1 ms), the 300 ms deadline and not responding, freeze (state `T`) and thaw,
snapshot and verified restore, no framebuffer write while hidden (and a misbehaving app as the negative control),
2 Enters and a touch typed while frozen drained before the thaw (the app then saw nothing), `oom_score_adj` 900 / 700
and back, a `run` app kept working hidden, SIGCONT before the close message, exit and SIGKILL while hidden, Esc held
3 s, Alt+Tab from evdev (keyboard grab deferred to the Alt release), the app's `home`, `close-all`, the cap, the memory
floor, shutdown with nothing left stopped, and the guardian (both a frozen app that ignores SIGHUP and a running app
end within 4.5 s of a SIGKILL of the supervisor's process; without the guardian both survive, checked by hand). The
render scene checks the display suspended mode with the real display backend: a toast and a forced refresh write no
byte over the app's frame, the home grid comes back pixel-identical, the snapshot restores byte for byte.

### Deck findings 2026-10-20 and build 0.8.0-pb.2

The owner ran parts A-K of the protocol on the deck with build 0.8.0-pb.1 (staged launcher, flag on): all passed.
The logs showed five things, fixed in pb.2:

1. **Launcher CPU about 5 % with viz1090 in front.** Cause: the supervisor ticked every 10 ms (and rebuilt its status
   text each time), and the 20 ms bridge timer kept the LVGL loop turning 50 times a second behind the app. On the PC
   (host suite's loop-cost scenario, a legacy app in front, `-O2`): **95 wake-ups/s and 14.3 ms CPU/s** (1.4 % of an
   x86 core; the deck's A53 is several times slower). Now the supervisor sleeps in `poll()` on everything that can
   happen (requests, control sockets, the keyboards, the leaders' pidfds, the dev FIFO) and wakes on its own only for
   what is due (deadlines, the Esc hold, MemAvailable every 2 s while an app is hidden, the keyboards re-checked once
   a second); the status file and the resident list are rebuilt only after a change. The UI events reach the LVGL
   thread through a new main-loop hook (`cp0_lvgl_set_loop_hook`, woken by `cp0_lvgl_wake()`); the bridge timer is a
   fallback at 2 s behind an app (1 s on the launcher, 100 ms while notices show). After: **1.0 wake-up/s and
   0.11-0.18 ms CPU/s** with an app in front, 0.3 wake-up/s with an app hidden; the LVGL loop 0.5 wake-up/s behind an
   app (by its timer periods, not measured). The host suite asserts at most 5 wake-ups/s. The keyboard thread's own
   paused loop (`usleep(50 ms)`, 20 wake-ups/s, also on the legacy path) is unchanged.
2. **Restore 12-15 ms, drain 7-10 ms** (P-a: restore about 2 ms). The restore checked the whole 1.2 MB page by reading
   it back from the framebuffer, an uncached mapping where reads are slow (the snapshot, a read of the same size,
   takes 9-14 ms); P-a's 2 ms timed only the copy. The check now reads 9 rows (first, last and 7 between). The drain
   found the app's processes by reading the `stat` of every process (about 125 on the deck; `wait_stopped` did the same
   every millisecond): it now walks the launcher's descendants through `/proc/<pid>/task/<tid>/children`
   (`CONFIG_PROC_CHILDREN=y` on the deck, read-only probe), with the full scan only as a fallback. Expected on the
   deck: restore about 2 ms, drain 1-2 ms (not measurable on the PC: the test framebuffer is a cached file).
3. **A notice erased by the next one** (`Closed MT Freeze (2 apps open)` then at once `MT Slow is not responding`):
   `multitask::NoticeQueue` (pure, 29 checks) keeps each notice at least 2 s, shows the next one after that, hides
   the last after 3 s, and shows again a notice an app covered before it could be read.
4. **No app to test the 3 s Esc hold** (they all quit on the press): a fifth test tile, MT NoEsc (`--no-esc`), and
   part J rewritten around it.
5. **Alt+Tab steps not visible in the log**: `session: select <name>` for each Tab / Shift+Tab (the first included)
   and `session: cancelled` for Esc with Alt held; the protocol lists the lines to expect for B5, D2 and D5.

Found on the PC while measuring: a launch while the app in front was closing was refused with a misleading notice;
it now waits for that app to end (model test).

### Verified on the PC only (needs the boards)

- Real evdev: `EVIOCGRAB` on libinput's own fds and on the touch fd (and libinput's behaviour when its grabbed fds
  are suspended and resumed), `pidfd_getfd` on evdev fds of a real app (P-a did it on the deck), a Bluetooth
  keyboard sleeping while grabbed, the presence watcher feeding the supervisor's evdev reader.
- Real fbdev: the deck's `BCM2708 FB` and the Pi 3A+'s `vc4drmfb` (RGB565, possibly two pages: the pan code is
  untested on hardware); snapshot / restore timings in the launcher.
- The launcher integration itself: `launch_fullscreen_supervised`, the bridge timer and the hand-overs on the device
  (the harness covers display_handoff, not launch.cpp), Alt+Tab from a built-in page coming back to that page, the
  text overlay, the key filter, the launcher's CPU while an app is in front.
- Today's real apps on the supervised path (System Monitor, Serial Console, Mesh Hop, viz1090, the template) and
  today's paths with the flag off (wrapped apps, terminal, built-in pages): unchanged code with the flag off;
  with it on only the fullscreen path differs.
- Under systemd: the guardian's interplay with the service stop, `oom_score_adj` from the service's 200.

### Deviations from the study

1. **Everything is behind `APPLAUNCH_MULTITASK=1`**: with it off the blocking fullscreen path is kept verbatim (the
   study's "restructure the fullscreen path" happens only with the flag). With it on, legacy fullscreen apps also
   run non-blocking under the supervisor (only the hide is refused).
2. **Not responding is resumable** (study 4.2: "card offers Close only"); P-d decides what its card offers.
3. **Cap reached with only running apps hidden: the launch is refused** with a notice (D6 says "ask first": that
   question needs the P-d UI).
4. **`switcher` from an app = `home`** until P-d.
5. **Alt+Tab stand-in**: no switcher screen; the app in front is hidden at the first Tab and the home grid shows one
   line of text in the existing toast; touch is ignored while it shows.
6. **Super in launcher pages and on the grid does nothing** (K2 "Super = Home" applies to the app in front only), and
   **Alt+Tab is consumed in launcher pages only while an app is resident** (study K1: always consumed).
7. **Input protection by the launcher** (grab of its own fds + drain), in addition to the app's duty (study risk 1
   relied on the app); `APPLAUNCH_MT_INPUT` chooses for experiments.
8. **A guardian process** (not in the study) ends resident apps if the launcher dies outside systemd.
9. **`oom_score_adj` base read per app** (study assumed 200; P-a measured 0 over ssh; the service gives 200).
10. Pointer-navigation devices (mice) are not grabbed; the drain covers them.

### Decisions for the owner to confirm

1. The flag (`APPLAUNCH_MULTITASK=1`, off by default; set per run with `board.py test-start --env` or a user
   drop-in for the service; no Settings row).
2. With the flag on, legacy fullscreen apps use the non-blocking path too.
3. Not responding apps can be resumed.
4. At the cap with only `run` apps hidden: refuse the launch (until the P-d question).
5. The Alt+Tab rules: the app is hidden at the first Tab; the cycle is resident apps in MRU order then Home; the
   selection starts on the second entry (a quick Alt+Tab = the previous app); Shift+Tab back; Esc with Alt held
   cancels; both Alt keys; Tab auto-repeat ignored.
6. Super does nothing in launcher pages; Alt+Tab passes through to pages (terminals) while nothing is resident.
7. `switcher` = `home` until P-d.
8. The stand-in notice wording: "X is not responding", "Closed X (4 apps open)", "Closed X: low memory (N MB free)",
   "4 apps open: close one first", "X cannot stay in the background", "X exited" (a hidden app that ended by itself).
9. The memory rule closes hidden apps only, one per reading; the app in front is never closed by it.
10. Default input protection grab + drain.
11. The test apps stay dev tools (`tools/multitasking-spike/desktop/`), not packaged.

### Risks

- **Stuck modifiers in apps** after an Alt+Tab (the app saw Alt down, never up): app side, APP-CONTRACT section 13
  asks apps to reset key state on `show`; today's apps never get hidden, so it starts with P-e.
- **Grab and libinput**: the grab is taken on fds libinput owns; a libinput that re-opens devices on its own (a
  Bluetooth keyboard reconnecting) gets the grab in `open_restricted`. If a board shows lost keys on the home grid
  with an app hidden, `APPLAUNCH_MT_INPUT=drain` turns the grab off.
- **A not responding app that is continued to be closed may draw once** over the launcher: the launcher repaints
  after it exits.
- **Helpers outside the app's process group** are not frozen or closed (D10: cgroup freezer later).
- **viz1090's readsb link** after a freeze (P-a: dropped at about 15 s): app side.
- The snapshot of each resident app costs one framebuffer page (1.2 MB on the deck, 4.8 MB at the cap).
- Wake-ups with the flag on (pb.2): the supervisor about once a second while an app is in front (the keyboards are
  re-checked), every 2 s while an app is hidden (MemAvailable), every 500 ms with nothing resident; the LVGL loop's
  fallback timer every 2 s behind an app, 1 s on the launcher, 100 ms while notices show.
