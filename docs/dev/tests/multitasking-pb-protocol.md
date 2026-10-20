# Multitasking P-b - owner protocol (launcher core)

Phase P-b of the multitasking study ([`reports/026-multitasking-study.md`](../reports/026-multitasking-study.md);
design as built: [`tasks/015-multitasking-pb.md`](../tasks/015-multitasking-pb.md)) adds the launcher core: apps
that declare `X-Background=run|freeze` can be hidden and resumed, switched with **Alt+Tab** and **Super**. There is
no switcher screen yet (P-d): while you hold Alt, a one-line text on the home grid lists the apps. Touch gestures are
P-c. **Nothing changes unless the launcher runs with `APPLAUNCH_MULTITASK=1`.**

No real app speaks the new protocol yet (that is P-e), so the tests use the fake app `mt-fbhapp` (the P-a spike's
`fbhapp`) installed four times: **MT Freeze** (`X-Background=freeze`), **MT Run** (`run`), **MT Slow** (`freeze`,
answers too late: becomes "not responding") and **MT Legacy** (no key: behaves like today's apps). Each draws a
coloured screen with counters: `frame` (pictures drawn), `work` (one tick per 0.1 s it actually ran, hidden or not:
it stops while frozen), `keys` / `enter` (keys it acted on), `touch` / `stale` (touches; stale = typed while it was
away). **Enter changes its colour**, so a stale Enter would be visible. Esc quits it (as at an app's root). `h` asks
the launcher to hide it (the protocol's `home` message).

| Part | Board | Time |
|---|---|---|
| 0. Setup | deck | 5 min (Controller) |
| A. Today's apps, flag off and on | deck | 10 min |
| B-F. Hide, resume, Alt+Tab, not responding, legacy | deck | 15 min |
| G-J. Cap, memory, close all, Esc hold | deck | 10 min |
| K. Launcher stopped with apps hidden | deck | 3 min |
| L. Pi 3A+ DSI (800x480, RGB565) | pi3a-dsi | 15 min |
| M. Answer sheet | - | 5 min |

Rules:
- Work in an ssh terminal on the PC next to the board (`ssh osrde@zero7.local`); a second one helps for the logs.
- **The launcher log:** `grep '\[mt\]' ~/applaunch-test/run.log` (test mode) shows every step (`hide`, `ack_ms=`,
  `SIGSTOP`, `snapshot_ms=`, `restore_ms=`, `drain ... keys=N`, `SIGCONT`). **The state:**
  `cat /run/user/$(id -u)/applaunch/multitask.status`. **Dev commands:**
  `echo close-all > /run/user/$(id -u)/applaunch/multitask.ctl` (also `home`, `switch NAME`, `close NAME`, `status`,
  `memory KB`).
- **If something is stuck** (an app frozen, a picture that does not go away, no keys): first
  `echo close-all > /run/user/$(id -u)/applaunch/multitask.ctl`; then `python tools/boards/board.py test-stop deck`
  (stops the test launcher; its guardian continues and ends the apps it held, and the installed launcher starts
  again); then `pkill -CONT -x mt-fbhapp; pkill -x mt-fbhapp`; last resort `sudo reboot`.

## 0. Setup (Controller, once per board)

Files (scratchpad `pkg-out\launcher-mtb\`): the bundle `cyberdeckzero-launcher-0.8.0-pb.1-arm64.tar.gz`, `mt-fbhapp`
(aarch64, static), `mt-fbhapp-*.desktop` (also in `tools/multitasking-spike/desktop/`). Nothing is installed over
the released launcher: the bundle runs staged (`board.py test-start`) and `test-stop` brings the installed one back.

1. Upload and stage the launcher (Windows, the repository's `tools/boards`, `PIPW` set):

       python tools/boards/board.py upload deck C:/Users/osrde/AppData/Local/Temp/claude/C--CLAUDE-zero7/5aaea98f-a498-4726-8890-7bd33adbcf64/scratchpad/pkg-out/launcher-mtb/cyberdeckzero-launcher-0.8.0-pb.1-arm64.tar.gz

2. Copy `mt-fbhapp` and the four `.desktop` files to the board's home (scp or `deck.py put`), then on the board:

       sudo install -m 755 ~/mt-fbhapp /usr/share/APPLaunch/bin/mt-fbhapp
       sudo install -m 644 ~/mt-fbhapp-*.desktop /usr/share/APPLaunch/applications/

   Expected: four tiles MT Freeze, MT Run, MT Slow, MT Legacy (plain icons) once the launcher (re)starts.
3. Start the test launcher **without** the flag first (part A1): `python tools/boards/board.py test-start deck`.
   Later with it: `python tools/boards/board.py test-start deck --env APPLAUNCH_MULTITASK=1` (test-start stops the
   running one first). Check: `grep '\[mt\]' ~/applaunch-test/run.log` shows
   `multitasking P-b on (APPLAUNCH_MULTITASK=1)` and `supervisor started: cap 4, input grab+drain, guardian on`.
4. At the very end: `python tools/boards/board.py test-stop deck`, then remove the test apps:
   `sudo rm /usr/share/APPLaunch/bin/mt-fbhapp /usr/share/APPLaunch/applications/mt-fbhapp-*.desktop`.

## A. Today's apps must not change (deck, 10 minutes)

A1. **Flag off** (`test-start deck` without `--env`). `grep -c '\[mt\]' ~/applaunch-test/run.log` must print `0`.
   1. Open **System Monitor**; use it; Esc back to its root, Esc again: it quits, the grid comes back clean (no
      leftover of the app), the tap that closed it opened nothing.
   2. Open it again and **hold Esc 3 s**: it closes (as today).
   3. Open **LAN Scan** (a wrapped app in the scaled window): toolbar, keys, hold Esc 3 s to return home.
   4. Open the **CLI** terminal, type `ls`, leave with Esc.

A2. **Flag on** (`test-start deck --env APPLAUNCH_MULTITASK=1`). The same four steps, plus **Serial Console**,
   **Mesh Hop** and **viz1090** (tap Start on its intro screen): each opens, works, quits with Esc at its root, and
   hold Esc 3 s kills it. They have no `X-Background`, so **Alt+Tab and Super do nothing inside them** (the log says
   `Alt+Tab ignored: ... cannot stay in the background`). Look for: the launcher never drawing over an app (no flash
   of the grid, no "Loading..." left on top), the grid coming back clean, keys working on the grid at once.
   - With viz1090 running: `top -b -n 1 | grep -E 'cyberdeckzero|viz1090'` - the launcher should stay near 0-1 % CPU
     (it no longer blocks, but it pauses its own timers while an app is in front).

## B. Hide and resume a frozen app (deck)

1. Open **MT Freeze**. It animates (a yellow box moves at the bottom; `frame` and `work` grow).
2. Press **Super** (if the keyboard has a Windows/Meta key) or press `h` on the keyboard (the app asks to be hidden).
   Expected: the home grid at once. Log: `MT Freeze: sent "hide 1"`, `ack 1 (hide) ack_ms=0` (under 1 ms), `SIGSTOP`,
   `snapshot ... snapshot_ms=` (about 15 ms on the deck), `launcher in front`, `keyboard grab on`, `touch grab on`.
   Status file: `state=hidden-frozen ... oom=900`.
3. On the grid: press Enter three times on a tile you do not want to open? **No** - use keys that do nothing on the
   grid instead: press **Esc** twice and the arrow keys a few times, and **tap the empty status strip** twice.
4. Tap the **MT Freeze** tile. Expected: its picture is back **at once** (no "Loading..."), the box moves on from where
   it stopped, **`work` did not grow while it was hidden** (it was frozen), `keys` / `enter` / `touch` did **not**
   change and `stale` is 0 (nothing typed on the grid reached it; it did not quit from the Esc). Log: `restore ...
   restore_ms=` (about 2 ms) `verified=1`, `drain fds=... keys=0` (the grab kept its queue empty; a number above 0
   means the drain caught what got through), `SIGCONT`, `ack 2 (show)`.
5. Repeat 2-4 five times quickly. Any flash of the grid over the app, or of the app over the grid, is a failure.

## C. A running app keeps working hidden (deck)

1. Open **MT Run**, note `work`, hide it (Super or `h`), wait 10 s on the grid, tap its tile.
   Expected: **`work` grew by about 100** while it was hidden (it ran); no `SIGSTOP`/`SIGCONT` in the log;
   status while hidden: `state=hidden-running ... oom=700`; the grid was never drawn over while it was hidden.

## D. Alt+Tab and Super (deck, keyboard)

With **MT Freeze** and **MT Run** both resident (open and hide each once):
1. Open MT Run. Hold **Alt**, press **Tab** once and keep Alt down. Expected: the grid, with one line of text near the
   top: `Alt+Tab:  MT Run (in front)  [MT Freeze (paused)]  Home` (the selection in brackets). MT Run was hidden.
2. Still holding Alt: Tab moves the brackets on (`[Home]`, then back to the first), **Shift+Tab** moves back.
3. Select MT Freeze and **release Alt**: MT Freeze is in front, as it was.
4. Alt+Tab and release at once (a quick tap): you are back in MT Run (the previous app).
5. Alt+Tab, then **Esc while Alt is still held**: back to the app you started from.
6. From the **home grid**: Alt+Tab lists `Home` first; releasing on an app opens it. On the grid with no app resident,
   Alt+Tab does nothing (and a terminal page still receives Alt+Tab then).
7. **Super** tapped alone inside MT Run: back to the grid. Super inside a launcher page (Settings): nothing.
8. While the text is shown, taps on the grid do nothing.

## E. Not responding (deck)

1. Open **MT Slow**, then hide it (Super or `h`). It answers after 0.8 s, too late: after 0.3 s the launcher freezes
   it. Expected: the grid; a toast `MT Slow is not responding`; log `SIGSTOP` with no ack before it; status
   `state=not-responding`.
2. Tap its tile: it comes back (picture restored, `work` resumes). Log: `ack 1 (late)` then `ack 2 (show)`.

## F. Legacy apps with the flag on (deck)

1. **MT Legacy**: Super, `h` and Alt+Tab do nothing (log: `cannot stay in the background`); Esc quits it.
2. Hold Esc 3 s in MT Legacy: it closes; the grid returns.

## G. Cap (deck)

1. `python tools/boards/board.py test-start deck --env APPLAUNCH_MULTITASK=1 --env APPLAUNCH_MT_CAP=2`.
2. Open and hide MT Freeze, then MT Run. Open MT Slow. Expected: a toast `Closed MT Freeze (2 apps open)` (the least
   recently used **frozen** app; MT Run is kept); MT Slow opens; `pgrep -x mt-fbhapp | wc -l` counts 2.
3. `python tools/boards/board.py test-start deck --env APPLAUNCH_MULTITASK=1 --env APPLAUNCH_MT_CAP=1`. Open and hide
   MT Run, then tap MT Freeze. Expected: a toast `1 app open: close one first` (a running app is never closed for the
   cap: decision D6 wants a question, which is P-d), MT Freeze does not start, the grid works normally (keys, touch).
4. Back to the normal cap: `test-start deck --env APPLAUNCH_MULTITASK=1`.

## H. Memory floor (deck, a fake reading)

1. Hide MT Freeze and MT Run. `echo "memory 50000" > /run/user/$(id -u)/applaunch/multitask.ctl`
   Expected: toast `Closed MT Freeze: low memory (48 MB free)` (the frozen one goes first).
2. `echo "memory 20000" > .../multitask.ctl`: now MT Run closes too (`20000` is under 32 MB).
3. Real readings run every 2 s while an app is hidden: with MT Freeze and MT Run hidden and viz1090 in front,
   `free -m` and the status file's `mem_available_kb`; nothing should close above 64 MB.

## I. Close all, nothing left stopped (deck)

1. Hide MT Freeze, MT Run and MT Slow (not responding). `echo close-all > /run/user/$(id -u)/applaunch/multitask.ctl`.
   Expected: all three end within 3 s; `ps -eo stat,comm | grep mt-fbhapp` prints nothing; log shows `SIGCONT` before
   the `close` message for the frozen ones; the grid is repainted after MT Slow (a not responding app) closed.

## J. Hold Esc 3 s in a lifecycle app (deck)

1. The test apps quit on the Esc press itself (as an app at its root does), so use **viz1090** (flag on): hold Esc
   in its map view. At 3 s it is closed (log `Esc held ... force close`, then `SIGTERM`, `SIGKILL` 3 s later if it is
   still there) and the grid returns clean. Same with MT Freeze: it quits at the press; the grid must come back.

## K. The launcher stopped with apps hidden (deck)

1. Hide MT Freeze (frozen) and MT Run (running). `python tools/boards/board.py test-stop deck`.
   Expected within 4 s: `pgrep -x mt-fbhapp` prints nothing, `ps -eo stat,comm | awk '$1 ~ /T/'` prints nothing (the
   guardian process `mt-guard` continued and ended them); the installed launcher starts again.

## L. Pi 3A+ with the DSI panel (800x480, RGB565)

Setup as in 0 with `pi3a-dsi` instead of `deck` (remember the 208 MB `/tmp`: clean it first, see
`tools/boards/README.md`). Then B (snapshot about 0.75 MB: note `snapshot_ms` and `restore_ms`), C, D (with the USB
or Bluetooth keyboard), E, I and K. The touch screen there is the FT5x06: in B3 tap the grid's empty area.

## M. Answer sheet

| # | Question | Answer |
|---|---|---|
| A1 | Flag off: anything different from the released launcher? | |
| A2 | Flag on: today's apps (System Monitor, Serial Console, Mesh Hop, viz1090, LAN Scan, CLI) unchanged? launcher CPU with viz1090 in front? | |
| B | Freeze: picture back at once? `work` stopped while hidden? any stale key or touch acted on (`stale`, colour change, quit)? `ack_ms`, `snapshot_ms`, `restore_ms`, `drain ... keys=`? any flash? | |
| C | Run: `work` grew while hidden? grid ever drawn over? | |
| D | Alt+Tab / Shift+Tab / release / Esc cancel / quick tap / from the grid / Super: as described? Super key present on the M4? | |
| E | Not responding: toast, resume OK? | |
| F | Legacy: nothing happens on Super / Alt+Tab, Esc and hold Esc as today? | |
| G | Cap: the right app closed, refusal message? | |
| H | Memory: fake readings close the right apps? real MemAvailable with 2 hidden + viz1090? | |
| I | Close all: nothing left stopped? | |
| J | Hold Esc 3 s closes? | |
| K | Launcher stopped: apps gone within 4 s? | |
| L | Pi 3A+: B-E, I, K as on the deck? timings? | |
| - | Anything else odd (a stuck key after Alt+Tab, a missing repaint, a slow switch)? | |
