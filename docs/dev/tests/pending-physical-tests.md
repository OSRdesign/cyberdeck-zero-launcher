# Pending physical tests (run by the user on the deck)

Testing is led by the Controller with the user (see ROLES.md): Claude does the code checks and deploys a build, the user
does the physical tests below and answers "OK" or what was wrong per step, and Claude routes the findings to the right
developer agent. Last updated 2026-10-06.

## A. Esc rule on LanScan 0.1.3 and Wi-Fi Survey 0.1.1 (built, code-checked, NOT yet deployed: needs PIPW)
Rule: a short Esc is always Back and never quits; the app quits only on a 3 s hold of Esc (launcher behaviour).
1. LanScan, device list: short Esc. Expected: nothing, footer shows "Hold Esc 3 s to exit" for about 2.5 s.
2. LanScan, open a device (ports list): Esc goes back to the device list.
3. Wi-Fi Survey, Networks and Channels tabs: short Esc shows the same hint and does not quit; detail view: Esc goes back to its tab.
4. Hold Esc 3 s in either app: back to the launcher.

## B. MeshZero (was meshcore) 0.1.0 draft: first human test, deferred ("we'll get back to it later")
The app is shelved as an unpublished draft; a local .deb is deployed with deck.py when you want to test (needs PIPW).
Wake the M4 keyboard first; do not send on air unless you want to. Answer per step number.
1. Open the tile. Status: board name, firmware, connected? What does the clock line say?
2. Switch Status / Chats / Contacts by touch; swipe-scroll a list (as set in Settings > Touch).
3. Top-level tab, short Esc: only the hint "Hold Esc 3 s to exit".
4. Hold Esc 3 s: back to the launcher.
5. Chat: Esc goes back; editor: Esc cancels it.
6. Compose on Public: type text with an accent (café), Backspace, check the "N left" counter, Esc (do not send).
7. Settings: frequency editor: 869.525 accepted, 900 refused in red, Esc; node name editor without saving.
8. Let the M4 sleep or disconnect, open an editor: "Keyboard needed: wake the Bluetooth keyboard"; wake it, editor works.
9. Settings > "Sync clock now": the clock policy writes the deck time to the board if the deck is online and the board has no GPS. What does Status say?
10. Unplug and replug the board's USB cable: reconnects by itself?
11. Tap contact rows, Advert (do not send unless you want to), the - and + steppers: respond, and a tap never acts twice?
12. Optional (writes the board's channel table): add a hashtag channel, see it in Chats, remove it with two-tap Remove and with Del.
Open code questions: channel text limit with a long node name (needs one real near-limit message); whether the official apps lower-case a #name before hashing (ours hashes it as typed, like meshcore-cli).

## C. Settings > Apps polish (task 006): still open from the user's side
- Tap the app rows, tabs, sources rows, "+ Add", "Sync my sources", "Update all"; close the failure panel by tap.
- Scroll with more than six rows.
- The real M4 keyboard with the letter shortcuts U / S / A / D.
- A real "No network" (cut Wi-Fi, then sync) and a real 404 on a real source.
- The first Sources row label looked cut off in two screenshots taken right after scrolling: look at it.
- Delete nothing else: the two launcher backups on the deck were already removed.

## D. Environment note
`deck.py` needs the PIPW environment variable (the deck password) in the Claude Code session; it was missing in the
session of 2026-10-06. Rotate the deck password (it leaked twice into tool output on 2026-10-04) and set PIPW again.
