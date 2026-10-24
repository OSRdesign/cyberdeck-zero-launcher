# 035 - board.state, app-to-launcher requests and a mini app SDK

Status: study written 2026-10-23 in the `launcher-c1` worktree. Launcher read at v0.8.5 (d0cb456). Apps repo read at
786d74b, whose `apps/_common/LAUNCHER_REF` is d0cb456 (v0.8.5). Reading only: no product code changed, nothing
committed, no board touched.

Markers: **S** = read in the source (file:line), **I** = inferred, needs a board or a closer read. Cost unit: one worker
session (ws). Owner decisions are **D1-D11** (section 7).

## 1. Summary

**A. board.state.** Yes, as one more launcher-written state file next to `screen.state`, `input.state` and
`status.state`. Contract 1 allows new files and keys (APP-CONTRACT section 8), so the version stays 1. The launcher
already computes most of the facts for its own Settings (`cp0_caps_probe`, `cp0_power_supply_battery`, the board
profile). Publishing them costs about 1 ws. Only some facts are exact:
- **Exact and cheap:** board id, arch, RAM, battery, backlight kind, local sound card.
- **Exact at probe time:** a sound-server sink. It changes when a Bluetooth speaker connects, so it must be
  re-probed.
- **Guesses:** "Bluetooth" as the sound kind, the physical-button hints, and any key map. The launcher does not know
  which keys a board's buttons send, so a key map would be a guess and is left out.

Screen, touch and keyboard facts stay in `screen.state` and `input.state`.

**B. Requests from apps.** Possible, without changing how apps run. An app is a separate process (its own process
group, the same user). Today it can only talk back through the lifecycle socket, and only an `X-Background` app under
multitasking gets one. The recommendation:
- **The channel:** one Unix stream socket, `$XDG_RUNTIME_DIR/applaunch/launcher.sock`, served by a launcher thread of
  its own. It works in compat, legacy fullscreen and supervised modes, and from a shell or ssh as the same user.
- **The protocol:** one request line, one reply line.
- **Identity:** the launcher tells which app is asking from the kernel's peer credentials (pid, then the process
  group it launched).

It is not a security boundary. Every app already runs as the same user, who can run `pactl`, write the backlight and
power off through polkit (S). The value is that the launcher keeps owning volume, brightness, sounds and notices.

**C. Mini SDK.** Measured against what the six apps would really use, the order is:
1. Volume, brightness and mute keys working while an app is in front. Today they do nothing. The launcher can do
   this alone, with **no app rebuild**.
2. A beep (a system sound): Mesh Hop on a message while hidden; Net Toolbox at the walk test's weak zone and when the
   wake watch ends.
3. A notice on the launcher's toast: Mesh Hop and Serial Console while hidden.
4. board.state, so an app offers a sound option only where there is sound (the deck has none).

Volume set from an app, brightness, power, Wi-Fi, clipboard and units are low value today: no app plays media, and
the guidelines send settings to the launcher.

**First slice:** keys (P0), board.state (P1), the socket with `hello`, `beep`, `notify` and `volume` (P2), then Mesh
Hop adopts it (P4). With the release (P3) that is about 5-6 ws plus three short owner tests. Only the apps that
adopt a feature need a re-pin.

## 2. Findings: how the launcher and apps talk today

### 2.1 Processes and threads

| Mode | How the app runs | Launcher thread state while it runs | Source |
| --- | --- | --- | --- |
| Compat (no `X-Fullscreen`) | `native_ui::run_external`, VFB shim, the launcher keeps drawing the compat window | LVGL loop alive | S: `launch.cpp:191-201` |
| Fullscreen, multitasking off (screens under 640x480, or `APPLAUNCH_MULTITASK=0`) | `ExecBlocking`, its own process group | **LVGL thread blocked** in the call, every LVGL timer off | S: `launch.cpp:222`, `:229-232`; `cp0_external_app_runner.cpp:99-107` |
| Fullscreen, multitasking on | supervisor spawns it in its own process group; a `SOCK_SEQPACKET` socketpair only when `X-Background` | LVGL timers paused (`display_handoff`), supervisor thread polls its fds | S: `launch.cpp:204-206`, `:412-428`; `supervisor_linux.cpp:227-261`; `display_handoff.hpp:18-22` |
| Terminal (`Terminal=true`) | the launcher's PTY page | LVGL alive | S: `launch.cpp:167-184` |

