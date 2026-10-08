# 016 Mesh Hop 0.1.2 (roadmap phase 1b) - hand-off (apps-dev)

```
Task: 016 mesh-hop phase 1b (preset refresh, conversation options, deleting history)   Agent: apps-dev   Result: done on the PC (built, unit tested, headless runs, screenshots); NOT run on the deck, no deck access
Changed: apps/mesh-hop in the apps repo, on top of the committed 0.1.1 on branch mesh-hop-draft, UNCOMMITTED (17 files modified including the rebuilt binary, 8 new, see below).
         apps/meshzero, the launcher source, packages/ and registry.json untouched. Docs: tasks/011-mesh-hop-roadmap.md (a "Phase 1b" section appended) and this report.
How it was checked: unit tests core 682 / simulator 30 / UI logic 743 checks, 0 failed (-Wall -Wextra, no warning); x86 headless build against the simulator
         (history run: 18 screenshots; preset runs: 8 cases; the 0.1.1 smoke run again as a regression); aarch64 build and .deb inspected.
         Not run: the aarch64 binary, the real panel and touch screen, the Bluetooth keyboard, curl and TLS on the deck.
Package: C:\CLAUDE\zero7\pkg-out\mesh-hop_0.1.2_arm64.deb  (1 054 726 bytes, md5 4e1dc36c646420a5165cb1344067cb83); Version 0.1.2, Depends libfreetype6 (unchanged), 4 files
Screens: C:\CLAUDE\zero7\pkg-out\mesh-hop-0.1.2-screens\{history, presets, smoke}\  (run-output files next to them)
Needs from the Controller: the deck test (protocol below, with two log steps for the Controller). The roadmap section to commit. No launcher request.
```

## What changed

