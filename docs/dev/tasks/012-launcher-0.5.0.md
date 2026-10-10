# 012 - Launcher 0.5.0 (brief)

Status: draft, 2026-10-10. Owner: Controller. Builder: launcher-dev. Verifier: deck-verifier (user runs physical tests).
Base: launcher v0.4.0 (main). Flow per ROLES.md: short-lived branch -> commit (OSRdesign, trailer) -> PR -> merge only on the user's word.

0.5.0 is a theme release. The first theme is the fallback on-screen keyboard. More features are still being evaluated and will be added below as separate numbered items; each gets its own evaluation report before it enters the build list.

## Scope

| # | Feature | Status | Evaluation |
|---|---------|--------|------------|
| F1 | Fallback on-screen keyboard, phases 0-2 | decided, ready to build | `reports/020-virtual-keyboard-evaluation.md` |
| F2 | Multi-board / multi-screen support | decided 2026-10-10: 0.5.0 targets Zero 2 W + Pi 3A+ only; Pi 5, HackBerry, Pi 4, Walnut later; uConsole dropped for now | `reports/021-multi-sbc-multi-screen.md` |
| F3.. | other launcher features | to evaluate | - |

Phase 3 of F1 (Mesh Hop embeds the component) belongs to the apps repo and ships with Mesh Hop 0.4.x, not with the launcher.

## F1 - Fallback on-screen keyboard

### Product rules (user decisions, 2026-10-10)
- The physical keyboard (USB or Bluetooth) stays the primary path. The on-screen keyboard is an automatic fallback only: it opens when a text field needs input AND no physical keyboard is present. The user cannot summon it and there is no hide key; tapping the field re-shows it.
- Settings > On-screen keyboard: Auto (default) / Off.
- A paired but asleep Bluetooth keyboard counts as present for N minutes after it was last seen (N configurable constant, start with 10 min; the M4 drops its device node when it sleeps).
- Tall keyboard, about 280-320 px, over the lower part of the 640x340 app window, with an echo line (masked for passwords).
- Characters: launcher pages accept UTF-8 (accents); compat apps US ASCII only.
- The on-screen Esc behaves like a physical Esc and counts for hold-Esc-to-exit like the toolbar Esc. It must ALWAYS send its release, even if the keyboard is hidden mid-press (else the Esc watchdog exits with 75, see the toolbar precedent in native_ui.cpp).
- Out of scope: terminal pages (native ST page), Calculator (keeps its own keypad), prebuilt hub apps and SDL apps (no field signal), third-party opt-in API (kept internal).

### Phase 0 - presence and multi-keyboard input (tier: savvy-heavy / launcher-dev)
Prerequisite for everything else: today the launcher reads only `/dev/input/bt-keyboard`, so a USB keyboard types nowhere.
1. Presence helper in `ext_components/cp0_lvgl` (C, with unit tests), a port of Mesh Hop's `keyboard_present()` rule (`apps/mesh-hop/src/main/core/input_state.cpp`): an `event*` node with KEY_A, KEY_Z, KEY_ENTER and KEY_SPACE, not BUS_VIRTUAL, name not starting with `applaunch-`. Hotplug by inotify on /dev/input, debounce 300-500 ms, 2 s safety poll.
2. Launcher keyboard thread (`cp0_lvgl_keyboard.c`) reads every device that passes the filter (libinput path context, add/remove on rescan). KEEP the fix that stops libinput re-adding a device after suspend/resume (duplicated keys bug).
3. The uinput hub forwarder (`native_vkbd.cpp`, `native_ui.cpp`) mirrors all of them to compat apps.
4. Launcher publishes policy: `$XDG_RUNTIME_DIR/applaunch/input.state`, atomic rename, `v=1 keyboards=N vkb=auto|off gen=G`.
5. Done when: unit tests pass; with only a USB keyboard, typing works in Wi-Fi password and in a compat app; BT keyboard behaviour unchanged (no duplicated keys); no on-screen keyboard exists yet.

### Phase 1 - component and launcher host (tier: savvy-heavy / launcher-dev)
1. Keyboard component in `ext_components/cp0_lvgl` (custom buttonmatrix): QWERTY, digits/symbols page, one-shot Shift + Caps, number-only pad for numeric fields, echo line, masked for passwords. The Controller supplies the visual design (section 2 of savvy-flow); the worker implements the structure from the mockup in the PR description of the brief hand-off.
2. Native occlusion rect in `cp0_lvgl_dpi_scaled.c`: native flush draws inside it, compat flush and external blit skip it, compat touch treats it as outside the window.
3. Suspend Settings > Touch gesture translation (list mode tap = Enter, swipe mode) while the keyboard is up; restore on hide. A tap in the window must never submit a half-typed field.
4. New injector: press AND release, both carrying `key_code` and `utf8` (the existing `cp0_keyboard_inject_text` is release-only and breaks consumers that read PRESSED).
5. Optional `cp0_keyboard_set_text_hint(text|number|password)` (default text); set it on Wi-Fi password (password), BT passkey (number), others (text).
6. Show/hide driven by `KBD_INPUT_CONTEXT_TEXT` (generation counter) combined with presence and the Auto/Off setting. Auto-dismiss on: field blur, physical keyboard appears (release latched modifiers), app exit/page change, lock/screensaver. Re-appear after a 1-3 s grace if the keyboard vanishes while a field is focused.
7. Opt-in for the sudo prompt (`cp0_sudo_async.cpp`, draws on lv_layer_top and never sets the TEXT context).
8. Pages covered: Settings Wi-Fi, Bluetooth PIN and alias, Settings > Apps, SSH form, sudo prompt. The BT PIN page alone justifies the phase (pairing the first keyboard).
9. Settings entry Auto / Off (persist in config.json).
10. Done when: PC tests (injector contract, presence, cp0_lvgl `run_tests.sh`) pass; build with APPLAUNCH_HW=pizero2w succeeds; deck test protocol written to `docs/dev/tests/launcher-0.5.0-keyboard-tests.md` (user runs it).

