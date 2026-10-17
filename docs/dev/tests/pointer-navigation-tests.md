# Pointer navigation - owner tests (spike, HackBerry)

The owner runs these on the HackBerry (192.168.68.214, built-in keyboard event0, BlackBerry-style optical pad event1
"ZitaoTech HACKBerryPiQ20 Mouse"); the Controller installs the build and sets the environment. Answer PASS / FAIL (+ what
you felt or saw) per line. Design and rules: `ext_components/cp0_lvgl/include/cp0_pointer_nav.h`; app side:
`docs/APP-CONTRACT.md` section 10b.

What it does: with Settings > System > Pointer navigation **On**, pushing (sliding a finger on) the pad gives arrow keys,
faster the harder / longer you push, one axis at a time; a click on the pad is **Enter**; a click held 0.6 s without
moving is the **options key** (Ctrl+O); the wheel of a USB mouse is Up / Down. Esc / back stays on the keyboard.

Setup (Controller): install the bundle (see the build's README), start the launcher from the menu. For the log, add
`APPLAUNCH_POINTER_DEBUG=1` to `/etc/applaunch/board.conf` (a hand edit: install.sh then leaves the file alone) and
restart the launcher; read it with `journalctl -b -u cyberdeckzero-desktop.service | grep PTR`. Lines to expect at
start: `[PTR] reader ready: dead zone 3, ...`, `[PTR] pointer devices: ZitaoTech HACKBerryPiQ20 Mouse
(/dev/input/event1)`, `[PTR] pointer navigation off (input.state)` then `... (setting)`.

## A. The setting
1. Settings > System: a row "Pointer navigation" shows "Off" (just above "On-screen keyboard"). Push the pad around on
   Home: nothing moves, a click does nothing (default Off).
2. Open the row: Off / On, Off checked. Choose On with the keyboard: back in System, the row shows "On". The log says
   `[PTR] pointer navigation on (setting)` and `[PTR] reading /dev/input/event1 (...), no grab`.
3. Quit the launcher (Settings > System > Quit to desktop) and start it again: still On. In the desktop, the pad still
   moves the mouse cursor normally while the launcher is not running (nothing is grabbed).

## B. Navigation by push (Home, then a Settings list)
4. A short slow flick up / down / left / right on Home: exactly **one** step in that direction, never two.
5. Push and keep pushing: one step at once, a pause (about a quarter second), then repeated steps; push harder or
   faster: faster steps, never a blur (cap about 12 a second). Speed feel: too slow / right / too fast?
6. Stop pushing (finger still or lifted): the steps stop at once (no step after you stopped, at most one already due).
7. A diagonal push: the selection moves along one axis only (the stronger one), it does not zig-zag.
8. A push mostly up with a little sideways wobble: only Up steps, never a sideways jump.
9. Finger resting on the pad without pushing for 10 s: nothing moves.
10. In Settings, Up / Down through a long list (System), Enter into a page with a click (see C), Esc on the keyboard
    to come back.

## C. Click and hold
11. A quick click on the pad on a Home tile: the app (or Settings) opens, like Enter.
12. Click on a Settings row (e.g. Screen): it opens. Click on a choice row (DarkTime): the value is chosen.
13. Hold the click 1-2 s without moving on Home: **no** app opens (the hold is the options key, which Home does not
    use). With the debug log: `OPTIONS` at about 0.6 s, no `ENTER`.
14. Click and slide while the button is down (a drag): no click, no options; the selection moves with the slide.

## D. No accidental clicks
15. Push quickly through the Home tiles and press the pad at the end of a push, as happens when the finger stops:
    no app opens by accident (a press within 0.15 s of a push is part of the push).
16. Pause half a second after a push, then click: it opens (the guard does not eat deliberate clicks).
17. Rest the finger and click while it rests (drift): Enter, no arrow step.

## E. With the keyboard
18. Esc on the keyboard always goes back, also right after pointer steps; the 3 s Esc hold still returns Home from an
    app.
19. Keyboard arrows and pointer steps mixed: both move the same selection, no step lost or doubled.
20. A text field (Settings > Wi-Fi > Join Hidden Network, or a Bluetooth Alias): while it edits, sideways pushes do
    **not** move the caret and a hold types nothing; Up / Down moves to the next field / out of the field, a click is
    Enter (submits / stops editing, as the keyboard's Enter).
21. With the on-screen keyboard up (the HackBerry runs with `APPLAUNCH_OSK=force`): the same as 20; typing on the
    on-screen keyboard by touch still works; the pointer never types a letter.

## F. Screen saver and lock
22. Let the screen go dark (Settings > Screen > DarkTime 10 s): a push wakes it (its first step only wakes, like a
    key press; keep pushing and the following steps move the selection); a click wakes it too without opening
    anything.
23. Finger resting on the pad, no push: the screen still goes dark after DarkTime (rest noise is not activity).

## G. In an app
24. An app built before this change (the Net Toolbox installed today): pointer pushes and clicks do **nothing** in the
    app, and **nothing happens in the launcher behind it** (after quitting the app, Home is where it was, nothing was
    launched). Log: `[PTR] stopped reading /dev/input/event1 (keyboard paused: an app runs)` when the app starts,
    `[PTR] reading ...` again when it ends.
25. Net Toolbox rebuilt against this launcher's cp0_lvgl (only if the Controller installed such a test build, e.g.
    `net-toolbox_0.4.1+ptr1`): its tool list moves Up / Down with pushes, a click opens a tool, Esc on the keyboard goes
    back. Same speed as the launcher. Log of the app: `[PTR] pointer navigation on (input.state)`.
26. (Optional, Mesh Hop rebuilt the same way) hold a click on a chat row: the Chats menu opens (Ctrl+O).

## H. Other pointers (optional)
27. Plug a USB mouse: the log lists it within a second or two (`[PTR] pointer devices: ...`); moving it navigates;
    the wheel gives one Up / Down per notch; a left click is Enter. Unplug it: `[PTR] stopped reading ... (gone)`, no
    stuck key, the pad still works.

## I. Retuning without rebuilding

Every number of the rules has an environment override, read when the launcher (or an app) starts. Put the lines in
`/etc/applaunch/board.conf` on the HackBerry (or pass `--env NAME=VALUE` to `tools/boards/board.py test-start`), then
restart the launcher. Apps started by the launcher inherit them. The first log line says how many overrides were taken
and prints the values in force.

| Variable | Default | Meaning / when to change it |
|---|---|---|
| `APPLAUNCH_POINTER_NAV` | (unset) | `1` / `0` forces pointer navigation on / off whatever the setting (tests) |
| `APPLAUNCH_POINTER_DEBUG` | (unset) | `1`: log every key sent or held back and the button (`[PTR] ...`) |
| `APPLAUNCH_POINTER_DEADZONE` | `3` | counts per report under which motion is rest. Raise it if things move by themselves, lower it if a gentle push does nothing |
| `APPLAUNCH_POINTER_FULL_LEVEL` | `60` | counts per report at which the fastest rate is reached. Lower it to reach top speed with less effort |
| `APPLAUNCH_POINTER_SLOW_RATE` | `3` | steps per second just above the dead zone (decimals with a dot: `2.5`) |
| `APPLAUNCH_POINTER_MAX_RATE` | `12.5` | the cap, steps per second |
| `APPLAUNCH_POINTER_REPEAT_DELAY_MS` | `250` | pause after the first step of a push. Raise it if a flick gives two steps |
| `APPLAUNCH_POINTER_RELEASE_MS` | `50` | no report for this long = released. Raise it if steps stutter during a steady push |
| `APPLAUNCH_POINTER_HOLD_MS` | `600` | click held this long = options |
| `APPLAUNCH_POINTER_TAP_SLOP` | `24` | counts the finger may move (net) while the button is down and still click / hold |
| `APPLAUNCH_POINTER_CLICK_GUARD_MS` | `150` | a press this soon after a push is no click. Raise it if clicks happen by accident at the end of pushes, lower it if quick clicks get lost |
| `APPLAUNCH_POINTER_SMOOTH_PCT` | `50` | weight of a new report in the smoothed level (higher reacts faster, lower filters noise more) |
| `APPLAUNCH_POINTER_AXIS_SWITCH_PCT` | `200` | the other axis takes over above this % of the current one |

What the defaults give for a steady push (counts per report -> steps in 1 s / 2 s): 3-6 -> 4 / 7-8, 10 -> 5 / 9,
20 -> 6 / 12, 30 -> 7 / 15, 45 -> 9 / 20, 60 and more -> 11 / 23. The measured HackBerry pad: a slow push peaks at
37-55 counts per report but lasts 0.1-0.2 s (one step); a hard push about 1.1 s (4-5 steps).
