# Multitasking P-a - owner protocol (spike and measure)

Phase P-a of the multitasking study ([`reports/026-multitasking-study.md`](../reports/026-multitasking-study.md),
sections 7.1, 7.2, 7.4) measures three things before any launcher code is written:

1. **Touch** (risk 3, decision D2): can a **3-finger hold of 1 s** be told from a palm, a grip or two thumbs typing
   on the 2.8" deck (Goodix, 640x480) and the Pi 3A+ (FT5x06, 800x480)? How close do three fingers get?
2. **Framebuffer handoff** (risks 1 and 2): freeze an app, draw over it, give the panel back. Does the app repaint
   correctly, and does it **ignore what was typed while it was frozen** (a stale Enter)? How long do snapshot and
   restore take?
3. **Memory** (risk 4): RSS / PSS / swap of each app running and frozen, how many fit resident (viz1090 included),
   CPU of a frozen app, readsb behind a frozen viz1090, `oom_score_adj` without privilege.

The tools are in `tools/multitasking-spike/` (README there). Nothing in the launcher changes. You do the touching and
the watching; the tools write everything to `~/mta-results/` on each board.

| Part | Board | Time | Takes the screen? |
|---|---|---|---|
| A. Touch recordings | deck `zero7.local` | 12 min | no (launcher stopped, panel frozen on its last picture) |
| B. Touch recordings | Pi 3A+ `pi3a.local` | 12 min | no |
| C. Touch recordings (optional) | HackBerry `192.168.68.214` | 10 min | no |
| D. Framebuffer handoff | deck | 20 min | **yes**: test apps and colour bars |
| E. Memory | deck | 12 min | **yes**: five apps start one after the other |
| F. Handoff + memory | Pi 3A+ | 20 min | **yes** |
| G. Analyse, send back | each board | 5 min | no |

Rules for every part:
- Work in an ssh terminal on the PC (`ssh osrde@zero7.local`); the tools print what to do there.
- `systemctl --user stop cyberdeckzero` stops the launcher: the panel keeps its last picture and does nothing.
  **Always start it again** at the end of a part: `systemctl --user start cyberdeckzero` (the steps include it).
- If anything gets stuck (colour bars stay, an app frozen, the launcher gone): `sh ~/mta-spike/mta-recover.sh`
  (continues every stopped process, ends the tools, starts the launcher). Last resort: `sudo reboot`.
- Write your observations into the answer sheet at the end (one line per question).

## 0. Setup (Controller, once per board)

The Controller copies `mta-spike-aarch64.tar.gz` to the board and unpacks it into `~/mta-spike`
(`tar xzf mta-spike-aarch64.tar.gz -C ~`). Check (10 s):

    ls ~/mta-spike
    ~/mta-spike/mtrec --list

Expected: `fbhandoff fbhapp launcher-env.sh memprobe.py mta-recover.sh mta_analyse.py mta_synth.py mtrec README.md
RUN.txt`, and on the deck a line `/dev/input/event0    touch_score=7  keyboard=0 slots=5  "11-005d Goodix Capacitive
TouchScreen"` (the Pi 3A+: `slots=10 "10-0038 generic ft5x06 (79)"`). A line `(cannot open: not in group input?)`
means the user is not in group `input`: tell the Controller.

## A. Deck: touch recordings (about 12 minutes)

Have the deck in your hands as you normally hold it; the PC screen shows the instructions.

1. Stop the launcher, record the guided script, start the launcher again (one line; about 6 minutes):

       cd ~/mta-spike && systemctl --user stop cyberdeckzero; ./mtrec --guided --board deck; systemctl --user start cyberdeckzero

   It prints first `mtrec: /dev/input/event0 "11-005d Goodix Capacitive TouchScreen" mt-b, 5 slot(s), x 0..639
   y 0..479` and `panel 56.6 x 42.5 mm (screen.state), swap 1 (preset-deck)`. Then for each step: the instruction,
   `3... 2... 1... GO`, sometimes `LIFT`, then `STOP: hands off` and a `seen:` line, for example after a 3-finger
   hold: `seen: max 3 finger(s), 3 contact(s), 0 short (<30 ms), closest 18.4 mm, moved up to 0.9 mm, major 9..14`.
   Do each step as you naturally would; **do not try to help the detector**. If a step clearly went wrong (you
   were not ready), note its name; you can redo it in step 2.

   The steps: hands off; the four corners (checks the axis swap); 1-finger taps; 1-finger drag; 2-finger pinch;
   2-finger pan; **3-finger hold x5** (down together, still until LIFT at 2 s); 3 fingers as close as comfortable x3;
   3 fingers wide x2; quick 3-finger taps; 3-finger swipe up; 4 fingers x2; **palm** flat x3; side of the hand x2;
   **grip** the deck as when carrying it x3; both thumbs resting on the screen edges x2; **two thumbs typing** on the
   lower half x2; hands off. At the end a table and `mtrec: saved /home/osrde/mta-results/mtrec-zero7-<date>.csv`.

