# 033 - A CLI / terminal for Mesh Hop: what it would be and how to build it

Status: study written 2026-10-23, **revised the same day** for the owner's new direction (below). Untracked in the
launcher worktree (detached at v0.8.5, d0cb456). Apps repo read at 786d74b (branch `publish-repin-0.8.5`), Mesh Hop
0.5.3. Upstream cloned read-only into the session scratchpad: meshcore-cli dfcc74e (v1.6.6, 2026-10-14), meshcore_py
105f407 (v2.3.17, 2026-10-16), python-prompt-toolkit 6c7fbc1 (3.0.53+, 2026-10-20). None of it was run. No product
code changed, nothing committed, no board touched.

Markers: **S** = read in our source (file:line); **U** = read in the upstream source cloned today (file:line);
**W** = read on the web today (URL in section 12); **I** = inferred, needs a board or a run. Cost unit: one worker
session.

## What changed in this revision

- **New direction from the owner.** Mesh Hop does not re-implement a meshcore-cli-style command language, and does not
  speak the repeater CLI itself. It runs the **real meshcore-cli** (and its library `meshcore`, from meshcore_py) in a
  pty on a Mesh Hop page. An upstream protocol change then means upgrading Python packages, not rebuilding the app.
- **The owner's requirements, recorded:**
  - the page opens from a Home **CLI** row;
  - it works for the companion itself and for remote repeaters and room servers administered over LoRa through the
    connected node (no USB to the repeater);
  - the password is remembered per repeater;
  - no confirm box for dangerous commands, but a **Help** button listing the commands, with warnings on the dangerous
    ones;
  - order of work: **notifications first** (report 035), then this console, then a read-only status / neighbours /
    owner view of repeaters the owner does not administer.
- **Dropped** from the first version:
  - B1, the native remote-admin console: login box, CLI text frames built in C++, a Yes/No list;
  - B3, the app-side command language;
  - the "passwords never stored" decision.
  - A (the read-only view) is no longer an alternative to the console: it is the third step.
- **New:** upstream facts (licence, dependencies, Debian 13, wheels), how meshcore-cli behaves in a session (4.2), the
  three ways to share the node (section 5), the terminal front ends (4.4), packaging and updates (4.5), the official
  apps' remote management (3.2), and new options, plan and decisions.
- **Kept:** the protocol facts of 2.1 and 2.2 (now background: meshcore-cli implements them), and the text-type
  finding, which is now a prerequisite fix.

## 1. Summary

- **What it is.** A Home **CLI** row opens a page that runs upstream **meshcore-cli** (MIT, Python 3.10+) in its chat
  mode. Mesh Hop carries frames and draws text; all command handling stays upstream.
  - At the root (the companion): `infos`, `ver`, `clock`, `advert`, `contacts`, `get`/`set`.
  - For a repeater or room: `to <name>`, `login` (meshcore-cli saves the password per repeater key), any repeater CLI
    command, plus `rs` / `rn` / `rt` (status, neighbours, telemetry), `trace` and `?<cmd>` help.
  - The companion's own text CLI (`cli <cmd>`, command 66) needs firmware v1.18 (level 14). The owner's boards
    reported level 11 on 2026-10-10.
- **How it reaches the node: a loopback relay (option b, recommended).**
  - Mesh Hop keeps its link (USB, BLE or IP) and opens a port on 127.0.0.1 for the session.
  - It starts `meshcli -t 127.0.0.1 -p <port>` and passes frames through while its own command queue is paused.
    meshcore_py frames TCP exactly as serial (U: `tcp_cx.py:74-153`).
  - Mesh Hop still parses every frame (S: `client.cpp:483-484`), so Chats, unread counts and notifications go on while
    the console is open.
  - The alternatives are worse:
    - **(a) handing the node over** loses every message to the CLI, which syncs and auto-fetches the node's queue
      (U: `meshcore_cli.py:1084`, `:637`), and needs Bluetooth LE in Python;
    - **(c) a separate app** fights Mesh Hop for the node.
- **Front end.**
  - **First slice: a line console.** `TERM=dumb` makes prompt_toolkit print a plain prompt (U: `prompt.py:1058-1118`),
    and `-c off` turns meshcore-cli's colours off. Serial Console's `TermBuffer` shows the output, and Mesh Hop's own
    entry line sends whole lines. The Help page lists the commands with warnings.
  - **Later, if Tab completion and colours are missed:** a full terminal, ported from the launcher's st page.
- **Packaging.**
  - Pinned wheels bundled in the `.deb`: a postinst must not use the network (S: `APP-GUIDELINES.md:303`).
  - Debian 13 provides prompt_toolkit 3.0.51, requests and pyserial (W).
  - pycryptodome must be bundled per architecture: Debian's package has only the `Cryptodome` namespace (W), and
    meshcore_py imports `Crypto` (U).
  - Later, an on-device **Update** into a user venv.
- **Cost.** The main path is about **4.5-5 worker sessions**, after the notifications work, plus a 15-minute board
  check.
- **Prerequisite fix (section 2.3).** A repeater's CLI reply (text type 1) lands in Chats today:
  `Model::add_incoming` ignores the text type (S: `model.cpp:587-627`, called at `client.cpp:483-484`). With the relay,
  Mesh Hop sees every CLI reply, so each one would land in Chats as an unread message. Fixing this comes first and
  costs 0.25.
- **Upstream moves fast.** meshcore-cli has had 150 releases since 2025-03-25 and meshcore 108 (W: PyPI). Pin the
  versions and bump them deliberately.

## 2. What "CLI" means here

### 2.1 Which firmware exposes which console

| Firmware | Text CLI on its USB serial | Companion protocol (binary) | Remote CLI as a target over the mesh | Local CLI through the companion protocol |
| --- | --- | --- | --- | --- |
| Companion radio (USB, BLE, Wi-Fi builds) | **no**: binary frames `>`/`<` + length (S: `ref/README.md`) | yes: Mesh Hop, meshcore-cli | level 14+: runs `TXT_TYPE_CLI_COMMAND` (3) from contacts given "Remote CLI" (W; off by default per contact, W blog) | level 14+: `CMD_RUN_CLI_COMMAND` 66, reply 29 (W; U: meshcore-cli `cli`, `meshcore_cli.py:4252-4261`) |
| Repeater | yes: 115200, CR ends a line (W) | no | yes, **admin only**, after a login (W) | - |
| Room server | yes (plus `guest.password`, `allow.read.only`; W) | no | yes, admin after login | - |
| Sensor | yes (I) | no | yes (U: meshcore-cli treats type 4 like a repeater, `meshcore_cli.py:1548-1551`) | - |
| Ethernet variants (RAK13800) | the same CLI on TCP port 23 (W) | - | - | - |

