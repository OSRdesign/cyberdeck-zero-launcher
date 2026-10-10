# 020 - On-screen keyboard as a fallback when no physical keyboard is present: feasibility

Date 2026-10-10. Read-only code analysis. No deck access, no build, no repo change except this file.
Tags: **V** = verified in the code at the path:line given. **I** = inferred, or needs a deck check / spike.
Paths are relative to `launcher/`. `apps/...` means `cyberdeck-zero-apps/apps/...`.

## Executive summary

1. **Feasible, with a hybrid design (option C).** Build one LVGL keyboard component in `ext_components`. The launcher shows it as an overlay on its own pages and over compat apps. Full-screen apps can embed it if they choose to.
2. **Prerequisite.** The launcher reads only `/dev/input/bt-keyboard`, one udev name ("M4 Keyboard"), so a USB keyboard types nowhere today. Keyboard detection has to match what the input stack actually reads, so multi-keyboard input comes first (phase 0).
3. **Two corrections to the brief (V).** Settings runs in the 320x170 compat window, not on native 640x480 pages. ZClaw is a compat app, not a full-screen app.

| App class | Can we tell automatically that a field needs text? | Coverage |
|---|---|---|
| (a) Launcher pages: Settings Wi-Fi, BT, Apps, SSH form, sudo prompt | Yes: `KBD_INPUT_CONTEXT_TEXT` already marks it. The sudo prompt needs a 1-line opt-in. | Phase 1, good |
| (b) Native full-screen apps (Mesh Hop) | Only inside the app. The launcher is blocked while the app runs. | Phase 3, each app opts in |
| (c1) Compat apps built against our cp0_lvgl (Store, ZClaw, LaunchWizard, MeshZero) | Yes, once cp0_lvgl publishes its text context into the vfb header. App sources stay unchanged. | Phase 2, ASCII only |
| (c2) Prebuilt hub binaries, (d) SDL apps (viz1090) | No: they emit no signal at all | Not covered |

**Biggest risk:** a sleeping Bluetooth keyboard removes its device node. For a BT user, "no keyboard" is therefore true most of the time, and the on-screen keyboard would pop up whenever the M4 sleeps unless the user picks a policy (D2).
**Second risk:** if an on-screen Esc is destroyed mid-press, the launcher restarts (Esc watchdog, 3.5 s).

---

## 1. Keyboard presence detection

### What exists now (V)

**Launcher**
- The keyboard thread uses a libinput *path* context with exactly one device: `LV_LINUX_KEYBOARD_DEVICE=/dev/input/bt-keyboard` (`projects/APPLaunch/pizero2w/APPLaunch.service:11`; `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_keyboard.c:573-584`, `600`, `723-728`).
- Attach and detach are tracked in a private `kc.dev` (`642-646`, `668-680`). This state is not exported.
- The thread is paused (`libinput_suspend`) for every external app (`ext_components/cp0_lvgl/src/cp0/cp0_external_app_runner.cpp:57`).
- A full-screen app blocks the launcher's UI thread inside `ExecBlocking` (`projects/APPLaunch/main/ui/launch.cpp:195-209`).
- So the launcher's own keyboard state is not usable while any app runs.

**udev**
- One rule, one name: `KEYBOARD_NAME="M4 Keyboard"` creates the `bt-keyboard` symlink (`projects/APPLaunch/pizero2w/install.sh:20`, `90-93`).

**USB keyboards and other apps**
- A USB keyboard is not read by the launcher. It is not forwarded to compat apps either: the hub mirrors only `APPLAUNCH_LINUX_KEYBOARD_DEVICE` (`projects/APPLaunch/main/ui/native_ui.cpp:594-598`, `native_vkbd.cpp:49-86`).
- Mesh Hop falls back to "any keyboard" when the symlink is missing (`apps/mesh-hop/src/main/ui/platform.cpp:332-343`).
- viz1090 reads `bt-keyboard` only (`apps/viz1090/build/viz_fb_shim.c:457`).

