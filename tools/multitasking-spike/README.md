# Multitasking P-a spike tools

Small standalone programs for phase **P-a "Spike and measure"** of the multitasking study
([`docs/dev/reports/026-multitasking-study.md`](../../docs/dev/reports/026-multitasking-study.md), sections 5, 7.1,
7.2 and 7.4). Nothing here is part of the launcher, the release bundle or `check-all.sh`: the launcher is not changed.
The owner's step-by-step board protocol is
[`docs/dev/tests/multitasking-pa-protocol.md`](../../docs/dev/tests/multitasking-pa-protocol.md).

| Tool | Question it answers (study) | Changes anything? |
|---|---|---|
| `mtrec` (C) | 7.1 (1), risk 3: what the touch controller really reports for 1-4 fingers, palm, grip, two-thumb typing | no: read-only, never grabs |
| `mta_analyse.py` | 5.2, risk 3: can a 3-finger hold of 1 s (D2) be told from a palm or a grip; false positives per labelled step; finger spacing | no (reads recordings) |
| `fbhandoff` (C) + `fbhapp` (C) | 7.1 (2), 7.2, risks 1 and 2: hide / freeze, snapshot, test card, restore, thaw; does the app repaint and ignore what was typed while frozen | takes the panel; stops and restarts the launcher only with `--stop-launcher` |
| `memprobe.py` | 7.1 (3), (4), (6), risk 4: RSS / PSS / swap of each app running and frozen, 1-4 resident apps, CPU frozen vs running, readsb behind a frozen viz1090, `oom_score_adj` without privilege | starts and stops apps; the launcher only with `--stop-launcher` |
| `launcher-env.sh` | the launcher's environment for apps started over ssh (`--env-file`) | no |
| `mta-recover.sh` | panic button: continue stopped processes, end the tools, start the launcher | yes (that is its job) |

Everything writes to `~/mta-results/` by default: `mtrec-<host>-<date>.csv`, `fbhandoff-<host>-<date>.txt` (+
`-app.log`), `memprobe-<host>-<date>.txt` / `.json`.

## Build and test (PC, WSL)

```sh
sh tools/multitasking-spike/build.sh            # host + aarch64
sh tools/multitasking-spike/build.sh aarch64    # build/aarch64/{mtrec,fbhandoff,fbhapp}: static, any aarch64 glibc
sh tools/multitasking-spike/run_tests.sh        # all host tests, about 2 minutes
```

C11, POSIX and Linux headers only (the 5x7 font is `tools/dev/app-contract-probe/font5x7.h`); the aarch64 binaries
are linked statically with the WSL cross toolchain (`aarch64-linux-gnu-gcc` 13, glibc 2.39), so the boards' glibc
does not matter. The Python tools need Python 3.9+ standard library only (the boards have 3.13).

What the host tests cover (`run_tests.sh`): the multitouch tracker (`tests/test_mt_track.c`: slots, ghosts, swapped
axes in mm, travel, slot reuse, protocol A, single touch, `SYN_DROPPED`); the analyser and the G1 recogniser on
synthetic recordings (`tests/test_analyse.py`: every case of study section 5.3 on both axis layouts, per-step false
positives, the sweep, the corner check); `memprobe` (`tests/test_memprobe.py`: parsers, a full run with stand-in
apps, SIGTERM and SIGKILL of memprobe leave nothing stopped, the launcher refusal and `--stop-launcher` with a fake
`systemctl`); end to end (`tests/host_e2e.sh`): `fbhandoff` + `fbhapp` on a file-backed framebuffer with a FIFO as the
input node (legacy freeze, stale Enters seen, drain through `pidfd_getfd`, the lifecycle contract, a slow ack, an app
drawing while hidden, restore vs no restore, SIGTERM and SIGKILL safety, the refusal and `--stop-launcher`), `mtrec
--raw` against the analyser and the synthetic truth, `mtrec --script` over a FIFO.

