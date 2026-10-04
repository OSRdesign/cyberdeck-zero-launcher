# 005 - LanScan: licence notice for the bundled OUI list

Status: done (verifier PASS in report 007; lanscan 0.1.2 installed on the deck). Owner: apps-dev. Verifier: deck-verifier. Docs: docs-writer. Acceptance: the user.

## Goal
The `lanscan` package ships `usr/share/APPLaunch/share/oui.tsv` (IEEE OUI registry data, ~972 KB) but has no
`usr/share/doc/lanscan/copyright` file (seen in report 006; wifi-survey already has one). Add a proper copyright file
to the package so the licence and the data source are stated in the package itself.

## Acceptance criteria
- `apps/lanscan/root/usr/share/doc/lanscan/copyright` exists, in Debian copyright format, covering: the LanScan code
  (MIT, same wording as the app's other packages), the cp0_lvgl runtime from M5Stack as the other packages state it,
  and the IEEE OUI data (source: IEEE Registration Authority public OUI listing, `tools/make_oui.py` shows where it
  comes from; state the source and that it is redistributed as published). Reuse the wording of
  `apps/wifi-survey/root/usr/share/doc/wifi-survey/copyright` where it fits.
- Version bump 0.1.1 -> 0.1.2 (package contents changed); rebuild only the lanscan `.deb` and its registry entry.
  Do not rebuild the binary unless needed: reuse the current one (check the md5 stays identical). viz1090 and
  wifi-survey debs and entries unchanged (make_registry rewrites all debs: restore the others from the working copy
  you started with).
- `dpkg-deb -c` shows the copyright file; `app.json`/README version mentions updated (apps/lanscan/README.md and the
  table row in the top README of the apps repo: docs-writer does the prose, but keep versions consistent).

## Constraints
No behaviour change. No commit or push. Do not touch the launcher source. Hand-off report as
`docs/dev/reports/005-handoff.md` with what the verifier must re-check (install 0.1.2 over 0.1.1, app still works,
copyright file present on the deck at `/usr/share/doc/lanscan/copyright`, upgrade path in Settings > Apps).