**Best existing detector: Mesh Hop `keyboard_present()`**
- It parses `/proc/bus/input/devices` (`apps/mesh-hop/src/main/core/input_state.cpp:28-79`, unit-tested at `apps/mesh-hop/src/tests/test_core.cpp:332`).
- A device counts as a keyboard when it has KEY_A, KEY_Z, KEY_ENTER and KEY_SPACE.
- It excludes names containing `applaunch-` and `Bus=0006` (virtual).
- An evdev twin, `is_keyboard()`, applies the same rule (`apps/mesh-hop/src/main/ui/platform.cpp:78-91`).

**The uinput hub**
- `applaunch-vkbd` is `BUS_VIRTUAL` and declares every key 1..0x2ff (`native_vkbd.cpp:101-109`). Any capability scan therefore sees it as a full keyboard.
- It is never destroyed (there is no `UI_DEV_DESTROY` in the file), so after the first app it is always listed.
- The bus/name filter above excludes it correctly.

**Other devices with key capabilities**
- The touch panel is found by the name "Goodix" (`cp0_lvgl_dpi_scaled.c:195-220`). Mesh Hop's `touch_score` rejects devices that have KEY_A (`platform.cpp:60-76`).
- The RTL-SDR dongle's infrared node (`docs/dev/decisions.md:20-23`) is an rc-core device whose key bits depend on its keymap. **I:** run `cat /proc/bus/input/devices` on the deck with every peripheral plugged in, to confirm that only real keyboards pass the A/Z/Enter/Space test.

### Proposal

**Definition.** "Keyboard present" means at least one `event*` node that:
- has KEY_A, KEY_Z, KEY_ENTER and KEY_SPACE,
- is not `BUS_VIRTUAL`,
- has a name that does not start with `applaunch-`.

This is the Mesh Hop rule, already tested.

