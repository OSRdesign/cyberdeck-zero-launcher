# Launcher 0.5.0 - fallback on-screen keyboard tests (task 014 P2b, F1)

The user runs these on a board; the Controller deploys the build and sets the environment. Answer PASS / FAIL (+ what
you saw) per line. Background: `docs/dev/tasks/014-responsive-shell.md` section "On-screen keyboard widget (P2b, as
implemented)" and 012 F1.

Scope of this build: the keyboard exists only on **native** pages. Today that is the development page
Settings > System > Keyboard test, inside the native Settings. Compat pages (the deck's default Settings, Wi-Fi password,
Bluetooth PIN, Store search) are unchanged: they still need a physical keyboard.

Setup (Controller): launcher environment (board.conf or a service drop-in) `APPLAUNCH_SETTINGS_UI=native` and
`APPLAUNCH_DEV=1`. On the HackBerry (built-in keyboard) add `APPLAUNCH_OSK=force` to see the keyboard at all.
Restart the service after a change.

## A. No physical keyboard (USB unplugged; Bluetooth keyboard off for more than 10 minutes, or never connected since the launcher started)
1. Settings > System: "On-screen keyboard" shows "Auto"; "Keyboard test" is below it.
2. Open Keyboard test: the Text field is blue and the keyboard comes up at the bottom (deck: about 300 px, 5 rows: Esc Tab Ctrl + arrows, then q-p, a-l, Shift z-m Backspace, 123 , space . Enter). The field stays visible above it.
3. Type a few letters: they appear at the cursor. Keys easy to hit? Wrong neighbours?
4. Shift once: it turns light, the letters show capitals, one capital is typed, then lower case again. Shift twice quickly: Caps lock (blue) until you tap Shift again.
5. 123: digits and symbols; #+=: second symbol page (try the euro sign); abc: back to letters.
6. Hold Backspace: it repeats. The arrow keys move the cursor left / right.
7. Ctrl then c: nothing is typed (Ctrl+C goes to the page), Ctrl turns off by itself. Ctrl twice quickly: stays on (blue) until tapped again.
8. Tab: the Number field takes over, the keyboard becomes a number pad (1-9, Backspace 0 Enter). Letters cannot be typed there.
9. Tab again: Password field, letters keyboard, dots instead of characters. Tap the eye inside the field: the text shows, tap again: dots. The keyboard stays up.
10. Enter: the field stops editing (no blue border) and the keyboard goes. Tap the field: it comes back.
11. Esc on the keyboard: same as Enter. Wait 5 s: the launcher does NOT restart (no stuck Esc).
12. Hold the keyboard's Esc for 5 s while a field edits: the keyboard closes at once, no restart.
13. The header's Esc (or Esc twice): back to System, the keyboard is gone. Open Keyboard test again: it comes back.
14. Lock screen (Settings > Screen > DarkTime 10 s, then wait) while the keyboard is up: it is not shown over the lock; after unlocking it is back on the field.

## B. Presence and setting
15. With the keyboard up, plug in a USB keyboard: the on-screen keyboard goes within about half a second; typing continues on the USB keyboard in the same field. (If Shift or Ctrl was armed on screen, the USB keyboard types normal letters.)
16. Unplug the USB keyboard while a field edits: the on-screen keyboard comes back after about 2-3 s.
17. Bluetooth keyboard: wake / connect it while the on-screen keyboard is up: it goes. Let it sleep or switch it off: the on-screen keyboard stays away for 10 minutes, then comes back by itself (field still editing).
18. Settings > System > On-screen keyboard: choose Off. Keyboard test: the field edits but no keyboard appears. Set it back to Auto: the keyboard appears again. Reboot: the choice is kept.

## C. Nothing else changed
19. Without `APPLAUNCH_SETTINGS_UI=native` (the deck's default): Settings opens in the compat window as before, no "On-screen keyboard" entry, no keyboard anywhere.
20. Stock apps (Store, a Store app) and the home grid behave as before; Esc hold 3 s in a stock app still returns home.

## D. Other boards (when available)
21. Pi 3A+ 480x320: keyboard about 190 px; the field still visible above it (the header and one field row).
22. HackBerry 720x720 with `APPLAUNCH_OSK=force`: keyboard about half the screen (356 px), keys about 6.3 x 6.4 mm; the field being edited and its label stay visible above it.

Report anything odd; the Controller can capture the screen over ssh during the test.
