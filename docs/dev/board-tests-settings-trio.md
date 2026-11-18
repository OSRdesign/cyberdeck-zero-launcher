# Settings > Screen: Idle dim level, Terminal text size, Screen size - owner tests (about 10 minutes, one board)

For the build with the three new rows in Settings > Screen (v0.10.5 draft, decisions 2026-11-18). One board is enough; a
board with a levels backlight (Pi 3A+ DSI, Pi 4 B, Pi 5 DSI) shows all three rows (an on/off backlight, such as the
HackBerry's, has no Idle dim level row). Answer PASS / FAIL (+ what you saw) per line.

Setup (Controller): install the bundle; note the board's screen diagonal and which size the installer was given
(`grep -E 'screen_size|panel_mm' /etc/applaunch/install.conf`) and `grep -E 'PANEL_MM|DIM_PERCENT|TERM_FONT'
/etc/applaunch/board.conf`.

## A. Idle dim level (2 minutes)
1. Settings > Screen: the rows Brightness, DarkTime, (PDA mode where offered), **Idle dim level**, **Terminal text
   size**, **Screen size**, each with a grey chevron at the right and its value. Idle dim level shows "Default (10 %)"
   (or 20 % on a 5" / 7" DSI board: the profile's level).
2. Open Idle dim level: Default (..), 5, 10, 20, 30, 50 %, the current one checked, a grey line "Brightness while the
   screen is dimmed. Applies at once."
3. Pick 50 %. Back in the list the row shows "50 %". Set DarkTime to 10S, leave Settings, wait for the clock screensaver:
   the screen dims only to half, not to the dark level. Touch a key: full brightness returns.
4. Repeat with 5 %: the screen goes much darker (on a panel that is black at low levels it may look off: say so).
5. Set it back to Default. The next screensaver dim is the profile's level again.

## B. Terminal text size (3 minutes)
6. Open Terminal text size: Auto (20 px) (14 px on a portrait screen under 480 wide), Small 14, Medium 17, Large 20, XL
   24 px. Every row shows a grey grid note ("57 x 24": columns x rows); a grey line says "Columns x rows. Applies the
   next time the terminal opens." Nothing is cut off.
7. Pick Small 14 px. Open the CLI: the text is small and `tput cols; tput lines` give the grid the note showed. Pick XL
   24 px, leave the CLI and open it again: larger text, the grid of the XL note. An SSH session opened from the launcher
   (Home > SSH) has the same size.
8. Pick Auto again: the CLI is back to the board's default size.

## C. Screen size (3 minutes)
9. Open Screen size: Auto (board profile), 2.8, 3.5, 4.3, 5, 5.5, 7, 8, 10.1 in; the grey line shows "In use: ..."
   with its source (installer / board profile) and "A change applies at next start." No Custom row (open question).
10. Pick a size different from the one in use (for example 8 in on a 5" board). The row shows "8 in"; nothing on screen
    changes yet. Restart the launcher (the Controller does it, or reboot): the
    interface density changed (larger or smaller controls, the Settings rows fit a bigger or smaller panel); the log
    line `[screen-prefs] ... panel size` names the size. Open Screen size: "In use: 8 in from the setting".
11. Pick the real size of your screen, restart: back to the proper density. (Auto (board profile) instead clears the
    setting; an installer-given size stays.)

## D. Persistence (2 minutes)
12. With Idle dim level 20 %, Terminal text size Medium and the Screen size set, **reboot** the board. After it is up,
    Settings > Screen shows the same three values; the CLI opens at Medium; the size in use says "setting".
13. Settings > Updates > install the update feature's newer release (or, if no newer release is offered, the Controller
    runs the update helper with the draft): afterwards the three values are still there (they live in
    `~/.config/cardputerzero/config.json`, which the update never touches).
14. Settings > Screen on a different layout (turn PDA mode on where the board offers it, or use the touch screen only):
    the three rows and their lists are not cut off at the edges.

Controller after the test: `grep -E 'idle_dim_percent|terminal_font_px|screen_diag|screen_mm'
~/.config/cardputerzero/config.json` should show the stored values; setting each back to Default / Auto leaves the keys
at 0 / "" (the store has no remove).
