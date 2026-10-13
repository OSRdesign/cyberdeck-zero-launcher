# Launcher 0.5.0 - fallback on-screen keyboard tests (task 014 P2b, F1)

The user runs these on a board; the Controller deploys the build and sets the environment. Answer PASS / FAIL (+ what
you saw) per line. Background: `docs/dev/tasks/014-responsive-shell.md` section "On-screen keyboard widget (P2b, as
implemented)" and 012 F1.

Scope of this build: the keyboard exists only on **native** pages: the development page Settings > System > Keyboard
test and, since task 014 P3b, the native Wi-Fi page's password and hidden-network dialogs (section E), since P3c the
native Bluetooth pages' PIN / passkey dialogs, Alias and Rename (section G). Compat pages (Store search, stock apps) are unchanged: they
still need a physical keyboard. Since task 014 P3e the Settings are native everywhere the dpi-scaled backend exists
(section I); sections A to H were written while the compat Settings was the default.

Setup (Controller): the Keyboard test page needs a development build or `APPLAUNCH_DEV=1` in the launcher environment
(board.conf or a service drop-in); the native Settings needs no setting (`APPLAUNCH_SETTINGS_UI` no longer exists). On the HackBerry (built-in keyboard) add `APPLAUNCH_OSK=force` to see the keyboard at all.
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
19. (Obsolete since P3e: there is no compat Settings on the Pi boards any more; the native Settings is checked in section I.)
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
50. (Obsolete since P3e: the legacy Bluetooth pages are no longer reachable on the Pi boards; the native pages are
    steps 37-49.)
51. Pi 3A+ 480x320: with the number pad up the PIN dialog's title (the device) shows in the header's title place, the
    Esc button and the top bar unchanged; the field and an error stay readable in the dialog above the pad. The same for
    Rename (title "Rename" in the header).

Report anything odd; the Controller can capture the screen over ssh during the test.

## H. Apps page and sudo prompt (task 014 P3d; native Settings, also in a release build)

Apps needs the Store backend and network (the native Settings needs no setting since P3e).

