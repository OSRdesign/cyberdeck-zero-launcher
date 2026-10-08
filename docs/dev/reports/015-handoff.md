# 015 Mesh Hop 0.1.1 (roadmap phase 1) - hand-off (apps-dev)

```
Task: 015 mesh-hop phase 1 (brief 011)   Agent: apps-dev   Result: done on the PC (built, unit tested, headless runs, screenshots); NOT run on the deck, NOT tried with a real board
Changed: apps/mesh-hop in the apps repo (still untracked, nothing committed; `git status` of the apps repo: only `?? apps/mesh-hop/`
         plus the Controller's docs files). apps/meshzero, the launcher source, packages/ and registry.json untouched.
How it was checked: unit tests core 556 / simulator 30 / UI logic 699 checks, 0 failed; x86 headless build against the simulator
         (9 scripted runs, 52 screenshots, state dumps); aarch64 build and .deb inspected. Not run: the aarch64 binary, the real
         panel and touch screen, the Bluetooth keyboard, a real board.
Package: C:\CLAUDE\zero7\pkg-out\mesh-hop_0.1.1_arm64.deb  (1 016 788 bytes, md5 dac05c4b79b87c5baf4b4292af047c4c)
Screens: C:\CLAUDE\zero7\pkg-out\mesh-hop-0.1.1-screens\<run>\   (the 0.1.0 set in ...\mesh-hop-screens is unchanged)
Not done / risks: see "Could not verify" and "Risks".
Needs from the Controller: the deck test (protocol below), and the log of the channel send (path below). No launcher request.
```

## What changed, per item of brief 011 phase 1

