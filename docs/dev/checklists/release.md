# Release checklist (Controller, with `deck-verifier` and the user)

A release is an approval gate: the user says "cut vX.Y.Z".

1. `deck-verifier` report for every task in the release is PASS; the roadmap shows them `done`.
2. The launcher repo is clean (`git status`), everything intended is committed; apps repo likewise.
3. Version and notes: `docs-writer` drafts the release notes (what is new, how to install, how to update).
4. Build the bundle in WSL: `projects/APPLaunch/pizero2w/build.sh --with-store`; check the tarball is new
   (timestamp), contains the launcher, the Store, `install.sh`, the service file, the fonts.
5. Optional but wanted for launcher changes: `deck-verifier` installs the new binary on the deck and confirms the
   startup and the home grid.
6. Push both repos (user's go-ahead), then `gh release create vX.Y.Z <tarball> --target main --notes-file ...`.
7. `gh release view` shows published (not a draft) with the asset; update `ROADMAP.md` and the project notes.