The local companion CLI handles `ver`, `board`, `get`/`set name`, `set pin`, `tz.offset`, the radio settings,
`reboot`, `poweroff` and `wifi.*` on Wi-Fi builds (W blog, 2026-10-20). The official app v1.50.0 has a "Command Line"
screen for it under Settings > Extra Tools (W blog). meshcore-cli sends any unknown root command to `cli` when the
firmware reports level 14 or more (U: `:1511-1513`, `:5680-5681`).

### 2.2 The remote CLI on the wire (background: meshcore-cli and meshcore_py implement it)

1. **Login.**
   - `CMD_SEND_LOGIN` 26 is sent; the result arrives as push 0x85 (success) or 0x86 (failure).
   - The admin password gives admin, the guest password gives guest. The firmware compares the password with `strcmp`
     against both (W, re-read today).
   - An **admin login schedules an ACL save**; a guest login does not.
   - A **blank password is accepted from a key already in the ACL** (W, re-read today: "blank password, just check if
     sender is in ACL").
   - The admin right stays until `setperm` removes it.
2. **Command.**
   - Sent as a text message with text type `CLI_DATA` (1).
   - It runs only for an admin.
   - The same timestamp is treated as a retry and not run; an older timestamp is dropped (replay rule).
   - **No ACK** is sent for it.
   - The reply is built in `temp[166]` from offset 5, so it is about 160 bytes at most. It is sent after
     `CLI_REPLY_DELAY_MILLIS` 600 (W, re-read today).
3. **Reply.** The companion queues it as a contact message with text type 1. Any client gets it with
   `SYNC_NEXT_MESSAGE`. meshcore-cli prints it in grey as a message from the repeater (U: `:379-381`).
4. **Read-only requests without a CLI** (status, neighbours, telemetry, owner, ACL, regions): binary request 50 and
   anonymous request 57. These are meshcore-cli's `rs`, `rn`, `rt`, `ro`, `ra` and `rr` (U: `:1736-1746`). Status,
   neighbours and owner need any login, guest included.
5. **Serial-only commands** are refused over the mesh (`erase`, `log`, `set freq`...).
   - The FAQ also lists `get prv.key` / `set prv.key` as serial-only (W, FAQ 3.5).
   - The first version of this report read `set prv.key` as allowed remotely. **Conflict, to check** in
     `CommonCLI.cpp`.

### 2.3 What Mesh Hop has today (S)

- **Transports.**
  - USB serial uses `flock` + `TIOCEXCL`, so a second opener gets Busy (`serial_transport.cpp:174-179`).
  - BLE goes through BlueZ, with `AcquireNotify` on the TX characteristic (`ble/bluez_backend.hpp:17`).
  - TCP to port 5000: the board serves **one client**, and a new connection replaces the old one
    (`tcp_transport.hpp:4-8`).
  - After a lost link it retries in 1 s (`client.cpp:464`, `:615`).
- **Client.** One command in flight, with urgent and normal queues. Every incoming frame has its model effects applied
  whatever command is in flight (`client.cpp:472-484`). That is what lets a relay keep Chats up to date.
- **The text-type finding (to fix first).**
  - `parse_packet` reads `txt_type` (`protocol.cpp:246`, `:267`; constants at `protocol.hpp:114-118`).
  - `Model::add_incoming` ignores it, so a type-1 CLI reply becomes a chat message, with unread count and notice.
  - **Fix:** in `Client::on_frame`, when `!m->channel && m->txt_type == txttype::kCliData`, do not call `add_incoming`.
    Keep the packet-log entry. Add a unit test: a V3 contact message with type 1 adds nothing to Chats.
  - Type 2 (signed, room server posts) stays in Chats.
  - What a v1.18 companion passes to the app when a contact without "Remote CLI" sends it a command (type 3?) is
    unknown (I). Check it when a board runs v1.18.
  - The bug matters in every option. With the relay, every CLI reply crosses Mesh Hop. With a handover, late replies
    are synced by Mesh Hop after the CLI exits. Today, replies left queued by another app on the same node land in
    Chats too.
- **UI anchors.**
  - Home has rows Chats, Contacts, Settings and Node (`mesh_app.cpp:2420-2422`, README 0.5.0). **CLI** would be the
    fifth.
  - A hidden `Tab::Terminal` exists (`mesh_app.cpp:6719`).
  - The data folder is `$XDG_DATA_HOME/mesh-hop`, else `~/.local/share/mesh-hop` (`store.cpp:256-261`).
- **No flood-scope command** in Mesh Hop's protocol (`protocol.hpp`). meshcore-cli sets the scope on the node
  (U: `:2110-2133`).
- **Simulator.** `tools/meshcore_sim.py --tcp PORT` serves the same framing over TCP (S: `:595-627`). It has no login
  or CLI yet.

## 3. What a terminal could do that the UI does not

### 3.1 Uses

| Use | Reached by | Already in Mesh Hop? | Who needs it on a hobby deck |
| --- | --- | --- | --- |
| Repeater or room admin: `get`/`set` name, tx, repeat, advert intervals, flood.max, owner.info, lat/lon, `clock sync`, `advert`, `reboot`, `password`, `setperm`, `region ...` | `to <rep>`, `login`, then lines | no | **the owner, for a repeater or room server he runs**: the main justification |
| Repeater health: status, neighbours, telemetry, owner | `rs`, `rn`, `rt`, `ro` (guest login is enough) | no | anyone with a guest password. It is also the later read-only view |
| Companion's own commands: `infos`, `clock sync`, `advert`, contacts, channels, `get`/`set` | root of the CLI | mostly | little new, but the CLI is upstream's reference behaviour |
| Companion text CLI: `set pin`, `wifi.*`, `tz.offset`, `poweroff` | `cli <cmd>`, level 14 | no | after a firmware update |
| Bench setup of a USB repeater: `set freq`, `erase`, `get prv.key` | `meshcli -r -s <port>` (U: `:5546-5564`), or Serial Console | Serial Console (I) | the owner flashing a repeater. Out of scope here |
| Scripts, aliases, `apply_to`, pipes | the CLI | no | comes for free with upstream; not a goal |

### 3.2 How the official apps manage a repeater over LoRa

The official MeshCore app (Liam Cottle; Android, iOS, desktop; the web client at app.meshcore.nz) is not open source,
by community accounts (I). Its repeater screens are described by a community wiki (W: wcmesh). The FAQ adds that RF
remote management has a **wait timer**, which an in-app purchase removes (W: FAQ 1.1, 5.8). MeshCore Open is an
open-source Flutter client: its docs say MIT, and its repository page did not show a licence (W). Its docs map each
settings field to a CLI command, which makes them the best reference for later forms.

| Function | Official app (W: wcmesh wiki, FAQ) | MeshCore Open (W: its docs) | meshcore-cli (U) | On the wire |
| --- | --- | --- | --- | --- |
| Login | from the contact; admin or guest password; "Log In", "Direct" or "Flood" | long-press, "Manage Repeater"; "Save password"; 5 retries on timeout; only 15 characters count | `to <rep>`, `login` (reads or saves `<key>.pass`), `login%#region`, `rp` (reset path) | login 26, push 0x85/0x86 |
| Status | "Request Status" (not automatic): battery, clock, uptime, airtime TX/RX, RSSI/SNR, noise floor, packets flood/direct, duplicates, queue, debug flags | Status card | `rs` | binary STATUS |
| Neighbours | admin and guest | card, up to 15 | `rn`; CLI `neighbors` (8 entries) | binary NEIGHBOURS |
| Telemetry | admin and guest | card (Cayenne LPP) | `rt` | binary TELEMETRY |
| Owner info | view (guest), set (admin) | Settings | `ro`, `get`/`set owner.info` | anonymous OWNER / CLI |
| Command line | "Command Line" tab | CLI card: quick commands, history, help | the whole console | CLI_DATA |
| Radio | freq, BW, SF, CR; "save" then reboot | Radio: freq, tx, rxgain | `set radio f,bw,sf,cr`, `set tx` | CLI |
| Name, position | Position | Basic, Location | `set name`, `set lat`, `set lon` | CLI |
| Adverts | Advert, Advert Intervals | Advertisements | `advert`, `set advert.interval`, `set flood.advert.interval`, `set flood.max` | CLI |
| Passwords | Admin and Guest password | Basic (admin write-only) | `password`, `set guest.password` | CLI |
| Access control | Access Control | - | `ra`, `setperm` | binary ACL / CLI |
| Repeat, clock | Repeat Settings; Sync Clock ("can only go forward") | Features; auto `clock sync` after login | `set repeat`, `clock sync` | CLI |
| Reboot, version | Reboot, Version | Danger zone: reboot after a confirmation; erase never sent over the air | `reboot`, `ver` | CLI |
| Regions | not described | not described | `region ...`, `rr` | CLI / anonymous |

**What Mesh Hop could add later on top of the same commands:**
- Status, Neighbours and Owner: the read-only view, third in the owner's order.
- Then Name, Position, Owner info, Advert intervals, Repeat on/off, Clock sync, Advert, Reboot, Version.
- Radio and passwords last: they can lose the repeater.

A form can type its command into the running meshcore-cli session: one login state, upstream parsing, no C++ protocol
work. The other way is for Mesh Hop to send CLI_DATA itself, which means more C++ and re-interpreting replies.
That choice is decision D11.

## 4. How it fits our stack

### 4.1 Upstream at a glance (U, W)

| Item | meshcore-cli | meshcore (meshcore_py) |
| --- | --- | --- |
| Repository | github.com/meshcore-dev/meshcore-cli (pyproject still points at fdlamotte/) | github.com/meshcore-dev/meshcore_py |
| Read at | dfcc74e, v1.6.6, 2026-10-14 | 105f407, v2.3.17, 2026-10-16 |
| Licence | MIT (`LICENSE`, `pyproject.toml`) | MIT |
| Python | >= 3.10 | >= 3.10 |
| Dependencies | meshcore >= 2.3.16, bleak >= 0.22, prompt_toolkit >= 3.0.50, requests >= 2.28 | bleak, pyserial-asyncio-fast, pycayennelpp, pycryptodome |
| Packaging | sdist + pure wheel on PyPI; commands `meshcli`, `meshcore-cli`; README: `pipx install meshcore-cli`; a nix flake | sdist + pure wheel on PyPI |
| Size | one file, 5724 lines | 5287 lines |
| Cadence | 150 releases since 2025-03-25; 1.6.0 to 1.6.6 between 2026-08-12 and 2026-10-14 | 108 releases; 2.3.10 to 2.3.17 between 2026-09-13 and 2026-10-16 |

- **Licences of everything that would ship:**
  - MIT: meshcore-cli, meshcore, bleak, pycayennelpp, wcwidth;
  - BSD-3-Clause: prompt_toolkit, pyserial, pyserial-asyncio-fast;
  - BSD-2-Clause, Unlicense and MIT: pycryptodome (PyPI);
  - Apache-2.0: requests, whose dependency certifi is MPL-2.0.
  - All of these can be redistributed with our MIT repositories. `app.json` `license` becomes an SPDX AND-expression,
    and the copyright file lists each (S: `APP-GUIDELINES.md:284`).
  - Taking requests from Debian keeps MPL out of our package.
- **Debian 13 trixie** has Python 3.13 and these packages (W, packages.debian.org):
  - `python3-prompt-toolkit` 3.0.51: meets >= 3.0.50 today;
  - `python3-bleak` 0.22.3;
  - `python3-dbus-fast` 2.44.1;
  - `python3-serial`;
  - `python3-requests`;
  - `python3-pycryptodome`, which installs **only `Cryptodome/`** (W, file list). meshcore_py imports `Crypto`
    (U: `meshcore_parser.py:6-7`, `reader.py:14`), so pycryptodome must come from a wheel.
  - meshcore, meshcore-cli, pyserial-asyncio-fast and pycayennelpp are pure Python. Whether Debian has them was not
    checked (I).
  - PEP 668 blocks `pip install` into the system Python, so the code goes in a venv or a private directory on
    `sys.path`.
- **Wheels** (W, PyPI JSON today):
  - Pure: meshcore-cli, meshcore, prompt_toolkit 3.0.53, bleak 3.0.2, pyserial, pyserial-asyncio-fast, pycayennelpp,
    requests.
  - Binary, with manylinux aarch64 **and** x86_64 wheels:
    - pycryptodome 4.1.0: abi3, so any CPython 3;
    - dbus-fast 5.2.0: cp311-cp315, now requires Python >= 3.11;
    - charset-normalizer;
    - wcwidth 0.9.3.
- **Bluetooth LE in Python is optional.** meshcore-cli and meshcore_py import bleak inside `try` (U:
  `meshcore_cli.py:28-33`, `ble_cx.py:10-21`). With the relay, nothing needs bleak or dbus-fast. Mesh Hop's own BlueZ
  code stays as it is.
- **Always imported:** `requests` and `serial.tools.list_ports` (U: `meshcore_cli.py:12-13`), `serial_asyncio_fast`
  (U: `serial_cx.py:7`), `Crypto`, `cayennelpp`.
- **On a Pi Zero 2 W (512 MB), estimated from the dependencies (I, measure on the board):**
  - about 35-50 MB resident: Python 3.13, prompt_toolkit, asyncio and the requests stack;
  - start time:
    - 2.5-4 s with compiled `.pyc` files;
    - 10-20 s more on a first run that has to compile about 1.9 MB of source (prompt_toolkit alone is 1.4 MB);
    - then 1-3 s for its node handshake: app start, device query, contacts, channels, message sync.

### 4.2 How meshcore-cli behaves in a session (U: `meshcore_cli.py`)

- **Connections:**
  - `-s <port>[:dtr=,rts=,baud=]` and `-b`: serial;
  - `-a <addr>` / `-d <name>`: BLE through bleak, scanning for names starting with "MeshCore-";
  - `-S`: a full-screen selector of BLE and serial devices;
  - `-t <host> -p <port>`: TCP, port 5000 by default;
  - `-r`: a repeater's raw text CLI over serial (`:5419-5654`).
- **At connect:**
  - app start, with a 2 s timeout (U: `meshcore.py:206`);
  - device query;
  - firmware level 14 or more turns the `cli` fallback on (`:5672-5681`);
  - then the `init` and `<node>.init` scripts, if present (`:5691-5700`).
- **Chat mode:**
  - it loads contacts and channels, resets the flood scope, runs `sync_msgs` (which **pulls every waiting message off
    the node**), then **auto-fetches on every MESSAGES_WAITING** (`:1079-1086`, `:623-637`);
  - `to <dest>`, `to /`, `to ..`, `to !`; `/cmd` runs `cmd` at the root; `/<node>/<cmd>` runs it on a node
    (`:1420-1482`, `:1556-1606`);
  - on a repeater, room or sensor, a plain line is sent as `cmd` (`:1548-1551`);
  - `?<cmd>` gives help (`:1214-1216`); `quit`, `q` or Ctrl+D ends it.
- **Remote admin:**
  - `login` with no argument reads `~/.config/meshcore/<pubkey>.pass`. Otherwise it asks, **then saves** what was
    typed (`:1854-1879`). After a wrong password, `fp` (forget_password) is the way out (`:1881-1890`).
  - `rs`, `rn`, `rt`, `ro`, `rr`, `ra`, `trace` and `logout` act on the current contact (`:1730-1753`, `:1820-1825`).
  - `setperm` accepts a contact name (`:1791-1817`).
- **Files.** All of them go under `$HOME/.config/meshcore/`, and **only if that folder exists** (`:41-45`):
  - `history`: every typed line (`:1089-1092`);
  - `<node>.msgs`: JSON lines of every received message **and every sent command with its text** (`:580-604`,
    `:1677-1678`, `:2037-2050`);
  - `<key>.pass`: the password in plain text, mode from the umask (`:1873-1875`);
  - `default_address`, `init`, `scopes`.
- **Shell.** `| cmd`, `> file`, `<| cmd` and `handler_attach` run local commands as the deck user (`:1315`, `:1338`,
  `:4402-4404`). The console is a shell.
- **Internet.** Only `upload_card` / `upload_contact` post to map.meshcore.dev, and only when typed (`:4165`, `:4188`).

### 4.3 Where it lives

| Place | Reaches the node through | Pros | Cons |
| --- | --- | --- | --- |
| **A Mesh Hop page, from a Home CLI row (recommended)** | the loopback relay (section 5) | the owner's request; one client on the node; Chats and notifications go on; USB, BLE and IP alike | a terminal view to build in Mesh Hop (4.4) |
| A separate app with `Terminal=true` | its own connection (`-S`, `-s`, `-a`, `-t`) | about 1.5 sessions; the launcher's full-panel terminal page gives colours and Tab completion for free (S: `launch.cpp:167-184`) | not from Mesh Hop's Home; fights Mesh Hop for the node (section 5); the old 20 px bar (report 032) |
| The launcher's CLI page, `pipx` by hand | its own connection | no code | the same fights; plain-text files in `~/.config/meshcore` |
| The launcher's terminal opened by Mesh Hop through `launcher.sock` (report 035) | the relay | full terminal from the launcher, nothing to draw in Mesh Hop | needs 035's socket, a new "terminal" request and multitasking on (not on screens under 640x480). A later variant |

### 4.4 Terminal front end

**What prompt_toolkit needs (U: python-prompt-toolkit).**
- **Full mode:**
  - cursor movement and erase (CSI A/B/C/D/H/J/K);
  - SGR colours, including 256 colours: meshcore-cli uses `38;5;n` and `48;5;247` (U: `meshcore_cli.py:74-101`);
  - an answer to the cursor-position query `ESC[6n`;
  - the window size (`TIOCGWINSZ`, SIGWINCH);
  - raw keys: arrows, Tab, Esc (meshcore-cli sets `ttimeoutlen` 0.2);
  - a multi-column completion menu (`:1095-1098`, `:1194-1207`).
- **Dumb mode:**
  - When `TERM` is `dumb` or `unknown`, `prompt()` / `prompt_async()` print the prompt as plain text and echo each
    typed character, with no colour and no cursor movement (`shortcuts/prompt.py:1058-1062`, `:1072-1118`;
    `utils.py:314-324`).
  - **A password prompt echoes in clear in this mode**: the echo writes `text_before_cursor[-1:]` whatever
    `is_password` says (`prompt.py:1105-1107`).
  - With `-c off`, meshcore-cli's own colours are mostly gone, and its prompt becomes `name>` or `name/rep|path>`
    (`:1121-1192`).

**What we already have (S).**
- **Serial Console's `TermBuffer`** (`apps/serial-console/src/core/term.hpp:1-20`, 104 + 424 lines, with tests):
  - a line view, not an emulator;
  - handles CR, LF, BS and TAB, and CSI K, C, D, G, P and @;
  - drops colours, OSC and other sequences, and cuts long lines.
  - It fits dumb-mode output.
- **The launcher's st page `UISTPage`** (`projects/APPLaunch/main/ui/page_app/ui_app_st*.cpp`, about 2265 lines, plus
  models):
  - a real VT parser: ESC/CSI/OSC, scroll regions, mode 1049, SGR 38/48, cursor-position and device-attribute replies
    (`ui_app_st_parser.cpp:340-420`);
  - the on-screen keyboard inset shrinks the pty rows (`model/st_viewport_model.hpp`);
  - `STKeyEncoder` writes the Ctrl bytes (`model/st_key_encoder.hpp:13-29`), and a help overlay comes from a factory
    (`terminal_help_factory.hpp`).
  - It is **launcher-only code**: it is not under the pinned toolkit paths (`apps/_common/build_app.sh:104`).
  - The toolkit's pty sits behind a hal signal (`include/signal_register_plan.h:11`). Mesh Hop would use `forkpty`
    directly, about 100 lines.