2. More repetitions of the cases that matter (about 4 minutes):

       systemctl --user stop cyberdeckzero; ./mtrec --guided=hold3,hold3close,palm,grip,gripthumbs,osk --reps 5; systemctl --user start cyberdeckzero

3. Optional, with the launcher **running** (it reacts to the touches: you may open apps; Esc brings you back), only
   to compare noise with the launcher drawing (1 minute): `./mtrec --guided=idle,hold3,palm --reps 2`.

## B. Pi 3A+: touch recordings (about 12 minutes)

The Pi 3A+ touch stopped working with the **PiSugar S Plus** fitted (2026-10-15) and is flaky with it. Check first.

1. `ssh osrde@pi3a.local`, then `cd ~/mta-spike && ./mtrec --list`: the line with `10-0038 generic ft5x06 (79)` must
   be there.
2. Touch check (15 s): `systemctl --user stop cyberdeckzero; ./mtrec --duration 15 --board pi3a; systemctl --user start cyberdeckzero`
   and tap the screen a few times. Expected at the end: `TOTAL ... contacts` above 0. If it stays 0: power off, remove
   the PiSugar, power on from the USB supply, and repeat. Write down whether the PiSugar was fitted.
3. Same as A1 and A2 with `--board pi3a` (the FT5x06 reports no contact size: the `major` columns stay `-`):

       systemctl --user stop cyberdeckzero; ./mtrec --guided --board pi3a; systemctl --user start cyberdeckzero
       systemctl --user stop cyberdeckzero; ./mtrec --guided=hold3,hold3close,palm,grip,gripthumbs,osk --reps 5; systemctl --user start cyberdeckzero

## C. HackBerry: touch recordings (optional, about 10 minutes)

The HackBerry runs its desktop; the launcher is not running there. `mtrec` only reads, but the desktop will react to
your touches (it may open things). If you prefer a quiet screen, stop the desktop first (`sudo systemctl stop lightdm`,
and `sudo systemctl start lightdm` afterwards): your call.

    ssh osrde@192.168.68.214
    cd ~/mta-spike && ./mtrec --list
    ./mtrec --guided --board hackberry

Expected: the `15-0048 EP0110M09` line; `panel 72.0 x 72.0 mm (preset-hackberry)` unless a `screen.state` exists.

## D. Deck: framebuffer handoff (about 20 minutes)

Have the Bluetooth keyboard connected (press a key to wake it). Each run below takes about 25 s: the app starts, after
**8 s** the **colour bars** with a count-down appear for 6 s (the app is frozen or hidden behind them), then the app
comes back and is watched for 3 s, then it is closed. **While the colour bars are up** do what the step says; then
watch what the app does when it comes back. Every run appends one `fbh ... rec=cycle ...` line to
`~/mta-results/fbhandoff-zero7-<date>.txt` and prints a summary line like `cycle 1: FROZEN (SIGSTOP), snapshot 9.8 ms,
restore 4.1 ms, first repaint seen, app running, pending input YES`.

1. Prepare and stop the launcher once for the whole part:

       cd ~/mta-spike && sh launcher-env.sh > ~/mta-results/launcher.env && cat ~/mta-results/launcher.env
       T=$(sed -n 's/^Exec=//p' /usr/share/APPLaunch/applications/cyberdeck-app-template.desktop); echo "$T"
       systemctl --user stop cyberdeckzero

   Expected: lines like `APPLAUNCH_DISPLAY=dpi-scaled`, `APPLAUNCH_TOUCH_SWAP_XY=1`, `LV_LINUX_KEYBOARD_DEVICE=...`,
   then the App Template's program path (`$T` in the steps below; set it again if you open a new terminal).

2. **Fake app, nothing done about input.** During the bars: press **Enter twice** and tap the screen twice.

       ./fbhandoff --env-file ~/mta-results/launcher.env --no-contract --after 8 --hold 6 --label fake-none -- ./fbhapp --tag none

   The fake app shows `FBHAPP none`, a frame counter and a moving yellow square. Expected after the bars: the app
   comes back intact, its **background colour changes twice** (it acted on the two stale Enters) and the line
   `touch 2 stale 4` turns yellow (2 taps; 4 stale events: 2 Enters + 2 taps). Results: `app_pending=1`.

3. **Fake app, input drained.** Same actions during the bars.

       ./fbhandoff --env-file ~/mta-results/launcher.env --no-contract --input drain --after 8 --hold 6 --label fake-drain -- ./fbhapp --tag drain

   Expected: **no** colour change after the bars; results `drained_enters=2`, `drained_touches=2` (or more).

