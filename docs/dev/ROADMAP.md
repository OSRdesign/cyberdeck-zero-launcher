# Roadmap

Owner of this file: the Controller. Status: `idea` > `brief` > `building` > `verifying` > `accepting` > `done`.

## Now

| # | Task | Owner | Status | Brief |
| --- | --- | --- | --- | --- |
| 001 | **Wi-Fi survey** app (pilot of the agent setup) | apps-dev | done | `tasks/001-wifi-survey.md` |
| 002 | **LanScan source move** to the apps repo (lanscan 0.1.1) | apps-dev | accepting (verifier PASS, report 006; waits for the user's touch test) | `tasks/002-lanscan-move.md` |
| 003 | **viz1090 0.1.1**: touch screen found by capabilities | apps-dev | accepting (verifier PASS, report 006; waits for the user's finger test) | `tasks/003-viz1090-touch.md` |

## Next (ideas, order not fixed)

| Task | Owner | Notes |
| --- | --- | --- |
| Follow-up of 002: remove `projects/LanScan` and its references (`.gitignore`, `docs/HOSTING-APPS.md`, `docs/dev/DEV-GUIDE.md`, `projects/APPLaunch/pizero2w/README.md`, `projects/APPLaunch/pizero2w/bundle/README.md`), then confirm the launcher still builds | launcher-dev | after the user accepts 002; removes the last app source from the launcher repo |
| Add a copyright / licence notice to the lanscan package for the bundled IEEE OUI list (`oui.tsv`) | apps-dev | seen in report 006: the package has no `usr/share/doc/lanscan/copyright` |
| Fix `deck.py sudo` so the password is not on the remote command line (see decisions.md) | Controller | known issue |
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

2026-10-04: LanScan source moved into the apps repo (`apps/lanscan/src`, lanscan 0.1.1) and viz1090 0.1.1 (touch
screen found by capabilities and name, with retry, instead of a fixed `/dev/input/event1`) passed deck verification
(report 006) and are documented in the apps README. Both are installed on the deck; real touch acceptance by the
user is pending.
