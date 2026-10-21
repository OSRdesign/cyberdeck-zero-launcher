# cp0_lvgl Interface Reference

This document is the public contract for `ext_components/cp0_lvgl`. New pages and extension modules can use it as their first reference. When fixing a bug, changing behavior, or checking a platform difference, inspect the actual implementation under `src/` and the matching `*_contract.*` files. Device and SDL backends share signal names and argument formats, but their underlying capabilities can differ.

## Quick Start

Important headers:

- `include/hal_lvgl_bsp.h`: C++ signal declarations and `cp0_file_path`.
- `include/signal_register_plan.h`: the complete `cp0_signal_*` signature list.
- `include/cp0_lvgl_app.h`: C ABI for filesystem, network, process, battery, backlight, time, and sudo.
- `include/cp0_lvgl_app_runner.hpp`: LVGL runner.
- `include/ui_app_page.hpp`: page bases and page startup helpers.

Signals are global eventpp `CallbackList` objects. Request signals normally take `std::list<std::string>`: element zero is the command and the final argument is usually `std::function<void(int, std::string)>`. A callback receives `(code, data)`; `code == 0` is success and a negative code is an error. `data` may be empty, text, line records, or binary struct bytes. Do not assume the callback runs on the calling thread, and do not let callback exceptions escape.

Call `cp0_lvgl_init()` before using services; `cp0_lvgl_run()` does this automatically. Kconfig options named `CONFIG_CP0_LVGL_INIT_*` select services and `cp0_init_plan` controls their order. A disabled service has no usable implementation.

```cpp
cp0_signal_wifi_api({"Status"}, [](int code, std::string payload) {
    if (code != 0) return;
    // payload: connected:ssid:ip:signal:ethernet
});
```

Prefer the C wrappers in `cp0_lvgl_app.h` when one exists; they validate arguments and decode wire data.

## LVGL and Page API

```cpp
struct Cp0LvglRunOptions {
    std::function<void()> after_lvgl_init;
    std::function<void()> after_resource_init;
    std::function<bool()> setup;
    std::function<bool()> should_quit;
    std::function<void()> teardown;
};
int cp0_lvgl_run(Cp0LvglRunOptions options = {});
void cp0_lvgl_wake();
```

The runner supports one successful run per process. Its order is `lv_init`, `after_lvgl_init`, `cp0_lvgl_init`, `after_resource_init`, `setup`, the LVGL timer loop, `teardown`, and service cleanup. A false `setup`, an exception, or a missing display returns 1. `should_quit` ends the loop; `cp0_lvgl_wake()` wakes a loop waiting for the next LVGL timer.

`AppPageRoot` owns an LVGL screen and input group. `AppPage` adds the standard content container `ui_APP_Container` and top bar. `AppPageWithBottomBarLayout` adds `ui_BOTTOM_Container`. Use `cp0_lvgl_start_app_page(AppPageRoot&)` for an existing page or `cp0_lvgl_start_app<PageT>(args...)` to construct and load one. Create and destroy page objects on the LVGL thread. The standard top bar is 20 px high.

### Status strip and title (`include/cp0_ui_widgets.hpp`, `include/cp0_ui_status_strip.h`)

The launcher's top-right status strip (clock pill, Wi-Fi bars, Bluetooth glyph) and the home grid's title are part of the `cp0::ui` widget set. Native apps use them as they are: never draw your own clock or icons and never copy the code.

```cpp
using StatusSource = std::function<void(cp0_statusbar_state_t &state)>;
StatusSource default_status_source();          // sysfs / procfs, needs no service
class StatusStrip {
public:
    static StatusStrip *add(lv_obj_t *parent, StatusSource source = StatusSource());
    void refresh();
    lv_obj_t *canvas() const;
};
int status_strip_left();                       // x of the strip's leftmost drawn pixel
lv_obj_t *add_title(lv_obj_t *parent, const char *text);
```