### Phase 2 - compat apps (tier: savvy-medium / launcher-dev)
1. Append `text_input` / `text_type` to the vfb header (`applaunch_vfb.h`, 4096-byte page, zero = no hint; keep `VFB1` or bump to `VFB2` with graceful fallback).
2. cp0_lvgl writes the field into `$APPLAUNCH_VFB` whenever the input context changes; the launcher reads it in `external_poll` (30 Hz).
3. Injection through `native_vkbd::send()` with evdev codes; Shift sequences for capitals and symbols; ASCII only.
4. Rebuild AppStore (submodule source untouched) and ZClaw; Store search is the test.

## Deck tests the user runs (written per phase)
No keyboard: Wi-Fi password, BT passkey, Settings > Apps source, sudo password. Wake M4 while the keyboard is open: it hides. Sleep M4 with a field focused: it appears after the grace delay (and the N-minute policy). On-screen Esc on each prompt: no launcher restart after 5 s. Tap in the window while typing: no submit. USB keyboard in/out.

## Blockers and open items
- Deck capture of `/proc/bus/input/devices` with M4 awake, M4 asleep, a USB keyboard and the RTL-SDR dongle, to confirm the filter ignores the dongle's IR node. Phase 0 must not merge before this is checked.
- M4 real sleep/reconnect timing (sets the grace delay; the N minutes policy covers napping).
- Docs after PASS (docs-writer): `decisions.md` rule change, Mesh Hop `app.json` wording ("there is no on-screen keyboard") when F1 phase 3 ships, README, release notes for 0.5.0.
- Release: tag v0.5.0 with pizero2w-bundle (build.sh --with-store) once all included features are PASS.

## F2 - Multi-board / multi-screen (decisions 2026-10-10)

Measured facts and evidence: `reports/021-multi-sbc-multi-screen.md` (8 addenda). Tools to bring up boards: `projects/APPLaunch/tools/board-probe/`.

### Decisions (user)
- **0.5.0 official targets: Pi Zero 2 W (today) and Pi 3A+ with the 3.5" 320x480 SPI panel.** Pi 5 + HyperPixel, HackBerry CM5, Pi 4 and Walnut Pi follow in later releases (they stay in the plan, nothing is thrown away). **uConsole dropped for now** (Debian 12, glibc 2.36; no second bookworm bundle).
- One 64-bit (aarch64) build, Debian 13 / glibc >= 2.41 images only. No armhf.
- **Layout: letterbox first.** Keep the native 640x480 UI and the 2x 320x170 compat window on screens that fit them; on the 480x320 Pi 3A+ panel use a 1x compat canvas (320x170 fits) and a native layout for 480x320 (to be designed by the Controller, small: top bar + home grid + toolbar). Fully adaptive layout is a later release.
- **Boards that boot into the Raspberry Pi OS desktop (Pi 5, HackBerry; future release):** leave the desktop installed and untouched. The user starts the launcher manually; starting it stops the desktop (lightdm), as in the 2026-10-10 tests. A launcher Settings item "Quit launcher and restore desktop" stops the launcher and starts lightdm again. Needs a polkit rule for stop/start of lightdm for the user (like the existing NM/time/power rules). No boot-to-console change by default; an opt-in autostart can come later. The F1 keyboard overlay stays off while a desktop session owns the screen.

### Build list for 0.5.0 (Zero 2 W + Pi 3A+)
1. **Runtime board profile** (`/etc/applaunch/board.conf`, written by install.sh from DT model + display probe; compile-time APPLAUNCH_HW stays only for what must be compiled): framebuffer device (never assume fb0; Pi 3A+ is /dev/fb1), colour depth (16 bpp RGB565 on the 3A+), logical landscape size, rotation (3A+: pre-rotate the landscape canvas 90 deg CLOCKWISE into the 320x480 portrait buffer), touch device selection by capability (ABS_MT_POSITION_X + INPUT_PROP_DIRECT, name only as a hint), touch mapping (normalise by the device's own ABS range, then the same rotation), backlight kind (sysfs backlight class with brightness, or on/off on `backlight_gpio`, or PWM overlay as on the deck), which Settings items to show (hardware profile).
2. **Display backend** (`cp0_lvgl_dpi_scaled.c` or a sibling): configurable fb path, 16/32 bpp blit, rotating blit of dirty rects, 480x320 logical canvas. Tier: savvy-heavy.
3. **Native layout for 480x320** (home grid, top bar, toolbar) after a Controller design pass; Settings pages run in the compat window at 1x there.
4. **Packaging:** install.sh detects the board and writes the profile and the per-board boot lines it needs; add `libc6 (>= 2.38)` to every app .deb Depends now (clean refusal on older OS); apps repo registry stays aarch64.
5. **Pi 3A+ specifics:** Settings > Brightness degrades to on/off; BT/Wi-Fi as on the deck; RAM 425 MB (watch the launcher and compat apps).
6. **Tests the user runs:** `probe-board.sh` and `fb-test-pattern.py` (done on both boards), then a per-board launcher checklist in `docs/dev/tests/launcher-0.5.0-multiboard-tests.md` (home grid orientation, touch accuracy in all four corners, Settings > Touch gestures, compat app 1x, Esc hold, brightness, reboot persistence).

### Open
- Pi 3A+ touch axes in the final rotated view (derive from rotation, verify in tests).
- Stock 320x170 apps on the 3A+: 1x compat window position and toolbar placement (Controller design).
- Pi 4 and Walnut have no screen test yet.