Other facts that shape any request channel:
- **Same user.** Apps run as the deck user; `keep_root` apps run as root (S: `supervisor_linux.cpp:256`).
- **The launcher never draws over a fullscreen app.** So a notice for the user can only show when the launcher is in
  front (APP-CONTRACT section 10). The supervisor already queues its own notices that way: `NoticeQueue` shows
  nothing while an app is in front and keeps up to 4 waiting (S: `notice_queue.hpp:4-8`, `:22-26`).
- **Keys while an app runs.** The launcher's keyboard thread is paused (S: `cp0_esc_key_watch.hpp:4-5`):
  - the legacy runner reads only Esc (S: `cp0_external_app_runner.cpp:129`);
  - the supervisor reads every key of every keyboard passively while an app is in front (S: `multitask_ui.cpp:92-136`,
    `supervisor.cpp:780-798`, `multitask_model.cpp:271`), but acts only on Esc, Alt+Tab and Super;
  - the volume, brightness and mute keys are handled only on the launcher's own pages (S: `ui_global_hint.cpp:112-138`).
  So **in an app these keys do nothing today** (I for compat apps, whose keys pass through the launcher's virtual
  keyboard).
- **No dark time during apps.** The screen never dims or turns off while an app is in front:
  `ui_screensaver_set_foreground(0)` disables the idle timer (S: `launch.cpp:215`, `:441`; `ui_screensaver.cpp:655-660`; `native_screensaver.cpp:211-216`).
  On the HackBerry and the tablet a forgotten app keeps the panel lit.

### 2.2 Launcher to app (data)

| Channel | What | Source |
| --- | --- | --- |
| Per-launch environment | contract variables, `APPLAUNCH_STATE_DIR`; lifecycle fd for `X-Background` apps | S: `app_launch_env.cpp:15-21`, `:63` |
| Inherited environment | the whole board profile: the units load `EnvironmentFile=-/etc/applaunch/board.conf`, and every child inherits it. So an app already sees `APPLAUNCH_BOARD`, `APPLAUNCH_BACKLIGHT`, `APPLAUNCH_GAUGE` and `APPLAUNCH_HINT_*`, but not the detected results | S: `cyberdeckzero.service:26`, `cyberdeckzero-desktop.service:35`, `install.sh:490-600` |
| State files | `screen.state`, `input.state`, `status.state`; directory 0755, files 0644, atomic rename; `status.state` from its own thread every 2 s, with a `pid` liveness rule | S: `cp0_ui_state.c:34`, `:52-67`; `status_feed.cpp:45`, `:126`, `:139-157` |

### 2.3 App to launcher (commands)

| Channel | What it does | Why it is not the general answer | Source |
| --- | --- | --- | --- |
| Lifecycle control socket | `ack`, `home`, `switcher`; datagrams of at most 64 bytes; unknown messages are ignored | only `X-Background` apps, only with multitasking on; no reply to an unknown message, so an app cannot tell an older launcher from a slow one | S: `lifecycle_protocol.hpp:20-24`, `supervisor.cpp:706-757` |
| `multitask.ctl` | a **developer FIFO** in `$XDG_RUNTIME_DIR/applaunch/` (mkfifo 0600, opened read-write and non-blocking), lines `home`, `switch X`, `close X`, `close-all`, `status`, `memory KB`, `g1`, `g2`; the answer goes to the log and `multitask.status` | one-way (no reply), no sender identity, exists only with multitasking on, used by the board test protocols | S: `multitask_ui.cpp:618-622`, `supervisor.cpp:152-157`, `:827-840`, `:976-1008`, `:1306-1346` |
| ZMQ automation RPC | key injection and screenshots on `tcp://127.0.0.1:5557` | compiled out (`CP0_LVGL_USE_ZMQ_RPC` default `n`, not set in any profile); any local user could inject keys | S: `docs/automation-rpc.md`, `cp0_lvgl/Kconfig:15-17` |

### 2.4 What the launcher manages that apps could want

