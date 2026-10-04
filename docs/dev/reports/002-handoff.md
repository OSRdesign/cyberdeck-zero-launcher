Task: 002 lanscan-move        Agent: apps-dev        Result: done (built, not verified on the deck)

Changed (apps repo C:\CLAUDE\zero7\cyberdeck-zero-apps, all uncommitted):
- NEW apps/lanscan/src/ : full copy of projects/LanScan (main/, tools/make_oui.py, SConstruct, the four *config_defaults.mk). Sources unchanged, no behaviour or UI edit.
- NEW apps/lanscan/build/build.sh : same scheme as wifi-survey (copies src into the scratch project projects/LanScanBuild in the launcher tree, kept out of git via .git/info/exclude, scons under `flock /tmp/wsl-build.lock`, copies the binary to root/.../bin/M5CardputerZero-LanScan). It does not use projects/LanScan.
- MOD apps/lanscan/root/usr/share/APPLaunch/bin/M5CardputerZero-LanScan : rebuilt binary (4228360 bytes; the old one was 4293968, byte-different but built from identical sources, so build path/ID differences only).
- MOD apps/lanscan/app.json : version 0.1.0 -> 0.1.1 (binary changed); source_repo now https://github.com/OSRdesign/cyberdeck-zero-apps/tree/main/apps/lanscan. Package name, share_code, title, category, summary, permissions unchanged. Desktop file, icon and oui.tsv untouched (oui.tsv identical to projects/LanScan's).
- MOD apps/wifi-survey/build/build.sh : one line. Its guard tested for projects/LanScan in the launcher tree and would fail once that folder is removed; it now tests projects/APPLaunch. No deb content affected.
- NEW packages/lanscan_0.1.1_arm64.deb (1453066 bytes, sha256 22ac3a29...caa1a).
- MOD registry.json : only the lanscan entry (version, timestamps, source_repo, download url/md5/sha256/size). viz1090 and wifi-survey entries and debs are byte-identical to git HEAD (make_registry rewrote them; I restored the debs with git checkout and spliced only the lanscan entry back into HEAD's registry.json).
- packages/lanscan_0.1.0_arm64.deb is still in the folder (tracked) but no longer referenced; delete it when committing if you do not want to keep it.
Launcher repo: not touched (projects/LanScan intact, and no files modified; only the untracked .git/info/exclude gained `projects/LanScanBuild/`; a scratch dir projects/LanScanBuild/ exists, ignored by git).

How it was checked:
- `wsl -e sh /mnt/c/CLAUDE/zero7/cyberdeck-zero-apps/apps/lanscan/build/build.sh` : scons completed, ELF aarch64, installed into root/.
- `python tools/make_registry.py --owner OSRdesign --repo cyberdeck-zero-apps`, then the viz1090/wifi-survey restore described above; `git status` shows only the paths listed.
- `dpkg-deb -I` / `-c` on the new deb: Package lanscan, Version 0.1.1, arm64; contents: lanscan.desktop, M5CardputerZero-LanScan, oui.tsv (972547 bytes), images/lanscan.png. Same file set as 0.1.0.
- Not run on the deck.

Not done / risks:
- Binary differs from the old one at the byte level; behaviour should be the same but only the deck can confirm.
- projects/LanScan still exists (removal is the Controller's step after verification and user agreement).

Launcher references to LanScan, follow-up for launcher-dev (not edited), to remove or repoint once projects/LanScan is deleted:
- .gitignore
- docs/HOSTING-APPS.md
- docs/dev/DEV-GUIDE.md (the "projects/LanScan" example in the Building section)
- projects/APPLaunch/pizero2w/README.md
- also found by grep: projects/APPLaunch/pizero2w/bundle/README.md, docs/dev/ROADMAP.md, docs/dev/tasks/001-wifi-survey.md (the last two are dev docs/history). Check them too.
- The launcher scons/Kconfig do not reference LanScan (grep over the tree found only the files above); launcher build after removal still to be confirmed by launcher-dev.

What deck-verifier must re-check (same behaviour as the previous lanscan 0.1.0):
1. Install lanscan 0.1.1 over 0.1.0 (Settings > Apps, or dpkg -i); the tile shows "LAN Scan" in category Network with the same icon; launches from the desktop file, Exec M5CardputerZero-LanScan.
2. Device list: a real scan on the deck's Wi-Fi shows the devices with IP, MAC, vendor and name (mDNS, NetBIOS, reverse DNS); only the connected subnet is scanned; no root needed.
3. OUI vendor lookup: vendors resolve from /usr/share/APPLaunch/share/oui.tsv (compare a few known MACs against 0.1.0, or check an unknown-in-built-in-table vendor appears); the file is installed (972547 bytes).
4. Select a device: port scan common ports and the 1-1024 range runs, results display, Back returns; touch and keyboard both work; top bar and 640x480 layout as before; screenshots.
5. Exit leaves no residue on the screen, no crash in `journalctl --user -u APPLaunch`.
6. Registry: the Apps list offers lanscan 0.1.1 with the new sha256; viz1090 and wifi-survey entries unchanged.

Needs from the Controller: verification by deck-verifier; the decision on keeping lanscan_0.1.0 deb; after user agreement, delete projects/LanScan and have launcher-dev clean the references above (launcher-dev request); commit only on the user's go-ahead.
