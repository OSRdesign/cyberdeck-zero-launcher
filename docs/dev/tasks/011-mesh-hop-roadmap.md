# 011 - Mesh Hop: test fixes and the feature roadmap (draft brief)

Status: DRAFT 2026-10-07, waiting for the user's decisions at the end. Builds on task 010 (phase 1 delivered as mesh-hop
0.1.0, report 013). Inputs: the user's first deck test (`reports/013-user-feedback.md`), the user's feature picks and the
protocol feasibility pass (`reports/014-feasibility.md`, the source for every protocol statement below).
Owner: apps-dev. Verifier: deck-verifier or the Controller-led protocol (the user tests on the deck). Docs: docs-writer after acceptance.

## Rules that stay
No on-screen keyboard (BT keyboard for text; the user's QWERTY layout). Short Esc = Back, hold 3 s quits. Touch and keys for
everything. Package `mesh-hop` stays a draft. Never copy GPL code. `core/` remains LVGL-free and unit-tested. Every phase ends
with a hand-off report and a numbered deck test protocol for the user.

## Phase 1 - fixes from the first test, plus quick wins
Fixes (user feedback, all from `013-user-feedback.md`):
1. Unread total on the "Chats" tab title; channel and direct rows show an unread count instead of the last message.
2. Contacts: a "Loading..." popup while the list re-sorts; the sort is kept, and a tap opens the row that was tapped (the
   current bug opens the row at that position of the default order); Up/Down in a 161-contact list must be as fast as touch
   (virtualise the list, redraw only the visible rows).
3. Channel status: stop at "Sent" when the board accepts the message (channels have no ack, so never "Delivered").
   Check on the deck which frame the board returns for the channel send (report 014 says OK or MSG_SENT) and fix the state
   that stays on "sending". Direct messages keep Sending, Sent, Delivered, No ack.
4. Settings: replace `-`/`+` and free entry by a **choice popup**: the value in the middle with a large left arrow and a
   large right arrow, for bandwidth, spreading factor, coding rate and preset. Preset = the 26 entries of
   `api.meshcore.nz/api/v1/config` (report 014, answer b) kept as a dated data file (62.5 and 250 kHz must be accepted by the
   core). Hide the fixed Public channel from Settings; channels can be removed. Undo and Save to radio at the end of the list,
   each with a Yes / No confirmation box. Board GPS on/off when the firmware lists the `gps` custom variable (D6, see phase 2).
5. F1..F5 do nothing on the user's keyboard: keep Tab, add other keys for tab switching (to define with the user), remove the
   F-key hint from the UI. The left-edge swipe does not work and is not needed: remove it (and its hint).
Picked features that are cheap now: M3 (retry settings, direct only), M4 (mute a channel), C2 (filter contacts by type and by time since
heard), D1 (preset list, above), D10 (firmware version check: "too old" notice per feature), H1 (private and community
channels: 32-hex key entry, random key generation).

## Phase 2 - contact and channel management, radio and device
C3 bulk delete (one by one with a progress popup and a confirmation) and auto-add toggle; C4 discover nearby nodes (adverts heard
list, add or ignore; passive adverts plus an active zero-hop discover); C8 static contact details (key, last advert, position,
path); C1 contact groups (app side, with a backup file); M7 archive (search, filters, retention setting, export); D2 TX power,
repeat on/off where allowed (advert interval is an app timer, default off); D5 reboot and factory reset (double confirmation;
**no power-off command exists**); D6 board GPS on/off and position; D9 statistics (packets, airtime, errors, needs a newer
firmware).

## Phase 3 - packet-log features, paths, sharing, map
LOG_DATA parser in `core/` (needed once, reused by several features): **"heard by N repeaters"** on sent messages (channels
and direct) by counting echoes of our own message, with the honest label "heard back by N repeaters"; D8 packet log (app-side
capture with start / stop / clear / view, bounded); C5 share contact (export / import by `meshcore://` text and a QR code
display; no scanner); C6 path edit and reset, manual override, automatic route rotation (app logic); C7 trace path and ping;
R1 flood scope on the companion (region admin lives in the repeater terminal, phase 4); P1 map filter by type and time (with
the tile-less map of task 010); P2 own-location sharing and local custom markers (a privacy confirmation before any advert
with a position); M2 reactions as short texts (only if the user accepts the convention limits, see questions).

## Phase 4 - remote administration and heavy items
M10 room server login / logout and history replay (replay depth is decided by the firmware, to test on a real room server);
A1 remote command line on a repeater (the terminal tab of task 010; also the place for region administration and repeater
owner info); static telemetry and status of repeaters (only nodes that grant it); D11 TCP transport (optional; BLE is scheduled earlier, see "Decisions taken"); P4 offline map tiles (large, last).
Also from task 010: emoji, repeater polling and route view are scheduled with these phases (emoji spike before M2).

## Cannot be done as asked (report 014), with the proposed substitute
- Native reply (M1) and reactions (M2): the protocol has no field; offer local quoting (a text prefix `@[name] ...`) and, if accepted,
  reactions as short texts that other clients show as plain text.
- "Delivered" on channels: impossible; offered: Sent + "heard back by N repeaters".
- Advert interval on the companion, power off, a board-side packet log, QR scanning: not available; substitutes above.
- Contact groups are not stored on the board: backup file, lost on factory reset otherwise.

## Needs a board test before coding (do first, 30 minutes)
The frame the board returns for a channel send; whether echoes of our own channel message arrive in LOG_DATA on the user's
board; SET_FLOOD_SCOPE behaviour; FACTORY_RESET and GET_STATS support on the user's firmware.

## Decisions taken by the user (2026-10-08)
1. M1 / M2: **yes**, as text conventions (local quoting prefix for replies, short-text reactions), with the interop limits stated in the UI.
2. D11: **BLE is mandatory** (TCP optional). The Pi Zero 2 W Bluetooth controller can hold several LE connections at once, so the BT keyboard
   and a BLE board can coexist (to prove with a spike on the deck: BlueZ over D-Bus, GATT client on the Nordic UART service,
   pairing, reconnect; it shares the 2.4 GHz radio with Wi-Fi). The board then needs the BLE companion firmware instead of the USB one.
   The transport interface stays abstract. Scheduled as a spike at the end of phase 2 and the implementation in phase 3.
3. D2: advert interval as a scheduled advert from the app, default off (no answer given: taken as agreed).
4. Tab keys: **Tab cycles through the tabs in a loop**; direct touch for the rest. No other tab keys, no F-keys.
5. Phase order as written. 6. **Phase 1 started 2026-10-08** as one batch.

## Phase 1b - preset refresh, conversation options, deleting history (mesh-hop 0.1.2)
Requested by the user after the phase 1 deck tests (items 1-11 and 13 passed; item 12, the board GPS, cannot be tested: the only GPS
board is the BLE one). Delivered as one batch on top of 0.1.1, report `reports/016-handoff.md`. Package `mesh-hop` stays a draft.

**A. The preset list is kept up to date in the background.**
- At launch, once the deck is online, the app downloads `https://api.meshcore.nz/api/v1/config` in the background and never waits for
  it: the UI and the start-up do not block (a `posix_spawn` of `/usr/bin/curl`, polled with `waitpid(WNOHANG)` four times a second;
  curl has `--connect-timeout 6 --max-time 15 --max-filesize 200000`, https only, and the app kills it after 25 s). Online means a
  default route exists in `/proc/net/route` (Wi-Fi or any other interface); the start waits 4 s after launch, and while the deck is
  offline the check repeats every 30 s. One download per launch. A failure is silent for the user and written to `mesh-hop.log`.
- Why curl: the app links only libstdc++ and FreeType. TLS inside the app would mean OpenSSL or a bundled library (size, a security
  maintenance burden, a Depends line); curl is already on every deck: the launcher installer (`PKGS=... curl ...`) installs it, the
  Apps store of the launcher runs it, and Raspberry Pi OS Lite ships it. No `Depends` is added; without curl the feature logs
  "curl not found" and the app keeps the saved or built-in list.
- Strict validation (`core/preset_feed.*`): the real shape `config.suggested_radio_settings.entries[]` with `title`, `frequency`,
  `bandwidth`, `spreading_factor`, `coding_rate` (numbers written as text; plain digits only; other keys such as `network_settings`
  are ignored). Frequency 137 to 2500 MHz, bandwidth one of the LoRa values (62.5, 125, 250 ... as `validate_radio`), SF 5 to 12,
  CR 5 to 8, whole numbers for SF and CR, a clean UTF-8 title of 1 to 48 bytes, no duplicate titles, 5 to 120 entries, at most
  200 KB and 12 levels of nesting. One odd value and the whole list is refused.
- Cache: `~/.local/share/mesh-hop/presets.jsonl` (the validated fields only, a header line with the fetch date and the source,
  written through a temporary file). Later starts use it, offline included; a damaged or odd copy is ignored (logged) and the
  built-in 26-entry file (`radio_presets.inc`, 2026-10-07) is the fallback. A new list is adopted only while no popup is open.
- The preset box footer says which list is shown ("List from meshcore.nz, fetched 2026-10-08", "Saved copy of the meshcore.nz list,
  fetched ...", "List built into the app, 2026-10-07").

**B. Options of a conversation.** In Chats, Ctrl+O, or a hold of 3 s with the finger on the conversation name (a row of the left
pane or the name in the header; a bar fills from 0.5 s so the user sees it working; a move of more than 16 px, a scroll or an early
release cancels, and the release after a completed hold is not a tap) opens a box. Direct chat: Mute this contact / Unmute this
contact, Delete conversation, Cancel. Channel: Mute / Unmute this channel, Delete messages, Cancel. A muted contact works like a
muted channel (no pill, not counted in the Chats total, "muted" in the list and in the header; Ctrl+M now mutes either). Deleting
asks Yes / No first with the number of messages, removes them from memory and from `messages.jsonl`, and a deleted direct
conversation leaves the left pane; the contact stays in Contacts and on the board, and a new incoming message recreates the
conversation. A channel stays; only its history is cleared (the Settings channel menu has the same "Delete messages"). The Chats
footer shows the Ctrl+O hint.

**C. Bulk delete.** Settings has a History section (before Undo and Save, which stay the last two rows): Delete all messages (Yes / No
that states the count), Delete messages older than (arrow box with 7, 30 or 90 days, then Yes / No with the count; a message whose
time is unknown, from a start with no clock, is kept; with no clock the app says so), and a line pointing to the per-conversation
delete. Contacts, channels, mute flags and settings are never touched. The store rewrites `messages.jsonl` after every deletion; the
bounded-history rules (200 per conversation, 1500 in all) stay; message numbers are never reused (the read marks also raise the
next number at load), so the unread counts stay right after deletions and restarts.

**D.** Every box keeps buttons of 44 px or more (options and confirm buttons 52 to 56 px, arrows 100 px); Esc is unchanged: a short Esc
closes a box, then goes back one level, never quits; holding Esc for 3 s ends the app (the launcher).