| Thing | Launcher code | Notes |
| --- | --- | --- |
| Volume, mute | `pactl` on `@DEFAULT_SINK@`, else `amixer -c N` on the detected control | S: `cp0_audio_mixer_control.cpp:160-218`; saved as config `volume` after each change, rolled back on failure (`launcher_media_controls.cpp:76-91`) |
| Brightness | sysfs through the settings service, 10 steps, saved level, restored at start, dimmed and restored by dark time | S: `launcher_media_controls.cpp:141-158`, `:205-238` |
| System sounds | miniaudio, PulseAudio then ALSA backends; `SystemSoundPlay` answers "system sound disabled" when the switch is off, which is the default on our boards | S: `cp0_audio_system_sound_player.cpp:363`, `cp0_lvgl_audio.cpp:295-306`; release notes 0.8.4 |
| Capabilities | battery, backlight kind/max/writable, local card and control, server sink, profile hints | S: `cp0_capabilities.h:10-31`, `:70-87`; re-probed every 10 s for the keys (`settings_capabilities.cpp:21-40`) |
| Toast | `launcher_toast()`, supervisor `NoticeQueue` | S: `launcher_toast.h`, `notice_queue.hpp` |
| Power, clock, Wi-Fi | via logind, timedated and NetworkManager. polkit allows the `sudo` group (power, clock) and the `netdev` group (NetworkManager) from any session, so **any app of the deck user can do these directly** | S: `51-launcher-time-power.rules:5-18`, `50-networkmanager-netdev.rules:5-9` |

### 2.5 What the apps use today

No app plays sound, sets volume or brightness, or posts a notification. The search for `pactl`, `amixer`, `aplay`,
`brightness`, `notify-send`, `beep` and `xdg-open` in `apps/*/src` found nothing (S).

| App | `.desktop` | Would use | Source |
| --- | --- | --- | --- |
| Mesh Hop 0.5.3 | `X-Background=run` | beep and notice on a message while hidden. It already counts unread while hidden and has per-chat mute. Sound option only where there is sound | S: `mesh_app.cpp:2341-2343`, `model.cpp:712-727`, README:31 |
| Net Toolbox 0.4.3 | no `X-Background` | beep while in front: walk test enters the weak zone, wake watch "Awake after N s", speed test done. Wi-Fi joining belongs to the launcher's page (guidelines section 12) | S: README:13, :437, :465; `APP-GUIDELINES.md:429` |
| System Monitor 0.2.7 | `freeze` | board.state rows (board, sound, backlight, battery source); read-only by design, so no power actions | S: README:5-7 |
| Serial Console 0.4.5 | `run` | notice when the port goes away while hidden. Port arbitration is already done by the kernel: `TIOCEXCL`, and Mesh Hop also `flock` | S: `serial_io.cpp:111`, `serial_transport.cpp:174-179` |
| RSS Reader 0.1.3 | `freeze` | nothing: text feeds only, no media; a frozen app cannot send while hidden | S: README:8 |
| viz1090 0.2.3 | no `X-Background` | maybe a temporary night dim (not asked for) | I |

## 3. A: board.state

### 3.1 Fields