Paths are under `C:\CLAUDE\zero7\cyberdeck-zero-apps\apps\mesh-hop\` (`core` = `src/main/core`, `ui` = `src/main/ui`). The version is 0.1.2 in `app.json` (draft stays true),
in the README line and in the log line at start.

### A. Preset list refresh

* **New `core/preset_feed.{hpp,cpp}`** (no LVGL): `parse_preset_feed` (a small strict JSON reader for the real shape, because the feed has nested objects the flat reader of
  the store cannot read), `validate_preset_list`, `save_preset_cache` / `load_preset_cache`, `route_table_has_default` / `network_online`, and `PresetFetcher`.
* **How HTTPS is done, and why.** The app spawns `/usr/bin/curl` (`posix_spawn`, stdin / stdout / stderr on /dev/null, output to `presets.download` in the data folder) and polls it with
  `waitpid(WNOHANG)` every 250 ms from the UI tick, so nothing waits. Flags: `-sS -f -L --max-redirs 3 --connect-timeout 6 --max-time 15 --max-filesize 200000
  --proto =https --proto-redir =https`; the app also kills curl after 25 s and on exit. Why curl: the app links only libstdc++ and FreeType; TLS in-process would add OpenSSL
  (or a bundled TLS library): size, certificates and a security-update burden, for a 5 KB file fetched once. curl is already a launcher dependency (the installer's
  `PKGS="... curl ..."` line in `projects/APPLaunch/pizero2w/{,bundle/}install.sh`, the Apps store backend runs `curl` for its sync probe) and Raspberry Pi OS Lite ships it. I could not
  look at the deck: this is from the launcher repo, not a check on the device (protocol item 2 checks it). **No `Depends` added** (the app degrades silently without curl:
  log line "curl not found"). `MESHHOP_CURL` and `MESHHOP_PRESETS_URL` replace the program and the address (tests only; a non-https address gets no `--proto` flags).
* **When.** `App::presets_tick`: 4 s after the start, then every 30 s while the deck is offline; one download per launch. Online = a default route on an interface other than `lo`
  in `/proc/net/route` (Wi-Fi or any other link); `MESHHOP_ONLINE=0|1` forces it (tests). A failure is only a line in `mesh-hop.log` (`presets: refresh failed (...)`); no popup, no notice.
* **Validation** (whole list refused on any problem): the path `config.suggested_radio_settings.entries[]`; per entry `title` (string, 1 to 48 bytes, clean UTF-8, no control
  character, no leading / trailing space), `frequency`, `bandwidth`, `spreading_factor`, `coding_rate` (text or number, plain digits with at most one decimal point, no sign, no
  exponent), then `validate_radio` (137 to 2500 MHz, bandwidth among the LoRa values incl. 62.5 and 250, SF 5 to 12, CR 5 to 8), SF and CR whole, no duplicate title, 5 to 120
  entries, at most 200 KB, nesting at most 12, surrogate escapes refused. Other keys (`network_settings`, `description`, the `remote_management` block with its guest passwords) are
  ignored and never stored.
* **Cache** `presets.jsonl` in `~/.local/share/mesh-hop`: a header line `{"date":"2026-10-08","src":...,"n":26}` then one line per preset (validated fields only). Loaded at start (before the
  download) with the same checks: a missing, truncated, odd or wrongly counted copy is ignored (logged "the saved copy is damaged") and the built-in 26 entries are used. The date is the deck
  date of the download ("unknown" when the deck clock is not plausible).
* **Where it shows.** The footer line of the preset box (`make_choice`): "List from meshcore.nz, fetched 2026-10-08" / "Saved copy of the meshcore.nz list, fetched ..." / "List built into the
  app, 2026-10-07". A new list is adopted in the tick only while no popup is open (the preset box holds indexes into the list); a notice "Radio preset list updated (N entries)" shows when
  the list differs. `presets.{hpp,cpp}`: the list in use is now settable (`set_presets`, `reset_presets`, `presets_origin`); `radio_presets.inc` stays the fallback.
* The debug state line (`dump`) has `presets=<bundled|cached|fetched>/<n>/<date>`.

### B. Options of a conversation

* **Ctrl+O** in Chats (also while typing; ignored on the "+ Add channel" row with a notice) and a **3 s touch hold** on a row of the left pane or on the name in the header (an invisible 240 x 44 px
  button over the name; a plain tap on it does nothing). `HoldTracker` (`ui_logic.*`, unit tested) counts the hold: the cue (a gold bar in a panel above the entry, "Keep holding: options for
  <name>") appears after 0.5 s and fills over 3 s; a move over 16 px, a scroll of the list, a release, a popup opening or a lost press cancels it; when it completes the options box opens and
  the click that follows the release is swallowed (the headless run shows the row is not selected by that release). Events come from LVGL's PRESSED / PRESSING / RELEASED / PRESS_LOST on the rows
  (`trampoline_row`), nothing is added to the platform layer.
* **The box** (the existing menu popup, 52 px buttons + Cancel 48 px; `conversation_options`): direct = "Mute this contact" / "Unmute this contact", "Delete conversation"; channel = "Mute this
  channel" / "Unmute this channel", "Delete messages". Esc or Cancel closes it.
* **Mute contact**: the existing local flag (`prefs.txt`, `mute d:<id>`); no pill, not in the Chats total, "muted" in the list and in the header; Ctrl+M now mutes either kind. A bug found on the
  way: an open direct chat with no message was listed without its muted flag (fixed in `build_chat_rows`).
* **Delete**: a Yes / No box (default No, the Yes button red, "Delete") that names the count and says what stays ("... stays in Contacts; a new message starts a new conversation" /
  "The channel stays"). `Model::delete_conversation` removes the messages and the read mark (the mute flag is a preference and stays); the store rewrites `messages.jsonl`. The open direct
  chat leaves the left pane (`after_deletion`); a channel stays. The Settings channel menu has "Delete messages" too (4 items). Nothing to delete: a notice instead of a box.
* Footer hints: list "Up/Down: select  Enter: write  Ctrl+O: options"; entry "Enter: send  Esc: list  Up/Down: scroll  Ctrl+O: options". The hold is described in the Settings History note and the README.

### C. Bulk delete (Settings > History)

* New section "HISTORY" before "RADIO CHANGES" (Undo and Save stay the last two rows): "Delete all messages" (value: the count), "Delete older than" (7, 30 or 90 days), and a note row "One
  conversation: Chats: hold its name 3 s, or Ctrl+O". Older-than uses the arrow box (`make_days_choice`, default 30 days, remembers the last choice in the session), then Yes / No with the count
  ("Delete 4 messages older than 7 days ..."); zero messages gives a notice; no plausible clock gives a notice ("The clock is not set"); a message whose time is unknown (time before 2025) is
  never deleted by age.
* `Model::{message_count_in, delete_conversation, delete_all_messages, count_older_than, delete_older_than}`, `Listener::messages_removed` (the store compacts), `Store::messages_removed`.
  Contacts, channels, mute flags, retry settings and the board are never touched. Bounded history unchanged. **Seq numbers are never reused**: `load_read` now raises the next number above
  every read mark (otherwise "delete older" of everything, then a restart, would number new messages below the old marks and show them as read; test `history4`).
* Accounting after deletion: unread totals and the Chats tab badge recomputed from what is left (unit tested with a muted contact, a channel, restarts, `delete_all`).

### D. UI check

Every new box uses the existing popup builders: menu items 52 px, Cancel 48 px, confirm buttons 56 px, the arrow box 100 px arrows and 48 px Cancel / OK. Esc is unchanged: a short Esc closes a box
(options = cancel, confirm = No), then goes back one level, never quits; holding Esc 3 s quits (the launcher). Keys and taps are ignored while a popup is only being built (existing guard).
The Settings key handler uses `settings_row_selectable` (the History note is not selectable).

### Files

Modified: `core/{model,presets,store}.{hpp,cpp}`, `ui/{app,ui_logic}.{hpp,cpp}`, `src/main/src/main.cpp` (new script command `hold <x> <y> <ms> [shot]`), `src/tests/{run_tests.sh,test_core.cpp,test_ui.cpp}`,
`app.json`, `README.md`. New: `core/preset_feed.{hpp,cpp}`, `src/tests/data/meshcore_config.json` (the real feed of 2026-10-08, 5 KB, the unit-test fixture), `src/tests/{history.script,history_run.sh,seed_history.py,presets.script,presets_run.sh}`.
All files were normalised to LF (a scripted edit on Windows had produced CRLF in some of them; checked: none left). The package binary `root/usr/share/APPLaunch/bin/M5CardputerZero-mesh-hop` is rebuilt.

## How it was built and tested

* Unit tests: `wsl -e sh -c 'sh /mnt/c/CLAUDE/zero7/cyberdeck-zero-apps/apps/mesh-hop/src/tests/run_tests.sh'` -> `682 checks, 0 failed`, `sim: 30 checks, 0 failed`, `ui: 743 checks, 0 failed`.
  New: the feed parser on the **real JSON** (26 entries, equal to the bundled file) and on about 40 malformed or odd inputs (empty, truncated, trailing text, bad escapes, control character, surrogate,
  title too long / empty / not a string, frequency text / exponent / negative / too high / too low / empty / two dots, bandwidth 63 and 0, SF 4 / 13 / 7.5, CR 4 / 9, duplicate title, missing
  field, too few / too many entries, nesting, size over 200 KB); the cache (round trip, "unknown" date, refused empty list, damaged value, wrong count, bad date, truncated, garbage); the origin
  text; the route table parser (UP default route, subnet only, `lo`, route not UP, empty); the fetcher against fake curl scripts (good, junk, odd value, exit 22, empty file, missing program, hang
  with the 25 s deadline, abort); the store operations (delete a conversation, a channel, by age with the boundary and the unknown-time message, all; persistence over restarts; unread totals with a
  muted contact; recreation by a new message; the bound after deletions; the read-mark rule); the options box, prompts, days choice, History rows and the hold tracker in the UI tests.
* Host build: `wsl -e sh -c 'sh .../apps/mesh-hop/build/build.sh host'` (`~/mesh-hop-host/mesh-hop`); aarch64: `wsl -e sh -c 'sh .../apps/mesh-hop/build/build.sh'` (copies `src/` to the scratch project, `flock /tmp/wsl-build.lock scons`);
  no warning. `readelf -d`: libstdc++, libfreetype, libm, libc, ld-linux-aarch64, libgcc_s (no new library; `posix_spawn` is libc).
* Package: `python tools/make_registry.py --only mesh-hop --out C:/CLAUDE/zero7/pkg-out --owner OSRdesign --repo cyberdeck-zero-apps` -> `mesh-hop_0.1.2_arm64.deb`; `dpkg-deb -I/-c`: Package mesh-hop,
  Version 0.1.2, Depends libfreetype6, 4 files.
* Headless runs (`--headless`, simulator, own folders under `...\mesh-hop-0.1.2-screens\`):
  * `history\` (`history_run.sh`: a seeded history of 10 messages from 100 days to 1 hour old, plus the simulator's 2): the options box by Ctrl+O (`h02`, `h04` with Unmute), mute and "muted" in the list
    (`h03`), the delete confirmation (`h05`) and its No, the delete (`h06`: the entry is gone, 4 messages removed, contacts still 4), the hold cue at 1.7 s (`h07`) and the box at 3.4 s (`h08`, the release did
    not select the row), a channel (`h10`, `h11`, the channel stays listed), the header name (`h12`), Settings > History (`h13`), the days box (`h14`), the older-than confirmation (`h15`), delete-all
    confirmation (`h16`), everything gone (`h17`, `h18`). Checked at the end: `messages.jsonl` 10 lines before, 0 after; `contacts.jsonl` 4 and `channels.jsonl` unchanged; `prefs.txt` kept.
  * `presets\` (`presets_run.sh`, a local web server stands in for the feed): 1 online and fetched (27 entries of a modified copy; the popup says "List from meshcore.nz, fetched 2026-10-08"; cache written), 2
    offline start with the saved copy ("Saved copy ..."), 3 offline first start (built-in list), 4 server down (silent, log only), 5 refused list (an odd value in one entry), 6 curl missing, 7 damaged saved copy
    (ignored, logged), and **8 the real `https://api.meshcore.nz/api/v1/config` with the real curl of WSL: fetched 26 entries, cache written** (`REAL=1`).
  * `smoke\`: the 0.1.1 scripted run again (tabs, contacts, choice boxes, save / undo): same results as 0.1.1.

## Test protocol for the deck (the user, with the board; the Controller does the two log steps)

Install `mesh-hop_0.1.2_arm64.deb` over 0.1.1 (Settings > Apps flow or `deck.py install`); the history, contacts and mute flags of 0.1.1 are kept. **The deck clock must be right** (NTP) for HTTPS in items 2 and 3.

1. **Start.** Start Mesh Hop. Chats footer (no notice showing): "Up/Down: select  Enter: write  Ctrl+O: options". Tabs, Esc and the top bar as before.
2. **Preset refresh, online.** Wi-Fi connected. Start the app and use it normally for 10 s (touch the tabs: nothing may lag or freeze while the download runs). Settings > Preset: the box footer reads
   "List from meshcore.nz, fetched <today>" and the list has the current entries (26 today). *Controller:* `tail -n 15 ~/.local/share/mesh-hop/mesh-hop.log` shows `presets: online, downloading ...` then
   `presets: fetched 26 entries from https://api.meshcore.nz/api/v1/config`; `ls -l ~/.local/share/mesh-hop/presets.jsonl` exists (27 lines); `ls -l /usr/bin/curl` exists. If the log says
   `curl failed, exit code 60` the deck clock or its CA store is wrong; `exit code 6` is DNS; `curl not found` means no curl: tell me which.
