# 007 MeshCore: hand-off, ROUND 4 (rounds 3, 2 and 1 are kept below)

```
Task: 007 meshcore round 4        Agent: apps-dev        Result: done (built and unit tested; UI not run on a screen by me)
```

## Changed (round 4)

`C:\CLAUDE\zero7\cyberdeck-zero-apps`, branch `meshcore`, nothing committed. Version **0.1.3**: `packages/meshcore_0.1.3_arm64.deb` (1 431 892 bytes, md5 `95928f50ce8d1863aa08db976db80a8a`, equal to the registry entry;
the 0.1.2 deb was removed from `packages/`). Other debs/entries restored to HEAD. **registry.json keeps LF**: the merge is now done by a script that reads HEAD, appends only the meshcore entry and `generated_at`, and writes in binary;
`git diff` shows only the meshcore entry and `generated_at`, no CRLF warning. Built under the WSL lock, 0 warnings. Launcher untouched.

Unit tests: `test_core` 253 checks, `test_sim` 30, 0 failures (`run_tests.sh`, now linked with `-lpthread`).

New core files: `clock_policy.*` (table, date text), `deck_clock.*` (NTP probe thread), `nav.hpp` (Esc rule). Changed: `client.*`, `model.*`, `protocol.*`, `util.*`, `input_state.cpp`, `ui/meshcore.*`, `tools/meshcore_sim.py`, `README.md`, `app.json`, tests.

## 1. "Keyboard needed" check (defect of report 012)