**Two front ends.**

| | T1: line console (recommended first) | T2: full terminal |
| --- | --- | --- |
| How | `TERM=dumb`, `meshcli -t 127.0.0.1 -p <port> -c off`; pty with echo off; output into a `TermBuffer` (lifted to `apps/_common` or copied); Mesh Hop's own entry line sends the line + CR | port `UISTPage`'s parser, renderer and viewport into Mesh Hop (MIT, about 1600 lines), or into the toolkit (a launcher change and a re-pin); keys go straight to the pty |
| Gets | every command, `to`, `/cmd`, `?cmd`, replies, incoming messages, Mesh Hop's own Up/Down history | as T1, plus Tab completion, colours, a masked password, prompt_toolkit's history and Ctrl+R |
| Gives up | Tab completion; an incoming message prints after the prompt with no redraw (I); the password echo has to be **masked by the view** (the rest of the line after a `Password` prompt; tied to upstream's prompt text, I) | nothing |
| Cost | about 1.5 (page, view, entry, Help, Home row) | +1.5-2 over T1 |

**Typing (keyboard rule, report 020).**
- The BT or USB keyboard is primary. The fallback on-screen keyboard shows only while none is connected, and is never
  summoned.
- **T1 at 640x480:** the entry and the keyboard only; the output comes back when the keyboard goes. This is Serial
  Console's accepted behaviour (README:211).
- **T2:** the rows shrink with the keyboard inset, as in the launcher.

**Help and buttons.**
- **Help page:**
  - Mesh Hop's own short list: start here, root commands, repeater commands, and the **warnings of section 6**;
  - **"All commands":** the output of `meshcli -h`, captured once per installed version. It follows upstream with no
    edit from us.
  - Upstream's `?get`, `?set`, `?trace`, `?path` and `?lc` topics are reachable from the console.
- **Shortcut buttons** are the owner's UI call (D8).
  - In T1 they insert text into the entry line.
  - In T2 they write bytes to the pty: Tab `\t`, Up `ESC[A`, Ctrl+C `\x03`, Ctrl+D `\x04`.

### 4.5 Packaging, updates and removal

**Rules (S: `APP-GUIDELINES.md`):**
- a postinst with no network, no interaction, and at most a few seconds (`:302-304`);
- one architecture per package, with an ELF check (`:292-296`);
- `depends` names Debian packages (`:285`);
- `license` covers everything shipped (`:284`).

| | K1: bundled and pinned (recommended) | K2: user venv on first use | K3: both (recommended later) |
| --- | --- | --- | --- |
| How | at build time, `pip download --only-binary=:all: --no-deps --platform manylinux2014_<arch> --python-version 3.13` of a pinned list (meshcore-cli, meshcore, pyserial-asyncio-fast, pycayennelpp, pycryptodome), unpacked into `/usr/lib/mesh-hop/py/` (`root/` and `root.amd64/`). Depends: `python3 (>= 3.11), python3-prompt-toolkit, python3-serial, python3-requests`. A small `mccli.py`, run with `python3 -I`, puts the folder first on `sys.path` and calls `meshcore_cli.meshcore_cli.cli()` | on the first CLI use, `python3 -m venv ~/.local/share/mesh-hop/cli-venv` and `pip install meshcore-cli` | K1 is the default. An **Update meshcore-cli** action builds K2's venv (`--system-site-packages`) with the latest release, and **Back to bundled** deletes it. The wrapper uses the venv when its `meshcli -v` works |
| Offline | yes | no: needs the internet once, and `python3-venv` | yes |
| Size | about 2.5-3 MB per architecture (pycryptodome 2 MB; I) | 20-30 MB in the user's folder (bleak and dbus-fast included; I) | both |
| Upstream update | a Mesh Hop package release with new pins; no C++ change | `pip install -U` on the deck | either |
| Removal | clean: dpkg owns every file | the venv stays (user data); the README says how to delete it | as K1 + K2 |

- **`.pyc` files.** `/usr/lib` is not writable at run time, and compiling in the postinst could take tens of seconds on
  a Pi Zero 2 W (I). Two ways:
  - ship `.pyc` files compiled for 3.13 at build time (`--invalidation-mode unchecked-hash`);
  - or set `PYTHONPYCACHEPREFIX=~/.cache/mesh-hop/pycache`, which makes only the first start slow.
- **The CLI's files.**
  - The child gets `HOME=~/.local/share/mesh-hop/cli` and `umask 077`. meshcore-cli has no setting for its folder,
    only `$HOME` (U: `:41`).
  - So its `.config/meshcore` lives in Mesh Hop's data folder, and `<key>.pass` is 0600 (I).
  - D5 decides what happens to `history` and `<node>.msgs`.
  - Removing the package leaves the folder, like the rest of the user's data (`APP-GUIDELINES.md:304`).
- **Version policy.**
  - Exact pins in a requirements file in the app folder.
  - Bump them when upstream has something useful, after the simulator smoke test, and note the bump in the README.
- **What an upstream change can break:**
  - meshcore-cli's flags or chat behaviour: the wrapper and the T1 masking. The smoke test catches it.
  - The Python minimum: 3.10 today, and Debian 13 has 3.13.
  - The prompt_toolkit minimum rising above Debian's 3.0.51: then bundle prompt_toolkit and wcwidth too.
  - New protocol commands **pass through the relay unchanged**: no Mesh Hop change.
  - Frames above 300 bytes: meshcore_py's own limit (U: `tcp_cx.py:96`).
- **Tests.**
  - The apps repo has no hosted CI. `build.sh` runs the host unit tests and the simulator flows (S: `build.sh:6-33`).
  - A new flow:
    1. `meshcore_sim.py --tcp` serves a node;
    2. Mesh Hop headless connects to it and opens the relay;
    3. the bundled `meshcli -t 127.0.0.1 -p <relay> infos` must answer;
    4. a chat session scripted through a pty runs `to <repeater>`, `login` and `ver` once the simulator has login
       and CLI (plan P2).
  - Every pin bump runs it.

## 5. Sharing the node: three ways (item 2 of the brief)

**Facts.**
- meshcore_py frames TCP exactly as serial: `<` + u16 LE + payload out, `>` + u16 LE + payload in
  (U: `tcp_cx.py:74-125`, `:146`). So a relay is a frame pipe.
- A pty posing as a serial port is riskier. meshcore_py sets DTR and RTS on the port at connect
  (U: `serial_cx.py:45-47`), which a pty may refuse (I).
- The companion protocol has **no request ids**: answers come in order, and pushes (0x80 and up) come at any time.

| | (a) Hand over | (b) Loopback relay (recommended) | (c) Separate app |
| --- | --- | --- | --- |
| How | Mesh Hop drops the link and does not retry; runs `meshcli -s /dev/ttyACM0`, `-a <MAC>` or `-t <host> -p 5000`; reconnects when it exits | Mesh Hop binds 127.0.0.1 on port 0 and passes the port number in argv. It accepts one connection, then closes the listener. **Passthrough** while the CLI is connected (below) | its own `.desktop` with `Terminal=true`; picks the node with `-S` or flags |
| Messages and notifications during the session | **lost to Mesh Hop**: the CLI's sync and auto-fetch take them (U: `:1084`, `:637`); they end up in `<node>.msgs` | **kept**: Mesh Hop parses every frame the node sends the CLI (S: `client.cpp:483-484`) | lost to Mesh Hop, which has to be closed or disconnected |
| USB | possible (Mesh Hop releases `flock`) | unchanged | **Busy** while Mesh Hop holds the port (S: `serial_transport.cpp:174-179`); meshcore-cli then says "To connect to a repeater, use -r option" (U: `:5571-5572`), which misleads |
| BLE | needs bleak and dbus-fast in Python (Debian has them) and Mesh Hop's release of `AcquireNotify`; a fresh scan and connect each time (several s, I) | unchanged; no BLE in Python | as (a), and fails while Mesh Hop holds the notify (I) |
| IP | possible | unchanged | **fight**: the node replaces Mesh Hop's session, and Mesh Hop takes it back after 1 s (S: `tcp_transport.hpp:7-8`, `client.cpp:615`; I) |
| Mesh Hop hidden in the background | no link during the session | link kept | - |
| Mesh Hop changes | a release/resume state; BLE dependencies | relay, passthrough state, refresh at the end | none |

**Passthrough in (b), step by step.**
1. **Before starting the CLI**, Mesh Hop finishes its command in flight and pauses its queue and its message-retry
   timers. The CLI's app start allows only 2 s.
2. Every frame from the CLI goes to the transport.
3. Every frame from the node goes to the CLI.
   - Mesh Hop applies only **message and push effects**: messages (except type 1), adverts, path updates, sent
     confirmations and the radio log.
   - It does not apply the effects of answers to the CLI's own commands (contacts, channels). Whether today's effects
     tolerate those frames has to be checked; limiting them is the safe default.
   - A MESSAGES_WAITING push does not make Mesh Hop fetch: the CLI fetches, and Mesh Hop reads along.
4. **When the CLI exits:**
   - resume the queue;
   - re-read self info, contacts and channels, which the CLI may have changed;
   - reset the flood scope if the CLI changed it. Mesh Hop has no scope command today, so it needs one, built from
     meshcore_py's `set_flood_scope` (U: `commands/messaging.py:411`; whether the node keeps the scope is I).

**Risks of two programs on one link (b).**
- **Mesh Hop's own sends wait** until the console closes, because it cannot interleave without request ids. D6 makes
  leaving the page end the session.
- **Unknown side effects:**
  - a second app start on the same link (I);
  - the CLI's `scope`, `set` and `remove_contact` change the node under Mesh Hop: hence the refresh at the end.
- **Loopback exposure.** The listening port is open to any local process for the instant before the accept. They all
  run as the same user anyway (report 035, section 1).
- **meshcore_py's TCP heuristic** declares the link lost after 5 sends without an answer (U: `tcp_cx.py:12`,
  `:137-143`). A stalled relay ends the CLI, which is the right outcome.

