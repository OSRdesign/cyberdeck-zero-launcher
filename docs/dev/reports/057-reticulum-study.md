# 057 - Reticulum on the cyberdeck (study)

Date 2026-11-07. Research and design only: no product code, no app, no board touched, nothing installed or run on a
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
  `RNS/Interfaces/RNodeInterface.py`, `RNS/Stamps.py`, `RNS/Storage.py`, `RNS/__init__.py`; LXMF 1.3.0 (`README.md`,
  `LICENSE`, `setup.py`, `LXMF.py`, `LXMRouter.py`, `LXMessage.py`); NomadNet 1.5.0 (`README.md`, `LICENSE`,
  `setup.py`, `Guide.py`, `Node.py`); Sideband 2.2.0 (`README.md`, `LICENSE`, `sbapp/sideband/sense.py`); RNode
  Firmware 1.86 (`README.md`, `LICENSE`, `Boards.h`, `Bluetooth.h`); MeshChat 2.4.0 and MeshChatX; Reticulum-Go,
  Reticulum-rs, microReticulum, reticulum-kt, Columba and Prns (repository metadata, READMEs, licence files); PyPI JSON
  and Debian `madison` package indexes; three GitHub discussions with measurements; ERC Recommendation 70-03 (amended
  2026-10-09).

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
accepts a long-running Python process, which today's app guidelines rule out.** A native C++ reimplementation of the
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
   (V). Not known: resident memory, start time, and the cost of stamps. A message sent through a propagation node needs
   a cost-16 stamp, on average 65,536 SHA-256 passes over a 768 KB block, spread over all cores (V, source); my estimate
   on a Zero 2 W is a few minutes of full CPU per message (I): a heavy load for a Zero 2 W, which has not been tried (owner,
   2026-11-07: every earlier reset of the board was an accidental reboot or unplugging, not load).
6. **How an app talks to it (V).** Programs share one *shared instance* through a local socket, but each client still
   runs the full RNS protocol (crypto, links, announces) in its own process; the side channel is Python's
   `multiprocessing.connection` with msgpack. A native app therefore either reimplements RNS or talks to a Python
   helper. Recommended: one Python **bridge** process that is the shared instance, runs the LXMF router and offers a
   JSON-lines socket to our native apps (5.3).
7. **Transports (V).** RNode over USB serial is the main radio path; RNode over Bluetooth LE works on Linux through
   `bleak`; Wi-Fi and Ethernet through `AutoInterface` (zero configuration, IPv6 link-local); TCP or `BackboneInterface`
   to Internet entrypoints found on `directory.rns.recipes` or `rmap.world`; I2P needs an `i2pd` daemon. **Listen-only
   is one setting:** `outgoing = False` on an interface (4.1).
8. **Radio rules (V).** EU and France (ERC/REC 70-03 Annex 1; France implements every 863-870 MHz sub-band): 865-868
   MHz and 868.0-868.6 MHz allow 25 mW e.r.p. at 1 % duty cycle (or LBT+AFA), 869.4-869.65 MHz allows 500 mW e.r.p. at
   10 %. RNS enforces airtime with `airtime_limit_long` (rolling 60 minutes) and `airtime_limit_short` (about 15 s).
   1 % is 36 s on air per hour, about 74 announces at SF8/125 kHz (I, computed in 4.2).
9. **The owner's hardware (V, U).** The spare Seeed XIAO ESP32S3 + Wio-SX1262 ("XIAO S3 WIO", Mesh Hop's board B) is
   on the RNode firmware's supported list, with BLE. The XIAO nRF52840 (board A) and the T1000-E are not on the official
   list (U for community forks). One board runs one firmware: a board flashed as an RNode is no longer a MeshCore node.
10. **Conflict with Mesh Hop (V).** RNS opens an RNode's serial port without `flock` or `TIOCEXCL`, so Mesh Hop's lock
    check would not see that the port is in use. A port rule is needed (4.4).
11. **Licences (V).** RNS and LXMF: the "Reticulum License" = the MIT text plus two use conditions (no systems designed
    to harm humans; no use in AI, machine-learning or language-model training datasets). NomadNet and the RNode
    firmware: GPL-3.0. Sideband: CC BY-NC-SA 4.0. MeshChat: MIT. RNS and LXMF may be shipped in an app package with
    their notice; nothing enters the launcher (6).
