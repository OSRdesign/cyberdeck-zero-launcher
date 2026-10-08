# Roadmap

Owner of this file: the Controller. Status: `idea` > `brief` > `building` > `verifying` > `accepting` > `done`.

## Now

| # | Task | Owner | Status | Brief |
| --- | --- | --- | --- | --- |
| 001 | **Wi-Fi survey** app (pilot of the agent setup) | apps-dev | done | `tasks/001-wifi-survey.md` |
| 002 | **LanScan source move** to the apps repo (lanscan 0.1.1) | apps-dev | done | `tasks/002-lanscan-move.md` |
| 003 | **viz1090 0.1.1**: touch screen found by capabilities | apps-dev | done | `tasks/003-viz1090-touch.md` |
| 005 | **LanScan 0.1.2**: copyright file for the bundled IEEE OUI list | apps-dev | done | `tasks/005-lanscan-copyright.md` |
| 006 | **Settings > Apps polish** (tile order kept on upgrade, upgrade flow, sync progress, install feedback) | launcher-dev | done | `tasks/006-settings-apps-polish.md`, `reports/010.md` |

## Next (ideas, order not fixed)

| Task | Owner | Notes |
| --- | --- | --- |
| Follow-up of 002: remove `projects/LanScan` and its references (`.gitignore`, `docs/HOSTING-APPS.md`, `docs/dev/DEV-GUIDE.md`, `projects/APPLaunch/pizero2w/README.md`, `projects/APPLaunch/pizero2w/bundle/README.md`), then confirm the launcher still builds | launcher-dev | **done 2026-10-04** (task 004, `reports/004-handoff.md`): `projects/LanScan` removed, references cleaned, launcher builds; merged in pull request #4 |
| LoRa messaging (Meshtastic / MeshCore over a USB board) | apps-dev | MeshCore chosen: **Mesh Hop** (apps repo, `mesh-hop` 0.2.2, draft, not yet in the registry), see "Done (recent)" and `tasks/010`, `tasks/011`; next phases (BLE link, map, terminal) not started |
| Packet capture viewer | apps-dev | PacketScope from the Store covers part of it |

## Done (recent)

v0.1.0 first port; v0.2.0 Settings > Apps + LAN Scan; v0.3.0 full-screen apps, shared top bar, clean boot; viz1090
as an installable app (apps repo).

2026-10-04: Wi-Fi Survey 0.1.0 passed deck verification (report 005), documented in the apps README, and accepted by
the user on the deck.

2026-10-04: LanScan source moved into the apps repo (`apps/lanscan/src`, lanscan 0.1.1) and viz1090 0.1.1 (touch
screen found by capabilities and name, with retry, instead of a fixed `/dev/input/event1`) passed deck verification
(report 006) and are documented in the apps README. Both are installed on the deck and accepted by the user
(real touch works on both). `projects/LanScan` was removed from the launcher repo.

2026-10-05: LanScan 0.1.2 adds a Debian copyright file (MIT, cp0_lvgl, IEEE OUI data source and shortening noted) to the
package; verified on the deck (report 007). The apps README now has one child page per app, with screenshots for each.

2026-10-05: `docs/dev/deck/check_statusbar_drift.py` checks the apps' copies of `cp0_statusbar.[ch]` against the launcher (exit 0 =
identical); it is part of the verification and release checklists. No drift found today.

2026-10-05: ADS-B Radar (native LVGL app, never published) dropped by the user; viz1090 is the ADS-B app. The source was
archived outside the repos (`C:\CLAUDE\zero7rchive\AdsbRadar-source.zip`, build output left out) and the untracked
`projects/AdsbRadar` folder deleted.

2026-10-05: Settings > Apps polish (task 006) passed deck verification (report 010) and was accepted by the user: tile
order kept on upgrade, installed vs available version with Update / Update all, per-source sync progress with
failure reasons, install/remove feedback, letter shortcuts U/S/A/D. This changes the launcher binary
(`M5CardputerZero-APPLaunch`); the Store backend is unchanged. Documented in `docs/HOSTING-APPS.md`.

2026-10-07 to 2026-10-09: **Mesh Hop** (`mesh-hop`, apps repo, tasks 010 and 011, reports 013 to 018): a full-screen 640x480 MeshCore client for a companion radio board on USB. Phase 1 (0.1.x: chats, contacts, settings, radio presets), phase 1b (conversation options, history deletion, preset list refresh) and phase 2 (0.2.x: message search, contacts select and groups, Nearby, statistics, path hash size, repeat, scheduled advert, per-board data, factory reset) are built and tested against the simulator and, for the earlier versions, on the deck by the user. Still `"draft": true`: not in `registry.json`, installed as a local `.deb`. Map and Terminal are placeholders and there is no BLE link yet. Documented on `apps/mesh-hop/README.md` in the apps repo (status `accepting`).
