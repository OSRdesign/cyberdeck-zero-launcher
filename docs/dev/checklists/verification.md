# Verification checklist (used by `deck-verifier`)

Write the result of each item in `docs/dev/reports/NNN.md` as PASS / FAIL / N/A with the evidence (command output,
screenshot path). A FAIL goes back to the owner; the Controller is told. Do not fix things yourself.

## A. Build
- [ ] The change builds from a clean checkout (launcher: scons in WSL; app: its build script), warnings noted.
- [ ] No unexpected files in `git status` (build output, `.venv`, caches are ignored; nothing from another task).

## B. Packages and registry (apps)
- [ ] `python tools/make_registry.py` succeeds; only the intended apps are in `registry.json`.
- [ ] `dpkg-deb -I` shows the right Package, Version, Architecture (arm64), Depends.
- [ ] `dpkg-deb -c` shows files where the app expects them; executables are `rwxr-xr-x`; the `.desktop` file is valid
      (`Exec` absolute and executable, `Icon` exists); no stray files outside `/usr/share/APPLaunch`, `/opt/<id>`.
- [ ] md5 / sha256 / size in `registry.json` match the `.deb`.
- [ ] A licence notice (`usr/share/doc/<id>/copyright`) exists when third-party code or data is bundled.

## C. On the deck
- [ ] Install through **Settings > Apps** (sudo password) succeeds; `dpkg -s <package>` says installed.
- [ ] The tile appears on the home grid with its icon; the app opens; the keyboard (awake and asleep) and touch work.
- [ ] Screenshots: the home grid (top bar identical to the launcher's), the app's main screens. Compared with the
      previous accepted screenshots where they exist.
- [ ] No errors in `journalctl` for `APPLaunch.service` (`sudo journalctl _SYSTEMD_USER_UNIT=APPLaunch.service`).
- [ ] Quit paths work (Esc, Esc held for 3 s, the app's own close).
- [ ] Remove through Settings > Apps leaves nothing behind (tile, files, `dpkg -s`).
- [ ] The launcher still starts at boot, the startup lines scroll and the grid is clean after the boot.

## D. Repo hygiene
- [ ] No passwords, tokens or personal data in the diff (`git diff` and new files).
- [ ] Right repo for each file (apps in the apps repo; the launcher only generic code).
- [ ] No large or binary files that should not be tracked; line endings LF.
- [ ] `cp0_statusbar.[ch]` in `apps/viz1090/build` identical to the launcher's copy (`python docs/dev/deck/check_statusbar_drift.py`, exit 0).

## E. Licences and credits
- [ ] Every bundled component has its notice and a source link (code, fonts, map data, libraries).
- [ ] GPL components: the exact source commit is named.

## F. Docs
- [ ] The READMEs list the same apps as `registry.json`; install steps match what was just done on the deck.
- [ ] Links resolve (`curl -sI`), screenshots referenced exist.
