# 014 - Mesh Hop feature feasibility (companion radio USB protocol)

Date 2026-10-07. Read-only research. Sources:
- V = verified in the local references: `ref/companion_protocol.md`, `payloads.md`, `packet_format.md`, `ref/meshcore_py/*.py` (packets.py, reader.py, commands_*.py), `ref/meshcore_cli.py`.
- W = verified on a public web page fetched today (meshcore-open README, MeshCore docs/faq.md, api.meshcore.nz config).
- I = inferred (not seen in a reference); needs a spike or a board test before building.

Already in `apps/mesh-hop/src/main/core` (V, read): APP_START, DEVICE_QUERY, GET/SET_CHANNEL, GET_CONTACTS, GET_CONTACT_BY_KEY,
SEND_TXT / SEND_CHANNEL_TXT, SYNC_NEXT, SET_RADIO, SET_TX_POWER, SET_NAME, RESET_PATH, SEND_ADVERT, GET/SET_TIME, GET_CUSTOM_VARS,
BATTERY; pushes ADVERT, PATH_UPDATE, ACK, MESSAGES_WAITING, LOG_DATA (only SNR/RSSI decoded, payload dropped), NEW_ADVERT,
CONTACT_DELETED, CONTACTS_FULL; direct-message retry with attempt counter and ack matching (client.cpp ~590-640).
Not in core: SET_CUSTOM_VAR, SET_OTHER_PARAMS, REBOOT, FACTORY_RESET, REMOVE/ADD/EXPORT/IMPORT/SHARE contact, login/logout,
trace, path discovery, stats, flood scope, auto-add config, binary requests, control data, CLI commands, raw packet parsing.

Effort: S = under a day-ish, one command + small UI; M = several commands/state + a new screen; L = new subsystem or unclear protocol.
"Core" = work in `core/`; "UI" = new screen/widget only. Phases: 1 = quick wins on existing core, 2 = contact/channel management,
3 = packet-log based features, 4 = remote admin and heavy items.

## Feature table

