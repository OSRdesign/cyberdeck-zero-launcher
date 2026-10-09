# 019 Mesh Hop 0.3.0 (phase 3 part 1) - hand-off

```
Result: built and tested on the PC; NOT run on the deck, NOT tried with a real board. Nothing committed, pushed, deployed or published.
Package: C:\CLAUDE\zero7\pkg-out\mesh-hop_0.3.0_arm64.deb  (1 216 366 bytes, sha256 60505409fb964f989fab3ea40b4e45339ac646329f825fc719d8a7ab23e9fd28)
Deck test: tests/mesh-hop-0.3.0-deck-tests.md (21 steps, board B)
```

## What changed (apps repo, apps/mesh-hop, uncommitted)
* `core/logdata.*`: parser of the board's radio log (PUSH_LOG_RX_DATA), shared by the two features below.
* "Heard back by N repeaters" on own channel messages (`client.cpp`, `model.*`, `store.*`: final count saved as `"hb"` in `messages.jsonl`).
* `core/packet_log.*`: D8 packet log, Settings > Packet log (start / stop / clear, row detail with path and hex, 500 packets in memory).
* `core/position.*` and `client set_position`: Position box (manual lat/lon, Clear, GPS switch only when the firmware lists `gps`).
* Simulator options and scripts (`heard-back`, `packet-log`, `position`, `position-nogps`), `tools/board_probe.py`, README.
* Version 0.3.0 in `app.json`, `ui/app.cpp` (kVersion) and the README header. Registry, other apps, top README untouched.

## How verified
* `build/build.sh` (aarch64) and `tools/make_registry.py --only mesh-hop --out C:/CLAUDE/zero7/pkg-out`; nothing outside apps/mesh-hop changed.
* `src/tests/run_tests.sh`: core 2469, simulator 111, UI logic 958 checks, 0 failed.
* Simulator screenshots in `C:\CLAUDE\zero7\pkg-out\` (heard-back-*, packet-log-*, position-box-*), from the previous worker.

## Untested on hardware
Everything new: real LOG_DATA frames from fw 1.15.0, echo matching with real repeaters, SET_ADVERT_LATLON on board B, packet log
scroll speed with 500 rows on the Pi Zero 2 W, install over 0.2.2 with real data.

## Known limits
* Heard-back is an approximation: the app cannot decrypt, it matches channel hash and packet size and counts distinct routes. A count still moving when the app quits is lost.
* The GPS switch cannot be tested on board B (no `gps` variable); only the hidden case is covered on the deck.
* Packet log not saved or exported.
* The README's packet log paragraph sits in the "New in 0.2.0" list (it says "phase 3" inline); the top README still lists 0.2.2 (right until 0.3.0 is published).