| Key | Meaning | Source | Cost | Accuracy | Tier |
| --- | --- | --- | --- | --- | --- |
| `v`, `gen`, `pid` | as `status.state` (liveness by `kill(pid, 0)`) | - | - | exact | v1 |
| `launcher` | launcher version, e.g. `0.8.6` | build | none | exact | v1 |
| `board` | `APPLAUNCH_BOARD` (`hackberry-cm5`, `pc-...`); `""` = no profile (the deck) or unknown | board.conf | none | exact label | v1 |
| `arch` | `uname -m` (`aarch64`, `x86_64`) | uname | none | exact | v1 |
| `ram_mb` | MemTotal in MB; the app picks its own class | /proc/meminfo | none | exact | v1 |
| `battery` | `0` none, `1` present, `2` not set up (hint) | `cp0_power_supply_battery`, `APPLAUNCH_HINT_BATTERY` | <1 ms | exact (the strip's own rule) | v1 |
| `battery_source` | `power_supply:<name>` or `gauge:max17048` | same | <1 ms | exact | v1 |
| `backlight` | `none`, `onoff`, `levels` | `cp0_caps_backlight` | <1 ms | exact (sysfs or profile) | v1 |
| `backlight_steps` | the steps the launcher offers: `10`, `2` (On / Off), `0` | `setup_values` | none | exact | v1 |
| `sound` | `0` none, `1` present, `2` not set up | `cp0_caps_probe` | card <1 ms; server up to 500 ms (`pactl`) | card exact; "not HDMI" is a heuristic; sink exact at probe time | v1 |
| `sound_kind` | `alsa`, `server`, `bluetooth` | card, or server sink name (`bluez_output.*`) | as above | `bluetooth` from the sink name is a guess (I) | v1 |
| `*_note` (`battery_note`, `backlight_note`, `sound_note`) | the hint text, for display only (`button`, `bluetooth:XWF-M18-M28-M38`) | `APPLAUNCH_HINT_*` | none | profile text, not a measurement | v1 |
| `requests` | the verbs `launcher.sock` answers **on this board** (`volume` only with sound) | launcher | none | exact | v1 (with P2) |
| `wifi_if`, `eth_if`, `bt` | interface names the launcher uses, adapter present | nmcli, sysfs | small | exact but dynamic (USB adapters) | later (Net Toolbox has its own) |
| `serial`, `i2c`, `spi`, `gpio` | device nodes present and openable (`access`) | /dev | small | presence exact; says nothing about what is attached | later, low value |
| `keys_reserved` | keys the launcher takes (Esc hold, Alt+Tab, Super, and volume/brightness/mute after P0) | launcher policy | none | exact | later |
| hardware button map | which keys a board's buttons send | **no source**: the HackBerry dimmer is not a key (report 030), the tablet's buttons arrive as unknown i8042 keys (`install.sh:586`), `EVIOCGBIT` over-reports | - | guess | **no** (only from a measured board.conf line) |
| screen, touch, slots, keyboards | already in `screen.state` / `input.state` | - | - | - | stay there |

### 3.2 Rules

- **Writing.** The launcher writes the file at start and again after the first sound-server probe, which runs off the
  start path. After that it writes only on change, by atomic rename, `gen + 1`.
- **Dynamic facts.** The sound server is re-probed on the status feed's thread every 30 s, and only where `pactl`
  exists. A Bluetooth speaker connecting flips `sound` within that time. Levels (volume, brightness) are not in the
  file: they go through requests (section 4).
- **Values.** Enumerations as above. A fact the launcher does not know is an **absent key**, never `0`. Hints are for
  display only, beyond the documented prefixes.
- **Stability.** As section 8 of the contract: keys only added, readers ignore unknown keys, `v` stays 1.
- **Reader.** `cp0_app_board_read(NULL, &board)` in `cp0_app_contract.c`, LVGL-free, with the `status.state` liveness
  rule. On -1 (older launcher, launcher gone, ssh without it) the app assumes nothing and hides what depends on it.
- **Naming.** The file also carries one launcher fact (`requests`). If the owner prefers, that goes into a separate
  `launcher.state` (D1).

## 4. B: a request channel from apps to the launcher

### 4.1 Options

| | (1) Extend the control fd | (2) Socket in the state directory | (3) D-Bus session bus | (4) Apps call tools directly |
| --- | --- | --- | --- | --- |
| Reaches | `X-Background` apps under multitasking (640x480 and up) only; not Net Toolbox, viz1090, compat, terminal, legacy path | every app in every mode, terminal apps and scripts through a CLI, ssh as the same user | apps that link a bus library, if a session bus runs | every app |
| Identity | implicit: the fd belongs to one app | `SO_PEERCRED` pid, then its process group, which the supervisor or runner recorded (S: `supervisor_linux.cpp:251`, `cp0_external_app_runner.cpp:99-107`); good, not perfect | the bus peer's pid (same quality) | none |
| Replies, old launcher | the protocol ignores unknown messages, so an old launcher means waiting for a timeout | `ENOENT` / `ECONNREFUSED` at once; `err unknown` for a new verb | name not on the bus at once | n/a |
| Launcher side | the supervisor loop has a 300 ms hide deadline, so slow verbs need a worker; the legacy path has no socket at all | one thread like `status_feed`, independent of LVGL and of the supervisor | GIO is linked (system bus for BlueZ, S: `cp0_bluez_dbus_client.cpp:345`); a session bus on the appliance boards (user service, lingering) is not verified (I) | none |
| App side | already in the helper (`cp0_app_lifecycle_send`) | new copy-ready C file, about 200 lines, libc + POSIX | a library (Mesh Hop already `dlopen`s libsystemd, S: `bluez_backend.cpp:81`); no longer copy-ready | each app redoes the `pactl` to `amixer` chain, the backlight kinds, the step rule |
| State ownership | launcher | launcher | launcher | **split**: the app changes the level behind the launcher's saved config, and its sounds ignore the user's switch |
| Cost | 1-1.5 ws, and (2) is still needed for the rest | about 2 ws | 3+ ws | none in the launcher, 0.5+ per app |
| Verdict | keep for the lifecycle only | **recommended** | no | allowed, discouraged |

A FIFO variant of (2), such as reusing `multitask.ctl`, is rejected: it has no reply and no sender (2.3).

### 4.2 Security and permissions

- **Who can connect.** `$XDG_RUNTIME_DIR` is `/run/user/<uid>`, mode 0700, so only the deck user and root can reach
  the socket (I: systemd's default, not checked on the boards). The launcher also checks the peer uid (own uid or 0).
- **No /tmp fallback.** The state files fall back to `/tmp/applaunch-*`; the socket must not. In a shared `/tmp`,
  another user could create the path first.
- **No privilege added.** Every client is the same user, who can already reach `pactl`, the backlight (`video`), power
  off and NetworkManager directly (2.4). So permissions here are an **agreement, not a wall**. The launcher must
  never become a privilege path: no command execution, no sudo, no package actions, no free settings writes.
- **v1: no gate.** The v1 verbs are harmless: `beep` follows the sound setting, `notify` goes to the queue, and
  `volume` does what the keys do. They are:
  - rate-limited per client: one beep per second, and the notice queue's own limit of 4 waiting;
  - capped in size: notice text at most 160 characters, printable;
  - logged with the app's name (`[req] mesh-hop: beep message`).
- **Later verbs.** Brightness borrow, open an app or page, and anything power-related would be declared in the
  `.desktop` file (`X-Permissions=brightness,open`). That is one parser line (S: `desktop_entry.cpp:115-130`). An
  undeclared verb gets `err denied`.
- **Power.** Only with the launcher's own confirmation, which it can draw only when it is in front. So it is not
  offered.
- **ssh and other clients.** A client that is not a launched app gets the v1 verbs too, logged as `unknown`. It
  could run `pactl` anyway.

### 4.3 Who owns what

| State | Owner | An app may | Persisted |
| --- | --- | --- | --- |
| Volume, mute | launcher (Settings, keys) | get; set or step through a request, saved exactly like a key press | launcher config `volume` |
| Brightness | launcher | get (v1); later a temporary borrow, restored when the app exits or is hidden | launcher config |
| Dark time | launcher; off while an app is in front today | nothing in v1; later an "idle allowed" opt-in (D9) | launcher config `dark_time` |
| System and app sounds | the user's switch in Settings | `beep`, which follows the switch (D4) | launcher |
| Notices | the launcher shows them; the app supplies the text | `notify` | not kept (v1) |

### 4.4 Reliability

- **Never block the UI.** The server thread never touches LVGL and never blocks the supervisor. A notice is handed to
  the LVGL thread with `lv_async_call`. On the legacy path that waits until the app exits, because the LVGL thread is
  blocked (S: `launch.cpp:229`). That is acceptable: the toast could not show earlier anyway.
- **Slow verbs.** `pactl` takes up to 500 ms, so verbs run with timeouts. A client is served one request at a time.
  At most 8 connections; an idle one is closed after 30 s.
- **Calling the services from a thread.** Calling `cp0_signal_audio_api` from a non-LVGL thread is not verified (I).
  The status feed already calls `cp0_signal_bt_api` from its own thread (S: `status_feed.cpp:61`).
- **Restart.** At start the launcher unlinks a stale socket and binds again. A client reconnects once on `EPIPE`, then
  reports "no service".

### 4.5 Degrade rules

| Situation | board.state | launcher.sock | The app |
| --- | --- | --- | --- |
| launcher 0.8.6 or later | `v=1` | answers | uses both |
| launcher 0.8.5 or older | missing | `ENOENT` | hides sound options, skips beeps and notices silently, shows no error |
| launcher crashed or stopped | `pid` dead, so the reader returns -1 | `ECONNREFUSED` | as an older launcher |
| started over ssh, launcher running | readable (I: `XDG_RUNTIME_DIR` from pam_systemd) | answers, logged as `unknown` | works |
| started over ssh, no launcher | missing | missing | as an older launcher |
| multitasking off | same | same (the server does not depend on the supervisor) | a notice shows after the app exits |
| app frozen while hidden | - | it cannot send (stopped) | only `run` apps notify in the background |

## 5. C: the mini SDK

### 5.1 Candidates, ranked for our apps

| # | Item | Who would use it (2.5) | Value | Cost | Tier |
| --- | --- | --- | --- | --- | --- |
| 1 | Volume, brightness, mute keys handled by the launcher while an app is in front: the supervisor already reads every key passively, and the legacy runner's key watch can pass more than Esc. No OSD (the launcher cannot draw over a fullscreen app) | every fullscreen app, published ones included | high | 0.5-1 ws, **no rebuild** | first slice |
| 2 | `beep [name]`: a system sound slot (`notify`, `done`, `warn`) | Mesh Hop, Net Toolbox | high where there is sound (tablet, HackBerry with its speaker connected); nothing on the deck | in P2 | first slice |
| 3 | `notify <text>`: the launcher's toast via `NoticeQueue`, prefixed with the app's name | Mesh Hop, Serial Console (`run`, hidden) | high for Mesh Hop | in P2 | first slice |
| 4 | board.state | System Monitor; Mesh Hop and Net Toolbox for the sound option | medium | 1 ws | first slice |
| 5 | `volume get/set/up/down/mute` | no app plays media today | low, but almost free (the backend exists) | in P2 | first slice |
| 6 | Unread badge on the switcher card or the home tile | Mesh Hop | medium | 1 ws + a visual decision | later |
| 7 | Brightness borrow (restored on exit or hide) | viz1090 at night (not asked for) | low | 0.5 ws | later |
| 8 | Idle dim while an app is in front (supervisor sees keys and touch passively) | battery boards, every app | medium | 1-1.5 ws | later (D9) |
| 9 | Open another app, or a launcher Settings page (Wi-Fi) | Net Toolbox | low-medium | 1+ ws (hide the app, navigate) | later |
| 10 | "Port held by X" helper (scan `/proc/*/fd`) | Serial Console, Mesh Hop | low | 0.3 ws, SDK only | later |
| 11 | Power actions, Wi-Fi connect | Settings has them; guidelines section 12 says not to duplicate | low | - | no |
| 12 | Clipboard, file picker, secrets, permission prompts | nothing in the launcher, no app asks | low | 1+ ws | no |
| 13 | Time zone, locale, units, theme/tokens | system-wide (timedated) / `screen.state` already | - | 0 | done |
| 14 | Haptics, LEDs | no such hardware on our boards | - | - | no |

### 5.2 The normalized shape

- **Data, launcher to app:** state files (`gen`, atomic rename, `pid` liveness). New: `board.state`.
- **Commands, app to launcher:** one socket, request then reply. New: `launcher.sock`.
- **Lifecycle:** the control fd, unchanged.
- **Versions:** the contract stays 1, because the file and the socket are additions. The socket protocol has its own
  level, given in `hello`. New verbs and new reply fields are only added, and an unknown verb gets `err unknown`.
- **Discovery:** `requests=` in board.state, for an app deciding what to show without connecting, and the `hello`
  reply as the live truth.

Protocol sketch (`SOCK_STREAM`, so `socat` or `nc -U` work for tests; UTF-8 lines of at most 256 bytes):

```
> hello 1                    < ok 1 launcher=0.8.6 app=mesh-hop verbs=volume,beep,notify
> beep notify                < ok          | ok off (sounds switched off) | err absent (no sound output)
> notify 2 new messages      < ok queued   | err limit
> volume get                 < ok 40 muted=0
> volume set 55              < ok 55 muted=0
> volume up | down | mute | unmute
> brightness get             < ok 70 kind=levels      (read-only in v1)
errors: err unknown | err bad | err absent | err denied | err busy | err limit
```

SDK files (ext_components/cp0_lvgl, MIT, plain C, no LVGL):

```c
/* cp0_app_request.h */
int  cp0_app_req_open(cp0_app_req_t *r, int timeout_ms);   /* 0; -1 = no launcher service */
int  cp0_app_req_call(cp0_app_req_t *r, const char *line, char *reply, size_t size, int timeout_ms);
                                                          /* 0 "ok ...", 1 "err ...", -1 I/O or timeout */
int  cp0_app_req_has(const cp0_app_req_t *r, const char *verb);
void cp0_app_req_close(cp0_app_req_t *r);
int  cp0_app_beep(const char *name);                      /* one-shot: open, hello, call, close */
int  cp0_app_notify(const char *text);
/* cp0_app_contract.h gains cp0_app_board_read() / _parse() */
```

Around those files:
- **LVGL apps.** They call through a toolkit wrapper that runs on a worker (`cdk::launcher::beep()`,
  fire-and-forget). The guidelines forbid blocking calls on the LVGL thread.
- **Unavailable service.** The wrapper caches "no service" for 10 s.
- **CLI.** A small binary built from the same file (`cyberdeckzero-ctl beep`; name is D10). It serves terminal apps,
  scripts and the owner's board tests.

### 5.3 Checks in both repos

- **Launcher:**
  - unit tests of the parser and the policy (rate limits, identity lookup);
  - a client against the real server thread, as `test_status_feed.cpp` does;
  - the probe (`tools/dev/app-contract-probe`) prints board.state, so `run.sh contract` covers it.
- **Apps repo.** It has no CI (no `.github`); its checks are scripts run by hand at release.
  - `docs/dev/deck/check_statusbar_drift.py` checks only the strip's copies (S: `:23-29`), but viz1090 also copies
    `cp0_app_contract.c` and others into `apps/viz1090/build` (S).
  - Generalize the script to a list of the copy-ready SDK files (one more 0.3 ws, could ride P2).
  - `_common/headless` gets a fake `launcher.sock` so scenes can `expect request beep notify`, as the lifecycle
    scenes already play the launcher.

### 5.4 Other platforms, briefly (background, not checked today)

- **Android:** system services, with permissions declared in the package manifest. The system knows the caller's
  identity; the app does not state it.
- **freedesktop portals:** D-Bus request objects with user consent dialogs. Identity comes from the sandbox. That is
  too heavy for a Zero 2 W.
- **Handheld and kiosk launchers:** usually have no app API at all, and the app owns the device.

Our middle ground keeps the Android lesson (identity from the launcher that started the app, declarations in the
package) without the weight.

## 6. What needs a rebuild

| Item | Already-published apps | Apps that adopt it |
| --- | --- | --- |
| P0 keys in apps | **work without a rebuild** (launcher only) | - |
| board.state | nothing reads it; scripts and the CLI can | re-pin + code |
| launcher.sock | nothing connects; terminal apps and scripts can use the CLI | re-pin + code (native apps get the client through `cp0_lvgl`, since `src/*.c` is globbed; viz1090's shim copies it) |
| Strip | unchanged, so **no forced re-pin** of the other apps | - |

The re-pin cost from report 032 is 1-1.5 ws for all six apps on both arches; one app with its feature is about 1 ws.

## 7. Decisions

| # | Question | Options | Recommendation |
| --- | --- | --- | --- |
| D1 | board.state | no / v1 fields of 3.1 / everything incl. device nodes and key map | **v1 fields**; device nodes later; no key map. `requests=` in it, or a separate `launcher.state` |
| D2 | Request transport | control fd / socket / D-Bus / direct tools | **socket** `$XDG_RUNTIME_DIR/applaunch/launcher.sock`, no /tmp fallback |
| D3 | Permissions | none / `X-Permissions` now / prompts | **none for the v1 verbs** (logged, rate-limited); `X-Permissions` for later verbs |
| D4 | App beeps and the system-sounds switch (off by default) | follow the switch (silent by default) / a separate "App sounds" switch, on by default, shown only with sound / always play | **owner's call (a Settings row is a UI decision)**. Following the switch costs nothing but makes Mesh Hop silent until the user also accepts lock and select clicks |
| D5 | How a notice shows | existing toast via `NoticeQueue` / plus a badge / none | **toast** first; a badge is a visual decision, later |
| D6 | Volume, brightness, mute keys inside apps | launcher handles them (no OSD over fullscreen apps) / leave to apps | **launcher** (P0); the contract says these keys are the launcher's |
| D7 | Brightness from apps | none / temporary borrow / persistent | **none in v1** |
| D8 | Power and network actions from apps | offer / not | **not** (Settings has them; "open a page" later) |
| D9 | Dark time while an app is in front | keep "never" / apps opt in to idle dim | **keep** now; revisit for the battery boards |
| D10 | CLI name | `cyberdeckzero-ctl` / `applaunch-ctl` / none | `cyberdeckzero-ctl` (NAMING.md product names) |
| D11 | First adopters | Mesh Hop / plus Net Toolbox beeps | **Mesh Hop**; Net Toolbox when it next releases |

## 8. Plan

| Phase | Tier | Repo | Content | Cost | Owner test |
| --- | --- | --- | --- | --- | --- |
| P0 | main | launcher | media keys acted on while an app is in front (supervisor's raw keys; legacy runner's key watch); work on a worker; contract note on reserved keys; tests | 0.5-1 | 5 min, tablet or HackBerry: in System Monitor press volume up / down, hear it; Settings shows the new level after Esc |
| P1 | main | launcher | board.state writer (start, after the probe, 30 s sound re-probe, on change), `cp0_app_board_read`, APP-CONTRACT section 5c, probe output, tests | 1 | with P2 |
| P2 | main | launcher | `launcher.sock` thread, parser, identity, rate limits, `hello`/`beep`/`notify`/`volume`/`brightness get`, `NoticeQueue` hand-over, `cp0_app_request.{h,c}`, CLI, APP-CONTRACT section 14, tests; drift script generalized | 2-2.5 | 10 min over ssh: `cyberdeckzero-ctl hello`, `beep`, `notify hi` on the home grid; the same with an app in front (toast after Esc); `cat $XDG_RUNTIME_DIR/applaunch/board.state` on two boards |
| P3 | main | launcher | release 0.8.6 | 0.3 | - |
| P4 | main | apps | re-pin; `_common` wrapper and fake socket for scenes; Mesh Hop: beep + notice on a message while hidden (muted chats excluded), option shown only with `sound=1`; publish | 1-1.5 | 10 min, HackBerry (speaker connected) or tablet: hide Mesh Hop, send a message from another node, hear it, see the toast on the home grid |
| P5 | secondary | apps | Net Toolbox beeps (walk test weak zone, wake watch, speed test done) | 0.5 | 5 min |
| P6 | later | both | badge (D5), brightness borrow (D7), idle dim (D9), open app/page, `X-Permissions`, port-holder helper | 1 each | - |

Main path P0-P4: about 4.8-6.3 ws plus three short owner tests. The smallest useful slice is P0 alone (0.5-1 ws). It
gives the volume keys in every app without touching an app.

## 9. Risks

- **Two owners of the same key.** An app that also acts on `KEY_VOLUMEUP` (none does today) would double the step.
  The contract must say these keys are the launcher's, as it does for Alt+Tab.
- **No feedback.** Volume changes inside a fullscreen app have no OSD. The user only hears the change. A later
  `status.state` `volume=` key could let the toolkit draw one (rebuild).
- **Silent by default.** If D4 follows the system-sounds switch, beeps are silent by default, and the feature looks
  broken.
- **Identity is weaker for some processes.** The pid-to-process-group lookup is weaker for a helper that left the
  app's group or a pid reused. Acceptable, since nothing privileged depends on it.
- **No sound on the deck and the Pi 5.** Beeps there do nothing (`err absent`). Board tests need the tablet or the
  HackBerry with its speaker connected.
- **Thread safety.** Calling the audio service from the server thread needs checking (4.4).
- **The contract only grows.** board.state keys and the socket protocol are additions to contract 1 and must stay.

## 10. Not verified

- Nothing was run on a board. The state of `/run/user/<uid>` (mode, presence for ssh logins with lingering) and a
  session D-Bus on the appliance boards are assumptions.
- Whether volume keys reach the launcher's handler while a **compat** app runs (its keys go through the launcher's
  virtual keyboard).
- The thread safety of `cp0_signal_audio_api` from a non-LVGL thread.
- The sink name prefix `bluez_output.` for the HackBerry speaker under PipeWire.
- The platform comparison (5.4) is general knowledge, not read today.
