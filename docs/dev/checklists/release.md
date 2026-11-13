# Release checklist (Controller, with the user)

A release is an approval gate: the user says "cut vX.Y.Z". Testing is Controller-led: the Controller checks the code
and deploys, the user runs the physical tests on the boards from the Controller's test protocol (no `deck-verifier`
run by default).

1. Every task in the release has passed the user's board tests; the roadmap shows them `done`.
2. `python docs/dev/deck/check_statusbar_drift.py` exits 0 (the apps' copies of the top bar match the launcher's).
3. The launcher repo is clean (`git status`), everything intended is committed; apps repo likewise.
   `tools/dev/check-all.sh` passes on main, and so do the installer tests, which `check-all.sh` does not run (see
   [Running the checks](../../../tools/dev/README.md#running-the-checks) for both and for the known failures).
4. Version and notes: `docs-writer` drafts the release notes `docs/dev/release-notes-X.Y.Z.md` (what is new, how to
   install, how to update), following the [rules of the notes](#release-notes-rules) below. The notes go to main in
   their own docs-only pull request (the notes file, the `ROADMAP.md` line, and the README board list when a board was
   added; as #151 for v0.10.0 and #156 for v0.10.1), squash-merged: `gh pr merge --squash` with `--subject` and
   `--body` that end with both trailers (`Co-Authored-By: ...` and `Claude-Session: ...`; without `--body` gh drops
   `Claude-Session`), as for every squash merge. The squash-merge commit of this pull request is the release commit:
   the bundles are built from it and the tag names it.
5. Build both bundles in WSL **from the release commit**. `build.sh` copies `docs/dev/release-notes-<version>.md`
   into the bundle (`/usr/share/doc/cyberdeckzero-launcher/`, What's new offline), so a build from an earlier commit
   ships without the notes. Run it **inside WSL** (`wsl bash -lc '...'`): `wsl-build.sh` started from Git Bash
   re-runs itself in WSL through `wsl.exe`, and the variables below do not reach it there. First
   `git submodule update --init projects/AppStore` in the release checkout: the Store is our fork with one patch
   (decisions 2026-11-13) and `build.sh --with-store` refuses Store sources without it.
   ```
   export APPLAUNCH_VERSION=X.Y.Z APPLAUNCH_CHANNEL=release LEGACY_BUNDLE_NAME=0
   bash tools/dev/wsl-build.sh --arch arm64 --with-store
   bash tools/dev/wsl-build.sh --arch amd64 --with-store
   ```
   - `APPLAUNCH_VERSION=X.Y.Z` (no `v`): the version goes into the tarball's name and, through `projects/APPLaunch/main/SConstruct`, into
     the binary (About, `board.state`, What's new). Without it the tarball is `cyberdeckzero-launcher-dev-<arch>.tar.gz`,
     the bundle's `VERSION` file says `dev` and the launcher reports `0.0.0-dev`; Settings > Updates looks for the
     asset `cyberdeckzero-launcher-<tag version>-<arch>.tar.gz`, so such a file on a release is never found.
   - `APPLAUNCH_CHANNEL=release`: `wsl-build.sh` builds the `development` channel by default (development-only pages
     such as System > Keyboard test show, About / System "Channel" reads `development`). `build.sh` alone defaults
     to `release`; CI builds `prerelease`. Settings > Updates installs only on the release channel.
   - `LEGACY_BUNDLE_NAME=0` (or `build.sh --no-legacy-name`): otherwise the arm64 build also writes
     `pizero2w-bundle.tar.gz`, the quick-start name of the releases before the rename (v0.7.0); it is no longer
     uploaded.
   - `--arch amd64` is a native build for PCs: it needs the -dev packages listed by `build.sh -h`. For v0.10.0 and
     v0.10.1 they came from a folder of unpacked `.deb` files (`~/zero7/xb-amd64-devroot/root` on the build box),
     given to the build through `PKG_CONFIG_PATH` and `CONFIG_TOOLCHAIN_FLAGS` (`-isystem` and `-L` into that root);
     the script of those builds is `~/zero7/rel0101.sh`.
   - Build from a clean LF copy of the release commit, not from a working checkout:
     `git -c core.autocrlf=false clone --no-checkout /mnt/c/CLAUDE/zero7/launcher/.git <dir>`, then
     `git checkout <release commit>`, and fresh build trees (`--build-dir`), as `rel0101.sh` does.
   - The tarballs are in `<build tree>/projects/APPLaunch/pizero2w/` (amd64: the build tree with `-amd64` unless
     `--build-dir` is given; `wsl-build.sh` prints the path). Check each is new (timestamp) and contains the launcher,
     the Store, `install.sh`, the service file, the fonts, `VERSION` (`X.Y.Z`) and
     `payload/doc/release-notes-X.Y.Z.md`.
   - Write a `.sha256` for each, in the folder that holds the tarballs (`build.sh` does not write them):
     `echo "$(sha256sum $f | cut -d' ' -f1) $f" > $f.sha256 && sha256sum -c $f.sha256`, `$f` the tarball's name: one
     line `<hash> <name>` with one space (as for v0.8.13 to v0.10.1). Settings > Updates checks the download against
     this asset.
6. Optional but wanted for launcher changes: the Controller installs the new bundle on a board (the deck at least) and
   the user confirms the startup and the home grid.
7. Push both repos (user's go-ahead). Tag the release commit with an **annotated** tag and push it:
   `git -c user.name=OSRdesign -c user.email=157315681+OSRdesign@users.noreply.github.com tag -a vX.Y.Z <commit> -m "Launcher vX.Y.Z" -m "<headline>"`,
   then `git push github vX.Y.Z`. `git tag -a` needs a git identity, and the checkouts have none configured: hence the
   `-c` options. If the tag is not on GitHub when the release is created, `gh release create` makes a lightweight
   tag from main instead (this happened to v0.10.1); `--verify-tag` below stops it from doing so. Then
   ```
   gh release create vX.Y.Z --repo OSRdesign/cyberdeck-zero-launcher --verify-tag --latest \
     --title "Launcher vX.Y.Z: <headline>" --notes-file docs/dev/release-notes-X.Y.Z.md \
     cyberdeckzero-launcher-X.Y.Z-arm64.tar.gz cyberdeckzero-launcher-X.Y.Z-arm64.tar.gz.sha256 \
     cyberdeckzero-launcher-X.Y.Z-amd64.tar.gz cyberdeckzero-launcher-X.Y.Z-amd64.tar.gz.sha256
   ```
   (`--latest`: GitHub's latest release; `--notes-file`: the whole notes file, install text included, is the release
   body.) The four assets are exactly these names: Settings > Updates finds the bundle by its name and checks it
   against the `.sha256` asset; it offers only a plain `vX.Y.Z` tag that is neither a draft nor a pre-release.
8. `gh release view vX.Y.Z` shows published (not a draft, not a pre-release) with the four assets, and
   `git ls-remote --tags github vX.Y.Z` shows two lines (the tag object and `vX.Y.Z^{}`): one line means a
   lightweight tag. Update `ROADMAP.md` and the project notes.

## App publishes (apps repo)

An app is published by its own pull request in the apps repo (its `docs/PUBLISHING.md`), not by a launcher release. Its
gate (owner decision of 2026-11-12, `decisions.md`): the [`app-design-qa`](../agents/app-design-qa.md) verdict for the
package being published (the same version and source commit) is **READY**, or every FAIL left in it carries the owner's
written waiver, the owner's words and their date recorded in the QA report
([`app-design.md`](app-design.md)). A BLOCKED verdict stops the publish. The audit of the apps already published is a
separate task.

## Release notes rules

`projects/APPLaunch/tests/test_settings_update_model.cpp` (the `app-unit` step of `tools/dev/check-all.sh`) reads every
`docs/dev/release-notes-*.md` and fails on a file that breaks these rules. Settings > Updates > What's new shows
the part above the marker, converted to plain text (`notes_blocks()` in `projects/APPLaunch/main/ui/settings/settings_update_model.cpp`).

- The first line is `**New since vA.B.C** (pull requests #n and #m: <headline>)`: the converted text must start with
  "New since v".
- Then at least two bullets, **plain bullets only: no nested bullets.** The test counts the nested bullets of all the
  notes and expects 6, the ones in the 0.8.0 notes.
- **No picture** above the marker (an image alone, or a table of images, becomes the line "(pictures in the online
  notes)"; only the 0.8.8 and 0.8.9 notes may have it). A table becomes "(a table in the online notes)", HTML
  comments are dropped, and bold, code and link markup is removed (the link's text stays): keep the user-facing part
  to paragraphs and bullets.
- The user-facing part **ends with the line `<!-- launcher-notes-end -->`**: What's new shows everything above it
  (without the marker it stops at the line starting with `**Install**`, else shows all).
- Install text only below the marker: "Upgrading from" and `tar xzf` above it fail the test.
- Below the marker, the fixed install block, as in the latest notes: who can update from Settings > Updates and who
  needs one install by hand (boards on v0.8.12 or older: `install.sh`); the `sha256sum -c`, `tar xzf` and
  `sudo ./install.sh` lines; the `install.sh` options per board; the Store credit inside a
  `<!-- branding-allow-begin: ... -->` / `<!-- branding-allow-end -->` block (the `branding` check).

A quick check of the notes alone, without the rest of `check-all.sh`, in WSL from an LF copy of the repository
(`<sdk>` is a tree's `SDK` folder, for example `~/zero7/launcher/SDK`; `ensure_cjson.sh` downloads cJSON when it is
missing there):

```
cd projects/APPLaunch/tests && c=$(sh ensure_cjson.sh <sdk>) && mkdir -p /tmp/notes-test &&
cc -std=c99 -O1 -w -c "$c/cJSON.c" -o /tmp/notes-test/cJSON.o &&
g++ -std=c++17 -Wall -Wextra -Werror -I../main/ui/settings -I"$c" test_settings_update_model.cpp \
  ../main/ui/settings/settings_update_model.cpp /tmp/notes-test/cJSON.o -o /tmp/notes-test/t &&
/tmp/notes-test/t fixtures/update ../../../docs/dev
```

It prints `settings update model OK`; a file that is not plain prints `<file>: not plain: [...]` before the assertion.
