# 057 - Reticulum on the cyberdeck (study)

Date 2026-11-07. **Status: the owner accepted all decisions D1-D19 on 2026-11-07 (section 1); the next step is probe P0 (section 8), dry-run on the PC first, then on the Pi 4B and the deck with the owner present, using the owner's Heltec V4 with RNode firmware as the radio.** Research and design only: no product code, no app, no board touched, nothing installed or run on a
board. The question: can the cyberdeck boards join Reticulum networks (RNS, LXMF messaging, NomadNet pages) through the
owner's LoRa boards flashed with RNode firmware and through Wi-Fi, and what would it cost. This report gives the facts,
the options and my recommendations; design and visual decisions stay with the controller and the owner.

**What I read.**
- Launcher `github/main` @ `28817a50`: reports 054 and 056 (format, `position.state` draft in 054 section 6.3, the port
  rules of 056 section 4.2), `docs/OPEN_SOURCE_COMPONENTS.md`, `docs/dev/decisions.md`, `docs/dev/ref/README.md` (GPL
  clients as UX references only), `projects/APPLaunch/pizero2w/install.sh` (the launcher runs as the systemd user
  service `cyberdeckzero.service` with lingering on appliance boards, and from the menu on desktop boards).
- Apps `origin/main` @ `67e546b` (2026-11-07): `docs/APP-GUIDELINES.md` (sections 0, 1, 4, 5, 6, 10, 12, 14), the
  `README.md` apps table, Mesh Hop 0.8.2 (`app.json`, `cli/requirements.txt`, `cli/vendor.py`,
  `src/core/serial_transport.cpp`), Deck Atlas 0.1.0 `README.md`, `tools/make_registry.py` (the `license` field).
- Upstream, read 2026-11-07 (Sources, with versions and commits): Reticulum `master` @ `cb1260891d` = **RNS 1.6.0,
  released today**: the manual sources (`docs/source/*.rst`), `LICENSE`, `setup.py`, `MIRROR.md`, `RNS/Reticulum.py`,
  `RNS/Interfaces/RNodeInterface.py`, `RNS/Utilities/rnodeconf.py`, `RNS/_version.py`, `RNS/Stamps.py`,
  `RNS/Storage.py`, `RNS/__init__.py`; LXMF 1.3.0 (`README.md`, `LICENSE`, `setup.py`, `LXMF.py`, `LXMRouter.py`,
  `LXMessage.py`); NomadNet 1.5.0 (`README.md`, `LICENSE`, `setup.py`, `Guide.py`, `Node.py`); Sideband 2.2.0
  (`README.md`, `LICENSE`, `sbapp/sideband/sense.py`); RNode Firmware 1.86 (`README.md`, `LICENSE`, `Boards.h`,
  `Bluetooth.h`, `Config.h`, `RNode_Firmware.ino`); MeshChat 2.4.0 and MeshChatX; Reticulum-Go, Reticulum-rs,
  microReticulum, reticulum-kt, Columba and Prns (repository metadata, READMEs, licence files); PyPI JSON and Debian
  `madison` package indexes; three GitHub discussions with measurements.

**Scope (owner, 2026-11-07).** *Transport-agnostic:* "our app, per Reticulum's advantage, should be able to use any
compatible hardware: Ethernet, Wi-Fi, Bluetooth, RNode, radio...". The apps never assume LoRa; they work over whichever
Reticulum interfaces the user enables (4.1), and a deck with only Wi-Fi is a valid Reticulum node. *Worldwide:* an RNode
is a separate device, any compatible board with RNode firmware (the owner has a Heltec LoRa32 V4 running it, 4.3), not
our hardware; the app does not flash or own it. For an RNode the app sets only what Reticulum's `RNodeInterface`
exposes (4.2), including the airtime (duty-cycle) limits; it has no region presets and decides or enforces no region.

**A correction to the brief.** Mesh Hop is a **MeshCore** client, not a Meshtastic client (`apps/mesh-hop/app.json`:
"MeshCore messenger"). It reaches its companion radio over USB, BLE or TCP. Nothing below depends on the difference,
except that the owner's MeshCore boards are not RNodes (4.3).

**Markers.** **V** = read in a cited source or in our code; **I** = inferred or estimated by me; **U** = unverified: no
source found, or not checkable without a board (collected in section 10). **Cost unit:** one worker session (ws), about
0.4 M tokens (as in reports 054 and 056).

## 0. Summary

**Verdict: feasible, but only as "the upstream Python implementation in one background service, plus our own native
LVGL apps that talk to it over a small local socket", and only after a one-board probe answers three unknowns: the RAM
and CPU of Python Reticulum on a Pi Zero 2 W, the CPU cost of the new proof-of-work stamps, and whether the owner
accepts a long-running Python process, which today's app guidelines rule out.** The service carries an interface
manager, so the same apps run over Wi-Fi, Ethernet, TCP, an RNode or any other Reticulum interface the user turns on. A native C++ reimplementation of the
protocol is not recommended: it is the largest option, and upstream treats machine-generated reimplementations as
infringing derivatives (6.3).

1. **What it is (V).** Reticulum (RNS) is a cryptographic network stack for slow and fast links alike: no IP
   addresses, every endpoint is a 16-byte destination hash derived from a public key; reachability spreads by signed
   *announces*; *links* give encrypted, forward-secret channels across any number of hops. LXMF is the message format
   and store-and-forward protocol on top (propagation nodes keep messages for offline users). NomadNet is a terminal
   messenger and page browser/server (micron markup), Sideband a graphical LXMF client, RNode a firmware that turns a
   LoRa board into a raw-LoRa modem for RNS (2).
2. **The current state (V).** RNS 1.6.0, LXMF 1.3.0 and NomadNet 1.5.0 were all released on 2026-11-07. RNS 1.6.0
   moves storage to SQLite, claims up to 80 % less memory for non-transport instances, and starts *network stamps*
   (proof of work on identities, announces, path and link requests): generated from 1.6.1, enforced in 1.6.2, after
   which nodes that did not update "will not be able to communicate with stamp-enforcing nodes". Whatever we ship has
   to follow upstream releases closely (3.6).
3. **The maintainer (V).** The GitHub repositories are "a public mirror"; `MIRROR.md` (2025-12-27) says there will be
   "no support, no responses to issues, no discussions"; issues are switched off. Releases continue at a high rate (8
   RNS releases since 2026-08-28). One person holds the project.
4. **Software (V).** Only the Python reference is mature. It installs with `pip install rns` (a pure-Python wheel that
   depends on `cryptography` and `pyserial`); there is **no Debian package** of RNS, LXMF or NomadNet in bookworm,
   trixie, forky or sid; the dependencies are in Debian 13 (`python3-cryptography` 43, `python3-serial` 3.5,
   `python3-bleak` 0.22.3). The Go, Rust, C++ and Kotlin implementations call themselves experimental or are disowned
   by upstream (3.1).
5. **Resources (U).** No published RAM or CPU figure for RNS on a Zero 2 W was found. Known: a Zero 2 W running RNS
   idles at about 400 mW (V, community table); the LXMF propagation-node index ran at 3,867 messages/s on a Zero 2 W
   (V). Not known on the Zero 2 W: resident memory, start time, and the cost of stamps (Pi 4B, 8.2: about 38 MB idle, 0.45 s start, and a cost-16 stamp did not finish in 8.5 minutes at 2 cores). A message sent through a propagation node needs
   a cost-16 stamp, on average 65,536 SHA-256 passes over a 768 KB block, spread over all cores (V, source); my first estimate
   on a Zero 2 W was a few minutes of full CPU per message (I); the Pi 4B run (8.2) suggests much more: a heavy load for a Zero 2 W, which has not been tried (owner,
   2026-11-07: every earlier reset of the board was an accidental reboot or unplugging, not load).
6. **How an app talks to it (V).** Programs share one *shared instance* through a local socket, but each client still
   runs the full RNS protocol (crypto, links, announces) in its own process; the side channel is Python's
   `multiprocessing.connection` with msgpack. A native app therefore either reimplements RNS or talks to a Python
   helper. Recommended: one Python **bridge** process that is the shared instance, runs the LXMF router and offers a
   JSON-lines socket to our native apps (5.3).
7. **Transports (V).** RNS 1.6.0 documents eleven built-in interface types: `AutoInterface` (Wi-Fi and Ethernet, zero
   configuration, IPv6 link-local), `BackboneInterface`, `TCPServerInterface`, `TCPClientInterface`, `UDPInterface`,
   `I2PInterface` (needs an `i2pd` daemon), `RNodeInterface` (USB serial, Bluetooth LE through `bleak`, or TCP),
   `RNodeMultiInterface`, `SerialInterface`, `PipeInterface`, `KISSInterface` and `AX25KISSInterface` (packet radio),
   plus custom interface modules. None is privileged: the apps are transport-agnostic and the bridge's interface manager
   lists, adds, enables and disables interfaces per type, showing only that type's options (4.1, 5.3). **Listen-only is
   one setting per interface, `outgoing = False`, but the PC dry run (8.1) showed it silences announces and path
   requests only: a message to a destination whose path is already known still went out (D8 must also be enforced by
   the bridge).**
8. **RNode settings (V).** On an RNode interface the host sets `frequency`, `bandwidth`, `txpower`, `spreadingfactor` and
   `codingrate` (all required: RNS takes the interface offline without them), optionally `airtime_limit_short` (about
   15 s window) and `airtime_limit_long` (rolling 60 minutes), both in percent and enforced by the RNode firmware,
   `flow_control`, and an ID beacon (`id_callsign`, `id_interval`). The interface manager's RNode form is exactly that list plus the
   common interface options; no region presets (4.2, D10). Every 1 % of `airtime_limit_long` is 36 s on air per hour,
   about 74 announces at SF8/125 kHz (I, 4.2).
9. **The owner's hardware (V, U).** The owner already runs RNode firmware on a **Heltec LoRa32 V4**: on the official
   supported list (RNode Firmware README; `rnodeconf --autoinstall` choice 9, firmware `rnode_firmware_heltec32v4pa.zip`
   in release 1.86), ESP32-S3 + SX1262 with PA, BLE and Wi-Fi, no Bluetooth Classic (`Boards.h`); `rnodeconf` calls
   RNode firmware on Heltec boards "experimental". It is the probe's RNode (8). Compatibility notes only: the spare XIAO
   ESP32S3 + Wio-SX1262 is also supported; the XIAO nRF52840 and the T1000-E are not on the official list. One board
   runs one firmware.
10. **Conflict with Mesh Hop (V).** RNS opens an RNode's serial port without `flock` or `TIOCEXCL`, so Mesh Hop's lock
    check would not see that the port is in use. A port rule is needed (4.4).
11. **Licences (V).** RNS and LXMF: the "Reticulum License" = the MIT text plus two use conditions (no systems designed
    to harm humans; no use in AI, machine-learning or language-model training datasets). NomadNet and the RNode
    firmware: GPL-3.0. Sideband: CC BY-NC-SA 4.0. MeshChat: MIT. RNS and LXMF may be shipped in an app package with
    their notice; nothing enters the launcher (6).
12. **Apps (I).** A status app with the interface manager and an announce monitor (c), a messenger (a), a NomadNet browser (b), position sharing with Deck
    Atlas (e) and file transfer (f) all share the bridge. Wrapping NomadNet's terminal UI (d) is cheap but likely
    awkward on our terminal page (U); wrapping Sideband is not practical (Kivy GUI, 68.6 MB wheel, CC BY-NC-SA) (5).

**Recommendation (accepted by the owner, 2026-11-07).** Run probe P0 on the Pi 4B and then the deck, over Wi-Fi and with the owner's
Heltec V4 RNode receiving only; decide on the numbers. If they pass: P1 a `reticulum` service package with the interface
manager and a listen-first status app, P2 the messenger, P3 the NomadNet browser, P4 positions with Deck Atlas. **MVP (P0 + P1 + docs) 4.5-6 ws
(1.8-2.4 M tokens); with the messenger 8.25-11.25 ws; everything listed 13.75-19.5 ws (5.5-7.8 M tokens)** (7).

**Top three risks.** (1) Python RNS memory and CPU, and stamp proof-of-work, on the Zero 2 W (no load problem has been seen on the deck; owner: the earlier resets were accidental unplugging or reboots);
(2) upstream churn (stamp enforcement in 1.6.2) with a single maintainer who takes no issues; (3) licence and
provenance: a non-standard licence with use conditions, and upstream's stance on AI-generated reimplementations, in a
project whose code is written with AI assistance (9).

**Owner questions:** section 11.

## 1. Decisions for the owner

**Owner decisions, 2026-11-07.** The owner accepted all recommendations D1-D19 of this report, "as we revised the plans". For each row below the recommendation column is therefore the decision (text kept as written; the Status column marks it Accepted). Next step: probe P0 (section 8). P0 is first dry-run on the PC (no board, loopback only, to validate the commands and versions); then it runs on the Pi 4B and on the deck with the owner present, with the owner's Heltec V4 (RNode firmware) as the radio. The budget in D2 and the "if P0 confirms" conditions in D13 and D15 still apply: P0's numbers decide whether the build starts.

