Task: 004 remove projects/LanScan (follow-up of 002)        Agent: launcher-dev        Result: done
Changed:
- Removed (git rm, 16 tracked files, plus untracked build/ and dist/ deleted): `projects/LanScan/` (whole folder; nothing else touched).
- `.gitignore` (dropped the `!projects/LanScan/tools` exception)
- `docs/HOSTING-APPS.md` (starter-app link and "Example app" row now point to `apps/lanscan` in the apps repo)
- `docs/dev/DEV-GUIDE.md` (build section no longer cites projects/LanScan; says app sources live in the apps repo)
- `projects/APPLaunch/pizero2w/README.md` and `projects/APPLaunch/pizero2w/bundle/README.md` (source now "apps repo, apps/lanscan")
- `docs/dev/ROADMAP.md` follow-up row marked done
- No `cp0_statusbar.*` change; the apps repo was not touched.
How it was checked:
- Dependencies: `apps/lanscan/build/build.sh` builds in `projects/LanScanBuild` (scratch, in .git/info/exclude), not `projects/LanScan`. `wifi-survey/build/build.sh` uses `projects/WifiSurvey`; viz1090's build dir builds on the Pi and has no reference. Nothing needs `projects/LanScan`.
- Tree diff against `apps/lanscan/src` (excluding build/dist): identical (SConstruct, config_defaults, main/, tools/make_oui.py). `projects/LanScan/APPLaunch` (lanscan.desktop, lanscan.png, oui.tsv) is byte-identical to the copies in `apps/lanscan/root/usr/share/APPLaunch/...`. No differences, nothing lost.
- Repo grep (excluding SDK, build, dist, reports, tasks): no remaining `projects/LanScan` reference. Left on purpose: `projects/LanScanBuild` and `projects/WifiSurvey` (untracked scratch, excluded), a comment in `projects/WifiSurvey/main/ui/wifiscan.cpp` about the LAN Scan oui.tsv, and generic "LAN Scan" mentions in README.md, ROADMAP history and docs/OPEN_SOURCE_COMPONENTS.md. No scons, Kconfig, CMake or packaging reference to LanScan exists.
- Bundle/installer: `pizero2w/install.sh`, `build.sh`, `bundle/` and `pizero2w-bundle.tar.gz` contain no LanScan file and do not install it; LAN Scan only arrives via Settings > Apps, which is unchanged (no code touched).
- Build: APPLaunch built under the WSL flock, "scons: done building targets". The dist binary was rebuilt: same size (14868008 bytes), new sha256 `1ca09e2c...cd13d7` (was `4a8aa4f4...55355b`). The change is not from this task's edits (no launcher source changed); most likely build-time strings/timestamps. Not deployed.
Not done / risks: Git status shows `projects/AdsbRadar/` untracked (pre-existing, not mine). Changes are staged (deletion) and uncommitted. The files I edited are LF in the working tree, git warns it will convert to CRLF (pre-existing autocrlf setting).
Needs from the Controller: commit approval from the user. deck-verifier test plan: none required for the deck since the binary has no functional change; optionally copy the new binary and confirm the launcher starts, Settings > Apps lists and installs lanscan.
