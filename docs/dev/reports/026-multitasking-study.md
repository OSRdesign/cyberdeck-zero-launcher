# 026 - Multitasking: keep apps alive, show what runs, switch with a gesture or a key

Status: research and plan written 2026-10-14 on branch `study-multitasking` (= main d4f648d). Nothing was built,
committed or deployed. Apps repo read at main 52fd1f2, plus the System Monitor branch `app-system-monitor` (worktree
`apps-sm`). Read-only probes on 2026-10-14 of the deck (zero7.local) and the Pi 3A+ with its 4.3" DSI panel
(192.168.68.170): `/proc`, `/sys`, `EVIOCGABS` ioctls, `ps`, `df`; nothing was stopped or written. The HackBerry
(.214) and the Pi 5 (.175) did not answer.

**Update 2026-10-20:** P-a measured (section 10); P-b, the launcher core, is built behind `APPLAUNCH_MULTITASK=1`
on branch `multitasking-pb` (section 11, `docs/dev/tasks/015-multitasking-pb.md`), board tests pending.

Markers: **V** = verified in code or measured on a board, **I** = inferred or taken from documentation or the web;
needs a board. Owner decisions are numbered **D1-D15** (section 8). UI items are proposals for the owner to decide.

## 0. Summary

**Today: one app at a time.** A fullscreen app blocks the launcher's UI thread until it exits (`launch.cpp:188-228`).
A wrapped (compat) app runs on a worker thread, but only one at a time (`native_ui.cpp:410,538`). Leaving an app ends
it. A "System Monitor watching viz1090" is therefore impossible today.

**Facts that shape the design**
- **Nobody grabs input.** The launcher never holds the touch screen exclusively (no `EVIOCGRAB` anywhere, V). Every
  fullscreen app opens the touch node and the keyboards itself. The launcher only stops reading because its timers
  are off. So an app left running in the background would also see everything typed or touched in the foreground app.
- **Nobody arbitrates the framebuffer.** fbdev has no ownership. Whoever writes last wins. The deck has no DRM at all
  (legacy `BCM2708 FB`); the Pi 3A+ uses KMS fbdev emulation (V).
- **Every board has multitouch.** The deck's Goodix has 5 slots; the Pi 3A+ DSI FT5x06 has 10 (V). Only the old
  resistive XPT2046 panel is single-touch (I).
- **The launcher flattens multitouch.** It ignores the slot number, so two fingers make its pointer jump between them
  (`cp0_lvgl_dpi_scaled.c:307-319`, V code). Every `cp0_lvgl` app has the same reader.
- **RAM is not the limit for 3-4 apps.** The deck has 414 MB. A native app is 13-15 MB of RSS, mostly file-backed.
  Both boards have zram swap. The memory cgroup is disabled and there is no PSI, but the cgroup v2 freezer is present
  and writable by the launcher's user (V).
- **The deck screen is 2.8"**, about 57 x 43 mm, not 4". Gesture sizes must be checked against that.

**Recommendation: a hybrid lifecycle, opt-in per app (option A+B).** An app declares `X-Background=run|freeze` in its
`.desktop` file and receives a small control socket.
- **Hiding:** the launcher sends `hide`. The app stops drawing and ignores input, then answers `ack`.
- **`run` apps** keep working hidden: viz1090 keeps decoding, Serial Console keeps logging, System Monitor keeps
  sampling.
- **`freeze` apps** are then stopped with `SIGSTOP`.
- **Showing:** the launcher puts the app's last frame back and sends `show`. The app drains stale input and repaints.
- **Legacy apps** (no key: wrapped apps, the Store, old builds) behave exactly as today.
- **The launcher** keeps one instance per app, a recently-used order, a limit of 4 resident apps and a low-memory rule.
  It draws the switcher itself while the apps are hidden.
- **Not chosen:** a compositor (impossible without KMS on the deck) and VT/DRM switching. Fast relaunch with saved
  state stays an app guideline.

