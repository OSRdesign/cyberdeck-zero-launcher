# 036 - Mesh Hop: managing a repeater from the contact list (picker, login, read-only view)

Status: study written 2026-10-23. Untracked in the launcher worktree. Apps repo read in `C:\CLAUDE\zero7\apps-cli`
(branch `mesh-hop-cli`, uncommitted CLI work: relay, CLI page, bundled meshcore-cli 1.6.6 / meshcore 2.3.17). Official app
web build v1.50.0+78 (b7528a7, 2026-09-25) downloaded to the scratchpad and read as data only (strings, never run).
Firmware read at MeshCore 81dbae5 (2026-10-15). No product code changed, nothing committed, no board touched.

Markers: **S** = our source (file:line); **U** = upstream source (meshcore_py, meshcore-cli, MeshCore firmware, file:line);
**A** = the official app's web build (UI strings, storage keys, its changelog); **I** = inferred, needs a board.
Cost unit: one worker session. Builds on report 033 (the CLI, the relay, decisions D1-D12).

## 1. Summary

- **The owner's problem.** The CLI works, but `to <name>` must be typed exactly, then `login`. The official app starts from
  the contact list, logs in outside any CLI and shows the repeater's data read-only.
- **Recommendation.** A native **Repeater page** in Mesh Hop, opened from a repeater or room in Contacts. It uses Mesh
  Hop's **own client**, not the CLI. It offers a masked login, a password remembered per repeater, and read-only
  Status / Neighbours / Owner sections refreshed on demand. **Open in CLI** opens the CLI page and types `to <full key>`
  for the user.
- **Why this is cheap and safe.** A login is an entry in the repeater's access list, keyed by the **companion's** public
  key (U: `simple_repeater/MyMesh.cpp:90-145`). Once the page has logged in, meshcore-cli on the same node can send admin
  commands without its own `login`, and the reverse is true too. The page and the CLI therefore share one session for free.
- **Passwords:** one store, meshcore-cli's own `<data>/cli/.config/meshcore/<64-hex key>.pass` (0600), so the page and
  the CLI's `login` read the same file.
- **Plan:** S1 "pick and log in" (about 2.25), S2 "read-only view" (about 2), S3 "admin parameters and telemetry"
  (about 1.75, optional). Recommended order: S1, S2, then S3 only on request.

## 2. What the official app does (A)

