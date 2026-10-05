# Decisions

Short, dated, never re-argued without the user. Newest first.

- **2026-10-05: Settings > Apps decisions (task 006).**
  - The home grid order is stored in the launcher's own config (`app_order` in `~/.config/cardputerzero/config.json`,
    FNV-1a keys of the `.desktop` names), because the Store backend (`projects/AppStore` submodule) cannot be changed.
  - Versions are compared the Debian way (`dpkg --compare-versions` semantics), never as text.
  - Sync failure reasons come from a curl probe of the source, because `--edit-registry` exits 0 for an unreachable
    source (it prints `REGISTRY UPDATED ... cached`). A source counts as synced only on a positive `ok`. The probe
    needed `execvp` in `run_program`: `execv` does not search PATH, so a bare `curl` never started.
  - After a failed install the backend's `pending-package.json` is parked aside
    (`pending-package.parked.<id>.<action>.json`) so other apps install, and it is restored before a retry of the
    same app, which then resumes. Deleting it would break that retry. After a wrong password or Esc the file may stay
    until the next operation; it blocks nothing.
  - Letter shortcuts compare the physical key code, not the ASCII value. W, E, R, T have evdev codes 17 to 20, equal
    to LVGL's up/down/right/left, so a real W/E/R/T press is recorded and the matching native key within 300 ms is
    ignored (same approach as Wi-Fi Survey). Real arrows and F/X/Z/C still work.

- **2026-10-04: a touch screen is found by its capabilities, never by a fixed `/dev/input/eventN`.** The numbers move
  (the RTL-SDR dongle's infrared node and the Bluetooth keyboard take event1 or event2). viz1090 0.1.1 scans
  `event*` for a device with multitouch X/Y and no keyboard keys, prefers a name with "goodix" or "touchscreen", and
  retries every second. `VIZ_TOUCH` stays as an override. Any new app that reads input does the same.
- **Fixed 2026-10-04: `deck.py sudo` exposed the sudo password.** It built `echo '<pw>' | sudo -S ...` as the
  remote command line, so `ps` on the deck, or a shell error that echoes the command, showed it (it happened twice in
  report 006; nothing was written to a file). Now the command is `sudo -S -p '' sh -c <shlex-quoted command>` and the
  password is written to the ssh channel's stdin, never in the command text (checked: `ps` on the deck shows no
  password). The password from those two runs should still be rotated if the tool logs of those runs are kept. Also: `deck.py put` with a Git Bash path
  such as `/tmp/x` is rewritten by MSYS, use `MSYS_NO_PATHCONV=1`.
- **2026-10-04: apps run in the launcher's 640x340 window**, under the shared top bar (320x170 drawing scaled 2x),
  not in X-Fullscreen. Decided by the user for Wi-Fi Survey. Full-screen stays an opt-in for apps like viz1090.
- **2026-10-04: a detail view follows the item's identity, not its list position.** Wi-Fi Survey tracks the access
  point by BSSID; if it vanishes the title shows "- gone" with the last values, and it recovers when it returns.
  R only rescans on every screen (held R does not switch screens).
- **2026-10-04: lean agent setup**: Controller plus `launcher-dev`, `apps-dev`, `deck-verifier`, `docs-writer`.
  Pilot task: Wi-Fi survey. One agent per app only when apps are built in parallel.
- **2026-10-04: apps live in the apps repo**, installed from Settings > Apps (user-added sources only). The launcher
  gets generic capabilities only (full-screen flag, shared top bar).
- **2026-10-04: normal boot lines stay visible.** No `quiet` / `fbcon=map` on the boot line. The launcher service
  waits for `systemctl is-system-running --wait` so nothing prints over the grid.
- **2026-10-03: the built-in CardputerZero Hub stays in the Store**, not in Settings > Apps.
- **2026-10-03: no root-free package installs.** Installing from Settings > Apps asks for the sudo password.
- **Releases and pushes need the user's explicit go-ahead.**
- **ADS-B Radar (native LVGL) was dropped on 2026-10-05**; viz1090 is the ADS-B app. Source archived outside the repos.