12. **Apps (I).** A status and announce monitor (c), a messenger (a), a NomadNet browser (b), position sharing with Deck
    Atlas (e) and file transfer (f) all share the bridge. Wrapping NomadNet's terminal UI (d) is cheap but likely
    awkward on our terminal page (U); wrapping Sideband is not practical (Kivy GUI, 68.6 MB wheel, CC BY-NC-SA) (5).

**Recommendation (mine, not a rule).** Run probe P0 on the Pi 4B and then the deck, with the XIAO S3 WIO flashed as an
RNode and receiving only; decide on the numbers. If they pass: P1 a `reticulum` service package and a listen-first
status app, P2 the messenger, P3 the NomadNet browser, P4 positions with Deck Atlas. **MVP (P0 + P1 + docs) 3.75-5 ws
(1.5-2 M tokens); with the messenger 7.5-10.25 ws; everything listed 13-18.5 ws (5.2-7.4 M tokens)** (7).

**Top three risks.** (1) Python RNS memory and CPU, and stamp proof-of-work, on the Zero 2 W (no load problem has been seen on the deck; owner: the earlier resets were accidental unplugging or reboots);
(2) upstream churn (stamp enforcement in 1.6.2) with a single maintainer who takes no issues; (3) licence and
provenance: a non-standard licence with use conditions, and upstream's stance on AI-generated reimplementations, in a
project whose code is written with AI assistance (9).

**Owner questions:** section 11.

## 1. Decisions for the owner

All open. The recommendation column is mine; the choice is the owner's (design case by case).