`keyboard_listed()` now skips devices whose name contains `applaunch-` (the launcher's own `applaunch-vkbd`) and devices on the virtual bus (`Bus=0006`). USB (0003) and Bluetooth (0005, the uhid M4 Keyboard) count. The test uses listings in the shape of the deck:
Goodix touch screen only, M4 Keyboard, `applaunch-vkbd`; results: touch alone no, vkbd alone no, touch+vkbd (BT keyboard asleep) **no**, M4 alone yes, all three yes, a USB keyboard yes, anything on Bus 0006 no.
Caveat for the verifier: a test keyboard made with uinput must not be named `applaunch-*` and should not be on the virtual bus (uinput default bus is usually USB 0003), otherwise the app will say "Keyboard needed" with it. The listing shapes are written from the report and the kernel format, not captured from the deck.

## 2. Board clock policy

Implemented as in the user's table (`clock_policy.hpp`, one function `decide_clock(deck, gps)`):

| Deck clock | Board GPS | Action | Source shown on Status |
| --- | --- | --- | --- |
| online | none/off (or not reported) | `06 SET_TIME` with the deck time | deck NTP |
| offline | none/off | typed prompt "Board clock: start date and time (local)", prefilled with the deck's date/time, Enter writes it, Esc skips | set by you / not set |
| offline | enabled | `05 GET_TIME`, nothing written | board GPS |
| online | enabled | same: nothing written (the user confirmed this default) | board GPS |

* "Online" = `timedatectl show -p NTPSynchronized --value` = `yes` (no root), run in a worker thread (`DeckClockProbe`), at app start and at every sync; if `timedatectl` fails, `nmcli -t -g CONNECTIVITY general` = `full` is used as a weaker hint. The policy waits up to 6 s for the answer;
  "unknown" (nothing answered) counts as offline.
* **GPS: what the protocol exposes.** The references (companion_protocol.md, payloads.md, meshcore_py) contain **no GPS enabled flag and no fix state**: SELF_INFO has only the advertised latitude/longitude, `adv_loc_policy` and a telemetry-mode bitfield (location telemetry), none of which says a GPS receiver is on or has a fix.
  The only generic channel is `CMD_GET_CUSTOM_VARS` (0x28, answer `RESP_CODE_CUSTOM_VARS` 21, text `key:value,key:value`). **Chosen**: the app asks it and treats a variable called `gps` with value `1/on/true/yes/enabled` as "GPS enabled". The name `gps` is **not confirmed by the references**: it is my assumption from the firmware's sensor settings.
  If the board answers "unsupported" or has no `gps` variable, the app treats it as "no GPS" (rows 1 and 2 of the table); the Status line then shows "GPS off" or "GPS ?" (the latter when the board did not answer). A GPS fix cannot be read at all: if the GPS is on but the board clock is before 2025 the app warns "GPS is on but the board clock looks unset (no fix yet?)", does not use that clock, and keeps the deck time.
  **The verifier should read the real board's custom variables read-only** (see below) to learn which row applies to this board and whether the variable exists.
* **Board clock as the time base.** When the board clock is the source (GPS, refused write, Esc at the prompt), `now_unix()` = deck clock + offset (offset = board - deck at the read), so message times and "Heard" ages use the board clock; after a write the offset makes the app clock equal what was written (the prompt case: the deck clock itself is wrong). The offset is reset to 0 for the unset-clock case.
* **Refused SET_TIME** (the firmware only moves its clock forward; error 6): notice "The board refused that time: its own clock is ahead (it only moves forward). Using the board clock", then GET_TIME and the offset as above, source "board clock, unchanged". Other errors: "The board refused the clock: ...". No answer: "The board did not answer the clock setting".
* Status has a **Clock** row ("deck NTP  2026-10-06 21:14  GPS off"); Settings has **Sync clock now** (re-runs everything, including the prompt). The old flag `Client::options().set_device_time` is kept (default true = policy on; false = never write and never ask, the board clock is read and used).
* The prompt is typed on the Bluetooth keyboard only (same editor as the others; digits, `-`, `:`, space; format `YYYY-MM-DD HH:MM` in the deck's time zone, 2025 to 2099). Without a keyboard the app skips the prompt, says "Keyboard needed to set the board clock: Settings > Sync clock now" and uses the board clock without writing.
* Unit tests cover every cell with fake deck/board states (online/offline/unknown x no GPS/GPS/not reported), typed time written and offset, Esc skip, GPS with an unset board clock, refused write (ahead) and other refusals, a deck check that is slow or never answers, and Sync clock now after the deck got NTP; plus the date parsing (UTC and Europe/Paris), the NTP/nmcli output parsers and the custom-variable parser.
* **Simulator** (`tools/meshcore_sim.py`): `--gps` (reports `gps:1`), `--no-custom-vars` (answers unsupported), `--board-clock-offset SEC` (board behind: negative, e.g. `-13500000` about 5 months; ahead: positive), `--refuse-set-time`; a SET_TIME in the past is always refused like the firmware. The deck side (NTP yes/no) comes from the deck itself: to play "offline" use a deck without NTP (`timedatectl set-ntp false` needs root: ask first) or stop network time on the test deck; the unit tests cover it with fakes.

## 3. Minor items of report 012

a. Typed editors ignore an Enter within 350 ms of opening (the double Enter that showed "Add a name after the #").
b. **Negative TX power**: `SelfInfo.tx_power` is now decoded as a signed byte (meshcore_py reads it unsigned); the editor/stepper range was already -9 to the board's maximum; `build_set_tx_power(-5)` = `0c fb ff ff ff`. Test: a frame with 0xFB decodes to -5, validation (-10 refused). **Crash cause** (simulator): `bytes([...self.txp...])` with a negative int raises `ValueError: bytes must be in range(0, 256)`; the simulator now masks `& 0xFF`.
c. registry.json LF: fixed as described at the top.

## 4. Esc rule (new user rule)

A short Esc is always Back and never quits the app. The only code that could end the app (`navigate_home()` and the `g_quit` flag) is removed from the Esc path; `meshcore_quit_requested()` stays but is never set. Holding Esc for 3 s is detected by the launcher itself (`cp0_esc_exit_policy`: hint after 0.5 s, terminate after 3 s, kill after 3 more; the state comes from the keyboard thread through `cp0_esc_state`, independent of the app), so the app does nothing for it and does not interfere: it handles Esc only on the short key event.
The mapping is a pure function (`nav.hpp::esc_pressed(screen, tab)`), unit tested for every screen.

| Screen | Short Esc |
| --- | --- |
| Status, Chats, Contacts, Settings (top level) | nothing changes; footer says "Hold Esc 3 s to exit" for 4 s |
| Chat (opened from Chats or from Contacts) | back to that list (Chats, or Contacts) |
| Contact detail | back to Contacts |
| Any editor (compose, node name, number, hashtag channel) | cancel the editor, back to where it was opened (Chat, Chats, Settings) |
| Board-clock prompt (typed editor) | skip: the board clock stays as it is, back to the screen underneath |
| Remove "Sure?" confirmation | Esc in the chat goes back to the list (the confirmation just expires) |

Footers now read "Hold Esc: exit" on Status and Chats and "Esc: back" / "Esc: cancel" / "Esc: skip" elsewhere.

**How the other apps handle Esc today** (for the question to the user): **Wi-Fi Survey** (README, code `back()`): Esc closes the detail, otherwise **quits on a short Esc** (`navigate_home`). **LAN Scan** (`lanscan.cpp::back()`): Esc leaves the ports view, otherwise calls `navigate_home()`: **also quits on a short Esc** at the top (footers say "Esc: back"). viz1090: per the coordinator, intro and map return to the launcher on Esc (not read by me). So none of the three follows the new rule yet, MeshCore is the first.

## What `deck-verifier` must re-check

### Simulator (all of it testable there)
1. Clock cases with `meshcore_sim.py`: (a) default (no GPS reported as `gps:0`, deck NTP yes): Status Clock "deck NTP ... GPS off", the sim prints "board clock set to ..."; (b) `--gps`: no write, "board GPS ... GPS on"; also with a deck that is online (the confirmed default) and with `--board-clock-offset -13500000` (board months behind: "Heard" ages and message times follow the board clock);
   (c) `--no-custom-vars`: "GPS ?" and the no-GPS rows; (d) `--board-clock-offset 600 --refuse-set-time` or a board ahead: the "refused ... ahead" notice, source "board clock, unchanged", times follow the board; (e) deck offline (no NTP): the typed prompt opens by itself at connect with the deck's date/time prefilled, edit it, Enter: the sim prints the written time, Status "set by you", times follow; Esc: "Board clock left unchanged", source "not set"; bad text ("2026-13-01 10:00", "abc") refused in red;
   (f) Settings > Sync clock now re-runs the policy (prompt again when offline); (g) no keyboard (BT keyboard asleep): the prompt is skipped with "Keyboard needed to set the board clock: Settings > Sync clock now".
2. "Keyboard needed" with the real listing: remove the keyboard (symlink/uinput device not named `applaunch-*`) and open an editor: the message appears and no editor opens; with the keyboard present editors open.
3. Negative TX power: set -5 in the simulator and Save: no crash, Status shows "-5 dBm" after read-back. Double Enter on "+ Add hashtag": no "Add a name after the #" flash.
4. Esc on every screen as in the table above; a short Esc at Status/Chats/Contacts/Settings never leaves the app and shows the hint; holding Esc 3 s still returns to the launcher (the launcher's behaviour) from every screen including the editors.
5. Packaging: md5 `95928f50ce8d1863aa08db976db80a8a`, registry diff only the meshcore entry, LF endings, copyright unchanged, version 0.1.3.

### Real board, read-only (no clock write unless the user later allows it)
* **Do not press Sync clock now and do not accept the clock prompt on the real board**: the app's own policy at connect may write the clock (deck NTP + no GPS reported = SET_TIME, forward only). The real board was set by earlier runs, so it equals the deck: a forward-only write is harmless, but the verifier should note it and may start the app with the real board only after reading as below.
* Read what the board reports about GPS and the clock with your own probe (same open sequence): `28` GET_CUSTOM_VARS (expect `21` + text: report the whole text, whether a `gps` key exists and its value) and `05` GET_TIME (compare with the deck: `timedatectl`). From these say which row of the table applies to this board (`gps` absent or 0 = rows 1/2; the board is online-synced deck: SET_TIME row).
* Connect with the app and check Status "Clock" and "GPS on/off/?" agree with your probe, that the keyboard check sees the M4 keyboard when awake and does not when asleep, and that nothing else was written (channel table digest, name, radio parameters identical).

### Needs the user's touch / real keyboard
Esc behaviour with the user's own keyboard (short Esc vs hold 3 s; whether the launcher's hint appears at 0.5 s as expected with the app's footer hint), the clock prompt with the real keyboard, "Keyboard needed" with the real asleep M4, the touch items of earlier rounds, and the still-open items: channel text limit near the limit (needs one real message), case of hashtag names (`#Test` vs `#test`), whether the `gps` custom variable is what the user's board uses.

## Needs from the Controller

* Ask the user whether Wi-Fi Survey, LAN Scan and viz1090 get the same Esc rule (short Esc = Back only, hold 3 s to exit).
* Decision whether the custom variable name `gps` should be confirmed from the MeshCore firmware source (companion radio `getCustomVar` / sensor settings) before relying on it.

---

# 007 MeshCore: hand-off, ROUND 3 (rounds 2 and 1 are kept below)

```
Task: 007 meshcore round 3        Agent: apps-dev        Result: done (built and unit tested; UI changes not run on a screen by me)
```

## Changed (round 3)

`C:\CLAUDE\zero7\cyberdeck-zero-apps`, branch `meshcore`, nothing committed. Version **0.1.2**: `packages/meshcore_0.1.2_arm64.deb` (1 402 886 bytes, md5 `cfaf7a43d9e12f6deaa102182bc1d0a4`,
equal to the registry entry). The 0.1.1 deb is removed from `packages/` (never published). `registry.json`: only the `meshcore` entry (0.1.2) and `generated_at` differ from HEAD; the lanscan,
viz1090 and wifi-survey debs and entries are restored to HEAD. Built under the WSL lock, 0 warnings. Launcher untouched.

* New `core/input_state.{hpp,cpp}`: `is_delete_key()`, `keyboard_present()` / `keyboard_listed()`.
* `core/client.*`, `core/model.*`: marker reconciliation, `Client::Options` + `maybe_set_device_time()`, `ChannelRec.key_ok`.
* `ui/meshcore.*`: the on-screen keyboard is gone; typed numeric editors; Del; keyboard-needed message; tap/Enter debounce.
* `root/usr/share/doc/meshcore/copyright`, `README.md`, `app.json`, `tests/test_core.cpp`.

## Product rule: no on-screen keyboard

* All of it is removed: `lv_keyboard_create`, its styling, `kb_event_cb`, the number-pad mode, the byte-trimming that existed only for it, the README/app.json/footer mentions. The binary no longer references it
  (the editor still uses an `lv_textarea` as a plain text display; it is not clickable and takes no input from touch).
* The editor (compose, node name, hashtag channel, numbers) now takes the whole area below the header: 112 px of text at the 14 px font instead of the 38 px box (about 5 lines instead of 2), and the footer stays visible
  ("Type on the keyboard   Enter: send   Esc: cancel", for numbers "Digits . -   Enter: accept   Esc: cancel"). Header buttons: `< Back`/Cancel and Send/OK/Add stay (touch can still send an already typed text).
* Typed numeric editors (Enter, or a tap on the row outside the `-` `+` zones): Frequency, Bandwidth, Spreading factor, Coding rate, TX power; digits, `.` (a `,` is turned into `.`) and `-`; Backspace/Del erases; Enter accepts, Esc cancels.
  Validation = the steppers' rules: frequency 137-2500 MHz, bandwidth one of the LoRa list (7.8 ... 500, a close value is snapped), SF 5-12, CR 5-8, TX power -9 to the board's maximum; a refusal stays in the editor with the reason in red.
  The `-` `+` steppers (0.025 MHz, list steps, +-1) are unchanged and need no keyboard.
* **Keyboard needed**: opening any editor first checks `/proc/bus/input/devices` for an input device with the letter keys, Enter and Space (a sleeping Bluetooth keyboard is not listed; checked at most once a second). If none:
  footer message (red, 4 s) **"Keyboard needed: wake the Bluetooth keyboard"** and no editor opens. If the file cannot be read the app assumes a keyboard is there (never blocks on a guess). Everything that needs no typing
  (tabs, rows, Chats/Contacts, adverts, EU 868 preset, steppers, Save, Remove, Send of a typed text) is unaffected. The check is a heuristic on kernel input devices: the launcher exposes no "keyboard present" API.
  A keyboard that goes to sleep while an editor is open leaves the editor open (text kept, Send/Back by touch).
* The launcher's per-app touch gestures (Settings > Touch: drag = Up/Down, tap = Enter) arrive as ordinary keys. Since my own tap handlers also act on a tap, an Enter within 350 ms of an opening/navigation is ignored so one tap
  never acts twice (e.g. opening a chat and then immediately the editor). Drag scrolling by my own handler plus the gesture's Up/Down can add up on lists; not guarded (cosmetic): the verifier should look at it with the gesture on.

## Defects from report 011

1. **Del**: the launcher's raw keyboard event carries the evdev code 111 (`key_item.key_code`) while the LVGL keypad path carries `LV_KEY_DEL` 127. Cause of the bug: the app listened for 111 on the LVGL path, where it never
   arrives (the launcher converts it). Fix: Del is handled once, in the raw handler (`handle_raw_key`, pressed state, Chat view), with `is_delete_key()` accepting 111 and 127; the LVGL path no longer handles Del (handling both would confirm the
   two-tap removal by itself). The Remove button keeps its two taps ("Sure?" within 4 s). Regression test: `is_delete_key(111) && is_delete_key(127)` and not Backspace (14, 8) in `test_core`; the key plumbing itself is UI and not host testable.
   Keys: letters A, D, U, M, S, V are raw key codes in the raw handler (unchanged, they worked in 011: W/E/R/T are not used); in text editors every key comes from the raw handler with the launcher's keypad path intercepted.
2. Cut-off keyboard: gone with the keyboard.
3. **Copyright**: the credits are now the header paragraph's `Comment:`; every other paragraph has `Files:`, `Copyright:`, `License:` (plus the standalone `License: MIT` text paragraph at the end, which is valid DEP-5 and has no `Files:` by design).
   Structural check done by script (7 paragraphs: header with Format/Upstream-Name/Source/Comment; 5 `Files:` stanzas each with Copyright and License; the License paragraph). `lintian` was not available.
4. **Stale "added by this app" markers**: at connect, after the last of the channel slots was read (GET_CHANNEL up to max_channels), `Model::reconcile_added()` drops every marker whose name is not in a non-empty slot, or whose slot holds that name
   with a key other than the hashtag key (`ChannelRec.key_ok` is read live from the board's reply). So `#zero7test` disappears at the next connect. Unit tests: markers for a missing name and for a same-name channel with another key are dropped
   (returns 2, then 0); at a replug a marker backed by the board (slot 5, right key) stays and one for a name that is not on the board goes; a removed-by-someone-else channel is forgotten.
   **Cleaning the stale file on the deck by hand** (optional now): the history directory is `~/.local/share/meshcore/` (`$MESHCORE_DATA`, else `$XDG_DATA_HOME/meshcore`). `channels.jsonl` has one JSON object per line: slot lines
   `{"idx":2,"nm":"#zero7test","em":0}` and marker lines `{"ad":"#zero7test"}`; delete the line `{"ad":"#zero7test"}` (e.g. `sed -i '/"ad":"#zero7test"/d' ~/.local/share/meshcore/channels.jsonl`, app closed). Other files: `messages.jsonl`
   (`{"i":seq,"k":"c:0"|"d:<12 hex>","d":0|1,"t":ts,"s":sender_ts,"n":sender,"x":text,"st":state,"h":hops,"snr":..,"e":note}` plus `{"u":seq,"st":n}` state updates), `contacts.jsonl`, `read.txt` (`<conversation> <last read seq>`), `lastport`,
   optional `port`.

## SET_TIME isolated (behaviour unchanged)

All of the clock writing is in `Client::maybe_set_device_time()` (client.cpp), called once from the connect sequence, and gated by `Client::options().set_device_time` (default **true** = the 0.1.0/0.1.1 behaviour: send the deck's clock at every connect
when the deck's year is 2025 or later). The decision is a one-line change in that function: never = default `false`; "only if more than one day off" = compare with the time the board reported (the app does not read GET_TIME today: add a `05` GET_TIME
request before the write and send only when `abs(board - deck) > 86400`). A test checks that with the flag off no `06` command is sent. Remember: the firmware only moves its clock forward; the 157 d "Heard" ages came from the board's old clock.

## What `deck-verifier` must re-check

With the user's real-board limits as before (read-only; no Save, no name change; the one advert and one Public message were already used in 011: no more air traffic without the user). Add / Remove hashtag channel on the simulator unless the user agrees.

1. Install 0.1.2: md5 `cfaf7a43d9e12f6deaa102182bc1d0a4`; `dpkg-deb -I` version 0.1.2; copyright structure (all paragraphs valid, `lintian --check` if available).
2. **No on-screen keyboard anywhere**: open compose (Chat > Enter / Message), Add hashtag channel (Chats), node name (Settings row 0), frequency, bandwidth, SF, CR, TX power: only the text area, no keyboard, no cut-off.
3. **BT keyboard entry in each editor**: compose (letters, accents, space, Backspace, Del, Enter sends, Esc cancels, "N left" counts bytes and stops at the limit); channel (`#` prefilled, space refused, Enter adds); name (Enter OK, 31 byte limit); the five numeric editors
   (digits `.` `-`, Enter accepts and the row turns gold as unsaved, bad values refused in red: e.g. frequency 100, bandwidth 100, SF 4, CR 9, TX power above the board's max).
4. **Del**: in the chat of an app-added channel on the simulator: Del once shows "Sure?", Del again (within 4 s) removes it and returns to Chats; the footer says "Del: remove channel"; in a Public/#test chat Del does nothing.
5. **Touch buttons**: tabs, `< Back`, Message, Send/Cancel/OK/Add, Remove (two taps), Advert buttons, `-` `+` zones, Preset EU 868, Save/Undo rows; tapping a settings row opens the typed editor; with Settings > Touch = Swipe + Fire for MeshCore check that a tap never acts twice and drag scrolls.
6. **No keyboard**: with the BT keyboard asleep (or the uinput keyboard removed and the launcher restarted) tap Message, a Settings number row, the node name row, "+ Add hashtag channel": each shows "Keyboard needed: wake the Bluetooth keyboard" and opens nothing;
   tabs, scrolling, adverts, preset, steppers, Save keep working.
7. Marker reconciliation: put `{"ad":"#zero7test"}` into `channels.jsonl`, connect (real board or simulator): the marker is gone after connect (the line disappears from the rewritten file when the channels are saved).
8. Everything else from the earlier plans at 0.1.2 (accents, contact ages "Heard" unchanged by design, unplug/replug, Esc).

**Needs the user's touch / real keyboard**: the touch buttons and the gesture interplay above, the Remove double tap, the editors with the user's own Bluetooth keyboard (key mapping of Del, Backspace, `.` and `-` on that keyboard's layout), the keyboard-asleep message,
and the decision on the SET_TIME policy. Still unconfirmed: the channel text limit with a long node name (needs one real near-limit message) and the case of hashtag names.

---

# 007 MeshCore: hand-off, ROUND 2 (round 1 is kept below)

```
Task: 007 meshcore round 2        Agent: apps-dev        Result: done (built and unit tested; UI still never run on a screen)
```

## Changed (round 2)

`C:\CLAUDE\zero7\cyberdeck-zero-apps`, branch `meshcore`, nothing committed. Package bumped to **0.1.1**: `packages/meshcore_0.1.1_arm64.deb`
(1 400 450 bytes; the 0.1.0 deb was removed, it never left this branch). `registry.json`: only the `meshcore` entry added (version 0.1.1) and
`generated_at`; lanscan, viz1090, wifi-survey debs and entries restored to HEAD. Launcher repo untouched. Built under the WSL lock, 0 warnings.

* `core/sha256.{hpp,cpp}` (new): SHA-256, `hashtag_secret(name)`, `validate_hashtag(name)`.
* `core/client.*`: `add_hashtag_channel(name)`, `remove_channel(idx)`. `core/model.*`, `core/store.*`: `ChannelRec.app_added`, names of added channels
  persisted in `channels.jsonl` (`{"ad":"#name"}` lines), `free_channel_slot()`.
* `core/serial_transport.*`: open sequence reduced (risk a).
* `ui/meshcore.*`: "+ Add hashtag channel" row in Chats, Remove (two taps, or Del) in the chat of an app-added channel, Preset EU 868 row,
  band hint, remaining-bytes counter in the editor, font loading.
* `tests/test_core.cpp` (167 checks, was 136), `tests/test_sim.cpp` (29, was 24), `README.md`, `app.json`. All pass (`run_tests.sh`).

## 1. Hashtag channels

The derivation is in the references, no guess needed: `meshcore_py/commands_device.py::set_channel`:
`if channel_name.startswith("#") or channel_secret is None: secret = sha256(channel_name.encode("utf-8")).digest()[0:16]`, and
`companion_protocol.md`: "first 16 bytes of sha256("#test")", example key `9cd8fcf22a47333b591d96a2b848b73f`. So the hashed string is the name **as typed, including
the `#`, case preserved**. A unit test checks exactly that vector. Not settled by the references: whether MeshCore apps lower-case the name before hashing
(`#Test` and `#test` would then be one channel for them but two for this app). The app does not change the case.

Flow: name entry (BT keyboard or on-screen keyboard; a missing `#` is prepended; spaces refused; 30 bytes at most with the `#`) -> refused if the name is already on the radio or
no slot is free -> `20 <slot> <name, 32 bytes NUL padded> <16 byte key>` into the **first free slot with index 1 or more** (slot 0 stays for Public) -> `1F <slot>` to read it back ->
it appears in Chats. Removal: only a channel this app added (its name is in the persisted list): `20 <idx>` with an empty name and a zero key (doc: "Delete Channel"), then `1F`.
Messages of a removed channel stay in the history under `c:<idx>`; a channel added later into that slot would show them (history is keyed by slot).
Private channels with a 16-byte key: **not in v1** (no UI to enter a key).

Tests: SHA-256 against the RFC 6234 / FIPS 180-4 vectors (empty, "abc", 448-bit, 896-bit, one million "a") and padding edges at 55/56/64 bytes; the documented `#test` key; add into the
first free slot with the right key bytes on the wire; duplicate and bad names refused; add refused by the radio leaves nothing marked; removal only for app-added channels (`#test` and
Public refused); slot reuse; persistence of the added list; and add/remove through the real serial path against the simulator.

## 2. EU 868

* Frequency step 0.025 MHz (Left/Right and the `-` `+` zones); typed entry still possible (generic 137 to 2500 MHz check).
* Band hint: with the Frequency row selected the footer says "EU 868 band (863-870 MHz), step 0.025 MHz" or, in red, "Outside the EU 868 band (863-870 MHz)!"; the value turns red outside the band
  (a warning, not a block). 863-870 MHz is the usual EU 868 band, it is not read from the board.
* "Preset EU 868" row (Settings, active only when connected): sets the edited frequency to 869.525 MHz and nothing else; bandwidth, SF and CR keep what the board reports. It still needs
  **Save to radio** (S) to write: the preset itself sends nothing.
* Validation against what the board reports: TX power up to the board's `max_tx_power`; bandwidth from the LoRa list, SF 5-12, CR 5-8. SELF_INFO reports no frequency limits.
* 24 h clock and metric units (ages, volts, MHz) were already used; the clock is the deck's local time.

## 3. Risks revisited

a. **DTR/RTS**: the explicit modem-line change (DTR on, RTS off) is removed. The open is now: `open(O_RDWR|O_NOCTTY|O_NONBLOCK)`, flock, raw termios 115200 8N1, CLOCAL, no flow control, input flush.
   Nothing else: no baud change (never 1200), no DTR/RTS toggling. This is the sequence of the coordinator's probe (no reset of the board). Documented in `serial_transport.hpp`.
b. **Font**: the launcher bundle ships `share/font/DejaVuSans.ttf` (installed under `/usr/share/APPLaunch/share/font/`); the launcher's own pages get it through `cp0_fonts()` by bare name. LanScan and
   Wi-Fi Survey use only the built-in Montserrat (no accents), so there is no app mechanism to copy. The app now tries the absolute path `/usr/share/APPLaunch/share/font/DejaVuSans.ttf`, then the Debian
   `fonts-dejavu-core` path, then the bare name, and `cp0_fonts()` falls back to Montserrat if nothing loads (accents would then be boxes, nothing crashes). Message text, names and list text use it;
   headers, tabs and buttons stay Montserrat. I could not look at the deck's filesystem: the verifier must check that accents render.
c. **Channel text limit**: estimate kept (direct 133 bytes; channel `150 - len(node name) - 2`, between 40 and 133). The editor enforces it in bytes (also against the on-screen keyboard) and shows "N left"
   (red at 10 or fewer). **To confirm with one real test message near the limit.**

## What `deck-verifier` must re-check with the real board (user limits: read-only commands, ONE advert, ONE short test message on Public; no Save to radio, no settings changes)

Allowed on the real board: connect, Status, contacts, channel list, message sync (reads the radio's queue), battery, accent rendering, key and touch navigation, the editor screens (cancel or Undo), and
**one** advert and **one** short message on Public (channel messages have no ACK: "(sent)" is the only state to expect).

Not on the real board (use `meshcore_sim.py`): Save to radio, node name change, any second advert or message. **Add / Remove hashtag channel write the board's channel table (SET_CHANNEL)**: not radio "settings",
but they change the stored configuration, so only with the user's agreement; test them on the simulator.

Re-check list:
1. Real board: connects by USB id 2886:8044, Status shows model, firmware and battery; the board is not reset at open (the Status must not bounce to "Connecting" again); contacts and 40 channel slots read; waiting messages synced.
2. Accents render (a received or typed message with e.g. "é", "ç") rather than boxes; if boxes, report which font path exists on the deck.
3. The one short test message on Public: the editor shows "N left" correctly; the message ends "(sent)".
4. Channel limit: the user must decide whether that single message may be a near-limit one (suggest about 100 characters, short node name) so the estimate can be confirmed; otherwise it stays unconfirmed.
5. The one advert: notice "Advert sent".
6. Simulator: add `#eu868` (BT and touch keyboard), it shows in Chats, open it, Remove (two taps, or Del), duplicate name and no-free-slot messages (fill slots by adding many channels).
7. Simulator Settings: step 0.025 MHz, footer hint turns red below 863 and above 870 MHz, Preset EU 868 changes only the frequency, Save then read-back.
8. Editor refuses text beyond the limit (byte count, also with accented text typed on the on-screen keyboard).
9. Everything from the round 1 plan (simulator part) again, version 0.1.1.

## Needs from the Controller / open questions

* Decision on item 4 (near-limit test message).
* Case of hashtag names (section 1): fetch from the MeshCore/meshcore-cli source how it normalises a `#name` before hashing (look for the code calling `set_channel` for a name starting with `#`), or test with a channel
  created by another MeshCore app.
* Confirm the user accepts Add / Remove hashtag channel on the real board (it writes the channel table), otherwise simulator only.

---

# 007 MeshCore: hand-off

```
Task: 007 meshcore        Agent: apps-dev        Result: done (built and unit tested; UI never run on a screen yet)
```

## Changed

All in `C:\CLAUDE\zero7\cyberdeck-zero-apps`, local branch `meshcore` (created with Windows git, nothing committed or pushed).

* `apps/meshcore/` (new), same structure as `wifi-survey`:
  * `app.json` (share_code and package `meshcore`, version 0.1.0, title "MeshCore", category Radio, MIT, depends none), `icon.png` (64x64), `README.md`.
  * `build/build.sh` (launcher worktree scratch project `projects/MeshCore`, WSL lock, copies `dist/MeshCore` to the package tree).
  * `src/main/core/` (no LVGL): `frame` (framing), `protocol` (builders and decoders), `transport.hpp` (interface),
    `serial_transport` (termios USB serial), `model`, `store` (JSON lines history), `client` (session and queue), `util`.
  * `src/main/ui/meshcore.{hpp,cpp}` (LVGL pages), `src/main/src/main.cpp`.
  * `src/tests/test_core.cpp`, `test_sim.cpp`, `run_tests.sh`.
  * `tools/meshcore_sim.py` (simulated companion radio over a pty).
  * `root/usr/share/APPLaunch/{bin/M5CardputerZero-meshcore, applications/meshcore.desktop, share/images/meshcore.png}`,
    `root/usr/share/doc/meshcore/copyright` (Debian format, Files: on every stanza).
* `packages/meshcore_0.1.0_arm64.deb` (new) and `registry.json` (only the `meshcore` entry added, plus `generated_at`).
  `make_registry.py` rewrote the lanscan, viz1090 and wifi-survey debs: they were restored to HEAD (`git status` shows only
  `registry.json` modified and the two new paths). The registry diff was merged by hand to keep the other entries byte-identical.
* Launcher repo: no source touched. Like the wifi-survey build, `build.sh` appends `projects/MeshCore/` to the launcher's local
  `.git/info/exclude` (untracked scratch project).

## How it was checked

* `wsl -e sh apps/meshcore/src/tests/run_tests.sh`: `test_core` 136 checks, 0 failed; `test_sim` (real `SerialTransport` + `Client` +
  `Store` against `tools/meshcore_sim.py` over a pty: handshake, 4 contacts, 40 channel slots, message sync, DM sent then delivered,
  echo, channel message, settings round trip, advert, unplug and replug, history reload after restart) 24 checks, 0 failed.
  Compiled with `-Wall -Wextra`, no warnings.
* Cross build under `flock /tmp/wsl-build.lock` (build.sh): `scons: done building targets`, 0 warnings, aarch64 ELF installed.
* `dpkg-deb -I` / `-c` on `packages/meshcore_0.1.0_arm64.deb`: 4 files plus dirs as laid out above.
* NOT done: the LVGL UI was not run (no SDL on the build host, no screen). Layout, colours, touch zones, the on-screen keyboard and
  the text editing are untested until the deck. Screenshots: none yet.

## .deb

`packages/meshcore_0.1.0_arm64.deb` (1 386 920 bytes, binary `M5CardputerZero-meshcore`, 6.0 MB not stripped).

## Protocol decisions (what the app sends)

Framing: device to app `0x3E` + u16 LE length + payload; app to device `0x3C` + same. Bytes outside a frame are skipped; a `0x3E`
followed by length 0 or above 300 is dropped one byte at a time (meshcore_py drops 3). Received limit 300 (meshcore_py), sent limit 172
(firmware MAX_FRAME_SIZE, doc). 115200 8N1, no flow control, DTR asserted and RTS released after open (as meshcore_py).

Start-up (queue, one command in flight, matched by response code, 2 to 8 s timeouts, 3 consecutive timeouts drop the link):
1. `16 03` DEVICE_QUERY (repeated up to 5 times: the board may be rebooting after the port opened; silence gives "Not a companion radio").
2. `01 03 + 6 spaces + "mcdeck"` APP_START, answer SELF_INFO.
3. `06 <unix>` SET_TIME only when the deck clock is after 2025-01-01 (the firmware refuses to go back: an error is harmless).
4. `14` battery, `04` GET_CONTACTS (CONTACT_START / CONTACT* / CONTACT_END, a timer renewed per frame), `1F n` GET_CHANNEL for n below
   max_channels (cap 40), then `0A` SYNC_NEXT until NO_MORE_MSGS. Again on every push 0x83 and every 30 s. Battery every 60 s (also the liveness probe).

Then: advert `07` (zero-hop) / `07 01` (flood); direct message `02 00 <attempt> <ts> <6 byte key prefix> <text>`, answered by MSG_SENT
(expected ack, suggested timeout) then the ACK push marks "delivered"; no ACK after 1.2 x suggested timeout: attempt 1, then (path reset
`0D <key>` if the contact had a route) attempt 2, then "no ack". Channel message `03 00 <idx> <ts> <text>`; the sent state comes from OK or
MSG_SENT. Settings: `08` name, `0B` radio (kHz and Hz, rounded not truncated), `0C` TX power, then APP_START again to read back; `20` SET_CHANNEL
only for "Add the Public channel" (slot 0, well known key). `1E <key>` GET_CONTACT_BY_KEY after an ADVERTISEMENT push to learn the new name.
Pushes handled: 0x80, 0x83, 0x82, 0x8A, 0x8F, 0x90; 0x88 (RX log) and 0x81 are decoded and ignored; unknown codes never break the stream.
V3 message frames (SNR) are used because APP_START byte 1 is 3.

### Where the doc and meshcore_py disagree (meshcore_py followed)

1. APP_START: doc says bytes 1-7 reserved/ignored; meshcore_py sends `01 03` + 6 spaces + name. Byte 1 is the app version (3), needed for V3 frames.
2. SEND_CHANNEL_MESSAGE: doc says MSG_SENT; meshcore_py waits for OK. Both are accepted.
3. ACK push: doc says a 6 byte code; meshcore_py reads a 4 byte code then a 4 byte round trip time.
4. SELF_INFO latitude/longitude: doc pseudocode reads unsigned; they are signed (meshcore_py).
5. Frame size: doc 172; meshcore_py accepts up to 300 received.
6. Doc describes BLE only (one frame per notification, MTU, GATT); serial framing comes from meshcore_py `serial_cx.py`.
7. The doc's DEVICE_INFO only describes fields up to the version string; a firmware-level 11 board sends 2 more bytes (repeat flag, path hash mode) after it,
   decoded as meshcore_py does (`fw >= 9`, `fw >= 10`). Real vector below.
8. Channel text limit: doc says 133 characters; the firmware prepends "<node name>: " to channel text, so the app limits channel text to 150 minus
   name length minus 2 (at most 133). Unconfirmed against firmware source.

### Real board facts (coordinator probe, used as the golden vector and in the simulator)

Seeed XIAO nRF52840 (2886:8044), companion v1.15.0-dee3e26, DEVICE_INFO fw level 11, max_contacts 350, max_channels 40, ble pin 0, build "19-Apr-2026",
model "Seeed Xiao-nrf52". It pushed a 0x88 log frame (length 100 here) before any query: handled (parser resync plus push decoding); a test feeds exactly that shape.
`test_core.cpp::test_real_device_info` decodes the 85-byte frame the board sent; the simulator replies with the same payload.

## Test plan

### Simulator (verifier, deck, no board)
1. Copy `apps/meshcore/tools/meshcore_sim.py` to the deck, `python3 meshcore_sim.py --echo --chatter 30 &` (creates `/tmp/ttyMC0`), then
   `echo /tmp/ttyMC0 > ~/.local/share/meshcore/port` (delete that file or set nothing for the real board). Options: `--drop-acks`, `--fail-send`, `--ack-delay`, `--noise`.
   stdin commands: `msg`, `chan`, `advert`, `unplug`, `plug`, `quit`.
2. Install the .deb through Settings > Apps, launch MeshCore. Expect: Status shows "Connected", board "Seeed Xiao-nrf52 v1.15.0-dee3e26", node SimNode, radio 869.525 MHz BW 250 SF11 CR 4/5 22 dBm.
3. Chats tab: Public, #test, Alice with unread markers (2 waiting messages); open one, unread clears. Contacts: Alice, Bob, Hilltop repeater, Weather room; Enter on Alice opens the chat, on the repeater the detail.
4. Compose with the BT keyboard (Enter sends, Esc cancels, accents) and with the touch keyboard; status goes sending, sent, delivered (and "got: ..." comes back with --echo). With `--drop-acks`: retries then "no ack". With `--fail-send`: "failed: radio queue full".
5. Settings: change SF and frequency (Left/Right, tap `-` `+`), edit the name (Enter, on-screen keyboard), Save: values read back; Undo. Frequency edit with a typed value.
6. Unplug test: `unplug` then `plug` on the simulator stdin: Status "Board disconnected", cached contacts and history remain, reconnects by itself.
7. Error states: remove the `port` file with no board: "No board found"; `chmod 000` on the pty or a wrong group: "Permission denied" hint; start the simulator twice with the same link or hold the port with `cat`: "Port in use".
8. Close and reopen the app: history and unread marks kept (`~/.local/share/meshcore/*.jsonl`).
9. Keys: W/E/R/T are not used; letters A, D, U, M, S, V work by raw key code; Tab switches tabs; Esc backs out then quits.

### Real board (read-only first)
Plug the XIAO (stable path `/dev/serial/by-id/usb-Seeed_Studio_XIAO_nRF52840_...-if00`; auto-detect picks it by USB id 2886). Read-only checks: connection and Status page, contact list,
channel list, message sync (this removes messages from the radio's queue: they are stored on the deck), battery. Do NOT use Save to radio, Add the Public channel, or send messages/adverts
until the user agrees (they change the radio or transmit on air). The app itself sends SET_TIME at connect (harmless, forward only) and nothing else unprompted.

### Needs the user's touch
Tabs, row taps, swipe scroll in lists and chats, `-` `+` zones in Settings, header Message/Send/Cancel buttons, the on-screen keyboard (size, readability, number mode for the frequency),
the Advert buttons, tap targets at 2x scale.

## Risks

* UI untested on a screen (layout overflow, label truncation, the on-screen keyboard theme, DejaVu font loading via `cp0_fonts()` with Montserrat fallback: accents may show as boxes if the font does not resolve).
* Opening the port asserts DTR/RTS before the app can release RTS. On ESP32 native-USB boards that can reset the board; the nRF52 XIAO needs DTR for data. The first DEVICE_QUERY is retried for this reason.
* ModemManager (if installed) probes new ttyACM devices with AT commands and can hold the port: shown as "Port in use".
* Contact "last heard": the deck's clock of the last advert or message, else the radio's lastmod, else the node's own clock (may be wrong). Without NTP the deck clock is unset: ages show "-".
* Channel messages carry the sender only as "Name: text" inside the text; conversations are keyed by channel index (`c:<idx>`): reassigning a slot mixes histories.
* Direct messages to repeaters/sensors (CLI commands, logins), room-server login, contact add/remove/share, path control, map, BLE and file transfer are not in v1.
* Pending messages at app exit are stored as failed ("interrupted").
* Package depends on nothing, but the `dialout` group is required (default user has it); not stripped (6 MB).

## Open questions for the user

1. Board: the probe says Seeed XIAO nRF52840 (which carrier / LoRa module? only needed for the max TX power, the board reports it: confirm the 22 dBm shown on Status).
2. Channels: is the Public channel already in slot 0 and which other channels (hashtag channels need the name, private ones need the 16 byte key)? v1 can only add the Public channel; adding a hashtag channel needs SHA-256 (small) and a name entry: wanted?
3. Which region frequency plan (868.1 / 869.525 / 910.525 ...) so the settings steps (0.025 MHz) and defaults suit you?
4. May the verifier send an advert and a test message on air with the real board, and may it use Save to radio?
5. Category "Radio" (as viz1090) kept; confirm.

## Needs from the Controller

Hand to `deck-verifier` with the plan above; a launcher request is not needed. Screenshots of each screen (Status, Chats, Contacts, Chat, Compose, Settings, Detail) must be taken on the deck.
