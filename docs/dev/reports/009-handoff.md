# 009 hand-off: MeshCore app renamed to MeshZero (draft, unpublished)

## Renamed
`apps/meshcore` -> `apps/meshzero` (plain move). Package/share_code/title `meshzero`/`MeshZero`, version reset to 0.1.0 (app.json, README).
Binary `M5CardputerZero-meshzero`, `meshzero.desktop` (Name/Exec/Icon), icon `meshzero.png`, `usr/share/doc/meshzero/copyright`
(Upstream-Name MeshZero, Source .../apps/meshzero), build.sh (scratch project `projects/MeshZero`), C++ namespace `meshzero`,
`UIMeshZeroPage`, `meshzero.cpp/.hpp`, page title, env vars `MESHZERO_PORT/_DATA/_SIM`, data dir `~/.local/share/meshzero`
(also `$XDG_DATA_HOME/meshzero`), test tmp names. `port` / `lastport` file names unchanged (they live in the data dir).
Simulator kept as `tools/meshcore_sim.py` (it simulates a MeshCore radio).
Migration: `Store::migrate_legacy_dir()` (store.cpp), called from `Store::default_dir()`: if `~/.local/share/meshcore` exists and
`meshzero` does not, rename it. Unit-tested (`test_migrate_legacy`: moves; no-op when old is gone; both kept when new exists).
README: status/draft note, credits (wadamesh GPL-3.0, Meshy GPL-3.0-or-later, ideas only), migration note; copyright file credit updated.

## Remaining "meshcore" hits (51 lines), all protocol/firmware/credits or the legacy name
- `MeshCore` in README, app.json description/summary, copyright, client.cpp user texts ("MeshCore board", "MeshCore companion radio firmware"),
  protocol/frame/transport headers, test comment (board firmware v1.15.0), simulator docstring: the protocol/firmware.
- `meshcore_py`: wire-format reference credit in code comments, README, copyright.
- `meshcore-dev` URLs (MeshCore MIT credit, meshcore_py repo) in README and copyright.
- `meshcore_sim` (script name, README, run_tests.sh, test_sim.cpp, simulator output): simulates a MeshCore radio.
- Lowercase `meshcore` in store.cpp (2), test_core.cpp (1), README (1): the legacy data dir, only for the one-time migration and its test/doc.

## Draft mechanism
`app.json` `"draft": true` -> `tools/make_registry.py` skips the app ("skipping draft app: meshzero"). Documented in the make_registry
docstring and in the root README (section 3, app.json keys). New: `--only <id> --out <folder>` builds just that app's .deb into a folder
(draft allowed), does not touch `packages/` or `registry.json`.
Dry run: registry.json and packages backed up, make_registry run, meshzero absent, same 3 apps (lanscan, viz1090, wifi-survey) with the
same structure; only generated_at, published/updated timestamps and the deb md5/sha256/size differ (debs embed timestamps, as expected).
Everything restored byte-exact from the backup (`cmp`/`diff -r` clean).

## Old artefacts
Deleted `packages/meshcore_0.1.3_arm64.deb`; removed the `meshcore` entry from `registry.json` (the rest untouched, LF, same formatting).

## Local deb (not published)
`C:\CLAUDE\zero7\archive\meshzero\meshzero_0.1.0_arm64.deb`, md5 `82a9ec0491c058c6ae574399f0741f33`, 1429490 bytes.
Built with `python tools/make_registry.py --only meshzero --out C:/CLAUDE/zero7/archive/meshzero`.
Note for the deck: the 0.1.x `meshcore` package (tile, binary, doc dir) is a different package; remove it (`apt remove meshcore`) so there
is no duplicate tile. History migrates by itself at first start.

## Tests
- `wsl -e sh .../apps/meshzero/src/tests/run_tests.sh`: core 260 checks, 0 failed (incl. migration); sim 30 checks, 0 failed.
- `build.sh` under the WSL lock: aarch64 ELF built and installed into `root/`.
- Not run on the deck.

## git status --short (apps repo)
```
 M README.md
 M apps/lanscan/README.md
 M apps/lanscan/app.json
 M apps/lanscan/root/usr/share/APPLaunch/bin/M5CardputerZero-LanScan
 M apps/lanscan/src/main/ui/lanscan.cpp
 M apps/lanscan/src/main/ui/lanscan.hpp
 M apps/wifi-survey/README.md
 M apps/wifi-survey/app.json
 M apps/wifi-survey/root/usr/share/APPLaunch/bin/M5CardputerZero-wifi-survey
 M apps/wifi-survey/src/main/ui/wifisurvey.cpp
 M apps/wifi-survey/src/main/ui/wifisurvey.hpp
 M registry.json
 M tools/make_registry.py
?? apps/lanscan/src/main/ui/esc_policy.hpp
?? apps/lanscan/src/tests/
?? apps/meshzero/
?? apps/wifi-survey/src/main/ui/esc_policy.hpp
?? apps/wifi-survey/src/tests/test_esc.cpp
?? packages/lanscan_0.1.3_arm64.deb
?? packages/wifi-survey_0.1.1_arm64.deb
```
Root `README.md` and `registry.json` also carry the other task's changes; mine are the `draft` bullet and the meshcore entry removal.
