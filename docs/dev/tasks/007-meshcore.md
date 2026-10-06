# 007 - MeshCore messaging app (first version)

Status: building. Owner: apps-dev. Verifier: deck-verifier. Docs: docs-writer. Acceptance: the user, on the deck with their board.

## Goal
A native touch LVGL app in the apps repo (`apps/meshcore`) that makes the deck a MeshCore messenger: it controls a
MeshCore **companion radio board over USB serial** (Bluetooth LE later), like the user's model project wadamesh
(a touch UI for MeshCore firmware) but with the radio on a separate board. LoRa itself is done by the board; the app
speaks the MeshCore companion protocol. The user has a board and will plug it in; the model is not known yet (find
it from USB vendor/product ids when it appears).

## Scope of the first version (all four chosen by the user)
1. **Contacts + adverts**: list contacts/nodes (name, type, last heard, signal if available), send an advert
   (flood and zero-hop if supported).
2. **Messages**: direct messages to a contact and channel messages (public channel and configured channels), read and
   send text; unread markers; touch + keyboard entry (BT keyboard and an on-screen LVGL keyboard).
3. **Radio status + settings**: connection state, device info (firmware version, node name, battery/voltage if
   reported), radio parameters (frequency, bandwidth, SF, CR, TX power), node name; edit and save them.
4. **Message history on disk**: keep sent/received messages and contacts cache between runs (`~/.local/share/meshcore`
   or the launcher's app data convention; JSON lines or SQLite-free flat files), bounded size.
Out of scope for v1: map with offline tiles, Bluetooth LE transport, Lua/other extensions, file transfer, firmware
flashing. Design the transport as an interface so BLE can be added (BlueZ GATT) without touching the rest.

## Technical notes
- Protocol: read `docs/dev/ref/README.md` and the files next to it first. The doc is BLE-oriented and "may be
  inaccurate": `docs/dev/ref/meshcore_py/` is a working reference client (framing in `serial_cx.py`, packets in
  `packets.py`/`reader.py`, commands in `commands_*.py`). Our code is written from these (MIT), **never copied from
  wadamesh (GPL-3)**.
- Layers: (a) `transport` (serial now: auto-detect `/dev/ttyACM*` / `/dev/ttyUSB*`, choose by USB id when several, retry
  and reconnect on unplug/replug, never block the UI thread); (b) `protocol` (pure C++, no LVGL, frame parser/encoder,
  commands/responses/push notifications) with unit tests using recorded/synthetic byte streams; (c) `model`
  (contacts, conversations, settings, history store); (d) `ui` (LVGL pages). Keep (b) and (c) testable on the PC.
- Simulated peer for testing without a board: a small script (Python on the deck or WSL, pty or socat) that speaks
  enough of the companion protocol (device query, app start, contacts, a few messages, advert, send-message ack,
  get/set radio params). Put it under `apps/meshcore/tools/` so the verifier can run it; the verifier will use the real
  board when the user plugs it in.
- UI: 320x170 logical canvas scaled 2x into the launcher window with the shared top bar (see the Wi-Fi Survey and LAN
  Scan apps for structure, key handling with a possibly absent keyboard fd, raw-key listener on the page's own screen,
  letter keys via raw key codes, not ASCII; keycodes 17-20 collide with arrows). Touch must work (tap rows, tabs, a
  send button); every action also reachable by keys. Screens: Status/Home, Contacts, Chat (DM or channel), Compose,
  Settings. Show clear states: no board found, port busy/permission denied (user is in `dialout`), wrong firmware
  (not a companion radio), disconnected, sending / sent / failed.
- Package: `meshcore`, share code `meshcore`, title "MeshCore", category Network (or Radio if the registry allows),
  license MIT, depends none beyond libc (serial via termios; no extra libs). Icon 64x64 in the style of the existing
  app icons. Credits: MeshCore (MIT, the protocol and firmware), meshcore_py (MIT, used as reference). No Python
  dependency in the shipped app.
- Build and package like `apps/wifi-survey` (build.sh using the launcher worktree, WSL build lock, `tools/make_registry.py`,
  restore other packages' debs and registry entries afterwards). Never run git from WSL. Add a `copyright` file in
  `root/usr/share/doc/meshcore/` (Debian format, Files: on every stanza).

## Definition of done
`checklists/app-packaging.md` done; unit tests for the protocol layer pass; app runs against the simulated peer on the
deck (verifier PASS, report), then against the user's real board; screenshots of each screen; README page for the app
(docs-writer); the user accepts on the deck with the real radio.

## Constraints
Do not touch the launcher source (list any launcher capability you need as a request). No commit or push. Hand-off
report `docs/dev/reports/007-handoff.md` (ROLES.md format) with the protocol decisions (which commands used, anything
the doc got wrong versus the firmware/meshcore_py), what the verifier can test with the simulator and what needs the
real board and the user's touch.

## Decision 2026-10-06: board clock policy (user)
The deck has no RTC, so its clock is only trustworthy once it is network-synced; a MeshCore board with a GPS receiver
may have the correct time without any network. At connect the app picks one of:

| Deck clock (NTP synced = "online") | Board GPS | Action |
| --- | --- | --- |
| online | no GPS / GPS disabled | force the deck time into the board (SET_TIME) |
| offline | no GPS / GPS disabled | ask the user which date and time to start from, proposing the deck's current values (typed on the Bluetooth keyboard, no on-screen keyboard), then write that to the board |
| offline | GPS enabled | use the board's clock (read GET_TIME): do not write to it; message and "heard" times are shown from the board clock |
| online | GPS enabled | not specified by the user: default = do not write, use the board's GPS-disciplined clock; open question for the user if the GPS has no fix |

Notes for the implementation: "online" means the deck clock is trusted (`timedatectl show -p NTPSynchronized --value` = yes,
no root needed; NetworkManager connectivity as a fallback hint), not just a Wi-Fi link. Find out from the references what
the companion protocol exposes about GPS (enabled flag, fix state) and say what is and is not available. The firmware may
refuse a SET_TIME that goes backwards: handle the error with a clear message. Status shows where the clock came from
("deck NTP", "set by you", "board GPS") and Settings has a "Sync clock now" action that re-runs the policy.

## Update 2026-10-06: renamed MeshZero and shelved (task 009)
"meshcore" is the protocol/firmware brand of another project, so our app is **MeshZero** (`apps/meshzero`, version reset to
0.1.0, data dir `~/.local/share/meshzero` with a one-time migration from `meshcore`). It is committed as an unpublished
draft (`"draft": true` in app.json, skipped by `tools/make_registry.py`); the user will come back to it later. Physical
tests are pending: see `docs/dev/tests/pending-physical-tests.md`. Product rules set during this task: no on-screen
keyboard (Bluetooth keyboard for text), short Esc = Back and hold Esc 3 s to quit, board clock policy (table above).
Other MeshCore clients (wadamesh, Meshy, MeshCore Open) were evaluated: none runs as is on the deck, see `docs/dev/ref/README.md`.
A local package of the draft is built with `python tools/make_registry.py --only meshzero --out <folder outside packages/>`.
