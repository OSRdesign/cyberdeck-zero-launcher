# Settings > Apps checks for app updates when it opens: board test

A dev build of the branch `apps-autocheck` (`0.9.4-autocheck.2` or later; Settings > About says "dev build"). One board
with a network is enough (the Pi 4B, where the problem was seen); a second one with a 480x320 or 320x480 screen only for
the look of the new row and the status line. Answer "OK" or what was wrong per step. Background: `docs/HOSTING-APPS.md`,
"Checking for updates".

Before: note the time of the cached list, `ssh <board> "grep -h synced_at ~/.cache/cardputerzero-appstore/registry-*.json"`.
The check only runs for a list older than 10 minutes: wait 10 minutes after the last sync (or install) before step 1.

1. Settings > Apps. Expected: the root (Installed (N), Available (M), Sources) at once, then within a few seconds the
   line under the title says `Checking for updates...`, then `Up to date (just now)` (or `Updated just now (1 failed)` if
   one of your sources did not answer: Sources shows it `failed`). If an app was published since the cached list (Deck
   Atlas 0.2.0 on the Pi 4B), Installed shows it in yellow (`0.1.0>0.2.0`) without Sync my sources.
2. The cache file's time moved: the grep above shows the time of step 1.
3. Esc back to Settings, Apps again within 10 minutes. Expected: no `Checking...`; the line says `Updated 1 min ago` (or
   the minutes since step 1), and stays current while you wait on the page.
4. Open Installed during the check (step 1 again after 10 minutes, Enter on Installed and Down at once). Expected: the
   rows change where they are, the highlight does not jump; the status line never touches the orange up arrow.
5. Wi-Fi off (Settings > Wi-Fi), wait 10 minutes after the last check, Settings > Apps. Expected: no `Checking...`, the
   line `Offline: showing the list from <age> ago`, the cached lists, no error box. Wi-Fi back on.
6. Sources: the last row `Auto-check updates`, value `On`, the label whole. Enter: `Off`.
   `ssh <board> "grep -o '\"apps_auto_check[^,]*' ~/.config/cardputerzero/config.json"` shows `0`. After 10 minutes,
   Settings > Apps again: no line, no check (as before the change). Set it back to `On`.
7. Sync my sources (or `S`): every source shows `waiting`, `syncing...`, `done` as before, even right after a check.