| # | Feature | Protocol support | How (command / event) | Effort | Notes / risks | Phase |
|---|---|---|---|---|---|---|
| M1 | Reply to a specific message | NO native; app convention only (I) | No reply field in TXT/GRP_TXT payloads (V: payloads.md has timestamp, txt_type+attempt, text only). meshcore-open advertises "inline reply" (W) so it must be a text prefix. | S UI + S core | Other clients will only see the prefix text. Local-only quoting (show the quoted line in own UI, send plain `@[name] text`) is safe. Cannot match meshcore-open's exact wire format without reading its source (not verified). | 2 |
| M2 | Emoji reactions | NO native; text convention (I) | Same as M1: meshcore-open has reactions (W) over plain text. 133-byte limit and airtime cost per reaction. | M | Needs the emoji font spike (task 010). Each reaction is a real message that floods the channel: warn user. Format interop unverified. | 3 |
| M3 | Auto retry of failed send with path reset | YES for direct (V) | SEND_TXT attempt byte (0..3) + RESET_PATH (13) after N failures, as `send_msg_with_retry` (flood after 2 attempts, same timestamp each attempt). Already partly in core. Channels: no ack, so retry = user "resend" only. | S | Make attempts/flood_after settings. meshcore-open: "automatic retry with configurable path clearing" (W). | 1 |
| M4 | Mute a channel | Local only (no protocol need) | App-side flag in the store; suppress unread/notification. | S | None. | 1 |
| M7 | Message archive, filters, retention | Local only | Store already holds 1500 msgs / 200 per conversation (model.hpp). Add retention setting, search/filter UI, export. | M (store + UI) | RAM/flash budget on the deck; board queue itself is not an archive. | 2 |
| M10 | Room server login/logout, history replay | YES (V) | LOGIN: CMD 26 (32-byte key + password) -> MSG_SENT then push LOGIN_SUCCESS 0x85 (perms, is_admin, server_timestamp, fw level) / LOGIN_FAILED 0x86; LOGOUT: CMD 29. Room payload carries a "sync since" timestamp (V payloads.md "Room server login"); room posts then arrive as CONTACT_MSG with txt_type SIGNED_PLAIN (2: 4-byte author prefix + text) via SYNC_NEXT. | M | Companion command 26 has no field for the sync-since timestamp in meshcore_py (V), so replay depth is firmware decided: verify on a real room server. Keep-alive (binary req 0x02) needed to stay logged in (I). | 4 |
| C1 | User contact groups | Local only (partial) | App-side groups keyed by public key. Contact `flags` byte (bit 0 favourite, V cli help) is the only on-board marker. | S | Groups are not on the board: lost on factory reset unless backed up. | 2 |
| C2 | Filter by type / time since heard | YES (local) | Contact record has type, last_advert, out_path_len (V). Core already tracks heard_local. | S (UI) | Slow list with 161 contacts is a UI virtualisation matter (feedback 4, 5). | 1 |
| C3 | Bulk delete, auto-add toggle | YES (V) | REMOVE_CONTACT CMD 15 (32-byte key) one by one; push CONTACT_DELETED 0x8F; SET_AUTOADD_CONFIG 58 / GET 59 -> AUTOADD_CONFIG 0x19; manual-add flag also in SET_OTHER_PARAMS 38 (byte "manual_add_contacts", V). | S-M | Bulk = loop with a progress popup and confirm; no bulk command on the wire. Pending (not auto-added) contacts arrive as push NEW_ADVERT 0x8A; add by ADD_UPDATE_CONTACT 9 (V cli: pending_contacts, add_pending). | 2 |
| C4 | Discover nearby nodes, add/ignore | YES (V) | (a) passive: pushes ADVERT 0x80 / NEW_ADVERT 0x8A when manual-add is on; (b) active zero-hop: CMD 55 SEND_CONTROL_DATA with DISCOVER_REQ (type filter bits, 4-byte tag), answers as push CONTROL_DATA 0x8E / DISCOVER_RESP 0x90 (node type, SNR, pubkey 8 or 32 bytes) (V packets.py, payloads.md, cli `node_discover`). | M | Control packets are 0-hop only. Need to build the heard-list model and add/ignore actions (ADD_UPDATE_CONTACT). Note 0x90 is both a push code (CONTACTS_FULL) and a control subtype: different namespaces. | 2 |
| C5 | Share contact (card export/import, QR) | YES (V) | EXPORT_CONTACT CMD 17 (empty = self) -> CONTACT_URI 0x0B; IMPORT_CONTACT CMD 18 + card bytes; SHARE_CONTACT CMD 16 floods it as a zero-hop advert-share. | M | QR display needs a QR generator (small MIT lib) - no scanner on the deck (no camera known): import by paste/typing the `meshcore://` URI or by receiving a shared advert. | 3 |
| C6 | Edit/reset path, manual override, automatic route rotation | YES (V) | Reset: CMD 13. Manual path: ADD_UPDATE_CONTACT CMD 9 with out_path_len + 64-byte path (path_hash_mode in top 2 bits, V commands_contact.update_contact). Path learned: push PATH_UPDATE 0x81. Discover: CMD 52 PATH_DISCOVERY -> PATH_DISCOVERY_RESPONSE 0x8D. | M | "Automatic rotation" has no firmware feature: app logic (reset after N failures, M3). Writing a bad path silently breaks delivery: confirm dialog, "back to flood" button. | 3 |
| C7 | Trace path and ping | YES (V) | Trace: CMD 36 SEND_TRACE_PATH (tag, auth, flags, path hashes) -> push TRACE_DATA 0x89 (per-hop SNR). "Ping" = trace to a direct repeater, or telemetry/status request (CMD 27 / binary req 50) and measure time. | M | Trace needs the path hash chain; unknown routes use PATH_DISCOVERY. Round-trip timing is app side. | 3 |
| C8 | Contact details (key, last advert, position, telemetry) | YES (V) | Key, type, last_advert, adv_lat/lon, path already in CONTACT frame. GET_ADVERT_PATH 42 -> ADVERT_PATH 0x16. Telemetry: BINARY_REQ 50 type TELEMETRY (3) -> BINARY_RESPONSE 0x8C or TELEMETRY_RESPONSE 0x8B (CayenneLPP, big-endian); STATUS (1) for repeaters (battery, uptime, airtime, packets); telemetry permission flags must be allowed by the remote (change_flags tel_l/tel_a). | S (static) + M (telemetry) | Telemetry only works for nodes that grant it; many repeaters will time out. | 2 (static) / 4 (telemetry) |
| H1 | Private and community channels (key entry) | YES (V) | SET_CHANNEL 32 with idx, 32-byte name, 16-byte secret (V companion_protocol.md). Key entry as 32 hex chars; random key generation; share key as text. Hashtag = first 16 bytes of sha256("#name") (already in core). Slot 0 public key 8b3387e9c5cdea6ac9e5edbaa115cd72. | S-M | "Community" is a client concept (a shared named key, W meshcore-open) not a protocol type. Bluetooth keyboard typing 32 hex chars: paste not available -> allow QR/URI later. Max channels from DEVICE_INFO. | 1 |
| R1 | Regions: scope floods, home region, allow/deny | PARTIAL (V) | Scope the companion's floods: SET_FLOOD_SCOPE CMD 54 (16-byte key = first 16 bytes of sha256("#region"), zeros = none; flag 1 = force unscoped); default scope SET/GET_DEFAULT_FLOOD_SCOPE 63/64 -> DEFAULT_FLOOD_SCOPE 0x1C; packets carry transport codes (packet_format V). Reading a repeater's regions: anon request REGIONS (0x01) (`req_regions`). Allow/deny flood per region is a repeater CLI setting (`region allowf/denyf/put/save`, V cli list), done through remote CLI (A1), not on the companion. | M | The companion only chooses the scope of what it sends. Region admin = A1. Whether scope applies to channel messages only or also adverts: not verified. | 3 |
| D1 | All firmware presets as a list | YES (local list) | Radio set via SET_RADIO CMD 11 (freq kHz, bw kHz, sf, cr [, repeat byte]). Preset list below (W). | S | Keep list as data file; mark "(Deprecated)". Companion firmware may refuse out-of-band values (ERR illegal arg). | 1 |
| D2 | TX power, advert interval, repeat on/off | PARTIAL (V) | TX power: SET_RADIO_TX_POWER 12 (already in core; max in SELF_INFO). Repeat: trailing byte of SET_RADIO 11 and DEVICE_INFO byte 80 "client repeat" (fw v9+) (V); allowed repeat bands: GET_ALLOWED_REPEAT_FREQ 60. Advert interval: NOT a companion command; the companion advertises when the app calls SEND_SELF_ADVERT 7 (flood flag) or on its own firmware setting (CLI `set advert.interval` is for repeaters). App can schedule adverts itself. | S-M | Client repeat is only allowed on certain frequencies; offer only when GET_ALLOWED_REPEAT_FREQ returns the current freq. Advert interval = app timer (I). | 2 |
| D3 | Owner info | PARTIAL (V) | Companion node name: SET_ADVERT_NAME 8 (in core). `owner.info` is a repeater CLI setting; reading a repeater's owner: anon request OWNER (0x02). No owner field in SELF_INFO. | S | Offer node name (done) + view repeater owners. | 4 |
| D5 | Reboot / power off / factory reset | PARTIAL (V) | REBOOT CMD 19 (V). FACTORY_RESET CMD 51 (V, payload byte `33`). Power off: no companion command in the references (I: none). | S | Double confirm for factory reset (erases keys and contacts). Reboot drops the USB link: reconnect logic exists. | 2 |
| D6 | Board GPS on/off and position | PARTIAL (V) | Custom variable `gps` read by GET_CUSTOM_VARS 40 (core already reads it for clock policy), set by SET_CUSTOM_VAR 41 (`key:value`). Position: SELF_INFO lat/lon, or SET_ADVERT_LATLON 14, advert location policy in SET_OTHER_PARAMS. | S-M | Only boards with a GPS list the var (feedback 7). Live position updates: no push; re-read SELF_INFO / custom vars (I). | 2 |
| D8 | Packet log start/stop/view/erase | PARTIAL (V) | Radio RX log: push LOG_DATA 0x88 (SNR, RSSI, raw on-air packet) arrives continuously, nothing to enable in the references (core currently drops the payload). Parse header, route, payload type, path (packet_format V). No "erase" or board-side log command: log is app-side. | M-L | Start/stop/erase = app-side buffer. Encrypted payloads cannot be read without keys (channels can, via known channel keys, as reader.py does `decrypt_channels`). Bound the buffer for RAM. | 3 |
| D9 | Statistics (packets, airtime, errors) | YES (V) | GET_STATS CMD 56 + subtype: 0 core (battery mV, uptime, errors, queue), 1 radio (noise floor, last RSSI/SNR, tx/rx airtime s), 2 packets (recv, sent, flood/direct tx/rx, recv_errors in 30-byte frame) -> RESP 0x18. | S-M | Needs firmware that has CMD 56 (newer); fall back to hiding. | 2 |
| D10 | Firmware version check, "too old" notice | YES (V) | DEVICE_INFO (fw_ver byte, build, model, version string); compare to a minimum per feature (e.g. stats, scope, repeat need newer). | S | "Latest released" needs internet and is not in the protocol: only a built-in minimum. | 1 |
| D11 | BLE and TCP transports | PARTIAL | Same frames: BLE uses GATT NUS UUIDs (V companion_protocol.md) with no 0x3C/0x3E framing; TCP is available in meshcore_cli (`-t host`) and meshcore-open (W) with the same serial-style framing (I). Serial is what the deck uses. | BLE: L; TCP: S-M | Transport interface already abstract (transport.hpp). BLE needs BlueZ/D-Bus + pairing PIN on a Pi where Bluetooth is also used by the keyboard. TCP only useful for WiFi boards (none by default). Recommend: not now. | 4 (TCP), later (BLE) |
| A1 | Remote command line on a repeater | YES (V) | After LOGIN (M10 flow): send text with txt_type CLI_DATA/CLI_CMD (TxtType 1/3) via SEND_TXT (CMD 2, txt_type byte) (V commands_messaging.send_cmd); replies come back as CONTACT_MSG with txt_type CLI_DATA (1) via SYNC_NEXT (V). No ack for CLI commands (V payloads.md). Local-device CLI: RUN_CLI_COMMAND CMD 66 -> CLI_REPLY 0x1D (V). | M | Reply matching by order only (no tag); 133-byte limit per line; slow multi-hop. Terminal tab of task 010 fits. | 4 |
| P1 | Map filter by node type and time | YES (local) | Contact type/last_advert/lat/lon from CONTACT frames. | S (after map exists) | Depends on the map tab (task 010 phase 3). | 3 |
| P2 | Share location, custom markers | PARTIAL | Own location: SET_ADVERT_LATLON 14 + advert (V). Custom markers: app-local only. Sharing a marker to others: no protocol (text convention only, I). | S (own pos) / M (markers) | Be careful: advertising position is a privacy action; confirm. | 3 |
| P4 | Offline map tiles | NO protocol need (not a radio feature) | Tile storage/downloader on the deck (task 010 option B). | L | Tile source policy, storage and PNG decode RAM; keep for later. | later |

