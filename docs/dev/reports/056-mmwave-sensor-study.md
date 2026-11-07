# 056 - mmWave: an app for the Hi-Link HLK-LD2452 tracking radar (study and probe)

Date 2026-11-06. Study plus a probe of the real sensor from the owner's Windows PC: no product code, no board touched,
nothing built or installed on a board. The owner's sensor is an HLK-LD2452 (24 GHz multi-target tracking radar,
UART) on a CH340 USB-serial board (`VID_1A86 PID_7523`, COM6 on the PC). This report gives the protocol facts, what the
sensor answered, a detection method for the boards and options for the app; design and visual decisions stay with
the controller and the owner.

**What I read.**
- Launcher `github/main` @ `d06faf54`: `docs/APP-CONTRACT.md` (sections 3, 6, 13: `X-MinSize`, `X-Portrait`,
  `X-Background`, exclusive hardware), report 054 (format, section 7.2 sizes rule).
- Apps `origin/main` @ `bec05f3`: `docs/APP-GUIDELINES.md` (permissions, no blocking UI); Mesh Hop
  `src/core/serial_transport.cpp` (`list_serial_ports`, `flock` + `TIOCEXCL`, known bridge VIDs); Serial Console
  `src/core/ports.hpp`, `probe.hpp`, `probe.cpp` (listen speeds), `identify.hpp` (binary frame classifier),
  `serial_io.cpp` (`TIOCEXCL`), `tty_speed.cpp` (`termios2`/`BOTHER`), `app.json`, `.desktop`; viz1090
  `src/core/gps.hpp`/`gps.cpp` (listen-only NMEA port scan).
- Hi-Link documents and open-source drivers (Sources, with licences), read 2026-11-06.

**Markers.** **V** = read in a cited document or source file (Sources list); **M** = measured on the owner's sensor in
this study (logs in section 3); **I** = inferred or estimated, needs a check. **Cost unit:** one worker session (ws),
about 0.4 M tokens (as in report 054).

## 0. Summary

**Verdict: the LD2452 sends exactly the LD2450 tracking frame and answers the LD2450 "enable/end configuration"
handshake, but stays silent to the LD2450 read queries (firmware, MAC, tracking mode, zone filter).** Build the app
around the data stream; treat the LD2452's parameter commands as unknown.

