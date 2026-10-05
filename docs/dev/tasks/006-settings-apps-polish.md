# 006 - Settings > Apps polish

Status: done (passed deck verification, report 010; accepted by the user 2026-10-05). Owner: launcher-dev. Verifier: deck-verifier. Docs: docs-writer. Acceptance: the user, on the deck (touch UI).

Code: `projects/APPLaunch/main/ui/settings/settings_apps_page.[ch]pp`, `apps_backend.[ch]pp`, the desktop app loader
(`main/ui/desktop_app_loader.cpp`) for the grid order. Generic launcher capabilities only: no app-specific code.

## Goals (all four chosen by the user)
1. **Keep the tile position on upgrade.** Today an upgraded or reinstalled app's tile jumps to the end of the launcher
   grid (seen on every lanscan reinstall). An upgrade (same package, newer version) must keep the tile where it was.
   Find why (probably the desktop file / apps folder order, a remove+install sequence, or a mtime-based order) and
   fix it at the cause. A first install still goes to the end; a remove then re-install may too.
2. **Upgrade flow.** When the registry has a newer version than the installed one: show installed vs available
   version on the app row/detail, an Update action (same md5-check + install backend), and an Update all (if more
   than one). Apps that are current show their version only. Version comparison must be Debian-style
   (`dpkg --compare-versions` semantics), not string compare.
3. **Per-source sync progress.** While syncing the registry from each source: show which source is being fetched,
   done/failed per source, and the reason of a failure (no network, HTTP status, bad JSON, md5 of registry), without
   blocking the UI. A failed source must not hide apps from the other sources.
4. **Install/remove feedback.** Visible progress during download/install/remove, and clear final messages: success,
   md5/sha mismatch, dpkg error (first line of the error), no network, not enough space. No silent failures.

## Constraints
- 320x170 drawing scaled into the launcher window, same fonts and top bar, keyboard keys and touch both work
  (keyboard fd may be absent when the BT keyboard sleeps). Follow the existing page structure and string style.
- No new dependency. No change to the registry format (apps repo) unless unavoidable: report it as a decision.
- Add or extend unit tests where the repo has them (see `projects/APPLaunch/tests`, `ext_components/cp0_lvgl/tests`),
  especially version comparison and the sync-state logic.
- Build under the WSL lock (DEV-GUIDE). Do not deploy to the deck, do not commit or push.
- Hand-off report `docs/dev/reports/006-handoff.md` (ROLES.md format) with a test plan that says exactly what the
  verifier can check over ssh and what needs the user's hands (touch).