| # | Decision | Options | Recommendation (not a rule) | Status |
|---|---|---|---|---|
| D1 | Start at all | run probe P0 now; park the topic | run P0 (one evening: about 1-1.5 h of the owner's time, section 8) before any build | Accepted (2026-11-07) |
| D2 | A long-running Python process | keep guideline 6 ("no Python for long-running parts"): no Reticulum; an exception for this one service; an exception for any app | an exception for this one service only, and only if P0 meets a budget the owner sets. My proposal: on the deck, VmRSS at most 60 MB idle, idle CPU at most 3 %, ready within 20 s of start | Accepted (2026-11-07) |
| D3 | Architecture | A: our Python bridge is the shared instance, native apps talk JSON to it; B: upstream `rnsd` as the service, the bridge as one of its clients; C: a native C++ RNS and LXMF in our apps; D: an alternative implementation (Go, Rust, C++); E: only wrap NomadNet's terminal UI | A (it behaves as B by itself when the user already runs an `rnsd`: 3.5); not C; D only if P0 fails and a later look finds a mature one; E only as an optional stopgap (D15) | Accepted (2026-11-07) |
| D4 | Packaging | pinned wheels vendored in our `.deb` (Mesh Hop's precedent); the user runs `pip`/`pipx`; a `.deb` that runs `pip` at install | vendored and pinned (`rns`, `lxmf`), with Debian's `python3-cryptography` and `python3-serial` as `depends` and `python3-bleak` as `recommends`; a new package with each upstream release while 1.6.x settles | Accepted (2026-11-07) |
| D5 | Names | package and unit `reticulum`; `rns-bridge`; `deck-rns` | the owner's choice; `reticulum` reads well in Settings > Apps but must not be mistaken for an upstream package (the README says it is ours) | Accepted (2026-11-07) |
| D6 | Service lifetime | always on from boot; started by an app and stopped with the last one; a switch | started on demand by our apps, plus a "Keep running (receive while no app is open)" switch in the status app, off by default | Accepted (2026-11-07) |
| D7 | Transport (routing for other nodes) | off; on | off on every board of ours (the manual reserves it for stationary, always-on systems: 2.2) | Accepted (2026-11-07) |
| D8 | What may transmit | everything the config enables; nothing until the owner switches it on | nothing transmits until the owner switches it on, per interface (`outgoing = False` as the default); no announce, path request or message without the owner's action | Accepted (2026-11-07) |
| D9 | Default interfaces | upstream default (AutoInterface on every network); AutoInterface only on a named device after opt-in; an Internet entrypoint preconfigured | no interface on at first start; the user adds and enables each one in the interface manager (D19): AutoInterface on `wlan0`, Ethernet or a chosen USB adapter, TCP entrypoints only when typed or picked by the user, an RNode only when added | Accepted (2026-11-07) |
| D10 | RNode settings page: contents and defaults | the `RNodeInterface` options only (4.2); add region presets or frequency lists; prefill upstream's example values (867.2 MHz, 125 kHz, 7 dBm, SF8, CR 5) | the `RNodeInterface` options only, no region presets, no frequency allowlist, no hard-coded plan; the radio fields start empty and the owner/user must fill them (RNS itself refuses an RNode interface without them, 4.2); `airtime_limit_long` and `airtime_limit_short` always visible, empty by default (empty = no limit, as upstream), the owner/user fills them; transmitting stays off until the user switches an interface on (D8) | Accepted (2026-11-07) |
| D11 | Which board is the RNode (context only, optional) | any compatible board with RNode firmware (4.3) | the owner's choice outside the app; he already runs RNode firmware on a Heltec LoRa32 V4 (official list, USB and BLE), which P0 uses; compatibility notes only: the XIAO S3 WIO is also supported, the XIAO nRF52840 is not and can stay on MeshCore for Mesh Hop | Accepted (2026-11-07) |
| D12 | Identity and announces | one LXMF identity per deck; one per app; announce policy | one identity per deck in the service's data folder (mode 0600); export only on the owner's action; announce only after the owner has set a display name and pressed Announce; automatic re-announce off, at most every 6 h when on | Accepted (2026-11-07) |
| D13 | Propagation node | none; picked from heard announces; sync policy | none by default; the owner picks one; sync by hand, or on a timer only while on Wi-Fi; the app warns that sending through it costs minutes of CPU on a Zero 2 W (if P0 confirms) | Accepted (2026-11-07) |
| D14 | First app | status app with interface manager and announce monitor; messenger | the status app: listen-first, no transmit needed, holds the interface manager (D19) and shows each interface's facts | Accepted (2026-11-07) |
| D15 | NomadNet terminal wrapper | build as a stopgap; skip | only if P0 shows it usable in the launcher's terminal page (U) | Accepted (2026-11-07) |
| D16 | Provenance rule for this feature | as for other apps; strict | strict: workers use only the public Python API of RNS and LXMF and documented formats; they never read upstream source in order to write protocol, crypto or markup-parser code; the README states AI assistance (6.3) | Accepted (2026-11-07) |
| D17 | AI account data use | check; ignore | check that the sessions used for this work are not used for model training (6.2: the licence's AI-training condition) | Accepted (2026-11-07) |
| D18 | Mesh Hop guard | none; skip ports held by any other process | a small Mesh Hop task: treat a port held by any other process as busy (`/proc` check), and name an RNode as "used by Reticulum" (4.4) | Accepted (2026-11-07) |
| D19 | Transport model | LoRa-first apps around an RNode; transport-agnostic apps over any Reticulum interface | transport-agnostic (owner's statement, 2026-11-07): an interface manager in the bridge and the status app lists, adds, edits, enables and disables interfaces of every documented type (4.1), each with only its own options and per-interface status (online, rx/tx bytes, bitrate, errors); D8 applies per interface; hardware-neutral wording ("interface", not "radio"); the other apps never ask which medium is in use | Accepted (2026-11-07) |

## 2. Reticulum and its parts (question 1)

### 2.1 The pieces and how they fit

| Piece | What it is | Version on 2026-11-07 | Language, licence | Role for us |
|---|---|---|---|---|
| RNS | the network stack; ships `rnsd` (daemon), `rnstatus`, `rnpath`, `rnprobe`, `rnid`, `rncp`, `rnx`, `rnsh`, `rnodeconf`, `rngit`, `rnpkg`, `rnir` (V `setup.py`) | 1.6.0, 2026-11-07 | Python, Reticulum License | the core |
| LXMF | message format and delivery protocol on RNS; the LXM Router; propagation nodes; `lxmd` daemon | 1.3.0, 2026-11-07 | Python, Reticulum License | messaging |
| LXST | real-time voice and streams on RNS | 0.5.3, 2026-09-26 | Python; GitHub reports no standard licence (licence not read, U) | not for us (audio) |
| NomadNet | terminal (urwid) messenger, page browser and page server, micron markup, `--daemon` mode | 1.5.0, 2026-11-07 | Python, GPL-3.0 (`LICENSE`; `setup.py` classifier says MIT, 6.1) | pages to browse; a TUI to wrap |
| Sideband | LXMF and LXST client for Android, Linux, macOS, Windows: telemetry, plugins, paper messages, `--daemon` mode | 2.2.0, 2026-10-01 | Python + Kivy, CC BY-NC-SA 4.0 + a binary grant | feature reference; the phones of the owner's contacts |
| MeshChat / MeshChatX | Python backend + web UI over a WebSocket; Electron desktop builds | 2.4.0 (2026-07-06) / active fork | MIT / 0BSD + MIT | heavy; the manual now lists MeshChatX, not MeshChat |
| Columba | Android LXMF app | 2.2.6 | Kotlin, MPL-2.0 | phones |
| RNode | firmware that makes LoRa boards a raw-LoRa modem for RNS, over USB, BLE, Bluetooth Classic or Wi-Fi | 1.86, 2026-04-24 | C++, GPL-3.0 | the radio |

How they fit (V unless marked): RNS moves packets between destinations over *interfaces* (radios, serial lines, IP);
LXMF builds signed messages on it (destination, source, Ed25519 signature, msgpack payload: 111 bytes of overhead);
NomadNet, Sideband, MeshChatX and Columba are apps using LXMF; an RNode is only the interface device. The RNode "does
not use the LoRaWAN standard, but uses a custom MAC layer on top of the plain LoRa modulation" (manual), so it cannot
talk to MeshCore or Meshtastic nodes even on the same channel (I).

### 2.2 Protocol facts that matter on a small device

| Concept | Fact (V, manual or source unless marked) | Consequence for us |
|---|---|---|
| Destination | 16 bytes: SHA-256 of an app name and aspects (plus the public key for *single* destinations), truncated to 128 bits. Types: single (encrypted to a key), group (pre-shared key), plain (unencrypted), link. Plain and group packets are not carried over multiple hops | users see 32-hex-digit addresses; a contact is an LXMF address |
| Identity | an Ed25519 signing key and an X25519 key; "obtaining access to the identity keys equals obtaining access and controlling reachability to any destinations created by that identity" | key storage and backup matter (5.3); the file is not passphrase-protected (I) |
| Announce | 167 bytes, signed; carries the public key, application data (LXMF: the display name) and a random blob; forwarded by transport nodes up to 128 hops; announce traffic capped at 2 % of each interface's bandwidth; apps should announce at start and then "only once every 6 hours or so"; announcing "more often than once an hour, you're most likely doing something wrong" | an announce tells everyone in range, and along every path, that this address exists, its name and when (I); policy D12 |
| Paths | a path request is 51 bytes; a transport node knows only the next hop; slow segments need not hold the whole network's table, paths are resolved on request | the deck never needs a big table; 1.6.0 also shards path tables on transport nodes |
| Link | 3 packets, 297 bytes to set up; keep-alive about 0.45 bit/s; forward secrecy; the initiator stays anonymous unless it identifies inside the link | about 0.9 s of airtime to open at SF8/125 kHz (I, 4.2) |
| Resource | reliable transfer of any size over a link, with compression, sequencing and checks | file transfer and NomadNet pages |
| Packet | MTU 500 bytes; header 2 bytes, 1 or 2 addresses of 16 bytes, context 1 byte, data 0-465 bytes; the medium needs at least 5 bit/s, half duplex | works on any radio we have |
| Instance and Transport Node | every program is a Reticulum *instance*; it becomes a *Transport Node* (relays traffic, passes announces, answers path requests) only with `enable_transport = Yes`, meant for systems "fixed in one place" and available most of the time; default No | the deck is an instance, never a transport node (D7); a Pi 4B at home could be one (7.4) |
| Interface modes | full (default), gateway, access_point, roaming, boundary, internal: they change announce propagation and path expiry | only relevant on a transport node |
| IFAC | named virtual networks and passphrases on an interface: a per-packet signature (8-512 bits) drops foreign packets | a private family network on a shared channel |
| Interface discovery | `discover_interfaces`, signed discovery announces with stamps, `autoconnect_discovered_interfaces`, `bootstrap_only`; directories `directory.rns.recipes` and `rmap.world` | the old "testnet" host names still appear in manual examples (`dublin.connect.reticulum.network:4965`), but the manual now speaks of a volunteer "distributed backbone"; their reachability today is U |
| Propagation node | stores LXMF messages for offline users; nodes peer and synchronise; a client picks one and downloads its messages; messages expire after 30 days (`MESSAGE_EXPIRY`) | offline delivery; a propagation node itself is not for a Zero 2 W (memory, 3.3) |
| Stamps | proof of work. LXMF: a recipient may demand an inbound stamp cost; a propagation node demands a stamp on every message it accepts, default cost 16, minimum 13 (`LXMRouter.py`); tickets let known contacts skip stamps. RNS 1.6.x: stamps on identities, announces, path and link requests. On Linux the work runs in one process per CPU core for costs of 14 and above (`Stamps.py`) | CPU and power on the Zero 2 W (3.3) |
| Cryptography | Ed25519, X25519, HKDF, tokens based on Fernet with AES-256-CBC and HMAC-SHA256, SHA-256 and SHA-512; OpenSSL through PyCA `cryptography` by default; a pure-Python fallback (`rnspure`) that is "much" slower and less reviewed | use the Debian `python3-cryptography`, never `rnspure` |
| Audit status | LXMF "has not been externally security audited" (README); NomadNet "is beta software" (README) | say so in our README |

The LXMF README still describes AES-128 link encryption; the manual says AES-256 (V both). The manual is the newer
text.

**Node and client, in short (V, I).** In RNS terms every running program is an *instance*; a *Transport Node* is an
instance that also routes for others. In LXMF terms a *client* is an app with its own delivery destination (an LXMF
address) that sends and receives messages, and a *propagation node* is a server that stores and forwards messages for
clients that are offline. A NomadNet *node* is a destination that serves pages and files. The deck would be an RNS
instance without transport and an LXMF client (I); a Pi 4B at home could be all three.

## 3. Software on our boards (question 2)

### 3.1 Implementations

| Implementation | Language, licence | State, in its own words or facts | Compatibility | Fit for us |
|---|---|---|---|---|
| RNS (reference) | Python 3.7+, Reticulum License | 1.6.0; `Development Status :: 5 - Production/Stable` (`setup.py`); core under 15,000 lines (manual) | defines the protocol | the only mature option |
| `rnspure` | the same code, no declared dependencies | contents "completely identical" to `rns` (manual) | same | only where `cryptography` cannot be installed: not our case |
| Reticulum-Go (Quad4 Software) | Go, Reticulum License (earlier MIT, 0BSD, Apache-2.0) | v1.4.2, 2026-11-01; "still considered experimental, may not always be up to date with the Python reference, and not ready to be reliably used" | claims wire compatibility; the manual lists Ren Browser, built on it | a later look if Python fails P0 |
| Reticulum-rs (Beechat) | Rust, MIT | last push 2026-08-22; TCP, serial, Kaonic transports; tests against Python RNS | partial (no LXMF mentioned) | no |
| microReticulum | C++, Apache-2.0 | 0.5.0, 2026-06-27; "Port of Reticulum Network Stack to C++", aimed at microcontrollers | U | firmware, not apps |
| reticulum-kt | Kotlin, MPL-2.0 | "almost completely AI generated. You probably shouldn't use this" (its README) | own conformance suite | no |
| Prns | Rust, claims Apache-2.0 / MIT | the manual's Brandolini's Reference examines it as a machine-generated derivative of RNS and concludes: "Treat the Prns project as hostile" | n/a | never |

The manual's guide to evaluating implementations welcomes honest, human-made implementations and names "roughly a
dozen machine-generated 'Reticulum implementations'" (V). This weighs on D3 and D16.

### 3.2 Getting it onto a board

Packages (V, PyPI JSON and Debian `madison`, 2026-11-07):

| Item | Fact |
|---|---|
| `rns` 1.6.0 | one `py3-none-any` wheel, 681 KB; requires `cryptography>=3.4.7`, `pyserial>=3.5`; Python 3.7+ |
| `lxmf` 1.3.0 | `py3-none-any`, 75 KB; requires `rns>=1.6.0` |
| `nomadnet` 1.5.0 | 337 KB; requires `rns>=1.6.0`, `lxmf>=1.3.0`, `urwid>=3.0.5`, `qrcode` |
| `sbapp` (Sideband) 2.2.0 | a 68.6 MB wheel; requires Kivy, numpy, pillow, pycodec2, LXST and more |
| Debian | no package for RNS, LXMF or NomadNet in bookworm, trixie, forky or sid |
| Debian 13 (trixie) arm64 | `python3-cryptography` 43.0.0, `python3-serial` 3.5, `python3-bleak` 0.22.3, `python3-urwid` 2.6.16 (below NomadNet's 3.0.5), `python3-qrcode` 8.2, `python3-msgpack` 1.0.3, `pipx` 1.7.1 |

Upstream's install advice for a Raspberry Pi is a 64-bit OS, `sudo apt install python3 python3-pip python3-cryptography
python3-pyserial`, then `pip install rns --break-system-packages`; on Debian since 2023, `pipx install rns` (V manual).
On 32-bit systems `cryptography` must be built with Rust, and `rustup` can run out of 512 MB (V, discussion #262); all
our boards are arm64 or amd64 (V guidelines 5). Upstream does not see a `.deb` as necessary ("mistakes the mechanism
for the goal", V manual) but the licence allows redistribution.

| Way to install | Pros | Cons |
|---|---|---|
| The user runs `pip`/`pipx` | upstream's way; always current | outside Settings > Apps; `--break-system-packages`; our apps cannot rely on a version |
| **Pinned wheels vendored into our `.deb`** (Mesh Hop's meshcore-cli bundle: pinned wheels with sha256, unpacked into `/opt/mesh-hop/cli/py`, one `LICENSE.<name>` per bundled distribution, V `cli/vendor.py`) | installs and updates from Settings > Apps; tested versions; the same pattern already passed review; `rns` and `lxmf` are pure Python (one bundle for both architectures) | we must re-release on upstream updates (1.6.2 enforcement); duplicated if the user also pip-installs |
| A `.deb` that runs `pip` in `postinst` | small package | breaks guideline 5 (no network or long jobs in maintainer scripts) |
| Upstream `rnpkg` (package distribution over Reticulum) | upstream tool | not read; U |

### 3.3 Resources on a Pi Zero 2 W

| Item | What is known | Marker |
|---|---|---|
| RAM of `rnsd` or of a Python LXMF client | no published figure for a Zero 2 W (searched the web and the discussions) | U, probe P0-b |
| RNS 1.6.0 memory | "for non-transport instances, standalone applications and appliance devices by up to 80%" less, about 50 % less for long-running transport nodes (release notes) | V claim; on our boards U |
| LXMF propagation node memory | a testnet node with about 70,000 messages and 100+ peers went from about 950 MB to about 175 MB with LXMF 0.6.1 | V (#684); a propagation node is not for the deck |
| Propagation index speed | Zero 2 W 3,867 messages/s, Pi 4 8,180, Pi 5 18,223 | V (#684) |
| Power | Zero 2 W running RNS (Alpine), Wi-Fi and Bluetooth off: about 400 mW; Wi-Fi connected: about 530 mW; RNodes: RAK4631 about 125 mW, Heltec v3 about 800 mW | V (#963, community measurements) |
| Python start | Mesh Hop's meshcore-cli on the deck: 13.1 s cold (bytecode compilation), 2.5 s warm | V (Mesh Hop history; guidelines 14) |
| A cost-16 stamp | on average 2^16 = 65,536 attempts; each attempt is one SHA-256 of a 768 KB workblock (3,000 HKDF chunks of 256 bytes) plus 32 bytes; one worker process per core | V (`Stamps.py`) |
| Its time on a Zero 2 W | about 50 GB hashed per stamp; at an assumed 60-120 MB/s of SHA-256 per Cortex-A53 core without the ARMv8 crypto extensions, 2-5 minutes on all 4 cores | I (the throughput and the missing extensions are U) |
| RNS 1.6.1 destination stamps | generated once per destination, "relatively high default target values", "Expect some CPU usage" | V; the cost U |
| Our rules | native app VmRSS under 25 MB; "no Python for long-running parts"; idle CPU a few percent; Mesh Hop's earlier deck resets were put down to CPU-heavy Python work, but the owner says they were accidental reboots or unplugging, so there is no load evidence either way | V (guidelines 6, 14) |

Mitigations to try in P0 (I): run the service with `Nice=10`, `IOSchedulingClass=idle` and `CPUQuota=200%` in its
systemd unit (the stamp workers then share two cores' worth of time: slower but cooler), keep the manual's
`MALLOC_ARENA_MAX=2`, compile the bytecode at install time (`python3 -m compileall -q` in `postinst`, seconds, I) so the
first start is not the 13 s cold case, and start stamp work only on demand with a visible "Preparing message (proof of
work)..." state that can be cancelled (guidelines 6).

### 3.4 Start-up, service model, SD wear

- **Service.** The manual gives both a system unit and a user unit (`Type=simple`, `Restart=always`, `RestartSec=3`,
  `Environment=MALLOC_ARENA_MAX=2`, `ExecStart=rnsd --service`; lingering for start at boot) (V). Our appliance boards
  already run the launcher as a user service with lingering (V `install.sh`), so a user unit fits; on the desktop
  boards (HackBerry, Pi 5, tablet) the app would start the user unit itself (I).
- **Start-up time** to "ready" on the deck: U (probe P0-b).
- **First start (V).** "When Reticulum is started for the first time, it will create a default configuration file, with
  one active interface", the AutoInterface, which sends discovery traffic on every network the board is on (I). Our
  package must write its own first configuration before RNS ever starts (5.3).
- **SD wear (V).** RNS 1.6.0 stores known destinations, ratchets and cached announces in SQLite (WAL,
  `synchronous=NORMAL`, journal capped at 8 MB), persists every 12 hours (`PERSIST_INTERVAL`) and cleans caches every 15
  minutes; LXMF 1.3.0 adds an SQLite store. With `--service`, RNS writes a log file capped at 5 MB plus one rotated
  copy. Proposal (I): no `--service`, log to the journal at level 3 (notices), so a non-transport deck writes little.

### 3.5 How an app can talk to a running instance

| Way | What it is (V, `Reticulum.py` and the manual) | For us |
|---|---|---|
| Shared instance | the first program to start RNS opens the interfaces; later programs connect to it over an abstract Unix socket (`\0rns/<instance_name>`, or TCP 37428/37429) and exchange raw RNS packets; every client still runs the RNS stack itself | a native C++ client would have to be an RNS implementation |
| RPC channel | `multiprocessing.connection`, authenticated with a hash of the transport identity's private key, msgpack messages (statistics, path table, identity data) | internal and undocumented |
| Python API | `RNS.Reticulum`, `Destination`, `Link`, `Resource`, announce handlers, `get_interface_stats()` (per interface: rx/tx, bitrate, and for RNodes RSSI, SNR, noise floor, interference, airtime and channel load short/long, battery, CPU/memory load of the device) | what the bridge uses |
| LXMF API | `LXMRouter` (outbound, delivery callbacks, propagation node selection and sync), `LXMessage` | what the bridge uses |
| `lxmd` | LXMF router and propagation daemon; `--on-inbound PATH` runs a program per received message | receive-only hook, no send API: not enough |
| CLI tools | `rnstatus -j` (JSON), `rnpath`, `rnprobe`, `rnid` | fine for probes; each call starts Python (seconds on the deck, I) |
| MeshChat backend | `meshchat.py` "runs a Reticulum instance and a WebSocket server"; the web page sends and receives LXMF "encoded in json via the WebSocket" | an existing JSON API, but web-shaped, not a stable contract, and heavy (I) |

A useful property (V): if a shared instance already runs (the user's own `rnsd`, NomadNet), a new program connects to
it instead of opening the interfaces. Our bridge therefore works both as the instance and as a client, with no code of
ours for either case.

### 3.6 Maintenance status

- `MIRROR.md`, added 2025-12-27: "All potential future development is happening elsewhere"; "there will be no support,
  no responses to issues, no discussions"; "Occasional updates may appear at unpredictable intervals" (V). GitHub
  issues are off, discussions on (V, API).
- In practice: RNS 1.5.1 (2026-08-28) to 1.6.0 (2026-11-07) is 8 releases; LXMF 0.9.9 (2026-05-21) to 1.3.0 is 8 releases (V PyPI).
- Stamp roll-out (V release notes): 1.6.0 verifies but does not enforce or emit; 1.6.1 emits by default; 1.6.2
  enforces, and old nodes "will not be able to communicate with stamp-enforcing nodes".
- Consequence (I): a deck left on an old bundle drops off the network after 1.6.2; the package needs a release per
  upstream release for a while (a small worker task each, about 0.1-0.25 ws, I), and a documented way for the owner to
  run a newer upstream version before ours catches up.

## 4. Transports and radio (question 3)

### 4.1 Interfaces (transport-agnostic)

**Principle (owner, 2026-11-07).** Reticulum runs over any medium that carries at least 5 bit/s half duplex (2.2); our
apps take advantage of that and never assume LoRa. They talk only to the bridge (5.3), which hands RNS whatever
interfaces the user has enabled; a deck with only Wi-Fi, only Ethernet, only a TCP entrypoint or only an RNode is
equally a Reticulum node. The status app's **interface manager** lists every configured interface with its state and
offers an add-interface form per type with only that type's options (I, proposal; D19). UI wording stays
hardware-neutral: "interface" and "network", not "radio", except on an RNode's own page.

Supported interface types, RNS 1.6.0 (V: manual `interfaces.rst` at `cb1260891d` for the types and their options;
`Reticulum.py` for the types the loader knows). Every type also takes the common options of 4.2 (`enabled`, `outgoing`,
`mode`, `network_name`, `passphrase`, ...).

| Type | Medium | Type-specific options the add form shows (manual) | What the owner has | Notes (V unless marked) |
|---|---|---|---|---|
| `AutoInterface` | Wi-Fi or Ethernet, same layer-2 network, IPv6 link-local multicast | `group_id`, `multicast_address_type`, `devices`, `ignored_devices`, `discovery_scope`, `discovery_port`, `data_port` | built-in Wi-Fi (2.4 GHz only on the Zero 2 W, V report 054), Ethernet on some boards, USB Wi-Fi adapters | zero configuration; UDP 29716 and 42671 by default; client isolation on an access point blocks it; `devices = wlan0` pins it |
| `BackboneInterface` | TCP over any IP network (Linux and Android only) | `listen_on`, `port`, `device`, `prefer_ipv6`, `remote`, `target_host`, `target_port`, fast-flapping options | any network | the newer high-performance TCP interface |
| `TCPClientInterface` | TCP to a host | `target_host`, `target_port`, `kiss_framing`, `fixed_mtu`, `i2p_tunneled` | any network | "TCP connections reveal the IP address of both your instance and the server"; also reaches KISS modems over TCP |
| `TCPServerInterface` | TCP listener | `listen_ip`, `listen_port`, `device`, `prefer_ipv6`, `port`, `i2p_tunneled` | a LAN | a home node or a Pi 4B serving decks on the LAN; a public listener is not for a deck (I) |
| `UDPInterface` | UDP unicast or broadcast | `listen_ip`, `listen_port`, `forward_ip`, `forward_port`, `device`, `port` | a LAN | AutoInterface is the simpler choice on Wi-Fi (manual) |
| `I2PInterface` | I2P through a local `i2pd` | `connectable`, `peers` | | hides both IP addresses; the daemon's RAM and CPU on a Zero 2 W are U; heavy (I) |
| `RNodeInterface` | an RNode over USB serial, BLE (`ble://`, `ble://<name>`, `ble://<MAC>`, needs `bleak` and prior pairing) or TCP (`tcp://<host>`, RNodes with Wi-Fi) | `port`, `frequency`, `bandwidth`, `txpower`, `spreadingfactor`, `codingrate`, `airtime_limit_short`, `airtime_limit_long`, `flow_control`, `id_callsign`, `id_interval` (4.2) | Heltec LoRa32 V4 with RNode firmware (4.3) | RNS needs RNode firmware 1.52 or later (`REQUIRED_FW_VER`); BLE uses the Nordic UART service; the Zero 2 W's combined Wi-Fi/Bluetooth chip also serves the Bluetooth keyboard (guidelines 6) |
| `RNodeMultiInterface` | an RNode with several transceivers | `port`, ID beacon, then per sub-interface `vport`, `frequency`, `bandwidth`, `txpower`, `spreadingfactor`, `codingrate`, airtime limits | none | same rules as `RNodeInterface` |
| `SerialInterface` | a transparent serial link (cable, serial radio modem; a Bluetooth Classic RFCOMM port, I) | `port`, `speed`, `databits`, `parity`, `stopbits` | | |
| `KISSInterface` | a KISS TNC and radio (packet radio) | serial options, `preamble`, `txtail`, `persistence`, `slottime`, `id_callsign`, `id_interval`, `flow_control` | none known | the band and its rules are the user's matter |
| `AX25KISSInterface` | a KISS TNC with AX.25 framing | serial and KISS options, `callsign`, `ssid` | none known | as above |
| `PipeInterface` | any program on stdin/stdout | `command`, `respawn_delay` | | custom media |
| Custom interface modules | user-supplied Python in the configuration's `interfaces` folder | defined by the module | | loaded by RNS (V `Reticulum.py`); the add form can offer them only as raw text (I) |

The loader also knows a `WeaveInterface` that the manual does not document (V `Reticulum.py`; what it is: U); the
interface manager leaves undocumented types out until upstream documents them.

### 4.2 RNode settings the host controls (`RNodeInterface`)

The RNode keeps no radio settings of its own in normal mode: RNS sends them to the device each time the interface comes
up (KISS commands for frequency, bandwidth, TX power, spreading factor, coding rate and the two airtime limits, V
`RNodeInterface.py`), then reads them back and takes the interface offline on a mismatch of frequency (more than 100 Hz),
bandwidth, TX power, spreading factor or radio state (V, `validateRadioState`). So the deck's configuration is the only
place where the radio is set, and the app's RNode page is the editor for it.

Options `RNodeInterface` reads from its configuration section (V: `RNodeInterface.py` lines 150-169 and 308-349,
RNS 1.6.0 @ `cb1260891d`; manual `interfaces.rst`, "RNode LoRa Interface"):

| Option | Unit, type | Accepted by RNS 1.6.0 | If absent | Upstream example (manual) |
|---|---|---|---|---|
| `port` | serial path, `ble://[name or MAC]`, `tcp://host` | any; required | `ValueError`, interface offline | `/dev/ttyUSB0` |
| `frequency` | Hz, integer | 137,000,000 to 3,000,000,000 | 0: invalid, interface offline | `867200000` |
| `bandwidth` | Hz, integer | 7,800 to 1,625,000 | 0: invalid, interface offline | `125000` |
| `txpower` | dBm, integer | 0 to 37 | 0: invalid, interface offline | `7` ("7 dBm (5 mW)") |
| `spreadingfactor` | integer | 5 to 12 (the manual's comment says 7 to 12) | 0: invalid, interface offline | `8` |
| `codingrate` | integer, 5 = 4/5 ... 8 = 4/8 | 5 to 8 | 0: invalid, interface offline | `5` |
| `airtime_limit_short` | percent, float, window of about 15 s | 0 to 100 | no limit sent | commented out: `33` |
| `airtime_limit_long` | percent, float, rolling 60 minutes | 0 to 100 | no limit sent | commented out: `1.5` |
| `flow_control` | boolean | | `False` | commented out: `False` ("for certain homebrew RNode interfaces with low amounts of RAM") |
| `id_callsign` | text, at most 32 bytes UTF-8 | both needed, else ignored | no ID beacon | commented out: `MYCALL-0` |
| `id_interval` | seconds, integer | with `id_callsign` | no ID beacon | commented out: `600` |

Common interface options that also apply (V, manual "Common Interface Options"): `enabled` (default off), `outgoing`
(default `True`; `False` = no announces and no path requests on that interface, but a packet to an already known
path is still sent, 8.1; D8), `mode` (default `full`), `network_name`, `passphrase`, `ifac_size` (8-512
bits), `announce_cap` (default 2 %), `bitrate`, `gravity`, `bootstrap_only`, plus the interface-discovery options
(`discoverable` and its `discovery_*` fields), which are not for the deck.

Facts on the device side (V, RNode Firmware 1.86, `master` @ `56775c51b6`):
- The airtime limits are enforced **in the firmware**: RNS sends the percentage times 100 (`CMD_ST_ALOCK`,
  `CMD_LT_ALOCK`); the firmware stops transmitting while the short- or long-term airtime is above its limit
  (`airtime_lock` in `RNode_Firmware.ino`); a value of 100 % or more means no limit; both default to 0 = no limit
  (`Config.h`).
- Interference avoidance (CSMA, on by default: `avoid_interference = true` in `Config.h`) is a device setting changed
  with `rnodeconf -x` / `-X`, not an `RNodeInterface` option.
- `rnodeconf --freq/--bw/--txp/--sf/--cr` store radio settings in the device "for TNC mode" (V `rnodeconf.py` help),
  the stand-alone KISS mode; they do not apply when RNS drives the RNode (I, since RNS sets every value at start).

What the app shows (proposal, D10): the interface manager's RNode form (add and edit) with exactly the options above (the ID beacon and
`flow_control` under "Advanced"); the radio fields start empty, the airtime fields are always visible and start empty
(no limit, as upstream); no region preset, frequency list or plan. The app validates only RNS's own ranges, so the user
sees the error before RNS logs it. Radio rules differ by country: frequency, bandwidth, spreading factor, TX power and
airtime (duty cycle) are the user's choice, the app does not decide or enforce a region, and the user is responsible for
the rules where they transmit (not legal advice).

Airtime of typical packets (I: Semtech's LoRa formula, 8-symbol preamble, explicit header, CRC on; the RNode's own
preamble may be longer):

| Setting | Bit rate | Path request, 51 B | Announce, 167 B | 500 B packet | Link set-up (83+115+99 B) | Announces per hour per 1 % of `airtime_limit_long` |
|---|---|---|---|---|---|---|
| SF8, 125 kHz, CR 4/5 | 3.1 kbit/s | 0.19 s | 0.48 s | 1.33 s | about 0.9 s | about 74 |
| SF10, 125 kHz | 0.98 kbit/s | 0.62 s | 1.56 s | 4.30 s | about 3 s | about 23 |
| SF7, 250 kHz | 10.9 kbit/s | 0.05 s | 0.14 s | 0.38 s | about 0.25 s | about 260 |

A 10 KB file over LoRa at SF8 is about 30 s of airtime before overheads (I), almost all of a 1 % hourly limit; the
status app can show it from the RNode's reported airtime and channel load (3.5).

Antennas (I): an antenna matched to the chosen band on the RNode; never transmit without one. The Wio-SX1262 module's
antenna connector type is U.

### 4.3 The owner's boards (compatibility note)

Any compatible board with RNode firmware can be the RNode (owner, 2026-11-07); it is the owner's own board, and the app
does not flash, update or own it. This table only records the owner's hardware and which boards upstream supports (V RNode Firmware 1.86 README: any SX1276, SX1278, SX1262, SX1268 or SX1280 module with
SPI and the DIO pins); it is not a purchase recommendation (D11).

| Board | RNode? | Notes |
|---|---|---|
| **Heltec LoRa32 V4** (the owner's, RNode firmware installed) | **yes, official**: "Heltec LoRa32 v4 devices" (V RNode README); `BOARD_HELTEC32_V4 0x3F` (V `Boards.h`); in `rnodeconf` as product "Heltec LoRa32 v4" (`PRODUCT_H32_V4 0xC3`), model `0xC8` (with PA), hardware range 860-930 MHz, 28 dBm maximum, firmware `rnode_firmware_heltec32v4pa.zip`, which release 1.86 (2026-04-24) ships (V `rnodeconf.py`, release assets) | ESP32-S3, SX1262 with TCXO, PA and LNA, display, Wi-Fi, BLE, no Bluetooth Classic (V `Boards.h`); `rnodeconf`'s installer: RNode firmware on Heltec devices "should currently be considered experimental"; host links: USB serial (V for RNodes in general; whether the V4 shows as native USB CDC or through a USB-UART chip, and its `/dev` name: U), BLE (V `HAS_BLE`), and TCP over Wi-Fi (U for this board); the firmware version on the owner's board: U (read with `rnodeconf -i`) |
| XIAO ESP32S3 + Wio-SX1262 (Mesh Hop's spare board B, MeshCore at last check) | **yes**: "SeeedStudio XIAO ESP32S3 devices (with Wio-SX1262)" (V RNode README) | BLE (V); flash with `rnodeconf --autoinstall` from the PC (V manual) or a web flasher (V README: Liam Cottle's, RNS Moscow, Aetherlab); it stops being a MeshCore node until reflashed (I) |
| Seeed XIAO nRF52840 (board A, Mesh Hop's main node) | not on the official list (V) | keep it on MeshCore |
| Seeed T1000-E | not on the official list (V) | forks U |
| "Other LoRa boards" | unknown models | the official list: LilyGO T-Beam (SX1276/8 and SX1262/8), T-Beam Supreme, T-Deck (display off), LoRa32 v1.0/v2.0/v2.1, T3S3 (SX1276/8, SX1262/8, SX1280), T-Echo; Heltec LoRa32 v2/v3/v4, Heltec T114; RAK4631; XIAO ESP32S3 + Wio-SX1262; homebrew ATmega1284p, ATmega2560, Feather ESP32, generic ESP32 (V) |
| USB Wi-Fi adapters | n/a | a second Wi-Fi interface lets AutoInterface run on a dedicated Reticulum network (shared access point, ad-hoc or mesh mode) while `wlan0` stays on the home network (I); a 5 GHz adapter adds a band the Zero 2 W lacks; driver support per adapter U |

### 4.4 Mesh Hop and port arbitration

Facts (V): Mesh Hop opens a port, treats `EBUSY` (another program set `TIOCEXCL`) as busy, takes
`flock(LOCK_EX|LOCK_NB)` and sets `TIOCEXCL` (`serial_transport.cpp`). RNS's `RNodeInterface` opens the port through
pyserial without `exclusive` and without `TIOCEXCL` (`RNodeInterface.py`). Mesh Hop never connects by itself: the user
taps a node (Mesh Hop 0.5.0 decision).

Consequences (I): while RNS holds an RNode, Mesh Hop's lock check succeeds, and a tap on that port would open it a
second time and split the byte stream between two programs; the other way round, a port Mesh Hop holds makes RNS's open
fail with `EBUSY`, and RNS keeps retrying (its retry behaviour U).

Proposed rule:
1. **One owner per radio.** The Reticulum service owns the RNodes in its configuration, named by
   `/dev/serial/by-id/...` (the manual's advice, V), never `ttyUSB0`.
2. **Busy means held by anyone.** Our serial apps (Mesh Hop, Serial Console, mmWave Radar, Deck Atlas's GPS reader)
   treat a port as busy when any other process of the user has it open (`/proc/<pid>/fd`), the rule report 056 already
   proposed (V 056 4.2).
3. **The bridge also locks.** Before RNS opens a configured RNode, the bridge opens the same device and takes `flock`
   on it for as long as the interface is up; Mesh Hop's existing check then sees it as busy (I: `flock` on a device node
   is advisory and per inode, so it works between cooperating programs).
4. **Say who has it.** The status app shows "RNode on ttyACM0 (Heltec V4), used by Reticulum"; Mesh Hop's node list says
   "used by Reticulum". An RNode answers the KISS detect command (`CMD_DETECT 0x08`, `DETECT_REQ 0x73`, V), so Serial
   Console could identify one, but only on request, since it writes to the port.
5. **BLE.** One central per RNode; scans short and only from the status app, as Mesh Hop does.

## 5. App concepts and architecture (question 4)

### 5.1 Concepts

| # | Concept | What it needs | Size (ws) | Small screens, guidelines |
|---|---|---|---|---|
| a | **Native LVGL messenger** | bridge: LXMF send/receive with delivery states (outbound, sent, delivered, failed), contacts from heard announces or typed/pasted addresses, propagation node choice and sync, identity export; UI: contacts, conversations, compose with the shared on-screen keyboard rule, a Settings page | L, 3.5-5 | Mesh Hop's chat pages are the house pattern (V); 640x480 first, 480x320 and 320x480 by decision; launcher toasts and beeps for new messages through `launcher.sock` (V guidelines 3) |
| b | **NomadNet page browser** | bridge: a link to the node's `nomadnetwork.node` destination and a request for `/page/index.mu` (V `Node.py`), responses as micron text; a micron renderer: formatting, 3-hex-digit colours, alignment, headings and sections, dividers, links, anchors, tables, input fields, checkboxes, radio groups, request links, partials (with refresh), literals, comments, page colour and cache headers, images in WebP only (V `Guide.py` sections) | M-L, 2.5-4 | micron is written for terminal columns; on 640x480 at a readable size about 50-60 columns (I); stb_image (already in the repo) has no WebP decoder (I), so images start as their alt text |
| c | **Status app: interface manager, network status and announce monitor** | bridge: the interface manager (list, add, edit, enable, disable; one form per type with that type's options, 4.1 and 4.2), per-interface status (online, rx/tx bytes, bitrate, errors; for RNodes also RSSI, SNR, noise floor, airtime, channel load, battery), heard announces with hops and interface, paths, the per-interface transmit switch | M, 2-2.75 (part of P1) | listen-first; fits every size; works on a deck with only Wi-Fi; the natural first app |
| d | **Wrapping Sideband or the NomadNet TUI** | NomadNet: kind B terminal app, the upstream GPL program unmodified with `urwid>=3.0.5` bundled (Debian's is 2.6.16); Sideband: Kivy needs a display server or an SDL/KMS backend, a 68.6 MB wheel plus numpy and pillow, CC BY-NC-SA | NomadNet S, 0.5-1; Sideband not practical | the terminal page has no status bar, touch only scrolls, no F-keys, about 40 columns at 480 px wide (V guidelines 1 and 2); NomadNet's usability there is U |
| e | **Positions and telemetry with Deck Atlas** | `position.state` (the draft contract of report 054 6.3; its writer comes with Deck Atlas's next version, V Deck Atlas README); LXMF `FIELD_TELEMETRY` (0x02) in Sideband's packed sensor format: the location sensor packs latitude and longitude as signed 32-bit integers in millionths of a degree, altitude, speed, bearing and accuracy in hundredths, plus a timestamp (V, `sense.py`, read as a format, no code copied); received positions written as an overlay file for Deck Atlas (report 054 6.2). Sideband's plugin system has telemetry, command and service plugins, among them a gpsd location example (V README), so a desktop Sideband can be the collector on the other side | M, 1.25-2 | never automatic: a position goes only to a contact the owner picks, per send or per explicit "share for N minutes" |
| f | **File transfer** | LXMF `FIELD_FILE_ATTACHMENTS` (0x05), or an RNS Resource (as `rncp`) | S-M, 1-1.5 | fine over Wi-Fi; over LoRa the bit rate and any airtime limit the user sets slow it (4.2) |
| g1 | Paper messages and identity QR codes | LXMF's PAPER method and `lxm://` URIs; a QR encoder exists in the repo (Project Nayuki's MIT encoder in Home Assistant, V guidelines 4) | S, 0.5 | exchanging addresses without announcing (privacy) |
| g2 | A home transport node | a Pi 4B, always on, with an RNode and Wi-Fi, `enable_transport = Yes` | S (configuration) | the manual's "personal infrastructure" advice; decks reach the mesh through it over Wi-Fi |
| g3 | Reticulum Relay Chat (live rooms) | the RRC protocol and an `rrcd` hub (V manual) | M | later |
| g4 | Remote shell `rnsh` / `rnx` | in RNS | S as a terminal app | administration of the home node over LoRa; later, and with care |
| g5 | Offline libraries over NomadNet | Retipedia serves ZIM files to NomadNet clients (V manual) | through (b) | links to the "offline resources" exploration topic |

Common core: (a), (b), (c), (e), (f), g1 and g3 all need the same service (RNS + LXMF + the bridge API). Only (d) stands
apart.

### 5.2 Architecture options

| Option | Description | Pros | Cons |
|---|---|---|---|
| **A** | one Python bridge process: the RNS shared instance + the LXMF router + a JSON-lines socket; native LVGL apps | one Python process; only upstream's public API; NomadNet and `rnstatus` can join the same instance; native, touch-complete UIs | a long-running Python process (D2); RAM unknown (P0); about a thousand lines of our own Python to maintain (I) |
| B | upstream `rnsd` as the service, the bridge as its client | the user can stop or swap `rnsd`; a clean split | two Python processes, roughly twice the memory (I) |
| C | native C++ RNS endpoint and LXMF in our apps, talking to `rnsd` through the local socket | no Python in our apps | the largest option by far; crypto and protocol correctness; interoperability tests; upstream regards machine-generated reimplementations as infringing (6.3); still needs `rnsd` for the interfaces |
| D | an alternative implementation (Reticulum-Go as daemon or library) | a Go binary, maybe lighter (U) | experimental by its own README; LXMF coverage U; Reticulum License anyway |
| E | wrap NomadNet's terminal UI only | cheapest | no touch, no native UI, a GPL program carried as is |
| F | MeshChat or MeshChatX backend, native UI on its WebSocket JSON | an existing JSON API | heavy, web-centred, no stable contract (I) |

Recommendation: A, which also behaves as B when the user runs their own instance (3.5).

### 5.3 Proposed design (draft, for the owner's decisions)

**Service package.** Name per D5. Contents (I): `/opt/<id>/py/` the vendored, pinned `rns` and `lxmf`;
`/opt/<id>/bridge/` our MIT Python; a systemd user unit with the limits of 3.3; `/usr/share/doc/<id>/` with
`copyright`, `LICENSE.rns`, `LICENSE.lxmf`. `depends: python3 (>= 3.10), python3-cryptography, python3-serial`;
`recommends: python3-bleak`; `permissions: background_service, network, additional_hardware`; `license: "MIT AND
LicenseRef-Reticulum"` (`make_registry.py` does not validate the field, V). Whether Settings > Apps handles a package
without a home tile is U; the simple way is to ship the service inside the status app's package and let the other
Reticulum apps depend on it.

**How apps find it.** A socket `$XDG_RUNTIME_DIR/<id>/bridge.sock` (mode 0600, on tmpfs, no SD writes) and a small
`key=value` state file beside it (pid, versions, up/down, identity hash, LXMF address, interfaces up), in the style of
`status.state`. An app that finds neither offers "Start Reticulum" (`systemctl --user start <unit>`, I).

**Interface manager (D19).** The bridge owns the `[interfaces]` section of the configuration. It offers the documented
types of 4.1 with, for each, the list of that type's options (name, type, range or choices, upstream default if any),
so the status app builds one add/edit form per type from data rather than from code per medium; it validates against
RNS's own ranges, writes the configuration (backup, atomic write) and brings the interface up or down. New interfaces
start with `enabled = no` and `outgoing = False` (D8). Status per interface comes from `get_interface_stats()` (3.5).
RNS 1.6.0 can add, remove, re-attach and reload an interface at run time without restarting the instance
(`rnstatus --attach/--detach/--reload <name>`, or `attach_interface`, `detach_interface`, `reload_interface` in the Python
API, PC dry run 8.1); the interface section must exist in the config file first, so the bridge writes the file and then
attaches. No service restart is needed.

**Bridge protocol (sketch).** JSON lines, requests with an id and a timeout, plus an event stream: `hello`, `status`,
`ifaces`, `iface.types` (types and their option lists), `iface.add`, `iface.edit`, `iface.remove`, `iface.enable`,
`iface.outgoing` (the transmit switch), `announces.subscribe`, `paths`, `announce`, `lxmf.send`,
`lxmf.messages`, LXMF state events, `pn.list`, `pn.set`, `pn.sync`, `page.get`, `identity.info`, `identity.export`,
`service.stop`. Every verb that transmits is an explicit request (D8); long work (stamps, page loads, syncs) reports
progress and can be cancelled (guidelines 6).

**Configuration.** `~/.reticulum` stays the standard location, so NomadNet, `rnstatus` and, on the desktop boards,
Sideband share it (the manual's search order is `/etc/reticulum`, `~/.config/reticulum`, `~/.reticulum`, V). The
package writes a first configuration only when none exists: `share_instance = Yes`, `enable_transport = No`, every
interface present but disabled, log level 3; an existing configuration is never overwritten. The status app changes it
only through the bridge (backup copy, atomic write, interface reload); hand edits are kept (I).

**Identity.** The LXMF identity in `~/.local/share/<id>/` (mode 0600), separate from NomadNet's; the transport identity
stays in `~/.reticulum/storage` (V). Backup: "Export identity" shows the key as base32 text and a QR code after a
warning, as `rnid -x -B` does (V `rnid` options); import by pasting. Keys never in logs (guidelines 6). Losing or
lending the SD card hands over the identity (I).

**Permissions.** `dialout` for USB RNodes (the "explain the fix" message of guidelines 6); BlueZ over D-Bus for BLE, as
Mesh Hop already does as the deck user (V); no root at run time.

**Security and privacy rules (proposal).** Nothing transmits without the owner's action (D8); the display name stays
empty until set, and announces carry it; no automatic re-announce faster than 6 hours; positions never sent
automatically; AutoInterface only on chosen network devices; no public entrypoint preconfigured; IFAC passphrases and
identity files mode 0600; syncing with a propagation node tells that node which LXMF address is asking (I); logs hold no
message bodies, keys or passphrases; `rnpath -B` blackholes a spamming identity (V); clean exit on `SIGTERM`.

**Launcher.** No change needed (I). Everything lives in the apps repository.

## 6. Licensing (question 5)

This section is a reading of the licence texts for the owner's decision, not legal advice.

### 6.1 Components

| Component | Licence, as read in the repository (2026-11-07) | Ship or bundle | Link or wrap | What must ship |
|---|---|---|---|---|
| RNS 1.6.0 | Reticulum License, "Copyright (c) 2016-2026 Mark Qvist"; the manual: in force since 2025-04-15, from RNS 0.9.4 | yes, inside an app package, never in the launcher | imported by our Python bridge; our native apps only speak JSON to it | the full licence text as `LICENSE.rns` and an entry in `copyright` (guidelines 4) |
| LXMF 1.3.0 | Reticulum License, "Copyright (c) 2020-2025 Mark Qvist" | yes, as RNS | as RNS | `LICENSE.lxmf` |
| NomadNet 1.5.0 | GPL-3.0 (`LICENSE`; the README calls it "the open source GPLv3 license"); its `setup.py` classifier says "MIT License", which the licence file and README contradict: treat it as GPL-3.0 | only as a separate upstream program (kind B), unmodified | a separate process; no code copied into our MIT apps | GPL text, source URL in the description and README (guidelines 4, 10) |
| Sideband 2.2.0 | CC BY-NC-SA 4.0, plus "Permission is hereby granted to use Sideband in binary form ... and to freely distribute binary copies ... so long as no payment or compensation is charged" (README) | no: NonCommercial-ShareAlike does not fit our MIT repository (I) | a feature reference; its data formats read as facts for interoperability | nothing |
| RNode Firmware 1.86 | GPL-3.0; its SX1276 driver MIT | never shipped by us; the owner flashes it with `rnodeconf` | a separate device | nothing |
| MeshChat 2.4.0 | MIT | not planned | | |
| MeshChatX | 0BSD for its changes, MIT for the MeshChat parts | not planned | | |
| `cryptography`, `pyserial`, `bleak` | Debian packages, not bundled (their licences not read here, U) | Debian dependencies | | nothing of ours |
| Reticulum-Go | Reticulum License | not planned | | |

### 6.2 The Reticulum License and our rules

The text (V, `LICENSE` and the manual's chapter) is the MIT licence with two added conditions:
- "The Software shall not be used in any kind of system which includes amongst its functions the ability to
  purposefully do harm to human beings."
- "The Software shall not be used, directly or indirectly, in the creation of an artificial intelligence, machine
  learning or language model training dataset, including but not limited to any use that contributes to the training
  or development of such a model or algorithm."

Against our rules:
- **Launcher (V).** The launcher stays MIT-only; nothing of Reticulum goes into it.
- **Apps repository (V).** Guidelines section 12 rules out what "is not licensable under MIT, or under a licence you can
  ship and credit"; the Reticulum License can be shipped and credited (I), as Mesh Hop already ships BSD and Unlicense
  parts beside MIT.
- **Condition 1.** A hobby messaging device; no conflict (I).
- **Condition 2.** Upstream says the condition "does not forbid the use of machine-learning technology to *assist*
  development" (V manual). The open point for us (I, my interpretation): our code is written by AI agents; if the
  content of those sessions, which may include RNS or LXMF source a worker read, can be used by the AI provider to train
  models, that could be argued to "contribute to the training" of a model. Hence D17 (check the account's data-use
  setting) and D16 (workers stay on the public API and documentation).
- **Notice.** The licence text travels with every vendored copy.
- **Not an OSI or SPDX licence.** Some users and distributions consider it non-free; the manual argues the opposite
  (V). The app README should state the licence plainly.

### 6.3 Provenance: upstream's stance on machine-generated code

What the manual says (V, "Brandolini's Reference", read 2026-11-07): the protocol is public domain and "a genuine
clean-room implementation of the protocol, written by human beings ... is free and welcome"; but "an implementation
that was generated from RNS, reproduced by a copy process, is a derivative of licensed work"; claims of an LLM-made
"clean room" are rejected; it sets out a "void grant" argument from US copyright law; it separates *assistance* (the
human decides, understands and "can answer for every single line") from *substitution*; disclosed assistance is
accepted ("Technological assistance is not the problem"). The same warning heads the READMEs of the RNode firmware,
LXMF, NomadNet and Sideband (V).

What it means for us (I):
1. Do not reimplement RNS, LXMF or the micron parser from upstream source. Use the Python libraries as they are.
2. Keep our own protocol-touching code thin (a bridge calling the public API) and say in the README that it was written
   with AI assistance (our commits already carry `Co-Authored-By` trailers).
3. Write the micron renderer from the guide's user-facing description and our own test pages; the only code reference
   allowed is Micron Parser JS (Unlicense, V), never NomadNet's parser.
4. Never present our code as a "Reticulum implementation".

The risk, plainly: a thin, disclosed bridge that uses RNS as a library is what the licence allows and what upstream
calls assistance; the remaining risk is mostly reputational inside the community (I). A native reimplementation (option
C) would be the exact pattern the manual attacks.

### 6.4 What the owner must decide

D4 (vendoring), D16 (provenance rule), D17 (AI data use), and whether to accept a package carrying the Reticulum
License at all. All accepted by the owner on 2026-11-07 (section 1); D17 still needs the owner's check of the account setting.

## 7. Effort, phases, tests (question 6)

### 7.1 Phases

All costs are estimates (I), in worker sessions of about 0.4 M tokens; the controller's design passes and reviews are
extra.

| Phase | Content | Size | Cost (ws) | Owner time on boards | Exit |
|---|---|---|---|---|---|
| P0 probe | section 8: venv install on the Pi 4B and the deck, memory, CPU and start time, stamp timings, LXMF between the deck and the PC over Wi-Fi, the owner's Heltec V4 RNode listen-only for 30 minutes, NomadNet's TUI at the terminal page's size | S | 0.5-0.75 (probe scripts, reading the logs, a short note) | 60-90 min | go/no-go against the D2 budget |
| P1a service | the package: vendored `rns` and `lxmf`, the bridge (status, the interface manager with per-type option lists for every documented type, announces, transmit switches, announce, identity), the first-run configuration, the user unit with limits, the port lock (4.4), PC tests with two real RNS instances over TCP and AutoInterface, packages for arm64 and amd64 | M | 1.75-2.25 | | |
| P1b status app | native app: service start/stop, the interface list with per-interface status, the add/edit form per type (the RNode form per D10, 4.2), heard announces, paths, transmit switches, identity page; sizes per decision (640x480 first) | M | 2-2.75 | 20 min | |
| Docs (P1) | README rows, app page | | 0.25 | | |
| P2 messenger | (a) of 5.1 | L | 3.5-5 | 30 min | |
| Docs (P2) | | | 0.25 | | |
| P3 NomadNet browser | (b): text, links, fields first; partials and tables next; images later | M-L | 2.5-4 | 20 min | |
| P4 positions | (e), after Deck Atlas writes `position.state` | M | 1.25-2 | 20 min | |
| P5 files | (f) | S-M | 1-1.5 | 15 min | |
| Mesh Hop guard | D18 | S | 0.25 | 5 min | |
| Docs (P3-P5) | | | 0.5 | | |
| Optional: NomadNet wrapper | (d), D15 | S | 0.5-1 | 10 min | |

**Totals (I):** MVP (P0 + P1 + docs) **4.5-6 ws (1.8-2.4 M tokens)**; with the messenger **8.25-11.25 ws**; everything
except the optional wrapper **13.75-19.5 ws (5.5-7.8 M tokens)**. Upstream follow-up releases: about 0.1-0.25 ws each.

### 7.2 What can be tested where

| On the PC, no radio | Needs a board |
|---|---|
| RNS and LXMF in a WSL venv; two real instances linked by TCP or AutoInterface on a virtual network (I); bridge tests against real RNS (no mock of the protocol); LXMF interoperability with NomadNet and Sideband on the PC; multi-hop cases in the Reticulated simulator (V manual); native apps against a fake bridge replaying JSON fixtures; headless scenes at every size; micron golden pages | RAM, CPU, start time, stamps and power on the Zero 2 W; RNode over USB and BLE; AutoInterface between real devices; the port lock against Mesh Hop; the terminal page for NomadNet; `X-Background` behaviour; long runs (memory growth over a day) |

### 7.3 Open unknowns

Collected in section 10. The ones that decide the plan: Python memory and start time on the deck, stamp time and its
effect on power, NomadNet in the terminal page, and the Heltec V4's host link (USB device name, BLE pairing).

### 7.4 Recommendation, with reasons

1. **Run P0 first.** It costs about one evening and settles D2, D13 and D15 with numbers.
2. **If it passes, architecture A** with the interface manager, status app before messenger: the status app needs no
   transmit and works over any interface, so it is useful while the owner chooses interfaces, RNode settings and an
   announce policy, and it exercises the whole service.
3. **Keep the deck a client.** For a steady LoRa presence, make the Pi 4B a home transport node (configuration only,
   g2) and let the decks reach it over Wi-Fi, as the manual recommends.
4. **Do not build C or D now.** C is the largest option and the one upstream objects to; D is experimental by its own
   account.
5. **Treat propagated messages on the Zero 2 W as open.** If P0 confirms minutes per stamp, the deck sends direct and
   opportunistic messages only, and propagation sync runs on the Pi 4B, Pi 5 or tablet.

## 8. Probes (dry-run on the PC 2026-11-07; Pi 4B run 2026-11-07, 8.2; deck run not yet done)

The Pi 4B first (faster), then the deck (zero7), with the owner present for the deck runs. Every run
has a timeout; nothing touches `~/.reticulum` (a separate configuration folder and `instance_name = probe`); no launcher
restart; cleanup at the end. The commands below are the draft commands as **corrected by the PC dry run of 2026-11-07**
(WSL Ubuntu 24.04, Python 3.12.3, loopback only, nothing on a board; results and the list of corrections in 8.1). They still
need a first run on the Pi 4B and the deck.

**P0-a, install (each board; measure: install time, disk use).**

```sh
python3 --version
dpkg -l python3-cryptography python3-serial python3-venv | tail -n +6
python3 -m venv --system-site-packages ~/rns-probe
time ~/rns-probe/bin/pip install --no-deps rns==1.6.0 lxmf==1.3.0
~/rns-probe/bin/rnsd --version
du -sh ~/rns-probe
~/rns-probe/bin/pip check
```

On the PC, `--no-deps` with system site packages needed nothing beyond the distribution's `python3-cryptography` (41.0.7)
and `python3-serial` (3.5); `pip check` was clean and `rnsd` ran. Pin with hashes (8.1).

**P0-b, import and idle daemon (measure: import time cold and warm, time to the "Started rnsd" line, VmRSS and CPU at
1 and 10 minutes).**

```sh
for i in 1 2; do /usr/bin/time -f "%e s, max RSS %M KB" ~/rns-probe/bin/python -c "import RNS, LXMF"; done
mkdir -p ~/rns-probe/cfg
cat > ~/rns-probe/cfg/config <<'EOF'
[reticulum]
  enable_transport = No
  share_instance = Yes
  instance_name = probe
[logging]
  loglevel = 4
[interfaces]
  [[Default Interface]]
    type = AutoInterface
    enabled = no
EOF
PYTHONUNBUFFERED=1 MALLOC_ARENA_MAX=2 ~/rns-probe/bin/rnsd --config ~/rns-probe/cfg -v > ~/rns-probe/rnsd.log 2>&1 &
P=$!   # not pgrep: with a `timeout` wrapper pgrep also returns the wrapper's PID
sleep 60; grep -E 'VmRSS|VmHWM' /proc/$P/status; ps -o pcpu,etimes -p $P
sleep 540; grep -E 'VmRSS|VmHWM' /proc/$P/status; ps -o pcpu,etimes -p $P; kill $P
```

`PYTHONUNBUFFERED=1` is needed: without it the log is block-buffered when redirected to a file and stays empty until the
process ends. The "Started rnsd version 1.6.0" line is logged at Notice level and appears with `-v`. A start-up time
is easier to take as the time until the abstract socket `@rns/<instance_name>/rpc` appears in `/proc/net/unix`.

**P0-c, Wi-Fi (measure: peers seen, LXMF delivery deck to PC and back, VmRSS with traffic).** Set `enabled = yes` and
`devices = wlan0` on the AutoInterface (then `rnstatus --config ~/rns-probe/cfg --attach "Default Interface"` applies it
without a restart, 8.1), run `rnsd` or NomadNet on the PC on the same Wi-Fi, check
`~/rns-probe/bin/rnstatus --config ~/rns-probe/cfg`, and exchange a message. The LXMF repository's example sender and
receiver are not in the wheel; the dry run used our own small two-way script written against the public API
(`LXMRouter`, `register_delivery_identity`, `register_delivery_callback`, `LXMessage`, `handle_outbound`), which also
works inside a process that attached to the shared instance. A client must use the same config directory as the
daemon (8.1).

**P0-d, stamps (Pi 4B first; deck only with the owner present; measure: seconds per cost, temperature, throttling
flags before and after).**

```sh
vcgencmd measure_temp; vcgencmd get_throttled
systemd-run --user --scope -p CPUQuota=200% nice -n 19 timeout 900 ~/rns-probe/bin/python - <<'EOF'
import os, time
import RNS
from RNS import Stamps
for cost in (8, 12, 16):
    t = time.time(); stamp, value = Stamps.generate_stamp(os.urandom(32), cost)
    print(cost, value, round(time.time() - t, 1), "s", flush=True)
EOF
vcgencmd measure_temp; vcgencmd get_throttled
```

**P0-e, RNode listen-only (the owner's Heltec LoRa32 V4, which already runs RNode firmware; if it needs an update, he
does it on the PC outside our software with `rnodeconf --update`; measure: device info, received bytes, RSSI, SNR, noise
floor and channel load every 5 minutes for 30 minutes, VmRSS and CPU).** On the deck: `~/rns-probe/bin/rnodeconf
/dev/serial/by-id/<the device> -i`, then add the section to the probe configuration and run `rnstatus --config ~/rns-probe/cfg --attach "RNode Probe"` (no daemon restart needed, 8.1). RNS needs all five
radio values even for receiving (4.2): the owner fills in a frequency, bandwidth, spreading factor and coding rate he is
allowed to use (ideally those of a Reticulum network he wants to hear) and a low TX power; nothing is sent, since
`outgoing = False`:

```ini
  [[RNode Probe]]
    type = RNodeInterface
    enabled = yes
    port = /dev/serial/by-id/<the device>
    frequency = <Hz, the owner's choice>
    bandwidth = <Hz, the owner's choice>
    txpower = <dBm, low>
    spreadingfactor = <7-12>
    codingrate = <5-8>
    outgoing = False
```

```sh
for i in $(seq 6); do ~/rns-probe/bin/rnstatus --config ~/rns-probe/cfg -j > ~/rns-probe/status-$i.json; sleep 300; done
```

**P0-f, NomadNet's TUI (measure: the terminal page's real grid, usability at that size, VmRSS).** In the launcher's
terminal page run `stty size` to read its grid on the deck (640x480) and on zero8 (480x320). Then
`~/rns-probe/bin/pip install nomadnet==1.5.0` (it pulls `urwid>=3.0.5` from PyPI) and run
`~/rns-probe/bin/nomadnet --rnsconfig ~/rns-probe/cfg` over SSH with the terminal resized to that grid (`stty cols <c>
rows <r>`); note whether conversations, the network list and a page are usable.

**P0-d note.** `Stamps.generate_stamp(os.urandom(32), cost)` is the right call and returns `(stamp, value)`; LXMF has
the same function in `LXMF.LXStamper`. On the PC it ran in one process at cost 8 and 12 and on every core at cost 16, so
`CPUQuota` matters.

**P0-g, cleanup.** Stop the probe daemon, `rm -rf ~/rns-probe ~/.nomadnetwork` (NomadNet's own folder, created by P0-f
only).

### 8.1 PC dry run, 2026-11-07 (PC numbers, not Zero 2 W numbers)

Host: WSL2 Ubuntu 24.04, Python 3.12.3, 12 logical cores, 7.8 GB RAM, loopback only, `AutoInterface` disabled, separate
config folders with their own `instance_name`, nothing under `~/.reticulum`, nothing sent outside 127.0.0.1. Raw logs
are in `C:\CLAUDE\zero7\reticulum-work\` (not in a repo).

**Versions and hashes (for D4).** PyPI has `rns` 1.6.0 and `lxmf` 1.3.0, and they are also the newest, so there is no
second version to test. The hashes of `pip download --no-deps` equal the PyPI sha256 values:

| File | sha256 |
|---|---|
| `rns-1.6.0-py3-none-any.whl` (681,475 B, uploaded 2026-11-07) | `fcf2d07eebafea7e56a94542bd74646e451b0f33bb3efb5f914bbbba38b39f36` |
| `lxmf-1.3.0-py3-none-any.whl` (74,764 B, uploaded 2026-11-07) | `bd26a8f7218519365b1a1940dd0fd1d3a4785aa4970d2de85807c3f846acd382` |
| `rns-1.6.0.tar.gz` | `aa99d550dacd0c895d81cac6d0740fbd8e89bdb3d452c555a51ef2305a6e5e4d` |
| `lxmf-1.3.0.tar.gz` | `5847534a7e69768425e02ff159096baf385ac99c0919da79e9ed0100e8f46814` |

Dependencies: `rns` requires `cryptography` and `pyserial`, `lxmf` requires `rns`. With `--no-deps` and system site
packages, `python3-cryptography` 41.0.7 and `python3-serial` 3.5 (Ubuntu apt) were enough (`pip check` clean). NomadNet
1.5.0 (installed only to import it) pulled `urwid` 5.1.0, `qrcode` 8.2 and `wcwidth` 0.9.4; `import nomadnet` works
(lower case) and it was not run further.

| Measure (PC) | Result |
|---|---|
| Install of both wheels (`--no-deps`, with network) | 1.3 s |
| Disk: whole venv / `RNS` / `LXMF` | 24 MB (includes pip and setuptools) / 6.7 MB / 0.86 MB; 39 MB with NomadNet |
| `import RNS, LXMF` (3 runs; no cold-cache run possible) | 0.10-0.11 s, max RSS 34 MB |
| `rnsd` start until its sockets exist | 0.12-0.16 s |
| `rnsd` idle VmRSS (no interface, share_instance on, 10 threads) | 39.6-41.6 MB at 2 s and 1 min; at 10 min 40.2 MB (default malloc) and 41.5 MB (`MALLOC_ARENA_MAX=2`); VmHWM equal to VmRSS |
| `rnsd` idle CPU | 0.1-0.3 % average over 10 min (0 s CPU time shown) |
| `MALLOC_ARENA_MAX=2` | no measurable gain on the PC (and that instance also served the attach tests) |
| Two LXMF nodes on loopback TCP (one process each, in-process `Reticulum`) | VmRSS 40.5-43.3 MB, VmHWM 43.1-45.2 MB after four messages |
| `RNS.Reticulum()` constructor | 0.03-0.06 s (imports not included) |
| LXMF delivery, both directions, opportunistic | 8-27 ms to the receiver, delivery proof back in 11-26 ms |
| LXMF delivery, both directions, direct (link set up included) | 5-24 ms to the receiver, proof 7-21 ms |
| Stamp `generate_stamp`, cost 8 | 0.26 s, 1 core |
| Stamp, cost 12 | 8.3 s, 1 core (one sample) |
| Stamp, cost 16 (3 samples) | 1.2, 4.5, 1.7 s wall; 12.5, 50.4, 18.8 CPU-seconds; 10.5-11.3 cores of 12 |

Stamp reading: cost 16 is about 12-50 CPU-seconds on this PC (about 27 on average over three samples), spread over all
cores. A Zero 2 W has 4 slower cores, so the estimate of "minutes of full CPU" (section 0, item 5) is not contradicted;
the real figure still comes from P0-d.

**Listen-only (`outgoing = False`), tested on the client end of a loopback TCP link.** The interface still receives
(the server's announce was heard and a path learned once the server announced again). Its own announces and path
requests are not sent: the peer never learned a path to the listener, and a node that started listening after the
peer's announce could not get a path at all (it cannot send a path request). **But an LXMF opportunistic message to a
destination with a known path was transmitted and delivered with the interface set to `outgoing = False`**; the direct
(link) message failed after about 57 s. So the flag stops what Reticulum sends by itself, not what an app asks it to
send. The bridge must therefore refuse send requests while an interface is receive-only (D8). Not tested: the same flag
on `RNodeInterface` (needs hardware).

**Run-time interface changes (answer: yes).** With `rnsd` running and `share_instance = Yes`: `rnstatus --config <dir>
--attach <name>` brought up an interface that was `enabled = no` in the file after the file was edited to `yes`;
`--detach` closed its listening port at once and `--attach` reopened it; a section added to the file while running
attached; changing a port and `--reload` rebound it. Each is logged at Notice level ("Interface X was attached"). The
same works from a Python client of the shared instance through `RNS.Reticulum.attach_interface(name)` and
`detach_interface(name)` (both returned `True`); `reload_interface(name)` exists. Interface sections are read from the
config file, so the bridge edits the file first. `SIGHUP` is not a reload: it killed `rnsd`. No restart is needed.

**What the shared instance exposes.** On Linux (default `shared_instance_type`): two abstract Unix sockets,
`@rns/<instance_name>` (the data socket, used by `LocalClientInterface`) and `@rns/<instance_name>/rpc` (control:
interface stats, path table, attach and detach); abstract sockets have no file permissions, so any local process can
reach the data socket. The RPC is authenticated by a key tied to the config folder (`storage/transport_identity`, 64
bytes). A second process that calls `RNS.Reticulum(configdir)` with the **same** config folder becomes a client at once
(0.035 s, own VmRSS 39 MB, `is_connected_to_shared_instance` True; `get_interface_stats()`, `get_path_table()`, announce
and an in-process LXMF router all worked). A client with a **different** config folder but the same `instance_name`
attached to the data socket but the RPC refused it ("digest sent was rejected") and it logged a database error: the
instance name alone is not enough. `rnstatus`, `rnpath` and `rnodeconf --help` run. For the bridge: every client process
still carries the full RNS stack (about 40 MB each), so native apps should talk to the bridge, not attach to the
instance; and the bridge's config folder is the credential. A TCP variant of the shared instance is listed in the manual
and was not tested here.

**Draft commands that were wrong or needed adjusting.**

1. `P=$(pgrep -f 'rnsd --config')` returned two or more PIDs (the `timeout` wrapper too, 1 MB RSS): use `$!`.
2. `rnsd -v > file` stayed empty (stdout block-buffered): set `PYTHONUNBUFFERED=1`; "Started rnsd" is a Notice line.
3. P0-c: the LXMF `docs/example_*.py` files are not in the wheel, and a client must share the daemon's config folder; our own script was used.
4. P0-e: no daemon restart is needed, use `rnstatus --attach`.
5. P0-a lacked `pip check`; P0-d's call `generate_stamp(os.urandom(32), cost)` was right.
6. `import NomadNet` is `import nomadnet`.
7. PC tooling only: `wsl.exe -- bash -c '...$VAR...'` from Windows loses `$VAR`; use script files.

### 8.2 Measured on the Pi 4B, 2026-11-07 / 08 (Pi 4B numbers, not Zero 2 W numbers)

Board: Raspberry Pi 4 Model B Rev 1.5 (Cortex-A72, 4 cores, 1.8 GHz, governor `ondemand`), 905 MB RAM, zram swap 904 MB
(0 used throughout), Debian 13 (trixie), kernel 6.18.50+rpt-rpi-v8, Python 3.13.5, `python3-cryptography` 43.0.0, Wi-Fi
(`wlan0`) only. Everything lived under `~/rns-probe` (venv with `--system-site-packages`); nothing under `~/.reticulum`,
no launcher file or unit touched; `AutoInterface` disabled throughout; no public testnet, no external host, nothing on a
radio. Raw logs and the scripts: `C:\CLAUDE\zero7\reticulum-work\pi4b\` (not in a repo). It is a 1 GB model: a fair
stand-in for memory (same aarch64 Python), not for CPU.

**Deviations from the draft commands.** (1) `python3-serial` was missing and was installed with `apt` (the only apt
change: `python3-serial` 3.5-2, 469 kB); `python3-venv` and `python3-cryptography` were already there. (2) The wheels were
`pip download`ed and their sha256 checked against 8.1 (both matched) before `pip install --no-deps --no-index` from the
local files. (3) `/usr/bin/time` and `bc` are not on the image, so timings use Python's clock. (4) A first idle run
measured the `timeout` wrapper's PID instead of the daemon (the `$!` advice of 8.1 holds only without `timeout`); it
was repeated without the wrapper. (5) `pip check` prints unrelated complaints about system packages (`types-*`,
`apt-listchanges`); none about `rns` or `lxmf`. (6) P0-d stopped at the temperature rule (below), so cost 16 and the 100 %
run have no result. (7) `CPUQuota` worked from `systemd-run --user --scope` (user systemd running, the `cpu` controller
delegated).

| Measure | Pi 4B (this section) | PC (8.1, for reference) |
|---|---|---|
| Install of both wheels, local files, `--no-deps` | 3.4 s (download 1.8 s) | 1.3 s with network |
| Disk: venv / `RNS` / `LXMF` | 22 MB / 6.8 MB / 0.86 MB | 24 / 6.7 / 0.86 MB |
| `import RNS, LXMF`, cold (after `drop_caches`) | 0.95 s wall, 0.55 s CPU | no cold run |
| `import RNS, LXMF`, warm (3 runs) | 0.49-0.50 s wall (0.49 CPU), max RSS 36.5 MB | 0.10-0.11 s, 34 MB |
| `rnsd` start until its sockets exist | 0.45 s (both runs); the "Started rnsd" line has the same second as "Configuration loaded" | 0.12-0.16 s |
| `rnsd` idle VmRSS, 10 threads | 38.1 MB at 1 min, 38.3 MB at 10 min (VmHWM equal); with `MALLOC_ARENA_MAX=2` 38.1 / 38.3 MB | 39.6-41.6 MB |
| `MALLOC_ARENA_MAX=2` | no difference | none |
| `rnsd` idle CPU | 0.1 % at 10 min (1 CPU-second in 10 min); 0.8-1.0 % in the first minute | 0.1-0.3 % |
| `rnsd` with one TCP peer, four LXMF messages | VmRSS 38.7 MB; the LXMF client (shared instance) 39.1 MB | 40.5-43.3 MB |
| `rnstatus --attach / --detach / --reload` | work identically (port opens, closes, reopens, rebinds; Notice lines logged) | same |
| Stamp cost 8 (1 process) | 3.5 s, 1 core | 0.26 s |
| Stamp cost 12 (1 sample) | 21.7 s, 1 core | 8.3 s (one sample) |
| Stamp cost 16 (`CPUQuota=200%`, `nice 19`) | **not finished after about 8.5 min**; stopped by the temperature rule at 80.3 C | 1.2-4.5 s wall, 12.5-50.4 CPU-s on 12 cores |
| Temperature, idle daemons | 46.7 C before, 49.6 C after 10 min | not recorded |
| `vcgencmd get_throttled` | 0x0 at every sample (177 samples, 3 s apart), also at 80.3 C | not applicable |

**P0-c over the LAN (controlled).** `rnsd` on the Pi with a `TCPServerInterface` bound to `192.168.68.167` only, port 4242,
`outgoing` at its default; an in-process LXMF node on the PC (WSL2, `TCPClientInterface` to that address, the same pinned
wheels re-created in `~/rns-probe-pc`; WSL reached the Pi without a workaround); the Pi's LXMF node attached to the
shared instance of its `rnsd`. Both announced; each heard the other's announce within about a second of its own start.
Both directions, both methods delivered: the sender's delivery proof arrived after **23-26 ms (opportunistic)** and
**47-48 ms (direct, link setup included)** in both directions. One-way latency cannot be read from the message timestamps
(the two clocks differ by an unknown amount under 1 s). The Pi's TCP interface counted 1.4 kB received and 1.3 kB sent for
the whole exchange. Only the PC (192.168.68.113) connected, for 100 s. At the end the server and node were stopped and
`ss -ltn` showed port 4242 closed.

**P0-d, stamps, in full.** Both runs used `systemd-run --user --scope -p CPUQuota=200% nice -n 19 timeout 900` with a
sampler that stops on more than 80 C or any current throttle bit. Cost 8: 3.5 s, one core; cost 12: 21.7 s, one core.
During the cost-16 run the temperature rose from 49 C to 70 C within 4 minutes and reached 80.3 C at about 8.5 minutes
(no fan; the case and heatsink were not checked), where the sampler stopped it. The firmware never throttled (`0x0`
throughout). The stop killed the parent only: its four worker processes ran on for about 40 s until I killed them by PID,
and the temperature peaked at 81.8 C in that time. **Lesson for the bridge: a stamp generator is a process group; stop it
as a group (a cgroup scope, `systemctl --user stop`), not by one PID.** The cost-16 run has no completion time: one
unfinished sample says only that on a Pi 4B at a 200 % quota it took more than about 8.5 minutes (the PC needed 12-50
CPU-s, the cost-8 ratio Pi 4B to PC is 13.6, so 160-700 CPU-s were expected; a long tail is possible). The 100 % quota
run was not made (stop and report).

**What this implies for the Zero 2 W (estimate, labelled as such).** Memory carries over best: the same 64-bit Python and
RNS should idle near 38-40 MB with 10 threads, on a board with 512 MB (I). For CPU I have no benchmark: a Zero 2 W's four
Cortex-A53 cores at 1.0 GHz are, by general knowledge of the two cores and their clocks, probably 2.5-3.5 times slower
than the Pi 4B's A72 cores at 1.8 GHz (I, not measured, to be measured on the deck). On that ratio: import 1.2-1.7 s warm
and a few seconds cold, `rnsd` start about 1.5 s, cost 8 about 10 s, cost 12 about 1 minute, and cost 16 more than 20
minutes at 2 cores, with a heat problem as well. **So a cost-16 stamp is not a task for the deck: only the Pi 4B (or the
PC) should generate propagation stamps, or the deck should not send through propagation nodes (I).** Plain messages
(opportunistic, direct) cost milliseconds of CPU on the Pi 4B, so they should be cheap on the deck too. `outgoing = False`
on a radio (P0-e) and NomadNet's page (P0-f) were not run, as agreed.

## 9. Risks, ranked

1. **Zero 2 W resources.** Python's memory is probably about 40 MB (Pi 4B, 8.2); a cost-16 stamp did not finish in 8.5 minutes on a Pi 4B at 2 cores and heated it to 80 C, so on the deck it is probably out of reach (I), on a board where no
   heavy Python load has been run yet (the owner's earlier resets were accidents, not load). Mitigation: P0; `Nice`, `CPUQuota`; no propagation sync on the deck if
   slow; the Pi 4B as the home node.
2. **Upstream churn and governance.** Stamp enforcement in 1.6.2 cuts off nodes that did not update; releases are
   frequent; no issue tracker; one maintainer who stepped back from public interaction (V). Mitigation: a release of our
   package per upstream release while 1.6.x settles; a documented way to run a newer upstream version.
3. **Licence and provenance.** A non-standard licence with use conditions; upstream's hostility to machine-generated
   derivatives; our AI-assisted workflow (V, I). Mitigation: D16, D17, a thin disclosed bridge, no reimplementation.
4. **Port conflicts** with Mesh Hop and Serial Console, since RNS takes no lock (V). Mitigation: 4.4.
5. **Privacy.** Announces broadcast a name and presence; telemetry can reveal location; AutoInterface on public Wi-Fi
   shows the deck on the LAN (I). Mitigation: the rules of 5.3.
6. **A precedent.** An exception to "no Python for long-running parts" may invite others (I). Mitigation: scope the
   exception to this service by name.

User responsibility (not a risk we mitigate): the radio settings, including airtime limits, are the user's, and so is
following the rules where they transmit (4.2).

## 10. Not verified

- Memory, CPU and start time of RNS (and of the bridge) on the Zero 2 W; the 1.6.0 memory claims on our boards. (PC numbers
  exist, 8.1, and Pi 4B numbers, 8.2: about 38-40 MB idle, 0.15 s and 0.45 s start; they say nothing certain about the Zero 2 W.)
- Stamp time on the Zero 2 W (my 2-5 minutes for cost 16 is an estimate that the Pi 4B run, 8.2, contradicts: no completion in 8.5 minutes; the completion time of a cost-16 stamp is still unmeasured on every board, and so are costs 10 and 14); SHA-256 throughput per core; whether the
  BCM2710A1 lacks the ARMv8 crypto extensions (widely reported, not checked).
- The default destination-stamp cost from RNS 1.6.1 and its one-time CPU.
- How often LXMF users (Sideband, MeshChatX, Columba) set an inbound stamp cost, which makes direct messages cost CPU
  too.
- NomadNet's usability in the launcher's terminal page; the page's real column count at 640x480 and 480x320.
- RNode over BLE on our boards: pairing without a display on the owner's RNode, `bleak` 0.22.3 with RNS 1.6.0.
- RNS's retry behaviour when an RNode port is busy; whether RNode firmware sends anything before it is configured.
- Heltec LoRa32 V4: the RNode firmware version on the owner's board; whether it enumerates as native USB CDC or
  through a USB-UART chip (and so its `/dev/serial/by-id` name); BLE pairing with BlueZ on our boards; whether its TCP
  host link over Wi-Fi works; how the firmware maps a requested `txpower` through the PA (28 dBm maximum) and whether
  the read-back value matches what RNS sent.
- Which other boards of the owner's can run RNode (community forks not checked); the Wio-SX1262 antenna connector.
- What the undocumented `WeaveInterface` is. (Verified on the PC, 8.1, and on the Pi 4B for attach, detach and reload, 8.2: run-time attach, detach and reload of interfaces; the
  shared-instance sockets and attach rules; stamp costs 8, 12, 16 on the PC. Still open: the same on the Zero 2 W, and
  `outgoing = False` on an `RNodeInterface`.)
- Whether the RNode firmware clamps a `txpower` above the radio's maximum (RNS would then see a mismatch and take the
  interface offline, V `validateRadioState`), and whether it accepts spreading factors 5 and 6, which RNS 1.6.0 allows
  but the manual does not list.
- The current reachability of the old testnet hosts (rmap.world and directory.rns.recipes not read).
- `i2pd` resource use on a Zero 2 W.
- Python version, `venv` behaviour and journald persistence on our board images (Debian 13 suggests Python 3.13).
- Whether Settings > Apps handles a package without a home tile.
- Interoperability claims of Reticulum-Go, Reticulum-rs and microReticulum (not tested); LXST's licence text.
- Licences of `cryptography`, `pyserial` and `bleak` (not read; Debian dependencies, not bundled).
- MeshChat and MeshChatX resource use.

## 11. Questions for the owner

D1-D19 are accepted (section 1). The questions below are the details the owner still has to supply for P0 or later phases; where a question restates a decision, the accepted recommendation is the default.

1. **The RNode for P0 (D11, context only):** the Heltec V4: which RNode firmware version does it run (`rnodeconf -i`),
   and which values (frequency, bandwidth, spreading factor, coding rate) will you enter for the probe?
2. **Who would you talk to:** people with Sideband, Columba or MeshChatX, a local Reticulum group, or only your own
   devices (then a private network with an IFAC passphrase)?
3. **RNode settings page (D10):** empty radio fields that the user must fill, and the airtime-limit fields always
   visible and empty by default: agreed?
4. **Python service (D2):** acceptable if P0 meets a budget? Which limits (my proposal: 60 MB, 3 % idle CPU, 20 s)?
5. **Receive in the background (D6):** only while a Reticulum app is open, or a "keep running" switch?
6. **A home node:** should the Pi 4B become an always-on transport node with an RNode?
7. **Internet:** never, or only entrypoints you pick (D9)?
8. **P0:** may it run on the Pi 4B and the deck, with you present for the deck, using the commands of section 8?
9. **AI data use (D17):** can you confirm the account's setting for model training?

## Sources

All read on 2026-11-07 unless noted.

- Reticulum (RNS), repository mirror, `master` @ `cb1260891d` (2026-11-07): https://github.com/markqvist/Reticulum ;
  release RNS 1.6.0 (2026-11-07, notes quoted in 0, 3.3, 3.6): https://github.com/markqvist/Reticulum/releases/tag/1.6.0 ;
  `LICENSE`: https://github.com/markqvist/Reticulum/blob/master/LICENSE ; `MIRROR.md` (added 2025-12-27):
  https://github.com/markqvist/Reticulum/blob/master/MIRROR.md ; `setup.py`, `RNS/_version.py` (1.6.0),
  `RNS/Reticulum.py` (interface loader), `RNS/Interfaces/RNodeInterface.py` (option parsing lines 150-169, validation
  lines 308-349, `validateRadioState`), `RNS/Utilities/rnodeconf.py` (options, product and model tables, Heltec
  installers), `RNS/Stamps.py`, `RNS/Storage.py`, `RNS/__init__.py` at the same commit.
- Reticulum manual (sources at the same commit; web edition https://reticulum.network/manual/): Getting Started Fast
  (`gettingstartedfast.rst`), Understanding Reticulum (`understanding.rst`), Using Reticulum on Your System
  (`using.rst`), Configuring Interfaces (`interfaces.rst`), Communications Hardware (`hardware.rst`), Programs Using
  Reticulum (`software.rst`), Reticulum License (`license.rst`), Brandolini's Reference (`brandolinis.rst`,
  https://reticulum.network/manual/brandolinis.html).
- LXMF, `master` @ `a0793f6677`, release 1.3.0 (2026-11-07): https://github.com/markqvist/LXMF (`README.md`, `LICENSE`,
  `setup.py`, `LXMF/LXMF.py`, `LXMF/LXMRouter.py`, `LXMF/LXMessage.py`).
- NomadNet, `master` @ `9dd3d861ea`, release 1.5.0 (2026-11-07): https://github.com/markqvist/NomadNet (`README.md`,
  `LICENSE`, `setup.py`, `nomadnet/ui/textui/Guide.py`, `nomadnet/Node.py`).
- Sideband, `main` @ `ef572b58e8`, release 2.2.0 (2026-10-01): https://github.com/markqvist/Sideband (`README.md`,
  `LICENSE`, `sbapp/sideband/sense.py`, read for the telemetry format only).
- RNode Firmware, `master` @ `56775c51b6`, release 1.86 (2026-04-24): https://github.com/markqvist/RNode_Firmware
  (`README.md`, `LICENSE`, `Boards.h` (`BOARD_HELTEC32_V4`), `Bluetooth.h`, `Config.h`, `RNode_Firmware.ino`); release
  1.86 assets (including `rnode_firmware_heltec32v4pa.zip`): https://github.com/markqvist/RNode_Firmware/releases/tag/1.86
- LXST 0.5.3 (metadata only): https://github.com/markqvist/LXST
- MeshChat 2.4.0 (2026-07-06), MIT: https://github.com/liamcottle/reticulum-meshchat ; MeshChatX (0BSD + MIT):
  https://github.com/Quad4-Software/MeshChatX
- Reticulum-Go v1.4.2 (2026-11-01): https://github.com/Quad4-Software/Reticulum-Go ; Ren Browser:
  https://github.com/Quad4-Software/Ren-Browser
- Reticulum-rs (MIT): https://github.com/BeechatNetworkSystemsLtd/Reticulum-rs
- microReticulum 0.5.0 (Apache-2.0): https://github.com/attermann/microReticulum
- reticulum-kt (MPL-2.0): https://github.com/torlando-tech/reticulum-kt ; Columba v2.2.6 (MPL-2.0):
  https://github.com/torlando-tech/columba
- Prns (named in the manual's Brandolini's Reference): https://github.com/KenAKAFrosty/Prns
- Micron Parser JS 1.1.0 (Unlicense): https://github.com/RFnexus/micron-parser-js
- PyPI JSON: https://pypi.org/pypi/rns/json , https://pypi.org/pypi/rnspure/json , https://pypi.org/pypi/lxmf/json ,
  https://pypi.org/pypi/nomadnet/json , https://pypi.org/pypi/sbapp/json
- Debian package versions (madison): https://qa.debian.org/madison.php (queries for `python3-rns`, `rns`, `python3-lxmf`,
  `python3-cryptography`, `python3-serial`, `python3-bleak`, `python3-urwid`, `python3-qrcode`, `python3-msgpack`,
  `python3-dbus-fast`, `pipx`; suites bookworm, trixie, forky, sid; arm64 and all).
- GitHub discussions (measurements): #684 "Propagation Node got all your RAM?" (2025-01-22, memory and index speed):
  https://github.com/markqvist/Reticulum/discussions/684 ; #262 "32 bit raspberry pi limitations" (2023):
  https://github.com/markqvist/Reticulum/discussions/262 ; #963 "Power Usage of devices" (2025-10/11):
  https://github.com/markqvist/Reticulum/discussions/963
- Our code and documents: apps `origin/main` @ `67e546b` (`docs/APP-GUIDELINES.md`, `README.md`,
  `apps/mesh-hop/app.json`, `apps/mesh-hop/cli/requirements.txt`, `apps/mesh-hop/cli/vendor.py`,
  `apps/mesh-hop/src/core/serial_transport.cpp`, `apps/deck-atlas/README.md`, `tools/make_registry.py`); launcher
  `github/main` @ `28817a50` (`docs/dev/reports/054-offline-maps-wardriving-study.md`,
  `docs/dev/reports/056-mmwave-sensor-study.md`, `docs/OPEN_SOURCE_COMPONENTS.md`, `docs/dev/decisions.md`, `docs/dev/ref/README.md`,
  `projects/APPLaunch/pizero2w/install.sh`).
