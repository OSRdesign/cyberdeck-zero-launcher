# Launcher 0.5.0 - fallback on-screen keyboard tests (task 014 P2b, F1)

The user runs these on a board; the Controller deploys the build and sets the environment. Answer PASS / FAIL (+ what
you saw) per line. Background: `docs/dev/tasks/014-responsive-shell.md` section "On-screen keyboard widget (P2b, as
implemented)" and 012 F1.

Scope of this build: the keyboard exists only on **native** pages: the development page Settings > System > Keyboard
test and, since task 014 P3b, the native Wi-Fi page's password and hidden-network dialogs (section E), since P3c the
native Bluetooth pages' PIN / passkey dialogs, Alias and Rename (section G). Compat pages (the deck's default Settings
with its Wi-Fi password page, Bluetooth PIN, Store search) are unchanged: they still need a physical keyboard.

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
35. Pi 3A+ 480x320: in the password dialog (26-27) the network name shows in the header's title place while the
    keyboard is up (the Esc button and the top bar stay as they are, nothing of the page title peeks out); the field and
    "Wrong password" are readable in the dialog above the keyboard.

## F. Read-only pages (task 014 P3a)
36. System > About, Storage, Licenses; User; Date & Time > Info: key left, value right, readable at a glance; the clock in
    Info ticks; Licenses scrolls by drag and by Up / Down; Esc goes back. On the Pi 3A+ a long value (User > Password)
    goes onto a smaller line or two instead of being cut.

## G. Bluetooth pages (task 014 P3c; native Settings, also in a release build)
Needs: a phone, a Bluetooth keyboard that is NOT yet paired with this board (or forget it first, step 45), ideally an
old headset or car kit that asks for a PIN. The Controller deploys a build with the cp0_lvgl backend change of P3c (the
keyboard's passkey reaches the page: steps 41-42 cannot work without it). Do 37-44 with no physical keyboard (on-screen
keyboard), 45-48 with the Bluetooth keyboard connected.
37. Settings > Bluetooth: the Power / Discoverable / Named Only switches as before; "Alias: <name>", "Paired Devices" and
    "Scan" now open native pages (header like the home grid, Esc button, no bottom toolbar), also in a release build.
38. Paired Devices: the paired devices, each with "Connected" or "Paired"; a long name ends with dots. The status line
    shows the board's Bluetooth name and address. Drag / tap / Up / Down / Enter / Esc work as on the other pages.
39. Scan: a "Scan" switch on top (on), then the devices found, refreshed about every 1.5 s, unpaired ones with their
    signal ("-60 dBm"). With Named Only on, devices without a name (or only an address as name) are not listed; turn
    Named Only off in the section, open Scan again: they are listed.
40. Pair the phone: tap it in Scan. Its row says "Pairing..."; a dialog "Confirm Bluetooth pairing" shows the phone's
    name, address and "Pairing code: ######" with No / Yes; the phone shows the same code. Yes on the board, confirm on
    the phone: the status line says "Paired with <phone>", the list is scanned again, the highlight stays on the phone,
    which is now "Connected" or "Paired". (If the phone then asks to allow a service, a "Allow Bluetooth service?"
    dialog shows: Yes.)
41. Pair the keyboard with a passkey: Scan, tap the keyboard. A dialog "Bluetooth Passkey" shows "Type this code on the
    device, then press its Enter:" and a 6-digit code. Type it on the keyboard and press its Enter: the dialog closes by
    itself, "Paired with <keyboard>", the keyboard types in the launcher.
42. (If the keyboard is an old model) the same with a PIN shown instead of a passkey ("Bluetooth PIN Code"): same steps.
43. Pair a device that asks for a PIN (old headset, car kit; often 0000): a dialog titled with the device has a
    "Bluetooth PIN code" field and the on-screen NUMBER PAD (no buttons while it is up). The pad's Enter on an empty field says "PIN must be 1-16
    characters." in red and the dialog stays. Type the PIN, the pad's Enter: paired. Try once more on another device
    (or forget it first) and press the pad's Esc (or the header's Esc): the request is rejected, "Bluetooth action
    failed: ..." in red in the status line, the device is not paired.
