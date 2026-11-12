# Reports index

The reports are numbered in order of writing. A report is a study (read-only, options and a recommendation), a hand-off
(what a builder changed and how it was checked), or a verification verdict. This index lists the reports that are in
this directory on `main`, then the numbers that other files cite but that are **not** in the repository. Written
2026-11-12 from `git` and from the files; the status of a study is in the report's own first lines.

Tasks (the briefs) are in [`../tasks/`](../tasks/); each task file starts with its status. The roadmap is
[`../ROADMAP.md`](../ROADMAP.md); owner decisions are in [`../decisions.md`](../decisions.md).

## Reports on main

Reports 001 to 019 are mostly task hand-offs (`NNN-handoff.md`, by the builder) and verification verdicts (`NNN.md`, by
`deck-verifier`, starting with `VERDICT: PASS` or `FAIL`); their numbers are not the task numbers. The folders `002` to
`012` hold the screenshots of the verdicts.

| File | Subject (first line of the file) |
| --- | --- |
| [`001-handoff.md`](001-handoff.md) | Task: 001 Wi-Fi survey (round 2, after report 004)        Agent: apps-dev        Result: done (built; not run on the deck) |
| [`002-handoff.md`](002-handoff.md) | Task: 002 lanscan-move        Agent: apps-dev        Result: done (built, not verified on the deck) |
| [`002.md`](002.md) | VERDICT: PASS (limited scope: launcher binary deploy and smoke test only) |
| [`003-handoff.md`](003-handoff.md) | Task: 003 viz1090 touch fix        Agent: apps-dev        Result: done (built, not tested on the deck) |
| [`003.md`](003.md) | VERDICT: FAIL (one visible UI defect, README missing; install/remove and data path are fine; keys/touch/Channels screen still need the user) |
| [`004-handoff.md`](004-handoff.md) | Task: 004 remove projects/LanScan (follow-up of 002)        Agent: launcher-dev        Result: done |
| [`004.md`](004.md) | VERDICT: FAIL (dBm cell is still truncated in every row, "dBm" is never visible; R key switches tabs instead of only refreshing; the self-opening detail view did not reproduce; install, launch, Channels screen and removal are fine) |
| [`005-handoff.md`](005-handoff.md) | hand-off (apps-dev): LanScan 0.1.2, copyright file |
| [`005.md`](005.md) | VERDICT: PASS (all round 2 fixes confirmed on the deck: dBm fully readable, column titles and 17 px rows not clipped, R only rescans, detail follows the BSSID and shows "- gone". Touch items and the real Bluetooth keyboard are left to the user. One unexplained deck reboot during the run, see Incident.) |
| [`006-handoff.md`](006-handoff.md) | Hand-off 006 - Settings > Apps polish (round 3, after report 009) |
| [`006.md`](006.md) | VERDICT: PASS (A: LanScan 0.1.1 behaves like 0.1.0; B: viz1090 0.1.1 opens the Goodix touch device by scanning, retries after absence, keyboard/close/Esc intact. Real finger touch and a real Bluetooth keyboard present at launch were not testable by me, see "Needs the user".) |
| [`007-handoff.md`](007-handoff.md) | MeshCore: hand-off, ROUND 4 (rounds 3, 2 and 1 are kept below) |
| [`007.md`](007.md) | VERDICT: PASS (LanScan 0.1.2 installs over 0.1.1, copyright file present, no behaviour change; Settings > Apps upgrade UI and real touch not testable by me) |
| [`008-handoff.md`](008-handoff.md) | hand-off: Esc rule in LAN Scan and Wi-Fi Survey |
| [`008.md`](008.md) | VERDICT: FAIL (Goal 1, 2 and 4 work on the deck. Goal 3 is only partly met: the real backend reports a broken source with exit code 0, so the live row says "done", the status says "Synced N sources", and "HTTP 404" / "Connection failed" can never appear. The `U` key and the other letter shortcuts do nothing from a keyboard. Details in "Findings for the owner".) |
| [`009-handoff.md`](009-handoff.md) | hand-off: MeshCore app renamed to MeshZero (draft, unpublished) |
| [`009.md`](009.md) | VERDICT: FAIL (round 2 fixes 2, 3, 4, 5 work on the deck; fix 1 is half done: the sync now fails correctly and shows "Synced N of M", but the reason is wrong: a 404 shows "Connection failed" because the curl probe never runs. Rolled back to 15df070a...) |
| [`010.md`](010.md) | VERDICT: PASS (round 3: every sync reason shows correctly and fits the row, the final status colour is right on the first frame; round-2 regression and unit tests pass; new launcher left deployed) |
| [`011.md`](011.md) | VERDICT: FAIL (meshcore 0.1.1: 3 defects to send back to apps-dev: Del key never removes a channel; the on-screen keyboard is cut off at the bottom of the app window; the copyright file has a paragraph without `Files:`. Everything else on the simulator and on the real board passed, the real board ended unchanged.) |
| [`012.md`](012.md) | VERDICT: FAIL (meshcore 0.1.2: one defect to send back: the "Keyboard needed" message can never appear on the deck, because the launcher itself creates a full virtual keyboard whenever an app runs. Everything else passed: no on-screen keyboard anywhere, typed editors, Del, marker cleanup, read-only real board unchanged.) |
| [`013-handoff.md`](013-handoff.md) | Mesh Hop phase 1 - hand-off (apps-dev) |
| [`013-user-feedback.md`](013-user-feedback.md) | Mesh Hop 0.1.0 - user feedback after the first test on the deck (2026-10-07) |
| [`014-feasibility.md`](014-feasibility.md) | Mesh Hop feature feasibility (companion radio USB protocol) |
| [`015-handoff.md`](015-handoff.md) | Mesh Hop 0.1.1 (roadmap phase 1) - hand-off (apps-dev) |
| [`016-handoff.md`](016-handoff.md) | Mesh Hop 0.1.2 (roadmap phase 1b) - hand-off (apps-dev) |
| [`017-handoff.md`](017-handoff.md) | Mesh Hop 0.2.0 (roadmap phase 2) - hand-off (apps-dev) |
| [`018-handoff.md`](018-handoff.md) | Mesh Hop 0.2.2 (fix round after the deck test of 0.2.0 / 0.2.1) - hand-off (apps-dev) |
| [`019-mesh-hop-0.3.0.md`](019-mesh-hop-0.3.0.md) | Mesh Hop 0.3.0 (phase 3 part 1) - hand-off |
| [`020-virtual-keyboard-evaluation.md`](020-virtual-keyboard-evaluation.md) | On-screen keyboard as a fallback when no physical keyboard is present: feasibility |
| [`021-multi-sbc-multi-screen.md`](021-multi-sbc-multi-screen.md) | Launcher on several boards and capacitive screens (feasibility) |
| [`022-responsive-shell-study.md`](022-responsive-shell-study.md) | Responsive launcher shell (option B): architecture study |
| [`023-p5-app-contract-plan.md`](023-p5-app-contract-plan.md) | P5 app contract: investigation and plan |
| [`024-apps-platform-study.md`](024-apps-platform-study.md) | Small native cyberdeck apps: platform, template, guidelines, plan |
| [`025-rebranding-plan.md`](025-rebranding-plan.md) | Rebranding to "CyberdeckZero Launcher" (study summary, decisions and plan) |
| [`026-multitasking-study.md`](026-multitasking-study.md) | Multitasking: keep apps alive, show what runs, switch with a gesture or a key |
| [`027-net-toolbox.md`](027-net-toolbox.md) | Net Toolbox: a network toolbox app with live, visual tools |
| [`028-x86-port.md`](028-x86-port.md) | x86 (amd64) port of the whole stack: launcher, Settings, Store backend, apps |
| [`029-upstream-decoupling.md`](029-upstream-decoupling.md) | Decoupling from the upstream CardputerZero launcher: remove the code we do not use |
| [`032-apps-status-strip.md`](032-apps-status-strip.md) | The status strip in every app: battery everywhere (A), Wi-Fi and Bluetooth on the FZ-M1 (B) |
| [`033-mesh-hop-cli.md`](033-mesh-hop-cli.md) | A CLI / terminal for Mesh Hop: what it would be and how to build it |
| [`035-board-state-and-app-sdk.md`](035-board-state-and-app-sdk.md) | board.state, app-to-launcher requests and a mini app SDK |
| [`036-mesh-hop-repeater-ux.md`](036-mesh-hop-repeater-ux.md) | Mesh Hop: managing a repeater from the contact list (picker, login, read-only view) |
| [`037-notes-app.md`](037-notes-app.md) | A Markdown notes app for the deck: what to build, what to drop |
| [`038-pda-profile-study.md`](038-pda-profile-study.md) | PDA profile: a portrait launcher with a "Strokes" writing zone |
| [`039-strokes-s1.md`](039-strokes-s1.md) | Strokes S1: the recogniser core (hand-off) |
| [`040-pda-s2.md`](040-pda-s2.md) | PDA profile, phase S2: a portrait launcher, no writing zone |
| [`041-pda-s3.md`](041-pda-s3.md) | PDA profile, phase S3: the writing zone |
| [`042-strokes-training.md`](042-strokes-training.md) | PDA profile: Strokes training (S6, brought forward) and the writing zone on demand |
| [`043-pda-large-screens.md`](043-pda-large-screens.md) | PDA mode on large screens (short note) |
| [`044-apps-with-the-band.md`](044-apps-with-the-band.md) | apps with the band (PDA S4), plan |
| [`046-home-grid-app-order.md`](046-home-grid-app-order.md) | Home grid app order: drag and drop on the grid, or an order list in Settings |
| [`054-offline-maps-wardriving-study.md`](054-offline-maps-wardriving-study.md) | Offline maps with an external GPS, and wardriving (study) |
| [`055-maps-p0-results.md`](055-maps-p0-results.md) | Deck Atlas P0 spike: results (generated tiles) |
| [`056-mmwave-sensor-study.md`](056-mmwave-sensor-study.md) | mmWave: an app for the Hi-Link HLK-LD2452 tracking radar (study and probe) |
| [`058-sound-recorder-study.md`](058-sound-recorder-study.md) | Sound recorder app (study) |

