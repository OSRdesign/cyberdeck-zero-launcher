Task: 003 viz1090 touch fix        Agent: apps-dev        Result: done (built, not tested on the deck)

Changed:
- C:\CLAUDE\zero7\cyberdeck-zero-apps\apps\viz1090\build\viz_fb_shim.c
  - The touch screen is found by scanning /dev/input/event0..31 when VIZ_TOUCH is unset or empty. A device qualifies if
    it reports ABS_MT_POSITION_X and ABS_MT_POSITION_Y (EVIOCGBIT) and does not report KEY_A (so no keyboard).
    A name containing "goodix" or "touchscreen" (case-insensitive) wins over any other multitouch device.
  - VIZ_TOUCH stays as an explicit override (opened as given).
  - When no touch device exists, or it disappears (read error), touch_fd goes back to -1 and the scan is retried
    once a second. Before this it stayed closed for good (touch_fd = -2 / -1). State (slots, pinch, close-button
    flags) is reset on every (re)open.
  - Unchanged: swap/invert env defaults, normalisation, finger/pinch events, close button, keyboard path.
  - Header comment updated (default is now "found by scanning").
- C:\CLAUDE\zero7\cyberdeck-zero-apps\apps\viz1090\root\opt\viz1090\libviz_fb.so (rebuilt)
- C:\CLAUDE\zero7\cyberdeck-zero-apps\apps\viz1090\app.json: version 0.1.0 -> 0.1.1
- C:\CLAUDE\zero7\cyberdeck-zero-apps\packages\viz1090_0.1.1_arm64.deb (new; viz1090_0.1.0 left in place)
- C:\CLAUDE\zero7\cyberdeck-zero-apps\registry.json: only the viz1090 entry changed (0.1.1, new url/md5/sha256/size).
  md5 7f197fef8a3aaa6ba500feb144e0ca7f, size 5482718.

Other hardcoded assumptions: none. vizsetup.cpp has no device paths (it runs through the same libviz_fb.so, so the
intro screen gets the fix too); run_viz1090.sh and the .desktop file do not mention event nodes or VIZ_TOUCH;
the keyboard uses /dev/input/bt-keyboard (symlink, retried every second) and is untouched. build/README.md does
not name event1.

How it was checked:
- Cross-compiled in WSL under flock /tmp/wsl-build.lock with aarch64-linux-gnu-gcc (-shared -fPIC -O2 -Wall, no
  warnings) from viz_fb_shim.c + cp0_statusbar.c. No arm64 SDL2/freetype dev packages exist in WSL, so SDL2 2.30
  headers (amd64 libsdl2-dev, extracted into the scratchpad, SDL_DISABLE_*INTRIN_H) and a stub libfreetype.so.6 for
  the NEEDED entry were used. NEEDED list matches the old .so (libfreetype.so.6, libm, libc). The official
  build_on_pi.sh route (native on the Pi) was not used.
- python tools/make_registry.py, then restored from the starting working copy (backups in the session scratchpad):
  lanscan_0.1.1_arm64.deb (md5 09abd3dc...), lanscan_0.1.0, wifi-survey_0.1.0 (md5 9f54fe19...), and their
  registry entries are identical to before. dpkg-deb -I/-c on the new package look right.
- Not run on the deck; no screenshots.

Not done / risks:
- Detection logic is untested on real hardware. Rebuilding on the Pi with build_on_pi.sh would give a native .so if
  the cross-built one misbehaves (unlikely: only libc/libm/freetype symbols are used).
- If a second multitouch non-keyboard device exists (a trackpad) and the Goodix name does not match, the first one
  found is used; override with VIZ_TOUCH.
- Nothing committed or pushed; lanscan 0.1.1 files remain uncommitted.

Needs from the Controller: hand to deck-verifier.

Verifier test plan (install viz1090 0.1.1 via Settings > Apps or copy the .deb):
1. With the Bluetooth keyboard connected (it takes event1) start viz1090: touch must work (this was the bug).
2. Same with the keyboard asleep / not present, then wake it while the app runs: touch still works, keyboard starts
   working after a moment (bt-keyboard symlink).
3. Intro screen (vizsetup): tap each button and the airport-code field, start; keyboard typing still works there.
4. Map: tap on an aircraft, one-finger drag pans, two-finger pinch zooms (also check pinch direction is not reversed).
5. Close button (top right): a tap quits to the launcher; the map does not see that touch.
6. Keyboard: arrows, +/-, PageUp/Down, Esc still work.
7. Optional: start with VIZ_TOUCH=/dev/input/eventN set to confirm the override, and check journalctl/stderr for
   "[viz_fb] touch screen opened". Check that quitting and restarting the app repeatedly keeps touch working
   (the order of event nodes may change after a reboot).
