# Hand-off 006 - Settings > Apps polish (round 3, after report 009)

```
Task: 006 settings-apps-polish (round 3)        Agent: launcher-dev        Result: done (built and unit tested; not deployed, not seen on the deck)
```

Round 3 fixes only what report 009 failed. Everything below the "Round 2" heading is unchanged history. Nothing committed,
pushed or deployed. `cp0_statusbar.*` untouched. Binary `C:\CLAUDE\zero7\launcher\projects\APPLaunch\dist\M5CardputerZero-APPLaunch` md5 `d81fcdb4eb552ed81867e5cd21167976` (aarch64, built in WSL under the lock; the only compiler warning is an old unrelated one about a `tm` format in another file).

## Round 3 changes

Files (all under `C:\CLAUDE\zero7\launcher\projects\APPLaunch\`): `main/ui/settings/apps_backend.cpp`,
`main/ui/settings/apps_status_model.cpp` / `.hpp`, `main/ui/settings/settings_apps_page.cpp`, `tests/test_settings_apps_model.cpp`.

1. **Sync reasons.** Cause confirmed by reading: `run_program` used `execv`, which does not search PATH, so `probe_source`'s bare
   `"curl"` died with exit 127 and every failure fell into `Connection failed`. Fix at the cause: `execvp` in `run_program`
   (other launcher code, e.g. LaunchWizard, ZClaw, cp0_process_commands, already uses `execvp`; the backend path is absolute and
   still works). The result parsing moved into the pure `apps_status::curl_probe(run_rc, output)` (testable). Mapping now:
   HTTP status >= 400 -> `HTTP <code>`; curl 7 -> `Connection refused` (was `No network`); curl 6 or no default route -> `No network`;
   curl 28 or our own 20 s deadline -> `Timed out`; any other curl code (52 empty reply, 56 reset, 127 not started) ->
   `Connection failed`; invalid JSON -> `Bad JSON`. All at most 18 characters. Tests: curl exit code + http code -> reason table in
   `sync_reasons()` and the updated `sync_verdicts()`.
2. **Green for 2 s.** Cause: after the last source `publish_sync_locked()` set the final text "Synced N of M" with the colour flags
   cleared (green); the amber/red only came from `finish()` after `task_refresh` (the `--summary` list reload, about 2 s). Fix:
   `publish_sync_locked` now sets the amber/red flag from `SyncProgress::severity()` as soon as every source is finished and none is
   running, so the colour is right on the first frame; `finish()` keeps the same value.
3. **pending-package.json after wrong password / Esc: left as is.** Not a one-line cleanup: the backend keeps that file, it does not
   block other apps (report 009), and deleting it would risk breaking the resume of the same app on retry (the 008 problem). Report
   009 shows it disappears at the next operation. The round-2 hand-off sentence "the backend already clears it itself" was wrong.

Unit tests (WSL, under the lock, g++ -std=c++17 -Wall -Wextra -Werror): `test_settings_apps_model` passes (the other two test files are untouched by round 3 and were not rerun).

## What deck-verifier must re-check (round 3)

Test registry as in 009 (http server on the deck: good, 404, HTML, drop-without-reply, stopped server, slow). `curl` must be on the
deck PATH (it is: 009 ran it).

- Sync reasons on the row / footer: deleted file on a live server -> `HTTP 404`; stopped server -> `Connection refused`;
  connection dropped without reply -> `Connection failed`; HTML -> `Bad JSON`; a source that hangs longer than 10 s -> `Timed out`.
  Check the text fits the row.
- Colour from the first frame: after the last source the footer is amber (`Synced N of M sources`) or red (`Sync failed (N)`)
  immediately, never green first; all good stays green. Compare the first frame after the last row turns done/failed.
- Quick regression: round-2 items 2 (S/U/A/D, W/E/R/T inert), 3 (dpkg reason), 4 (parked file, other app installs, retry resumes),
  5 (status line with long names); goals 1 (tile order), 2 (versions, Update all, single update), 4 (seconds, success lines, MD5 panel,
  wrong password, Esc `Cancelled`, space, backend missing). Journal has no new error.
- `pending-package.json` after wrong password / Esc may remain until the next operation: expected, not a failure.

Needs the user's touch: a real `No network` (cut the Wi-Fi / unresolvable host), a real 404 on a real source, tap rows and the
failure panel, keys from the real M4 keyboard, judging amber/wording.

---

# Round 2 (history)


```
Task: 006 settings-apps-polish (round 2)        Agent: launcher-dev        Result: done (built and unit tested; not deployed, not seen on the deck)
```

Branch: `settings-apps-polish` (local; round 1 changes kept; nothing committed, pushed or deployed).
Binary: `C:\CLAUDE\zero7\launcher\projects\APPLaunch\dist\M5CardputerZero-APPLaunch` (aarch64, md5 `bbfcfe8323b8d6f68b844fa60333065d`,
built in WSL under `flock /tmp/wsl-build.lock`, no warning from the changed files).
Store backend (`projects/AppStore` submodule) unchanged. `cp0_statusbar.*` untouched (no change needed in the apps repo).
Only the launcher binary goes to the deck.

Unit tests (g++ -std=c++17 -Wall -Wextra -Werror, WSL, under the lock), all pass: `test_settings_apps_model.cpp`,
`test_dynamic_app_registry.cpp`, `test_desktop_app_order.cpp`. No change to `tests/run_tests.sh` was needed (the model test
already builds `apps_backend.cpp` and `apps_status_model.cpp`). The two older failures (`test_external_framebuffer_ownership.py`,
`test_settings_menu_order.py`) fail on `main` too (report 008) and were not touched.

## Changed files per fix

Shared by several fixes: `main/ui/settings/settings_apps_page.cpp` / `.hpp` (CRLF in the `.hpp`, kept),
`main/ui/settings/apps_status_model.cpp` / `.hpp`, `main/ui/settings/apps_backend.cpp` / `.hpp`,
`tests/test_settings_apps_model.cpp`. All under `C:\CLAUDE\zero7\launcher\projects\APPLaunch\`.

1. **Sync failures now show.**
   - `apps_status_model`: new `update_registry_status` (reads the `REGISTRY UPDATED ... <status>` line), `decide_sync`
     (success only on a positive `ok`), `SyncProgress::severity`; final line is `Synced 3 of 4 sources` (was `Synced 3, 1 failed`).
   - `settings_apps_page.cpp` `task_sync`: after each `--edit-registry` it reads the source's record from `--registries`
     (status and error), decides per source with `decide_sync`, and on failure runs the existing curl probe, so
     `HTTP 404`, `Connection failed`, `No network`, `Timed out`, `Bad JSON` appear on the row and in the footer. The
     final status is green (all good), amber (some failed), red (all failed). New `status_warn` flag and amber
     support in `set_status`. Cached apps of failed sources and the apps of good sources stay listed (unchanged path).
   - Tests: `sync_verdicts()` (cached/error/ok, exit code vs record, timeout, probe reasons, 3 of 4 amber).
2. **Letter shortcuts U, S, A, D.** `settings_apps_page.cpp`: new `shortcut_key` (called from the raw `LV_EVENT_KEYBOARD`
   callback when not editing) compares physical `key_code` (`KEY_U`, `KEY_S`, `KEY_A`, `KEY_D`), pressed state only,
   no Ctrl/Alt, ignored while the failure panel is open; `start_sync` shared with the Sync row path. The ASCII comparisons
   in `handle_key` are removed; arrows, Enter, Esc and `Del` (native keys) are unchanged. The raw listener is now
   registered on the page's own screen (`lv_obj_get_screen(ComponensObj)`, fallback active screen). Hint lines unchanged.
   Extra, same mechanism: W/E/R/T (codes 17-20 equal LV_KEY_UP/DOWN/RIGHT/LEFT, seen in report 008 as letters acting as
   navigation) are recorded by the raw listener and the matching native key within 300 ms is ignored, as in Wi-Fi Survey.
   Real arrows and the F/X/Z/C aliases (their raw code is not 17-20) still work. No unit test possible for the key path
   (LVGL); logic is a physical-code comparison.
3. **dpkg reason line.** `package_failure` (`apps_status_model.cpp`): when the dpkg line only announces the package (ends with
   `:`), the panel detail is the next one or two reason lines (stops at a blank line, `ERROR`/`PROGRESS` records, the next
   `dpkg:` line, `Errors were encountered`), each cut to 95 characters, total at most 200. If nothing follows, the
   announcement line is shown as before. Test updated and extended.
4. **Pending transaction after a failed install.** Cause: the failed dpkg step leaves the half-installed package in the backend's
   `pending-package.json`; the backend refuses every other app (`another package transaction is pending`) and, when the
   same app is retried, resumes it. Fix in the launcher only (`apps_backend::park_pending_transaction` /
   `restore_pending_transaction`, `state_dir`): after a failed privileged step (not wrong password, cancel or timeout, where
   the backend already clears it itself) the file is renamed to `pending-package.parked.<id>.<action>.json`; before
   preparing the same app and action again it is moved back if no other transaction exists, so the retry still resumes
   exactly as verified in 008, while another app now installs normally. Headlines: a pending conflict now reads
   `Unfinished install` (names the package), and any backend refusal at the prepare stage that is not a download reads
   `Could not prepare`; `Download failed` is only used when the error mentions a download or a curl code. Tests: `parked_transactions()`
   (temp state dir via `M5APPSTORE_STATE_DIR`), failure classification.
5. **Clipped status line.** `settings_apps_page.cpp` status label widened (x 124, width 188), and the running step is built by the new pure
   `apps_status::stage_line`: the step text is cut with `~`, the counters are always kept (`Downloading Wi-Fi S~ (2/2) 14s`,
   at most 30 characters). Test `stage_lines()`. Also fixed while there: the empty list text uses the whole row width
   (was clipped), and the Apps tab says `Loading...` instead of `No apps` until the first list has arrived.

## Decisions

- A source counts as synced only on a positive `ok` (from the UPDATED line, else from the source record); anything unknown
  is a failure. The backend's own text for a cached source is only `download failed: <url>`, so the HTTP code still comes
  from the curl probe (10 s at most per failed source).
- The parked pending file is kept in the backend's state directory, not deleted: deleting it would make a retry of the same
  app fail (`unable to determine current Debian package state`, the package stays `iF`). The half-configured package itself
  is left as dpkg has it (as before).
- Amber for a partial failure reuses the existing yellow of the page.

## What deck-verifier must re-check

Deploy as in the previous plan (back up `config.json` and the launcher binary first). Test registry on the deck as in 008.

1. Goal 3 (sync): four sources (good, HTML instead of JSON, deleted file, stopped server). Press `S` or the Sync row.
   Rows go `waiting`, `syncing...`, then `done` or red `failed` live (not `done` for the broken ones). Selecting a failed
   row shows its reason (`HTTP 404` for the deleted file on a server that answers, `Connection failed` or `No network` for the
   stopped server, `Bad JSON` for HTML). Final status `Synced 1 of 4 sources` in amber (all failing: red `Sync failed (N)`;
   all good: green `Synced N sources`). Apps of the good source and the cached apps stay in the Apps tab.
2. Keys (uinput keyboard `v008-vkbd`): `S` on any tab starts a sync; `A` on Sources opens the add editor (type, Enter/Esc work);
   `D` on a user source asks `Remove this source?` (Enter yes, Esc no); `U` on Apps with two updates starts Update all.
   Arrows, Enter, Esc, Left/Right tabs unchanged. Check that `W`, `E`, `R`, `T` no longer move the selection or switch tabs,
   while the arrow keys and F/X/Z/C still do. Check the same once with the real M4 keyboard if awake.
3. dpkg failure (`v008fail`): the panel shows the real reason (`installed v008fail package post-installation script ... exit status 1`),
   at most two lines, not only `dpkg: error processing package ...:`.
4. After that failure install a different app: it must install (no `Download failed`, no `Unfinished install`). Then retry
   `v008fail` with the failflag removed: it must still complete. Look in `~/.local/share/cardputerzero-appstore/` for the
   `pending-package.parked.*` file after the failure (present) and after the retry (gone). If a conflict still happens
   the headline must be `Unfinished install`.
5. Status line during Update all with longer names: counters `(1/2) 3s` stay visible; the list text `No apps: add your source in Sources`
   and `The Store is needed ...` fits; `Loading...` shows at start.
6. Regression: goal 1 (tile order on first run, upgrade, new app last), goal 2 (versions, `Update all (N)` row, single update,
   one password prompt per app), goal 4 (elapsed seconds, success lines, MD5 panel, wrong password 3 tries, Esc `Cancelled`,
   not enough space, backend missing). Journal has no new error.

Needs the user's touch: tap rows (app, Update all, source, Add, Sync, tabs), tap to close the failure panel; judge the amber
status colour, the wording (`Unfinished install`, `Could not prepare`), and the narrower left column; scrolling with more than
6 rows; a real `No network`; Update all on a real source with two published updates.

## Not done / risks

- The key path was not exercised on a device (no simulator run); it follows the Wi-Fi Survey approach. Verify on the deck.
- A source that responds 200 with a valid but empty registry counts as `ok` (the backend says `ok`).
- Parked files of an app that is never retried stay in the state directory (a few hundred bytes each).
- Docs: `docs/HOSTING-APPS.md` still needs the update-row and failure-message sentence (docs-writer).