## Answer (a): channel message stays "sending"

Verified facts:
- Channel send (CMD 3): meshcore_py waits for **OK** (`send_chan_msg` accepts OK/ERROR) while the doc says MSG_SENT 0x06. Core already accepts both (client.cpp ~657) and marks "Sent".
- A channel message has **no ack**. The ACK push (0x82, 4-byte code + trip time) only answers a direct message whose expected_ack came back in MSG_SENT (V: reader.py ACK, send_msg_with_retry). Group text is a broadcast: no recipient, no ack packet (V: payloads.md has ack only for TXT/path; GRP_TXT has none). So "delivered" is impossible for channels.
- Therefore the app must stop at the end of the radio stage: the status "sending" should end as soon as OK/MSG_SENT arrives (or fail on ERROR/timeout). If the user saw "sending" for ever, the state is probably not being cleared when the OK frame is not matched (bug to check: in the board test the reply may be a different code such as MSG_SENT with a different timing, or queued behind an unrelated frame). To check on the deck: log the frame code returned for CMD 3. I did not run the board.

Honest status model (proposal):
| Direct message | Channel message |
|---|---|
| Sending (queued to the board) -> Sent (MSG_SENT, flood or direct shown) -> Delivered (ACK with matching code, show trip_time) / No ack after retries | Sending -> Sent (board accepted; on air) -> "Heard by N" (echo count, see below) ; never "Delivered" |
A reply arriving in the same channel may be shown as a soft hint but does not prove delivery of one message.