- `StatusStrip::add(parent)` draws the strip at the top right of `parent` (a full-width screen or `Page::header()`, at y 0), placed and scaled by the layout service (shell `bar_h`, `status_w`, `status_top`, `status_pct`). It belongs to `parent`: its 1 s clock timer and 2 s status timer stop and it frees itself when `parent` is deleted. Never delete it yourself. Under the SDL simulator it returns NULL (the FreeType renderer `cp0_statusbar.c` is built for the device only).
- The source fills `wifi_up`, `wifi_pct`, `bt_on` and `bt_connected`; the strip sets the clock itself. It runs on the LVGL thread when the strip is added, every 2 s while the strip's screen is active, and on `refresh()`. It must return at once. Without a source the strip reads sysfs and procfs (`cp0_statusbar_read_state`). The launcher passes its own source (`native_ui::fill_status`: its Wi-Fi and Bluetooth services). `cp0_ui_status_set_wifi()` applies the bars' rule: connected shows at least one bar.
- Call `refresh()` right after loading the screen, so the icons are current at once.
- Fonts: `share/font/Montserrat-Medium.ttf` and `FontAwesome5-Solid+Brands+Regular.woff` through `cp0_file_path_c`, else `/usr/share/APPLaunch/share/font`. Apps do not bundle them.
- `status_strip_left()` is the `strip_left` argument of `cp0_ui_roller_page()`: a header keeps its own controls left of it. `add_title()` is the label `Page`'s `TitleFactory` makes (shell `title_x`, `title_y`, `title_px`, gold `#F0B400`).

### Lists: Roller or CompactList (`include/cp0_ui_widgets.hpp`, `include/cp0_ui_list.h`)