3. **Offline with the saved copy.** Turn Wi-Fi off (or move out of range), quit the app (hold Esc 3 s), start it again. Settings > Preset footer: "Saved copy of the meshcore.nz list, fetched <the date of item 2>". No
   error, no popup. Log: `presets: using the saved copy (...)` and `the deck is offline, no refresh`.
4. **Offline, nothing saved.** *Controller:* `rm ~/.local/share/mesh-hop/presets.jsonl`, Wi-Fi still off, start the app: the footer reads "List built into the app, 2026-10-07" (26 entries). Then switch Wi-Fi on while the app runs: within about a minute
   the log shows the download and the list changes by itself (the notice "Radio preset list updated" only if it differs from the built-in one).
5. **A refused or failed refresh is silent.** Wi-Fi on but with no internet (for example unplug the router's uplink, or `MESHHOP_PRESETS_URL` not needed): the app is normal, nothing on screen; the log has `presets: refresh failed (...)`.
6. **Options by keyboard, direct chat.** Open a direct chat (Chats > a person). Ctrl+O: a box titled with the name: "Mute this contact", "Delete conversation", Cancel. Esc closes it (you stay in Chats). Ctrl+O again, Enter (mute): the list row shows
   "muted" (no number), the header shows "muted". Have that person send you a message: no gold number on the row, none on the Chats tab. Ctrl+O: the box now says "Unmute this contact"; take it.
7. **Options by touch hold.** On a direct row in the left list put a finger and keep it still: after about half a second a bar "Keep holding: options for <name>" appears and fills; at 3 s the options box opens; lift the finger: the row is **not** also opened.
   Try: lift at 1 s (nothing opens, the row is just selected), slide the finger away while holding (the bar disappears, nothing opens), drag the list (nothing opens). Hold on the **name in the header** (top of the right pane): the same box. Hold on a **channel** row: "Mute this channel" / "Delete messages" / Cancel.
8. **Delete a conversation.** Direct chat with some messages: options > Delete conversation: the box says "Delete N messages with <name> from this deck? <name> stays in Contacts ...". Choose No (or Esc): nothing changes. Again, Yes: "Deleted N messages", the
   person's row **disappears from the left list**. Contacts tab: the person is still there (the same number of contacts). *Controller:* `wc -l ~/.local/share/mesh-hop/messages.jsonl` is smaller by N. Send them a message from Contacts (Enter on the row) or have them write: the conversation comes back with only the new message.
9. **Delete a channel's messages.** Channel > options > Delete messages > Yes: the history of that channel is empty, the channel is still in the list and in Settings > Channels. Same from Settings > Channels: tap a channel name, "Delete messages".
10. **Delete older than.** Settings > History > "Delete older than": the box with the big arrows (30 days); step to 7 days and OK: a Yes / No box that states how many messages are older than 7 days. No: nothing deleted. Do it again with Yes: those messages go, newer ones stay (check a chat). With 90 days and none that old:
    a notice "No message is older than 90 days".
11. **Delete all.** Settings > History > "Delete all messages": the row shows the count; the box states the count; No, then Yes: every conversation is empty, the Chats tab number is gone, the direct rows disappear, the channels stay. Contacts, Settings > Channels and the radio settings
    (Settings > Radio, unchanged and not "unsaved") are the same as before. Quit and start the app again: still empty, contacts and channels still there; receive one message: it shows as unread (1).
12. **Escape and size.** In every new box a short Esc closes it (confirm = No); after closing, Esc on a top-level screen only shows "Hold Esc 3 s to exit"; holding Esc 3 s quits. All the new buttons are easy to hit with a finger (say if any is not).
13. *Not testable now:* the board GPS (item 12 of phase 1).

## Could not verify

* **Nothing ran on the deck.** Not checked on the device: that `/usr/bin/curl` exists, that TLS to `api.meshcore.nz` works from the Pi (it needs a correct deck clock: the Pi has no RTC; a wrong clock gives curl exit 60 and the app falls back to the saved or built-in list), DNS and IPv6 behaviour,
  the time curl takes on a Pi Zero 2 W (the app does not wait for it).
* **The 3 s hold on the real touch driver.** The headless run injects the finger into LVGL's pointer, which exercises the event path (PRESSED, PRESSING, RELEASED, the click swallowing, the cancel by movement and scroll) but not the evdev reader; a jittery panel that moves a held finger by more than 16 px would cancel the hold (the
  constant is `HoldTracker::kSlop`). The place of the cue (above the message entry) and its look on the real panel are untested.
