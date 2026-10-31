# Release checklist (Controller, with `deck-verifier` and the user)

A release is an approval gate: the user says "cut vX.Y.Z".

1. `deck-verifier` report for every task in the release is PASS; the roadmap shows them `done`.
2. `python docs/dev/deck/check_statusbar_drift.py` exits 0 (the apps' copies of the top bar match the launcher's).
3. The launcher repo is clean (`git status`), everything intended is committed; apps repo likewise.
4. Version and notes: `docs-writer` drafts the release notes `docs/dev/release-notes-X.Y.Z.md` (what is new, how to
   install, how to update). The user-facing part **must end with the line `<!-- launcher-notes-end -->`**: Settings >
   Updates > What's new shows everything above it (without the marker it stops at the line starting with
   `**Install**`, else shows all). The notes say that boards on v0.8.12 or older need one install by hand
   (`install.sh`). Commit the notes before the build: `build.sh` copies the file of this version into the bundle
   (`/usr/share/doc/cyberdeckzero-launcher/`, What's new offline).
5. Build the bundle in WSL: `projects/APPLaunch/pizero2w/build.sh --with-store`; check the tarball is new
   (timestamp), contains the launcher, the Store, `install.sh`, the service file, the fonts.
   `build.sh` builds the `release` channel unless `APPLAUNCH_CHANNEL` is set (so development-only pages such as
   System > Keyboard test stay hidden, and About / System "Channel" reads `release`); `tools/dev/wsl-build.sh` builds
   `development` by default, so a bundle meant for a release is built with `build.sh` directly (or with
   `APPLAUNCH_CHANNEL=release tools/dev/wsl-build.sh`). CI builds `prerelease`.
6. Optional but wanted for launcher changes: `deck-verifier` installs the new binary on the deck and confirms the
   startup and the home grid.
7. Push both repos (user's go-ahead). Tag the docs commit (the release-notes commit on main) with an annotated tag:
   `git tag -a vX.Y.Z <commit> -m "vX.Y.Z: <headline>"`, push it. Then
   `gh release create vX.Y.Z --title vX.Y.Z --notes-file docs/dev/release-notes-X.Y.Z.md <assets>`. Assets, four:
   `cyberdeckzero-launcher-X.Y.Z-arm64.tar.gz`, `cyberdeckzero-launcher-X.Y.Z-amd64.tar.gz` and a `.sha256` for each
   (`sha256sum <tarball> > <tarball>.sha256`, in the tarball's folder). Settings > Updates finds the bundle by that
   name and checks it against the `.sha256` asset; it offers only a plain `vX.Y.Z` tag that is neither a draft nor a
   pre-release.
8. `gh release view` shows published (not a draft) with the four assets; update `ROADMAP.md` and the project notes.