Two list widgets share the page scaffold, the keys (Up / Down, Enter and Right activate, Esc and Left go back, through
`LV_EVENT_KEY` on the page's input group, so the navigation aliases of `cp0_keyboard_navigation_contract.h` work) and
the touch rule (a tap selects a row, a tap on the selected row activates it).

| Use | When |
| --- | --- |
| `Roller` | Few big items you pick from: menus, Settings, choice lists, toggles and sliders. The selected row is centred under a fixed bar, the rows around it shrink and fade, orange chevrons show that more rows exist. A `Nav` row may opt in to `CompactList`'s grey row chevron (`RowSpec::chevron`: the value, then the chevron at the right edge; Settings > Date & Time > Time zone); a `Nav` value too long for its row ends with dots so the label keeps at least a third of it. |
| `CompactList` | Data and app lists, especially 8 rows or more: process tables, logs, sensor readings, serial ports, a menu inside an app. Top-aligned (row 0 under the header, the highlight moves down; the list scrolls only to keep the highlight and one row of context in view), no scroll chevrons, a thin scrollbar at the right edge while scrolling (gone 1 s later, none when every row fits). |

`CompactList` has two densities from the layout tokens: `ListDensity::Compact` (rows of the `CP0_TOK_LIST_ROW_H` token,
5.0 mm, at least 24 px: 57 px and about 7.5 rows under the header on the 640x480 deck; for dense data, below the 7 mm
list-row rule on purpose because a mis-tap only moves the highlight) and `ListDensity::Comfortable` (the roller's 7 mm
`CP0_TOK_ROW_H` row, for 3-6 touch-friendly rows and any list whose rows trigger an action). Rows are items (label left, cut with dots; value right,
never cut, coloured by `ValueTone::Normal / Ok / Warn / Bad / Muted`; Muted is the label grey for an absent / unset thing, never the only signal of status) or section headers (grey, upper case, never selected).
`set_rows()` is the live refresh: only changed texts and colours are touched, the scroll offset and the selected index
stay (no flicker, no jump). `reset_rows()` is new content (top, highlight on `selected`). `read_only` makes a plain
scrolling list without highlight; `empty_text` (or your own card in `empty_area()`) shows while there is no row;
`layout.separator_px = 1` draws hairlines between items.

```cpp
cp0_ui_roller_page_t page_layout;
cp0_ui_roller_page(cp0_ui_metrics_get(), cp0::ui::status_strip_left(), &page_layout);
cp0::ui::Page page(screen, page_layout, cp0::ui::add_title, [] { /* back */ });

cp0_ui_list_layout_t layout;
cp0_ui_list_page_layout(cp0_ui_metrics_get(), &page_layout, CP0_UI_LIST_COMPACT, &layout);
cp0::ui::CompactListEvents events;
events.activate = [](int index, std::intptr_t id) { open_process(static_cast<int>(id)); };
events.back = [] { /* the page's back */ };
cp0::ui::CompactListOptions options;
options.empty_text = "No process";
cp0::ui::CompactList list(page.root(), layout, std::move(events), options);
lv_group_add_obj(group, list.focus_object());
lv_group_focus_obj(list.focus_object());

std::vector<cp0::ui::ListRow> rows;
rows.push_back(cp0::ui::ListRow::section("Top processes by CPU"));
rows.push_back(cp0::ui::ListRow::item("python3 logger.py", "23.4 %", cp0::ui::ValueTone::Warn, 1234 /* id */));
list.reset_rows(rows);   // first time / new page: top, first item highlighted
list.set_rows(rows);     // every second afterwards: values in place, scroll and selection kept
```

#### Row affordances: what a row does, at a glance

An item can say what activating it does, so the user tells actionable rows from static information before touching
one. It is an optional field of each row, `ListRow::affordance` (`cp0::ui::RowAffordance`, also the last argument of
`ListRow::item()`), and it is purely visual: keys, taps, row heights and tap targets do not change.

| Affordance | Use it when the row ... | Look |
| --- | --- | --- |
| `Open` | navigates: opens a page or a sub-list (a tool, a detail page) | a grey chevron `›` at the right edge |
| `Edit` | changes a value, in place or through a picker ("Network: Automatic (wlan0)", a baud rate) | the value in its tone (green, ...), then the chevron |
| `Action` | performs something (Save, Start, Listen, Send, Wake) and can be started now | a filled white dot in the chevron's slot; the label stays white |
| `ActionRunning` | is that action, in progress (listening, a speed test running) | the same dot in the accent blue `#5AAAFF` |
| `Info` | is read-only information (an address, a vendor, a reading) | no chevron; a `Normal` value is drawn grey (`Muted`, `#9A9A9A`); an explicit `Ok` / `Warn` / `Bad` / `Muted` tone is kept |
| `Unspecified` (default) | has not been tagged yet | exactly as before affordances existed |

Apps adopt it **per row**: a row nobody tagged keeps today's look pixel for pixel, so an app opts in row by row, in its
own change. On an `Info` row only an explicit tone (`Ok`, `Warn`, `Bad`) keeps a coloured value. The chevron (`CP0_UI_LIST_CHEVRON_TEXT`, U+203A of Montserrat Medium)
is 160 % of the label size (its ink about 7/8 of the capitals' height), centred on the row's middle and drawn in its own box at
the right inset; the label and the value end a gap before that box, the value is still never cut. The Action dot
(60 % of the label size, 20 px on the deck's comfortable rows) sits in that same box, centred in it and on the row's
middle, so the values of chevron rows and dot rows end in one column. White is the idle dot on the black rows and on
the selected bar (`#2B2B2B`, 14:1); blue says the action runs. Switch a row between `Action` and `ActionRunning` with
the live refresh (`set_rows()`, or `update_row()` / `set_value()` keep the row's other fields): only the dot's colour
changes, nothing is rebuilt, the selection and the scroll stay. Sections ignore the field.

```cpp
using cp0::ui::ListRow;
using cp0::ui::RowAffordance;
using cp0::ui::ValueTone;
rows.push_back(ListRow::item("Network", "Automatic (wlan0)", ValueTone::Normal, kNetwork, RowAffordance::Edit));
rows.push_back(ListRow::item("Diagnose my connection", "", ValueTone::Normal, kDiagnose, RowAffordance::Open));
rows.push_back(ListRow::item("MAC address", mac, ValueTone::Normal, 0, RowAffordance::Info));
rows.push_back(ListRow::item("Save for Wake-on-LAN", "", ValueTone::Normal, kSaveWol, RowAffordance::Action));
rows.push_back(ListRow::item("Listen", listening ? "listening" : "", ValueTone::Normal, kListen,
                             listening ? RowAffordance::ActionRunning : RowAffordance::Action));
list.set_rows(rows); // the listen state changed: only that dot turns blue (or white again)
```

A list of your own draws the same mapping from `cp0_ui_list.h`: `cp0_ui_list_has_chevron()`, `cp0_ui_list_has_dot()`,
`cp0_ui_list_label_rgb()`, `cp0_ui_list_value_rgb()`, `cp0_ui_list_dot_rgb()`, `cp0_ui_list_chevron_px()` / `_w()`,
`cp0_ui_list_trail_w()` (what to keep free at the right, the same for a chevron and a dot), `cp0_ui_list_dot_px()` /
`cp0_ui_list_dot_x()` and `cp0_ui_list_chevron_y()` (the chevron or the dot centred in a row of any height, a two-line
row too), with the palette constants `CP0_UI_LIST_CHEVRON_RGB`, `CP0_UI_LIST_DOT_IDLE_RGB` and
`CP0_UI_LIST_DOT_RUNNING_RGB` (= `CP0_UI_LIST_ACCENT_RGB`).

The maths (row tops, scroll-to-visible, key and tap rules, the refresh rule, the affordance mapping) are pure C in
`cp0_ui_list.c`, unit-tested by `tests/test_ui_list.c`; the render harness scene `compact_list` shows both densities at
640x480, 800x480, 720x720 and 480x320, `compact_list_affordance` every affordance at the five sizes.

## Public C ABI

Declarations are in `include/cp0_lvgl_app.h` and `include/hal_lvgl_bsp.h`. Unless noted otherwise, 0 means success and a negative value means failure.

### Filesystem and paths

```c
void cp0_lvgl_init(void);
const char *cp0_file_path_c(const char *file);
int cp0_dir_list(const char *path, cp0_dirent_t *entries, int max_entries, int *out_count);
cp0_watcher_t cp0_dir_watch_start(const char *path);
int cp0_dir_watch_poll(cp0_watcher_t watcher);
void cp0_dir_watch_stop(cp0_watcher_t watcher);
int cp0_file_read_first_line(const char *path, char *out, int out_size);
```

`cp0_file_path_c` resolves logical names such as `applications`, `appstore_exec`, `launcher_settings`, `home_dir`, `lock_file`, `keyboard_device`, `keyboard_map`, and `share/images/`, `share/audio/`, or font resources. The returned C string belongs to a thread-local cache; copy it if it must outlive the call. Watch handles are nonzero module-owned values and may only be passed to the matching start/poll/stop functions.

### Network and Wi-Fi

```c
int cp0_network_list(cp0_netif_info_t *entries, int max_entries, int *out_count);
int cp0_wifi_status_read(cp0_wifi_status_t *status);
int cp0_wifi_scan(cp0_wifi_ap_t *entries, int max_entries);
int cp0_wifi_connect(const char *ssid, const char *password);
int cp0_wifi_connect_hidden(const char *ssid, const char *password);
int cp0_wifi_profile_forget(const char *ssid);
int cp0_wifi_profile_exists(const char *ssid);
int cp0_wifi_disconnect_active(void);
int cp0_wifi_radio_enabled(void);
int cp0_wifi_radio_set_enabled(int enabled);
```

`cp0_wifi_scan` returns an AP count or `CP0_WIFI_ERROR_INVALID=-1`, `RADIO_OFF=-2`, `AUTH=-3`, `NOT_FOUND=-4`, `IP_CONFIG=-5`, `SERVICE=-6`, or `TIMEOUT=-7`. `signal` is normally RSSI dBm; legacy backends may use 0..100.

### Process and system

`cp0_process_exec_blocking`, `cp0_process_spawn`, `cp0_process_stop`, `cp0_process_check_lock`, `cp0_process_kill`, `cp0_system_shutdown`, `cp0_system_reboot`, `cp0_process_run_argv`, and `cp0_process_capture_argv` are declared in `cp0_lvgl_app.h`. `keep_root` and `background` are 0/1. `grace_ms` is limited to 300000. `cp0_process_run_sudo` is deprecated; use asynchronous sudo. Check an executable with `cp0_desktop_exec_is_safe` before launching it.

### Sudo C API

`cp0_sudo_run_argv_async` and `cp0_sudo_run_shell_async` accept a callback-thread selection, output callback, completion callback, and user pointer. `cp0_sudo_run_argv_async_ex` additionally accepts `auth_timeout_ms`, `exec_timeout_ms`, and an output `request_id`. `cp0_sudo_cancel(request_id)` is idempotent while the id is retained; unknown or expired ids return `-ENOENT`. `cp0_sudo_queue_password` queues a short-lived password for the LVGL prompt.

`CP0_SUDO_CALLBACK_LVGL` delivers output and completion on the LVGL thread; `CP0_SUDO_CALLBACK_WORKER` calls them on a worker. Output is ordered and LVGL delivery has bounded backpressure, so callbacks must return quickly. Completion results are `CP0_SUDO_RESULT_SUCCESS`, `AUTH_FAILED`, `EXEC_FAILED`, `CANCELLED`, or `TIMED_OUT`, together with the process exit code.

### Battery, backlight, and time

`cp0_battery_read` reads the latest background snapshot and performs no hardware I/O; use it only when `valid == 1`. Battery units are mV, mA, 0.1 C, mAh, and percent SOC. `cp0_bq27220_calibrate` accepts indices 0..3. `cp0_backlight_write` clamps to 0..max. `cp0_time_set` requires `YYYY-MM-DD HH:MM:SS`; manual RTC persistence requires NTP to be disabled. `cp0_time_ntp_get/set` controls NTP.

Other C functions read Ethernet/default network information, account information, time text and desktop execution safety.

### App contract reader (`include/cp0_app_contract.h`)

Public since task 014 P5, for apps the launcher starts. Pure C99 + POSIX, no LVGL and no cp0 runtime: an app may copy `include/cp0_app_contract.h` + `src/cp0_app_contract.c` with `include/cp0_ui_metrics.h` + `src/cp0_ui_metrics.c` (class names, token fallback), and `src/cp0/cp0_fb_output.{h,c}` to draw on a rotated or 16 bpp framebuffer.

```c
int cp0_app_env_read(cp0_app_env_t *out);                         /* 0: started by a contract-1 launcher, -1 otherwise */
int cp0_app_state_path(const char *file, char *buf, size_t size); /* APPLAUNCH_STATE_DIR, XDG_RUNTIME_DIR/applaunch, /tmp */
int cp0_app_screen_read(const char *path, cp0_app_screen_t *out); /* NULL = default path; parse: cp0_app_screen_parse */
int cp0_app_screen_compute(int w, int h, int rot, int ppmm_x100, cp0_app_screen_t *out); /* no file: same numbers */
int cp0_app_input_read(const char *path, cp0_app_input_t *out);   /* parse: cp0_app_input_parse */
int cp0_app_osk_allowed(const cp0_app_input_t *in, int64_t now_mono_s); /* the shared on-screen keyboard rule */
int64_t cp0_app_monotonic_s(void);
```

The readers return 0 or -1 and always fill `out` (cleared on failure); unknown keys are ignored. `cp0_ui_osk_policy_allows()` calls the same `cp0_app_osk_rule()`, so apps and the launcher decide alike. Variables, file keys, failure modes and the stability promise: [`APP-CONTRACT.md`](APP-CONTRACT.md).

## Complete `cp0_signal_*` Usage

All `args` below are `std::list<std::string>` with the command at element zero. Request callbacks use `void(int code, std::string data)` unless a different signature is shown.

### Audio

- `cp0_signal_audio_play(std::string path)`: one-way playback of a path.
- `cp0_signal_audio_cap(bool enable)`: one-way start/stop capture.
- `cp0_signal_system_play(std::string name)`: play a system sound name. Register extra names first with `RegisterSystemSounds`.
- `cp0_signal_audio_setup`: `{"set_callback"}`, `{"set_waveform"}` or `{"set_waveform","0"}`, and `{"stop_play"}` set status callbacks, waveform reporting, and stop playback.
- `cp0_signal_audio_api`: `PlayFile path`, `Play asset`, `PlayPause`, `PlayContinue`, `PlayEnd`, `Cap`, `CapPause`, `CapContinue`, `CapEnd`, `CapFileSave path`, `SetCallback`, `VolumeRead`, `VolumeWrite 0..100`, `MuteRead`, `MuteToggle`, `SetSystemSoundNames [up to 3 names]`, `RegisterSystemSounds [1..32 names]`, `SystemSoundPlay 0..2`, `SystemSoundSuspend`, `SystemSoundPrepare`, and `SystemSoundEnable [0|1/on/off...]`. `Play` resolves a separator-free name as a resource; `PlayFile` uses the supplied path. Errors are negative codes with text.

### PTY

`cp0_signal_pty_api` commands are `Open executable [columns] [rows] [argv...]` (default 80x24, returns a numeric handle), `Read handle [max_read]` (default 4096, max 1 MiB; code is byte count), `Write handle data` (code is byte count), `Resize handle columns rows`, `CheckChild handle` (code 0 running, 1 exited; data is exit code), and `Close handle`. Handles are not PIDs. Linux children use `TERM=vt100` and may drop privileges according to `run_as_user`; teardown kills remaining children.

### Configuration

`cp0_signal_config_api` uses `$HOME/.config/cardputerzero/config.json`, up to 64 entries, 63-byte keys, and 255-byte values. Commands are `Init`, `Save`, `GetInt key fallback`, `SetInt key value`, `GetStr key fallback`, `SetStr key value`, and `SetManyAndSave key value [key value...]`. Set operations change memory; call `Save` to persist, or use the transaction command for atomic update-and-save.

### Filesystem signal: `cp0_signal_filesystem_api(args, cb)`

Commands are `Path logical_name`, `DirList path`, `DirListDetail path`, `Exists path`, `ReadFile path [max_bytes]`, `EnsureDirForUser path`, `Touch path`, `Remove path`, `WatchStart path`, `WatchPoll handle`, and `WatchStop handle`. `Exists` returns `1/0`; `WatchStart` returns a handle; directory data is line-encoded. Empty or NUL-containing paths are invalid. Unknown commands return code -2.

### Wi-Fi

Commands are `Status`, `Scan [0..32]`, `Connect ssid [password]`, `ConnectHidden ssid [password]`, `Disconnect`, `ProfileForget ssid`, `ProfileExists ssid`, `ProfileDisconnectActive`, `RadioEnabled`, and `RadioSetEnabled on|off|1|0|true|false`. Status data is `connected:ssid:ip:signal:ethernet`. Scan data has one `ssid:signal:security:in_use:saved` record per line, with backslash, colon, LF, and CR escaped. Prefer the C wrappers for decoding.

### Bluetooth and Agent

`cp0_signal_bt_api` supports `BtStatus`, `BtList [max=16]`, `BtConnectedList [max=16]`, `BtScan [max=16]`, `BtReset`, `BtPower 0|1`, `BtAlias name`, `BtDiscoverable 0|1`, `BtDiscoveryStart`, `BtDiscoveryStop`, `BtPair address`, `BtCancelPairing address`, `BtConnect address`, `BtDisconnect address`, `BtRemove address`, and `BtDeviceAlias address name` (a known device's name, BlueZ `Device1.Alias`; the name follows the `BtAlias` rules: 1-63 bytes, no control characters). Addresses must be `XX:XX:XX:XX:XX:XX`. Status data is `powered<TAB>address<TAB>discoverable<TAB>alias`; device lines are `address<TAB>rssi<TAB>connected<TAB>paired<TAB>trusted<TAB>name`.

Session commands are `BtSessionInit` (returns an id), `BtSessionDeinit id`, `BtStatusGet id`, `BtConnectedListInit id`, `BtConnectedListGet id`, `BtConnectedListDeinit id`, `BtScanOn id`, and `BtScanOff id`. `BtScanOn` reports from a worker about every three seconds; stop it before destroying the session. `BtScan`, pairing, and connection operations can complete asynchronously.

`cp0_signal_bt_agent` has signature `(uint64_t id, std::string method, std::string device, std::string value, std::function<void(bool accepted, std::string text)> reply)`. It originates on a BlueZ worker thread. Marshal UI work to the LVGL/event loop, then call `reply` exactly once. `method` is `RequestPinCode`, `RequestPasskey`, `RequestConfirmation` (value: the 6-digit code), `RequestAuthorization` or `AuthorizeService` (value: the service UUID). `DisplayPasskey` (value: the 6-digit passkey) and `DisplayPinCode` (value: the PIN) are notices: BlueZ was already answered and the user types the code on the remote device (a keyboard); their `reply` does nothing, and they are only sent while a subscriber exists.

### Settings

`cp0_signal_settings_api` commands are `BacklightRead`, `BacklightMax`, `BacklightWrite value`, `Log topic message`, and `TimeStr`. Integer results are returned as decimal text; `TimeStr` is `HH:MM`.

### Process, OS information, and time

`cp0_signal_process_api` commands are `ExecBlocking executable keep_root`, `Spawn executable keep_root`, `Stop pid`, `CheckLock path`, `Kill pid grace_ms`, `RunArgv background argv...`, `RunSudo password argv...` (legacy only), `CaptureArgv argv...`, `DesktopExecIsSafe executable`, `Shutdown`, `Reboot`, and `DelayMs milliseconds`. `Spawn` returns a PID; `CaptureArgv` returns stdout.

`cp0_signal_osinfo_api` and `cp0_signal_timedate_api` share one service. Commands are `NetworkDefaultInfoRead`, `EthInfoRead` (three lines: IPv4, gateway, MAC), `NetworkList` (tab-separated interface records), `AccountInfoRead` (user and hostname), `TimeSet timestamp`, `LocalTime` (`year,month,day,hour,minute,second`), `RandomU32`, `NtpGet`, and `NtpSet 0|1`.

### Battery, screenshot, camera, and soundcard

- `cp0_signal_bq27220_api`: `Read` returns encoded `cp0_battery_info_t`; `Calibrate index` accepts 0..3.
- `cp0_signal_screenshot_api`: `Save directory` writes a BMP and returns its complete path.
- `cp0_signal_battery_pub(std::function<void()>)`: triggers one battery event publication to the active LVGL screen. The function argument is retained for signature compatibility, not subscription registration; call on the LVGL thread.
- `cp0_signal_camera_api`: `Open [width] [height]`, `Start`, `Close`, `Stop`, `Capture [path] [width] [height]`, `Photo`, `Status`, `SetCallback`, `SetFrameCallback`, `ZoomIn`, `ZoomOut`, and `Pan dx dy`. Defaults are 320x150. Status code 0 means streaming and 1 means stopped; unsupported libcamera returns -10. Frame callbacks may run off-thread and must be marshalled to LVGL.
- `cp0_signal_soundcard_api`: `ListCards`, `ListControls card_index`, `GetControlDetail card_index control`, and `SetControl card_index control value`. Values preserve raw `amixer` text; unavailable ALSA/process support returns `not supported` or an error.

### Sudo signals

`cp0_signal_sudo_argv_async` takes `(argv, auth_timeout_ms, exec_timeout_ms, complete, started)`. `started(ret, request_id)` arrives first; on success `ret == 0` and the id is nonzero. `complete(result_code, exit_code)` then arrives once. Cancel with `cp0_signal_sudo_cancel(request_id, done)`; `done(0)` means cancellation was accepted.

`cp0_signal_system_admin_async` has the same signature but only accepts `NtpSet 0|1`, `TimezoneSet zone` (runs `timedatectl set-timezone zone`, the zone one argument with no shell; 1..64 bytes of `[A-Za-z0-9_+/-]`, each `/`-separated part starting with a letter: `cp0::tz::valid_name`, `include/cp0_timezone.hpp`), and `TimeSet YYYY-MM-DD HH:MM:SS`. Invalid arguments call only `started(-EINVAL, 0)` and do not produce a completion callback. Keep request ids and cancel them when a UI workflow leaves or times out.

`cp0_signal_network()` and `cp0_signal_forkexec()` are reserved plan declarations. The current `cp0_lvgl` sources do not register them, so confirm an implementation before calling.

## Change and Debug Rules

1. Read this document and `include/signal_register_plan.h` before changing a call site.
2. When changing an interface, update this document, the matching contract, C wrapper, call sites, and tests.
3. For bug fixes, inspect the implementation and contract files; verify callback threads, initialization, teardown, background workers, and wire escaping.
4. Run `ext_components/cp0_lvgl/tests/run_tests.sh`. Changes involving Linux commands, BlueZ, libcamera, framebuffer, or sudo also require target-platform validation.
