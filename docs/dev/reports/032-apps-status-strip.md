# 032 - The status strip in every app: battery everywhere (A), Wi-Fi and Bluetooth on the FZ-M1 (B)

Status: investigation written 2026-10-23 on branch `reports-032-034`. Launcher read at v0.8.4 (95fb15e). Apps repo read
at origin/main d84fdf0, whose `apps/_common/LAUNCHER_REF` is 2b1a070 (v0.8.1). Reading only: no product code changed,
nothing committed. **The FZ-M1 tablet was offline**: ssh port 22 at 192.168.68.183 did not answer on 2026-10-23 (three
tries; one probe run timed out while connecting). So nothing in this report was measured on the tablet.

Markers: **S** = read in the source (file:line), **M** = measured today on the dev PC, **I** = inferred, needs a board.
Cost unit: one worker session.

## 1. Summary

**A. How the battery reaches every app.** A native app gets its strip from `cp0::ui::StatusStrip`. That code is
compiled into each app binary from the pinned launcher tree, so the strip's drawing and its data sources are both
frozen at the app's pin (S). The published apps are pinned at v0.8.1 to v0.8.2, so **no published app shows the
battery** (S). A rebuild at the v0.8.4 pin brings the battery to System Monitor, Net Toolbox, Mesh Hop, RSS Reader,
Serial Console and the LVGL pages of viz1090. No source change is needed: the strip reads the battery itself (S).
Three groups still miss it after that rebuild:
- viz1090's full-screen map (its own copy of the renderer);
- the compat apps (LAN Scan, Wi-Fi Survey, MeshZero);
- the launcher's own compat pages (CLI, Python, the SSH session). Their old 20 px top bar hides its battery on
  every dpi-scaled board (S).

No option reaches an app binary that already exists: every option needs one more rebuild of each fullscreen app.
**Recommendation:**
- a **run-time status feed**: the launcher writes `status.state` next to `screen.state`, and the toolkit reads it,
  falling back to sysfs;
- **one last re-pin** of every app to adopt the feed;
- a **re-pin checklist line** for future changes to the drawing.

The owner's shared-library idea is assessed in 3.3. As a shared LVGL widget it is not recommended. An LVGL-free
renderer `.so` is possible later.

**B. Wi-Fi and Bluetooth in apps on the FZ-M1.**
- **Wi-Fi, root cause (S; the interface name is not measured, I).** The apps' strip has no source of its own and
  reads sysfs (`cp0_statusbar_read_state`). That function hardcodes `wlan0`: `/sys/class/net/wlan0/operstate` and
  the `wlan0:` line of `/proc/net/wireless`. On a PC the kernel names the Wi-Fi interface `wlpXsY`. System Monitor's
  FZ-M1 fixture has `wlp2s0`. So an app shows four grey bars while connected. The launcher's strip uses NetworkManager
  (`nmcli`), which does not depend on the name, so it works.
- **Bluetooth (narrowed, not proven).** The app reads `/sys/class/bluetooth/hci*` and rfkill. This does not depend on
  the board, but it cannot see BlueZ's "Powered" state. An adapter that is switched off but not rfkill-blocked
  therefore shows a grey glyph in apps, while the launcher shows none.
- **Not the cause (S):** D-Bus, the session environment, the amd64 build, XFCE. The default source uses none of them.

The fix: the feed above, plus an interface-agnostic sysfs fallback (launcher repo, about 2 sessions with tests), and
then one re-pin and rebuild of the apps (apps repo, about 1.5 sessions with publishing).

## 2. How an app gets its header today

### 2.1 The pieces