Paths below are under `C:\CLAUDE\zero7\cyberdeck-zero-apps\apps\mesh-hop\` (`core` = `src/main/core`, `ui` = `src/main/ui`).

1. **Unread counts.** The Chats tab title carries a gold pill with the unread total (99+ above 99; muted channels are not
   counted). Channel and direct rows of the left pane show **only** the unread count (a gold pill, nothing for none, the word
   "muted" for a muted channel); the last message is gone from the list. Rows are 44 px high. `ui/app.cpp` (render_tabs,
   render_chats), `ui/ui_logic.*` (`fmt_count_badge`, `ChatRow.muted`), `core/model.*` (`unread` honours the mute flag,
   `unread_raw`).
2. **Contacts.**
   * Sort state is a value (`ContactView`: sort, reverse, filters) kept by the app while the screen is left and re-entered.
   * **The tap bug.** The old list resolved a tap by the row *position* in a list that could be rebuilt (or not, while a finger was
     down) in another order. Now every row carries the contact key (`RowSpec.id`) and the tap resolves it from the row that was
     touched; the rows come from one `ContactSnapshot` (sort + rows) so the order on screen and the order the app uses cannot
     differ. Test: 161 contacts, sorted by name, the key at position i is the node shown at position i; the model changing under
     the screen does not move it (`test_contact_view_and_snapshot`). Headless run: sort name reversed, tap row 1 -> "Weather room"
     opens; after a drag-scroll the tapped row is the one under the finger (`smoke`, `big-161-contacts`).
   * **"Loading..." popup** while a sort or a filter is applied (drawn first, the rows are rebuilt a few ticks later, at least 90 ms
     on screen; keys and taps are ignored for that moment).
   * **Speed.** `RowList` is now virtual (pool of about 11 row widgets re-bound while scrolling; data of every row kept). Up/Down
     only re-binds the two rows involved and scrolls; the list is rebuilt only when the model, the sort, the filter or the minute of
     the ages changes. Headless measure on the PC with 161 contacts: Up/Down **0.2 to 0.3 ms per key**, PgDn 0.8 ms, 9 to 10
     row widgets alive (`bench` command, `big-161-contacts/run-output.txt`). The deck is about 20 times slower than this PC at
     most; the real number must be measured there (protocol item 8).
   * **C2 filters:** touch chips for the type (All, Chat, Repeater, Room, Sensor) and for the time since heard (Any time, 1 h,
     24 h, 7 d), keys `T` and `H`; "n of 161" in the counter; never-heard nodes fail any time filter. Chips are 44 px high touch
     targets.
3. **Channel send status.** `core/client.cpp` `send_channel`: the command now completes on **any non-error, non-push answer**
   (OK, MSG_SENT or an unexpected code) and the message turns to "sent"; an Error answer gives "failed: reason"; if nothing
   arrives, a timer ends "sending" **5 s after the tap** with "sent (unconfirmed)" (gold, no check mark), so a channel message never
   stays on "sending" without an error; a late answer replaces it by plain "sent". A channel is never "delivered" (an ACK-looking
   push changes nothing; `message_status` shows "sent" even if a Delivered state were set). Direct messages keep sending, sent,
   delivered, no ack (test run unchanged). **Log:** `~/.local/share/mesh-hop/mesh-hop.log` (new `core/log.*`, bounded 64 KB with one
   `.1` rotation, no message text) gets, for every channel send: `channel send seq=N channel=C bytes=B`, every frame the board
   sends while that command waits (`rx 0xNN len=L while waiting for command 3`, pushes included) and the verdict
   (`answer 0x00 -> sent`, `error 3`, `no answer ... shown as sent (unconfirmed)`). `MESHHOP_DEBUG=1` logs every frame. Simulator
   cases: `--chan-reply ok | msgsent | odd | none` (the logs of the four runs are in `mesh-hop-0.1.1-screens\chan-*\run-output.txt`).
4. **Settings.**
   * **Choice popup** (`App::popup_*`, `ui_logic` `Choice`, `choice_key`): the value in the middle, a 100 x 100 px arrow button on
     each side, Cancel and OK 170 x 48; Left/Right (also Up/Down, PgUp/PgDn, Home/End) step, Enter accepts, Esc cancels, position
     "n / N", an explanation line. Used for **bandwidth, spreading factor, coding rate, TX power, preset** and the two retry
     rows. The `-` / `+` buttons are gone everywhere; Left/Right on a settings row still step the value without the popup.
     Frequency stays a typed number (free value, BT keyboard) and the node name a typed text.
   * **Presets:** 26 entries from report 014 (answer b) in the dated data file `core/radio_presets.inc` (source URL
     `https://api.meshcore.nz/api/v1/config`, date 2026-10-07, shown at the foot of the popup; "(Deprecated)" entries keep their name
     and are counted by `RadioPreset::deprecated()`); choosing one fills frequency, bandwidth, SF and CR and keeps name and TX power.
     62.5 and 250 kHz are in `lora_bandwidths()`; the test sends every preset through `validate_radio` and `build_set_radio` and
     checks the wire units (62.5 kHz = 62500 Hz). The row shows the matching preset name or "Custom".
   * **Public channel (slot 0)** is not listed in Settings (still in Chats). Every other channel is **removable** in Settings
     (button, or Del, or the row menu) behind a Yes / No box ("Remove"); `Client::remove_channel` no longer needs "added by this app"
     (slot 0 is refused; the mute flag goes with the channel). The Chats header "Remove" button was dropped (it is now Mute).
   * **Undo changes** and **Save to radio** are the **last two rows**; each opens a Yes / No box that lists the changes
     ("Frequency 869.525 -> 869.432 MHz ..."); `S` / `V` open the same boxes. Default focus is No.
   * **Board GPS** row (Settings > Board) when the firmware lists the `gps` custom variable: tap / Enter sends SET_CUSTOM_VAR
     `gps:1|0` and reads the variables back; hidden when the board lists no `gps`; see item 6 (D10) for old firmware.
5. **Keys.** Tab / Shift+Tab cycle the five tabs in a loop (unit-tested loop); the F1..F5 binding is gone (the evdev translator still
   decodes F keys, the app ignores them) and no hint mentions them; the left-edge swipe (`EdgeSwipe`, `on_edge_swipe`,
   `swipe_consumed`, the `swipe-back` script command, its tests and README text) is removed. Direct touch stays.
