# Decisions

Short, dated, never re-argued without the user. Newest first.

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