| Piece | File | LVGL? | Data it reads |
| --- | --- | --- | --- |
| Renderer: pill, bars, Bluetooth glyph, battery | `cp0_lvgl/src/cp0/cp0_statusbar.c` (FreeType into an ARGB buffer) | no | none (given a state) |
| Default source (no services) | `cp0_statusbar.c:94-139` `cp0_statusbar_read_state` | no | `wlan0` in sysfs/procfs (`:98`, `:103`); `/sys/class/bluetooth/hci*` (`:113-122`); bluetooth rfkill `soft` (`:123-134`) |
| Battery | `cp0_lvgl/src/cp0_power_supply.c:288-310` | no | `/sys/class/power_supply` (type Battery), else `$APPLAUNCH_GAUGE` MAX17048 over i2c-dev (`:254-286`) |
| Widget | `cp0_ui_status_strip_widget.cpp:80-180` `cp0::ui::StatusStrip` | yes (canvas, 1 s and 2 s timers) | battery itself (`:163-165`), then the `StatusSource` (`:166`); without a source `default_status_source()` (`:80-89`) |
| Launcher's source | `projects/APPLaunch/main/ui/native_ui.cpp:646-664` `fill_status` | - | its Wi-Fi service (`nmcli`, `cp0_lvgl_network.cpp:501-540`) and Bluetooth service (`BtStatus`, `BtConnectedList` over BlueZ D-Bus) |
| Old 20 px top bar (320x170 pages) | `cp0_lvgl/src/ui_app_page.cpp:729-775`, `:861-863` | yes | Wi-Fi via `cp0_wifi_status_read` (that process's Wi-Fi service); Bluetooth via the BT service or a `bluetoothctl` poll; **battery hidden on dpi-scaled boards** |

### 2.2 Kinds of app

| Kind | Examples | Header drawn by | Wi-Fi / Bluetooth from | Battery |
| --- | --- | --- | --- | --- |
| Launcher native pages | home grid, Settings, Calculator, switcher, SSH form | launcher, `StatusStrip` + `fill_status` (S: `native_ui.cpp:226`, `settings_native_host.cpp:156`, `ui_app_ssh_native.cpp:229`) | nmcli, BlueZ | yes, current toolkit |
| Native fullscreen cdk apps | System Monitor, Net Toolbox, Mesh Hop, RSS Reader, Serial Console, viz1090's LVGL pages | the app, `StatusStrip::add(page->header())` with no source (S: e.g. `monitor_app.cpp:85`, `toolbox_app.cpp:134`, `mesh_app.cpp:907`) | sysfs default source, compiled in at the pin | only at a pin of v0.8.3 or later (v0.8.4 or later for the HackBerry gauge) |
| Fullscreen non-LVGL drawing | viz1090's map (`libviz_fb.so` shim) | the shim, with a copied `cp0_statusbar.c` (S: `viz_fb_shim.c:500-509`) | copied sysfs reader (`wlan0`) | no, the copy predates it |
| Compat apps (VFB2, 320x170) | LAN Scan, Wi-Fi Survey, MeshZero draft (arm64 only) | the app's old 20 px bar | their own Wi-Fi service (nmcli); Bluetooth by `bluetoothctl` poll | no: hidden on dpi-scaled boards; its gauge reads the bq27220 service |
| Launcher compat pages | CLI, Python, SSH session terminal (`UISTPage : AppPage`) | the launcher's old 20 px bar, drawn in the compat window | launcher services | no: hidden |

A fullscreen app owns the framebuffer and the launcher writes no pixel while it runs (S: `launch.cpp:208-222`,
`multitask/display_handoff.hpp:18-22`). The compat chrome has no strip (S: `native_ui.cpp:399-445`).

On the tablet, apps start only from the launcher: the desktop unit stops lightdm, and apps get the unit's environment
(`EnvironmentFile=/etc/applaunch/board.conf`, `PAMName=login`; S: `pizero2w/cyberdeckzero-desktop.service`). The
default source reads no environment variable for Wi-Fi or Bluetooth. Only the battery reads
`$APPLAUNCH_POWER_SUPPLY_ROOT` and `$APPLAUNCH_GAUGE`, which the HackBerry profile sets and an app started by the
launcher inherits.

### 2.3 What an app author does

1. Build on `cp0_lvgl` through `apps/_common` (`build_app.sh`, `LAUNCHER_REF`). It links cp0_lvgl and LVGL
   statically. The app's cp0_lvgl starts only the filesystem service (S: `_common/scons/cross_config_defaults.mk:105`),
   so the Wi-Fi and Bluetooth signals have no handler in an app.
2. Call `cp0::ui::StatusStrip::add(page->header())` with no source. Never copy the strip (APP-CONTRACT section 9,
   `docs/cp0_lvgl.en.md` "Status strip and title").
3. A non-LVGL app copies `cp0_statusbar.[ch]` and keeps it in sync (`docs/dev/deck/check_statusbar_drift.py`).
4. Get a newer strip by moving `LAUNCHER_REF` and rebuilding. A new toolkit file is picked up on its own: `src/*.c*`
   is globbed (S: `cp0_lvgl/SConstruct:74`), so the v0.8.4 battery needs no app source change.

### 2.4 Our apps: pin today and battery after a rebuild at v0.8.4

| App | Arch | Pin of the published build (build-info, S) | Battery today | After a rebuild at v0.8.4 |
| --- | --- | --- | --- | --- |
| System Monitor 0.2.6 | arm64, amd64 | 3ee67a2 (v0.8.2) | no | yes |
| Net Toolbox 0.4.2 | arm64, amd64 | arm64 f35c2d6, amd64 2b1a070 tree | no | yes |
| Mesh Hop 0.5.2, RSS Reader 0.1.2, Serial Console 0.4.4 | arm64, amd64 | 2b1a070 tree | no | yes |
| viz1090 0.2.2 | arm64 | f35c2d6 | no | intro and settings pages yes; **map no** (shim copy; the copy also needs `cp0_power_supply.c`) |
| LAN Scan 0.1.3, Wi-Fi Survey 0.1.1, MeshZero (draft) | arm64 | old trees | no | **no** (old bar hides it; needs an owner decision on that bar, D4) |
| CLI, Python, SSH session (launcher) | both | launcher | no | **no** (same old bar, launcher-only change) |

Measured on the dev PC: `check_statusbar_drift.py` exits 2 today (M). viz1090's copies drifted when the battery was
added, and the script still lists Mesh Hop as a copy holder, although Mesh Hop now links cp0_lvgl. Release checklist
step 2 ("exits 0") cannot pass as written.

## 3. A: making it reach every app

### 3.1 The constraint

Every byte of the strip in an app, drawing and data alike, is inside the app binary. No launcher-side change can alter
what an existing binary draws or reads. The only exception is a launcher-drawn overlay, and the launcher may draw
only over compat apps (2.2). So every option below costs **one rebuild of each fullscreen app**. They differ in what
the *next* change costs.

### 3.2 Options

| Option | Fixes data later without rebuild | Fixes drawing later without rebuild | Compat apps | Fullscreen apps | Cost now | Risk |
| --- | --- | --- | --- | --- | --- | --- |
| O1 Re-pin policy only (release checklist: a toolkit change to the strip opens an apps re-pin task) | no | no | rebuild each | rebuild each | 0.1 + about 1-1.5 per toolkit release (6 apps x 2 arches, header shots, publish) | users run stale apps until they update in Settings > Apps |
| **O2 Run-time status feed** (`status.state`, toolkit reads it, sysfs fallback) | **yes** | no | needs the reader in their old bar (rebuild once) | yes after one rebuild | launcher 1.5-2, apps 1-1.5 once | small: a new state file in contract 1 (keys only added) |
| O3a Shared LVGL widget library (the strip as LVGL objects from a launcher `.so`) | yes | yes | no | after relinking every app against a shared LVGL | 4-6 + every app rebuilt | high: one LVGL and cp0_lvgl ABI for all apps; any LVGL or `lv_conf` change breaks every app at once |
| O3b LVGL-free renderer `.so` (`dlopen`, C ABI v1, draws pixels into the app's canvas, fallback to the built-in renderer) | yes | yes | no | after one rebuild | launcher about 2, apps 1-1.5 once | medium: an ABI to keep forever; a crash in the `.so` is a crash of every app |
| O4 Launcher-composited overlay | yes | yes | yes, no rebuild (launcher already composites VFB2) | **no**: the app owns `/dev/fb0` and redraws its header over any overlay; making it work would need a VFB for fullscreen apps (an extra full-frame copy: 1.2 MB per frame on the deck, 4 MB on the tablet) | compat only: about 1 | visual decision (where the strip sits over the scaled 320x170 picture and its old bar) |

### 3.3 The owner's idea: the top bar as a shared library installed by the launcher

1. **The LVGL coupling.** `StatusStrip` is LVGL objects: a canvas, timers, the parent's delete event. LVGL and
   cp0_lvgl are linked statically into each app (S: `_common/README.md` "Build"). The executable does not export
   them, and each app has its own `lv_global`. A `.so` that creates LVGL objects therefore needs one LVGL shared by
   the app and the `.so`, which means a launcher-shipped `liblvgl.so` (and in practice `libcp0_lvgl.so`), with every
   app linked against it.
   - **Build model:** that ends the pinned-toolkit model. The pin becomes a run-time soname dependency, and LVGL has
     no ABI promise: struct layouts follow `lv_conf.h`.
   - **Size:** about 2.5 MB less per app binary (the template app, almost only LVGL and cp0_lvgl, is 2.9 MB; apps are
     3.3 to 6.4 MB, M).
   - **Memory:** saved only while two apps run at once (multitasking).
   - **Packages:** the launcher's arm64 and amd64 bundles would carry the libraries. Old apps keep their static copy
     and are not affected.

   Verdict: **no** (O3a). The risk is "a launcher update breaks every app", in exchange for a strip that updates
   itself.
2. **A stable C ABI.** Feasible only without LVGL (O3b). Possible names: `libcyberdeck_strip.so.1` in
   `/usr/share/APPLaunch/lib` (next to `libapplaunch_vfb.so`), with `strip_v1_measure()` (the leftmost column for
   `status_strip_left()`) and `strip_v1_render(argb, w, rows, stride, geom, now)`. That render call reads the data
   itself and draws with FreeType. Its structs carry a size field, and the toolkit `dlopen`s it by soname with the
   built-in renderer as fallback. APP-CONTRACT today has three layers (contract, SDK source "no ABI promise",
   internal). This would be a fourth, "runtime library", with its own promise: soname major = breaking, additions
   only within `.so.1`. The fonts are already the launcher's (`/usr/share/APPLaunch/share/font`).
3. **Apps that do not opt in, old apps, compat apps.** They keep what they compiled, under every option. Only O4
   reaches compat apps without a rebuild, and nothing reaches an existing fullscreen binary. A compat app's picture
   could get the launcher's strip composited over it (O4), but its own 20 px bar is still there underneath. That is a
   visual call for the owner (D4).
4. **The middle road (O2).** The launcher publishes the values it already has, and each app's toolkit only reads them.
   It fixes stale data (Wi-Fi name, Bluetooth "Powered", battery, gauge, Ethernet later), not stale drawing. The
   drawing has changed once in 0.8.x (the battery, 0.8.3). The known pending items (Wi-Fi name, Bluetooth powered,
   HackBerry gauge) are all data.
5. **Cost and migration order.** O2 first (launcher 1.5-2, then one apps re-pin 1-1.5). O3b only if the strip's
   drawing starts changing more than about once per quarter (launcher about 2, plus one more re-pin). O3a never.

**Recommendation for A: O2 plus O1 as the policy for drawing changes.** O4 for compat apps is decided separately
(D4).

### 3.4 Sketch of the feed (for the implementer; field names are a proposal)

- **File.** `status.state` in the state directory, in the same order as `screen.state`: `$APPLAUNCH_STATE_DIR`, else
  `$XDG_RUNTIME_DIR/applaunch/`, else `/tmp/applaunch-status.state`. The format is `screen.state`'s: `v=1`, `gen`,
  then `pid=<launcher pid>`, `wifi=0|1`, `wifi_pct=0..100`, `eth=0|1`, `bt=off|on|connected`, `battery=-1|0..100`,
  `battery_state=Charging|...`.
- **Writer.** The launcher writes the file atomically (temporary file, then rename) from a thread, not an LVGL timer:
  the legacy fullscreen path turns the timers off (`launch.cpp:222`). It rewrites only when a value changes. Its
  inputs are the Wi-Fi service's 3 s cache, the BT service and `cp0_power_supply_battery`. On tmpfs, so no SD wear.
- **Reader.** A copy-ready `cp0_app_status_read()` in `cp0_app_contract.c` (LVGL-free, so viz1090's shim can use it).
  `default_status_source()` uses it when `v=1` and `pid` is alive (`kill(pid, 0)`), else the sysfs reader. The strip
  takes the battery from the feed when the feed has one.
- **Contract.** APP-CONTRACT gets a section next to section 4. Contract 1 allows adding files and keys (section 8),
  so the version does not change.

## 4. B: Wi-Fi and Bluetooth in apps on the FZ-M1

### 4.1 What each process sees (predicted from the source: the tablet was offline)

| Process | Wi-Fi | Bluetooth | Battery |
| --- | --- | --- | --- |
| `cyberdeckzero` (launcher, desktop unit) | `nmcli -t -f DEVICE,TYPE,STATE,CONNECTION dev status`: the first `wifi` row, whatever its name (S: `cp0_network_policy.cpp:101-133`); signal from `nmcli dev wifi list` | BlueZ `Powered` and the connected list over the system bus (user in the `bluetooth` group, `install.sh`) | `BAT1` (power_supply) |
| System Monitor 0.2.6, Net Toolbox 0.4.2 amd64 (and every cdk app) | `/sys/class/net/wlan0/operstate`: **missing on a `wlpXsY` PC**, so `wifi_up=0` and four grey bars | `/sys/class/bluetooth/hci0` present: glyph; `hci0:<handle>` entries: blue; any bluetooth rfkill `soft=1`: no glyph. Not visible: BlueZ powered off | none (pins before v0.8.3) |
| Same apps rebuilt at v0.8.4 | unchanged (same `wlan0` code) | unchanged | `BAT1` |

Environment and D-Bus do not matter for the apps' Wi-Fi and Bluetooth: the default source opens no socket and reads no
variable (S, `cp0_statusbar.c:94-139`). So the amd64 build, XFCE, the PAM session and `DBUS_*` are ruled out. The same
`wlan0` assumption was flagged in report 021 (section on interface names) and is still there.

### 4.2 Root cause and candidates

- **Wi-Fi: root cause in the source.** `wlan0` is hardcoded in `cp0_statusbar.c:98` and `:103` (also the toolkit
  copy in viz1090). The launcher's strip uses `nmcli`, which does not depend on the name, so the home grid shows bars
  and the apps do not. Unverified premise: that the tablet's interface is not `wlan0`. Ubuntu 26.04 on x86 uses
  predictable names unless `net.ifnames=0`, and System Monitor's FZ-M1 fixture has `wlp2s0` (S; partly hand-made,
  I).
- **Bluetooth: candidates.**
  - C1: the adapter is powered off in BlueZ but not rfkill-blocked. The app shows a grey glyph, the launcher none.
    This happens on every board.
  - C2: `hci0` is missing, or soft- or hard-blocked (a second bluetooth rfkill entry blocks the glyph). The launcher
    shows nothing either.
  - C3: a connected keyboard with no `hci0:<handle>` entry, so the glyph stays grey. Unlikely: the kernel adds one
    per ACL and LE connection (I).
  - C4: the owner saw the grey Wi-Fi bars and the missing battery and counted the Bluetooth glyph with them.
- **The one experiment (10 minutes, read-only).** Open the home grid, note its icons, then open System Monitor and
  note its icons. Over ssh, run: `ls /sys/class/net /sys/class/bluetooth; grep -H . /sys/class/rfkill/*/type
  /sys/class/rfkill/*/soft /sys/class/rfkill/*/hard; bluetoothctl show | grep -E 'Powered|PowerState';
  bluetoothctl devices Connected; nmcli -t -f DEVICE,TYPE,STATE dev`. Reading the result:
  - no `wlan0` confirms the Wi-Fi cause;
  - `hci0` present, `Powered: no`, `soft=0` is C1;
  - no `hci0`, or `soft=1` or `hard=1` on any bluetooth entry, is C2;
  - `Powered: yes`, a device connected and no `hci0:` entry is C3;
  - everything consistent with the icons is C4.

### 4.3 Fix

| Part | Repo | Cost |
| --- | --- | --- |
| Interface-agnostic sysfs fallback: the Wi-Fi interface from `/proc/net/wireless` (it lists wireless interfaces only) or `/sys/class/net/*/wireless`, `operstate` up; a sysfs-root test hook; unit tests with `wlan0` and `wlp2s0` trees; `cp0_lvgl_network.cpp:522`'s `wlan0` fallback too | launcher | 0.5 |
| The feed (3.4): Bluetooth "Powered" and connected exactly as the launcher shows them; Ethernet and the gauge for free | launcher | 1-1.5 |
| Release 0.8.5 notes; drift script fixed (Mesh Hop off the copy list; viz1090 copies refreshed or listed with `cp0_power_supply`) | launcher | 0.3 |
| Re-pin `LAUNCHER_REF`; rebuild the 5 dual-arch apps and viz1090; header shots against the launcher's; publish | apps | 1-1.5 |
| viz1090 shim: refresh the copies, read the feed, add the battery | apps | 0.5 (tier 2) |

## 5. Decisions

| # | Question | Options | Recommendation |
| --- | --- | --- | --- |
| D1 | How the strip's data reaches apps | O1 re-pin only / O2 feed / O3b renderer `.so` | **O2** with the sysfs fix as fallback |
| D2 | Drawing changes later | O1 checklist line / O3b | **O1** now; revisit O3b if the strip changes more than about once per quarter |
| D3 | Shared LVGL library (O3a) | yes / no | **no** (3.3) |
| D4 | Battery in compat apps and the launcher's compat pages (CLI, Python, SSH session) | leave as is / show the old bar's battery from power_supply / overlay the new strip (O4) | owner's visual call; cheapest is the old bar's battery fed from `cp0_power_supply_battery` in the launcher's own pages (about 0.5) |
| D5 | Release checklist step 2 (drift check fails today) | fix the script's lists / drop the step | fix the lists in the same launcher PR |

## 6. Plan

| Phase | Tier | Repo | Content | Cost | Order |
| --- | --- | --- | --- | --- | --- |
| P1 | main path | launcher | sysfs fallback without `wlan0`, feed writer and reader, contract section, tests, drift lists | 2 | first |
| P2 | main path | launcher | release 0.8.5 (both bundles) | 0.3 | after P1 |
| P3 | main path | apps | re-pin to the 0.8.5 commit, rebuild System Monitor, Net Toolbox, Mesh Hop, RSS Reader, Serial Console (arm64 and amd64), viz1090 pages; publish | 1-1.5 | after P2 |
| P4 | main path | owner | tablet (10 min): the 4.2 experiment before installing (to settle the Bluetooth candidate), then the home grid and two apps show the same Wi-Fi, Bluetooth and battery; HackBerry: battery in one app | - | with P3 |
| P5 | secondary | apps | viz1090 shim reads the feed and the battery | 0.5 | any time after P2 |
| P6 | secondary | launcher | D4 outcome for the compat pages | 0.5-1 | after the owner's call |
| P7 | later | launcher + apps | O3b renderer `.so`, only if D2 is revisited | 2 + re-pin | not planned |

Total for the main path: about 3.5-4 sessions plus a 10-minute owner check.

## 7. Risks

- Until P3 is installed, every app on the tablet keeps the grey bars: the fix needs the users to update their apps
  in Settings > Apps.
- The feed's staleness rule (`pid` alive) shows the launcher's last values if the launcher hangs without exiting.
  Acceptable: the strip is informational.
- Two strips that disagree (home grid vs app) remain possible on a board where the launcher runs without its Wi-Fi or
  BT service. Both then use the sysfs fallback.
- O2 adds a public file to contract 1. Its keys must stay forever (section 8).
- If the tablet's interface turns out to be `wlan0` after all, the Wi-Fi root cause is wrong. The 4.2 experiment
  settles it before any code is written.

## 8. Not verified

- Nothing on the tablet. It was offline, so there is no `nmcli`, `rfkill`, `bluetoothctl`, `/proc/<pid>/environ` or
  journal evidence, and no reproduction with the installed System Monitor or Net Toolbox amd64. The 4.1 table is a
  prediction from the source.
- That the launcher's own strip shows Wi-Fi and Bluetooth on the tablet (expected from the source; the release notes
  record only the battery being checked there).
- Whether the kernel's `hci0:<handle>` entries appear for LE keyboards on the tablet's kernel (C3).
- The HackBerry gauge in an app (needs a rebuild at v0.8.4 or later and a board).
- Binary sizes are of the committed `root/` and `root.amd64/` trees, not of the installed packages.