**Not testable on the PC** (WSL has no evdev and no uinput: `CONFIG_INPUT_EVDEV` is off): real evdev ioctls
(`EVIOCGABS`, `EVIOCSCLOCKID`, `EVIOCGRAB`), the capability scoring of real devices, a real fbdev (`FBIOGET_*`, the
deck's `BCM2708 FB` and the 3A+'s `vc4drmfb` speed), `systemctl --user` itself, and of course real fingers. Those are
what the board protocol is for.

## mtrec

```sh
./mtrec --list                        # input devices, touch score, slots
./mtrec --guided --board deck         # the guided script (about 6 minutes), saves ~/mta-results/mtrec-*.csv
./mtrec --guided=hold3,palm,grip --reps 5
./mtrec --guided --countdown 3         # shorter count-down before each step (1..10 s, default 5)
./mtrec --duration 30                 # free recording, live finger count
./mtrec --steps                       # the default steps
```

- Picks the touch screen the way the launcher does (`cp0_lvgl_dpi_scaled.c` `touch_score`: direct or BTN_TOUCH,
  multitouch preferred, virtual devices skipped), or `--dev`. Opens it read-only, **never `EVIOCGRAB`**: the launcher
  or an app can run at the same time (they then also act on the touches, so the protocol stops the launcher).
- Protocol B slots, protocol A (counts only) and single-touch devices. Records every event with its kernel time
  (`EVIOCSCLOCKID(CLOCK_MONOTONIC)` on its own fd) as `ev,t_us,type,code,value,name`; guided steps add
  `seg,t_us,begin|end,<step>.<rep>,<instruction>` lines. `#` lines hold the device, its axes, the mode, the slot
  count, the panel size and axis swap (and where they came from), and at the end the statistics.
- Millimetres: each raw axis is normalised by its range, as the launcher's touch path does, then scaled by the panel
  size in display orientation; on the deck the raw X axis runs along the display's height (`APPLAUNCH_TOUCH_SWAP_XY=1`
  in the launcher's unit). Sources, first found wins: `--swap` / `--panel-mm`, the environment, the running
  launcher's environment, `/etc/applaunch/board.conf`, `screen.state` (`w`, `h`, `ppmm`), the `--board` preset. The
  analyser can override both afterwards: the raw events are kept.
- Statistics, per step and in total: most fingers at once, contacts, short-lived contacts (< 30 ms), closest pair
  (mm), largest move from the landing point (mm), `TOUCH_MAJOR` range (Goodix only; the FT5x06 reports none). One
  `seen:` line after each guided step lets the user redo a step that went wrong.
- Guided steps start after a count-down, `5... 4... 3... 2... 1... GO` by default (`--countdown N`, 1..10 s); the
  step's duration and its LIFT time count from GO.
- Guided script (`--steps`): idle, corners (checks the axis swap), tap1, drag1, pinch2, pan2, hold3 x5, hold3close
  x3, hold3wide x2, tap3, swipe3, four x2, palm x3, palmside x2, grip x3, gripthumbs x2, osk x2 (two thumbs on the
  lower half), idle_end. `--script FILE` takes `id|seconds|reps|lift_at|instruction` lines; `--wait-enter` pauses
  before each step.
- `--raw --dev FILE --abs X0,X1,Y0,Y1,SLOTS`: a file or FIFO of raw `struct input_event` (host tests).

## mta_analyse.py

```sh
python3 mta_analyse.py ~/mta-results/mtrec-zero7-*.csv --json ~/mta-results/analyse-zero7.json
python3 mta_analyse.py REC.csv --t-hold 700 --major-max 40 --sweep grid
```

Per recording: the per-class table (reps, most fingers, contacts, short contacts, closest pair, p95 move,
`TOUCH_MAJOR` p50 / p95 / max), the spacing of three fingers when the third lands (`hold3*` steps), the corner check
(warns when the axis swap looks wrong), and the **G1 recogniser exactly as study section 5.2 specifies it** with the
owner's D2 hold of 1000 ms and the study's thresholds (land 250 ms, still 4 mm, separation 8 mm, edge 2 mm, major
off): hits on `hold3*` steps, **false positives per labelled step** for every other class, fires between steps, and
why missed holds were rejected (`land`, `sep`, `edge`, `major`, `move`, `lift`, `extra`). Then a sweep, one parameter
at a time around that line (`--sweep grid` for all combinations, `none` to skip); the major thresholds tried come from
the finger data. The `G1` class is meant as the reference for the C recogniser of phase P-c. `mta_synth.py` makes
synthetic recordings (`python3 mta_synth.py demo --csv demo.csv`).