## 6. Safety

- **No confirm box (owner).** The real CLI cannot be filtered without re-implementing it. The **warnings live in the
  Help page**.
- **Commands the Help marks as dangerous:**
  - **Can lose the repeater** (someone must visit it): `set radio`, `tempradio`, `set repeat off`, `region denyf *`,
    `set flood.max 0`, `poweroff` / `shutdown`, `start ota`, `set prv.key` (if allowed remotely: 2.2). `set freq` is
    serial only.
  - **Can lock the admin out:** `password` (its reply echoes the new password), `set guest.password`, `setperm`.
  - **Silent:** `reboot` sends no reply, so "no answer" is normal.
  - **The companion itself, at the CLI root:**
    - `reboot`, `set radio`, `set tx`, `set name`, `time`, `send_raw`;
    - `remove_contact`, `apply_to ... remove_contact` (many at once), `remove_channel`, `set_channel`,
      `flush_pending`;
    - with level 14: `cli poweroff` (nothing turns it back on), `cli set pin` (BLE), `cli wifi.*` (over IP).
  - **Leaves the deck:** `upload_card` and `upload_contact` publish to map.meshcore.dev.
  - **Shell:** `| cmd`, `<| cmd`, `> file` and `handler_attach` run commands as the deck user.
- **No ACK, no retry.** A CLI command gets no ACK, and meshcore-cli sends `cmd` once ("first and only attempt",
  U: meshcore_py `commands/messaging.py:65-88`). No reply means it may or may not have run. The Help says so, and
  says not to repeat risky commands blindly.
