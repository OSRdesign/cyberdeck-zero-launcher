# Launcher 0.5.0 multi-board tests - Pi 3A+ with the 3.5" 480x320 panel

Installed 2026-10-10 on the Pi 3A+ (host PI3A, user osrde): launcher + Store, profile `/etc/applaunch/board.conf` (pi3a-luckfox35), udev rule for the GPIO backlight, `cmdline.txt` backup `cmdline.txt.bak-applaunch`. Hold the board in landscape. Answer PASS / FAIL (+ what you saw) per line.

Already verified by the Controller (framebuffer capture over ssh, no need to repeat): service runs, home screen upright 480x320, 3x2 grid, status bar, nothing cropped.

## A. Look (eye only)
1. Home: tiles evenly spaced, labels readable, no overlap with icons, nothing cut at the edges. Stray dash at the far left of the top bar before "ZERO": visible? (known, to fix)
2. Clock, Wi-Fi bars and Bluetooth icon in the top bar readable and sharp.
3. Colours look right (orange clock pill, blue selection border).

## B. Touch accuracy (finger)
4. Tap each of the 6 tiles at its centre: the right app opens (Settings, Store, CLI, Python, SSH, IP Panel). Go back after each.
5. Tap near the 4 corners of the screen (top-left tile edge, bottom-right tile edge): the tile under the finger reacts, not a neighbour.
6. Drag the home grid up/down (if it scrolls): follows the finger direction.

## C. Stock apps and Settings (320x170 window at 1x)
7. Open Settings: window centred with black margins left/right and about 25 px top/bottom; toolbar (Esc, arrows, Enter) at the bottom, 100 px high.
8. Tap each toolbar button: Esc = back, arrows move, Enter selects. Buttons easy to hit with a thumb?
9. Inside the Settings window: tap an item, drag the list up/down. Touch lands where you tap (window offset correct).
10. Esc hold 3 s returns to home (no launcher restart).
11. Open Store and one more app (IP Panel, CLI): same window/toolbar behaviour, text readable at 1x.

## D. Settings pages
12. Settings > Brightness: shows On / Off 10 s. Choose "Off 10 s": screen goes dark, a countdown note, it comes back by itself after 10 s (and by a tap/Esc earlier).
13. Settings > Date & Time, Wi-Fi, Bluetooth open and react to touch.
14. System/About shows a "Board: pi3a-luckfox35" row.

## E. Screensaver / lock
15. Settings > DarkTime: set 10 s, wait: clock screensaver appears, full size, readable, fits (no clipping); any touch wakes it (and is swallowed).
16. Toast messages (e.g. after changing something) are readable and not cut.

## F. Calculator
17. Calculator opens, keypad fits the screen, buttons tap correctly.

## G. Persistence
18. Reboot the board: the launcher comes back by itself on the panel, upright; touch still accurate; boot text does not stay over the grid.

Report anything odd (a screenshot taken with your phone is enough). The Controller can capture the screen over ssh at any time during your tests.

## Known limits (not failures)
- Stock apps other than the Store carry their own backlight code: brightness inside them does nothing on this board.
- Speaker and audio items hidden as on the deck.
- Fully adaptive layout for other sizes comes in a later release.