Counting the repeaters that heard a sent message (I, based on V building blocks):
- The board forwards every received on-air packet as LOG_DATA 0x88 (SNR, RSSI, raw packet) (V reader.py). Repeaters re-broadcast a flooded packet, so our own radio hears the **echo** of our message.
- Method: after sending, remember the sender timestamp and text; for each LOG_DATA parse the header (payload type GRP_TXT 0x05 for channels, TXT_MSG 0x02 for direct; packet_format V). For channel messages decrypt with the channel key (16-byte secret, known from GET_CHANNEL; AES-128 ECB + 2-byte HMAC, as in reader.py `findLogChannelMsg` which hashes sender_timestamp+text to match, V) and compare timestamp + text; match counts as an echo. The echo's **path** (hop hashes, 1-3 bytes each; path_length byte encodes count/size) lists the repeaters it went through; the last hash is the repeater we heard it from. Count **distinct first-hop hashes** (last path element) = repeaters that retransmitted it within hearing range; for deeper counts count distinct path hash chains.
- Limits: only repeaters within our own radio range are counted; hashes are 1 byte (collisions possible, resolve against contact list repeaters only as hint); a repeater that heard but whose retransmission was suppressed (already seen) is not counted; no echo when we are the only node. So the figure is "heard back from N repeaters".
- Direct messages: the ACK/returned path (PATH push 0x81, `out_path_len`) gives the **route and hop count** of the delivery, and in flood mode the same echo counting applies to TXT_MSG packets (payload is encrypted with the recipient key; match by the packet's hash/ciphertext bytes instead of decrypting - needs the identical-ciphertext match, I). Also the ACK's `trip_time` is verified.
- Needs: parse the payload of LOG_DATA in core (currently ignored), a short ring buffer of pending sent messages (30-60 s window), AES-128 and HMAC-SHA256 code (MIT or own; core has SHA-256 only) - effort M. Add packet parsing once and reuse for D8 packet log.

## Answer (b): radio presets

Source (W): `https://api.meshcore.nz/api/v1/config` (the suggested radio settings list used by the official apps/flasher; fetched 2026-10-07). Not found in the local references (`meshcore_cli.py` has no presets). Frequency MHz, bandwidth kHz, SF, CR (5..8 means 4/5..4/8 as in SET_RADIO).

| Name | Freq MHz | BW kHz | SF | CR |
|---|---|---|---|---|
| Australia | 915.800 | 250 | 10 | 5 |
| Australia (Narrow) | 916.575 | 62.5 | 7 | 7 |
| Australia (Mid) | 915.075 | 125 | 9 | 5 |
| Australia: SA, WA | 923.125 | 62.5 | 8 | 8 |
| Australia: QLD | 923.125 | 62.5 | 8 | 5 |
| Brazil | 923.125 | 62.5 | 8 | 8 |
| Canada | 910.525 | 62.5 | 7 | 5 |
| Costa Rica | 910.525 | 125 | 11 | 5 |
| EU/UK (Narrow) | 869.618 | 62.5 | 8 | 8 |
| EU/UK (Deprecated) | 869.525 | 250 | 11 | 5 |
| Czech Republic (Narrow) | 869.432 | 62.5 | 7 | 5 |
| EU 433MHz (Long Range) | 433.650 | 250 | 11 | 5 |
| EU 433MHz (Narrow) | 433.650 | 62.5 | 8 | 8 |
| Hungary | 869.618 | 62.5 | 7 | 5 |
| Netherlands | 869.618 | 62.5 | 7 | 5 |
| Netherlands (Limburg) | 869.618 | 62.5 | 8 | 8 |
| New Zealand (Narrow) | 917.375 | 62.5 | 7 | 5 |
| New Zealand (Gisborne) | 917.375 | 250 | 11 | 5 |
| Portugal 433 | 433.375 | 62.5 | 9 | 6 |
| Portugal 868 | 869.618 | 62.5 | 7 | 6 |
| Slovakia | 869.618 | 62.5 | 7 | 5 |
| Switzerland | 869.618 | 62.5 | 8 | 8 |
| USA | 910.525 | 62.5 | 7 | 5 |
| USA - Southern California | 927.875 | 62.5 | 7 | 5 |
| Vietnam (Narrow) | 920.250 | 62.5 | 8 | 5 |
| Vietnam (Deprecated) | 920.250 | 250 | 11 | 5 |

26 entries. The list changes over time (the "Deprecated" ones show it): store as a data file with a date and the URL; the firmware has no command to list presets (V). Core `lora_bandwidths()` must include 62.5 and 250 (check 62.5 is accepted: SET_RADIO sends bw*1000 = 62500).

## Cannot be done (or not as asked) and what to offer instead

- M1 / M2 native reply and reaction: no protocol field. Offer: local quoting of a message in the compose box (text prefix) and reactions as short channel texts, labelled as "not understood by clients without the convention"; decide only after reading meshcore-open's actual format.
- C1 groups on the board: none. Offer: app-side groups with a backup file.
- D2 advert interval on the companion: no command. Offer: app timer that calls SEND_SELF_ADVERT (flood/zero-hop), default off.
- D5 power off: no command found. Offer: reboot only.
- D8 erase/start/stop of a board log: no board log. Offer: app-side capture with start/stop/clear.
- R1 allow/deny flood per region on the companion: only through a repeater admin session (A1). Offer: set scope on the companion; region editing in the repeater terminal.
- Channel "delivered": impossible. Offer: "Sent" + "heard back by N repeaters".
- C5 QR scanning: no camera. Offer: QR display and URI text export/import.
- D11 BLE: heavy and conflicts with the keyboard Bluetooth use; TCP only for WiFi boards. Offer: later, if a user needs it.
- P4 offline tiles: not protocol; large effort; keep the tile-less plot first (decision already taken).

## Suggested order

1. Phase 1: M3 settings, M4, C2, D1 (preset list popup), D10, H1 keys, channel status fix.
2. Phase 2: C3, C4, C8 static, D2 (repeat, TX), D5, D6, D9, M7, C1.
3. Phase 3: LOG_DATA parser (heard-by count, D8), C5, C6, C7, R1 scope, P1, P2, M2.
4. Phase 4: M10, A1, telemetry, D3 owner view, D11 TCP, P4.

Unverified and worth a board test before coding: the actual frame returned for CMD 3; channel echo detection via LOG_DATA on the deck's board; SET_FLOOD_SCOPE effect; FACTORY_RESET and GET_STATS support on the user's firmware version.