4. **Fake app, input grabbed during the freeze.** Same actions.

       ./fbhandoff --env-file ~/mta-results/launcher.env --no-contract --input grab --after 8 --hold 6 --label fake-grab -- ./fbhapp --tag grab

   Expected: no colour change; results `grab=event0:ok,...`, `grabbed_enters=2`, `app_pending=0`.

5. **Fake app speaking the proposed protocol** (hide / ack / show). Same actions.

       ./fbhandoff --env-file ~/mta-results/launcher.env --after 8 --hold 6 --label fake-contract -- ./fbhapp --tag contract

   Expected: the bars say `FROZEN (ack+SIGSTOP)`; no colour change; results `hide=ack ack_ms=` a few ms, `show_ack_ms=`.

6. **The App Template** (a real `cp0_lvgl` app). During the bars press **Down, Down, Enter** (and tap a row). After
   the bars: does the picture come back complete (no bars left anywhere)? Does the highlight move and a page open
   by itself (stale keys acted on)?

       ./fbhandoff --env-file ~/mta-results/launcher.env --after 8 --hold 6 --label template-none -- "$T"
       ./fbhandoff --env-file ~/mta-results/launcher.env --input drain --after 8 --hold 6 --label template-drain -- "$T"
       ./fbhandoff --env-file ~/mta-results/launcher.env --input grab --after 8 --hold 6 --label template-grab -- "$T"

   Then once **without the snapshot restore**, touching nothing during the bars: do the bars stay on parts of the
   screen after the thaw (the app only redraws what changed)?

       ./fbhandoff --env-file ~/mta-results/launcher.env --restore none --after 8 --hold 6 --label template-norestore -- "$T"

7. **System Monitor** (redraws by itself): repaint latency. Touch nothing.

       ./fbhandoff --env-file ~/mta-results/launcher.env --after 8 --hold 6 --label monitor -- /usr/share/APPLaunch/bin/system-monitor

8. **A stale Esc.** During the bars press **Esc once** (template at its root page).

       ./fbhandoff --env-file ~/mta-results/launcher.env --after 8 --hold 6 --label template-esc -- "$T"

   Expected if stale input is acted on: the template quits right after the bars (`app=exit:0` in the results).

9. **viz1090** (optional; plug the RTL-SDR in if you have it). The intro screen comes first: tap **Start** within
   25 s; the bars come at 30 s. During the bars do a **two-finger pinch** on the map.

       ./fbhandoff --env-file ~/mta-results/launcher.env --after 30 --hold 6 --label viz1090 -- /opt/viz1090/run_viz1090.sh

   After the bars: does the map come back complete? Did it zoom by itself (stale pinch)?

10. Start the launcher again: `systemctl --user start cyberdeckzero`. Expected: the home grid comes back.

## E. Deck: memory (about 12 minutes)

Five apps start one after the other, full screen; do not touch the deck except where asked. memprobe stops the
launcher and starts it again at the end.

    cd ~/mta-spike && python3 memprobe.py --stop-launcher --env-file ~/mta-results/launcher.env \
        --apps viz1090,system-monitor,serial-console,mesh-hop,cyberdeck-app-template --resident 5 \
        --settle 10 --settle-for viz1090=30 --prompt-for "viz1090=tap Start on the viz1090 intro screen now" \
        --tcp-port 30005 --oom-test

Whenever it prints `>>> tap Start on the viz1090 intro screen now`, tap Start (twice in the run: alone, and in the
resident series). It prints one `[state] MemAvailable ... MB` line per measurement, `CPU while running, 30 s...` and
`CPU while frozen, 30 s...` per app, then a table, per-app lines like `system-monitor: RSS 14.2 MB, PSS 9.8 MB (anon
5.1) ...; CPU running 3.1 %, frozen 0.0 %`, the resident series, `fit: ... N apps ... fit`, the TCP lines of port
30005 and `oom_score_adj (initial 200): 700=ok, 900=ok, 700=ok, 200=ok, 100=refused` (or similar). Files:
`~/mta-results/memprobe-zero7-<date>.txt` and `.json`. Write down whether the RTL-SDR was plugged in. memprobe
starts the launcher again only if it stopped it: if the launcher was already stopped, run
`systemctl --user start cyberdeckzero` afterwards.

## F. Pi 3A+: handoff and memory (about 20 minutes)