**Bluetooth states**
- Connected but idle: the node exists, so the keyboard counts as present (I: BT HID keeps the node while the link is up).
- Asleep: the node is gone, so the keyboard counts as absent (V: comments at `cp0_lvgl_keyboard.c:570-572` and `cp0_external_app_runner.cpp:60-61`; Mesh Hop's placeholder "Wake the Bluetooth keyboard to type" at `apps/mesh-hop/src/main/ui/app.cpp:1696`).
- The user must decide how a paired-but-asleep keyboard is treated (D2).

**Hotplug.** inotify on `/dev/input` (IN_CREATE, IN_DELETE, IN_ATTRIB) triggers a rescan of `/proc/bus/input/devices` (a few KB). Debounce 300-500 ms and add a 2 s safety poll. **I:** devtmpfs supports inotify; the udev permission change arrives as IN_ATTRIB after IN_CREATE.

**Ownership: compute locally, publish policy centrally.**
- A small C helper in `ext_components` (a port of `input_state.cpp`) lets any process compute presence itself. This needs no IPC and keeps working while the launcher is blocked by a full-screen app.
- The launcher owns the *policy* (Settings toggle, BT-sleep rule) and publishes `$XDG_RUNTIME_DIR/applaunch/input.state`, written by atomic rename. Format: `v=1 keyboards=N vkb=auto|off gen=G`. A `/dev/shm` file is also possible, following the vfb precedent (`ext_components/cp0_lvgl/include/applaunch_vfb.h:21`).
- Apps watch the file with inotify or stat it at 1 Hz or less. If the file is missing, an app uses its own helper result and assumes `vkb=auto`.

**Phase 0 must also make input match detection.**
- The launcher keyboard thread must read every device that passes the filter. The libinput path context accepts several devices, added and removed on rescan.
- The hub forwarder must mirror all of them.
- Otherwise a present USB keyboard suppresses the on-screen keyboard but cannot type: a dead end.
- **Keep the existing fix** against libinput re-adding a device after suspend (`cp0_lvgl_keyboard.c:638-641`). Without it, keys are delivered N times (this happened before).

## 2. Detecting that a text field needs input, per class

### (a) Launcher in-process pages: centrally hookable

**Signal (V).**
- Every text-entry page calls `cp0_keyboard_set_input_context(KBD_INPUT_CONTEXT_TEXT)` on entry and restores the previous context on exit:
  - Wi-Fi: `projects/APPLaunch/main/ui/settings/settings_wifi_page.cpp:1002-1017`
  - Bluetooth agent PIN/passkey: `settings_bluetooth_page.cpp:1560-1577`
  - Bluetooth alias: `settings_bluetooth_page.cpp:2486`
  - Settings > Apps: `settings_apps_page.cpp:862-879`
  - SSH form: `page_app/ui_app_ssh.cpp:198-205`
- This is a documented contract (`projects/APPLaunch/AGENTS.md:167`).
- The context is an atomic value with a generation counter (`cp0_lvgl_keyboard.c:143-161`). A 100 ms `lv_timer` that compares the generation, or an observer added to the setter, is enough to detect changes.

**LVGL focus events cannot be used (V).** Pages keep their own `std::string` buffers and use `lv_textarea` only for display (the "hidden input" at `settings_wifi_page.cpp:634-663`; `LV_STATE_FOCUSED` is set by hand at line `609`).

**Gaps**
- The sudo password prompt (`ext_components/cp0_lvgl/src/cp0_sudo_async.cpp:280-368`) is drawn on `lv_layer_top` and never sets the TEXT context. It needs a 1-line opt-in.
- The field *type* is unknown. The BT passkey accepts at most 6 digits (`settings_bluetooth_page.cpp:1806-1809`); the Wi-Fi password is masked (`settings_wifi_page.cpp:600-609`). This needs a new optional call, for example `cp0_keyboard_set_text_hint(text|number|password)`, defaulting to text.

**Key injection: a new injector is needed (V).**
- `cp0_keyboard_inject()` fills `utf8` only for control keys (`cp0_lvgl_keyboard.c:296-313`).
- `cp0_keyboard_inject_text()` creates RELEASED-only items (`315-335`, line `327`).
- Consumers differ:
  - Wi-Fi reads text on PRESSED/REPEATED (`settings_wifi_page.cpp:1523-1524`, `1578-1580`).
  - The sudo prompt reads it on RELEASED (`cp0_sudo_async.cpp:704-714`).
  - Bluetooth uses `key_text()` (`projects/APPLaunch/main/ui/keyboard_text_input.cpp:33-40`).
- The new injector must therefore emit press and release, both carrying `key_code` and `utf8`, exactly as a real key does (`cp0_lvgl_keyboard.c:452-469`).

### (b) Native full-screen apps (Mesh Hop): the app must opt in

- **V:** while a full-screen app runs, the launcher's UI thread is blocked, its timers are off, and the app owns `/dev/fb0` (`launch.cpp:187-209`). The launcher can neither know about fields nor draw.
- Mesh Hop knows its own fields: the compose box (`apps/mesh-hop/src/main/ui/app.cpp:695-714`), `nav_.compose_focus`, `editor_open_`. It also already has a presence check, `keyboard_ready()` (`app.cpp:1192-1195`).
- **Opt-in API needed:**
  - The app links the component.
  - It calls `vkb_show(type)` / `vkb_hide()` on focus changes.
  - It receives keys through its own key path. Mesh Hop has `KeyEvent` and `Platform::inject_key` (`apps/mesh-hop/src/main/ui/platform.hpp:50-53`).

### (c) Compat 320x170 apps under the vfb shim

- **V:** the shim intercepts only framebuffer `open`/`ioctl`/`mmap` (`projects/APPLaunch/pizero2w/vfb/applaunch_vfb_shim.c:1-9`, `58-61`). Keys reach the app through the uinput hub. Nothing tells the launcher where focus is.
- The vfb header page is 4096 bytes and uses 9 fields (`applaunch_vfb.h:20`, `26-36`), so there is room to append `text_input` and `text_type`.

**(c1) Apps built against our `ext_components/cp0_lvgl`: covered without touching app sources.**
- These apps already set the TEXT context:
  - Store: builds against our ext_components (`projects/AppStore/SConstruct:23-24`); sets the context at `appstore_input_controller.hpp:25-31` and `appstore.cpp:200`
  - ZClaw: `projects/ZClaw/main/ui/zclaw_input_dialog.cpp:154`
  - LaunchWizard
  - MeshZero (draft)
- Make cp0_lvgl write the new header field into `$APPLAUNCH_VFB` whenever the context changes. Facts that make this work:
  - The launcher sets that variable for every compat app (`native_ui.cpp:590`).
  - The file is mode 0666 (`native_ui.cpp:480-482`).
  - The shim does not intercept that path.
- The launcher reads the field in `external_poll`, which already runs at 30 Hz (`native_ui.cpp:524-556`).
- Apps only need a rebuild. The Store submodule source, which must not change (`decisions.md:8`), stays untouched.

**(c2) Prebuilt hub binaries: impossible to detect.**
- **I:** they link cp0_lvgl statically. LD_PRELOAD cannot interpose a function defined inside the executable, and no syscall reveals that a field has focus.
- The only fallback would be manual summoning, which the user has excluded.

### (d) SDL / other full-screen apps (viz1090)

- Same situation as (b): the launcher is blocked.
- viz1090 has no free-text field, and the vizsetup GPS keypad is an accepted exception. Out of scope.
- Any other SDL app would have to draw its own keyboard.

## 3. How the overlay could be drawn

### Geometry (V)

- Compat display: 320x170, drawn at scale 2 into a **640x340 window at (0,0)** (`ext_components/cp0_lvgl/src/cp0/cp0_lvgl_dpi_scaled.c:639-645`).
- The native toolbar strip below is **640x140**: `bar_y = 340`, buttons 120 px high (`native_ui.cpp:419-440`).
- In compat mode the native flush skips every window row (`cp0_lvgl_dpi_scaled.c:146-158`), so nothing native can draw over the window today.
- The existing "overlay rect" works the other way round. It is a single rect, in compat coordinates, where compat LVGL shows through an external app's picture (`62`, `110-123`, `481-488`, `521-533`). The Esc ribbon uses it (`native_ui.cpp:531-541`).

**I, panel density:** 2.8" 640x480 is about 57 x 43 mm, so about 11.3 px/mm.

### Option A: launcher-owned overlay

**Where it can be drawn**
- *A-strip:* the keyboard replaces the toolbar in the 640x140 strip. No display-manager change is needed, the window stays fully visible, and the native indev already owns that area (`cp0_lvgl_dpi_scaled.c:258-266`). But it leaves only 3 rows of about 44 px (about 3.9 mm).
- *A-tall:* the keyboard is taller and covers the lower part of the window. This needs a "native occlusion rect" in `cp0_lvgl_dpi_scaled.c`: `flush_native` draws inside the rect, `flush_compat` and `external_blit` skip it, and compat touch treats it as outside the window. About 60-100 lines (I).
- *A-compat:* draw on the compat top layer through the existing overlay rect. This needs no new mechanism, but the keys get 2x pixel-doubling and the rect is shared with the Esc ribbon. An LVGL keyboard inside the app window was cut off in verification 011 (`docs/dev/reports/011.md:1`, `109`).

**Full-screen apps: impossible.** The launcher is blocked and there is no compositor. The app would also receive the same touches, because nothing grabs the devices (touch opened without EVIOCGRAB at `cp0_lvgl_dpi_scaled.c:443`; keyboard grab disabled at `cp0_lvgl_keyboard.c:173-179`).

**Touch routing (V)**
- Overlay touches go to the native indev.
- Inside the window, gesture translation must be suspended while the keyboard is up. Settings list mode turns any tap in the window into Enter (`cp0_lvgl_dpi_scaled.c:375-379`), which would submit a half-typed password. App swipe mode has the same problem (`317-341`; `native_ui.cpp:581-588`).

**Key injection**
- Launcher pages: the new UTF-8 injector (section 2a).
- Compat apps: `native_vkbd::send()` with evdev codes (`native_vkbd.cpp:131-137`). The app decodes them with an xkb "us" map (`cp0_lvgl_keyboard.c:603`). Capitals and symbols therefore need Shift sequences, and only ASCII is possible (I: no dead keys).

**Keeping the field visible**
- The window cannot be panned: it is top-aligned with `oy=0` (`cp0_lvgl_dpi_scaled.c:644-645`).
- Settings fields sit near the top, at compat y 48-52, which is physical y 96-104 (`settings_wifi_page.cpp:600`, `683`). They stay visible above a keyboard of up to about 300 px.
- For compat apps the field position is unknown. The only generic answer is an echo line on the keyboard: a local echo of what was injected, masked for passwords. The user decides on this (D3).

### Option B: a keyboard component inside each process

- Each process draws the keyboard on its own display, gets touches through its own indev, and writes text through its own path.
- This is the only option for full-screen apps. Mesh Hop builds inside the launcher tree with its SDK (`apps/mesh-hop/build/build.sh:8-20`; `apps/mesh-hop/src/SConstruct:21-22`, `63-64`), so it can link the component statically.
- It is useless for 320x170 apps: there is no room (report 011).
- In Mesh Hop the compose box sits at y 350-396 (`app.cpp:696-697`), under any keyboard at the bottom of the screen. Mesh Hop must move the box above the keyboard while it is open (app work).

### Option C: hybrid (recommended)

Use the B component everywhere. The launcher hosts it (A-tall or A-strip) for classes (a) and (c1). Full-screen apps embed it for (b).

| | A only | B only | C hybrid |
|---|---|---|---|
| Launcher pages | yes | yes (it is the launcher's process) | yes |
| Compat apps (c1) | yes (uinput, ASCII) | no (no room) | yes |
| Full-screen apps | no | yes, opt-in | yes, opt-in |
| Code written once | yes | yes | yes |

## 4. Keyboard design inputs (constraints only, no visual design)

**Layouts**
- QWERTY letters, a digits/symbols page, one-shot Shift and Caps Lock.
- A numeric-only pad for number fields: BT passkey (6 digits), Mesh Hop frequency and position.
- Possibly a hex pad for Mesh Hop's 32-character channel key.

**Characters**
- Launcher pages accept any UTF-8.
- Compat apps: US ASCII only.
- Mesh Hop uses its own translator, which detects the keyboard layout (`platform.cpp:95`).

**Passwords**
- The echo is masked.
- Wi-Fi toggles password visibility with Left Alt (`settings_wifi_page.cpp:1541-1543`, `1575-1577`). A keyboard key could send that.

**Key size (I, needs a user test)**
- With 10 columns, keys are 64 px apart (about 5.7 mm, close to a phone in portrait).
- Rows should be at least about 60 px (5.3 mm). With 4 rows plus an echo line, the keyboard is about 280-320 px tall.
- The 140 px strip allows only about 44 px rows (3.9 mm).

**Esc / Back**
- On-screen Esc should act exactly like a physical Esc: pages cancel on Esc *press* (Wi-Fi `settings_wifi_page.cpp:1562`, BT `settings_bluetooth_page.cpp:1775`).
- Every injected Esc sets the Esc state (`ext_components/cp0_lvgl/src/cp0_keyboard_queue.c:34`). The watchdog calls `_Exit(75)` after 3.5 s (`esc_ui_watchdog.h:14-19`, `esc_ui_watchdog.cpp:76-87`), and it is armed on every page (`launch.cpp:120-126`).
- So the keyboard must always send the Esc release, including when it is hidden mid-press (toolbar precedent: `native_ui.cpp:401-407`). Hiding the keyboard after a field blurs on Esc press must wait until the release.
- Open question: should the on-screen Esc take part in hold-Esc-to-exit, as the toolbar Esc does (`native_ui.cpp:387`)? (D5)

**Enter**
- Enter sends Enter (submit).
- Multi-line fields need a newline key or Shift+Enter (ZClaw uses Shift+Enter: `projects/ZClaw/README.md`).

**Hide key**
- Because users cannot summon the keyboard, a "hide" key would leave them with no way to bring it back. Either leave it out, or re-show the keyboard when the field is tapped (D6).

**Gestures (Settings > Touch)**
- While the keyboard is open, suspend list and swipe translation inside the window, and restore it on hide (`cp0_display_set_touch_mode`, `cp0_lvgl_dpi_scaled.c:390-397`).

**Auto-dismiss**
- On field blur: the context leaves TEXT, or the hint/vfb field is cleared.
- When a physical keyboard appears (debounced).
- When the app exits (`finish_external`, `native_ui.cpp:502-521`) or the page changes.
- When the lock screen or screensaver shows.
- It re-appears if the keyboard disappears while a field still has focus, after a grace delay for a BT reconnect (I: 1-3 s).

**Physical keyboard connected while the overlay is open**
- Hide the overlay immediately.
- Release any latched Shift/Ctrl through the hub, or the app keeps a stuck modifier.
- The text typed so far stays in the field, and typing continues on the physical keyboard.

## 5. Cost and risk

**RAM (I)**
- Keyboard objects take tens of KB.
- The fonts are already linked (Montserrat 24/32: `native_ui.cpp:349`, `367`).
- No new draw buffer is needed: the native buffer of 640x96x4 = 245 KB already exists (`cp0_lvgl_dpi_scaled.c:667-669`).
- Expect under 0.5 MB RSS per process, against about 20 MB for the launcher and 414 MB on the deck.
- `LV_USE_KEYBOARD` is already on in the launcher build (`projects/APPLaunch/build/config/lvgl_config.h:117`, a generated file). A custom buttonmatrix gives more control over layouts.

**CPU (I)**
- Redraws happen only on key events.
- inotify presence costs about nothing while idle.
- The occlusion test adds one comparison per row in the flush loops.
- Negligible on a Pi Zero 2 W.

**Code size (I)**

| Phase | Repo | Files touched / new | Lines |
|---|---|---|---|
| 0 Presence + multi-keyboard input | launcher | `cp0_keyboard_presence.c/.h` (new) + tests, `cp0_lvgl_keyboard.c`, `native_vkbd.cpp`, `native_ui.cpp`, state writer | 350-500 |
| 1 Component + launcher host | launcher | component (new, 2-3 files), `cp0_lvgl_dpi_scaled.c`, `cp0_display.h`, `native_ui.cpp`, `cp0_lvgl_keyboard.c` (injector, hint), 4 settings pages + `cp0_sudo_async.cpp` (hints), Settings toggle | 700-1000 |
| 2 Compat apps (c1) | launcher | `applaunch_vfb.h`, cp0_lvgl publisher, `native_ui.cpp` reader, Shift/ASCII mapping; rebuild Store | 200-300 |
| 3 Mesh Hop | apps | `app.cpp` (show/hide, compose relayout), `platform.*`, README, `app.json` text | 250-400 |

**API and versioning**
- Append the vfb header fields; zero means "no hint", so old apps and old launchers keep working. Keep the `VFB1` magic or bump it to `VFB2`.
- The state file carries `v=1`.
- The component is versioned with `ext_components/cp0_lvgl/sdk_version.txt`, which the app SConstructs read.
- Third-party prebuilt apps are unaffected: no hint means no keyboard.

**Risks**
- Popping up and hiding as the BT keyboard sleeps and wakes (D2).
- The Esc watchdog.
- A regression of the duplicated-keys bug in a multi-device keyboard thread.
- A tap in the window acting as Enter.
- A stuck Shift on the uinput hub.
- A page that forgets to restore the context keeps the keyboard up. AGENTS.md already requires restoring it on teardown.
- The product rule changes: `docs/dev/decisions.md` and the memory note "no virtual keyboard" must be updated, and Mesh Hop's `app.json` says "there is no on-screen keyboard".

**Tests I can run on the PC**
- The presence parser (port Mesh Hop's tests).
- The injector contract (`projects/APPLaunch/tests/test_keyboard_text_input.cpp` style).
- cp0_lvgl tests (`ext_components/cp0_lvgl/tests/run_tests.sh`).
- Mesh Hop headless screenshots (`platform.hpp:33-35`, `50-53`).
- The launcher overlay itself cannot be rendered on a PC: the dpi-scaled backend is fbdev-only, and the SDL build was never verified in this fork.

**Tests the user runs on the deck** (to be written per phase)
- With no keyboard: Wi-Fi password, BT passkey, Settings > Apps source and sudo password.
- Wake the M4 while the keyboard is open: it hides.
- Sleep the M4 with a field focused: it appears after the grace delay.
- On-screen Esc on each prompt: no launcher restart after 5 s.
- A tap in the window while typing: no submit.
- USB keyboard plugged in and out.
- Phase 2: Store search.
- Phase 3: Mesh Hop compose.

## 6. Recommendation and plan

**Choose C (hybrid).** One component, with the launcher hosting it for its own pages and for compat apps, and opt-in embedding for full-screen apps. Start with launcher pages. Pairing the *first* Bluetooth keyboard with a PIN currently needs a keyboard, so the Bluetooth PIN page alone justifies phase 1.

| Phase | Scope | Worker tier | Effort (I) |
|---|---|---|---|
| 0 | Presence helper + state file; launcher reads and forwards every real keyboard (USB and BT) | savvy-heavy (fragile keyboard thread history) | 1 worker run + 1 deck test round |
| 1 | Component + launcher host on Settings / SSH / sudo; occlusion rect, gesture suspend, UTF-8 injector, type hint, Esc safety, Settings "On-screen keyboard: Auto / Off" | savvy-heavy (display manager + input contract); keyboard look from the user's choices, savvy-fable only if the user asks for a design pass | 2 runs + 2 test rounds |
| 2 | Compat apps (c1): vfb hint published by cp0_lvgl, uinput Shift mapping, Store/ZClaw rebuild | savvy-medium | 1 run + 1 test round |
| 3 | Mesh Hop embeds the component, compose/editor relayout, docs | savvy-medium (apps-dev) | 1-2 runs + 1 test round |

### Decisions for the user

- **D1** Confirm the rule change: "no on-screen keyboard" becomes "an automatic fallback only when no keyboard is present, never summoned manually". Then record it in `decisions.md`.
- **D2** A paired but asleep BT keyboard: (a) counts as absent, so the keyboard shows and waking the M4 hides it; (b) counts as present for N minutes after it was last seen; (c) a Settings choice.
- **D3** Geometry: the 140 px strip (keeps the window whole, small keys) or a taller keyboard over the lower window (bigger keys); echo line yes or no.
- **D4** Character set: US only, or accents too (accents possible only on launcher pages and in Mesh Hop).
- **D5** Should the on-screen Esc count for hold-Esc-to-exit?
- **D6** A hide key, or none.
- **D7** Settings toggle Auto/Off: yes or no, and the default.
- **D8** Terminal pages (CLI/Python/SSH terminal: the native ST page, `page_app/ui_app_st.hpp:124`): out of scope or a terminal layout. The Calculator keeps its own keypad.
- **D9** Publish the opt-in API for third-party apps now, or keep it internal until Mesh Hop has proved it.

### Blockers

- D1.
- Phase 0 must land before any keyboard is shown, otherwise a USB keyboard creates a dead end.
- A deck capture of `/proc/bus/input/devices` with the M4 awake, the M4 asleep, a USB keyboard and the RTL-SDR dongle, to confirm the filter.

## Open questions

1. What is the M4's actual sleep and reconnect behaviour: idle timeout, and is the first keypress lost on wake? This sets the grace delay. (Deck test.)
2. Does any non-keyboard node on the deck expose A/Z/Enter/Space: the IR receiver, gpio-keys, a USB hub with a keypad? (Deck capture.)
3. Is the Store always launched in the compat window (built-in exec `@appstore_exec`, `projects/APPLaunch/main/ui/builtin_app_registry.cpp:76`)? It looks like it, but the path through `launch_Exec` was not traced end to end (I).
4. Should a keyboard echo line show prefilled text? In compat apps the launcher cannot know the field's existing content, only what it injected itself.
5. LanScan and Wi-Fi Survey have no text fields (their sources were not found to use the TEXT context; only their binaries contain cp0_lvgl strings), so they need nothing. Confirm whether future apps should follow Mesh Hop's opt-in pattern.