* Ctrl+O on the user's Bluetooth keyboard (Ctrl+M was only tested by the user in phase 1).
* Memory and speed of the new code on the Pi (nothing heavy: a 5 KB parse, one child process, a message list rewrite per deletion; a deletion rewrites `messages.jsonl` once, at most 1500 lines).
* The wording of the notices and boxes on the real screen (the screenshots show them on the PC).

## Risks

* A deletion is final (no undo, said in the box). The age of a message is the app clock when it was stored; if the clock was wrong then (no NTP, clock typed by hand), "older than N days" is wrong for those messages. Messages stored before the clock was set (time before 2025) are kept by the age rule.
* The feed is trusted after HTTPS and the checks above, but a valid list may still carry a frequency that is not legal where the user lives; the user always confirms "Save to radio" with the values listed in the box, and nothing is sent to the board by the refresh itself.
* An unintended 3 s rest of a finger on a name opens the options box (Cancel / Esc closes it; nothing changes without a Yes). The hold works only in Chats and not while a box or editor is open.
* One download per launch: a first attempt that fails (Wi-Fi just up, DNS not ready) is not retried until the next launch. (A retry after 10 minutes is a small change if wanted.)
* The list popup and its index: a fetched list replaces the list in the tick only when no popup is open; a user who stays 10 s in the preset box sees the new list at the next opening.
* `Store::compact` rewrites the whole `messages.jsonl` after each deletion and `contacts_changed` still rewrites `contacts.jsonl` on every advert (unchanged from 0.1.1).

## Questions for the Controller

1. Add `curl` to `Depends` of the package? Not done (the launcher already requires it; the feature degrades silently), but it costs nothing if the installer path for apps resolves dependencies.
2. Retry the refresh later in the same launch (for example once after 10 minutes) when the first attempt failed while online?
3. Should the Preset box get a "Refresh now" action (touch) for a user who just connected? Not requested, not built.