The 3A+ draws through KMS fbdev emulation, 16 bits per pixel: the snapshot and restore numbers differ from the deck.

    cd ~/mta-spike && sh launcher-env.sh > ~/mta-results/launcher.env && systemctl --user stop cyberdeckzero
    ./fbhandoff --env-file ~/mta-results/launcher.env --no-contract --after 8 --hold 6 --label fake-none -- ./fbhapp --tag none
    ./fbhandoff --env-file ~/mta-results/launcher.env --no-contract --input drain --after 8 --hold 6 --label fake-drain -- ./fbhapp --tag drain
    ./fbhandoff --env-file ~/mta-results/launcher.env --after 8 --hold 6 --label monitor -- /usr/share/APPLaunch/bin/system-monitor
    ./fbhandoff --env-file ~/mta-results/launcher.env --restore none --after 8 --hold 6 --label monitor-norestore -- /usr/share/APPLaunch/bin/system-monitor
    systemctl --user start cyberdeckzero

(Enter twice and two taps during the bars of the first two, as in D2 and D3; if the touch does not work, keys only.)

    python3 memprobe.py --stop-launcher --env-file ~/mta-results/launcher.env \
        --apps viz1090,system-monitor,serial-console,mesh-hop,net-toolbox --resident 5 \
        --settle 10 --settle-for viz1090=30 --prompt-for "viz1090=tap Start on the viz1090 intro screen now" \
        --tcp-port 30005 --oom-test

## G. Analyse and send back (5 minutes per board)

On each board:

    cd ~/mta-spike && python3 mta_analyse.py ~/mta-results/mtrec-*.csv --json ~/mta-results/analyse-$(hostname).json | tee ~/mta-results/analyse-$(hostname).txt

Expected per recording: a class table (hold3, palm, grip, osk ...), `three fingers landing (hold3*): ... closest pair
min ... mm`, `G1 three-finger hold [hold=1000 ...]: hold3* hit 10/10 (1.00); false positives 0/23 labelled
segments ...` (the counts of the full guided script; whatever the numbers are, they are the result) and the sweep. `corners: axes match the swap setting` must be there for the deck; if it says `try --swap ...`, tell the
Controller.

Then tell the Controller **"P-a <board> done"**; the Controller copies `~/mta-results/` from each board (or copy it
yourself: `scp -r osrde@zero7.local:mta-results ./mta-results-deck`, same for `pi3a.local` and the HackBerry). Send
the answer sheet below with it.

## Answer sheet

Board, date, PiSugar fitted on the 3A+ (yes / no), RTL-SDR plugged in for viz1090 (yes / no).

| # | Question | Your answer |
|---|---|---|
| A1 | Any step that went wrong (name)? Did 3 fingers feel comfortable on the 2.8" screen? | |
| A2 | Could you hold 3 fingers still for 1 s easily? Did the "close together" holds feel natural? | |
| B2 | 3A+ touch check: contacts seen? PiSugar fitted? | |
| D2 | Fake app, no drain: colour changed after the bars (yes / no)? | |
| D3 | Fake app, drain: colour changed (yes / no)? | |
| D4 | Fake app, grab: colour changed (yes / no)? | |
| D5 | Fake app with the protocol: colour changed (yes / no)? | |
| D6 | Template: picture complete after the bars? Highlight moved / page opened by itself (none / drain / grab)? | |
| D6b | Template without restore: bars left on screen after the thaw (where)? | |
| D7 | System Monitor: any flicker or tearing when it came back? | |
| D8 | Stale Esc: did the template quit right after the bars? | |
| D9 | viz1090: map complete after the bars? Zoomed by itself? | |
| E | Anything odd (an app that did not start, the deck slow, swapping)? | |
| F | 3A+: same questions as D2, D3, D7 in one line | |

## What could go wrong, and recovery

| What | Effect | Recovery |
|---|---|---|
| You forget to start the launcher after a part | the panel keeps its last picture, keys and touches do nothing | `systemctl --user start cyberdeckzero` |
| The ssh connection drops during fbhandoff or memprobe | the tool gets SIGHUP and cleans up (panel restored, app ended, launcher started if the tool stopped it); if it was killed hard, its guardian process does the same within about 4 s | if the bars stay or an app stays frozen: reconnect and `sh ~/mta-spike/mta-recover.sh` |
| An app hangs (no Esc hold: the launcher is stopped) | the app keeps the panel | Ctrl-C in the ssh terminal ends fbhandoff and the app |
| A stale Esc or Enter acts in a real app during D | the template quits or opens a page | that is a result: note it; the next run starts fresh |
| Low memory during E (many apps resident) | the deck slows down, swaps to zram | wait; Ctrl-C ends memprobe and every app it started; it restarts the launcher |
| `mta-recover.sh` continues **every** stopped process of the user | a program you suspended yourself with Ctrl-Z in another terminal resumes in the background | none needed |
| Something else | | `sudo reboot` |

The tools never write outside `~/mta-results/` and `~/mta-spike/`, never need sudo, never change the launcher, its
settings or the boot files. `memprobe --oom-test` only changes the `oom_score_adj` of a `sleep` it started itself.