| # | Decision | Options | Recommendation (not a rule) |
|---|---|---|---|
| D1 | Start at all | run probe P0 now; park the topic | run P0 (one evening: about 1-1.5 h of the owner's time, section 8) before any build |
| D2 | A long-running Python process | keep guideline 6 ("no Python for long-running parts"): no Reticulum; an exception for this one service; an exception for any app | an exception for this one service only, and only if P0 meets a budget the owner sets. My proposal: on the deck, VmRSS at most 60 MB idle, idle CPU at most 3 %, ready within 20 s of start |
| D3 | Architecture | A: our Python bridge is the shared instance, native apps talk JSON to it; B: upstream `rnsd` as the service, the bridge as one of its clients; C: a native C++ RNS and LXMF in our apps; D: an alternative implementation (Go, Rust, C++); E: only wrap NomadNet's terminal UI | A (it behaves as B by itself when the user already runs an `rnsd`: 3.5); not C; D only if P0 fails and a later look finds a mature one; E only as an optional stopgap (D15) |
| D4 | Packaging | pinned wheels vendored in our `.deb` (Mesh Hop's precedent); the user runs `pip`/`pipx`; a `.deb` that runs `pip` at install | vendored and pinned (`rns`, `lxmf`), with Debian's `python3-cryptography` and `python3-serial` as `depends` and `python3-bleak` as `recommends`; a new package with each upstream release while 1.6.x settles |
| D5 | Names | package and unit `reticulum`; `rns-bridge`; `deck-rns` | the owner's choice; `reticulum` reads well in Settings > Apps but must not be mistaken for an upstream package (the README says it is ours) |
| D6 | Service lifetime | always on from boot; started by an app and stopped with the last one; a switch | started on demand by our apps, plus a "Keep running (receive while no app is open)" switch in the status app, off by default |
| D7 | Transport (routing for other nodes) | off; on | off on every board of ours (the manual reserves it for stationary, always-on systems: 2.2) |
| D8 | What may transmit | everything the config enables; nothing until the owner switches it on | nothing transmits until the owner switches it on, per interface (`outgoing = False` as the default); no announce, path request or message without the owner's action |
| D9 | Default interfaces | upstream default (AutoInterface on every network); AutoInterface only on a named device after opt-in; an Internet entrypoint preconfigured | no interface on at first start; AutoInterface on `wlan0` or a chosen USB adapter after opt-in; TCP entrypoints only when typed or picked by the owner |
| D10 | LoRa channel plan | 865-868 MHz (25 mW, 1 %); 869.4-869.65 MHz (500 mW, 10 %); whatever local Reticulum users run | the local plan if one exists (U); otherwise 867.2 MHz, 125 kHz, SF8, CR 4/5 (the manual's example) at no more than 14 dBm e.r.p. with `airtime_limit_long = 1`; stay off 869.525 MHz, where MeshCore (Mesh Hop's EU preset) and Meshtastic users sit |
| D11 | Which board becomes the RNode | XIAO S3 WIO; one of the "other LoRa boards"; buy one | the XIAO S3 WIO (supported, BLE); keep the XIAO nRF52840 on MeshCore for Mesh Hop |
| D12 | Identity and announces | one LXMF identity per deck; one per app; announce policy | one identity per deck in the service's data folder (mode 0600); export only on the owner's action; announce only after the owner has set a display name and pressed Announce; automatic re-announce off, at most every 6 h when on |
| D13 | Propagation node | none; picked from heard announces; sync policy | none by default; the owner picks one; sync by hand, or on a timer only while on Wi-Fi; the app warns that sending through it costs minutes of CPU on a Zero 2 W (if P0 confirms) |
| D14 | First app | status and announce monitor; messenger | the status monitor: listen-first, no transmit needed, shows the radio facts |
| D15 | NomadNet terminal wrapper | build as a stopgap; skip | only if P0 shows it usable in the launcher's terminal page (U) |
| D16 | Provenance rule for this feature | as for other apps; strict | strict: workers use only the public Python API of RNS and LXMF and documented formats; they never read upstream source in order to write protocol, crypto or markup-parser code; the README states AI assistance (6.3) |
| D17 | AI account data use | check; ignore | check that the sessions used for this work are not used for model training (6.2: the licence's AI-training condition) |
| D18 | Mesh Hop guard | none; skip ports held by any other process | a small Mesh Hop task: treat a port held by any other process as busy (`/proc` check), and name an RNode as "used by Reticulum" (4.4) |

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

### 4.1 Interfaces

| Interface | Needs | Cost | What the owner has | Notes (V unless marked) |
|---|---|---|---|---|
| `RNodeInterface`, USB serial | an RNode-flashed LoRa board, the `dialout` group, `pyserial` | a board and an 868 MHz antenna | XIAO S3 WIO (supported) | RNS needs RNode firmware 1.52 or later (`REQUIRED_FW_VER`); airtime limits; `outgoing = False` receives only |
| `RNodeInterface`, BLE (`port = ble://`, `ble://<name>`, `ble://<MAC>`) | an RNode with BLE, paired with the host first; `bleak` | `python3-bleak` | the XIAO S3 has BLE (`Boards.h`: `HAS_BLE true`) | uses the Nordic UART service; the pairing flow on a board without a display is U; the Zero 2 W's combined Wi-Fi/Bluetooth chip also serves the Bluetooth keyboard (guidelines 6) |
| `RNodeInterface`, TCP (`port = tcp://<host>`) | an RNode with Wi-Fi | none | ESP32 boards (which ones: U) | the radio can sit elsewhere, for example on a mast |
| Bluetooth Classic (ESP32 boards with `HAS_BLUETOOTH`) | an RFCOMM serial port | none | older ESP32 boards | U on our images |
| `RNodeMultiInterface` | RNodes with several transceivers | | none | |
| `AutoInterface` | Wi-Fi or Ethernet on the same layer-2 network, link-local IPv6, UDP 29716 and 42671 | free | built-in Wi-Fi (2.4 GHz only on the Zero 2 W, V report 054), USB Wi-Fi adapters | zero configuration; access points with client isolation block it; `devices = wlan0` pins it; `group_id` separates networks |
| `TCPClientInterface` / `BackboneInterface` | Internet and an entrypoint `host:port` | free | any network | "TCP connections reveal the IP address of both your instance and the server"; Backbone is Linux and Android only |
| `TCPServerInterface` | a public IP | | not for a deck | |
| `UDPInterface` | IP broadcast | free | a LAN | AutoInterface is the better choice on Wi-Fi (manual) |
| `I2PInterface` | an I2P daemon (`i2pd`) running beside RNS | the daemon's RAM and CPU (U) | | hides both IP addresses; heavy for a Zero 2 W (I) |
| `KISSInterface`, `AX25KISSInterface` | a packet-radio TNC and radio | a licence for amateur bands | none known | `id_callsign`; the rules for encrypted traffic on amateur bands in France are U and belong to the LoRa APRS study |
| `SerialInterface` | a transparent serial link | a cable | | lab use |
| `PipeInterface` | any program on stdin/stdout | | | custom media |

### 4.2 EU and France rules at 868 MHz

ERC/REC 70-03 Annex 1 (non-specific short range devices), amended 2026-10-09; Appendix 1 marks France "Y"
(implemented) for every sub-band below (V):

| Sub-band | Power | Spectrum access | For Reticulum |
|---|---|---|---|
| h1.3 863-865 MHz | 25 mW e.r.p. | 0.1 % duty cycle or LBT+AFA | poor: 3.6 s per hour |
| h1.4 865-868 MHz | 25 mW e.r.p. | 1 % duty cycle or LBT+AFA | good: the manual's example is 867.2 MHz |
| h1.5 868.0-868.6 MHz | 25 mW e.r.p. | 1 % duty cycle or LBT+AFA | LoRaWAN's default channels sit here (I) |
| h1.6 868.7-869.2 MHz | 25 mW e.r.p. | 0.1 % or LBT+AFA | poor |
| h1.7 869.4-869.65 MHz | 500 mW e.r.p. | 10 % or LBT+AFA | the range band; MeshCore's EU preset (869.525 MHz, Mesh Hop V report 007) and Meshtastic's EU default (I) use it |
| h1.8 869.7-870 MHz | 5 mW e.r.p. | no requirement | short range |
| h1.9 869.7-870 MHz | 25 mW e.r.p. | 1 % or LBT+AFA | only 300 kHz wide |

870-874.4 MHz and 915-919.4 MHz are marked "N" for France in the same table (V).

Notes:
- RNS's `airtime_limit_long` is enforced "over a rolling 60 minute window" and `airtime_limit_short` in "a window of
  approximately 15 seconds", both in percent (V manual), which matches an hourly duty cycle (the EN 300 220 definition
  was not read: U). Set `airtime_limit_long` to the sub-band's figure (1 or 10).
- e.r.p. includes the antenna: 25 mW is 14 dBm e.r.p.; RNS's `txpower` is the radio's output (I), so `txpower` at most
  14 dBm minus (antenna gain in dBi minus 2.15) for 25 mW; at most 27 dBm e.r.p. in h1.7, where the SX1262's +22 dBm
  maximum stays inside the limit with a small antenna (I).
- The RNode's "interference avoidance" (`rnodeconf -x`) is not a certified LBT+AFA scheme (U), so the duty-cycle route
  applies.
- The manual: "It is your responsibility to be aware of any relevant regulation for your location" (V).

Airtime of typical packets (I: Semtech's LoRa formula, 8-symbol preamble, explicit header, CRC on; the RNode's own
preamble may be longer):

| Setting | Bit rate | Path request, 51 B | Announce, 167 B | 500 B packet | Link set-up (83+115+99 B) | Announces per hour at 1 % |
|---|---|---|---|---|---|---|
| SF8, 125 kHz, CR 4/5 | 3.1 kbit/s | 0.19 s | 0.48 s | 1.33 s | about 0.9 s | about 74 |
| SF10, 125 kHz | 0.98 kbit/s | 0.62 s | 1.56 s | 4.30 s | about 3 s | about 23 |
| SF7, 250 kHz | 10.9 kbit/s | 0.05 s | 0.14 s | 0.38 s | about 0.25 s | about 260 |

A 10 KB file over LoRa at SF8 is about 30 s of airtime before overheads (I), almost the whole hour's budget at 1 %.

Antennas (I): an antenna tuned for 868 MHz on the RNode; never transmit without one; a higher-gain antenna needs a lower
`txpower` for the same e.r.p. The Wio-SX1262 module's antenna connector type is U.

### 4.3 What the owner's hardware can do

| Board | RNode? | Notes |
|---|---|---|
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
4. **Say who has it.** The status app shows "RNode on ttyACM0 (XIAO S3), used by Reticulum"; Mesh Hop's node list says
   "used by Reticulum". An RNode answers the KISS detect command (`CMD_DETECT 0x08`, `DETECT_REQ 0x73`, V), so Serial
   Console could identify one, but only on request, since it writes to the port.
5. **BLE.** One central per RNode; scans short and only from the status app, as Mesh Hop does.

## 5. App concepts and architecture (question 4)

### 5.1 Concepts

| # | Concept | What it needs | Size (ws) | Small screens, guidelines |
|---|---|---|---|---|
| a | **Native LVGL messenger** | bridge: LXMF send/receive with delivery states (outbound, sent, delivered, failed), contacts from heard announces or typed/pasted addresses, propagation node choice and sync, identity export; UI: contacts, conversations, compose with the shared on-screen keyboard rule, a Settings page | L, 3.5-5 | Mesh Hop's chat pages are the house pattern (V); 640x480 first, 480x320 and 320x480 by decision; launcher toasts and beeps for new messages through `launcher.sock` (V guidelines 3) |
| b | **NomadNet page browser** | bridge: a link to the node's `nomadnetwork.node` destination and a request for `/page/index.mu` (V `Node.py`), responses as micron text; a micron renderer: formatting, 3-hex-digit colours, alignment, headings and sections, dividers, links, anchors, tables, input fields, checkboxes, radio groups, request links, partials (with refresh), literals, comments, page colour and cache headers, images in WebP only (V `Guide.py` sections) | M-L, 2.5-4 | micron is written for terminal columns; on 640x480 at a readable size about 50-60 columns (I); stb_image (already in the repo) has no WebP decoder (I), so images start as their alt text |
| c | **Interface, network status and announce monitor** | bridge: interface statistics (for RNodes RSSI, SNR, noise floor, airtime, channel load, battery), heard announces with hops and interface, paths, the per-interface transmit switch | M, 1.5-2 (part of P1) | read-only and listen-first; fits every size; the natural first app |
| d | **Wrapping Sideband or the NomadNet TUI** | NomadNet: kind B terminal app, the upstream GPL program unmodified with `urwid>=3.0.5` bundled (Debian's is 2.6.16); Sideband: Kivy needs a display server or an SDL/KMS backend, a 68.6 MB wheel plus numpy and pillow, CC BY-NC-SA | NomadNet S, 0.5-1; Sideband not practical | the terminal page has no status bar, touch only scrolls, no F-keys, about 40 columns at 480 px wide (V guidelines 1 and 2); NomadNet's usability there is U |
| e | **Positions and telemetry with Deck Atlas** | `position.state` (the draft contract of report 054 6.3; its writer comes with Deck Atlas's next version, V Deck Atlas README); LXMF `FIELD_TELEMETRY` (0x02) in Sideband's packed sensor format: the location sensor packs latitude and longitude as signed 32-bit integers in millionths of a degree, altitude, speed, bearing and accuracy in hundredths, plus a timestamp (V, `sense.py`, read as a format, no code copied); received positions written as an overlay file for Deck Atlas (report 054 6.2). Sideband's plugin system has telemetry, command and service plugins, among them a gpsd location example (V README), so a desktop Sideband can be the collector on the other side | M, 1.25-2 | never automatic: a position goes only to a contact the owner picks, per send or per explicit "share for N minutes" |
| f | **File transfer** | LXMF `FIELD_FILE_ATTACHMENTS` (0x05), or an RNS Resource (as `rncp`) | S-M, 1-1.5 | fine over Wi-Fi; over LoRa the duty cycle limits it (4.2) |
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

**Bridge protocol (sketch).** JSON lines, requests with an id and a timeout, plus an event stream: `hello`, `status`,
`ifaces`, `iface.outgoing` (the transmit switch), `announces.subscribe`, `paths`, `announce`, `lxmf.send`,
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
License at all.

## 7. Effort, phases, tests (question 6)

### 7.1 Phases

All costs are estimates (I), in worker sessions of about 0.4 M tokens; the controller's design passes and reviews are
extra.

| Phase | Content | Size | Cost (ws) | Owner time on boards | Exit |
|---|---|---|---|---|---|
| P0 probe | section 8: venv install on the Pi 4B and the deck, memory, CPU and start time, stamp timings, LXMF between the deck and the PC over Wi-Fi, the XIAO S3 as a listen-only RNode for 30 minutes, NomadNet's TUI at the terminal page's size | S | 0.5-0.75 (probe scripts, reading the logs, a short note) | 60-90 min | go/no-go against the D2 budget |
| P1a service | the package: vendored `rns` and `lxmf`, the bridge (status, interfaces, announces, transmit switches, announce, identity), the first-run configuration, the user unit with limits, the port lock (4.4), PC tests with two real RNS instances, packages for arm64 and amd64 | M | 1.5-2 | | |
| P1b status app | native app: service start/stop, interfaces, an RNode radio page, heard announces, paths, transmit switches, identity page; sizes per decision (640x480 first) | M | 1.5-2 | 20 min | |
| Docs (P1) | README rows, app page | | 0.25 | | |
| P2 messenger | (a) of 5.1 | L | 3.5-5 | 30 min | |
| Docs (P2) | | | 0.25 | | |
| P3 NomadNet browser | (b): text, links, fields first; partials and tables next; images later | M-L | 2.5-4 | 20 min | |
| P4 positions | (e), after Deck Atlas writes `position.state` | M | 1.25-2 | 20 min | |
| P5 files | (f) | S-M | 1-1.5 | 15 min | |
| Mesh Hop guard | D18 | S | 0.25 | 5 min | |
| Docs (P3-P5) | | | 0.5 | | |
| Optional: NomadNet wrapper | (d), D15 | S | 0.5-1 | 10 min | |

**Totals (I):** MVP (P0 + P1 + docs) **3.75-5 ws (1.5-2 M tokens)**; with the messenger **7.5-10.25 ws**; everything
except the optional wrapper **13-18.5 ws (5.2-7.4 M tokens)**. Upstream follow-up releases: about 0.1-0.25 ws each.

### 7.2 What can be tested where

| On the PC, no radio | Needs a board |
|---|---|
| RNS and LXMF in a WSL venv; two real instances linked by TCP or AutoInterface on a virtual network (I); bridge tests against real RNS (no mock of the protocol); LXMF interoperability with NomadNet and Sideband on the PC; multi-hop cases in the Reticulated simulator (V manual); native apps against a fake bridge replaying JSON fixtures; headless scenes at every size; micron golden pages | RAM, CPU, start time, stamps and power on the Zero 2 W; RNode over USB and BLE; AutoInterface between real devices; the port lock against Mesh Hop; the terminal page for NomadNet; `X-Background` behaviour; long runs (memory growth over a day) |

### 7.3 Open unknowns

Collected in section 10. The ones that decide the plan: Python memory and start time on the deck, stamp time and its
effect on power, NomadNet in the terminal page, and the owner's other LoRa boards.

### 7.4 Recommendation, with reasons

1. **Run P0 first.** It costs about one evening and settles D2, D13 and D15 with numbers.
2. **If it passes, architecture A**, status app before messenger: the status app needs no transmit, so it is useful
   while the owner decides on channel plans and announces, and it exercises the whole service.
3. **Keep the deck a client.** For a steady LoRa presence, make the Pi 4B a home transport node (configuration only,
   g2) and let the decks reach it over Wi-Fi, as the manual recommends.
4. **Do not build C or D now.** C is the largest option and the one upstream objects to; D is experimental by its own
   account.
5. **Treat propagated messages on the Zero 2 W as open.** If P0 confirms minutes per stamp, the deck sends direct and
   opportunistic messages only, and propagation sync runs on the Pi 4B, Pi 5 or tablet.

## 8. Probes (proposed, not run)

The Pi 4B first (faster), then the deck (zero7), with the owner present for the deck runs. Every run
has a timeout; nothing touches `~/.reticulum` (a separate configuration folder and `instance_name = probe`); no launcher
restart; cleanup at the end. The commands are drafts to be checked against the installed versions at probe time.

**P0-a, install (each board; measure: install time, disk use).**

```sh
python3 --version
dpkg -l python3-cryptography python3-serial python3-venv | tail -n +6
python3 -m venv --system-site-packages ~/rns-probe
time ~/rns-probe/bin/pip install --no-deps rns==1.6.0 lxmf==1.3.0
~/rns-probe/bin/rnsd --version
du -sh ~/rns-probe
```

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
MALLOC_ARENA_MAX=2 timeout 900 ~/rns-probe/bin/rnsd --config ~/rns-probe/cfg -v > ~/rns-probe/rnsd.log 2>&1 &
sleep 60;  P=$(pgrep -f 'rnsd --config'); grep -E 'VmRSS|VmHWM' /proc/$P/status; ps -o pcpu,etimes -p $P
sleep 540; grep -E 'VmRSS|VmHWM' /proc/$P/status; ps -o pcpu,etimes -p $P
```

**P0-c, Wi-Fi (measure: peers seen, LXMF delivery deck to PC and back, VmRSS with traffic).** Set `enabled = yes` and
`devices = wlan0` on the AutoInterface, run `rnsd` or NomadNet on the PC on the same Wi-Fi, check
`~/rns-probe/bin/rnstatus --config ~/rns-probe/cfg`, and exchange a message with the LXMF repository's
`docs/example_receiver.py` and `docs/example_sender.py` (V README) run on the deck against the probe instance.

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

**P0-e, RNode listen-only (the owner flashes the XIAO S3 WIO on the PC first: `pip install rns`, then `rnodeconf
--autoinstall`; measure: device info, received bytes, RSSI, SNR, noise floor and channel load every 5 minutes for 30
minutes, VmRSS and CPU).** On the deck: `~/rns-probe/bin/rnodeconf /dev/serial/by-id/<the device> -i`, then add to the
probe configuration and restart the probe daemon:

```ini
  [[RNode XIAO]]
    type = RNodeInterface
    enabled = yes
    port = /dev/serial/by-id/<the device>
    frequency = 867200000
    bandwidth = 125000
    txpower = 7
    spreadingfactor = 8
    codingrate = 5
    outgoing = False
    airtime_limit_long = 1
    airtime_limit_short = 33
```

```sh
for i in $(seq 6); do ~/rns-probe/bin/rnstatus --config ~/rns-probe/cfg -j > ~/rns-probe/status-$i.json; sleep 300; done
```

**P0-f, NomadNet's TUI (measure: the terminal page's real grid, usability at that size, VmRSS).** In the launcher's
terminal page run `stty size` to read its grid on the deck (640x480) and on zero8 (480x320). Then
`~/rns-probe/bin/pip install nomadnet==1.5.0` (it pulls `urwid>=3.0.5` from PyPI) and run
`~/rns-probe/bin/nomadnet --rnsconfig ~/rns-probe/cfg` over SSH with the terminal resized to that grid (`stty cols <c>
rows <r>`); note whether conversations, the network list and a page are usable.

**P0-g, cleanup.** Stop the probe daemon, `rm -rf ~/rns-probe ~/.nomadnetwork` (NomadNet's own folder, created by P0-f
only).

## 9. Risks, ranked

1. **Zero 2 W resources.** Python's memory is unknown; a cost-16 stamp may take minutes of full CPU (I) on a board where no
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
6. **Radio legality** rests on correct settings (frequency, power, airtime). Mitigation: the status app sets
   `airtime_limit_long` from the chosen sub-band and refuses a power above the sub-band's e.r.p. for a declared antenna
   gain (I).
7. **A precedent.** An exception to "no Python for long-running parts" may invite others (I). Mitigation: scope the
   exception to this service by name.

## 10. Not verified

- Memory, CPU and start time of RNS (and of the bridge) on the Zero 2 W; the 1.6.0 memory claims on our boards.
- Stamp time on the Zero 2 W (my 2-5 minutes for cost 16 is an estimate); SHA-256 throughput per core; whether the
  BCM2710A1 lacks the ARMv8 crypto extensions (widely reported, not checked).
- The default destination-stamp cost from RNS 1.6.1 and its one-time CPU.
- How often LXMF users (Sideband, MeshChatX, Columba) set an inbound stamp cost, which makes direct messages cost CPU
  too.
- NomadNet's usability in the launcher's terminal page; the page's real column count at 640x480 and 480x320.
- RNode over BLE on our boards: pairing without a display on the XIAO S3, `bleak` 0.22.3 with RNS 1.6.0.
- RNS's retry behaviour when an RNode port is busy; whether RNode firmware sends anything before it is configured.
- Which of the owner's boards beyond the XIAO S3 can run RNode (community forks not checked); the Wio-SX1262 antenna
  connector.
- Local Reticulum activity and channel plans in France (rmap.world and directory.rns.recipes not read); the current
  reachability of the old testnet hosts.
- Meshtastic's EU default channel (869.525 MHz assumed).
- The EN 300 220 duty-cycle definition; whether RNode's interference avoidance counts as LBT+AFA (assumed not).
- Amateur-band rules in France for encrypted traffic.
- `i2pd` resource use on a Zero 2 W.
- Python version, `venv` behaviour and journald persistence on our board images (Debian 13 suggests Python 3.13).
- Whether Settings > Apps handles a package without a home tile.
- Interoperability claims of Reticulum-Go, Reticulum-rs and microReticulum (not tested); LXST's licence text.
- Licences of `cryptography`, `pyserial` and `bleak` (not read; Debian dependencies, not bundled).
- MeshChat and MeshChatX resource use.

## 11. Questions for the owner

1. **Your other LoRa boards:** which models (board and LoRa chip)? Any with a screen (useful for BLE pairing)?
2. **Who would you talk to:** people with Sideband, Columba or MeshChatX, a local Reticulum group, or only your own
   devices (then a private network with an IFAC passphrase)?
3. **Local activity:** do you know of Reticulum users or a channel plan near you?
4. **The XIAO S3 WIO:** may it be flashed as an RNode (it leaves MeshCore until reflashed)?
5. **Python service (D2):** acceptable if P0 meets a budget? Which limits (my proposal: 60 MB, 3 % idle CPU, 20 s)?
6. **Receive in the background (D6):** only while a Reticulum app is open, or a "keep running" switch?
7. **A home node:** should the Pi 4B become an always-on transport node with an RNode?
8. **Internet:** never, or only entrypoints you pick (D9)?
9. **P0:** may it run on the Pi 4B and the deck, with you present for the deck, using the commands of section 8?
10. **AI data use (D17):** can you confirm the account's setting for model training?

## Sources

All read on 2026-11-07 unless noted.

- Reticulum (RNS), repository mirror, `master` @ `cb1260891d` (2026-11-07): https://github.com/markqvist/Reticulum ;
  release RNS 1.6.0 (2026-11-07, notes quoted in 0, 3.3, 3.6): https://github.com/markqvist/Reticulum/releases/tag/1.6.0 ;
  `LICENSE`: https://github.com/markqvist/Reticulum/blob/master/LICENSE ; `MIRROR.md` (added 2025-12-27):
  https://github.com/markqvist/Reticulum/blob/master/MIRROR.md ; `setup.py`, `RNS/Reticulum.py`,
  `RNS/Interfaces/RNodeInterface.py`, `RNS/Stamps.py`, `RNS/Storage.py`, `RNS/__init__.py` at the same commit.
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
  (`README.md`, `LICENSE`, `Boards.h`, `Bluetooth.h`).
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
- ERC Recommendation 70-03 "Relating to the use of Short Range Devices", amendments of 9 October 2026, Annex 1 rows
  h1.0-h1.9 and h2-h3, Appendix 1 (France): https://docdb.cept.org/document/845 (PDF https://docdb.cept.org/download/5013)
- Our code and documents: apps `origin/main` @ `67e546b` (`docs/APP-GUIDELINES.md`, `README.md`,
  `apps/mesh-hop/app.json`, `apps/mesh-hop/cli/requirements.txt`, `apps/mesh-hop/cli/vendor.py`,
  `apps/mesh-hop/src/core/serial_transport.cpp`, `apps/deck-atlas/README.md`, `tools/make_registry.py`); launcher
  `github/main` @ `28817a50` (`docs/dev/reports/054-offline-maps-wardriving-study.md`,
  `docs/dev/reports/056-mmwave-sensor-study.md`, `docs/dev/reports/007-handoff.md` (EU 868 preset),
  `docs/OPEN_SOURCE_COMPONENTS.md`, `docs/dev/decisions.md`, `docs/dev/ref/README.md`,
  `projects/APPLaunch/pizero2w/install.sh`).