## Cited but not in the repository

Other files cite these numbers. Nothing here is invented: where a location could
not be established from git, this says so.

| No. | Subject | Where it is known to live |
| --- | --- | --- |
| 030 | Board capabilities (sound, battery, brightness) and run-time detection (v0.8.4) | git tag `archive/report-030-capabilities` (commit `a483e3c5`, 2026-10-22), file `docs/dev/reports/030-board-capabilities.md`; read it with `git show archive/report-030-capabilities:docs/dev/reports/030-board-capabilities.md` |
| 031 | Smarter installer for unknown boards (ROADMAP: "report 031 when written") | never written: no file or git ref found |
| 045 | The study of an emulator of old handheld devices (parked; ROADMAP "Exploration ideas") | apps repo, tag `archive/cloudpilot-study` |
| 047 | Updates: the launcher updates itself, and apt packages with protection (cited in many files, v0.8.13 to v0.9.2) | cited, not in the repository: no git ref has it. It exists as an untracked local file `docs/dev/reports/047-launcher-updates.md` on the owner's machine only (study of 2026-10-30) |
| 048 | The Store, the legacy apps and the `small-screen` branch (study, 2026-10-31) | cited, not in the repository: no git ref has it. A local file `048-store-and-small-screen.md` exists on the owner's machine (folder `pkg-out-store-study` of the working area; the same folder holds `049-app-fork-classification.md`, which nothing cites) |
| 057 | Reticulum on the cyberdeck (study, 2026-11-07; decisions D1-D19 accepted) | branch `study-reticulum` (pull request #148, not merged), file `docs/dev/reports/057-reticulum-study.md` |
| - | `portrait-320-study.md`: portrait at 320x480 on the zero8 (study of 2026-11-03; cited in ROADMAP, v0.9.4) | cited, not in the repository: no git ref has it. A local file on the owner's machine (folder `pkg-out-zero8` of the working area) |
| - | `brightness-zero8-study.md`: brightness on the zero8 (study of 2026-11-04; cited in ROADMAP, v0.9.4) | cited, not in the repository: no git ref has it. A local file on the owner's machine (folder `pkg-out-zero8` of the working area) |

Numbers with neither a file here nor a citation: 034, 049 to 053 (049 exists only as the local file named above).
The git tag `archive/pda-s7-study` also exists in this repository (a snapshot with report 043, the PDA large-screen study, see
report 043); it is not a numbered report.

## Adding a report

Take the next free number, write `docs/dev/reports/NNN-name.md` with a first line `# NNN - title`, and add a row here.
Name no file `release-notes-*.md` in `docs/dev` unless it is a release note: the unit test of Settings > Updates reads every
`docs/dev/release-notes-*.md`.
