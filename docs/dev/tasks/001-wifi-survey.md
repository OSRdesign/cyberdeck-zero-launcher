# 001 - Wi-Fi survey (pilot)

Status: accepting (verifier PASS in report 005; README entry done; waiting for the user, who still has to try touch
and the real Bluetooth keyboard).

Owner: apps-dev. Verifier: deck-verifier. Docs: docs-writer. Acceptance: the user, on the deck.

## Goal
A native touch app that lists the Wi-Fi networks around the deck and shows which channels are crowded. Passive
only (scan results from the OS; no injection, no deauth, no capture).

## Screens (320x170 drawing scaled 2x into the launcher's 640x340 window with the launcher top bar; decided by the user, no X-Fullscreen)
1. **Networks**: list sorted by signal. Per row: SSID (or "hidden"), signal bar + dBm, channel, band (2.4 / 5 GHz),
   security (Open / WPA2 / WPA3...), a mark on the network the deck is connected to. Touch or arrow keys scroll;
   Enter / tap a row opens a detail view (BSSID, vendor from the OUI table LanScan uses, frequency, rate).
2. **Channels**: bar chart of networks per channel for 2.4 GHz (1-13) and 5 GHz, highlighting the least crowded
   channel. Switch between screens with Tab / left-right / on-screen tabs.
3. Refresh every ~5 s and on demand (R key / button). Esc quits.

## Technical notes
- Data source: `nmcli -t -f ... device wifi list --rescan yes` (fallback `iw dev wlan0 scan` is not allowed:
  it needs root). Handle "no Wi-Fi adapter" and "scan busy" with a visible message, never a crash.
- Source in `apps/wifi-survey/src` of the apps repo, a build script that copies it into a launcher worktree
  tree (like `projects/LanScan`), runs scons under the build lock and copies the binary into `root/`.
- Package `wifi-survey`, share code `wifi-survey`, title "Wi-Fi Survey", category Network, license MIT,
  depends `network-manager`. Icon 64x64 in the style of the existing app icons.
- Reuse the LanScan page structure, key handling (keyboard fd may be absent when the BT keyboard sleeps) and OUI table.

## Definition of done
`checklists/app-packaging.md` done; `checklists/verification.md` PASS on the deck including install and remove from
Settings > Apps; screenshots of both screens; README entry; the user accepts on the deck.

## Out of scope
Capturing packets, connecting to networks, saving history (later ideas).
