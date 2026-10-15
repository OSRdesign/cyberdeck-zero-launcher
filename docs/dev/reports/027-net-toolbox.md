# 027 - Net Toolbox: a network toolbox app with live, visual tools

Status: study and plan written 2026-10-15 on branch `report-027-net-toolbox`. Nothing was built or deployed. Feasibility
probes of 2026-10-15 were read-only, on the deck (zero7.local) and the Pi 3A+ (192.168.68.170): `sysctl`, `getcap`,
`dpkg`, `/sys`, `/proc`, small socket tests, one `ping -c1` to the gateway. The apps repo was read at main 715421d
(LanScan, Wi-Fi Survey, `_common`).

Markers: **V** = verified in code or on a board, **I** = inferred, needs a board. Owner decisions are marked **D**.
UI items are proposals for the owner to decide.

## 0. Summary

Net Toolbox is one native LVGL app (package `net-toolbox`, `X-MinSize=640x480`, all boards) with the tools people
reach for when a network misbehaves, plus a few that are fun to watch. It does not repeat LanScan (host discovery,
port scan) or Wi-Fi Survey (channels).

**Owner decision 2026-10-15:** scope and phase order as in section 3, name **Net Toolbox**, report first, then tasks.

**Facts that shape the build (all V on the deck and the Pi 3A+)**
- **Ping needs no root.** `net.ipv4.ping_group_range` is `0 2147483647`; an unprivileged ICMP datagram socket (v4 and
  v6) opens. Raw ICMP does not (EPERM).
- **Traceroute can live in the app.** `traceroute`, `tracepath` and `mtr` are not installed, but `IP_TTL` plus
  `IP_RECVERR` on an unprivileged ICMP or UDP socket returns the "time exceeded" errors, so the app does its own
  hop-by-hop (and MTR-style repeated) probing. No `depends` entry needed.
- **DNS is ours to speak.** `dig`/`nslookup`/`host` are missing; the app builds and parses DNS over UDP itself (TCP
  fallback), which also lets the user pick the resolver and avoids `getaddrinfo`, which cannot be cancelled.
- **TLS and HTTP timing: use the `curl` program** (8.14.1, OpenSSL 3.5.7, `ca-certificates` present) through the
  shared `async_run`. `-w` gives the DNS/connect/TLS/first-byte timings and the certificate dates. Depends: `curl`.
- **Bluetooth** (bluez 5.82, `hci0` powered, the system bus open to any user) is reachable over D-Bus with
  libsystemd (sd-bus). The SDK has the headers; the shared `_common/scons/main.SConstruct` has a fixed library list
  and would need an optional per-app list. A scan as a normal user was not run (I).
- **Memory.** 414 MB board, about 240 MB available, guideline cap 25 MB RSS per app. The speed test streams to
  `/dev/null` (never a buffer, `/tmp` is a 208 MB RAM disk). One worker thread with `poll()` instead of LanScan's 24+
  detached 8 MB-stack threads. The 972 KB OUI table is not loaded whole.
- **Wi-Fi and Bluetooth share one radio chip**, so a BLE scan can hiccup a Bluetooth keyboard. Scan short, say so
  first.
- `iw` lives in `/usr/sbin` (not in the ssh PATH): call it by full path; `/proc/net/wireless` needs nothing.

## 1. Tools

| Tool | How | Phase |
|---|---|---|
| Interfaces | `getifaddrs`, `/sys/class/net`, `/proc/net/route`, `resolv.conf`, `/proc/net/wireless` | 0.1 |
| Diagnosis ("Why is my internet broken?") | link, address, gateway ping, DNS, internet ping, captive portal | 0.1 |
| Ping (loss, min/avg/max, jitter) | in-app ICMP datagram socket | 0.1 |
| DNS lookup (A, AAAA, MX, TXT, NS, PTR; chosen resolver; compare) | in-app UDP client | 0.1 |
| Port check (one port, presets) | in-app non-blocking connect | 0.1 |
| Traceroute / MTR | in-app TTL + `IP_RECVERR` | 0.2 |
| HTTP check (status, timing waterfall, cert days left) | `curl -w` | 0.2 |
| Public IP, whois | DNS TXT / curl trace; whois over TCP 43 (IANA referral) | 0.2 |
| Speed test (down, up, latency, under load) | `curl` to speed.cloudflare.com, streamed | 0.3 |
| Wake-on-LAN (saved machines) | in-app UDP broadcast | 0.3 |
| BLE radar | sd-bus to BlueZ | 0.3 |
| Wi-Fi walk test | `/proc/net/wireless` at 1 Hz | 0.4 |
| DNS race | the DNS client, several resolvers at once | 0.4 |
| Bandwidth meters | `/sys/class/net/*/statistics` at 1 Hz | 0.4 |

Every result can be saved to `~/net-logs` (text). Nothing leaves the board until the user starts a test.

## 2. Screens

Home is a Comfortable list of the tools (CompactList, same rules as System Monitor and Serial Console). Each tool is
a full-screen page with Esc/Left back. Touch and keyboard both work. Sketches at 640x480:

