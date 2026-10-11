# Launcher 0.5.0 - fallback on-screen keyboard tests (task 014 P2b, F1)

The user runs these on a board; the Controller deploys the build and sets the environment. Answer PASS / FAIL (+ what
you saw) per line. Background: `docs/dev/tasks/014-responsive-shell.md` section "On-screen keyboard widget (P2b, as
implemented)" and 012 F1.

Scope of this build: the keyboard exists only on **native** pages: the development page Settings > System > Keyboard
test and, since task 014 P3b, the native Wi-Fi page's password and hidden-network dialogs (section E). Compat pages (the
deck's default Settings with its Wi-Fi password page, Bluetooth PIN, Store search) are unchanged: they still need a
physical keyboard.

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

## E. Wi-Fi page (task 014 P3b; native Settings, also in a release build: `APPLAUNCH_DEV=0` is fine)
Have one network you can join that the board has never joined (a phone hotspot with a password is enough) and know its
password. Do E with no physical keyboard (on-screen keyboard), then 31-33 with the Bluetooth keyboard.
23. Settings > Wi-Fi: one page: the "Wi-Fi" switch on top, then the networks. The one in use is first, bold, with a blue
    check; saved ones say "Saved"; secured ones have a small lock; each network has four signal bars like the top bar's.
    Last rows: "Join Hidden Network", "Rescan". The status line (top left) shows the network and its IP.
24. Drag the list, tap a row (it comes to the centre), tap it again (it opens). Up / Down / Enter / Esc on a keyboard do
    the same.
25. Rescan: "Scanning..." on its row for a moment, then the list again. Leave the page open 30 s: the list stays current
    and does not jump.
26. Tap the new secured network: a dialog titled with its name and a Password field; the on-screen keyboard comes up and
    the dialog sits above it (no Connect button while the keyboard is up: the keyboard's Enter is Connect, its Esc
    Cancel).
27. Type a wrong password (at least 8 characters), Enter: "Connecting...", then "Wrong password" in red; the field is
    empty and the keyboard is back. Tap the eye in the field: the text shows.
28. Type the right password, Enter: the dialog closes, a toast says "Connected to ...", the network is first with the
    check, the status line shows it and its IP.
29. Enter on the connected network: Details, Forget, Disconnect. Details shows IP, Gateway, Signal, MAC. Esc twice: back on
    the network in the list.
30. Disconnect: the row says "Disconnecting...", then the network is "Saved" and the status line "Not connected". Tap it:
    it connects again without a password. Then Forget (from its actions): it is no longer "Saved" (a tap asks for the
    password again).
31. With the Bluetooth keyboard: the same dialog shows Cancel and Connect and no on-screen keyboard; Esc cancels, Enter
    connects; the buttons also work by touch.
32. Join Hidden Network: SSID and Password fields; Up / Down / Tab move between them; Esc cancels. (Join a real hidden
    network if you have one.)
33. The "Wi-Fi" switch off: the list goes, "Wi-Fi is off"; on again: it scans and the list comes back.
34. Esc from the dialog, the list, Details: always one level back; hold Esc 3 s from any of them: home, no restart.
35. Pi 3A+ 480x320: in the password dialog (26-27) the dialog covers the header while the keyboard is up; the network
    name, the field and "Wrong password" are readable above the keyboard.

## F. Read-only pages (task 014 P3a)
36. System > About, Storage, Licenses; User; Date & Time > Info: key left, value right, readable at a glance; the clock in
    Info ticks; Licenses scrolls by drag and by Up / Down; Esc goes back. On the Pi 3A+ a long value (User > Password)
    goes onto a smaller line or two instead of being cut.

Report anything odd; the Controller can capture the screen over ssh during the test.
