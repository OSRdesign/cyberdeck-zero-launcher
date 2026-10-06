# x86 port options: Panasonic FZ-M1 tablet

Author: `x86-architect`, 2026-10-07. Research and design only: no product code, no branch, no commit.
Measured = run on the tablet over read-only ssh, or a scratch build in WSL. Estimate = judgement, stated as such.

**Short answer:** the existing launcher already builds for x86_64 almost as it is (measured), and its 640x480 display
backend reads the panel size at runtime, so it fits the tablet's 1280x800 panel with no change. Port the current
LVGL launcher straight onto the tablet's framebuffer (Option A), with no display server. Add an LVGL virtual
keyboard with a terminal layout and an app layout. Publish a registry per architecture. Add a Wayland kiosk session
later, as an optional way to run the occasional desktop program (Option D).

## 1. Target facts

All values were measured on 2026-10-07 on the user's unit (host `toughpad`), unless a source is given.

| Area | Fact | How known |
| --- | --- | --- |
| Model | Panasonic **FZM1V-2** (FZ-M1 mk2 "Value"), AMI BIOS V2.50L20 (09/09/2016) | measured, `/sys/class/dmi/id` |
| CPU | Intel Atom x5-Z8550 (Cherry Trail / Cherryview), 4 cores, 0.48-2.4 GHz, SSE4.2/AES/MOVBE, **no AVX** | measured, `lscpu` |
| RAM | **1870 MiB usable (2 GB)**, plus 935 MiB zram swap, about 340 MiB used at idle. Retail specs list 4 GB for this model ([pbtech](https://www.pbtech.com/product/NBKPAN1209/app)), but this unit has 2 GB | measured |
| Storage | **128 GB eMMC** (`mmcblk0`, 116.5 GiB), not an SSD. 4.1 GB used | measured |
| Firmware | **64-bit UEFI** (`fw_platform_size` = 64), `BOOTX64.EFI`. The 32-bit UEFI problem of other Cherry Trail tablets does not apply. The standard Armbian image boots (Armbian has no ia32 UEFI support: [forum](https://forum.armbian.com/topic/23996-please-add-ia32-uefi-support/)) | measured |
| OS | Armbian 26.8.3, Ubuntu 26.04 "resolute", kernel **6.18.44-current-x86**, **glibc 2.43**, Python 3.14, no gcc. 340 packages, systemd `graphical.target`, but no display manager and no X11/Wayland installed | measured |
| GPU / KMS | i915 "cherryview" (Gen8), **KMS active**, `card1-eDP-1` connected at **1280x800**. Boot line has `i915.force_probe=*` | measured |
| Framebuffer | `/dev/fb0` = `i915drmfb`, **1280x800, 32 bpp** (`DRM_FBDEV_EMULATION=y`, `FB_DEVICE=y`). The panel is natively landscape, so no rotation is needed | measured |
| Panel size | EDID gives 152 x 94 mm, so **7.0 in and 214 ppi** (the deck is 2.8 in at about 286 ppi) | measured, EDID bytes 66-68 |
| Backlight | `/sys/class/backlight/intel_backlight` (raw, max 6000) | measured |
| Touch | **`SHRP0001:00 04DD:991B`** (Sharp), I2C-HID with `hid-multitouch`, `/dev/input/event11`. The kernel does not call it "Goodix" | measured |
| Built-in keys | `AT Translated Set 2 keyboard` (i8042: **present even with no keyboard attached**), `gpio-keys`, `Intel Virtual Buttons`, `Intel HID events`, `Panasonic Laptop Support` | measured |
| Attached now | Telink 2.4 GHz keyboard and mouse receiver (USB). **Intermec ED40 barcode scanner, which shows up as a full HID keyboard** | measured |
| Sensors | HID sensor hub through the Intel ISH: `accel_3d`, `gyro_3d`, `magn_3d`, `als`, `incli_3d`, `dev_rotation` (two sets). `iio-sensor-proxy` is not installed | measured |
| Battery | ACPI `BAT1` (Panasonic FZ-VZSU95, Li-ion, 49 Wh design), plus `AC` | measured |
| Wi-Fi | Intel 8260 class, `iwlwifi` (8000C firmware 36), `wlp1s0`, 5 GHz connected | measured |
| Bluetooth | Intel `8087:0a2b`, `btusb` + `btintel`, firmware `ibt-11-5`, `hci0` | measured |
| Audio | SOF `sof-bytcht rt5645` and HDMI LPE audio | measured |
| Networking stack | **netplan, systemd-networkd and wpa_supplicant. No NetworkManager and no BlueZ installed** (the launcher's Wi-Fi and Bluetooth backends need both) | measured |
| Permissions | user `osrde` is in `video`, `input`, `render`, `audio` and `sudo`. `/dev/uinput` is root-only (the deck needs a udev rule for the same reason) | measured |
| Suspend | `s2idle` only | measured |

**glibc and libraries (measured).** A native x86_64 scratch build of the current launcher (see section 2)
needs `GLIBC_2.38`, `GLIBCXX_3.4.30` and `CXXABI_1.3.13`. It links against `libinput.so.10`, `libxkbcommon.so.0`,
gio/gobject/glib 2.0, `libfreetype.so.6` and libstdc++. That is the same set as the deck binary, which also needs
`GLIBC_2.38`. Ubuntu 26.04 has glibc 2.43, so binaries built against glibc 2.39 or 2.41 run on it unchanged. Nothing
in the sources depends on the Ubuntu 26.04 version. Binary size is 7.9 MB of text (14 MB unstripped).

**Open questions for the user** (none of them blocks phase 1):
1. Is the barcode scanner, or any USB/BT keyboard, normally attached? The answer decides when the on-screen keyboard
   appears.
2. Is the tablet used in landscape only, or also in portrait (auto-rotate)?
3. May the tablet's networking move from netplan/networkd to NetworkManager? It is needed for the launcher's Wi-Fi
   page and top bar.
4. Should the normal boot lines stay visible on the tablet, as on the deck?

## 2. What in the current code is hardware-specific

Measured: a scratch copy of the tree builds for x86_64 with the native device backend `config_defaults.mk`
(`CONFIG_APPLAUNCH_LINUX_CP0`). Only the two NEON lines were removed. Every source file compiled. The link needed
`PKG_CONFIG_PATH` only because the -dev packages sat in a scratch sysroot. The scratch build lives in
`/tmp/x86scratch` in WSL, outside the repos. The table lists what still ties the code to the deck.

| Area | Files | Why it is specific | Effort to generalise |
| --- | --- | --- | --- |
| Build config | `projects/APPLaunch/*config_defaults.mk`, `pizero2w/build.sh` | NEON flags. Only aarch64 cross scripts exist. | **S**: one `linux_x86_64_native_cp0_config_defaults.mk` and a `--arch` in build.sh |
| Include paths with no sysroot | `ext_components/cp0_lvgl/SConstruct` (`toolchain_path` for dbus, freetype) | With an empty sysroot, `Path("")/usr/include/dbus-1.0` becomes a relative path. My scratch build went around it with wrapper flags. | **S**: use pkg-config when no sysroot is set (about 5 lines) |
| Display | `cp0_lvgl_dpi_scaled.c` | Already runtime: it reads the fbdev size and computes the integer scale. 1280x800 gives scale 4: a 1280x680 compat window and a 120 px toolbar. Only the name and the env var are deck-flavoured. | **none** for phase 1 (rename optional) |
| Touch discovery | `cp0_lvgl_dpi_scaled.c` `find_touch_device` | It looks for a name containing "Goodix". The tablet has `SHRP0001`. | **S**: find by capabilities, as decided on 2026-10-04 for viz1090 (about 30 lines) |
| Keyboard input | `cp0_lvgl_keyboard.c` | It reads **one** evdev path (`LV_LINUX_KEYBOARD_DEVICE`). The tablet has several keyboards that come and go, and the i8042 one is always there. | **S-M**: libinput udev seat (all keyboards) plus an ignore list (about 60 lines) |
| Native layout | `ui/native_ui.cpp` (`kCols=3`, `kTileH=188`, `kBarH=56`, fonts 24/32 px), `native_screensaver.cpp`, calculator page | It already uses the display width and height, but the constants were tuned for 640x480 at 286 ppi. At 1280x800 the tiles would be small. | **S-M**: one `ui_scale()` from panel size or ppi, applied at about 20 sites |
| Compat (stock) pages | Settings and stock apps, 320x170 | Fixed by design. They are shown scaled 4x. | **none** |
| Terminal | `ui/page_app/ui_app_st*.cpp` | Already sizes its grid from the display and the font. | **none** |
| Backlight | `cp0_lvgl_settings.cpp` (`/sys/class/backlight/backlight/...`) | The path is hardcoded. The tablet has `intel_backlight`. | **S**: first entry in `/sys/class/backlight` (about 10 lines) |
| Battery | `cp0_lvgl_bq27220.cpp`, `settings_hw_profile.hpp` (Battery hidden) | It looks for a bq27* gauge. The tablet has ACPI `BAT1`. | **M**: any `power_supply` with `TYPE=Battery`, plus a battery icon in the shared top bar |
| Settings menu profile | `settings_hw_profile.hpp`, `main/SConstruct` (`APPLAUNCH_HW=pizero2w`) | The profile is chosen at compile time. | **S** as a second compile-time profile, **M** if made runtime (about 15 `#if` sites) |
| Wi-Fi / Bluetooth | `cp0_lvgl_network.cpp` (nmcli/NetworkManager), `cp0_bluez_dbus_client.cpp` | Generic Linux, but the tablet has neither NetworkManager nor BlueZ. | **0 code**: install packages (a decision) |
| External stock apps | `native_ui.cpp` `run_external`, `pizero2w/vfb` shim, `native_vkbd` | Generic Linux (shm framebuffer, uinput). The shim must be rebuilt for amd64. | **S** |
| Install / service | `pizero2w/install.sh`, `APPLaunch.service`, udev and polkit rules, `config.txt.snippet`, backlight overlay | Pi boot config and overlay. Touch swap and invert env values. | **S**: an `x86/` copy without the Pi parts |
| Apps packaging | apps repo `tools/make_registry.py` (hardcodes `_arm64.deb`), `build_deb.py --arch` exists | The registry carries no architecture. The Store backend reads `download.url` only (checked by grep). | **S-M**, see Option A |
| Launcher updater | `cp0_launcher_updater.hpp` (`applaunch_arm64.deb`) | Upstream updater, disabled on the deck profile. | none (keep it disabled) |

## 3. Options

Token-cost scale (estimates): **S** is under 1 agent session and about 300 changed lines. **M** is 2 to 4
sessions and 300 to 1,200 lines. **L** is more than 4 sessions, or a rewrite.

### A. Current LVGL launcher on the tablet's framebuffer, no display server (recommended)

- **What it is.** The same binary design as on the deck, built natively for amd64, drawing to `/dev/fb0`
  (i915 fbdev emulation) through the existing dpi-scaled backend. Touch and keys come from evdev/libinput. The
  launcher runs as a systemd user service, as on the deck.
- **What changes.** Everything in the S rows of section 2. On top of that: an **LVGL virtual keyboard**, a
  `ui_scale()` for native screens, ACPI battery support, and per-arch app registries.
- **What is reused.** All of it: cp0 services (NetworkManager, BlueZ, sudo, timedated), the native home grid, the
  compat window, the toolbar, the shared top bar, the Esc-hold watchdog, the terminal, Settings > Apps, the Store
  backend, the vfb shim and the uinput hub.
- **RAM and CPU fit.** The deck runs at about 20 MB RSS (measured on the deck earlier). Estimate on the tablet:
  25-35 MB, out of 1.5 GB free. LVGL renders in software on the CPU (there is no SSE path, which is fine). A full
  1280x800 redraw is estimated at 30-60 ms on the Atom, and partial redraws are the normal case.
- **Touch, keyboard and virtual keyboard.**
  - **Touch:** found by capabilities. The tablet panel needs no axis swap (to be confirmed in phase 1).
  - **Physical keyboards:** all keyboards are read through libinput. A keyboard counts as "present" when a USB or
    BT device with letter keys exists and is not on an ignore list (the i8042 keyboard and the barcode scanner by
    default).
  - **Virtual keyboard:** one launcher component built on LVGL's `lv_keyboard` (MIT) with two layouts:
    - **Terminal:** a top row with Esc, Tab, Ctrl and Alt (sticky), arrows, `| ~ / - _`, plus a symbol layer
      `& ; < > $ * { } [ ] \ " '`.
    - **Apps:** QWERTY with a number row, and a numeric pad when the field is numeric.
  - **Where the keys go:** into LVGL text areas for the launcher's own pages, and into the existing **uinput hub**
    (`native_vkbd`) for the terminal and external apps. Any program that reads evdev sees them as real keys.
  - **When it shows:** automatically when no physical keyboard is present and a text field or the terminal has
    focus, plus a toggle key in the toolbar.
  - **The deck keeps its rule:** the keyboard is off there (a profile or setting), so its "no virtual keyboard"
    rule is untouched. This is a **tablet-specific decision**.
- **Packaging.**
  - **Package names:** one source tree. Each package is built as `<pkg>_<ver>_arm64.deb` and `<pkg>_<ver>_amd64.deb`
    (`build_deb.py --arch`). Scripts and data use `Architecture: all`.
  - **Registries:** `make_registry.py` writes `registry.json` (arm64, unchanged, so existing decks are unaffected)
    and `registry-amd64.json`. `app.json` gets an optional `"arch": ["arm64", "amd64"]`.
  - **Settings > Apps:** maps `owner/repo` to the registry for `dpkg --print-architecture` (about 20 lines in
    `apps_backend.cpp`). The Store backend is not changed, which respects the 2026-10-05 decision. If something
    still goes wrong, dpkg refuses a wrong-arch package with the existing "dpkg error" panel.
- **How apps are built.**
  - **LVGL apps:** lanscan, wifi-survey, meshzero and mesh-hop are built natively for amd64 in WSL. WSL is
    x86_64 Ubuntu 24.04 with glibc 2.39, so the binaries run on Ubuntu 26.04 and on Debian 13. It needs a one-time
    install of the -dev packages in WSL, or a Debian 13 container.
  - **viz1090:** its Pi-native build script needs an amd64 variant (it is SDL2 and already drives its own
    framebuffer bridge).
  - **CI:** optional later. GitHub has x86_64 and arm64 Ubuntu runners.
- **Risks.**
  - **i915 fbdev mmap writes may not reach the screen.** Mitigation: the code already has the LVGL DRM path
    (`LV_USE_LINUX_DRM` in `cp0_lvgl_freambuffer.c`). A KMS dumb-buffer variant of dpi-scaled is about 150 lines.
    Phase 1 checks this first.
  - **Console text can draw over the launcher.** fbcon messages on tty1 can scribble on the screen (the same issue
    as on the deck). Mitigation: wait for boot, as on the deck, and optionally switch the VT to `KD_GRAPHICS`.
  - **NetworkManager replaces networkd** for Wi-Fi. The change must be done on the console, because ssh over Wi-Fi
    can drop during the switch.
  - **No desktop programs (browser, GUI tools)** run in Option A alone. Option D covers that.
- **Token cost: M.**
  - Phase 1 (it runs on the tablet): S, 1 session.
  - Virtual keyboard: M, 1-2 sessions, about 400-500 lines.
  - Scale, battery and profile: S-M.
  - Multi-arch apps: S tooling plus one build per app.
  - Total estimate: about 1,000-1,400 lines over 5-7 sessions.
- **Pros:** reuses the most. One product on both devices. Lowest RAM. No GPL in the process tree beyond the system
  daemons the deck already uses. The deck keeps working unchanged.
- **Cons:** software rendering. Desktop software needs the Option D escape hatch. The virtual keyboard is our own
  code (about 500 lines).

### B. Launcher as a client of a kiosk compositor (cage, labwc or sway), apps free to be windowed

- **What it is.** A Wayland compositor owns KMS. The launcher becomes a full-screen Wayland or SDL2 client. Other
  programs (foot terminal, Firefox, SDL2 viz1090) run as normal windows. The virtual keyboard is `wvkbd` (GPL-3,
  needs layer-shell: labwc or sway, not cage) or `squeekboard` (GPL-3, heavier).
- **What changes.** The display layer: dpi-scaled must draw into a window buffer, and touch comes from Wayland/SDL
  events. The cp0 build currently selects the SDL backend *for all services*, and the SDL services are simulator
  stubs (for example `sdl_lvgl_network.cpp`), so mixing the SDL display with the real cp0 services means reworking
  the `cp0_lvgl` build split. X-Fullscreen framebuffer apps (the viz1090 bridge) stop working under a compositor
  and must be rebuilt as Wayland clients. The session also needs a seat (seatd or logind), a compositor config, and
  rotation and touch mapping in the compositor.
- **What is reused.** The UI code, the services after the build split, and packaging (same as A).
- **RAM and CPU fit.** Estimate: compositor 30-80 MB, wvkbd about 5 MB, squeekboard about 50 MB. It fits. Rendering
  goes through the GPU only for other clients; the launcher still renders in software.
- **Touch, keyboard and virtual keyboard.** The keyboard comes free and works in every Wayland app. The terminal
  layout comes from the wvkbd layouts (custom layouts are possible).
- **Risks.** It changes how the deck and the tablet differ: the deck cannot run a compositor comfortably on 414 MB,
  so two display stacks must be maintained. Hold-Esc and the shared top bar need compositor cooperation.
- **Licences.** labwc (GPL-2), wvkbd (GPL-3) and squeekboard (GPL-3) run as separate processes, so the repo stays
  MIT, but a bundled image must ship their sources and notices. cage and sway are MIT.
- **Token cost: L.** Build split, new display backend, session setup, rebuilt fullscreen apps. Estimate:
  1,500-2,500 lines and 8 or more sessions.
- **Pros:** any Linux GUI program runs. The on-screen keyboard is ready-made.
- **Cons:** the most new code and testing. It diverges from the deck. More moving parts on a 2 GB Atom.

### C. Standard mobile shell (Phosh or Plasma Mobile), launcher reduced to one app

- **What it is.** Ubuntu's packaged Phosh (with squeekboard, which has terminal layouts) or Plasma Mobile. LVGL apps
  run as Wayland clients. The launcher's home grid, Settings > Apps and top bar are replaced by the shell's own.
- **What changes and what it costs.** Little new code for the system itself. But the product identity (home grid,
  Settings > Apps, shared top bar, key rules) is lost on the tablet, and every LVGL app needs a Wayland build.
- **RAM and CPU fit.** Estimate: 400-700 MB, and GTK4 animations are sluggish on Cherry Trail. It fits 2 GB with
  little left over.
- **Licences.** GPL-3 shell (separate processes, so the repo stays MIT).
- **Token cost: M** for setup, plus an ongoing cost because two products diverge.
- **Verdict:** a reference for UX only. It throws away what works.

### D. Option A plus an on-demand Wayland session for desktop programs (later add-on)

- **What it is.** A generic launcher capability, `X-Session=wayland` in a `.desktop` file. The launcher stops
  drawing (as for `X-Fullscreen`), releases the display, and starts `labwc` (or `cage`) with `wvkbd` and the
  program. When the program exits, the launcher repaints everything.
- **Cost.** About 60 lines in `native_ui.cpp`, plus the system packages labwc, wvkbd and foot or a browser.
  **Token cost: S.**
- **Risk.** Moving DRM ownership between the launcher and the compositor and back. On the i915 fbdev path the
  kernel restores the console mode, so the launcher only has to force a full redraw.
- **Use.** A browser, GUI tools and any X11 or Wayland app, without paying for Option B every day.

## 4. Comparison and recommendation

| | A: LVGL on fbdev | B: kiosk compositor | C: mobile shell | D: A + on-demand session |
| --- | --- | --- | --- | --- |
| Reuse of current code | highest | medium (display split) | low | highest |
| New code (estimate) | 1,000-1,400 lines | 1,500-2,500 lines | little, but product rewrite | A + about 60 lines |
| Token cost | **M** | **L** | M + ongoing | **M + S** |
| RAM in use (estimate) | 25-35 MB | 80-200 MB | 400-700 MB | A, + about 100 MB while a session runs |
| Same stack as the deck | yes | no | no | yes |
| CLI keyboard | own LVGL keyboard (terminal layout) | wvkbd or squeekboard | squeekboard | own, plus wvkbd in sessions |
| Desktop GUI programs | no | yes | yes | yes (on demand) |
| GPL in the process tree | system daemons only | labwc, wvkbd | shell | system daemons, plus labwc/wvkbd during a session |
| Biggest risk | fbdev writes on i915 (DRM fallback exists) | build split, two stacks | product loss, speed | display handover |

**Recommendation: Option A now, Option D as an optional add-on.** It uses the stack that already works on the deck,
keeps one product on two devices, uses the least RAM, and needs the fewest new lines. The scratch build shows the
risk is low: the code compiles for x86_64 unchanged, and the display backend already adapts to 1280x800.

**Phase plan.**

| Phase | Content | Owner | Size | Needs from the user |
| --- | --- | --- | --- | --- |
| 1. Runs on the tablet | amd64 native config, cp0_lvgl pkg-config fix, touch found by capabilities, all keyboards through libinput, generic backlight, `x86/` service, udev rules and install script, rebuilt vfb shim. Result: home grid, Settings, terminal, Store, stock apps 4x | launcher-dev | S, 1 session | Approve package installs on the tablet (network-manager, bluez, libinput10) and the networkd to NetworkManager switch, done on the console. Install the -dev packages in WSL. Physical test |
| 2. Virtual keyboard | LVGL keyboard with terminal and app layouts, uinput output, auto-show from keyboard presence, toolbar toggle, off on the deck | launcher-dev | M, 1-2 sessions | Decisions 3 and 5. Touch tests |
| 3. Fits the 7 in panel | `ui_scale()` for native screens, ACPI battery (top bar and Settings), x86 Settings profile | launcher-dev | S-M | Decisions 4 and 6 |
| 4. Apps on two architectures | `app.json` `arch`, per-arch `.deb` and `registry-amd64.json`, Settings > Apps chooses by architecture, amd64 builds of lanscan, wifi-survey, meshzero and mesh-hop, then viz1090 | apps-dev + launcher-dev | S tooling + 1 build per app | Decision 7. After task 010, to avoid clashing with apps-dev |
| 5. Optional | Auto-rotate (iio accel), Option D sessions (browser and GUI programs) | launcher-dev | S each | Decisions 6 and 8 |

Phase 1 alone gives a usable tablet with a physical keyboard. Phase 2 makes it usable without one.

## 5. Decisions the user must take before a brief

1. **Approach:** Option A (with D later), or B.
2. **Tablet system changes:** install NetworkManager and BlueZ, and move Wi-Fi from netplan/networkd to
   NetworkManager. Without this, the Wi-Fi page and the top bar have no data.
3. **Virtual keyboard policy on the tablet:** auto-show when no physical keyboard is present and a field or the
   terminal has focus, plus a toolbar toggle. Confirm that the deck keeps "no virtual keyboard", either as a runtime
   setting defaulting to off or compiled out on the deck.
4. **Device profile:** a second compile-time Settings profile per architecture (cheapest, because the binaries are
   per-arch anyway), or runtime detection (more code). Display, touch, keyboard, backlight and battery are detected
   at runtime either way.
5. **What counts as a keyboard:** ignore the barcode scanner and the always-present i8042 keyboard by default (ignore
   list in `config.json`)?
6. **Orientation and size:** landscape only at first? Should native screens scale by physical size (ppi) or by pixel
   count?
7. **Packaging:** one apps repo with `registry.json` (arm64) and `registry-amd64.json`, and the `arch` field in
   `app.json`.
8. **Build host for amd64:** install the -dev packages in WSL (one apt command), or use a Debian 13 container.
   Stay on Armbian Ubuntu 26.04 (verified working), or reinstall with Armbian Debian 13 to match the deck. The
   recommendation is to stay.
9. **Boot look on the tablet:** boot lines visible as on the deck, or a quiet boot.

Sources: [pbtech FZ-M1 MK2 x5-Z8550](https://www.pbtech.com/product/NBKPAN1209/app),
[Armbian uefi-x86](https://www.armbian.com/uefi-x86/),
[Armbian ia32 UEFI request](https://forum.armbian.com/topic/23996-please-add-ia32-uefi-support/),
[Ubuntu 26.04 versions](https://debugpoint.com/ubuntu-26-04-lts),
[FZ-M1 multitouch bug 1739792](https://ubuntu-bugs.narkive.com/D9t3LC2Z/bug-1739792-new-touchscreen-input-on-panasonic-fz-m1-only-detects-two-fingers),
[wvkbd](https://github.com/jjsullivan5196/wvkbd), [cage](https://github.com/cage-kiosk/cage). Tablet probe script
and raw output were kept outside the repos (`%TEMP%\x86scratch\fzm1_probe.py` and `fzm1_probe.txt`, credentials read
from the environment, never stored).