```
Home                               Diagnosis (the health ladder)
+------------------------------+   +------------------------------+
| Net Toolbox          wlan0 ok|   | Why is my internet broken?   |
| ---------------------------- |   |                              |
| > Diagnose my connection     |   |  (o) Link up        wlan0    |
|   Ping                       |   |  (o) Address        .68.121  |
|   DNS lookup                 |   |  (o) Gateway        3 ms     |
|   Port check                 |   |  (x) DNS            timeout  |
|   Interfaces                 |   |  ( ) Internet                |
|   ...                        |   |  ( ) Captive portal          |
+------------------------------+   | The gateway answers but DNS  |
                                   | does not. Try another        |
                                   | resolver.        [Run again] |
                                   +------------------------------+

Ping                               Traceroute chain (0.2)
+------------------------------+   +------------------------------+
| 1.1.1.1   [Gateway][Edit]    |   | to example.com        12 hops|
| 12 ms  loss 0%  jitter 2 ms  |   | 1 gw        1 ms  ====       |
|  ___/\__/\_____/\___  graph  |   | 2 isp-a    14 ms  ======     |
| .....#...........x...  strip |   | 3 ???      --     (no reply) |
| min 9  avg 12  max 31        |   | 4 core-b   41 ms  ======!!!! |
|          [Pause] [Save log]  |   | 5 target   43 ms  =======    |
+------------------------------+   +------------------------------+
```

- **Health ladder:** rungs light up in turn, green for pass, red for the first failure, grey for not reached. The
  line under it is a plain-words reason, not a code.
- **Ping page:** latency line with the jitter band, and a strip below it with one cell per packet (green, amber for a
  slow one, red for a lost one) so a bad minute is visible at a glance. Targets: gateway, 1.1.1.1, 8.8.8.8, typed
  host, saved targets. The text fields use the keyboard, or the on-screen keyboard when none is connected.
- **Traceroute chain (0.2):** hops as nodes on a line, a bar per hop for latency, red when loss shows; a silent hop
  is drawn as a dotted node, not an error.
- **HTTP waterfall (0.2):** DNS, connect, TLS and first-byte as stacked horizontal bars; the certificate's days left in
  a badge (amber below 30, red below 7).
- **Speed gauge (0.3):** a needle dial while a phase runs, then the throughput chart; the "under load" latency shows
  bufferbloat as a number and a grade.
- **BLE radar (0.3):** a sweep with signal rings; devices are blips that fade when out of range; the list below
  shows names, vendor and RSSI.
- **Walk test (0.4):** signal against time while the user walks with the deck; dips under -75 dBm are marked.
- **DNS race (0.4):** the same query to several resolvers, bars that race to the finish.
- **Bandwidth (0.4):** in and out per interface as live bars.

All colours come from the app palette (docs/APP-GUIDELINES.md): green, amber, red only for status, never alone: each
state also has a word or a shape, for the colour-blind and for the small panel.

## 3. Phases

| Phase | Content | Libraries |
|---|---|---|
| 0.1 | Home, Interfaces, Diagnosis + ladder, Ping + strip, DNS lookup, Port check, log save | none beyond the base (`depends` as System Monitor) |
| 0.2 | Traceroute/MTR chain, HTTP waterfall, public IP, whois | `curl`, `ca-certificates` |
| 0.3 | Speed gauge, Wake-on-LAN, BLE radar | libsystemd (needs the shared build change, **D1**) |
| 0.4 | Walk test, DNS race, bandwidth meters | none |

0.1 already gives a useful app on every board with no new library and no build-system change.

## 4. Risks and decisions

- **D1 (needed at 0.3, not before):** change the shared `_common/scons/main.SConstruct` to accept an optional
  per-app library list so the app can link libsystemd. Alternative: leave BLE out or drive `bluetoothctl` through
  `async_run`. Proposal: the optional list, as a small separate apps PR before 0.3.
- **D2:** `bluez` stays an optional dependency; without it the BLE tool says Bluetooth is not available.
- **D3:** the speed test and the captive-portal check use third-party endpoints (Cloudflare, a generate_204 URL).
  Proposal: both documented in the README, both off until the user starts them, URLs editable later.
- **Resolver order:** `resolv.conf` lists 192.168.1.1 first on both boards, not the gateway. The diagnosis reports
  the resolver it actually used.
- **ICMP is not guaranteed** on other images (`ping_group_range`). The app tests the socket at start and says so
  when ping is unavailable.
- **Silent hops and rate-limited routers** are normal in traceroute; the UI shows them as such.
- **Not run yet (I):** the curl `%{certs}` expiry output, a chunked upload to the speed endpoint, a BLE scan as a
  normal user, the board test steps for the new pages.
- **Left out on purpose:** a world map of the hops (needs an offline geo database, heavy on 415 MB, little gain over
  the chain view).

## 5. Tasks (when 0.1 is approved)

1. Heavy: design brief from this report, core library (`src/core/`, no LVGL): interface reader, ICMP ping, DNS
   client, TCP check, diagnosis logic, with host tests and fixtures.
2. Careful: pages on CompactList (Home, Interfaces, Diagnosis ladder, Ping with strip, DNS, Port check), scene tests
   at 640x480 and 800x480, board test steps in `tests/BOARD-TESTS.md`.
3. Light: package, README, registry draft, cross build.
4. Orchestrator: review each diff and the rendered images; owner runs the physical tests on the deck and the
   Pi 3A+; merge only on the owner's word.

Each later phase repeats the same loop.
