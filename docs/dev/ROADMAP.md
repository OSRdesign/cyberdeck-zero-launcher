# Roadmap

Owner of this file: the Controller. Status: `idea` > `brief` > `building` > `verifying` > `accepting` > `done`.

## Now

| # | Task | Owner | Status | Brief |
| --- | --- | --- | --- | --- |
| 001 | **Wi-Fi survey** app (pilot of the agent setup) | apps-dev | done | `tasks/001-wifi-survey.md` |

## Next (ideas, order not fixed)

| Task | Owner | Notes |
| --- | --- | --- |
| Move the LanScan source from the launcher repo into the apps repo (`apps/lanscan/src`), same build method as the Wi-Fi survey | apps-dev | removes the last app source from the launcher repo |
| Keep ADS-B Radar (native LVGL app, untracked in `projects/AdsbRadar`) for later, or drop it; viz1090 covers the display | Controller + user | user said "keep for later" |
| LoRa messaging (Meshtastic / MeshCore over a USB board) | apps-dev | needs the user's firmware choice and hardware |
| Packet capture viewer | apps-dev | PacketScope from the Store covers part of it |
| Drift check of `cp0_statusbar.*` between the launcher and `apps/viz1090/build` | deck-verifier | part of every release check |
| Launcher: Settings > Apps polish (upgrade flow, per-source sync progress) | launcher-dev | |
| Docs: one screenshot per shipped app in the apps README | docs-writer | |

## Done (recent)

v0.1.0 first port; v0.2.0 Settings > Apps + LAN Scan; v0.3.0 full-screen apps, shared top bar, clean boot; viz1090
as an installable app (apps repo).

2026-10-04: Wi-Fi Survey 0.1.0 passed deck verification (report 005), documented in the apps README, and accepted by
the user on the deck.