**Gestures and keys (the owner's main question)**
- **Touch: 3-finger hold**, 700 ms, fingers still. This is the owner's idea, shortened from 1 s, with palm and grip
  rules. It conflicts with nothing our apps use.
- **Touch, visible and single-touch: long-press on the status strip** (500 ms). Every native app shows the strip.
- **Keyboard: Alt+Tab** (switcher; release Alt to switch), plus **Super** = Home if the keyboard has one.
- **Esc:** hold Esc 3 s still force-closes. Esc at an app's root minimises instead of quitting (D5).
- **Rejected as primary: a 2-finger swipe.** It collides with viz1090's pinch and pan and with two-thumb typing on the
  on-screen keyboard. "Locking" 2-finger gestures for the launcher would take a whole gesture class away from apps.

**Plan:** about 22 working days, in phases P-a to P-e. Keeping terminal sessions alive (P-f) is optional, +3 days.
- **P-a, spike and measure (3 days), first:** the framebuffer handoff with `SIGSTOP`, snapshot and restore; the
  multitouch recordings on the deck and the 3A+; the RSS of every app frozen and running.
- **Top risks:** input isolation; framebuffer handoff; 3-finger reliability on a 2.8" panel; memory with viz1090;
  restructuring the synchronous fullscreen path (section 7.4).

## 1. Today: how the launcher starts, owns and ends an app

### 1.1 The four launch paths (V)

| Path | Start | Who draws on the panel | Input | End |
|---|---|---|---|---|
| **Compat / wrapped (VFB)**: stock apps, Store, LanScan, Wi-Fi Survey, MeshZero | `launch_Exec` -> `native_ui::run_external` (`launch.cpp:176-186`, `native_ui.cpp:536-595`): worker thread, `LD_PRELOAD` shim, **one at a time** (`s_external` `:410`, refused while set `:538`; one VFB path `:436`) | **the launcher**: app draws 320x170 into the VFB file; `external_poll` blits it 2x at 30 Hz (`:498-530`) under the launcher's toolbar | touch -> arrow/Enter keys or nothing (`:553-561`); keys + toolbar -> one uinput device `applaunch-vkbd` (`:562,574-581`) | exit, or Esc held 3 s (runner) -> `finish_external` (`:473-495`) |
| **Fullscreen** (`X-Fullscreen=true`): Mesh Hop, viz1090, Serial Console 0.3.x, template, System Monitor | `launch.cpp:188-228`: `LVGL_RUN_FLAGE=0`, `lv_timer_enable(false)`, then the **UI thread blocks** in `ExecBlocking` -> `cp0_external_app_runner::run` (`cp0_lvgl_process.cpp:40-50`) | **the app**: opens `/dev/fb0` itself (cp0_lvgl backend; viz1090 `viz_fb_shim.c:41-60`; Mesh Hop `platform.cpp:110`); launcher draws nothing | app opens touch and keyboards itself; launcher keyboard thread paused (`runner :60`) | exit, or Esc held 3 s -> `killpg SIGTERM`, +3 s `SIGKILL` (`runner :114-145`); then `cp0_ui_state_republish`, touch swallow, home (`launch.cpp:217-228`) |
| **Terminal** (`Terminal=true`): CLI, Python, SSH | `launch_Exec_in_terminal` (`launch.cpp:153-170`): in-process `UISTPage` + PTY child | the launcher | launcher; keys to the PTY with Ctrl/Alt (`ui_app_st_renderer.cpp:281-367`) | child exit + a key; Esc held 3 s -> force home (`esc_hold_lifecycle_model.hpp:25`); page deleted = PTY closed (`ui_app_st_renderer.cpp:283-288`) |
| **Built-in pages** (Settings, Calculator, games) | in-process | the launcher | the launcher | Esc; force home at 3 s; UI watchdog `_Exit(75)` at 3.5 s if the UI thread is stuck (`esc_ui_watchdog.h`, `.cpp:86-87`) |

The runner makes the launcher a subreaper and puts the app in its own process group (`runner :61,97-107`). Helpers
started by the app stay in that group, for example viz1090's `readsb` (`run_viz1090.sh:28-32`). Commands started by
`_common/async_run` get their own groups (`async_run.hpp:16`).

### 1.2 Ownership today

```
systemd --user
 +- cyberdeckzero.service   cgroup .../app.slice/cyberdeckzero.service   oom_score_adj 200 (inherited by apps)
     +- cyberdeckzero  [UI thread | keyboard thread | presence watcher | Esc UI watchdog]
         |   writes /dev/fb0 (cp0_fb_output)        reads touch evdev + keyboards (no grab)
         |
         +- compat app (pgid)     draws into /dev/shm VFB --(launcher blits 30 Hz)--> /dev/fb0
         |                        reads applaunch-vkbd <- launcher forwards keys / toolbar / touch-as-keys
         |
         +- fullscreen app (pgid) UI thread BLOCKED in runner loop (10 ms): Esc hold -> SIGTERM / SIGKILL pgid
         |                        app writes /dev/fb0, reads touch + every keyboard itself (launcher timers off)
         |
         +- terminal page          in-process UISTPage + PTY child (shell, ssh, python)
```

### 1.3 Input routing (V unless marked)

- **No grab.** The runner says so (`:63-66`); the touch node is opened `O_RDONLY|O_NONBLOCK` with no grab
  (`cp0_lvgl_dpi_scaled.c:538`); the keyboard grab is commented out (`cp0_lvgl_keyboard.c:203`). Every open file gets
  every event. The brief assumed the launcher holds the touch exclusively: **it does not**. During a fullscreen app the
  launcher's touch fd is simply not read (its kernel buffer overflows, I); on return `cp0_display_set_touch_swallow(2)`
  stops the closing tap from clicking the grid (`launch.cpp:222`).
- **Multitouch is flattened.** `touch_update` ignores `ABS_MT_SLOT`, takes X/Y from whatever slot reported last, and
  any slot's `TRACKING_ID=-1` means "released" (`:313-315`). With two fingers the pointer jumps between them and
  lifting the second releases the first (I, from the code). The launcher and every `cp0_lvgl` app (template, System
  Monitor, Serial Console, Store) share this reader. Mesh Hop reads slot 0 only, which is correct
  (`platform.cpp:296-297`). viz1090's shim tracks 5 slots and turns the first two active ones into an SDL pinch
  (`viz_fb_shim.c:224-233,359-381`).
- **Keyboards.** The launcher's own thread is paused during a fullscreen app. The Esc-hold watcher reads every
  keyboard node (explicit device plus presence list; a Bluetooth keyboard is re-opened when it wakes, retried every
  500 ms) but looks at `KEY_ESC` only (`cp0_esc_key_watch.cpp:52-74`, `runner :122-130`). Native apps read every
  keyboard themselves (`_common/platform.hpp:12-13`).
- **Launcher global keys** (only while its keyboard thread runs, so not inside a fullscreen app): brightness and
  volume; Shift+F10 (LCD reset); SysRq, Print or Ctrl+Alt+S (screenshot); Compose or Menu (lock hint)
  (`global_hint_policy.cpp:59-95`); Tab held 3 s = lock (APPLaunch `AGENTS.md`). **Alt+Tab, Alt+Esc and Super are
  unused** (V grep).
- **Terminal pages** forward Ctrl and Alt combinations to the PTY; they keep F4-F8 (pan in big mode), F6, Fn+H and
  Shift scrollback (`ui_app_st_renderer.cpp:319-367`).

### 1.4 Esc rules today

| Where | Short Esc | Held |
|---|---|---|
| Fullscreen app | the app's own: back; at root **quit** (`_common/app.hpp:11-13`, Controller decision 024); Mesh Hop its own | 3 s: `SIGTERM` to the group, +3 s `SIGKILL` (`cp0_esc_exit_policy.hpp:13-15`). The 500 ms hint is not visible: the launcher is not drawing |
| Compat app | to the app through the vkbd | same, plus the ribbon "Hold ESC 3s to return home" drawn by the launcher (`native_ui.cpp:505-516`) |
| Launcher pages, terminal | back one level | 500 ms hint, 3 s force home; 3.5 s watchdog `_Exit(75)` -> systemd restarts the launcher |

### 1.5 Contract files, status strip, X-Fullscreen

- `screen.state` is written by the launcher only, at display creation and after every app exit (`gen+1`); `input.state`
  on keyboard changes (APP-CONTRACT §2-5). Neither says anything about running apps or multitouch. `X-Fullscreen=true`
  means the app owns the panel until it exits (APP-CONTRACT §6).
- The status strip `cp0::ui::StatusStrip` (clock pill, Wi-Fi bars, Bluetooth glyph) is a **non-clickable** canvas
  (`cp0_ui_status_strip_widget.cpp:105`). The home grid and every `cp0_lvgl` app's header use it. viz1090 and Mesh
  Hop draw copies of `cp0_statusbar.c`.

### 1.6 Found on the way (outside this study's scope)

- **A stale test.** `projects/APPLaunch/tests/test_external_framebuffer_ownership.py` fails on main with `ValueError`
  (V, run). It looks for `page->show_home_screen()` inside `launch_Exec`, which now calls `show_home()`
  (`launch.cpp:223`). `run_tests.sh:9` runs it, so the `app-unit` step of `check-all.sh` should fail (I, check-all
  not run). Multitasking must rewrite this test anyway: it encodes "the fullscreen run is synchronous, no thread, no
  timer".
- **Memory on the deck.** Its `/tmp` (tmpfs) holds 145 MB, probably old upload bundles. 161 MB of zram swap is in use
  and MemAvailable is 144 MB, against 249 MB on the Pi 3A+ (V).
- **Idle CPU.** The idle launcher used 6.4% of one core over 10 s on the home grid (V); its lifetime average is 11%.

## 2. Hardware facts

### 2.1 Touch controllers (probed 2026-10-14)

| Board | Panel | Controller (V unless I) | Slots (`ABS_MT_SLOT` max + 1) | Contact size | Display stack (V) |
|---|---|---|---|---|---|
| Deck, Pi Zero 2 W | Waveshare 2.8" DPI, 640x480, about 57 x 43 mm (I) | `11-005d Goodix Capacitive TouchScreen`, event0 | **5** (X 0..639, Y 0..479) | `ABS_MT_TOUCH_MAJOR` 0..255 | legacy fbdev `BCM2708 FB`, **no** `/dev/dri` |
| Pi 3A+ (DSI since 2026-10-13) | Waveshare 4.3" DSI 800x480, 94 x 56 mm | `10-0038 generic ft5x06 (79)`, event0 | **10** (X 0..799, Y 0..479) | none | KMS: `vc4drmfb` fbdev emulation, `/dev/dri/card0` |
| Pi 3A+ SPI (other panel) | Luckfox 3.5" 480x320 | Goodix capacitive (report 021) | MT; count not probed (I: 5) | I | fbdev on fb1, RGB565 |
| HackBerry CM5 | 720x720 | `EP0110M09`, edt_ft5x06 (021) | MT (I: 5 or 10); offline today | I | fbdev under a stopped lightdm |
| Pi 5 + HyperPixel 4 | 800x480 rotated | Goodix GT911 (021) | I: 5; offline today | I | fbdev |
| Walnut Pi 3.5" SPI | 480x320 | XPT2046 **resistive** (021) | **single touch** | none | - |

So every board in use reports at least 5 points. Only the resistive panel needs a single-touch fallback.

Three fingertips need about 35-45 mm (I). That fits on the deck's 57 mm but covers most of it. The FT5x06 reports no
contact size, so palm rejection there has to use the number of contacts and their spacing.

### 2.2 Memory and CPU budget

| | Deck (V) | Pi 3A+ DSI (V) |
|---|---|---|
| MemTotal / MemAvailable with the launcher at home | 414 / 144 MB (with the 145 MB of `/tmp` files) | 415 / 249 MB |
| Swap | zram 413 MB, 161 MB used | zram 414 MB, unused |
| CMA (in MemTotal, usable by movable pages) | 64 MB | 256 MB |
| Launcher VmRSS / RssAnon / RssFile | 18.3 / 4.9 / 13.5 MB, 13 threads | 21.3 / 5.0 / 16.2 MB |
| `oom_score_adj` (apps inherit it) | 200 | 200 |
| CPU, page size | 4 x Cortex-A53, 4 KiB | same |
| Framebuffer | 640x480x32, `virtual_size` 640x480 (single page): 1.2 MB per snapshot | 800x480x16: 0.75 MB |

**A native app** is 13-15 MB RSS (Wi-Fi Survey 13 MB, report 003; guideline under 25 MB, report 024); binaries on the
deck are 2.5-4.2 MB (V `ls`). As with the launcher (27% anonymous), most RSS is file-backed code and fonts the kernel
can drop and re-read; the anonymous part is about 5-8 MB (I: LVGL draw buffers of 1.2 MB at 640x480x32, heap, font
cache), which zram compresses under pressure. viz1090 plus readsb was never measured (I: 30-60 MB; P-a measures it).

**How many can stay resident:** with about 250 MB available (the deck after a `/tmp` cleanup, as on the 3A+) and a
64 MB floor, about 180 MB is left: ten native apps by RSS, or three plus viz1090. Memory does not set the limit for
3-4 apps; the cap of 4 is a usability choice (D6). A frozen app costs 0% CPU; a hidden `run` app only its own work.

### 2.3 Kernel and system building blocks (V)

- **cgroup v2, unified**, root controllers `cpuset cpu io pids`. The **memory controller is disabled**
  (`cgroup_disable=memory` on both kernel command lines): no per-app memory accounting or limit.
- **Freezer present:** `cgroup.freeze` exists and is owned by `osrde` in `.../app.slice/cyberdeckzero.service/`
  (`subtree_control` empty). systemd 257 has `systemd-run --user --scope` and `systemctl freeze`.
- **No PSI** (`/proc/pressure` is missing); **zram swap** on both boards.

## 3. Industry practice

### 3.1 Multitasking launchers

| System | How an app is minimised | How the user knows what runs | Switching: touch / keys | What a card shows | Background and memory policy |
|---|---|---|---|---|---|
| **Android 12+** | Home gesture or button. Since 12, Back on a root launcher activity also only **moves the task to the background** (Android Developers, "Tasks and the back stack") | Recents / Overview only; icons show nothing | swipe up from the bottom and hold = Overview; quick switch along the bottom edge; Alt+Tab with a keyboard (I) | last-frame snapshot; swipe the card away to close; "Clear all" | cached processes frozen with the **cgroup v2 freezer** (Android 11+, 10 s after becoming cached in 14) and killed by lmkd by `oom_score_adj` tier (AOSP "Cached apps freezer"; AOSP commit "OomAdjuster: use oom_adj_score tiers for cached apps") |
| **iOS / iPadOS** | Home gesture | app switcher only | swipe up and pause; iPad: 4/5-finger swipe up, pinch, sideways (Macworld, "6 only-for-iPad gestures") | snapshot taken when the app leaves | apps are suspended soon after leaving unless they declare a background mode (audio, location, VoIP; Apple `UIBackgroundModes`, I) |
| **webOS (Palm Pre)** | swipe up from the "gesture area" under the screen -> card view | cards | flick cards sideways; **throw a card up to close** | the app's card | about 10 cards at most (GSMArena Palm Pre review) |
| **MeeGo Harmattan (Nokia N9)** | swipe in from any edge | "Open applications": 2x2 or 3x3 thumbnails, most recent on top | tap; long-press a thumbnail for close / close all (Phonearena N9 review, WAPReview tips) | thumbnail | - |
| **Maemo 5 (N900)** | task switcher between home and menu | thumbnails | tap; a corner X closes (GSMArena N900 preview) | thumbnail | - |
| **Sailfish OS** | edge swipe to Home | open apps are **covers** on Home; active covers update and offer cover actions | edge swipe; hold Home to show close buttons (Sailfish UX framework; Qt wiki) | an app-drawn mini view | - |
| **Symbian S60** | Menu key | **a small blue circle by the icon of a running app** in the menu | **long-press the Menu key** = task list; C closes an app without switching to it (All About Symbian on the 5800; GSMArena C6 review) | list | - |
| **Phosh / Plasma Mobile** | swipe up from the bottom | overview / task switcher | swipe a preview up to close (Purism "Swipeable upgrade to the Librem 5 interface"); Plasma 6.2: fast swipe up = home, hold = switcher, sideways scrub (Plasma Mobile dev log 2025-06) | previews | apps keep running (Pine64 forum) |
| **Desktop: GNOME, Windows, macOS** | minimise / overview | taskbar, dock dots | touchpad 3-finger up = Overview (GNOME, It's FOSS); 3-finger up = Task View, 4-finger = desktops, also on touchscreens behind a setting (Windows 11, geekrewind); Alt+Tab everywhere | live windows | none |
| **KaiOS** | - | - | no stock switcher found; only a third-party mod (BananaHackers) | - | not verified |
| **Miyoo OnionOS** | Menu = save state + **GameSwitcher**; long Menu = save + main menu (Joey's Retro Handhelds guide; Onion v4 notes) | recent games list | Menu button | screenshot of the save state | nothing stays running: save-state-and-quit |
| **Steam Deck** | Steam button / "..." quick access over the running game (Steam Community, weak sources) | - | buttons | - | one game in Game Mode (I) |
| **RetroPie / EmulationStation, Anbernic stock** | none: one emulator at a time; RetroArch save states | - | hotkeys | - | (I) |
| **Flipper Zero** | none: the loader refuses to start a second app (firmware `app-loader.c`) | - | - | - | - |

### 3.2 Multi-finger and edge gestures, and their conflicts

| Gesture | Who uses it | Conflicts |
|---|---|---|
| 1 finger: tap, drag, long-press | every app | belongs to apps |
| 2 fingers: pinch, pan, rotate | maps (viz1090 zoom), image viewers | any 2-finger system gesture; two-thumb typing on an on-screen keyboard |
| 3 fingers | Windows Task View (touchpad and touchscreen); GNOME Overview (touchpad); some Android skins (screenshot, I) | almost no app gestures |
| 4-5 fingers | iPadOS multitasking; Windows desktops | needs a big screen; not on 57 mm |
| bottom-edge swipe | Android, iOS, Plasma, Phosh; webOS used a strip **below** the screen | app bottom bars, the on-screen keyboard. Android lets apps exclude back-gesture edges (limited length) but never the home gesture ("Ensure compatibility with gesture navigation") |
| side-edge swipe | Android back; N9 and Sailfish home; **our LIST mode: swipe in from the left = Esc** (`cp0_lvgl_dpi_scaled.c:472-474`) | horizontal scrolling, carousels |
| long-press a home key or area | Symbian (Menu key), older Android (recents) | none |

libinput interprets gestures on touchpads only, not touchscreens (libinput-gestures README); so on Linux the shell
recognises touch-screen gestures itself, which is what we would do.

### 3.3 Linux building blocks

| Block | What it gives | Limits for us |
|---|---|---|
| `SIGSTOP` / `SIGCONT` on the process group | freeze anything, no setup; the app can catch `SIGCONT` | helpers in other groups keep running |
| cgroup v2 freezer (`cgroup.freeze`, `cgroup.events`) | freezes a whole subtree, invisible to the app, still killable (`cgroup.kill`) (kernel commit 76f969e8948d "cgroup: cgroup v2 freezer") | one cgroup per app: `systemd-run --user --scope` or `Delegate=yes` on our service |
| fbdev | any process can write the panel | **no arbitration**, no per-client content: the launcher must enforce "one writer" |
| DRM master / VT switching | real handoff, kernel-restored modes | `SET/DROP_MASTER` needs root (Chromium armsoc commit); VT switching needs `VT_PROCESS` apps that repaint; the deck has no DRM |
| Wayland kiosk compositors (cage, labwc, weston kiosk-shell) | composition, input routing, `wl_touch.cancel` | need KMS + EGL; cage shows one maximised app at a time (cage(1)); our apps are fbdev / LVGL / SDL |
| `EVIOCGRAB`, uinput | exclusive input, virtual devices | per open file: breaks every app that reads the real node |
| `oom_score_adj`, zram | kill order, compressed swap | the kernel OOM killer acts late; no PSI, no memcg -> a userspace memory rule |

### 3.4 Lessons for us

1. **Leaving is not closing.** Android, iOS and the N9 all keep apps; the system decides when to kill. Android 12 even
   made Back at the root behave like Home.
2. **What runs is shown in one place,** a switcher, sometimes with a mark on the launcher icon (Symbian's blue
   circle). Cards show a snapshot, not a live view, except Sailfish's app-drawn covers.
3. **Background work is declared** (iOS background modes) or decided by the system (Android services versus cached
   apps). The default is "frozen".
4. **System touch gestures stay out of the 1- and 2-finger space** (3+ fingers on Windows and iPad) or live on an
   edge the app cannot fully claim (Android home gesture). Small devices add a physical key: Symbian's long-press
   Menu, OnionOS's Menu.
5. **The kill path is always one gesture away:** swipe the card up, an X, Close all.

## 4. Design options for our launcher

### 4.1 Evaluation

| | A. Freeze + snapshot | B. Cooperative "hidden" | **A+B hybrid by declaration** | C. Compositor | D. VT / DRM switching | E. Status quo + fast relaunch |
|---|---|---|---|---|---|---|
| What | `SIGSTOP` the group, keep the last frame for the card | new contract message: the app stops drawing, keeps working | `X-Background=freeze|run`; ack, then freeze for `freeze` | cage / labwc / weston + Wayland clients | each app on its own VT, `chvt` | apps save state on exit, restore on start |
| RAM | apps stay resident: anon 5-8 MB each (I), zram helps | same | same | + compositor 20-40 MB + CMA (I) | same as A | none |
| CPU / power | 0 when frozen | the app's background work only (it stops rendering) | both | GL composition; fbdev boards: software | as A | none |
| Effort | 8-10 days | 10-12 days | **about 22 days in all (section 7)** | weeks; KMS on the deck; rewrite of every app's display path | 10+ days, root for DRM | 1 day per app |
| Risk | stale input and repaint on thaw unless the app cooperates; helpers in other groups | an app that ignores `hide` keeps drawing | hide timeout -> forced freeze | very high | high; no DRM on the deck | low |
| Native `cp0_lvgl` apps | yes, with a drain on `SIGCONT` | one implementation in `cp0_lvgl` + `_common` | yes | port to the Wayland backend | must repaint on VT acquire | yes |
| Wrapped VFB apps | possible (launcher owns the panel), but one shared vkbd and one VFB | no (stock binaries) | **not multitasked** (legacy) | no | no | n/a |
| Terminal pages | n/a (in-process) | keep the page and PTY alive | P-f | - | - | - |
| viz1090, System Monitor | **paused**: the monitor sees nothing to monitor | keep running, no drawing | `run` | yes | yes | lose live state |
| Input routing | needs drain or grab | the app ignores input while hidden, drains on show | same | compositor | per VT | - |
| Framebuffer owner | one writer at a time, enforced by `SIGSTOP` | one writer, by agreement | agreement, enforced by freeze on timeout | compositor | kernel per VT (DRM) | one app |

**Recommendation: the A+B hybrid, opt-in through `X-Background`, with E as an app guideline.**
- **A alone fails the owner's own example:** a frozen viz1090 does nothing a monitor could watch.
- **B alone has no enforcement**, and `freeze` is the cheaper default for apps that do not need to work hidden.
- **C and D change the display stack** for no gain on fbdev.

### 4.2 The architecture

```
launcher (sole arbiter of /dev/fb0)                       per resident app
 +- app registry: id (.desktop), pgid, state, MRU order,   +- control socket (SOCK_SEQPACKET, fd APPLAUNCH_CONTROL_FD)
 |   X-Background, snapshot, hidden-since, oom_score_adj   +- process group (SIGSTOP / SIGCONT / SIGTERM / SIGKILL)
 +- supervisor thread (today's runner loop, no longer       +- reads touch + keyboards itself (no grab), but ignores
 |   blocking the UI thread): Esc hold, Alt+Tab, 3-finger      them while hidden and drains them on show
 |   hold, process exits, hide/show handshakes
 +- UI thread: grid, switcher; "display suspended" while an app is in the foreground (no flush, pointer released)
```

**Lifecycle per app:**

```
                 launch / tap tile / pick card
   (not running) ----------------------------------> FOREGROUND <------------------------------------+
                                                         | hide -> ack (<= 300 ms)                   |
                                    X-Background=run      |          X-Background=freeze              | snapshot back to fb0,
                         +--------------------------------+---------------------------+              | SIGCONT, show
                         v                                                            v              |
                  HIDDEN_RUNNING ------------------- show ---------------------- FROZEN (SIGSTOP) ----+
   no ack in 300 ms -> SIGSTOP -> NOT_RESPONDING (card offers Close only)
   any state -- exit / Close / Close all / Esc held 3 s (foreground) / memory rule / kernel OOM --> GONE
               (card removed; screen.state republished as today)
```

**Protocol** (an additive extension of contract 1; ASCII datagrams of 64 bytes or less):

| Direction | Message | Meaning / deadline |
|---|---|---|
| launcher -> app | `hide <n>` | stop writing the framebuffer, cancel any touch in progress (LVGL `lv_indev_reset`), ignore input. Answer `ack <n>` within 300 ms (after the current frame); `freeze` apps are stopped right after |
| launcher -> app | `show <n>` | the launcher has stopped drawing and put back the app's last frame. Read and drop all pending input (until `EAGAIN`), reset pointer state, full repaint, resume input |
| launcher -> app | `close <n>` | save and quit; `SIGTERM` after 3 s, `SIGKILL` 3 s later (today's Esc policy) |
| app -> launcher | `ack <n>` | hide or show done |
| app -> launcher | `home` | the user asked to leave (Esc at root, D5): the launcher hides the app and shows the grid |
| app -> launcher | `switcher` | the status strip was long-pressed in the app (G2) |

**Freeze and thaw:** `killpg(pgid, SIGSTOP)` after the ack, `SIGCONT` before `show`. On close, `SIGTERM` goes with a
`SIGCONT` (a stopped process acts on it only after continuing). When the launcher stops, systemd's stop sequence also
sends `SIGCONT` to the cgroup (I), so stopped apps cannot leak. A per-app cgroup v2 freezer (D10) is a later
hardening that also catches helpers that left the group.

**Snapshot:** taken from `/dev/fb0` right after the hide ack (the app's last frame), un-rotated through
`cp0_fb_output`'s mapping; 1.2 MB on the deck. Used twice: downscaled for the card, and written back before `show`, so
the user never sees the launcher's last frame under a partly repainted app.

**Memory rule (D6):**
- **Cap:** 4 resident apps on boards with 1 GB or less. Opening a 5th closes the least recently used `freeze` app with
  a toast; if all residents are `run` apps, the launcher asks first.
- **Floor:** while apps are resident, MemAvailable is read every 2 s. Under 64 MB the LRU `freeze` app is closed; under
  32 MB also the LRU `run` app, with a toast.
- **OOM order:** `oom_score_adj` 700 for hidden `run` apps, 900 for frozen ones, so the kernel kills them before the
  foreground app and the launcher (200). Lowering it again on `show` without privilege is to be verified in P-a.

**Hard resources:** a frozen app keeps its serial port or RTL-SDR open. Guideline: an app that holds exclusive
hardware declares `run` (it keeps using it) or releases it on `hide`.

### 4.3 Per-app declaration (proposal)

| App | Model today | `X-Background` | Why | Work |
|---|---|---|---|---|
| viz1090 | fullscreen, SDL shim | `run` | readsb keeps tracking; the shim skips the copy to fb0 and throttles `SDL_RenderPresent` to 2 Hz while hidden; it also ignores the pinch with 3 or more fingers | shim, about 120 lines |
| Serial Console | fullscreen, `_common` | `run` | keeps logging the port (a frozen tty overflows its buffer) | `_common` (shared) |
| System Monitor | fullscreen, `_common` | `run` | keeps its history; its sampling is low-rate already (024) | `_common` |
| Mesh Hop | fullscreen, own platform | `run` | keeps receiving messages | `platform.cpp`, about 120 lines |
| App template | fullscreen, `_common` | `freeze` | the example of the cheap default | `.desktop` only |
| LanScan, Wi-Fi Survey, MeshZero, Store, stock apps | compat VFB | none | legacy: behave as today | none |
| Terminal, SSH, Python pages | in-process | P-f | PTY kept alive in a hidden page | launcher |

## 5. Gestures and keys

### 5.1 The candidates

| Candidate | Conflicts (viz1090 pinch, scrolling, OSK) | Accidental triggers on 57 x 43 mm | Single-touch boards | Escape from a hung app | Discoverable | Verdict |
|---|---|---|---|---|---|---|
| **3-finger touch and hold** (owner) | none in our apps. viz1090 sees 2 of the 3 fingers as a pinch: tiny zoom jitter, fixed in the shim. LVGL apps see a jumping press, cancelled by `hide` | low with rules: exactly 3 contacts, landed within 250 ms, still, spaced, not at the edge | no | **yes** (the launcher sees it) | no: needs a hint | **primary touch gesture** |
| **2-finger swipe locked for the launcher** (owner) | **viz1090 pinch and 2-finger pan**; maps in general; two-thumb typing on the OSK makes short 2-finger strokes | medium (typing) | no | yes | no | **rejected** |
| Swipe up from the bottom edge | OSK bottom row, app bottom bars; the deck's case bezel may hide the edge (unknown) | medium | yes | yes | medium | **measure in P-a; optional later** |
| Swipe in from a side edge | our LIST mode already uses left edge = Esc; horizontal lists | medium | yes | yes | low | no |
| Hot corner (hold a corner) | the app's header Esc (top left), viz1090's X (top right) | medium | yes | yes | low | no |
| **Long-press the status strip** | none: the strip is not interactive today | very low | **yes** | no: the app must forward it | **yes**: it is on every native screen | **visible path and single-touch fallback** |
| An always-visible handle or button | costs space on 640x480; would sit over apps (the launcher cannot draw over a fullscreen app) | low | yes | - | yes | no |
| Two-finger tap | the start of a pinch; two thumbs on the OSK | high | no | yes | no | no |
| 3-finger swipe up (Windows-like) | none | low | no | yes | no | possible alternative to the hold (D2) |

### 5.2 The proposed set, exactly

Distances are in millimetres and turned into pixels with the screen density (`ppmm` in `screen.state`). Deck 11.30
px/mm; Pi 3A+ DSI 8.51; Pi 3A+ SPI 6.08; HackBerry 10.0.

| Name | Value | Deck px | 3A+ DSI px |
|---|---|---|---|
| `T_LAND` (all 3 contacts down after the first) | 250 ms | - | - |
| `T_HOLD` (still, after the 3rd contact) | 700 ms (owner said > 1 s; D2) | - | - |
| `D_STILL` (max travel of each contact during the hold) | 4 mm | 45 | 34 |
| `D_SEP` (min distance between any two contacts) | 8 mm | 90 | 68 |
| `E_EDGE` (no contact may land this close to an edge) | 2 mm | 23 | 17 |
| `MAJOR_MAX` (contact size, Goodix only) | measured in P-a (palm vs finger) | - | n/a |
| `T_STRIP` (status-strip long-press) | 500 ms, movement under 3 mm | 34 | 26 |
| `T_ACK` (hide answer) | 300 ms | - | - |

**G1: three-finger hold** (boards whose touch reports at least 3 slots). One recogniser, pure C in `cp0_lvgl`, used by
the launcher's own touch reader when the launcher is in front and by the supervisor (reading the node, no grab) when
an app is in front. A session starts at the first contact and ends when the last one lifts.
1. TRACKING: the first contact lands at `t0`.
2. ARMED when a 3rd contact lands at `t3 <= t0 + T_LAND`, and:
   - every pairwise distance is at least `D_SEP`;
   - no contact is within `E_EDGE` of an edge;
   - on Goodix boards, every contact is at most `MAJOR_MAX`.
3. REJECTED if any of these happens:
   - a 4th contact (palm or grip);
   - a contact lifts;
   - a contact moves more than `D_STILL` from where it landed;
   - `t3 > t0 + T_LAND`.
4. FIRED at `t3 + T_HOLD`, while the fingers are still down: the switcher opens. That is the feedback; no progress
   ring, because the launcher cannot draw over a fullscreen app. The launcher swallows the rest of the session.
5. In an app: `hide` arrives while the fingers are still down. The app cancels its press (no click) and never sees the
   release, because it ignores input until `show`.
6. In a legacy app (no lifecycle): G1 freezes it and offers "Close <app> / Keep" (section 6.2). While that sheet is up,
   the launcher grabs the touch device so the taps do not pile up in the frozen app's buffer. This gives touch-only
   users a way to kill a hung fullscreen app, which they lack today.

**G2: status strip long-press.** A single contact inside the strip rectangle (`status_strip_left()` to the right edge,
header height) for `T_STRIP`. On the home grid the launcher handles it. In an app, `StatusStrip` becomes clickable in
`cp0_lvgl` and sends `switcher`; the viz1090 shim and Mesh Hop check their own strip rectangle. Works on resistive
panels.

**K1: Alt+Tab** (the supervisor reads it from the same keyboard nodes as the Esc watcher; in launcher pages it is
consumed before the page, so a terminal does not get Meta-Tab).
- Alt down, Tab -> the switcher opens with the second most recent card selected.
- Tab again moves on; Shift+Tab moves back.
- **Alt release** switches to the selected card; the header's Home is selectable too (= the grid).
- Esc with Alt still held cancels: back to the app that was in front.
- The Tab-held-3-s lock must ignore Tab while Alt is down.
- In a fullscreen app the app also sees the Tab. `cp0_lvgl` drops Tab-with-Alt when `APPLAUNCH_LIFECYCLE=1`. Older
  apps move focus by one, a harmless side effect.

**K2: Super (Logo) pressed and released alone** = Home: minimise to the grid. Only if the keyboard has the key: not
verified for the M4 or the HackBerry keyboard. `KEY_HOMEPAGE` does the same.

**K3: Esc.**
- Short Esc = the app's back, unchanged.
- At an app's root, a lifecycle app sends `home` (minimise) instead of quitting (D5).
- **Hold Esc 3 s = force close the foreground app**, unchanged: hint at 500 ms, `SIGTERM`, `SIGKILL` 3 s later.
- In the switcher, Esc closes the switcher and goes back to where you were.

**Closing from touch:**
- the X on each card (9 mm target);
- flick a card up at least 12 mm to close it (webOS, Phosh, Android);
- **Close all** in the switcher header (closes the hidden apps; graceful `close`, then the Esc policy);
- a "Not responding" card offers Close (force).

**Discoverability (UI, D15):** the first time a lifecycle app is opened, a one-line toast:
"Hold three fingers, or long-press the clock, to switch apps".

### 5.3 Event sequences for tests (pure recogniser, `t` in ms, deck pixels)

Positive case (`FIRED` expected at t = 790):

```
t     slot id  x    y    event                         recogniser
0     0    10  200  240  contact down                  TRACKING (t0 = 0)
40    1    11  300  250  contact down                  -
90    2    12  400  245  contact down                  ARMED (t3 = 90; separations 101/100/200 px >= 90)
90-790        jitter <= 10 px on each slot             -
790                                                    FIRED -> SWITCHER
1100  0-2  -1            all lifted                    swallowed -> IDLE
```

Negative cases, each `REJECTED` or no event: the 3rd contact at t = 300 (`T_LAND`); slot 1 moves 50 px at t = 400
(`D_STILL`); a 4th contact at t = 500; a contact lands at x = 10 (`E_EDGE`); two contacts 60 px apart (`D_SEP`);
slot 2 lifts at t = 600; a 2-finger pinch over 1 s (viz1090); alternating two-thumb OSK taps of 80 ms for 3 s; a
3-contact palm with `TOUCH_MAJOR` above `MAJOR_MAX`; a ghost contact living 20 ms during a pinch. Every case is
replayed in both coordinate orientations (buffer, and legacy swap/invert).

Alt+Tab: `LEFTALT down, TAB down/up` -> switcher on card 2; `TAB` -> card 3; `LEFTALT up` -> resume card 3; `ESC`
with Alt held -> back to the original app; `TAB` alone -> nothing (the app gets it); `TAB` held 3 s with Alt held ->
**no lock**.

## 6. UI proposal (for the owner to decide)

### 6.1 Home grid: show what runs, keep the order

The tiles keep their order, so muscle memory survives. A resident app gets a mark under its label (Symbian's blue
circle; desktop dock dots): a filled dot means running hidden, a hollow ring means paused (frozen). Tapping that tile
**resumes** the app: one instance per app, as Android does. An optional count in the status strip, left of the clock,
doubles as the visible switcher entry (G2) (D8).

```
640x480 (deck)                                                  tile 192x188
+----------------------------------------------------------------+
|                                     [2]  12:41  |||  BT        |  status strip; [2] = resident apps (optional)
|  +----------------+  +----------------+  +----------------+    |
|  |                |  |                |  |                |    |
|  |     (icon)     |  |     (icon)     |  |     (icon)     |    |
|  |                |  |                |  |                |    |
|  |    viz1090     |  |    Monitor     |  |  Serial Cons.  |    |
|  |       *        |  |       o        |  |                |    |  * = running hidden, o = paused
|  +----------------+  +----------------+  +----------------+    |
|  +----------------+  +----------------+  +----------------+    |
|  |     (icon)     |  |     (icon)     |  |     (icon)     |    |
|  |    Mesh Hop    |  |     Store      |  |    Settings    |    |
|  +----------------+  +----------------+  +----------------+    |
+----------------------------------------------------------------+

480x320 (best effort)                    tile 146x125
+------------------------------------------------+
|                           [2] 12:41 ||| BT     |
| +------------+ +------------+ +------------+   |
| |   (icon)   | |   (icon)   | |   (icon)   |   |
| |  viz1090   | |  Monitor   | |  Serial C. |   |
| |     *      | |     o      | |            |   |
| +------------+ +------------+ +------------+   |
+------------------------------------------------+
```

### 6.2 The switcher

**Recommended: a 2x2 card grid.** With a cap of 4 every resident app is visible at once and nothing scrolls, as the
N9's 2x2 view did. Cards are in recently-used order: the app you came from is first, the selection starts on the
second. Each card has the snapshot, a state mark (running / paused / not responding) and an X. Esc or Home returns
(D7).

```
640x480: header 56 px, 4 cards of about 290x180 (thumbnail about 200x150, 4:3)
+----------------------------------------------------------------+
| [Home]        Running apps 3/4          [Close all]   12:41 ||| |
|  +--------------------------+   +--------------------------+   |
|  |  +--------------------+  |   |  +--------------------+  |   |
|  |  | last frame         |  |   |  | last frame         |  |   |
|  |  | viz1090            |  |   |  | Monitor            |  |   |
|  |  +--------------------+  |   |  +--------------------+  |   |
|  |  viz1090  running    [X] |   |  Monitor  paused     [X] |   |
|  +--------------------------+   +==========================+   |  <- selected (Alt+Tab, keys)
|  +--------------------------+   +--------------------------+   |
|  |  +--------------------+  |   |                          |   |
|  |  | last frame         |  |   |        (empty)           |   |
|  |  +--------------------+  |   |                          |   |
|  |  Serial Console run  [X] |   |                          |   |
|  +--------------------------+   +--------------------------+   |
+----------------------------------------------------------------+
   tap = resume, X or flick up = close, arrows + Enter, Esc = back

480x320: a list, because 4 cards would be 3 mm thumbnails
+------------------------------------------------+
| [Home]   Running 3/4          [Close all]      |
| +--------+                                     |
| | thumb  |  viz1090          running      [X]  |
| +--------+                                     |
| | thumb  |  Monitor          paused       [X]  |
| +--------+                                     |
| | thumb  |  Serial Console   running      [X]  |
| +--------+                                     |
+------------------------------------------------+
```

**Alternative (webOS / Phosh): one big card with its neighbours peeking.** Swipe sideways to browse, flick up to
close. It is nicer for Alt+Tab and needs more animation work.

```
+----------------------------------------------------------------+
| [Home]                                    [Close all]   12:41  |
|    +--+  +--------------------------------------------+  +--+  |
|    |  |  |                                            |  |  |  |
|    |  |  |         last frame of Monitor              |  |  |  |
|    |  |  |                                            |  |  |  |
|    +--+  +--------------------------------------------+  +--+  |
|                 Monitor   paused        [X]                    |
|                     o   *   o                                  |   position dots
+----------------------------------------------------------------+
```

**Legacy app sheet (G1 on an app without lifecycle).** The app is frozen behind it and the launcher holds the touch
grab.

```
+----------------------------------------------------------------+
|  (frozen picture of the legacy app, dimmed)                    |
|        +------------------------------------------+            |
|        |  LAN Scan cannot stay in the background. |            |
|        |     [ Close LAN Scan ]    [ Keep ]       |            |
|        +------------------------------------------+            |
+----------------------------------------------------------------+
```

### 6.3 Rules the UI follows

- **Launching an app that is already running resumes it.** There is never a second instance. App identity is its
  `.desktop` file.
- **Leaving an app:** Esc at root, Home, G1 or Alt+Tab hide it. **Closing is explicit:** an X, a flick, Close all, the
  legacy sheet, or Esc held 3 s.
- **The status strip** keeps its look. The optional `[n]` count is the only addition.
- **Limit and memory:** see section 4.2. When the launcher closes an app on its own, a toast says which.

### 6.4 Contract changes and compatibility

| Change | Kind | Old apps under the new launcher | New apps under an old launcher |
|---|---|---|---|
| `.desktop` `X-Background=none\|freeze\|run` (absent or unknown = `none`) | additive key | unchanged (`none`) | ignored: the app quits at root as before |
| env `APPLAUNCH_LIFECYCLE=1`, `APPLAUNCH_CONTROL_FD=<n>` (only for `X-Background` apps) | additive variables | not set | absent: the app keeps today's behaviour (quit at root) |
| messages `hide/show/close/ack/home/switcher` | new, internal to the socket | - | - |
| `screen.state` `touch_slots=N` (0 = no touch, 1 = single touch) | additive key | ignored | absent = unknown |
| Esc at root of a lifecycle app = `home` (D5) | behaviour, **only** when the app opted in and the launcher sent the fd | unchanged | unchanged |
| Hold Esc 3 s = force close | unchanged | - | - |
| App obligations: no fb writes while hidden; drain input on show; read touch slots properly | new section "13. Lifecycle" in APP-CONTRACT | - | - |

All of this is additive, so the contract stays **version 1**, under the stability rules of APP-CONTRACT §8. No `v=`
bump is needed.

Per app:
- viz1090, Mesh Hop, Serial Console, System Monitor and the template opt in after their code change.
- The wrapped apps, the Store and stock apps never see a difference.
- `_common` carries the lifecycle for all `cp0_lvgl` apps, so its `LAUNCHER_REF` moves once.

## 7. Plan

### 7.1 Phases

| Phase | Content | Entry | Exit | Days |
|---|---|---|---|---|
| **P-a Spike and measure** | (1) a raw multitouch recorder (a tiny C or Python evdev logger, read-only): 1-4 fingers, palm, grip, two-thumb OSK typing, on the deck GT911 and the 3A+ FT5x06; ghost rate, `TOUCH_MAJOR` of finger vs palm, how close 3 fingers can be on 57 mm. (2) Handoff prototype outside the launcher: run the template fullscreen, `SIGSTOP` the group, snapshot fb0, draw a test card, restore the snapshot, `SIGCONT`; look for tearing and missing repaint. (3) PSS/anon/swap of every app running and frozen (template, System Monitor, Serial Console, Mesh Hop, viz1090 + readsb). (4) viz1090: does readsb tolerate a stopped or slow viz1090 (TCP buffer, reconnect)? (5) Can a finger start a swipe at the deck's bottom edge in its case? (6) `oom_score_adj` raise and lower without privilege | D1, D2 agreed in principle | a data table; go/no-go on G1 and its thresholds; the handoff shows no flicker and no stale input | 3 |
| **P-b Launcher core** | non-blocking fullscreen run (a "display suspended" mode: no flush, pointer released; today's `lv_timer_enable(false)` also stops `lv_async_call`); app registry, MRU, one instance; control socket and env; hide/show/freeze/thaw/close/close all; ack timeout -> not responding; memory rule and `oom_score_adj`; snapshot capture and restore; Alt+Tab and Super in the supervisor and in the launcher's key path; rewrite `test_external_framebuffer_ownership.py` | P-a passed | unit tests of the lifecycle state machine and the memory policy; host test with a fake protocol app on a file-backed framebuffer; `check-all` green; goldens unchanged | 6 |
| **P-c Touch** | slot-aware multitouch tracker in `cp0_lvgl_dpi_scaled.c` (fixes the flattening; single pointer = the first slot); G1 recogniser (pure C, SDK-copyable for the shims); G2 on the strip; swallow after fire; `touch_slots` in `screen.state` | P-a data | table-driven tests (section 5.3) + replay of the P-a recordings; no change in the harness goldens | 4 |
| **P-d Switcher and grid UI** | the owner's choices from section 6 | D6-D9, D15 decided | harness scenes at 640x480, 800x480, 720x720, 1280x720 (+ 480x320 best effort); contact sheet reviewed | 4 |
| **P-e Apps** | `_common` lifecycle (hidden flag in the `cp0_lvgl` flush, drain + `lv_indev_reset` on show, Tab-with-Alt, strip `switcher`); viz1090 shim; Mesh Hop platform; `.desktop` keys; APP-CONTRACT §13, APP-GUIDELINES (save state on exit, hardware release, background CPU budget) | P-b, P-c | headless tests per app; board protocol below | 5 |
| P-f (optional) Terminal sessions | keep `UISTPage` + PTY alive while hidden; card from an LVGL snapshot | P-b | an SSH session survives a trip to another app | 3 |

Order: P-a, then P-b and P-c in parallel (different files: launcher `main/ui` versus `cp0_lvgl` touch), then P-d,
then P-e, then optionally P-f. Total core about 22 days.

### 7.2 Prototype first (to retire the biggest risks)

P-a items (2) and (1), in that order:
- **The framebuffer handoff with stale input:** freeze an app, draw over it, give it back, and prove it repaints and
  ignores what was typed meanwhile.
- **Multitouch on the real 2.8" Goodix:** can three fingers be told reliably from a palm or a grip?

Both run as small standalone programs on the deck, without touching the launcher.

### 7.3 Tests

- **Headless:** the recogniser cases of section 5.3 plus the P-a recordings as fixtures. The lifecycle state machine
  is tested as pure transitions:
  - ack, no ack, app exits while hidden, killed while frozen, close of a frozen app, resume of a running app;
  - LRU at the cap, the floor with a fake MemAvailable, launcher restart.
- **Host process tests:** a fake app speaking the protocol, like `app-contract-probe`, on the host, using a
  file-backed framebuffer. They check the hide deadline, freeze, `SIGCONT` before `SIGTERM`, the drain on show, and
  that no fb write happens while hidden.
- **On the board, scripted:** a uinput multitouch device injects G1, selected with `APPLAUNCH_TOUCH_DEV`. Capability
  scoring rejects virtual devices (`cp0_lvgl_dpi_scaled.c:236`), but an explicit path bypasses it (`:287-291`). A
  uinput keyboard injects Alt+Tab, as in reports 006-009.
- **Physical, run by the user from a protocol.** Deck: G1 in viz1090, Monitor, Serial Console and on the grid; no zoom
  jump in viz1090; System Monitor shows viz1090's CPU while viz1090 is hidden; Serial Console logs while hidden;
  Alt+Tab with the M4 keyboard; hold Esc 3 s still kills; Close all; 4-app cap; legacy sheet on LanScan. Pi 3A+ DSI:
  the same with the FT5x06 at 800x480. HackBerry: Alt+Tab on the built-in keyboard, G1 at 720x720. Pi 3A+ SPI (best
  effort): G2 only. Every board: `free -m` and `ps` with 4 apps resident.

### 7.4 Risks

| # | Risk | Effect | Mitigation |
|---|---|---|---|
| 1 | **Input isolation**: apps read evdev directly, no grab | a hidden or frozen app acts on keys and touches meant for another (a stale Enter) | opt-in only; ignore while hidden, drain on show (in `_common`, shim, Mesh Hop); grab while the launcher is in front; legacy apps never kept |
| 2 | **Framebuffer arbitration**: fbdev has none | an app that ignores `hide` corrupts the switcher or the next app | 300 ms ack deadline, then `SIGSTOP` and "not responding"; snapshot restore before `show` |
| 3 | **G1 reliability** on a 2.8" GT911 and a size-less FT5x06: fingers merged, ghosts, grip | missed or false switcher | P-a measurements first; G2 and K1 as equal paths; thresholds in settings |
| 4 | **Memory with viz1090** (unknown size), 414 MB, no PSI, no memcg; deck `/tmp` junk | thrashing in zram, late kernel OOM | cap 4, MemAvailable floor, `oom_score_adj` tiers, measure in P-a |
| 5 | **Restructuring the synchronous fullscreen path** (`launch.cpp:188-228`, the ownership test, `lv_timer_enable(false)` also stops `lv_async_call`, the 3.5 s watchdog `_Exit(75)` kills every resident app) | regressions for today's apps | display-suspend mode; keep the watchdog but arm it only for the launcher's own pages; every current app re-tested |
| 6 | Hardware held by a frozen app (RTL-SDR, serial port) | a second app cannot open it | guideline: `run` or release on `hide` |
| 7 | Discoverability of a hidden gesture | users never find G1 | first-run toast, G2 visible, Alt+Tab |

### 7.5 What not to do

- No compositor, and no switch of the deck to KMS. No `EVIOCGRAB` while an app is in front in v1 (every app reads the
  real nodes); grab only while the launcher itself is in front.
- Do not freeze or keep apps that did not opt in; do not multitask the wrapped VFB apps (one shared vkbd, one VFB
  path, stock binaries). Do not reserve 2-finger gestures.
- Do not use `SIGUSR1`/`SIGUSR2` as the protocol (libraries use them; no answer, no ordering). Do not rely on the
  kernel OOM killer alone.
- No app-specific code in the launcher: the declaration lives in the app's `.desktop` file. Do not build G1 before
  the P-a recordings.

## 8. Decisions for the owner

1. **D1 Model:** hybrid lifecycle (cooperative `hide`/`show` plus freeze), opt-in by `X-Background`, legacy apps
   unchanged. *Recommendation: yes.*
2. **D2 Touch gesture:** three-finger hold, and how long. *Recommendation: yes, at 700 ms (the owner proposed over 1 s;
   to be confirmed with the P-a data; adjustable 500-1500 ms in Settings). The 2-finger swipe is rejected. A
   3-finger swipe up is the fallback idea if holds prove unreliable.*
3. **D3 Visible and single-touch path:** long-press the status strip, 500 ms. *Recommendation: yes; long-press rather
   than tap, so that a stray touch on the header does nothing.*
4. **D4 Keys:** Alt+Tab switcher, release Alt to switch; Super = Home when present. *Recommendation: yes.*
5. **D5 Esc at an app's root:** minimise (Android 12 model) instead of quit, for lifecycle apps only. Hold Esc 3 s
   stays force close. *Recommendation: minimise; this revises the 2026-10-13 rule "short Esc at root quits" for
   apps that opt in.*
6. **D6 Limit and memory:** 4 resident apps on boards with 1 GB or less; floor 64 / 32 MB MemAvailable; the launcher
   closes the LRU frozen app by itself (toast) and asks before closing a `run` app at the cap. *Recommendation: yes.*
7. **D7 Switcher layout:** a 2x2 card grid (all visible; a list at 480x320), or a single card with neighbours.
   *Recommendation: 2x2.*
8. **D8 Grid indicator:** a dot under the label, order unchanged; optional `[n]` in the status strip.
   *Recommendation: dot yes, `[n]` yes.*
9. **D9 Closing from touch:** X on each card, flick up, Close all. *Recommendation: all three.*
10. **D10 Freezer:** `SIGSTOP` on the process group first; a per-app cgroup v2 freezer later if helpers outside the
    group matter. *Recommendation: yes.*
11. **D11 Terminal sessions kept alive (P-f).** *Recommendation: yes, after P-e.*
12. **D12 Wrapped VFB apps:** no multitasking; G1 offers the legacy Close/Keep sheet. *Recommendation: yes.*
13. **D13 Contract:** additive extension of contract 1 (`X-Background`, `APPLAUNCH_LIFECYCLE`, `APPLAUNCH_CONTROL_FD`,
    `touch_slots`), no version bump. *Recommendation: yes.*
14. **D14 Bottom-edge swipe** as a third path: decide after measuring the deck's case edge in P-a.
    *Recommendation: defer.*
15. **D15 First-run hint** toast for G1/G2. *Recommendation: yes, shown once.*

### Owner decisions (2026-10-14)

Taken as recommended except where marked. Status: **parked** after the decisions; nothing is built. Restart with P-a.

| D | Decision |
|---|---|
| D1 | Hybrid opt-in lifecycle (`X-Background=run\|freeze`), legacy apps unchanged |
| D2 | **Changed:** touch gesture = 3-finger hold **1 s** (not 700 ms); still to confirm with P-a data |
| D3 | Long-press on the status strip, 500 ms |
| D4 | Alt+Tab (release Alt to switch) and Super = Home when present |
| D5 | **Changed:** short Esc at an app's root keeps **quitting** (no minimise); hold Esc 3 s force-closes |
| D6 | 4 resident apps, MemAvailable floors 64 / 32 MB, LRU auto-close with a toast |
| D7 | 2x2 card grid (list at 480x320) |
| D8 | **Changed:** a dot under the tile label only; no `[n]` in the status strip |
| D9 | **Changed:** X on each card and a Close all button; no flick-up |
| D10 | `SIGSTOP` on the process group |
| D11 | Terminal sessions kept alive (P-f), after P-e |
| D12 | Wrapped VFB apps: no multitasking; legacy Close/Keep sheet |
| D13 | Additive extension of contract 1, no version bump |
| D14 | **Changed:** bottom-edge swipe dropped |
| D15 | First-run hint toast, shown once |

## 9. Not verified

**Status 2026-10-19: P-a started** (branch `multitasking-pa`). The spike tools are in `tools/multitasking-spike/`:
`mtrec` + `mta_analyse.py` (touch recordings, the G1 rules of section 5.2 replayed on them), `fbhandoff` + `fbhapp`
(the handoff of section 7.2), `memprobe.py` (memory, CPU, readsb, `oom_score_adj`). They are tested on the PC only;
the owner runs them on the boards with `docs/dev/tests/multitasking-pa-protocol.md`. Everything below stays open
until those results are in. Read-only probes the same day: the deck's MemAvailable is 269 MB (its `/tmp` was
emptied), and the HackBerry answered (touch `EP0110M09`: slots, positions, tracking id, no `TOUCH_MAJOR`; 4 GB RAM).

- **Not probed:** multitouch on the HackBerry and the Pi 5 (offline) and the Pi 3A+ SPI panel (not mounted): their
  slot counts come from report 021 and the driver names.
- **Real touch sequences:** whether three fingers fit and separate on the deck's panel, ghosts, palms, `TOUCH_MAJOR`
  values. Nobody touched the screens during this study.
- **Not measured:** viz1090 + readsb memory and CPU, and readsb with a stopped client; lowering `oom_score_adj`
  without privilege; systemd sending `SIGCONT` to stopped processes when it stops our service.
- **Hardware unknowns:** a Super key on the M4 and HackBerry keyboards; whether the deck's bezel allows edge swipes.
- **check-all** failing on the stale ownership test (the test itself fails, V).
- **Weak sources:** the Steam Deck, KaiOS and Tizen rows. iOS background modes are from Apple's documentation as
  generally known, not fetched.

**Files most relevant:** launcher `projects/APPLaunch/main/ui/{launch,native_ui}.cpp`,
`ext_components/cp0_lvgl/src/cp0/{cp0_external_app_runner.cpp,cp0_lvgl_dpi_scaled.c,cp0_esc_key_watch.cpp}`,
`ext_components/cp0_lvgl/include/cp0_esc_exit_policy.hpp`, `projects/APPLaunch/main/ui/model/global_hint_policy.cpp`,
`projects/APPLaunch/tests/test_external_framebuffer_ownership.py`; apps `apps/_common/{app,platform}.hpp`,
`apps/viz1090/build/viz_fb_shim.c`, `apps/mesh-hop/src/main/ui/platform.cpp`.

**Sources (web, 2026-10-14):** Android Developers, "Tasks and the back stack" (Back at root, Android 12) and "Ensure
compatibility with gesture navigation"; AOSP, "Cached apps freezer" and commit f07755e6e372 "OomAdjuster: use
oom_adj_score tiers for cached apps"; Macworld, "6 only-for-iPad gestures"; GSMArena, Palm Pre review, Nokia N900
preview, Nokia C6 review; Phonearena, Nokia N9 review; WAPReview, N9 Swipe UI tips; sailfishos.org UX framework and
"Sailfish application pitfalls"; Qt wiki, SailfishOS emulator basics; All About Symbian, Nokia 5800 interface; Purism,
"Swipeable upgrade to the Librem 5 interface"; Wikipedia, Phosh; Pine64 forum (Phosh); Plasma Mobile dev log
2025-06-18; It's FOSS, three-finger swipe in GNOME; GNOME gitlab issue 3678; geekrewind, Windows 11 touch gestures;
BananaHackers (KaiOS); Joey's Retro Handhelds, Miyoo Mini Plus guide; Onion v4 notes (Reddit mirror); Steam Community
(Steam Deck menus); Flipper Zero firmware `app-loader.c`; Linux commit 76f969e8948d "cgroup: cgroup v2 freezer" and its
LKML review; cage(1); Chromium xf86-video-armsoc Set/DropMaster commit; Weston issue 197; libinput-gestures README.

## 10. P-a results (measured 2026-10-19/20)

The owner ran `docs/dev/tests/multitasking-pa-protocol.md` on the deck (Pi Zero 2 W, 414 MB, `fb0` 640x480 at
32 bpp); the touch recordings also on the Pi 3A+. **V** throughout.

| Question | Result | Consequence for P-b and later |
|---|---|---|
| Handoff timing (`fbhandoff`) | freeze = `SIGSTOP` of the group; snapshot of `fb0` about **15 ms**; restore **1.5-2 ms**, verified byte for byte every time | snapshot on every hide, restore on every show |
| Repaint after the thaw | **without the restore a frozen LVGL app does not repaint** (100 % of the test card left); apps that redraw by themselves (System Monitor, viz1090) paint again 60-200 ms after `SIGCONT` | the launcher must restore a snapshot (or force a full redraw) whenever it hands the panel back: not cosmetic, mandatory |
| Stale input | an unprotected frozen app acts on what was typed or touched while hidden: the template's highlight moved 3 rows and the Enter fired, a stale Esc quit it; the fake app acted on 2 Enters and 2 taps within 6 ms of the thaw | input protection is needed even for apps that never cooperate |
| Protection without the app's help | **drain**: the app's evdev queues emptied from outside (`pidfd_getfd` of its fds; no Yama on the deck) -> the app saw 0 stale events. **grab**: `EVIOCGRAB` on the keyboard and touch nodes while it is hidden -> nothing pending for the app, 0.00 % screen change, and the grabber reads the events itself (Alt+Tab while an app is hidden) | P-b: grab primary (on the launcher's own fds while it is in front), drain as the safety net before every resume |
| The contract | a cooperating app answers `hide` in about **0.2 ms** and drains its own queue on `show`; an app without the contract (the template) timed out at the deadline and was stopped, as designed | 300 ms deadline, then `SIGSTOP` and "not responding" |
| Memory (MemAvailable 251 MB at the start) | PSS: **viz1090 117 MB** (47 MB anonymous), System Monitor 7.6, Serial Console 7.6, Mesh Hop 9.5, template 6.9 MB. Five resident apps: 136 MB PSS, 206 MB still available, no swap growth | the cap of 4 and the 64 / 32 MB floors (D6) are comfortable |
| CPU | viz1090 about **149 %** running; the others 0.5-1.3 %; **0 % frozen** | viz1090 must never run hidden as it is: `freeze` (or `run` only once its shim stops rendering, P-e) |
| `oom_score_adj` | start value **0** (the apps were started from an ssh session); an unprivileged process can raise it (700, 900) and lower it back to its start value, not below (-100 refused) | P-b raises hidden apps and lowers them back on show, from each app's own start value |
| readsb behind a frozen viz1090 | the TCP link (port 30005) dropped on readsb's side after about 15 s frozen; whether viz1090 reconnects after the thaw is **not verified** | app side (risk 6): `run` or release and reconnect around hide / show |
| Touch (P-c) | the deck's axes are not square (raw X 14.9 units/mm, Y 8.5): G1's thresholds must be applied after the transform to screen coordinates. 3-finger hold of 1000 ms (D2): **17 of 20** on the deck, **9 of 10** on the Pi 3A+, **0 false positives** | P-c |

**Where P-a contradicts or sharpens the study:**
- viz1090 + readsb is **117 MB PSS**, not the 30-60 MB guessed in section 2.2, and about 1.5 cores running. The
  conclusion "memory does not limit 3-4 apps" still holds (5 apps: 136 MB).
- Section 4.2 presented the snapshot restore as cosmetic ("never sees the launcher's last frame under a partly
  repainted app"). It is required: a frozen LVGL app does not repaint at all.
- Risk 1's mitigation relied on the apps ignoring input while hidden and draining on show. The launcher can protect
  any app on its own (drain and grab), so P-b does both, on top of the app's duty (APP-CONTRACT section 13).
- The deck's MemAvailable of 144 MB in section 2.2 included 145 MB of old `/tmp` files: 251-269 MB after the cleanup.
- `oom_score_adj`: P-a measured 0 for apps started over ssh. Apps started by the deck's service inherit the
  launcher's **200** (read-only probe 2026-10-20: `/proc/<launcher pid>/oom_score_adj` = 200), as section 2.2 said;
  P-b therefore reads each app's start value instead of assuming one.
- Still not measured: systemd continuing stopped processes when it stops the service. systemd documents it
  (`systemd.kill(5)`: `SIGCONT` follows the kill signal); P-b adds a guardian process for runs outside the service
  (the staged test launcher).

## 11. P-b status (2026-10-20)

Built on branch `multitasking-pb`, **off by default** (`APPLAUNCH_MULTITASK=1` turns it on; without it every code
path is today's). Design as built, files, tests and the decisions to confirm: `docs/dev/tasks/015-multitasking-pb.md`.
App side, draft: `docs/APP-CONTRACT.md` section 13. Board protocol: `docs/dev/tests/multitasking-pb-protocol.md`.

- Done: non-blocking fullscreen run (display suspended mode), app registry with MRU and one instance per app, the
  lifecycle state machine (pure, table-tested), control socket and environment, hide / show / freeze / thaw / close /
  close all, snapshot and restore on every handoff, grab + drain input protection, `oom_score_adj` tiers, the memory
  rule (cap 4, floors 64 / 32 MB), Alt+Tab and Super, the guardian, a dev entry (control FIFO, status file, log
  lines, a one-line text stand-in for the switcher).
- Verified on the PC only (unit tests, host process tests with the fake app on a file-backed framebuffer, the render
  harness); the board tests are pending.
- Next: P-c (touch) is independent; P-d needs the UI decisions (D7-D9, D15); P-e (apps) needs P-b merged.