- **The admin right persists** in the repeater's ACL, so a lost deck with its companion board is an admin (W). The
  README says so; `setperm <key> 0` removes it.
- **Secrets on disk.**
  - `<key>.pass` is plain text, 0600 under `umask 077`, in Mesh Hop's data folder. This is the owner's "remembered per
    repeater", done by upstream.
  - meshcore-cli's `history` and `<node>.msgs` also hold typed commands, including `password X`. D5: point both at
    `/dev/null` (I: that meshcore-cli accepts this), or keep them.
- **Mesh Hop's log rule is unchanged.** The relay logs frame codes and lengths only, and only with `MESHHOP_DEBUG=1`,
  never text.
- **On screen.** In T1, a typed password would show unless the view masks it (4.4).

## 7. Options

| Option | What | Pros | Cons | Cost |
| --- | --- | --- | --- | --- |
| **O1 (recommended)** relay (b) + line console T1 + bundle K1 | Home CLI row, meshcore-cli in a pty, Help page, passwords saved by meshcore-cli | the owner's requirements; upstream does every command; Chats and notifications go on; USB, BLE and IP; offline install | no Tab completion; Mesh Hop's sends wait during a session; Python start on a Pi Zero 2 W (I) | **about 4.5-5** (section 9) |
| O2 hand over (a) + T1 + K1 | as O1, but Mesh Hop lets go of the node | no passthrough logic | messages lost to Mesh Hop during a session; BLE in Python; slower start; link down while hidden | about 4.5-5 |
| O3 separate app (c) + K1 | `Terminal=true` app in the launcher's terminal page | cheapest; full terminal for free | not from Mesh Hop; conflicts on USB, BLE and IP; Mesh Hop must be closed | about 1.5 |
| O4 relay (b) + full terminal T2 | O1 with the launcher's st code ported | Tab completion, colours, masked password | larger port or a toolkit change and re-pin | about 6-6.5 |
| O5 relay (b) + the launcher's terminal through `launcher.sock` | Mesh Hop asks the launcher to open its terminal on the CLI | nothing to draw in Mesh Hop | needs 035's socket, a new request and multitasking; not on screens under 640x480 | about 3.5-4, after 035 |

