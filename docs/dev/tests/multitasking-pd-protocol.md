# Multitasking P-d - owner protocol (the switcher screen, the legacy sheet, the dots, the hint)

Phase P-d ([`tasks/015-multitasking-pd.md`](../tasks/015-multitasking-pd.md)) draws the switcher "Open apps": hold
three fingers 1 s in an app (G1) or long-press the clock (G2) and you get up to 4 cards (tap one to go there, its X
closes it), Home and Close all. In an app that cannot stay in the background a sheet asks "Close <app>?". About 15
minutes on the deck, touch only (a keyboard for step 6 if you like). The launcher runs staged, with
`APPLAUNCH_MULTITASK=1`; nothing is installed over the released one.

Watch the log in an ssh terminal on the PC (it only reads it):

    L=~/applaunch-test/run.log
    t() { grep -E '\[mt\] (switcher|session|hint|notice|G1 fired)' "$L" | tail -n "${1:-8}"; }

## 0. Setup (Controller)

Files: scratchpad `pkg-out\launcher-mtd\` (`INSTALL.txt`): the bundle `cyberdeckzero-launcher-0.8.0-pd.2-arm64.tar.gz`,
`mt-fbhapp` and the five `mt-fbhapp-*.desktop` (unchanged since P-b; install them only if they are not there).

    python tools/boards/board.py upload deck <path>/cyberdeckzero-launcher-0.8.0-pd.2-arm64.tar.gz
    rm -f ~/.config/cardputerzero/multitask-hint-shown            (on the deck: the hint shows again)
    python tools/boards/board.py test-start deck --env APPLAUNCH_MULTITASK=1

Expect in the log: `[mt] switcher screen (P-d): on (640x480); first-run hint "Hold three fingers to switch apps"
(/home/<user>/.config/cardputerzero/multitask-hint-shown)`.

## 1. Two apps, three fingers (5 min)

1. Tap **MT Freeze**, hold three fingers still 1 s: the switcher shows **one card, "MT Freeze", green dot, "In front"**,
   a dashed "Room for one more" in the three other slots. Lifting the fingers must click nothing.
2. Tap **Home**: the grid, with **a small green dot** at the top right of the MT Freeze tile, and once the toast
   **"Hold three fingers to switch apps"** (it may wait for the switcher to go).
3. Tap **MT Run**, hold three fingers: **two cards**, MT Run first ("In front", green, blue border), MT Freeze
   ("Paused", grey).

Log (`t 12`):

    [mt] switcher (G1): hiding MT Freeze first
    [mt] switcher: opened by G1 with 1 app (from MT Freeze)
    [mt] hint: "Hold three fingers to switch apps" (the first app left open; shown once, remembered in ...)
    [mt] switcher: home
    [mt] switcher (G1): hiding MT Run first
    [mt] switcher: opened by G1 with 2 apps (from MT Run)

## 2. A card (1 min)

Tap the **MT Freeze** card: MT Freeze is back **at once**, as you left it (no flash of the grid, no stale tap).
Log: `[mt] switcher: card tapped MT Freeze`, then P-b's `MT Freeze: restore ...`, `drain ...`, `SIGCONT ...`.

## 3. The X and Close all (2 min)

1. Three fingers in MT Freeze: two cards. Tap the **X of MT Run**: its card goes (dashed slot), the switcher stays.
   Log: `[mt] switcher: closed MT Run`.
2. Tap **Close all**: the grid, **no dot** left. Log: `[mt] switcher: close-all (1 app)`.
3. (Optional) Open MT Freeze, three fingers, then **hold three fingers again** (or tap the header's **Esc**): back in
   MT Freeze. Log: `[mt] switcher: cancelled by G1 (back to MT Freeze)` (Esc: `cancelled (back to MT Freeze)`).
   Close it again (Close all).

## 4. An app that cannot stay in the background (3 min)

1. Tap **MT Legacy**, hold three fingers: the app's picture **dims** and a sheet asks **"Close MT Legacy?"** with
   **Keep** and **Close**. Its yellow box and `frame` count stop (frozen). Lifting the fingers clicks nothing.
2. Tap **Keep**: MT Legacy is back exactly as it was, the box moves on from where it stopped; the tap on Keep did
   nothing in it (`grep -c 'stale=1 acted=1' /tmp/mt-fbhapp-legacy.log` stays 0).
3. Three fingers again, tap **Close**: the grid comes back, MT Legacy is gone (`pgrep -f 'tag legacy'` prints nothing).
4. (Optional) The same with **MT NoEsc**, and with viz1090 (its readsb is frozen too while the sheet is up).

Log:

    [mt] switcher: sheet shown for MT Legacy (G1): it cannot stay in the background
    [mt] MT Legacy: backdrop (its last frame dimmed under the sheet) backdrop_ms=...
    [mt] switcher: sheet keep MT Legacy
    [mt] switcher: sheet close MT Legacy

## 5. The dot and the hint once (2 min)

1. Open MT Run, three fingers, **Home**: the MT Run tile has the green dot; **no** toast this time (shown once).
   Tap the tile: MT Run comes back (no new start: `frame` and `work` go on). Close it from the switcher (X or
   Close all).
2. Reset of the hint, to see it again: delete `~/.config/cardputerzero/multitask-hint-shown` on the deck, then
   `python tools/boards/board.py test-start deck --env APPLAUNCH_MULTITASK=1` again.

## 6. Alt+Tab (optional, 2 min, a keyboard)

With MT Freeze hidden and MT Run in front: hold **Alt**, press **Tab**: the switcher shows with the **blue border on
MT Freeze**; Tab again moves it to **Home**, Shift+Tab back; **release Alt**: MT Freeze is in front. Alt+Tab then
**Backspace** (or Esc) before releasing Alt: back where you were. Touch does nothing while Alt is held.
Log: P-b's `session ...` lines and `[mt] switcher: opened by Alt+Tab with 2 apps (from MT Run)`.

## Answer sheet

Per step: OK / not OK, and what you saw. Also: anything drawn wrong or cut (a name, a button), anything that reacted
to the fingers after a gesture, the screen not coming back after Keep. Send `grep '\[mt\]' ~/applaunch-test/run.log`.

Stuck: `echo close-all > /run/user/$(id -u)/applaunch/multitask.ctl`, then `python tools/boards/board.py test-stop
deck` (the installed launcher starts again).