## fbhandoff and fbhapp

```sh
# one run, fbhandoff stops the launcher and starts it again at the end
./fbhandoff --stop-launcher --no-contract --after 6 --hold 6 --label fake-legacy -- ./fbhapp --tag legacy
# several runs: stop the launcher once yourself, give the apps its environment, start it again at the end
sh launcher-env.sh > ~/mta-results/launcher.env && systemctl --user stop cyberdeckzero
./fbhandoff --env-file ~/mta-results/launcher.env --after 8 --input drain -- /usr/share/APPLaunch/bin/system-monitor
systemctl --user start cyberdeckzero
```

The cycle, on a timer (`--after`, `--every`, `--cycles`) or `kill -USR1 <fbhandoff pid>`:

1. **hide**: the app gets `hide N` on the lifecycle socket proposed in study section 4.2 (`SOCK_SEQPACKET`; the child
   has `APPLAUNCH_LIFECYCLE=1` and `APPLAUNCH_CONTROL_FD=<fd>`; `--no-contract` offers none). `ack_ms` is the answer
   time; no `ack N` within `--ack-ms` (300) -> `SIGSTOP` of the whole process group (every app today). After an ack:
   `SIGSTOP` (`--background freeze`, default) or nothing (`run`: the app stays hidden but running).
2. **grab** (`--input grab|grab-drain`, study risk 1): `EVIOCGRAB` on every input node the app's group has open (read
   from `/proc/<pid>/fd`), counting what arrives; released before the thaw.
3. **snapshot** of the visible page (`FBIOGET_VSCREENINFO` / `FSCREENINFO`, `yoffset`, stride), timed.
4. **test card**: colour bars and a count-down, redrawn every 100 ms; any byte changed by someone else in between
   counts as a `foreign_write` (an app drawing while it should not).
5. **the app's own input queues**: each evdev fd of the group is duplicated with `pidfd_getfd(2)` (Linux 5.6+, same
   user, the deck has no Yama), which is the **same open file**, so `poll` tells whether events are waiting for the
   app (`app_pending`, non-destructive) and, with `--input drain|grab-drain`, reading it empties the app's queue
   (`drained_*`: events, key presses, Enters, Escs, touches, `SYN_DROPPED`).
6. **restore** the snapshot (timed, verified byte for byte; `--restore none` leaves the card to see whether the app
   repaints by itself), `SIGCONT`, `show N` and its ack.
7. **watch** the panel for `--watch-ms`: `first_change_ms` (repaint latency after the thaw), `last_change_ms`,
   `changed_px_pct` against the snapshot, `card_residue_pct` (how much of the card is still there).

One `fbh rec=cycle ...` line per cycle (key=value) on stdout and in the results file; the app's own stdout and stderr
go to `<results>-app.log`. Questions answered per cycle: `app_pending=1` means the app will read what was typed during
the freeze; with a real app the owner watches whether it acts on it (a stale Enter opens something, a stale Esc quits
it: `app=exit:0`). The real numbers for the deck's 640x480x32 panel: `snapshot_ms`, `restore_ms`, `stop_ms`,
`first_change_ms`.

`fbhapp` is a fake fullscreen app for the same runs: it draws the whole panel at 20 fps (`--static`: only when
something changes, like an LVGL app), reads the touch screen and every keyboard itself without a grab, quits on Esc,
changes its colour on every Enter (a stale Enter is visible), and logs every key and touch it acts on with how long it
waited in the kernel queue (`stale=1` above 150 ms). It speaks the proposed protocol (`hide` -> stop drawing, ignore
input, `ack`; `show` -> drop pending input, repaint, `ack`; `close` -> exit) and can misbehave on purpose:
`--ack-delay-ms`, `--ignore-hide`, `--draw-hidden`, `--no-drain`, `--no-contract`.