6. **Cheap picks.**
   * **M3 retries (direct only):** `RetrySettings` {tries 1..4, forget the route before try N or never}; default 3 and "before try 3" =
     the 0.1.0 behaviour (3 sends, RESET_PATH before the third). Two rows in Settings > Direct messages; saved at once in
     `prefs.txt`. Tests count the sends, the attempt byte (0..3) and the RESET_PATH for five settings and for "acknowledged on the first
     try" (`test_retry_settings`).
   * **M4 mute:** local flag per conversation (`Model::set_muted`, `prefs.txt`), header button / Ctrl+M in Chats, row buttons in Settings;
     no pill, not counted in the tab total, "muted" in the list. Ctrl+M (not a plain letter) because plain letters start a message.
   * **C2:** see item 2.
   * **D1:** see item 4.
   * **D10 firmware check:** `core/features.*`. DEVICE_INFO levels from the references (client repeat 9, path hash mode 10, for the next
     phases) and, for this phase, what the board answered: "unsupported" to GET_CUSTOM_VARS or to GET_CHANNEL turns the GPS row /
     the channel adding off with the notice "Firmware too old for this feature" (Settings rows and the notice at the tap); a board with
     custom variables but no `gps` shows no GPS row. Settings > Board also shows "Firmware v1.15.0-... (level 11)". The minimum
     levels for custom variables and channels are not in the references: those two are detected from the answer, not from a number.
   * **H1 private and community channels:** `core/channel_key.*`. "+ Add channel" (Chats row or Settings row) opens a menu:
     hashtag channel (as before); **private channel, key typed** (name editor, then a key editor that accepts hex digits and ignores
     spaces / dashes / colons, live "n of 32 hex characters", refuses a wrong length, non-hex and all zeros); **private channel,
     random key** (/dev/urandom, never all zeros; the key is shown in an Info box once the board holds the channel, grouped by 8,
     "Share this key as text"). **Key** button / `K` on a channel row shows the key of any channel (read live from the board; never
     written to the history files). Names starting with `#` are refused for private channels. Hashtag channels unchanged.

## How it was built and tested

* Unit tests: `wsl -e sh /mnt/c/CLAUDE/zero7/cyberdeck-zero-apps/apps/mesh-hop/src/tests/run_tests.sh` -> `556 checks, 0 failed`,
  `sim: 30 checks, 0 failed`, `ui: 699 checks, 0 failed` (-Wall -Wextra, no warning). New logic with tests: unread counts and mute,
  prefs persistence, sort / filter / snapshot (161 contacts), the preset table and its wire units, choice / confirm / menu popups and
  their keys, Settings layout (Save and Undo last, no slot 0, GPS / old firmware rows), status text, channel send against a fake board
  (OK, MSG_SENT, unexpected frame, silence, error, link loss, behind a slow command), retry settings, firmware features, channel key
  parsing / generation, private channel add and remove, the log file and its rotation, the change list of the Save box.
* Host build (headless): `wsl -e sh .../apps/mesh-hop/build/build.sh host` -> `~/mesh-hop-host/mesh-hop` (moved out of `/tmp`, which WSL
  empties between calls). aarch64: `wsl -e sh .../apps/mesh-hop/build/build.sh` (copies `src/` to the scratch project, `flock
  /tmp/wsl-build.lock scons`, strips and installs the binary); no build warnings. `readelf -d`: libstdc++, libfreetype, libm, libc,
  ld-linux-aarch64, libgcc_s.
* Package: `python tools/make_registry.py --only mesh-hop --out C:/CLAUDE/zero7/pkg-out --owner OSRdesign --repo cyberdeck-zero-apps`
  -> `mesh-hop_0.1.1_arm64.deb`; `dpkg-deb -I/-c`: Version 0.1.1, Depends libfreetype6, 4 files. `app.json` 0.1.1 (draft stays true) and the
  README (rewritten for 0.1.1: no swipe, no F keys, new screens and keys).