1. **Data frame (V, M).** 30 bytes, `AA FF 03 00` + 3 targets x 8 bytes + `55 CC`. Per target, four little-endian
   16-bit fields: x, y (mm), speed (cm/s), resolution (mm). x, y and speed are **sign-magnitude**: bit 15 set =
   positive, clear = negative, bits 0-14 the absolute value (not two's complement). Approaching the radar is a
   negative speed. An empty slot is 8 zero bytes. The LD2452 manual (V1.3, 2025-04-09) and the LD2450 protocol
   (V1.03, 2023-10-17) give the same layout and the same worked example (2.3).
2. **Commands (V, M).** The LD2452 manual documents **no** configuration command. On the real sensor at 9600 baud:
   enable config (`0xFF`) is acknowledged in 45 ms with the LD2450 answer (protocol `0x0001`, buffer 64) and stops
   the data stream; end config (`0xFE`) is acknowledged in 40 ms and the stream comes back at once. Read firmware
   (`0xA0`), read MAC (`0xA5`), query tracking mode (`0x91`) and query zone filter (`0xC1`) got **no byte at all**
   (each sent in two sessions, 1.5 s wait). So firmware, MAC and mode could not be read (3.3).
3. **Baud (M, V).** The sensor talks at **9600** baud, not the documented default 256000 (V, both manuals). It sends
   **19.3 frames per second** (51.8 ms apart), not the documented 10 Hz; at 9600 that fills 60 % of the line; no bad
   frame in about 13,000 frames (11.5 minutes). Why 9600 is not known (a seller setting or a firmware build, I); this
   study did not try to change it.
4. **A quirk to design for (M).** When the radar answers a command it cuts the data frame it is sending short, and
   the first byte of its answer arrives damaged (`F8` or `FC` instead of `FD`). A decoder must resync on `FC FB FA`
   (which can never occur inside a valid data frame) and validate the `04 03 02 01` footer (3.3, 4.4).
5. **Sensor facts (V, LD2452 manual).** 24-24.25 GHz FMCW, 1 transmit 2 receive antennas; 5 V supply (3.3 V
   possible depending on board parts), 126 mA average; up to 6 m straight ahead, azimuth +-60 deg, elevation +-30
   deg; up to 3 targets; range resolution 0.72 m, distance accuracy 0.15 m, angle accuracy 2-20 deg; UART "TTL
   level" 8N1 (the LD2450 says 3.3 V I/O, V; the LD2452 likely the same, I); connector J1 ZH1.5-4P: 5V, RX, TX, GND.
6. **Detection on the boards (proposal, 4).** Listen-only: scan free `ttyUSB*`/`ttyACM*` (and `/dev/serial0` only
   when the user turns the GPIO UART on), never a port another app holds (Mesh Hop/Serial Console lock with
   `flock`/`TIOCEXCL`; also check `/proc/*/fd`), speeds in the order remembered, 256000, 9600, 115200, then the rest of
   the LD2450 table; a port is the radar after 3 valid frames in a row; a silent port is left after 0.6 s; about 1 s
   to find the owner's sensor, 0.2 s once remembered. GPS (NMEA `$G...`, UBX `B5 62`), LoRa boards and LD2410
   presence radars (`F4 F3 F2 F1` frames) are named and left alone.
7. **App (recommendation, 5).** One native `cp0_lvgl` app in the apps repo, MIT: a pure `core/` (frame decoder,
   port detector, a small tracker, frame sources: serial, file replay, fake) with PC unit tests on the recorded
   fixture, and an LVGL UI with a top-down radar plot (range rings, the +-60 deg wedge, up to 3 targets with short
   trails and a radial speed mark), a table, a distance/speed graph, a sensor page and a clear "not found" page with
   the wiring. Sizes as Deck Atlas: `X-MinSize=480x320`, `X-Portrait=320x480`. Release 0.1 is **listen-only**:
   no command is sent unless a later phase, with the owner's word, adds settings.
8. **Phases (I).** 0.1 viewer 2-2.75 ws, docs 0.25; MVP about 2.25-3 ws (0.9-1.2 M tokens). Later and optional:
   recording/replay and an LD2410 view 1-1.5 ws; settings writes only for sensors that answer the queries (an
   LD2450), after the owner's word, 0.75-1 ws.

**Fixture (M).** `docs/dev/reports/056-assets/`: 15 s of the data stream at 9600 (raw `.bin` + decoded `.csv`) and the
raw bytes of one read-only configuration session (the cut frame, the damaged ACK, the end ACK and the resumed
stream). In the 15 s one person walks in from 5.5 m (x +2.6 m, y 4.9 m) to about 1 m in front of the sensor and
stays there: 290 frames (19.3 Hz), 29 without a target, then 261 with one target (always slot 1), no bad frame.
The speed comes in **steps of 17 cm/s** (-85 to +34 cm/s here, up to +136 in the long capture) and its sign agrees
with the distance trend in 147 of 157 checks: approaching is negative, as documented. Two people were not recorded.

**Owner questions:** section 9.

## 1. Decisions for the controller and the owner

All open; the recommendation is mine, the choice is the controller's or the owner's (design case by case).

| # | Decision | Options | Recommendation |
|---|---|---|---|
| D1 | Name, app id | "mmWave" / `mmwave`; "Radar" / `mmwave-radar`; "Tracking Radar" | "mmWave", `mmwave` (short, matches the brief) |
| D2 | Commands in 0.1 | (a) listen-only; (b) also the enable/end handshake to show "protocol 1, buffer 64" | (a): the handshake adds one line of information, stops the stream for about 0.1 s and writes to the port; the stream already identifies the sensor |
| D3 | Port scan scope | (a) every free USB serial port at start (viz1090's precedent); (b) only bridge chips (CH340, CP210x, FTDI, PL2303, CH9102) automatically, native microcontroller USB only when picked; (c) only the remembered or picked port | (b) |
| D4 | GPIO UART (`/dev/serial0`) | scanned automatically; only when turned on in the app | only when turned on (a console or another device may use it) |
| D5 | Units | m and m/s; m and km/h; cm and cm/s (raw) | m (2 decimals) and m/s (1 decimal), km/h as a setting |
| D6 | Left/right | trust the sign of x; a "flip left/right" setting | the setting, default from a 10 s owner check (2.3: the manual shows +x flipping with the board's orientation) |
| D7 | Hidden behaviour | `X-Background=freeze` and release the port on hide; `run` | `freeze` in 0.1 (a viewer; nothing to keep doing hidden); `run` only with recording (0.2) |
| D8 | Writing settings | never; a later phase for sensors that answer the read queries; also the LD2452 | a later phase, LD2450 only, after the owner's word; the LD2452 only with Hi-Link's LD2452 command document |
| D9 | LD2410 | ignore; name it in "not found"; full support later | name it in 0.1 (cheap), a view in 0.2 if wanted |

## 2. The sensor: documents and protocol

### 2.1 Sources and licences

| Source | What | Licence / use |
|---|---|---|
| Hi-Link, HLK-LD2452 User Manual V1.3 (2025-04-09), 22 pages | specifications, pins, the data frame (section 5), mounting, range figure | manufacturer document; facts only |
| Hi-Link, HLK-LD2450 Serial Communication Protocol V1.03 (2023-10-17) | the command set with exact frames, the data frame | manufacturer document; facts only |
| Hi-Link, LD2450 protocol V1.02 EN (2023-09) | same, before the tracking-mode query | facts only |
| ESPHome `ld2450` component (C++) | command words, ACK offsets, angle formula | **GPL-3.0** (ESPHome's C++ files; its Python is MIT, V): read for facts only, no code copied |
| ESPHome `ld2410`, `ld24xx` (C++) | LD2410 data frame header/footer, shared command framing | **GPL-3.0**, facts only |
| RBEGamer/HLK-LD2450 (Arduino) | LD2450 library | MIT (not read in detail) |
| csRon/HLK-LD2450 (Python) | LD2450 protocol and demos | MIT (not read in detail) |
| Fiooodooor/HLK-LD245X | LD2450 + LD2451 | BSD-3-Clause (not read in detail) |
| duramson/ld2450-rs (`ld2450-proto` crate) | parser and command builder | MIT OR Apache-2.0 (not read in detail) |
| ncmreynolds/ld2410 (Arduino) | LD2410 | LGPL-2.1, named only |

No source was found for an LD2452-specific command document (searches 2026-11-06: Hi-Link sites, distributors,
GitHub, ESPHome). The app's code will be written from the Hi-Link documents and our own measurements; the probe
scripts of this study (`C:\CLAUDE\zero7\mmwave-work\scripts\`, MIT, mine) copy nothing.

### 2.2 Specifications

| Item | HLK-LD2452 (manual V1.3) | HLK-LD2450 (protocol V1.03) | Marker |
|---|---|---|---|
| Band, waveform | 24-24.25 GHz FMCW, sweep 0.21-0.25 GHz, 1T2R microstrip antenna | 24 GHz | V |
| Supply | 3.3-5 V (5 V typical; 3.3 V "depending on mounted resistors and capacitors"), 126 mA average | 5 V, supply able to give more than 200 mA | V |
| UART | "TTL level", default 256000, 8N1 | 3.3 V I/O, default 256000, 8N1 | V (LD2452 3.3 V: I) |
| Range | 6 m max (changed in V1.3); range resolution 0.72 m; distance accuracy 0.15 m | not in the protocol document | V |
| Field of view | azimuth +-60 deg, elevation +-30 deg; figure 7-3: about 6 m at 0-15 deg, about 3-4 m near +-60 deg at 1.5 m mounting height | not in the protocol document | V (figure reading: I) |
| Angle accuracy | 2-20 deg | - | V |
| Targets | up to 3 (single-target mode in the PC tool) | up to 3, single/multi tracking modes | V |
| Update rate | 10 Hz | 10 frames per second | V; **19.3 Hz measured on the LD2452 (M)** |
| Distance gates | 36 cm per gate, up to gate 23 (the PC tool's blind-spot setting) | resolution field "distance gate size" | V |
| Connector | J1 ZH1.5-4P: 1 5V, 2 RX, 3 TX, 4 GND; J2 (programming/test): 5V, DP, DM, DEBUG TXD, GND | 5V, GND, Tx, Rx | V |
| Size, temperature | 23 x 42 mm, -20..70 deg C (table) / -40..85 deg C (overview) | - | V |
| Bluetooth | not mentioned | BLE, on by factory default (`0xA4`) | V (LD2452 has none: I) |
| Mounting | wall, 1.5-2 m high, antenna normal = 0 deg | - | V |
| Firmware upgrade | "HLK-LD2452 IAP Tool" over the J1 UART at 256000, two partitions APP0/APP1 | - | V |

### 2.3 Data frame

Little-endian throughout (V). 30 bytes:

| Offset | Bytes | Field |
|---|---|---|
| 0 | 4 | header `AA FF 03 00` |
| 4 | 8 | target 1 |
| 12 | 8 | target 2 |
| 20 | 8 | target 3 |
| 28 | 2 | footer `55 CC` |

Per target (8 bytes):

| Offset | Field | Encoding | Unit |
|---|---|---|---|
| +0 | x | sign-magnitude 16-bit: bit 15 set = positive, clear = negative; bits 0-14 = absolute value | mm |
| +2 | y | same; forward distance from the antenna plane (positive in front) | mm |
| +4 | speed | same; **approaching the radar is negative** | cm/s |
| +6 | resolution | unsigned 16-bit; LD2450 "distance gate size", LD2452 "single pixel distance value" | mm |

Worked example (V, LD2452 manual; the LD2450 document has the same with resolution `40 01` = 320 mm):
`AA FF 03 00 0E 03 B1 86 10 00 68 01` + 16 zero bytes + `55 CC`: x = `0x030E` = 782, bit 15 clear -> **-782 mm**;
y = `0x86B1` = 34481, bit 15 set -> 34481 - 32768 = **1713 mm**; speed = `0x0010` = 16, bit 15 clear -> **-16 cm/s**
(approaching); resolution `0x0168` = **360 mm**. Targets 2 and 3 are all zero: no target.

Derived values (proposal, I): distance = sqrt(x^2 + y^2); angle = atan2(x, y) in degrees (0 = straight ahead, the
wedge is +-60); direction = approaching when speed < 0, receding when > 0, still at 0 (the speed arrives in steps of
17 cm/s, M, so there is no smaller value to filter). ESPHome publishes angle = atan2(-x, y) (V), the opposite
sign: the sign of "left" is a convention to settle on the board. Figure 5-1 of the LD2452 manual (V) draws y out of the antenna face and x along
the board's long edge, and its two drawings show +x pointing to opposite sides with the board turned: so the
on-screen left/right needs a check with the real mounting (decision D6).

Speed is the radar's own (Doppler, radial) value (I): it says how fast the target closes or opens the distance,
not where it goes sideways. A true velocity vector would come from the tracker's position history.

Edge cases (I): a coordinate of exactly 0 can arrive as `00 00` ("-0") or `00 80`; an empty slot is the whole
8 bytes at zero, so test emptiness on all 8 bytes, not on x or y. The resolution field is mostly an implementation
detail (330 mm in every one of the 357 target frames recorded, M): show it on the sensor page or in the table, not
on the plot.

### 2.4 Command protocol (LD2450 V1.03)

Frames (V): `FD FC FB FA` | length (2 bytes, little-endian, of the in-frame data) | command word (2 bytes) | value |
`04 03 02 01`. The ACK repeats the word with `0x0100` added (`FF 00` -> `FF 01`), then a 2-byte status (0 success, 1
failure) and the return value. Every command must follow "enable configuration" and be followed by "end
configuration"; the document does not give the time limit between them. Data frames stop while in configuration
mode (M).

| Word | Name | Value | Exact frame sent | Answer (success) | Kind | Sent to COM6 | COM6 answer (M) |
|---|---|---|---|---|---|---|---|
| `0x00FF` | enable configuration | `0x0001` | `FD FC FB FA 04 00 FF 00 01 00 04 03 02 01` | `FD FC FB FA 08 00 FF 01 00 00 01 00 40 00 04 03 02 01`: status, protocol `0x0001`, buffer `0x0040` | mode only, nothing stored | yes | the same answer in 45 ms (first byte damaged, 3.3) |
| `0x00FE` | end configuration | none | `FD FC FB FA 02 00 FE 00 04 03 02 01` | `FD FC FB FA 04 00 FE 01 00 00 04 03 02 01` | mode only | yes | the same in 40 ms; stream back at once |
| `0x00A0` | read firmware version | none | `FD FC FB FA 02 00 A0 00 04 03 02 01` | `... 0C 00 A0 01 00 00` + type (2) + major (2) + minor (4); example `00 00 02 01 16 24 06 22` = V1.02.22062416 | **read** | yes | silence (1.5 s, 4 sessions) |
| `0x00A5` | get MAC address | `0x0001` | `FD FC FB FA 04 00 A5 00 01 00 04 03 02 01` | `... 0A 00 A5 01 00 00 8F 27 2E B8 0F 65 ...` (6 bytes; the text says "1 type byte + 3 bytes", the example and ESPHome use 6) | **read** | yes | silence |
| `0x0091` | query tracking mode | none | `FD FC FB FA 02 00 91 00 04 03 02 01` | `... 06 00 91 01 00 00 01 00 ...` single, `02 00` multi (default multi) | **read** | yes | silence |
| `0x00C1` | query zone filter | none | `FD FC FB FA 02 00 C1 00 04 03 02 01` | `... 1E 00 C1 01 00 00` + type (2: 0 off, 1 detect only inside, 2 ignore inside) + 3 rectangles x (x1, y1, x2, y2), **two's complement** int16 mm (the example `18 FC` = -1000), all zero = unused | **read** | yes | silence |
| `0x0080` | single-target tracking | none | `... 02 00 80 00 ...` | `... 80 01 00 00` | **write** | no | - |
| `0x0090` | multi-target tracking | none | `... 02 00 90 00 ...` | `... 90 01 00 00` | **write** | no | - |
| `0x00A1` | set baud rate | index (2): 1 9600, 2 19200, 3 38400, 4 57600, 5 115200, 6 230400, 7 256000 (default), 8 460800 | `... 04 00 A1 00 07 00 ...` | `... A1 01 00 00` | **write**, stored, after restart | no | - |
| `0x00A2` | restore factory settings | none | `... 02 00 A2 00 ...` | `... A2 01 00 00` | **write** (256000, BT on, multi, zones off) after restart | no | - |
| `0x00A3` | restart | none | `... 02 00 A3 00 ...` | `... A3 01 00 00`, then reboots | **state change** | no | - |
| `0x00A4` | Bluetooth on/off | `01 00` on, `00 00` off (the text writes "0x0100") | `... 04 00 A4 00 01 00 ...` | `... A4 01 00 00` | **write**, stored, after restart | no | - |
| `0x00C2` | set zone filter | 26 bytes (type + 3 rectangles) | `... 1C 00 C2 00 ...` | `... C2 01 00 00` | **write**, stored, immediate | no | - |
| `0x00C3` | - | - | - | not in the LD2450 V1.03 document nor in ESPHome (V) | - | no | - |

### 2.5 LD2452 versus LD2450

| Point | Same? | Evidence |
|---|---|---|
| Data frame layout, sign rule, units, example | **same** (only the example's resolution differs: 360 vs 320 mm) | V (both documents), M (our stream) |
| Command framing, enable/end handshake and its answer | **same** | M |
| Read queries `A0`, `A5`, `91`, `C1` | **not answered** by this LD2452 | M |
| Write commands | unknown (not tried, not documented for the LD2452) | - |
| Default baud | same on paper (256000) | V; this unit 9600 (M) |
| Frame rate | 10 Hz on paper for both | V; this unit 19.3 Hz (M) |
| Range, field of view | LD2452 6 m, +-60/+-30 deg | V |
| Bluetooth | LD2450 yes; LD2452 not mentioned | V / I |
| PC tool | LD2450 `ICLM_MTT.exe`; LD2452 `HLK-LD2452.exe` + IAP tool | V |

## 3. Probe of the owner's sensor (COM6, Windows PC)

### 3.1 Setup and safety

Python 3.11 and pyserial 3.5 on the PC; scripts in `C:\CLAUDE\zero7\mmwave-work\scripts\` (`ld245x.py` decoder,
`capture.py` and `watch.py` listen-only, `query_readonly.py` the identification, `make_fixture.py`). Every frame the
identification writes goes through one function that refuses anything outside an allowlist of six exact byte
strings (enable, `A0`, `A5`, `91`, `C1`, end), each asserted equal to the frame printed in the LD2450 V1.03
document; no set, baud, reset, restart, Bluetooth or zone-set frame exists in the scripts. End configuration is sent
in a `finally` block and the stream is checked afterwards. The port was opened with pyserial's defaults (DTR and RTS
raised on open; the LD2452 has no reset line on J1, V, so nothing reacts to them, I).

### 3.2 Passive capture

Three 15 s listen-only captures at 9600 8N1, then a long one that waited for a person (M):

| Capture | Bytes | Data frames | Frames/s | Gap median / max | Bad frames | Targets 0/1/2/3 |
|---|---|---|---|---|---|---|
| 1 (coarse timing) | 8700 | 290 | 19.5 | - | 0 | 290/0/0/0 |
| 2 | 8667 | 288 | 19.3 | 51.8 / 53.2 ms | 0 (26 bytes of a first partial frame) | 288/0/0/0 |
| 3 | 8671 | 289 | 19.3 | 51.9 / 53.0 ms | 0 | 289/0/0/0 |
| long (`watch.py`, 642 s, stopped 3 s after 300 target frames) | 371757 | 12391 | 19.3 | - | 0 | 12034/357/0/0 |
| fixture: the best 15 s of the long one | 8700 | 290 | 19.3 | 52 / 55 ms | 0 | 29/261/0/0 |

578 bytes/s = 19.3 frames x 30 bytes: the radar sends a frame every 51.8 ms; a frame takes 31.25 ms on the wire at
9600 (10 bits per byte), so the line is 60 % busy and a frame rate above 32 Hz would not fit. No frame had a bad
footer in about 13,260 frames (about 11.5 minutes).

The room was empty until about 597 s into the long capture; then one person walked in (the fixture window), stood
near 1 m and moved again (M):

| Fact | Value |
|---|---|
| target frames | 357, all in slot 1; slots 2 and 3 always zero (one person) |
| x, y in the fixture | x -49 to +3289 mm, y +606 to +4896 mm (y never negative) |
| distance path | 5.5 m at 1.5 s, 3.3 m at 4.5 s, 2.1 m at 6.4 s, 1.0 m at 8.4 s, then 0.65-1.0 m standing |
| speed values | multiples of 17 cm/s only: -85, -68, -51, -34, -17, 0, +17 ... +136 (long capture) |
| speed sign | negative while the distance falls: 147 of 157 half-second checks agree (approaching negative, V confirmed) |
| step between frames | median 27 mm, 95 % under 99 mm, max 137 mm (52 ms apart): smooth, no jumps |
| resolution field | 330 mm in every target frame |
| target lost | none during the 14 s after the person appeared in the window |

### 3.3 Read-only identification

Four sessions on 2026-11-06 at 9600; the last one (`query-9600-run4`) is the clean sequence (times from opening the
port):

| t (ms) | Direction | Bytes | Meaning |
|---|---|---|---|
| 0-1019 | RX | 20 data frames | stream before (the command is sent right after a frame's footer) |
| 1019 | TX | `FD FC FB FA 04 00 FF 00 01 00 04 03 02 01` | enable configuration |
| 1064 | RX | `AA FF 03 00 00 00` + `FC FC FB FA 08 00 FF 01 00 00 01 00 40 00 04 03 02 01` | a data frame cut after 6 bytes, then the ACK: success, protocol `0x0001`, buffer 64; first byte `FC` instead of `FD` |
| 1065 | TX | `FD FC FB FA 02 00 A0 00 04 03 02 01` | read firmware version |
| 1065-2584 | RX | nothing | no answer |
| 2584 | TX | `FD FC FB FA 04 00 A5 00 01 00 04 03 02 01` | get MAC address |
| 2584-4091 | RX | nothing | no answer |
| 4092 | TX | `FD FC FB FA 02 00 91 00 04 03 02 01` | query tracking mode |
| 4092-5609 | RX | nothing | no answer |
| 5610 | TX | `FD FC FB FA 02 00 C1 00 04 03 02 01` | query zone filter |
| 5610-7122 | RX | nothing | no answer |
| 7123 | TX | `FD FC FB FA 02 00 FE 00 04 03 02 01` | end configuration (in `finally`) |
| 7162 | RX | `FD FC FB FA 04 00 FE 01 00 00 04 03 02 01` | success |
| 7162-9182 | RX | 38 data frames in 2.0 s | **stream resumed** at 19 Hz |

The earlier sessions:
- Session 1 sent enable, saw no ACK (my first parser waited for the 30 bytes of the cut data frame and so held the
  ACK back), skipped the queries and ended configuration: end ACK received, stream back (37 frames in 2 s).
- Session 2: the same with the frame-end alignment; the same result.
- Session 3: enable sent twice (the first ACK again held by the parser, the second came clean as `FD FC FB FA ...`
  because no data frame was being sent any more), then the four queries: silence; end ACK; stream back (37 frames).
- Session 4: the fixed parser; the table above.

So, in total, the sensor received: enable configuration 5 times, end configuration 4 times, and each of `A0`, `A5`,
`91`, `C1` twice (sessions 3 and 4). Nothing else was ever written. After each session the stream resumed at about
19 frames per second (37, 37, 37 and 38 frames in 2 s), and a later 15 s capture (3.2, capture 3) shows the normal
stream: the sensor was left in its working mode every time.

What this shows:
- The LD2452 understands the LD2450 command framing and its configuration mode (M).
- It does not answer the four LD2450 read queries, at least with this firmware (M). Firmware version, MAC and tracking
  mode could not be read; the Hi-Link IAP tool's "Get Firmware Information" (V, at 256000) uses another protocol that
  is not documented.
- The damaged first ACK byte appears only when a data frame was cut (sessions 1-4: `F8`, `F8`, `F8`, `FC`; a clean
  `FD` in session 3's second answer, sent with no data frame in progress) (M). Sending the command right after a
  frame's footer (sessions 2-4) did not avoid the cut: the radar answers 25-30 ms after the command's last byte,
  when the next frame (20 ms gap at 9600) has already started (M).
- 256000 and 115200 were not tried for the queries: the sensor answers at 9600, and a frame written at another speed
  only reaches it as noise.

### 3.4 Baud

The data stream and the command answers are at 9600 (M); the controller's earlier listen at 256000 and 115200 saw only
zero bytes, which is what a too-fast receiver sees from a 9600 line (start bits read as `00` with framing errors, I).
Both manuals give 256000 as the default (V); the LD2450 can be set to 9600 with `0xA1` index 1 (V). Possible reasons
(I): the seller of this kit (LD2452 "with CH340 board" listings exist) set it, or a firmware build defaults to 9600.
The app must find the speed itself (4.3) and remember it.

### 3.5 Fixture

`docs/dev/reports/056-assets/` (all M, recorded 2026-11-06 at 9600 8N1 on COM6):

| File | Content |
|---|---|
| `ld2452-9600-walk-15s.bin` | 8700 raw bytes = 290 whole data frames, starting at a frame header: 29 empty, then one person walking in from 5.5 m to 1 m and standing (3.2) |
| `ld2452-9600-walk-15s.csv` | the same frames decoded, one row per frame (13.7 KB) |
| `ld2452-9600-config-session.bin` | raw bytes received during session 4 (1778 bytes): 20 data frames, the frame cut after 6 bytes and the damaged enable ACK (`FC FC FB FA ...`), then silence during the four queries, the end ACK, 38 resumed data frames; the test for the resync rule (expected: 58 data frames, 2 ACKs, 1 cut frame, 0 bad footers) |

CSV columns: `frame, t_ms` (arrival time on the PC, relative to the first frame; USB delivery adds a few ms of
jitter), `targets`, then for slots 1-3: `x_mm, y_mm, dist_mm, angle_deg, speed_cms, res_mm` (empty when the slot is
empty). `angle_deg` = atan2(x, y), positive toward +x (2.3). LF line endings.

## 4. Detection on the boards

### 4.1 Ports

| Port | Boards | Scan | Notes |
|---|---|---|---|
| `ttyUSB*` | all | yes | CH340 (`ch341` driver), CP210x, FTDI, PL2303 bridges; the owner's kit is CH340 `1a86:7523` (M) |
| `ttyACM*` | all | yes, except a native microcontroller VID unless picked | CDC ACM; a CH9102 bridge also shows here (I) |
| `/dev/serial0` (GPIO 14/15) | Pi Zero 2 W, 3A+, 4B, 5, HackBerry CM5 | only when turned on in the app (D4) | `ttyS0` (mini UART) or `ttyAMA0` depending on the board and `config.txt` (I); never the target of `/dev/serial1` (the Bluetooth UART: opening it cuts the Bluetooth keyboard, as Serial Console notes, V) |
| `ttyS*` | tablet (x86) | no | 8250 slots without hardware (Serial Console skips type 0, V); the tablet has no GPIO UART |

Serial Console's `scan_ports()` (V: sysfs walk to the USB device, VID/PID, driver, `/dev/serial/by-id`) is the model;
the code is MIT in the same repo and can be reused or shared.

### 4.2 Not disturbing other apps

- **Skip a held port.** Open with `O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC`; `EBUSY` means another program set
  `TIOCEXCL` (Serial Console, Mesh Hop: V) -> skip. Then `flock(LOCK_EX | LOCK_NB)`; `EWOULDBLOCK` means Mesh Hop or
  another cooperating app holds it (V) -> close and skip. viz1090's GPS watcher opens ports read-only **without** a
  lock (V), so also skip a port that appears in `/proc/<pid>/fd` of any other process we can read (the deck user's
  apps; root daemons are not visible, I). Changing the speed of a port someone else reads would break their stream,
  because termios settings belong to the device, not to the open file (I).
- **Hold it while we use it.** After a successful classification keep `flock` + `TIOCEXCL` (so viz1090 and Serial
  Console get `EBUSY` instead of reconfiguring it), release both on close and on `hide` (D7).
- **Write nothing during detection.** Listening is enough (4.4); writing to an unknown port can disturb another
  device.
- **Opening raises DTR and RTS** on any USB serial port (Linux tty layer, I). The LD2452 board has no line wired to
  them (J1 is 5V/RX/TX/GND, V). A microcontroller board with auto-reset could restart when opened (I), which is why
  D3 recommends scanning bridge chips automatically and native microcontroller USB (Espressif `303a`, Adafruit
  `239a`, Seeed `2886`, Raspberry Pi `2e8a`, Arduino `2341`) only when picked; Serial Console and viz1090 already open
  such ports (V), so this is caution, not a new risk.

### 4.3 Speeds and time budget

Order: the remembered speed for this port identity; **256000** (the documented default, most modules ship so);
**9600** (the owner's unit); **115200**; then 57600, 38400, 19200, 230400, 460800 (the LD2450 table, V). 256000 has no
`B` constant on Linux: set it with `termios2`/`BOTHER`, as Serial Console's `tty_speed.cpp` does (V). Whether the
`ch341` driver and the Pi UARTs reach 256000 accurately is not verified (I; the CH340 is commonly used at 256000 with
these radars; the mini UART's rate follows the core clock).

| Step | Budget | Rule |
|---|---|---|
| first speed on a port | up to 600 ms | no byte at all in 600 ms: the port is silent, leave it (a UART that sends nothing sends nothing at any speed) |
| each next speed | up to 400 ms | at least 3 frames fit in 400 ms at 10 Hz; stop early on a decision |
| found | 3 valid frames in a row | header, footer, 30 bytes apart, plausible values (4.4) |
| wrong speed | bytes, but no frame in the budget; early: 90 bytes (3 frames' worth) without any header | next speed |

Expected times (I): the owner's sensor unknown to the app: 256000 (garbage: at most 0.6 s, about 0.1 s with the early
rule) then 9600 (3 frames in about 0.16 s at 19 Hz) = **0.3-0.8 s**; remembered: **about 0.2 s**; a silent port
0.6 s; a worst case talking port that is not a radar 8 speeds x 0.4 s = 3.2 s. The scan runs on a worker thread;
the UI shows "Looking at ttyUSB0 (CH340) at 256000..." and can be cancelled (guidelines: no blocking UI, V).

### 4.4 Classifier

Feed every byte through one scanner (pure function, unit tested), which recognises:

| What | Rule | Result |
|---|---|---|
| LD2450-family tracking frame | `AA FF 03 00`, 30 bytes, `55 CC`; values plausible: \|x\| <= 8000 mm, 0 < y <= 8000 mm, \|speed\| <= 1000 cm/s, resolution <= 2000 mm, or the whole slot zero (limits I; the walk stayed well inside them: y 606-4896 mm, speed up to 136 cm/s, resolution 330 mm, M) | **radar found** after 3 in a row |
| Command ACK | `FD FC FB FA`, length <= 64, `04 03 02 01` at the end; **also with a damaged first byte**: `?? FC FB FA` + valid length + footer (M, 3.3) | logged (only after our own commands, later phases) |
| Cut data frame | `FC FB FA` found inside the 30 bytes of a data frame that has no valid footer: drop up to the ACK (`FC FB FA` cannot occur in a valid frame: any 3 bytes include a high byte, and a high byte of `FA`-`FC` would mean more than 31 m or 312 m/s) | resync, count "cut frames" |
| GPS (NMEA) | `$...*hh` sentences with a valid checksum (`$GP`, `$GN` ...; viz1090's `nmea`, V) | "GPS receiver, not a radar" |
| GPS (u-blox UBX) | `B5 62` + valid checksum (Serial Console's classifier, V) | "GPS (UBX), not a radar" |
| LD2410-family presence radar | data frame `F4 F3 F2 F1` ... `F8 F7 F6 F5` (ESPHome `ld2410`, V); default 256000; same command framing | "LD2410 presence radar (not supported yet)" (D9) |
| LoRa / Meshtastic / MeshCore board | text logs, Meshtastic API frames `94 C3` (Serial Console, V); none of them makes 30-byte `AA FF 03 00 ... 55 CC` frames | "not a radar" |
| Zeros or garbage | bytes without any frame | wrong speed, try the next |

The enable/end handshake (2.4) would be a second check, but it writes to the port and stops the stream; the
listen-only rule above is already conclusive (three frames with two fixed headers and footers at exact spacing), so
the handshake is not part of detection (D2).

### 4.5 Hot-plug, permissions, other software

- **Hot-plug.** Watch `/dev` with `inotify` for new `ttyUSB*`/`ttyACM*` (fallback: list `/dev` every 2 s, viz1090
  rests 4 s between scans, V). A read error `EIO`/`ENODEV`/`ENXIO` means the sensor was unplugged: show "Sensor
  disconnected", scan again with its remembered identity first. Remember the identity as the `/dev/serial/by-id` name
  or `vid:pid` + USB path (the CH340 usually has no serial number, I).
- **Permissions.** The user needs the `dialout` group; the "not found" page says the exact fix (guidelines, V).
- **brltty** on Ubuntu-based images is known to claim CH340 `1a86:7523` devices so that `ttyUSB0` disappears (I,
  widely reported): check on the tablet's image during the board test.
- **ModemManager**, if installed, may send `AT` probes to new ports (I): harmless to the radar (not a valid frame),
  but it holds the port for a moment; the scan retries.

### 4.6 Other radars later

The LD2410 (presence: moving/still distance and energy per gate, no x/y) shares the command framing and the speed
table; recognising its frames costs little in 0.1 (D9), a view of its own is 0.2 material. The LD2451 (vehicle, 100
m) and other Hi-Link models use other frames (I) and are out of scope until someone owns one.

## 5. App shape (options and recommendation)

### 5.1 Name and packaging (draft)

- App "mmWave", id and package `mmwave` (D1); `apps/mmwave/` in the apps repo, built with the shared SCons and the
  toolkit `cp0_lvgl` at the current pin; `arch: ["arm64", "amd64"]`; `depends`: the base
  (`libfreetype6, libinput10, libxkbcommon0`); `categories: ["Tools"]`; `permissions: {"additional_hardware": true}`;
  MIT.
- `.desktop`: `X-Fullscreen=true`, `X-MinSize=480x320`, `X-Portrait=320x480`, `X-Background=freeze` (D7). The sizes
  follow report 054 section 7.2 (V): `X-MinSize` is the landscape minimum, so `480x320` keeps the app on the 480x320
  boards; `X-Portrait=320x480` is the smallest portrait canvas, so the upright canvas is used on zero8 in PDA mode and
  on the larger portrait panels; declare `X-Portrait` only once the scenes pass on 320x480, 480x640 and 480x800
  (APP-CONTRACT section 6, V). An older launcher ignores both, so the app checks its canvas and says "needs a 480x320
  screen" below that.
- Settings in `~/.config/mmwave/` (remembered port identity and speed, units, flip left/right, plot range).

### 5.2 Module layout

| Option | Content | For / against |
|---|---|---|
| A (recommended) | `src/core/` pure C++ without LVGL: `frame` (decoder, scanner with the resync rule, command frame builder with a read-only allowlist for later), `detect` (port list, lock checks, speed order, classifier state machine), `tracker` (slot to track association, trails, direction, stale timeout), `source` (an interface: serial reader thread, file replay at recorded timing, fake generator for demo and tests), `settings`; `src/ui/`: radar view, table, graph, sensor page, not-found page | the house pattern (Serial Console, Net Toolbox); all logic tested on the PC |
| B | A, with the decoder and detector shared with Serial Console (a "LD24xx radar" rule in its identify page) | Serial Console gains a rule (0.25 ws), but two apps change together; better as a later small task |
| C | the decoder in the toolkit `cp0_lvgl` | one user only; a launcher PR and a pin move for nothing |

The tracker (I): the radar's slots look like track identities, but that is not documented; associate each new target
with the nearest track of the previous frame within a gate (about 0.6 m per 100 ms), start a track after 2 frames,
drop it after 0.5 s without a match, keep 2 s of positions as a trail. Smoothing is optional: the recorded walk is
already smooth (median 27 mm, at most 137 mm between frames 52 ms apart, one person kept in slot 1, M).

### 5.3 What to show

| Data | Source | Format (proposal) |
|---|---|---|
| per target: x, y | frame | m, 2 decimals (mm in the data; accuracy 0.15 m, V) |
| distance, angle | derived | m, 2 decimals; degrees, integer (2.3) |
| speed | frame | m/s with 1 decimal (cm/s in the data, in steps of 17 cm/s, M), km/h as a setting (D5); sign shown as approaching/receding/still |
| resolution | frame | mm, sensor page or table only |
| sensor | detection | model "LD2450-family tracking radar (LD2450/LD2452)", port and USB identity ("ttyUSB0, CH340 1a86:7523"), speed, measured frame rate, bad and cut frame counters |
| parameters | the safe queries | **not in 0.1** (listen-only); a sensor that answers (an LD2450) could show firmware, MAC, mode and zones in a later phase; for this LD2452 the page says "not reported by this sensor" |

### 5.4 Screens and sizes (proposals; layout is a design pass)

| Page | Content |
|---|---|
| Radar (home) | top-down plot: sensor at the bottom centre, the +-60 deg wedge, range rings every 1 m (0-6 m; a 3 m zoom), up to 3 targets as coloured dots with a 2 s trail and a radial speed mark (a short tick along the line to the sensor, inward when approaching), a status chip (port, frame rate, number of targets) |
| Table | one row per target: x, y, distance, angle, speed, direction; an empty slot shown as empty |
| Graph | distance and speed of each target over the last 30-60 s (`lv_chart`) |
| Sensor | the sensor rows of 5.3, the counters, the wiring help |
| Not found | what was tried, port by port ("ttyUSB0 CH340: no data at any speed"; "ttyACM0: GPS (NMEA), not a radar"; "ttyUSB1: in use by another app"; "permission denied: add yourself to dialout ..."), the wiring table (5.6), Retry; hot-plug finds the sensor by itself |

| Canvas | Boards | Notes (proposal) |
|---|---|---|
| 640x480 | deck | reference; the wedge is 1.73 times wider than deep (6 m deep, 2 x 6 x sin 60 = 10.4 m wide), so it fills the width |
| 800x480, 720x720, 1280x800 | Pi 3A+ DSI, Pi 5 HyperPixel, HackBerry, tablet | the same with a side table if the design pass wants it |
| 480x320 | zero8, Pi 3A+ SPI | plot only on the home page (about 46 px per metre), the table on its own page |
| 320x480 | zero8 upright (PDA mode) | plot on top (320 px wide, about 31 px per metre, 190 px deep), the table under it: portrait suits this sensor well |
| 480x640, 480x800 | portrait DSI panels, tablet upright | as 320x480 with more room |

### 5.5 Refresh rate on a Zero 2 W

The radar sends 19 frames/s (M); decode all of them on the reader thread (30 bytes each: negligible) and redraw the
plot on an LVGL timer at **10 Hz** with the latest state, invalidating the plot area only. Ten redraws a second of
three dots, three short polylines and static rings (the rings drawn once into a cached layer) should cost well under
20 % of one core on the deck (I, to measure in the 0.1 board test; fall back to 5 Hz if not). The graph page adds
points at 5 Hz.

### 5.6 No sensor: wiring table for the "not found" page

| Radar J1 pin | USB-serial adapter (as the owner's CH340 board) | Raspberry Pi GPIO header |
|---|---|---|
| 1 5V | 5V | pin 2 or 4 (5 V) |
| 2 RX | TX | pin 8 (GPIO 14, TXD) |
| 3 TX | RX | pin 10 (GPIO 15, RXD) |
| 4 GND | GND | pin 6 (GND) |

Notes (V unless marked): 5 V supply, about 126 mA average (LD2452; the LD2450 asks for a supply able to give 200 mA);
UART at 3.3 V logic (LD2450 V, LD2452 I: check with a meter before wiring to a Pi); J1 is a 1.5 mm ZH connector. For
the Pi UART: `enable_uart=1`, no login console on it (raspi-config: serial login no, hardware yes), then turn the GPIO
UART on in the app (I, standard Pi setup). The app should also say: "the radar sees motion; it is a room sensor, mount
it still: on a moving deck everything moves" (V: the manual asks for a firm, still mounting).

### 5.7 Tests

| Kind | Content |
|---|---|
| Unit (PC) | decoder on the 056 fixture: frame count, zero bad frames, the decoded CSV as the golden file; the document examples (2.3); sign-magnitude edge cases (`00 00`, `00 80`, `FF 7F`, `FF FF`); the config-session fixture: 58 data frames, 1 cut frame, the damaged enable ACK and the end ACK recognised; byte-by-byte feeding equals whole-buffer feeding (done in this study's Python scanner, M); garbage and noise resync; classifier negatives (NMEA, UBX, LD2410 frames, text logs, zeros) |
| Fake serial | Serial Console's pseudo-terminal `sim` pattern (V): a fake radar streaming the fixture at 9600 timing, one at "256000", one that stops (unplug: close the master, expect `EIO`), a port held with `flock` (must be skipped), a GPS fake (NMEA) |
| Tracker | scripted walks (approach, cross, two people passing) from the fake generator; track ids stable, trails capped, stale tracks dropped |
| Render scenes | `_common/headless`: radar with 0/1/3 targets, table, graph, sensor, not found (with a list of tried ports), at 640x480, 480x320, 320x480, 800x480, 480x800, 720x720 |
| Board (owner, short) | plug the kit into the deck: found within 2 s; walk toward and away: approaching/receding right; step to one side: the dot moves to the same side (else the flip setting); unplug and replug; zero8 at 480x320 and upright |

### 5.8 Licences

Our code MIT, written from the Hi-Link documents and our measurements. ESPHome's components are GPL-3.0 (C++) and are
read for facts only; nothing is copied from them or from the LGPL LD2410 library. The MIT/BSD/Apache drivers in 2.1
are not needed either; if one is ever used, its notice goes into `copyright`.

## 6. Phases and cost

All costs are estimates (I), in worker sessions of about 0.4 M tokens.

| Phase | Worker | Content | Cost (ws) | Owner test |
|---|---|---|---|---|
| P0 (folded into 0.1) | - | one-person walk fixture: done (3.5); still wanted: two people and a sideways walk (the owner walks while `watch.py` runs on the PC: 1-2 minutes), the left/right check | 0 (the owner's minutes) | 2 min |
| 0.1 core | savvy-medium | `core/`: decoder and scanner, detector (ports, locks, speeds, classifier), tracker, sources (serial with `termios2`, replay, fake), settings; unit and pty tests | 0.75-1 | - |
| 0.1 UI | savvy-medium | radar, table, graph, sensor and not-found pages at all sizes of 5.4, keys/touch/pointer, scenes | 1-1.5 | 10 min: section 5.7 board row |
| 0.1 packaging | savvy-light | `.desktop`, `app.json`, icon, README, BOARD-TESTS, amd64 | 0.25 | - |
| Docs | docs-writer | README rows, app page | 0.25 | - |
| 0.2 (optional) | savvy-medium | record to a file and replay on the board (`X-Background=run` while recording), an LD2410 view, the Serial Console identify rule | 1-1.5 | 10 min |
| 0.3 (later, owner's word) | savvy-medium | settings for sensors that answer the read queries (LD2450): tracking mode, zone filter, with confirmations; never baud or factory reset from the app; the LD2452 only if Hi-Link's LD2452 command document is found | 0.75-1 | 10 min with an LD2450 |

**Totals (I):** MVP 0.1 + docs **2.25-3 ws (about 0.9-1.2 M tokens)**; through 0.2: 3.25-4.5 ws; through 0.3:
4-5.5 ws. The controller's design pass and reviews are extra.

## 7. Not in scope, and risks

- **No writes in 0.1.** No baud change, factory reset, restart, Bluetooth switch, tracking-mode or zone setting;
  not even the enable/end handshake (D2). Changing sensor settings is a later phase that needs the owner's word (0.3).
- **No firmware upgrade**, no Bluetooth, no multi-sensor setups, no automation or Home Assistant export in this plan.
- **No launcher change**: detection lives in the app (the launcher stays generic).
- **Risks.** The LD2452 command set is undocumented and partly unanswered (2.5); a later firmware may differ. The
  frame rate (19 vs 10 Hz) and the speed (9600 vs 256000) differ from the manual on this unit, so the app must
  measure, not assume. Left/right depends on the mounting (D6). A radar on a moving deck sees the room move (5.6).
  `brltty` may hide CH340 ports on the tablet (4.5).

## 8. Not verified

- The LD2452's behaviour at 256000 or other speeds (not tried, would need a baud change).
- Whether the LD2452 answers any other command word (only the documented LD2450 read queries were tried).
- Whether the radar keeps each of two or three people in the same slot (one person stayed in slot 1, M; two people
  not recorded).
- Plausibility limits for fast movement and the tracker gate with two or three people (one walking person is
  recorded, 3.2).
- The sign of x on screen with a real mounting (D6).
- `ch341` and the Pi UARTs at 256000 on the boards; `brltty`/ModemManager on our images.
- CPU of a 10 Hz plot on the deck.
- The LD2452's I/O voltage (3.3 V assumed from the LD2450).

## 9. Questions for the owner

1. **Name:** "mmWave" (`mmwave`), or another name? (D1)
2. **More fixtures:** one person walking in is recorded (3.5). Could you add a sideways walk and, if possible, two
   people, while `python C:\CLAUDE\zero7\mmwave-work\scripts\watch.py COM6 9600 120 300 C:\CLAUDE\zero7\mmwave-work\captures\walk`
   runs on the PC (listen-only, stops by itself)? A note of which side you stepped to also settles left/right (D6).
3. **Where did the kit come from**, and was it ever set up with the Hi-Link PC tool? (It runs at 9600 and 19 Hz, not
   the manual's 256000 and 10 Hz.)
4. **Use case:** a room sensor on a desk or wall that the deck reads, or mounted on the deck itself? (A moving radar
   sees everything move.)
5. **Listen-only in 0.1** (D2): fine, or do you want the enable/end handshake for the "protocol 1" line?
6. **Port scan** (D3): automatic on bridge chips only, native microcontroller boards only when picked?
7. **Units** (D5): metres and m/s, or km/h, or cm?
8. **GPIO UART** (D4): will the sensor ever be wired to a Pi header, and on which board?
9. **Settings later** (D8, phase 0.3): wanted at all? For this LD2452 it would need Hi-Link's LD2452 command document
   (none found online); should I ask for it or look for it again later?
10. **LD2410** (D9): do you have one, or plan to?

## Sources

Read 2026-11-06.

- Hi-Link, *HLK-LD2452 User Manual* V1.3 (2025-04-09):
  https://ae01.alicdn.com/kf/Sac807194393446d2a41e91e063e60dd84.pdf (V: specifications, pins, section 5 data frame,
  figures 5-1 and 7-3)
- Hi-Link, *HLK-LD2450 Motion Target Detection and Tracking Module Serial Communication Protocol* V1.03 (2023-10-17):
  https://make.net.za/wp-content/datasheets/HLK%20LD2450%20Serial%20Communication%20Protocol%20v1.03.pdf (V: every
  command frame and ACK in 2.4, defaults, data frame)
- Hi-Link, *LD2450 Serial Communication Protocol* V1.02 EN:
  https://assets.super.so/79c0d2a8-d37a-438f-8fbe-c44778f3b0dd/files/ee2aafb0-f93e-47cd-b722-ad0f9e76b6f7/LD2450_Serial_Communication_Protocol_V1.02_EN.pdf
- ESPHome `ld2450` component (GPL-3.0 C++, facts only):
  https://github.com/esphome/esphome/blob/dev/esphome/components/ld2450/ld2450.cpp ; licence:
  https://github.com/esphome/esphome/blob/dev/LICENSE
- ESPHome `ld2410` and `ld24xx` (GPL-3.0 C++, facts only):
  https://github.com/esphome/esphome/blob/dev/esphome/components/ld2410/ld2410.cpp ,
  https://github.com/esphome/esphome/blob/dev/esphome/components/ld24xx/ld24xx.h
- ESPHome LD2450 documentation: https://esphome.io/components/sensor/ld2450/
- RBEGamer/HLK-LD2450 (MIT): https://github.com/RBEGamer/HLK-LD2450
- csRon/HLK-LD2450 (MIT): https://github.com/csRon/HLK-LD2450
- Fiooodooor/HLK-LD245X (BSD-3-Clause): https://github.com/Fiooodooor/HLK-LD245X
- duramson/ld2450-rs, crate `ld2450-proto` (MIT OR Apache-2.0): https://docs.rs/crate/ld2450-proto/0.2.0
- ncmreynolds/ld2410 (LGPL-2.1, named only): https://github.com/ncmreynolds/ld2410
- Our code (V): apps `origin/main` @ `bec05f3` (`apps/mesh-hop/src/core/serial_transport.cpp`,
  `apps/serial-console/src/core/{ports,probe,identify}.hpp`, `probe.cpp`, `serial_io.cpp`, `tty_speed.cpp`,
  `apps/viz1090/src/core/gps.{hpp,cpp}`, `docs/APP-GUIDELINES.md`); launcher `github/main` @ `d06faf54`
  (`docs/APP-CONTRACT.md`, `docs/dev/reports/054-offline-maps-wardriving-study.md`).
