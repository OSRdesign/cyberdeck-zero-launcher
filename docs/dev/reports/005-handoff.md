# 005 hand-off (apps-dev): LanScan 0.1.2, copyright file

## What changed (apps repo `C:\CLAUDE\zero7\cyberdeck-zero-apps`, nothing committed)
- New `apps/lanscan/root/usr/share/doc/lanscan/copyright` (Debian copyright format 1.0): LanScan code and assets (MIT,
  2026 OSRdesign, full MIT text), cp0_lvgl runtime (M5Stack Technology CO LTD, MIT, wording from wifi-survey), and
  `oui.tsv` (source: IEEE Registration Authority public listing, https://standards-oui.ieee.org/oui/oui.csv, via
  `apps/lanscan/src/tools/make_oui.py`).
- `apps/lanscan/app.json`: version 0.1.1 -> 0.1.2.
- Version numbers only: `apps/lanscan/README.md` (line 3 and line 22), top `README.md` table row. Prose and the
  "Changes" list untouched (docs-writer should add a 0.1.2 entry).
- New `packages/lanscan_0.1.2_arm64.deb`; `registry.json`: lanscan entry now 0.1.2 (new url, md5, sha256, size), plus
  a new `generated_at`. The 0.1.1 lanscan deb is left in `packages/`.

## How it was built
Pure Python `python tools/make_registry.py --owner OSRdesign --repo cyberdeck-zero-apps` (no scons, no WSL build lock
needed). The binary was not rebuilt: md5 inside the .deb is `b096a48f5ff34cc12205071add4050c4`, identical to the
working copy. Because make_registry rewrites every deb, I backed up registry.json and packages first, then restored
`viz1090_0.1.1` and `wifi-survey_0.1.0` debs with `git checkout` (git status shows them unmodified) and rebuilt
registry.json from HEAD with only the lanscan entry replaced. The backup was deleted afterwards.

## .deb
`packages/lanscan_0.1.2_arm64.deb`, 1454518 bytes, sha256 `8df12e0a6a71c77cd19ca80e96a5147e6ce5d333562e668bd1c3ec4f836948a1`,
md5 `d8bbb8b07ea3fd6fc4b54cdee50a5eec`. `dpkg-deb -c` lists `./usr/share/doc/lanscan/copyright` (2472 bytes) next to
the desktop file, binary, oui.tsv (972547 bytes) and icon. `dpkg-deb -I`: Package lanscan, Version 0.1.2.

## Test plan for deck-verifier
1. Install 0.1.2 over 0.1.1 (Settings > Apps shows the update offer; or dpkg -i) with no errors.
2. LAN Scan still starts and behaves as before: device list with vendor names, port scan, keys, exit.
3. `/usr/share/doc/lanscan/copyright` exists and reads correctly; `oui.tsv` still present (same size).
4. Settings > Apps upgrade path: 0.1.1 installed -> 0.1.2 offered, installs; afterwards no further update offered.
5. viz1090 and wifi-survey packages and registry entries unchanged (`git diff` shows nothing for them).

## Open questions
- The brief says the IEEE data is "redistributed as published". It is not quite: make_oui.py shortens company names
  (drops corporate suffixes, max 30 characters). The copyright file says so explicitly. Confirm that wording is fine.
- IEEE publishes the listing for public use but states no formal licence; the file says "public-listing" with no
  further claims. A stricter reading may want legal review.
- `registry.json` has CRLF line endings in the working copy (git warns); same as before, not changed by me.
- Existing wifi-survey 0.1.0 copyright is a short free-form text, not Debian format; not touched.