* Headless runs against the simulator (`src/tests/ui_smoke.sh`, scripts in `src/tests/`), each in its own folder under
  `...\mesh-hop-0.1.1-screens\`, with `run-output.txt` (the `say == ...` labels give the expected value of each `state` line):
  `smoke` (default board: 23 screenshots), `offline` (no board), `big-161-contacts` (`--contacts 157`: speed, sort kept, drag, tap, filters),
  `chan-silent`, `chan-msgsent`, `chan-odd-frame` (the three answers of a channel send), `fw-old` (`--old-firmware`), `fw-nogps`
  (`--no-gps-var`), `fw-gps`. `tools/meshcore_sim.py` got custom variables with a switchable `gps`, the channel-send answer options,
  `--contacts`, `--old-firmware`, `--no-gps-var`.

## Could not verify

* **The board frame for the channel send.** The simulator answers OK, MSG_SENT or an odd frame (all end at "sent") or nothing (ends at
  "sent (unconfirmed)" after 5 s). The real firmware's behaviour, and why 0.1.0 stayed on "sending", is still unknown: the log lines
  above will say what the board sends. Possible causes the new code covers: an answer code the old list did not accept, or an
  answer that never came. Not covered: a board that answers with an error that is not an error frame.
* **Anything touch related on the real screen:** the 44 px chips, the 100 px arrows, the drag-scroll with the pooled rows, a tap
  after a momentum scroll, the pressed state, the channel row buttons (zones by x: Mute 344-434, Key 440-510, Remove 516-620), a
  tap during the "Loading..." popup (ignored), the real touch axes. The headless runs inject taps and drags into LVGL's pointer
  input (so the event path is exercised), not the evdev reader.
* The Up/Down speed on the Pi Zero 2 W (only the PC number exists), the memory of the app (not measured again; the pool is about 30
  widgets per list), the look of the popups on the real panel (arrow glyphs are LV_SYMBOL_LEFT / RIGHT in Montserrat 40).
* Ctrl+M on the user's keyboard; the real value format of the `gps` custom variable on the user's firmware (the code reads "1" /
  "on" / "true" / "yes" / "enabled" as on and writes "1" / "0"; the simulator uses "gps:0" / "gps:1"); whether the user's board lists
  `gps` at all (the XIAO nRF52840 board of the first test may list none: then no GPS row is expected).
* The preset list was not fetched again (it is the 2026-10-07 list of report 014).
* Retry behaviour against a real contact (only the fake board and the simulator).

## Test protocol for the deck (the user, with the real board; the Controller runs the log step)

Install `mesh-hop_0.1.1_arm64.deb` over 0.1.0 (Settings > Apps flow or `deck.py install`); the history of 0.1.0 is kept.

1. **Start and tabs.** Start Mesh Hop from the tile. Look: top bar, tabs, no leftover residue. Tap each tab. Press Tab five times: it comes back to the
   starting tab (Shift+Tab goes backwards). F1..F5 do nothing and are not mentioned anywhere. A swipe from the left edge does nothing.
2. **Unread counts.** From another conversation or another app tab, let a message arrive (or ask someone to write in a channel and to you
   directly). Chats tab title: a gold number; the channel and direct rows show a number (no message text); opening the conversation
   clears its number and the total drops.
3. **Mute.** In a channel tap **Mute** (header) and, once, press Ctrl+M: the row says "muted", no number, the tab total ignores it; a message
   arriving there adds nothing. Unmute. Same from Settings > Channels (Mute button).
4. **Channel send (the first-test bug).** Write in a channel (Public or a hashtag channel) and send. Expected: "sending" then **"sent"** (blue check) within a
   second or two; never "delivered". If it shows "sent (unconfirmed)" (gold) the board did not answer: note it. Do it three times.
   *Controller:* then read `~/.local/share/mesh-hop/mesh-hop.log` (for example `tail -n 40 ~/.local/share/mesh-hop/mesh-hop.log` over ssh): the lines
   `channel send seq=...`, `rx 0x.. len=.. while waiting for command 3` and `answer 0x.. -> sent` (or `no answer`) say which frame the board returns.
   Send one direct message too: sending, sent, delivered (or no ack).
5. **Contacts sort and tap.** Contacts tab, tap the **Name** title: "Loading..." appears briefly, the list is sorted. Tap a row in the middle of the
   list: the node you touched opens (chat for a chat node, detail for a repeater). Come back (Esc / Tab): the sort is still by name. Tap
   the title again: reversed. Do the same sorting by Heard and by Distance.
6. **Contacts filters.** Tap the chips Repeater, then 24 h, then All and Any time: the list and the count ("n of 161") follow; `T` and `H` do the same from the keyboard.
7. **Drag.** Drag the list with a finger: it follows, nothing opens; tap a row right after: the row under the finger opens.
8. **Speed.** Hold Down for a few seconds in the 161-contact list, then do the same by dragging: the selection should keep up like the touch
   scroll does (say if it lags, and roughly how long it takes from the first to the last row; Home / End jump).
9. **Choice popups.** Settings: tap **Bandwidth**: a box with the value in the middle and a big arrow each side; tap the arrows, then OK (the row
   turns gold with a star). Same with **Spreading factor**, **Coding rate**, **TX power** (keys: Left / Right, Enter, Esc). Tap **Preset**: the list of 26
   names with "(Deprecated)" on two of them; pick "EU/UK (Narrow)": frequency 869.618, 62.5 kHz, SF8, 4/8 fill in. Nothing is sent to the board yet.
10. **Save and undo.** Scroll to the end (or press End): "Undo changes" then "Save to radio" are the last rows. Tap Undo: a Yes / No box lists the changes; answer No
    (nothing happens), tap Undo again and Yes (values back). Change a preset again, Save: the box lists the changes, Yes: "Settings saved to the radio" and the values stay.
    **Put the radio back to your usual settings afterwards** (the preset list or Undo before saving), or keep them if that is what you want.
11. **Channels in Settings.** The Public channel is not listed. Add a hashtag channel (menu "+ Add channel"), then **Remove** it: a Yes / No box first. Add a
    **private channel with a typed key** (any 32 hex characters, for example a key from another MeshCore client): wrong length / non-hex characters are refused; the
    channel appears. Add a **private channel with a random key**: the key shows in a box, as text. **Key** on a channel row shows it again (for a hashtag channel too).
12. **GPS.** If the board lists a GPS, Settings > Board shows "Board GPS": tap it, the value flips On / Off and the footer says so; with no GPS on the board there is no such row.
13. **Retries.** Settings > Direct messages: "Message tries" and "Forget the route" open a choice box. Defaults 3 and "Before try 3". Switch the board's recipient off (or send to a node out of
    range) and see "no ack" after the number of tries you set (about 3 s per try); put the defaults back.
14. **Firmware text.** Settings > Board shows the firmware ("v1.15.0-... (level 11)"). (An old firmware would show "Firmware too old for this feature" for the GPS or the channels row.)
15. **Escape rules.** Short Esc closes a box, then goes back one level, never quits; hold Esc 3 s quits. Unplug and replug the board: reconnects, the Settings rows come back.

## Risks

* The real cause of "sending" is unknown until item 4's log is read; the 5 s fallback hides it from the user but a channel message may then be shown as sent when it never left
  (for example when the command waited behind a long contact download for more than 5 s: the timer starts at the tap, not at the write). The log distinguishes the two.
* "Finish on any non-error frame" could in theory take a stale answer to an earlier command for the answer to the channel send; the channel send is sent urgent and alone in flight, so the window is tiny.
* The pooled list is new code on LVGL's scroll path; a bug would show as a wrong row under the finger, a row not drawn after a fast flick, or a scroll that stops. The tests cover the data, the headless run
  covers drag and tap on the PC; the deck panel and its touch driver are untested.
* The Loading popup shows for at least 90 ms even when the work is instant (a deliberate flash; `MESHHOP_LOADING_MS` changes it, the screenshots use 400).
* `Store::contacts_changed` still rewrites the whole `contacts.jsonl` on every advert or message (161 lines each time): not part of this task, but on the deck's SD card it may be worth a later look.
* Settings > Save sends SET_RADIO with 62.5 or 250 kHz bandwidth values exactly as the presets say; a firmware that refuses a value answers an error and the notice says "Not saved: ..." (the refusal path is tested).
* The key of a channel is shown on screen: anybody next to the deck can read it.
* Test scripts and the README mention paths in WSL; `ui_smoke.sh` now takes `SIMARGS` and `LOADING_MS`.

## Questions for the Controller

1. Ctrl+M is the Chats shortcut for Mute because plain letters start a message: acceptable, or another key?
2. The Chats header "Remove" button was replaced by "Mute" (removal is in Settings with the confirmation, as the brief says): confirm.
3. Should the preset list be refreshed from `api.meshcore.nz` before the deck test (it is one file, `core/radio_presets.inc`, and a date)?