44. Alias: the page shows "Name" with the current name in a field and the on-screen keyboard. Change it, Enter: back in
    the Bluetooth section, which now says "Alias: <new name>"; the phone sees the new name (refresh its Bluetooth list).
    An empty name + Enter: "Name must be 1-63 UTF-8 bytes." in red, the page stays.
45. Forget (Bluetooth keyboard connected now): Paired Devices, Enter on a device: Connect (or Disconnect), Forget,
    Rename. Forget: a dialog "Forget this device?" with its name, Cancel / Forget (the keyboard's Esc / Enter act for
    them). Forget: "Forgot <name>", the device leaves the list. It must be paired again to be used.
46. Disconnect / Connect a paired device from its actions: its row says "Disconnecting..." / "Connecting...", then
    "Paired" / "Connected", and the status line says so.
47. Rename a paired device: a dialog with its name in a field, Cancel / Save; change it, Enter: "Renamed to <name>", the
    list shows the new name (BlueZ keeps it across reboots).
48. Switch Power off in the section, open Paired Devices or Scan: a notice "Bluetooth power is off / Turn on Power
    before continuing." with OK; OK, Enter or Esc goes back to the section. Switch Power on again.
49. Esc everywhere: dialog -> device actions -> list -> section, one level per press; hold Esc 3 s from any of them:
    home, no restart. Leaving Scan while it pairs cancels the pairing (the device is not left half-paired).
50. Compat check (deck default, no `APPLAUNCH_SETTINGS_UI=native`): Bluetooth > Paired Devices / Scan / Alias look and
    work exactly as before (the legacy pages: D remove, R rescan, B reset keys, the PIN prompt with physical keys).
51. Pi 3A+ 480x320: with the number pad up the PIN dialog's title (the device) shows in the header's title place, the
    Esc button and the top bar unchanged; the field and an error stay readable in the dialog above the pad. The same for
    Rename (title "Rename" in the header).

Report anything odd; the Controller can capture the screen over ssh during the test.

## H. Apps page and sudo prompt (task 014 P3d; native Settings, also in a release build)

Set `APPLAUNCH_SETTINGS_UI=native` as for section G. Apps needs the Store backend and network.

Apps (keyboard):
52. Settings > Apps shows "Update all (N)" first when several updates exist, then the apps with versions, Sources last.
53. Enter on an app that is not installed asks for the sudo password; it installs; its row spins meanwhile.
54. U runs Update all, asking for the password once per app.
55. D on an installed app, then Enter, removes it; Esc cancels the confirm.
56. Right opens Sources; Left or Esc returns to Apps on the Sources row.
57. On Sources: A, type owner/repo, Enter adds the source; S syncs.
58. W / E / R / T never move the highlight.
Apps (touch):
59. Tap a row to highlight it, tap again to open its action list; drag scrolls; the header Esc goes back.
60. Tap an app: Install / Update / Remove (as applicable) and Cancel; Remove asks first; Update starts and the row spins.
61. Tap a source: Disable or Enable, Sync (that source only), Remove, Cancel; after a partial sync the status line is amber ("Synced 1 of 2 sources"); a long failure reason wraps.
62. No keyboard: "Add a GitHub source" opens the on-screen keyboard; its Enter submits; Cancel and OK are 9 mm targets.
Sudo prompt (any sudo action, e.g. an Apps install):
63. Physical keyboard: type, Enter: "Authenticating..."; a wrong password shows the red "Wrong password (n/3)" with an empty field.
64. Physical keyboard: Esc cancels the prompt and the page is not left in Esc state.
65. Touch, no keyboard: the on-screen keyboard appears, the password is masked, OK and Cancel are 9 mm targets.
66. The Enter that opened the prompt does not submit it.
67. Plug a keyboard in while the prompt is up: the on-screen keyboard goes away.
68. A stock app in the compat window still shows the old 240x116 box.

Report anything odd; the Controller can capture the screen over ssh during the test.