Since P-b (task 015) `fbhapp` also shows `work`, one tick per 100 ms the process actually runs, hidden or not (no
catch-up after a freeze: a `run` app's count grows while hidden, a frozen one's does not), and sends the protocol's
`home` when `h` is pressed. A hidden app no longer polls every millisecond. `--no-hup` ignores SIGHUP (the
launcher's guardian test: the kernel sends SIGHUP to a stopped process group left orphaned). The launcher's host process tests
(`projects/APPLaunch/tests/multitask/run_supervisor_host.sh`) build and drive it.

**Test apps for the launcher (P-b).** `desktop/` installs `fbhapp` (aarch64 build, copied as
`/usr/share/APPLaunch/bin/mt-fbhapp`) five times: `mt-fbhapp-freeze.desktop` (`X-Background=freeze`),
`mt-fbhapp-run.desktop` (`run`), `mt-fbhapp-slow.desktop` (`freeze`, `--ack-delay-ms 800`: answers too late, so the
launcher marks it not responding), `mt-fbhapp-legacy.desktop` (no key: today's behaviour) and
`mt-fbhapp-noesc.desktop` (no key, `--no-esc`: Esc does nothing in it, for the launcher's hold-Esc test). Each logs to
`/tmp/mt-fbhapp-<tag>.log`. They only stay in the background under a launcher started with `APPLAUNCH_MULTITASK=1`;
the board protocol is `docs/dev/tests/multitasking-pb-protocol.md`.

**Safety.** It refuses to start while the launcher (or another process) has the framebuffer open, exit status 3,
unless `--force` (both then draw) or `--stop-launcher` (`systemctl --user stop cyberdeckzero`, and `start` again at
the end). Ctrl-C, SIGTERM and SIGHUP (the ssh session closed) end a cycle cleanly: the snapshot goes back, grabs are
released, the group gets `SIGCONT`, `close`, `SIGTERM`, then `SIGKILL` 3 s later, the launcher is started again if
fbhandoff stopped it. A **guardian** process (its own session, ignores signals) does the same if fbhandoff dies
without cleaning up, even by SIGKILL, and appends `fbh rec=guardian ...` to the results. The app runs with stdin from
`/dev/null` (a background process group reading the terminal would stop). With the launcher stopped and no
`--env-file`, the app still finds the screen through `screen.state` (it stays in `$XDG_RUNTIME_DIR/applaunch/`), but
not the deck's touch axes: use `launcher-env.sh` first, or let fbhandoff copy `APPLAUNCH_*` / `LV_*` from the running
launcher (default, before `--stop-launcher` stops it).

## memprobe.py

```sh
python3 memprobe.py --stop-launcher --apps cyberdeck-app-template,system-monitor,serial-console,mesh-hop,viz1090 \
    --settle 10 --settle-for viz1090=40 --prompt-for "viz1090=tap Start on the intro screen" \
    --tcp-port 30005 --oom-test --env-file ~/mta-results/launcher.env
```

For each app alone: started in its own process group (`Exec=` of `/usr/share/APPLaunch/applications/<id>.desktop`,
or `NAME=COMMAND`), measured running after `--settle`, CPU over `--cpu-window` (30 s), frozen with `SIGSTOP`, CPU
again, thawed (still alive?), closed. Then the resident series (`--resident`, 4): app 1 started and frozen, app 2
started, ... with MemAvailable after each start and the fit estimate (apps of the measured mean cost above the 64 MB
floor of D6). Per state: MemAvailable, swap used, zram original / compressed, load, and per process RSS, PSS,
PSS_anon, PSS_file, swap (`smaps_rollup`). `--tcp-port 30005` logs the queues of viz1090's connection to readsb in
every phase (a growing `tx_queue` on readsb's side while viz1090 is frozen; still connected after the thaw?).
`--oom-test` raises a child's `oom_score_adj` to 700 and 900 and lowers it back to its start value and below.

"Hidden but running" does not exist yet (no app implements `hide`): the running CPU figure is the foreground app
drawing, an upper bound; frozen should be 0. Nothing is left stopped: every exit path sends `SIGCONT` then `SIGTERM`
(then `SIGKILL`), and a guardian process does it if memprobe itself is killed.