Later, in the owner's order:

| Item | What | Cost |
| --- | --- | --- |
| R | read-only view: status, neighbours, owner. Native parsers in Mesh Hop (the old option A, about 2), or meshcore-cli's JSON (`.rs <name>` / `meshcli -j`) through the relay (about 1, slower per request) | 1-2 |
| F | touch forms (3.2) typing into the session | 1.5-2 |
| U | Update / Back to bundled (K3) | about 1 |
| T2 | full terminal in place of T1 | 1.5-2 |
| C | Serial Console README note for a repeater on USB (or `meshcli -r -s <port>`) | 0.25 |

## 8. Decisions

| # | Question | Options | Recommendation |
| --- | --- | --- | --- |
| D1 | How the CLI reaches the node | relay (b) / hand over (a) / separate app (c) | **relay (b)** |
| D2 | Front end | line console T1 / full terminal T2 | **T1 first**; T2 if Tab completion or colours are missed |
| D3 | Packaging | bundled K1 / venv K2 / both K3 | **K1** now, **K3** later |
| D4 | Upstream versions | pinned and bumped by us / latest on the deck | **pinned**, bumped after the smoke test; the on-deck latest comes with K3 |
| D5 | meshcore-cli's `history` and `<node>.msgs` (they hold typed commands, passwords included) | `/dev/null` / keep in Mesh Hop's data folder | **`/dev/null`** (Mesh Hop's entry has its own history); `<key>.pass` kept |
| D6 | Session lifetime | leaving the page ends it / it keeps running behind | **leaving ends it**, since Mesh Hop's own sends wait while it runs |
| D7 | Help content | curated list + `meshcli -h` / curated only | **both** |
| D8 | Shortcut buttons (UI) | none / Help only / Help + Login, `to` picker, Status, Neighbours, Root, Stop | **owner's call**; the slice needs only Help |
| D9 | Home row label and place | "CLI" fifth row / elsewhere | owner said "CLI" on Home: confirm the wording |
| D10 | x86 tablet (amd64) | include / arm64 only | **include**: pycryptodome has an amd64 wheel |
| D11 | Later forms and read-only view | through the CLI session / native C++ | decide at that step; through the CLI keeps upstream parsing |
| D12 | `get`/`set prv.key` over the mesh | - | check `CommonCLI.cpp` before the Help text is written (2.2) |

## 9. Plan

| Phase | Tier | Repo | Content | Cost | Order |
| --- | --- | --- | --- | --- | --- |
| P0 | main path | owner | answers D1-D9; firmware level of the boards; one repeater or room he administers, reachable over LoRa | - | first |
| - | main path | launcher, apps | **notifications** (report 035 P2 and P4) | per 035 | before P3 |
| P1 | main path | apps | text-type fix: type 1 out of Chats, with a unit test. Can ship with the notifications release | 0.25 | any time |
| P2 | secondary for the slice | apps | simulator: login (admin and guest), CLI_DATA replies (`ver`, `get`/`set name`, unknown command, silent mode), login failure | 0.5 | with P3 |
| P3 | main path | apps | relay and passthrough in `Client`: listener, pause and resume, message and push effects only, refresh and scope reset at the end; unit tests with a fake transport; a headless flow with `meshcore_sim --tcp` | 1.25 | after P1 |
| P4 | main path | apps | bundle K1: pinned requirements, per-architecture fetch in `build.sh`, `mccli.py`, `depends`, licence and copyright; smoke flow (`infos` through the relay) | 1 | with P3 |
| P5 | main path | apps | CLI page T1: `forkpty`, `TermBuffer` view (lift Serial Console's `term.*` to `apps/_common`, or copy), entry and history, password masking, Help page, Home row; flows at 640x480 and 800x480, touch only and keyboard | 1.5 | after P3, P4 |
| P6 | main path | apps | README, BOARD-TESTS section, version 0.6.0, package; publish on the owner's word | 0.5 | after P5 |
| P7 | main path | owner | board check (section 10) | 15 min | after P6 |
| P8-P12 | secondary | apps | R, F, U, T2, C (section 7) | 0.25-2 each | in the owner's order |

Main path: P1 + P3 + P4 + P5 + P6 = **about 4.5 sessions**, or about 5 with P2. Then the owner's 15-minute check.

## 10. Minimal first slice and its board check

**What the slice does.**
- A Home **CLI** row opens the page. The bundled, pinned meshcore-cli starts through the relay. The prompt shows the
  node's name.
- The output view and one entry line; Enter sends; Up/Down for history; Esc leaves.
- **Help** lists the commands with warnings, plus "All commands" from `meshcli -h`.
- `login` on a repeater asks once, shows `*`, and meshcore-cli saves the password in Mesh Hop's data folder. After
  that, `login` asks nothing.
- Leaving ends the session. Mesh Hop resumes and refreshes.
- Chats keep receiving during the session, and no repeater reply ever lands in Chats.

**Board check (USB, one companion, a repeater the owner administers over LoRa).**
1. Home > CLI: the prompt appears. Note how many seconds it takes.
2. `infos` and `ver` answer.
3. `to <repeater>` then `login`: the password is asked and masked, then "Login success".
4. `ver`, `get name`, `clock` and `rs` answer.
5. Esc, then Home > CLI again, `to <repeater>`, `login`: no password is asked.
6. With the console open, another node sends the deck a direct message. It shows in the console. After leaving, it
   is in Chats with its unread count, and there is no message from the repeater in Chats.
7. After leaving, a chat sent from Mesh Hop goes out.

BLE or IP only if the owner wants a second pass: the relay does not depend on the transport.

## 11. Risks

- **Upstream cadence:** releases every few days (meshcore) to weeks (meshcore-cli). Pins and the smoke test contain
  it. The on-deck Update (K3) can bring a version we never ran.
- **Dumb-mode quirks (T1):**
  - incoming messages print after the prompt with no redraw;
  - the password masking depends on upstream's prompt text ("Password for <name>: ", U: `:1867`).
  - If that text changes, a typed password shows on screen; it is never stored by Mesh Hop.
- **Passthrough** pauses Mesh Hop's own sends and retries. A long session delays a chat typed in another page, which
  is why D6 makes leaving end the session.
- **Python on a Pi Zero 2 W:** 35-50 MB and several seconds to start (I). The first start without `.pyc` files can
  take 10-20 s more.
- **The console is a shell** (pipes and handlers). It runs as the deck user, like the launcher's CLI page: not a
  security boundary.
- **Secrets on disk:** `<key>.pass` in plain text (0600). The admin right persists on the repeater.
- **Side effects of the CLI on the node** (scope, contacts, channels): handled by the refresh at the end, but not
  proven (I).
- **Debian versions:** a future meshcore-cli may need a prompt_toolkit newer than trixie's 3.0.51. Then bundle it
  (pure Python).
- **x86 tablet:** Armbian x86 on Ubuntu 26.04 may have a Python other than 3.13 (I). The pure wheels and abi3
  pycryptodome do not care, but `.pyc` files built for 3.13 would be unused there.
- **No ACK and the replay rule** are unchanged from the firmware: a command may or may not have run, and a companion
  clock behind the repeater's makes commands silently ignored (W).

## 12. Not verified, and sources

Not verified:
- Nothing upstream was run: no import time, no memory figure, no dumb-mode session, no relay. The 4.1 numbers are
  inferred.
- prompt_toolkit's dumb mode under `patch_stdout(raw=True)` with messages arriving, and the password echo in practice.
- That passthrough with message and push effects only leaves Mesh Hop's model consistent, and that a second app start
  is harmless.
- That a pty would refuse DTR/RTS (why TCP was chosen over a pty posing as a serial port).
- That meshcore-cli accepts `/dev/null` symlinks for `history` and `<node>.msgs`.
- `set prv.key` over the mesh (2.2), and the text type a v1.18 companion passes to the app for a refused remote CLI
  command.
- The official app's exact screens in its current version: the wcmesh wiki has no date. That it is closed source.
  MeshCore Open's licence in its repository (only its docs say MIT).
- Whether Debian 13 packages meshcore, meshcore-cli, pyserial-asyncio-fast or pycayennelpp.
- Whether `python3` is installed on every deck image. The launcher's Python page runs `python3`
  (S: `builtin_app_registry.cpp:82`), so it is likely.
- No board was touched. The firmware facts come from a page summariser of the raw files.

Sources (read 2026-10-23):
- meshcore-cli: https://github.com/meshcore-dev/meshcore-cli (cloned at dfcc74e); PyPI
  https://pypi.org/project/meshcore-cli/
- meshcore_py: https://github.com/meshcore-dev/meshcore_py (cloned at 105f407); PyPI https://pypi.org/project/meshcore/
- prompt_toolkit: https://github.com/prompt-toolkit/python-prompt-toolkit (cloned at 6c7fbc1)
- PyPI JSON for bleak, dbus-fast, prompt-toolkit, pycryptodome, pyserial, pyserial-asyncio-fast, pycayennelpp,
  requests, wcwidth and charset-normalizer: `https://pypi.org/pypi/<name>/json`
- Debian trixie packages: https://packages.debian.org/search?keywords=python3-prompt-toolkit&suite=trixie,
  `...python3-bleak...`, `...python3-dbus-fast...`; the python3-pycryptodome file list:
  https://packages.debian.org/trixie/all/python3-pycryptodome/filelist
- MeshCore FAQ: https://docs.meshcore.io/faq/
- CLI reference: https://docs.meshcore.io/cli_commands
- Companion protocol doc (2026-03-08): https://docs.meshcore.io/companion_protocol/
- Repeater firmware (login, CLI_DATA, reply buffer, delay):
  https://raw.githubusercontent.com/meshcore-dev/MeshCore/main/examples/simple_repeater/MyMesh.cpp
- Companion firmware: https://raw.githubusercontent.com/meshcore-dev/MeshCore/main/examples/companion_radio/MyMesh.cpp
  and `MyMesh.h`
- The official app's repeater screens (community wiki): https://wiki.wcmesh.com/meshcore-app/repeater_access/
- MeshCore Open repeater management: https://meshcoreopen.org/docs/repeater/; repository
  https://github.com/zjs81/meshcore-open
- Companion CLI and the app's Command Line screen (third party, 2026-10-20):
  https://nodakmesh.org/blog/meshcore-companion-cli-guide
- Release 1.16.0 (frame size 176): https://blog.meshcore.io/2026/06/06/release-1-16-0