**Flow (from the v1.50.0 web build's English UI strings and its CHANGELOG).**
1. **Contacts list.** Tapping a repeater opens "Repeater Login". A room opens "Room Login". A sensor (or a repeater
   without the paid remote-management unlock) opens contact details. Contact details carry "Manage", "View Telemetry"
   and "Ping (Zero Hop)".
2. **Login screen.** Title "Repeater Login", "Please log in to manage this repeater."
   - A Password field and a **"Remember Password?"** checkbox.
   - **"Log In"** shows the route. A long press offers "Log in via current path." / "Log in directly to this repeater."
     (zero hop) / "Log in via all repeaters." (flood).
   - Hint: "Some repeater owners may allow guests to view metrics without a password." A blank password is a guest
     login.
   - **"Resume Session"** skips the login when the app logged in before.
3. **Login errors.**
   - "Failed to log in!"
   - "You didn't enter a password. Guest access may be disabled, or this repeater may be unreachable."
   - "The password you entered may be incorrect, or this repeater may be unreachable."
   - The app's server config can switch guest logins off ("Guest login is currently unavailable") and can refuse
     flood-routed guest logins: "To avoid overwhelming the mesh with flood packets, please set a path to log in to this
     repeater as a guest." Its config keys are `guest_login_enabled`, `guest_login_passwords` and
     `flood_routed_guest_login_enabled`.
4. **After login.** "Repeater Admin", with the banner "You are logged in as an Admin / as a Guest".
   - **Status** tab: "Request Status" button; status is not fetched by itself (an "Auto Fetch Status" toggle exists).
     "To use minimal mesh traffic, please manually request the repeater status." The fetch retries up to 3 times
     (`autoRetry(..., 3)` in the code). "Clock • At Login" comes from the login reply. Fields: Uptime, Total Airtime
     TX/RX, Last RSSI, Last SNR, Packets Sent/Received (total, flood, direct), Duplicate Packets Seen, Received Packet
     Errors, TX Queue, Debug Flags, noise floor and battery. On failure: "Repeater Status Unavailable - Your session may
     have expired, or this repeater may be unreachable."
   - **Guest Tools** tab (guest): Owner Info ("View public contact details"), Neighbours, Telemetry. Old firmware gives
     "This repeater requires a firmware update to enable guest access to owner info / to neighbours."
   - **Settings list** (admin): Owner Info (with "View as Guest" and "Get from Repeater"), Advert, Advert Intervals,
     Position, Sync Clock, Access Control, Admin Password, Guest Password, Change Identity Key, Manage Regions, Network,
     Neighbours, Repeat Settings, Telemetry, Reboot, Version. Each reads its value with a CLI `get` ("Radio settings
     could not be parsed.", "Unexpected Response").
   - **Command Line** tab (admin): Command Help, history, resend.
5. **Neighbours.** List or map; sorted newest, oldest, strongest or weakest; "Load More" (paginated); "Discover
   Neighbours"; remove (admin only).
6. **Storage.** Per contact in the app's database: `repeater_admin_password` (one field, admin or guest), `room_password`,
   `remote_management_is_admin`, `remote_management_last_firmware_ver_level`.
7. **Logout.** "You have been logged out from this repeater."

The build is opaque code, but the strings, storage keys and changelog were enough to reconstruct the flow. No code or
asset was copied.

## 3. What can be read, and with which login (U, firmware 81dbae5)

| Data | No password | Guest (guest password, or a blank one when the owner left `guest.password` empty) | Admin | On the wire |
| --- | --- | --- | --- | --- |
| Name, type, key, route, position, last advert, SNR | yes: already in Mesh Hop's contact detail (S: `mesh_app.cpp:3521-3534`) | = | = | adverts |
| Login result: role, permissions, **repeater clock**, firmware level | - | yes | yes | login 26 → push 0x85 (U: companion `MyMesh.cpp:704-716`) |
| Owner info + name | **anonymous** request OWNER 0x02. Only answered when it arrives **direct** (zero hop or known path), **4 per 3 min per repeater for everyone** (U: `MyMesh.cpp:584-589`, `:869`) | binary request 0x07 "owner info": **firmware version** + name + owner info. Needs repeater firmware level ≥ 2 (U: `:372-374`) | + `get owner.info` (CLI) | anon 57 / binary 50 → push 0x8C |
| Clock, repeat on/off, bridge | anonymous BASIC 0x03 (direct only): clock + flag 0x80 "repeat disabled" (U: `:184-209`) | same | + `clock`, `get repeat` | anon 57 |
| Regions | anonymous REGIONS 0x01 (direct only) | same | + `region` | anon 57 |
| **Status**: battery mV, TX queue, noise floor, last RSSI/SNR, packets rx/tx, flood/direct counts, airtime TX/RX, uptime, duplicates, rx errors, debug flags | - | **yes** ("guests can also access this now", U: `:216-238`) | yes | binary 0x01; 52 bytes (U: meshcore_py `parsing.py:66-107`) |
| **Neighbours** (zero-hop repeaters only): key prefix, heard N s ago, SNR | - | **yes** on current firmware (U: `:276-371`); older firmware: admin only (A) | yes | binary 0x06; 130-byte page: 14 entries with a 4-byte prefix |
| Telemetry | - | **base only**: battery voltage, MCU temperature (U: `:240-250`) | all sensors | binary 0x03, Cayenne LPP |
| Access list | - | - | yes (U: `:262-274`) | binary 0x05 |
| Parameters: `get name`, `radio` (freq, bw, sf, cr), `tx`, `repeat`, `lat`, `lon`, `advert.interval`, `flood.advert.interval`, `flood.max`, `owner.info`, `guest.password`, `af`, `rxdelay`, `txdelay`, `path.hash.mode`, `loop.detect`, `role`; `ver`, `board`, `clock`, `power`, `neighbors` | - | - | **CLI only**: text type CLI_DATA 1, admin only (U: `:702`; `CommonCLI.cpp:701-835`; `CommonRadioPrefs.cpp:32-220`) | SEND_TXT type 1 → contact message type 1 |

**Protocol facts that shape the design.**
- **A wrong password gets no answer.** The repeater returns 0 and sends nothing (U: `:106-111`), so "wrong password" and
  "out of reach" look the same: a timeout. The LOGIN_FAIL push exists only for legacy replies (U: companion `:717-722`).
- **The firmware's default guest password is empty** (U: `CommonCLI.h:228-234`, never set in `MyMesh.cpp:890-908`). A
  blank login therefore gives guest on every repeater whose owner did not set one. A blank login from a key already in
  the access list restores its role (U: `:92-99`).
- **"Logout" is local only.** CMD_LOGOUT 29 stops the companion's keep-alive (U: companion `:1748-1751`), and keep-alive
  is 0 today (U: `:138`). The repeater keeps us in its list:
  - an admin until `setperm`;
  - a guest until a reboot or eviction (guests are not saved to flash, U: `:126`).
- **One remote request at a time per companion.** Every login, status or binary/anon request calls `clearPendingReqs()`
  (U: companion `:1588`, `:1619`, `:1639`, `:1730`). A second request makes the first one's reply unmatchable.
- **Timeouts.** RESP_CODE_SENT carries a tag, a flood flag and `est_timeout` in ms (U: companion `:1590-1594`; S: `MsgSent`,
  `protocol.hpp:172-176`). meshcore_py waits `est_timeout / 800` s, that is 1.25x (U: `binary.py:33`). Requests flood
  when no path is known; the reply then returns a path, so the next request goes direct (U: `:683-698`).
- **CLI replies have no tag, but a 2-character prefix `xx|` is reflected** ("optional prefix (for companion radio CLI)",
  U: `MyMesh.cpp:1235-1239`). That gives reliable matching of `get` replies. The firmware version that introduced it is
  unknown (I), so the page must fall back to order matching.
- **Each CLI reply is about 160 bytes at most, sent 600 ms after the command, with no ACK** (033 section 2.2). A command
  repeated with the same timestamp is not run again.
- **Rooms** use the same login, status, telemetry and access list. They have no neighbours, no owner request and no
  anonymous requests.
  - A blank password gives read-only when `allow.read.only` is on (U: `simple_room_server/MyMesh.cpp:335-353`).
  - After a login the room pushes its posts. They arrive as text type 2, which stays in Chats: wanted.

## 4. Design

### 4.1 Native page, not the CLI (decision)

The Repeater page talks through `Client` with new remote operations. Each one is a normal queued command (26 login,
50 binary request, 57 anonymous request, 2 with text type 1 for `get`), followed by a **remote wait**: a Client timer for
the push whose tag matches.

Why native, and not meshcore-cli driven through the relay:
- **No Python start.** The CLI takes several seconds on a Pi Zero 2 W, and more the first time (033 section 11).
- **No relay session**, so the relay does not hold Mesh Hop's sends (033 D6). Chats, DMs and retries carry on during a
  30 s LoRa wait: the wait is a timer, not a command in flight.
- **Structured replies** (status, neighbours) are parsed from bytes rather than scraped from text that changes with
  meshcore-cli's releases.
- **A real masked field** and **the full key everywhere.** meshcore-cli's own `login` looks the repeater up by
  `adv_name`, and a hex-looking name is tried as a key prefix first, with the first match winning
  (U: `meshcore_cli.py:1877`, `:2355-2368`). Two repeaters with the same name can therefore log into the wrong one.

**How it coexists with the relay.**
- Remote operations are refused while `relay_active()`.
- `start_relay()` and a lost link **cancel** the remote queue. A late 0x8C is then ignored, since no tag is pending.
- The page and the CLI are never open together (the CLI ends when its page is left, D6). The access-list session
  survives a CLI session.
- The type-1 fix already keeps CLI replies out of Chats (S: README:204). S3 routes a type-1 reply from the repeater being
  read to the waiting `get` instead.

### 4.2 Entry points and picker

- **Contacts > a repeater or room > detail.** The detail's action bar (S: `mesh_app.cpp:3524-3529`, today Message /
  Groups / Delete) gains **Manage**, which opens the Repeater page. Sensors come later.
- **CLI page.** A **To…** button opens a picker: the Contacts list with the type filter preset to Repeater + Room
  (S: `TypeFilter`, `ui_logic.hpp:127`) and its search. Choosing a row types `to <64-hex key>`.
- **Repeater page > Open in CLI.** Opens the CLI page. When `CliScreen::at_prompt()` first turns true
  (S: `cli_logic.hpp:55`), the page sends `to <64-hex key>\n` once.
  - No quoting is needed, and duplicate or hex-looking names cannot mislead it: meshcore-cli tries a hex argument as a
    key prefix first, and a full 64-character key matches one contact only (U: `:1420-1430`, `:2355-2368`).
  - The prompt then shows `Node/Rep|0>`. If the page is logged in as admin, commands work at once, and `login` is not
    needed.
- **Keyboard.** The usual list keys; Enter opens; Esc goes back.

### 4.3 The Repeater page (640x480 and 800x480, one column, the existing list rows)

- **Header:** the name, then "Repeater" or "Room", the route ("direct, 2 hops" / "flood") and the age of the last advert.
- **Bar:** **Log in** (becomes **Log out** after a login), **Refresh**, **Open in CLI**.
- **Sections** of list rows (label on the left, grey value on the right), each section header showing its state: "read
  14:02", "waiting… up to 12 s", "no answer" or "admin only".
  - **Access:** Not logged in / Guest / Admin since hh:mm; "Password saved" with **Forget**; the repeater clock at login
    and its skew from the deck. A warning appears when the repeater is ahead by more than 60 s, because its replay rule
    then drops our commands.
  - **Status** (guest or admin).
  - **Neighbours** (guest or admin): name resolved from Contacts by key prefix, else the hex prefix; SNR; heard N ago;
    **More** while the reply says more remain.
  - **Owner:**
    - logged in: request 0x07, which also gives the firmware version;
    - not logged in: the anonymous OWNER request, offered only when the route is direct;
    - otherwise "Log in to read".
  - **Parameters** (admin, S3).
- **Login popup:**
  - a masked field (≤ 15 bytes, the firmware's `char[16]`, U: `CommonCLI.h:31,39`);
  - **Remember on this deck** (default on);
  - **Log in**, plus **Log in as guest** (blank password);
  - a "No known route: the login is sent to the whole mesh" line when the route is flood.
  - Enter logs in, Esc cancels. The on-screen keyboard appears only without a physical keyboard (owner rule).
- **Refresh:**
  - With no selection: Status, then Neighbours, then Owner, one after the other.
  - With a section selected: that section only.
  - Nothing is fetched automatically except Status, once, right after a successful login (proposed default; the
    official app fetches nothing).
  - **Cancel** stops the queue.
- **Errors (texts to settle with the owner):**
  - a login timeout: "No answer. Wrong password, or <name> is out of reach.";
  - a request timeout: "No answer. <name> may be out of reach, or its session ended (reboot): log in again.";
  - ERR NOT_FOUND / TABLE_FULL / UNSUPPORTED: immediate messages ("not on the node", "the node is busy",
    "firmware too old").
  - **No automatic retry.** A **Try again** row is offered; a retry after a direct timeout may offer "reset route and
    retry by flood" (polish).
- **Log out:** "Log out (this deck)". It clears the page's session and sends CMD_LOGOUT. Its help line says the repeater
  still lists this deck until `setperm` (admin) or a reboot (guest).

### 4.4 Passwords (decision)

- **Reuse meshcore-cli's file:** `<data>/cli/.config/meshcore/<64-hex key>.pass`, one line (U: `:1857-1875`). Mesh Hop
  already creates this folder for the CLI (S: README:227-231).
- **Written only after a successful login** and only when Remember is on, as a temporary file renamed into place, mode
  0600. A blank (guest) login writes nothing. **Forget** deletes the file, the same as `fp` in the CLI.
- The password is never logged (debug logs carry codes and lengths only) and never shown. The field is cleared after
  sending.
- **Why not Mesh Hop's own store:** two stores would drift. With one store, `login` in the CLI after a pick asks nothing,
  and a password typed in the CLI pre-fills the page. The plain text at 0600 is the 033 decision, unchanged.

## 5. Slices

| Slice | Content | Simulator must learn (`tools/meshcore_sim.py`) | Cost |
| --- | --- | --- | --- |
| **S1 Pick and log in** (main path) | Client remote queue (login 26, logout 29, tag + `est_timeout`, push 0x85/0x86, timer, one at a time, cancel on relay or link loss) with unit tests on a fake transport and clock (0.75). Shared `.pass` store with tests (0.25). Contacts detail **Manage**, a page skeleton (header, Access, Login popup), **Open in CLI** (`to <key>` at the first prompt), the CLI **To…** picker; flows at both sizes, touch only and keyboard (0.75). README, version, BOARD-TESTS (0.25). | Login exists (S: `:302-323`, `:615-622`). Add: the **firmware default empty guest password** (today `hello`, `:828`); `--remote-delay` and `--remote-drop`; contacts with **duplicate names, spaces and a hex-looking name**; "repeater rebooted" (forgets the access list) | **2.25** |
| **S2 Read-only view** (main path) | Binary request 50 + push 0x8C; parsers for status (52 B), neighbours (pages) and owner 0x07; anonymous 57 OWNER only with a direct route; per-section Refresh, serial "Refresh all", error rows (1.25). Sections and neighbour names from Contacts (0.5). Docs (0.25) | CMD 50 STATUS / NEIGHBOURS / OWNER_INFO with tag in SENT and 0x8C carrying the tag; CMD 57 OWNER / BASIC **answered only on a direct route**, with the 4-per-180 s limiter; an "old repeater" mode (no 0x07, neighbours admin only) | **2** |
| **S3 Admin parameters + telemetry** (secondary) | `get` reads over CLI_DATA with an `xx|` tag, one at a time, each with its timeout, the type-1 reply routed to the read; Parameters section (name, radio, tx, repeat, lat/lon, advert intervals, flood.max, clock, ver) (0.75). Telemetry: a Cayenne LPP subset (voltage, temperature, humidity, pressure, GPS) and the access list (0.75). Docs (0.25) | `get` keys `radio`, `tx`, `lat`, `lon`, `advert.interval`, `flood.advert.interval`, `flood.max`, `owner.info`; **prefix reflection**; LPP telemetry (guest base vs admin all); ACL 0x05 | **1.75** |

**Recommended order:** S1, then S2, with a board check after each; S3 only if the owner asks.
- S1's picker and Open in CLI (about 0.75 of it) can ship alone if needed: it removes the typing problem the same day.
- **What the simulator cannot show:** real airtime and timeouts, flood versus direct on the owner's mesh, the anonymous
  limiter shared with other users, and the exact firmware versions of his repeater and companion. The companion levels
  for CMD 50 and 57 are unknown (I); the boards reported level 11 on 2026-10-10.
- **Board check (owner, about 10 min, his repeater over LoRa):**
  - after S1: Manage → Log in (admin, remembered) → Open in CLI → `ver` answers with no `login` typed;
  - after S2: Refresh shows Status and Neighbours; a repeater he does not own shows Status with **Log in as guest**, or
    times out cleanly.

## 6. Open questions for the owner (default in bold)

1. **Entry:** **Manage on the repeater or room detail, plus To… on the CLI page**; or also a menu on the Contacts row?
2. **Password store:** **shared with meshcore-cli (`<key>.pass`)**, or Mesh Hop's own?
3. **Guest without a password:** offer **Log in as guest** (blank) **even by flood**, one attempt, with the "whole
   mesh" note; or only when a route is known, as the official app's server sometimes enforces?
4. **What loads by itself:** **Status once after login; everything else on Refresh**; or nothing automatic, like the
   official app?
5. **S3 (admin parameters, telemetry, access list):** **later, only if wanted**; the CLI covers the one repeater he
   owns.
6. **Scope:** **repeaters and rooms; sensors later.**

## 7. Risks

- **Silent failures are normal.** A wrong password, an expired session and an out-of-reach repeater all look the same.
  The texts must say so.
- **The firmware moves.**
  - Older repeaters keep neighbours and owner info for admins and lack 0x07.
  - Newer firmware may change what guests can see.
  - The `xx|` prefix may be missing on older repeaters, which then needs order matching with a late-reply risk.
- **Flood logins and requests cost airtime.** One request at a time and no automatic retry keep this in check.
- **The admin right persists** on the repeater whatever "Log out" does; the page says so.
- **meshcore-cli's own `login`** still uses the name (section 4.1). The Help should advise logging in on the Repeater
  page.

## 8. Sources

- **U:**
  - meshcore_py 2.3.17, bundled in `apps/mesh-hop/root/opt/mesh-hop/cli/py/meshcore`: `commands/binary.py`,
    `commands/base.py:325-429`, `commands/messaging.py:29-92`, `packets.py`, `parsing.py:66-107`;
  - meshcore-cli 1.6.6 `meshcore_cli.py`;
  - MeshCore 81dbae5: `examples/simple_repeater/MyMesh.cpp`, `examples/simple_room_server/MyMesh.cpp`,
    `examples/companion_radio/MyMesh.cpp`, `src/helpers/CommonCLI.{h,cpp}`, `src/helpers/CommonRadioPrefs.cpp`,
    `docs/cli_commands.md`. The firmware files were read from the scratchpad copies (`fw-src`), and
    `CommonRadioPrefs.cpp` was fetched with `gh` today.
- **A:** https://files.liamcottle.net/MeshCore/v1.50.0/MeshCore-v1.50.0+78-b7528a7-web.zip (`main.dart.js` string
  literals, `assets/CHANGELOG.md`), extracted with read-only Python run with `-I`.
- **S:** apps repo `apps/mesh-hop` (branch `mesh-hop-cli`, working tree): `src/core/client.hpp:163-207`,
  `src/core/protocol.hpp`, `src/ui/cli_logic.hpp`, `src/ui/ui_logic.hpp:127,476`, `src/ui/mesh_app.cpp:3521-3534`,
  `tools/meshcore_sim.py`, `README.md:199-244`.

## 9. Gap survey: official app vs Mesh Hop

Added 2026-10-23. The **whole** official app v1.50.0 (same build as section 2: English UI strings of `main.dart.js` and
`assets/CHANGELOG.md` v1.0.0-v1.50.0, read as data) against Mesh Hop 0.6.0 plus the uncommitted CLI work (`apps-cli`,
`mesh-hop-cli`). The repeater and room flow is sections 2-5 and is not repeated. App versions "v1.x" are changelog entries
(A). Firmware lines (U) are from the scratchpad copy `src-dl/MeshCore`. Verdicts: **SAME**, **MISSING**, **DIFF** (done
differently; the more correct one is named when it matters).

### 9.1 Area by area

| Area | Official app (A) | Mesh Hop today (S) | Verdict |
| --- | --- | --- | --- |
| Connect, pair | BLE / USB / Wi-Fi tabs, last tab first, Wi-Fi history, PIN banner, "Exit MeshCore?" | one list, last node highlighted, nothing auto-connects, PIN box, BLE link returns, "Quit Mesh Hop?" (README:108-124) | SAME |
| Node settings | change BLE PIN (CMD 37), Wi-Fi SSID (v1.50), local companion CLI (CMD 66, fw 1.18) | none (`protocol.hpp:23-56`) | MISSING, low |
| Contact list | types + favourites filter; A-Z / heard / latest messages; **search by name or `<abcd` key prefix** (v1.21) | types, heard age, deck groups; 6 sorts (`ui_logic.hpp:126-158`); **no text search**, letters are shortcuts (README:281) | DIFF; search MISSING |
| Favourites | star, filter; "Overwrite Oldest" replaces the oldest **non-favourite**; purge keeps favourites | no favourite (`groups.hpp:4-6`), yet offers "Replace the oldest when the list is full" (`ui_phase2.cpp:457`): nothing is protected | MISSING (flag bit 0, honoured by the firmware) |
| Add / share contact | add by key, `meshcore://contact/add?type&name&public_key` link, QR show/scan, copy key, share zero-hop (CMD 16), export/import (17/18) | add from Nearby only (`client.hpp:113`); own key not on Settings (`mesh_app.cpp:3630-3647`) | MISSING |
| Auto-add | on/off, per type, overwrite, max hops (v1.41) | same minus max hops (read only, `protocol.hpp:270-277`) | SAME; max hops MISSING |
| Path | path view **with repeater names**, red when a hash matches several; set path; **Reset Path**; send Direct/Flood (v1.21) | route as hex hashes (`ui_phase2.cpp:190-200`); reset only inside retries (`client.cpp:945-952`) | MISSING |
| Ping, discover | Ping (zero hop) = a 1-hop trace (I; "Respond to path tracing and zero hop pings"), warning when contacts share the first byte; discover tool, saved list | Nearby: zero-hop discover, add/ignore, session only (`model.hpp:97-134`) | discover SAME; ping MISSING |
| DM delivery | 3 tries flood / 5 direct (v1.14), last try flood, Auto Reset Path, random gap (v1.40), **late ack of any try marks delivered** (v1.19), Retry Sending | 1-4 tries, "Forget the route before try N" (`model.hpp:145-149`); ack matched only while in flight (`client.cpp:957-966`): an ack after "no ack" is lost; no manual retry | DIFF; **official more correct** on late acks |
| Channel delivery | "Heard N repeats" with paths; **Send Again** when none (v1.35) | "heard back by N repeaters" (`ui_logic.cpp:1276-1279`); no resend | SAME; resend MISSING |
| Message info | hold: sender time, SNR, hops, paths; copy, reply, delete one, block sender | head = sender + `HH:MM` (`mesh_app.cpp:3351-3357`); hops and SNR stored, never shown (`model.hpp:36-38`) | MISSING |
| Dates | date dividers, "New Messages" divider, jump to oldest unread | `HH:MM` only, no date (`ui_logic.cpp:235-244`) | MISSING |
| Length | DM / room 150 characters (v1.31); channel from own name, 10 B less when scoped (v1.16, v1.42) | **bytes**: DM 133, channel min(133, 148 - name) (`client.cpp:832-840`). Firmware: 160 B, channel `name: ` included and cut silently (U: `BaseChatMesh.h:8`, `.cpp:453-454, 527`) | DIFF: bytes are right, but 17-27 B unused |
| Mentions | Reply / `@` inserts `@[name]`; own mention highlighted (code tests `"@[" + name + "]"`) | raw text | MISSING |
| Alerts, mute | system notifications, per channel All / Mentions / None | unread counts, mute (README:131-136), switcher card (README:38-42) | DIFF (platform); mute SAME |
| Search, dupes | search DMs and channels; sender retries shown once | richer search (README:150-153); de-dup (`model.cpp:616`) | SAME |
| Hashtag channel | `#` + a-z, 0-9, hyphen, no edge or double hyphen | `#` + 1-30 bytes without spaces, **upper case allowed** (`sha256.cpp:76-84`, `ui_logic.cpp:221-223`) | **DIFF, official correct**: key = SHA-256 of the exact name (U: meshcore_py `device.py:235-236`); `#Paris` is not `#paris` |
| Private channel | random / typed key, not all zeros, **duplicate key refused**, QR + `meshcore://channel/add?name&secret` | same minus duplicate-key check (only the name, `client.cpp:1123`) and QR; key as text | SAME; small gaps |
| Regions | channel scope, default scope, discover regions | never sets one; the board's default scope applies (`client.cpp:820-827`; U: companion `MyMesh.cpp:502-524`) | MISSING, low (I: matters only if local repeaters deny unscoped floods) |
| Radio | presets, freq/BW/SF/CR, signed TX power, repeat with ranges + confirm, path hash size with old-firmware warning | same set (README:184-187, `ui_logic.cpp:816-823`); presets carry no hash size | SAME |
| Node name | ≤ 31 B, not blank, **no `[` `]`** (v1.27, mention syntax), trimmed | ≤ 31 B, not blank, no control characters (`model.cpp:79-86`) | DIFF, official correct |
| Position in adverts | "Share Position in Advert" switch, warning: public, can reach the internet | read-only hint (`mesh_app.cpp:4816-4817`); policy byte preserved (`client.cpp:1360-1365`) | MISSING switch |
| Adverts | zero-hop / flood / clipboard; no app schedule (I) | + **app schedule 1/3/6/12 h**, flood allowed (`model.cpp:164`) | DIFF: 1 h flood is under the 3 h floor the firmware sets for repeaters (U: `CommonCLI.cpp:495`) |
| Clock | sync; "cannot go backwards" | sync (NTP, GPS, typed), refusal explained (`client.cpp:367`) | SAME |
| Device info | model, fw version and date, **storage**, battery, **own key**, link type | firmware, battery, clock (`mesh_app.cpp:3630-3647`); storage parsed only (`protocol.hpp:192-196`) | MISSING (cheap) |
| Telemetry | own; request a contact's (battery, GPS, sensors, auto 10 s); per-contact permissions | none; mode byte preserved | MISSING |
| Reboot, reset | confirm each | confirm; reset: 2 questions, 3 s Yes, key check (`mesh_app.cpp:6037`, README:328-335) | SAME (ours safer) |
| Keys, config | export/import private key; config JSON (contacts, channels, regions, identity) | none; deck caches per board (README:339-351) | MISSING |
| Tools, map | trace path (list/map, return path), RX log, noise floor, LOS, coverage, maps | packet log, statistics with noise floor (README:181-183); no trace, no map (README:355) | trace, map MISSING |

### 9.2 Top gaps for a small-screen deck owner (ranked)

| # | Gap | Why on the deck | Cost |
| --- | --- | --- | --- |
| 1 | **Message info and actions**: select a message, Ctrl+O / hold → sender time, SNR, hops, route; **Retry** (DM no ack), **Send again** (channel, heard back 0), **Reply** `@[name]` | no way today to see how a message came, or to resend without retyping on the on-screen keyboard | 1 (messages become selectable) |
| 2 | **Hashtag names in lower case** a-z, 0-9, `-` (type-time lower-casing) | a capital letter silently splits the deck from every phone on that channel | **cheap** 0.25 |
| 3 | **Ping / trace** a repeater from its detail (CMD 36, push 0x89): SNR each hop, both ways; warn when two contacts share the hash | "do I reach my repeater?" with one small packet, no login | 1-1.25 |
| 4 | **Reset path** + **Send by flood** (contact detail, chat menu) | today a dead route costs failed tries first | **cheap** 0.25 |
| 5 | **Late ack → delivered**: keep a finished DM's ack codes ~2 min | ends false "no ack" and duplicate resends | **cheap** 0.25 |
| 6 | **Route with repeater names**, "2 candidates" when a hash is shared | hex hashes say nothing on a small screen | **cheap** 0.5 |
| 7 | **Dates** in conversations (a divider, or "Mon 14:02") | 200 messages per chat span days | **cheap** 0.25 |
| 8 | **Share by QR**: my contact and a channel as `meshcore://` links; own key on Settings | a phone with the official app adds the deck or joins by camera, no 64-hex typing | 0.75 (I: `lv_qrcode` not confirmed in `cp0_lvgl`) |
| 9 | **Contact search** (name or key prefix) | 160+ contacts; also assumed by section 4.2's picker | 0.5 |
| 10 | **Favourites** (bit 0): filter, spared by "Replace the oldest" and bulk delete | overwrite can drop the owner's own repeater today | 0.5 |

**Cheap safety trims (about 0.5 together):**
- a Share-position switch with the warning;
- no 1 h choice for a flood schedule (zero-hop keeps it);
- names without `[` `]` and trimmed;
- refuse a private key already on the board;
- DM limit 150 B, channel 160 - (name + 2) - 10.

**Later, only if asked:** telemetry (S3), map, key/config export, BLE PIN, block sender, regions, mention alerts (need
launcher notifications, 035), 5 tries (the firmware level for tries above 4 is I; U: `BaseChatMesh.cpp:454-468`).

**Notes.**
- Correction to 4.2: Contacts has **no** search today (`ui_logic.hpp:135-139`); the To… picker needs gap 9, or makes do
  with the type filter and sort by name.
- Mesh Hop is ahead on search filters, groups, bulk delete with progress, reset guards, byte counter, packet log,
  "Firmware too old" rows and the preset feed.
- Inferred (I): ping as a 1-hop trace; the app's exact channel-limit formula; whether the app counts characters or bytes;
  whether regions matter on the owner's mesh.
