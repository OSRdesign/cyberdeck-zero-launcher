# 008 hand-off: Esc rule in LAN Scan and Wi-Fi Survey

Apps repo `C:\CLAUDE\zero7\cyberdeck-zero-apps`, branch `meshcore`, nothing committed or pushed, meshcore work untouched.

## Changed files (apps repo)
- lanscan: `apps/lanscan/src/main/ui/{lanscan.cpp,lanscan.hpp}`, new `esc_policy.hpp`, new `src/tests/test_esc.cpp`, `README.md`, `app.json` (0.1.3), rebuilt binary in `root/`
- wifi-survey: `apps/wifi-survey/src/main/ui/{wifisurvey.cpp,wifisurvey.hpp}`, new `esc_policy.hpp`, new `src/tests/test_esc.cpp`, `README.md`, `app.json` (0.1.1), rebuilt binary in `root/`
- `README.md` (version column), `registry.json` (lanscan and wifi-survey entries + generated_at only), new `packages/lanscan_0.1.3_arm64.deb`, `packages/wifi-survey_0.1.1_arm64.deb`

## Esc behaviour (short press; hold 3 s is the launcher's, untouched)
| App | Screen | Short Esc |
| --- | --- | --- |
| LAN Scan | Hosts | Nothing; footer shows "Hold Esc 3 s to exit" for 2.5 s |
| LAN Scan | Ports | Back to Hosts |
| Wi-Fi Survey | Networks | Nothing; footer hint 2.5 s |
| Wi-Fi Survey | Channels | Nothing; footer hint 2.5 s |
| Wi-Fi Survey | Detail | Back to the tab it came from |

No `navigate_home` or `g_quit` is set from Esc any more (`wifisurvey_quit_requested()` kept for main, never true now). Top-level footers say "Hold Esc: exit"; Ports and Detail footers keep "Esc: back".

## Packages (md5)
- lanscan_0.1.3_arm64.deb 05d9ef668cbdf4db7f0c7622a153c4a9
- wifi-survey_0.1.1_arm64.deb 53626f14d5a5d710c0c71ac82b3cc4ea
- Restored byte-identical from backup: viz1090 0.1.0/0.1.1, meshcore 0.1.3 (95928f50...), old lanscan 0.1.0-0.1.2 and wifi-survey 0.1.0 (the repo keeps old debs, so they stay).
- registry.json: lanscan/wifi-survey entries from make_registry, everything else (viz1090, meshcore, order) as before; LF endings, no CR.

## What I ran
- Host test per app (`g++ -std=c++17 -I../main/ui test_esc.cpp`, in WSL): both print "ok" (pure function (view, Esc) -> hint or next view).
- `apps/<id>/build/build.sh` for both (WSL, under the build lock): aarch64 builds clean.
- `tools/make_registry.py`, then restored the other debs and registry entries from a backup (`C:\CLAUDE\zero7\tmp008`, outside the repo, can be deleted).
- Not run on the deck (no deck-verifier by default).

## Manual test (user)
1. LAN Scan, device list: short Esc stays in the app and shows "Hold Esc 3 s to exit" for about 2 s.
2. LAN Scan: Enter on a device (ports), short Esc returns to the list; then short Esc again shows the hint only.
3. Wi-Fi Survey: Networks and Channels (Tab): short Esc shows the hint, app stays.
4. Wi-Fi Survey: Enter on a network (detail), short Esc returns to the tab you came from.
5. In each app hold Esc for 3 s on any screen: the launcher exits the app.