Apps (keyboard):
52. Settings > Apps shows "Installed (N)", "Available (M)", "Sources" (P3e split); Installed shows "Update all (N)" first when several updates exist, then the installed apps with versions (an update amber); Available lists the apps not installed with their version.
53. On Available, Enter on an app asks for the sudo password; it installs; its row spins meanwhile; then it leaves Available and the counts change ("Installed (N+1)", "Available (M-1)").
54. On Installed, U runs Update all, asking for the password once per app.
55. On Installed, D on an app, then Enter, removes it; Esc cancels the confirm. Enter on an app opens its actions (Update when offered, Remove, Cancel).
56. Right or Enter on the Apps root opens the highlighted level; Left or Esc returns to the Apps root on that row (title "Apps > Installed" etc.), then to Settings.
57. On Sources: A, type owner/repo, Enter adds the source; S syncs.
58. W / E / R / T never move the highlight.
Apps (touch):
59. Tap a row to highlight it, tap again to open it (a level, an installed app's action list); drag scrolls; the header Esc goes back one level.
60. Tap an installed app: Update (when offered), Remove, Cancel; Remove asks first; Update starts and the row spins. Tap an available app: it installs at once (no list).
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

## I. Native Settings by default on the deck (task 014 P3e)

The flip: Settings is native on every Pi board with no switch and no compat path. Build: a release bundle from
`pizero2w/build.sh` (development-only pages hidden, About / System "Channel" reads `release`). Needs the deck for 1 to 11
and the Pi 3A+ for 12; a BT keyboard for 2, 4, 5 and 8. Item 81 needs a bundle built with `--with-store` and network,
item 82 any Pi board.

69. Settings opens full screen, no toolbar, the header's Esc works, the status strip matches the home grid.
70. Every section by touch only, then by the BT keyboard only.
71. Brightness slider by drag and by Left / Right; the level survives a restart. DarkTime 10 s locks the screen.
72. Wi-Fi: Details; Forget, then rejoin with the password. With no keyboard present the on-screen keyboard appears.
73. Bluetooth: Power; Discoverable; the keyboard shows Connected; Rename.
74. A Launcher toggle hides a tile; a Touch choice still applies in Snake or Tank.
75. Apps: the root shows Installed (N) / Available (M) / Sources; Available loads; install with the sudo dialog, and cancel;
    Installed: an app's actions (Update / Remove / Cancel).
76. Date & Time: Network Time off; Set Manually: change the minute and save; year 2000 shows the warning (Cancel, then
    Confirm); Network Time back on.
77. System: Storage; Licenses scrolls; On-screen keyboard Auto / Off; Reboot then No; Shutdown then No.
78. Hold Esc on a deep page goes home; the lock screen works while in Settings.
79. A stock app opened after Settings keeps its compat window, toolbar and touch.
80. Pi 3A+: Brightness On / Off 10 s and the About Board row; the Keyboard test page is not shown (release build).
81. Fresh install lists our apps under Available: on a user with no app source (no
    `~/.local/share/cardputerzero-appstore/registries.json`, or the Store's hub only), `sudo ./install.sh` prints
    "default app source: added OSRdesign/cyberdeck-zero-apps" and "synced: N apps"; Settings > Apps > Available lists
    LAN Scan, viz1090, Wi-Fi Survey, Mesh Hop... (minus the installed ones) without typing anything; Sources shows
    OSRdesign/cyberdeck-zero-apps. Remove it in Sources, run `install.sh` again: it is not added back ("already decided").
82. Launcher > Store hides / shows the Store tile: Settings > Launcher lists Store (on); switch it off: the Store tile
    leaves the home grid; it stays hidden after a restart of the launcher and after an app install from Settings > Apps;
    Settings > Apps still works; switch it on: the tile is back. By touch and by keyboard.

Report anything odd; the Controller can capture the screen over ssh during the test.

## J. Overlays, Calculator, clock and SSH form (task 014 P4)

Overlays (deck 640x480, Pi 3A+ 480x320, HackBerry 720x720):
83. Connect to Wi-Fi in Settings: the toast is readable and is not full width; a wrong password shows the toast over the header without clipping.
84. Hold Esc in Settings, then in the Calculator: the ribbon shows and returns home at 3 s.
85. Hold Esc over a stock app: the ribbon sits at the top of the app window and the app repaints cleanly afterwards.
86. Brightness and volume keys at home, in Settings, over a stock app: the OSD is centred and readable (bigger than before on the deck and the Pi 3A+: expected), and disappears after about 1 s; mute shows too.
87. Launch a stock app and a native page: "Loading..." is readable and centred, then vanishes.
Calculator and clock:
88. Deck: the Calculator looks exactly as before; typing, `=`, an error from 1/0, hold-Esc 3 s to home work. After DarkTime the clock is the same size and place as before; touch or a key wakes it.
89. Pi 3A+: the Calculator and the clock are unchanged (digits clear the key rows).
90. HackBerry: the "Calculator" title sits where it does in Settings with the Esc button and status strip; the header Esc leaves; a long expression and an 8+ digit result never touch the strip or the first key row; keys are comfortable by touch; the clock is about 12 % larger and centred and wakes on touch.
SSH form:
91. Deck + BT keyboard: the form fits without scrolling and no on-screen keyboard shows; type a host, Down/Tab to Port and User, Enter on User connects; leaving the terminal returns to the native form.
92. Clear Host and Connect: red "Host is required", no terminal. Port 0 shows "Port must be 1-65535"; an empty Port or User connects with ssh defaults.
93. F1 opens help, Esc returns; the header Esc goes home.
94. Pi 3A+ (touch): the keyboard appears on Host and the Connect/Help row hides; tap Port: only the number pad, digits only; Esc brings the buttons back; after a "Host is required" error, tapping Host shows the error in red under the field above the keyboard.
95. Pi 3A+: Connect, use the session, exit; the saved values are back after a relaunch.
96. HackBerry: layout, keyboard as configured, Connect and Help reachable.

Report anything odd; the Controller can capture the screen over ssh during the test.

## K. Terminal on-screen keyboard (task 014 P4b)

Pi 3A+ (touch only, no keyboard connected):
97. Open CLI: the on-screen keyboard appears, the prompt is on the last line above it, and `stty size` prints `5 40`.
98. Type `ls -la | grep a` using the 123 and #+= layers, then Enter. Up/Down recall history, Left/Right edit the line, holding Backspace repeats.
99. `cd /us` + Tab completes. Ctrl then c interrupts `sleep 30`. Ctrl-L, Ctrl-A, Ctrl-E, Ctrl-R and Ctrl-U work; the Ctrl key lights while armed and turns blue when locked.
100. `nano /tmp/t`: type text, Ctrl-O then Enter saves, Ctrl-X exits (also try `nano -x`).
101. SSH: fill the form on the keyboard and connect; type the password; on the remote run `stty size`, open nano, Ctrl-C a `ping`, use Tab and the arrows. Ctrl-D logs out; tapping a key returns to the form.
102. Hold the keyboard's Esc for 3 s: the ribbon shows, then home. No launcher restart within 5 s.
103. Ctrl-D in CLI shows "Press any key to exit"; two taps go home. In Python, Ctrl-D then one tap goes home.
104. Plug a USB keyboard mid-session: the on-screen keyboard goes, the grid returns to 12 rows with its lines restored. Unplug it: the keyboard returns about 2 s later.
Deck and HackBerry (with a physical keyboard):
105. Deck with the BT keyboard awake: CLI, Python and SSH show no on-screen keyboard and the full grid; Ctrl-C from the keyboard works.
106. HackBerry: CLI, Python and SSH show no on-screen keyboard and 24 rows; the built-in Ctrl, Tab and arrows work.

Report anything odd; the Controller can capture the screen over ssh during the test.

## L. App contract (task 014 P5)

Background: `docs/APP-CONTRACT.md`. The probe app is installed by hand (`tools/dev/app-contract-probe/README.md`,
"Install on a board"): three tiles, **Probe compat**, **Probe full** (fullscreen) and **Probe min** (fullscreen,
`X-MinSize=640x480`). Each probe writes its report to `/tmp/app-contract-probe-<compat|full|min>.txt` and leaves with
Esc or Q (or after 5 minutes). The commands below run in an ssh session on the board as the launcher's user.

Deck (640x480), **baseline before the new build is deployed** (old launcher):
107. Open the Store, leave it, then run `cat /run/user/$(id -u)/applaunch/screen.state`: note `w=` (expected `w=320`:
     the old Store overwrote the launcher's file, report 023 F1). Note the `gen=` line if there is one.

Deck, after the deploy (new launcher, probe installed):
108. Home, Settings and the Calculator look as before. The Store, LAN Scan and Wi-Fi Survey open in the 2x window
     with the toolbar under it; hold-Esc returns home from each.
109. Open and leave the Store, then `cat /run/user/$(id -u)/applaunch/screen.state`: `w=640`, `h=480`, `class=standard`,
     and a `gen=` higher than before the Store was opened (run the command once before opening the Store to compare).
110. Mesh Hop and viz1090 open and close as before.
111. Probe compat: the pattern fills the window above the toolbar (white and red border on the window's edge, grid,
     orange square at the bottom right). `grep summary /tmp/app-contract-probe-compat.txt` prints
     `summary=compat 320x170 0,0,640,340 scale 2 standard 1130`. Esc (keyboard or toolbar) leaves.
112. Probe full: the pattern fills the whole screen (border on the panel's edge). Measure the orange square with a
     ruler: 9 mm per side (it covers 9 x 9 grid cells; the bright grid lines are 10 mm apart). `grep summary
     /tmp/app-contract-probe-full.txt` prints `summary=fullscreen 640x480 0,0,640,480 scale 1 standard 1130`.
113. `input.state`: with the Bluetooth keyboard connected, `cat /run/user/$(id -u)/applaunch/input.state` shows
     `keyboards=1` (or more), `bt_keyboards=1`, `vkb=auto`. Switch the Bluetooth keyboard off and run it again:
     `keyboards=0`, `bt_keyboards=0`, `bt_gone=` a non-zero number, and a higher `gen=`.
114. Settings > System > On-screen keyboard: Off, then `grep vkb /run/user/$(id -u)/applaunch/input.state` prints
     `vkb=off`; set it back to Auto: `vkb=auto`. (With Probe full open, its line "gen ... vkb ..." follows within a
     second.)
115. The Probe min tile is shown (640x480 is not smaller than 640x480) and opens like Probe full.

Pi 3A+ (480x320, rotated, 16 bpp):
116. Home, Settings, the Calculator and a stock app look as before (tests 108 and 109 on this board: `w=480`, `h=320`,
     `class=compact`, `rot=90`).
117. Probe compat opens at 1x in the window at (80,25) above the toolbar: `grep summary /tmp/app-contract-probe-compat.txt`
     prints `summary=compat 320x170 80,25,320,170 scale 1 compact` followed by the density (about 630).
118. Probe full is upright (text reads normally, the square is at the bottom right), the colours are right (orange
     square, green summary line) in 16 bpp, and the square measures 9 mm.
119. There is no Probe min tile, in the home grid nor in Settings > Apps (480x320 is smaller than 640x480).

HackBerry (720x720, 16 KB pages):
120. (Only on a HackBerry that has the Store backend installed; in test mode it has none, so this was not run there: Probe compat in item 122b covers the same 16 KB-page code path.) Baseline before the new build: does LAN Scan open today? (Expected no, or a black window: report 023 F3.) Note what you see.
121. (Same condition.) After the deploy: LAN Scan and Wi-Fi Survey open in the 2x window with the toolbar; hold-Esc returns home.
122. Probe full fills the screen and `grep summary /tmp/app-contract-probe-full.txt` prints
     `summary=fullscreen 720x720 0,0,720,720 scale 1 square` and the density. The Probe min tile is shown.
122b. Probe compat (a wrapped app) opens and fills the 640x340 window on the 16 KB-page kernel (the VFB2 64 KiB header fix); it did not before.
123. The Settings > System > On-screen keyboard choice has Auto and Off only, so `grep vkb /run/user/$(id -u)/applaunch/input.state` prints `vkb=auto` (or `vkb=off`). `vkb=force` only exists with the environment variable `APPLAUNCH_OSK=force`: the Controller starts the test with `python tools/boards/board.py test-start hackberry --env APPLAUNCH_OSK=force`, and the grep then prints `vkb=force`.

Report anything odd; the Controller can capture the screen over ssh during the test.

## M. Apps sources persist (task 014, Apps sources persistence fix; deck and Pi 3A+)

Deck and Pi 3A+ (needs network for the sync; check item 124 first with `ssh <board> "grep -c url ~/.local/share/cardputerzero-appstore/registries.json"`).

124. Settings > Apps > Sources: add a source (any `owner/repo` that has a registry.json, for example `OSRdesign/cyberdeck-zero-apps` when it is not listed). Leave Settings, open the Store from the home grid, close it and come back to Settings > Apps > Sources: the source is still listed. `ssh <board> "grep -c url ~/.local/share/cardputerzero-appstore/registries.json"` shows the source (the hub and the source: 2 or more), also after the Store was opened.
125. Fresh board (or a board that has no source of its own and no `apps_default_source` yet): the first time Settings > Apps opens, the footer shows `Adding OSRdesign/cyberdeck-zero-apps...`, then Sources lists it and Available lists its apps. `ssh <board> "grep -o '\"appstore.registry[^,]*\|\"apps_default_source[^,]*' ~/.config/cardputerzero/config.json"` shows the `appstore.registry.*` keys and `apps_default_source`.
126. Remove that source (Sources, D, confirm), close and reopen Settings > Apps, then restart the launcher (`sudo systemctl restart applaunch`, or the board's usual restart): the source stays removed (it is not added back).
127. With a source of your own already there on a board that has no `apps_default_source`: the first opening of Settings > Apps adds nothing; the flag is set (item 125's grep).
128. Switch a source off (Enter on it), open the Store, come back: it is still off.
129. Seeded source survives opening the Store first: on a board where `install.sh` seeded the source (or after `registries.json` has the source and `apps_default_source` is not yet set), restart the launcher, wait 10 seconds, open the Store before Settings > Apps, close it: `grep -c url ~/.local/share/cardputerzero-appstore/registries.json` still shows the source, and Settings > Apps > Sources lists it.

## N. Text console and desktop boards (`cyberdeckzero-console`, `cyberdeckzero-desktop.service`; task 012 F2)

Background: `projects/APPLaunch/pizero2w/README.md`, sections "Text console" and "Desktop boards". The Controller
installs the bundle with `python tools/boards/board.py install <board>` (deck, Pi 3A+, HackBerry) and keeps an ssh
session open for the checks in backquotes. Answer PASS / FAIL (+ what you saw) per line. The unit, helper and binary
names below are those of v0.7.0 (up to v0.6.0: `applaunch-console`, `applaunch-desktop.service`, `APPLaunch.service`;
the rename itself is checked in section O).

Appliance boards (the deck and the Pi 3A+), after the install and one reboot:
130. During the boot the usual boot lines scroll on the panel as before; the launcher then starts on a clean screen.
131. `systemctl is-active cyberdeckzero-console.service` prints `active`; `python tools/boards/board.py status <board>`
     shows `console-unit: active` and `text-console: vtcon1 bind=0` (the vtcon number may differ).
132. On the board (ssh): `sudo chvt 2; sudo chvt 1`. The launcher stays intact: no boot lines, no login prompt, no
     text drawn over it, also after you tap around the home grid and Settings.
133. While the launcher runs, `sudo apt-get install --reinstall -y <a few packages>` (or a real install of about 100
     packages): nothing is drawn over the launcher during or after it.
134. `sudo reboot` from ssh, then Settings > System > Reboot: each time the boot lines show during the boot and the
     launcher comes up clean (the unit is enabled: `systemctl is-enabled cyberdeckzero-console.service` prints `enabled`).
135. Settings > System > Shutdown: the board powers off as before. Settings > System has no "Quit to desktop" entry.
136. Hold Esc for 4 s in Settings: the hold-Esc ribbon returns to the home grid at 3 s as before; the launcher keeps
     running (the exit-75 recovery only fires when the page is stuck; it then restarts the user service as before).

HackBerry CM5 (Raspberry Pi OS desktop), after the install (`board.py install hackberry`; `--check` first shows
`session: desktop (display manager lightdm.service enabled; ...)` and the unit text):
137. The install printed "not started now" and the desktop is still there. `systemctl is-enabled
     cyberdeckzero-desktop.service` prints `static` (cannot be enabled); `systemctl --user is-enabled cyberdeckzero.service`
     says it does not exist; `systemctl is-enabled getty@tty1.service` is unchanged (`enabled`).
138. Reboot: the board boots to its normal desktop; the launcher does not start by itself.
139. Applications menu (Accessories): "CyberdeckZero Launcher" is listed (no icon of its own yet). Open it: no password is asked, the
     desktop goes away within a few seconds and the launcher fills the screen (home grid, touch and keyboard work).
     `board.py status hackberry` shows `lightdm: inactive`, `cyberdeckzero-desktop.service: active`, `text-console: ...
     bind=0`.
140. Keys typed in the launcher (a Wi-Fi password field, the terminal) go nowhere else: `fgconsole` (sudo) prints `7`,
     and `journalctl -b -u getty@tty1 --since -5min` shows no login attempt.
141. Settings > System lists "Quit to desktop" (above Reboot and Shutdown). Open it, answer No: nothing happens. Open it
     again, answer Yes: the launcher closes and the desktop comes back within a few seconds (as after a boot: the
     login screen, or the desktop when lightdm logs the user in by itself). `journalctl -b -u cyberdeckzero-desktop.service | tail` shows
     `[SESSION] quit requested`, the unit stopping with status 0 and `desktop: lightdm.service started again`.
142. Start the launcher again from the menu, then Settings > System > Shutdown > Yes: the board powers off WITHOUT the
     desktop appearing in between (watch the screen until it is dark). After the next boot:
     `journalctl -b -1 -u cyberdeckzero-desktop.service | tail` shows `the system is shutting down: lightdm.service is not
     started again`.
143. Same with Settings > System > Reboot: the board reboots straight to the desktop (no desktop flash before the
     reboot), the launcher does not start by itself.
144. Start the launcher, then from ssh `sudo pkill -9 -x cyberdeckzero` (a crash): the desktop comes back within a
     few seconds; starting the launcher from the menu works again right away.
145. Hold Esc for 4 s in Settings: the hold-Esc ribbon returns to the home grid at 3 s; the launcher keeps running and
     the desktop does not come back (the ribbon is not a quit). The exit-75 recovery (a stuck page) cannot be caused
     on purpose; it ends like item 144: `systemctl show -p Restart cyberdeckzero-desktop.service` prints `Restart=no`, so
     any exit gives the desktop back and nothing restarts the launcher in a loop.
146. Start the launcher, then from ssh `sudo chvt 2; sudo chvt 7`: nothing is drawn over the launcher. Quit to desktop,
     then switch to a text console (Ctrl+Alt+F2 on the built-in keyboard, or `sudo chvt 2`): the text console shows
     there (it was given back), `sudo chvt 7` returns to the desktop.
147. While the launcher runs, `sudo apt-get install --reinstall -y <a few packages>` from ssh: nothing is drawn over
     the launcher.
148. The launcher's state files exist while it runs, also with no ssh session open during the start:
     `ls /run/user/$(id -u)/applaunch/` lists `screen.state` and `input.state`; sounds (if the board has audio) play.
149. From ssh, `systemctl start cyberdeckzero-desktop.service` as the user (no sudo) works without a password;
     `systemctl stop cyberdeckzero-desktop.service` (no sudo) brings the desktop back. `systemctl restart lightdm` without
     sudo still asks for a password (nothing else was opened up; answer Ctrl+C).
150. `./uninstall.sh --dry-run` from the bundle lists the desktop unit, the menu entry and `52-cyberdeckzero-desktop.rules`;
     after a real `./uninstall.sh` (from ssh) the menu entry is gone, the desktop is running and
     `cat /sys/class/vtconsole/vtcon1/bind` prints `1`.

## O. The rename to CyberdeckZero Launcher: install over v0.6.0 (report 025 section 5; v0.7.0)

Background: `docs/dev/reports/025-rebranding-plan.md` sections 2 and 5. The binary is now
`/usr/share/APPLaunch/bin/cyberdeckzero`, the units `cyberdeckzero.service` (user service, appliance boards),
`cyberdeckzero-console.service` and `cyberdeckzero-desktop.service`, the polkit rule `52-cyberdeckzero-desktop.rules`,
the menu entry `cyberdeckzero.desktop`; the earlier binary and helper names stay as links for one release. Paths,
`/etc/applaunch/board.conf`, the `APPLAUNCH_*` variables and the Store are unchanged. Deck, Pi 3A+ and HackBerry, each
running v0.6.0 before. Answer PASS / FAIL (+ what you saw) per line.

Before the install (Controller, on each board; the backup is the rollback's material):
151. Backup: `sudo tar czf ~/pre-v0.7.0.tgz /usr/share/APPLaunch/bin /etc/systemd/system/applaunch-*.service
     /etc/polkit-1/rules.d /usr/share/applications /etc/applaunch ~/.config/systemd/user` (a "No such file" line for
     a path a board does not have is fine), and `sha256sum /etc/applaunch/board.conf` (deck: no such file; note it).
152. `python tools/boards/board.py upload <board> <bundle>` then `board.py install <board> --check`: the plan has a
     "migration from the names of v0.6.0 and before" block. Deck and Pi 3A+: `user service APPLaunch.service of
     <user>: found`, `applaunch-console.service: found`, `applaunch-desktop.service: not found`; HackBerry:
     `applaunch-desktop.service: found`, the rule and `applaunch.desktop: found`, `APPLaunch.service ...: not found`.
     All boards: the earlier binary `found: replaced by a link to cyberdeckzero` and `logo_lcd.png` listed among the
     files of earlier bundles (and `adb/cardputer-adb*`). Nothing changed yet (`board.py status <board>` as before).
152b. Pi 3A+ with the Waveshare 4.3" DSI panel (`board.py` entry `pi3a-dsi`). Before testing, the board must boot with
     the panel's own overlay: `grep -E '^dtoverlay=vc4-kms-(v3d|dsi)' /boot/firmware/config.txt` prints
     `dtoverlay=vc4-kms-v3d` and `dtoverlay=vc4-kms-dsi-waveshare-800x480`, and not the generic
     `dtoverlay=vc4-kms-dsi-7inch` (with it the picture flickers and shakes); install.sh does not edit config.txt on this
     board, so this is set by hand (Pi README, "Boards"). Then the plan says `board: pi3a-wsdsi43`,
     `profile: pi3a-wsdsi43: /etc/applaunch/board.conf was edited by hand: kept (use --force to replace it)` (the
     owner's hand-written profile) and `boot config: cmdline cursor/consoleblank only (the deck's PWM backlight overlay
     is not for this board)`. After the install: `sha256sum /boot/firmware/config.txt` is unchanged, there is no
     `config.txt.bak-applaunch` from this install, and `grep -o 'fbcon=[^ ]*' /boot/firmware/cmdline.txt` prints
     nothing.

Install and checks (deck and Pi 3A+: install, then one reboot; HackBerry: install with the launcher running, then
one reboot):
153. `board.py install <board>`: the output has `== Migrating an earlier install`; deck and Pi 3A+: `user service
     APPLaunch.service of <user>: stopped, disabled and removed` and `applaunch-console.service disabled and removed`,
     then the launcher comes back on the panel within a few seconds; HackBerry: `the earlier launcher
     (applaunch-desktop.service) was running: stopped, the desktop comes back` and the desktop is there again. At no
     time are two launchers on the screen.
154. Deck and Pi 3A+: `systemctl --user is-active cyberdeckzero.service` prints `active`, `systemctl --user cat
     APPLaunch.service` finds no file, `ls ~/.config/systemd/user ~/.config/systemd/user/default.target.wants` lists
     `cyberdeckzero.service` only; `systemctl is-enabled cyberdeckzero-console.service` prints `enabled` and
     `systemctl is-enabled applaunch-console.service` says it does not exist; `board.py status <board>` shows
     `service: active`, `console-unit: active`, `old-service APPLaunch.service: inactive`, `text-console: ... bind=0`.
155. The process: `pgrep -ax cyberdeckzero` shows `/usr/share/APPLaunch/bin/cyberdeckzero` and `top` shows
     `cyberdeckzero`.
     <!-- branding-allow-begin: the earlier binary name, checked to be gone -->
     With no app open, `pgrep -ax M5CardputerZero` prints nothing (apps still named with the earlier prefix share that
     process name while they run). `readlink /usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch` prints `cyberdeckzero`.
     <!-- branding-allow-end -->
     `readlink /usr/share/APPLaunch/bin/applaunch-console` prints `cyberdeckzero-console`.
156. `/etc/applaunch/board.conf` is unchanged (`sha256sum` as in item 151; no new `board.conf.bak`).
157. `ls /usr/share/APPLaunch/share/images/logo_lcd.png /usr/share/APPLaunch/adb` says no such file for both;
     `ls -R /usr/share/doc/cyberdeckzero-launcher/` lists `LICENSE`, `LICENSES/` (with the texts), `components/`
     (`cp0_lvgl-LICENSE`, `LVGL-LICENSE.txt`, `freetype-LICENSE.txt`, ...), `fonts/JetBrainsMono-OFL.txt` and
     `OPEN_SOURCE_COMPONENTS.md`.
158. Reboot: the launcher comes up by itself (deck, Pi 3A+) as in items 130 and 131; HackBerry: the board boots to its
     desktop and nothing starts by itself.
159. HackBerry: the applications menu (Accessories) shows "CyberdeckZero Launcher" once (no second, older entry);
     `ls /usr/share/applications | grep -e applaunch -e cyberdeckzero` prints `cyberdeckzero.desktop` only and
     `ls /etc/polkit-1/rules.d | grep desktop` prints `52-cyberdeckzero-desktop.rules` only. Open it: no password,
     the launcher takes the screen; `board.py status hackberry` shows `cyberdeckzero-desktop.service: active` and
     `old applaunch-desktop.service: inactive`. Settings > System > Quit to desktop: the desktop comes back.
160. Settings > System > About and Licenses read correctly (the product name, the credit and disclaimer of report
     025); Settings > Bluetooth: the board's name is unchanged, or "CyberdeckZero" where it had none.
161. The Store syncs, installs LanScan and removes it again; Settings > Apps > Sources keeps its sources (section M,
     item 124). LanScan, viz1090, Wi-Fi Survey, Mesh Hop and the probe start from the home grid and hold-Esc returns
     home.
162. `board.py install <board>` a second time: `nothing to migrate (no unit, rule or menu entry of the earlier
     names)`, no file of earlier bundles removed, the launcher restarts (deck, Pi 3A+) and items 154 to 157 still hold.
163. `./uninstall.sh --dry-run` from the bundle (in `/tmp/applaunch-bundle/bundle`) lists both generations: `user
     service cyberdeckzero.service / APPLaunch.service`, `cyberdeckzero-console.service / applaunch-console.service`,
     `cyberdeckzero-desktop.service / applaunch-desktop.service`, the menu entry and `52-cyberdeckzero-desktop,
     52-applaunch-desktop`, and `/usr/share/doc/cyberdeckzero-launcher`; it changes nothing.

Rollback to v0.6.0 (only if a check fails; then report which):
- Deck, Pi 3A+: `systemctl --user disable --now cyberdeckzero.service`, then `sudo systemctl disable --now
  cyberdeckzero-console.service`, `rm ~/.config/systemd/user/cyberdeckzero.service`, `sudo rm
  /etc/systemd/system/cyberdeckzero-console.service`, `sudo systemctl daemon-reload; systemctl --user daemon-reload`,
  then `sudo ./install.sh` from the v0.6.0 bundle (it puts back its binary and helper as files over the links, its
  units, and starts its `APPLaunch.service`).
- HackBerry: quit the launcher if it runs (or `sudo systemctl stop cyberdeckzero-desktop.service`), then `sudo rm
  /etc/systemd/system/cyberdeckzero-desktop.service /etc/polkit-1/rules.d/52-cyberdeckzero-desktop.rules
  /usr/share/applications/cyberdeckzero.desktop; sudo systemctl daemon-reload`, then `sudo ./install.sh` from the
  v0.6.0 bundle.
- Optional afterwards (v0.6.0 does not use them): `sudo rm /usr/share/APPLaunch/bin/cyberdeckzero
  /usr/share/APPLaunch/bin/cyberdeckzero-console; sudo rm -rf /usr/share/doc/cyberdeckzero-launcher`. The removed
  `logo_lcd.png` comes back with the v0.6.0 bundle; `board.conf` was never touched.
