# Release checklist (Controller, with `deck-verifier` and the user)

A release is an approval gate: the user says "cut vX.Y.Z".

1. `deck-verifier` report for every task in the release is PASS; the roadmap shows them `done`.
2. `python docs/dev/deck/check_statusbar_drift.py` exits 0 (the apps' copies of the top bar match the launcher's).
3. The launcher repo is clean (`git status`), everything intended is committed; apps repo likewise.
4. Version and notes: `docs-writer` drafts the release notes (what is new, how to install, how to update).
5. Build the bundle in WSL: `projects/APPLaunch/pizero2w/build.sh --with-store`; check the tarball is new
   (timestamp), contains the launcher, the Store, `install.sh`, the service file, the fonts.
6. Optional but wanted for launcher changes: `deck-verifier` installs the new binary on the deck and confirms the
   startup and the home grid.
7. Push both repos (user's go-ahead), then `gh release create vX.Y.Z <tarball> --target main --notes-file ...`.
8. `gh release view` shows published (not a draft) with the asset; update `ROADMAP.md` and the project notes.
