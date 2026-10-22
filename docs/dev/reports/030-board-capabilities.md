# 030 - Board capabilities: battery, brightness and sound per board, detected at run time

Status: design written 2026-10-22 on branch `report-030-capabilities` (= launcher main dc9a9ab, v0.8.3). Research and
this document only: no product code, no board touched (the board facts below were measured by the owner today on
launcher 0.8.3 and handed in with the brief), nothing merged.

Owner request (2026-10-22): "battery level, screen brightness level and sound on hackberry, sound on tablet, screen
brightness on pi5, objective underneath ability for the launcher to identify when a board has sound, a battery etc."

Markers: **M** = measured on the board today (brief), **V** = verified in the launcher code (file:line), **S** = read
in a cited source (vendor repository, schematic, kernel tree, forum), **I** = inferred, needs a board. Owner decisions
are **C1-C12** (section 6).

## 1. Summary and recommendation

**What the request needs, per target:**

| Target | What it is | Launcher work | Install step | Confidence |
| --- | --- | --- | --- | --- |
| HackBerry battery | MAX17048 fuel gauge at 0x36 on the touch panel's I2C bus (S). The stock Pi kernel has its driver (`max17040_battery`, `=m`) (S). No kernel device is created today, so `/sys/class/power_supply` is empty (M) | none: the v0.8.3 `power_supply` reader and strip icon take it as they are | a root boot-time unit creates the I2C device on the touch bus. No config.txt change, no reboot, no DKMS | high for the hardware, medium for the bus lookup |
| HackBerry sound | **a Bluetooth speaker built into the case**: an MH-M18 Bluetooth audio receiver named `XWF-M18-M28-M38` feeds a PAM8406 amplifier and two speakers (S). There is no wired audio path from the CM5 (S, schematic) | pair it and **trust** it in Settings > Bluetooth (the launcher never sets Trusted today, V). The Speaker row comes from the sound server's Bluetooth sink | possibly a WirePlumber drop-in so Bluetooth audio runs while the desktop is stopped (I) | high for the hardware, unknown for PipeWire while the launcher runs |
| HackBerry brightness | **not controllable from the CM5**. The boost converter is always on, and brightness comes from a hardware dimmer chip with a push button (S, schematic) | no backlight to drive. Show "use the button" (C7) and/or dim the launcher in software | none | high (schematic), the button needs a check by the owner |
| Tablet sound | HDA Intel PCH with real controls (M). The row is hidden twice: by the compile-time profile and because the native Settings has no widget for the volume page (V) | runtime Speaker row (a native Choice, like Brightness) and a volume backend that also works without PulseAudio/PipeWire (ALSA) | `audio` group, `alsa-utils` | high |
| Pi 5 brightness | HyperPixel 4 `gpio-backlight` on GPIO19: on/off only, max 1 (M, S). Today Settings offers 10 levels that all write 1 (V) | detect max 1 and use the existing on/off kind. Levels need PWM | levels only with a PWM-backlight overlay plus a reboot (C8) | high for on/off; PWM dimming is reported to work on a Pi 4 and a Zero W, **not verified on a Pi 5** |

**Recommended approach.**
- **One small run-time module, `cp0_capabilities`, in `cp0_lvgl`.** It is plain C like `cp0_power_supply` and reads
  sysfs and procfs. A separate, slower probe asks the sound server. It reports, for battery, backlight and sound,
  one of: present (with its details), absent, or "expected but not set up" (from a board.conf hint).
- **Settings and the hotkeys read it** instead of `APPLAUNCH_SETTINGS_SPEAKER/BATTERY/ETHERNET`.
- **install.sh does the one-time root parts** (gauge device, udev rules, groups, optional overlays). The launcher
  never needs sudo at run time.

Phases c-0 to c-2 (section 7) deliver everything asked except Pi 5 levels. That is about **3-4 worker sessions** plus
three short owner tests.

## 2. Today: what the launcher does per board kind

### 2.1 Code (V)

| Area | Where | Behaviour |
| --- | --- | --- |
| Compile-time profile | `main/ui/settings/settings_hw_profile.hpp:14-26` | every bundle builds with `APPLAUNCH_HW=pizero2w`: `SPEAKER 0`, `ETHERNET 0`, `BATTERY 0` (the bq27220 pages) |
| Speaker row | `settings_page.cpp:744-753` | compiled out. It is also `PageType::FullCustom`, which `settings_node_kind` maps to `Custom` (`settings_page.cpp:684`), and the native host hides Custom rows (`settings_native_host.cpp:220-221`). `LvSettingVolumePage3` is already an `LvSettingValuePage3Base` (`settings_volume_page.hpp:17`) like Brightness, so it can become a native Choice: add it to the value-page list (`settings_page.cpp:689-693`) and drop FullCustom |
| Volume backend | `cp0_audio_mixer_control.cpp:38-90` | `pactl get/set-sink-volume @DEFAULT_SINK@`, mute likewise: **works only when a PulseAudio or PipeWire-pulse server answers** |
| System sounds | `cp0_audio_system_sound_player.cpp:361` | miniaudio with the **PulseAudio backend only**. Without a server, no lock/unlock sounds (silently) |
| ALSA access that exists | `cp0_lvgl_soundcard.cpp:30-42` | `/proc/asound/cards`, `amixer -c N` list / sget / sset (soundcard service). Not used by the volume page |
| Volume hotkeys | `ui_global_hint.cpp:126`, `launcher_media_controls.cpp:164-202` | KEY_VOLUMEUP/DOWN/MUTE go to the same pactl backend on every board, with or without sound |
| Battery | `cp0_power_supply.h:9-15`, `cp0_ui_status_strip_widget.cpp:163-165`, `settings_page.cpp:798-800` | **already run-time** (v0.8.3, X12). The first `power_supply` of type Battery (not scope Device, present, with a charge) drives the strip icon and Settings > Battery. Test root `APPLAUNCH_POWER_SUPPLY_ROOT`. The strip lives in `cp0_lvgl`, so apps' strips show it too |
| Backlight kind | `cp0_backlight_profile.c:31-40`, `cp0_lvgl_settings.cpp:173-178` | **from `APPLAUNCH_BACKLIGHT` only**: `gpio:<dir>` on/off, `sysfs:<dir>` levels, unset = levels on `/sys/class/backlight/backlight`. No scan of `/sys/class/backlight` |
| Brightness row | `settings_page.cpp:731`, `:597-612` | always shown: On / "Off 10 s" for gpio, else 10 levels. `brightness_step_value` floors at 1 (`setup_value_policy.cpp:69-75`): **with max 1 every level writes 1** |
| Backlight permissions | `install.sh:625-645`, `90-backlight-unblank.rules:4` | udev rules for `backlight_gpio` and for the profile's `sysfs:` device. The deck's `backlight` gets only `bl_power` from rule 90 |
| Groups | `install.sh:1148-1150` | `input,video,netdev` (+`bluetooth`). **No `audio`** |
| Bluetooth | `cp0_bluez_dbus_client.cpp:666-676`, `:758-779` | reads `Trusted` but never sets it: Pair / Connect / Disconnect / Remove only. The paired list does not show Trusted |
| Desktop variant | `cyberdeckzero-desktop.service:26-29` | `User=` + `PAMName=login`: a login session so "the user's own services (sound) stay up" after the desktop stops |

### 2.2 Per board (today, 0.8.3)

| Board | Battery | Backlight (profile, kind) | Sound |
| --- | --- | --- | --- |
| Deck (Zero 2 W, Waveshare 2.8" DPI) | none, no row | unset = levels on `/sys/class/backlight/backlight`, the PWM overlay install.sh adds (`waveshare-pwm-backlight.dts:29-31`, V) | no row. The Zero 2 W has no analog output (I) |
| Pi 3A+ Luckfox SPI | none | `gpio:/sys/class/backlight/backlight_gpio` on/off (`install.sh:461`, V) | no row. A 3.5 mm jack exists on the 3A+ (I, ALSA "Headphones", not measured) |
| Pi 3A+ Waveshare DSI | none | `sysfs:/sys/class/backlight/10-0045` levels (`install.sh:486`, V) | as above |
| Pi 5 + HyperPixel 4 | none | unset = "levels" on `backlight`, **max 1** (M): 10 levels, all on | no row; ALSA vc4-hdmi only, no controls (M) |
| HackBerry CM5 | none, `power_supply` empty (M) | unset; `/sys/class/backlight` empty (M): the row is shown but does nothing | no row; ALSA vc4-hdmi-0/1 only (M). `Master`/`Capture` on the default device point to a PipeWire/Pulse ALSA plugin (I) |
| FZ-M1 tablet | BAT1 + AC, shown (M, v0.8.3) | `sysfs:/sys/class/backlight/intel_backlight` 374/937, works (M) | hidden (profile + native host), although HDA Intel PCH has Master/Headphone/Speaker/PCM (M) |

## 3. Per target: options and recommendation

### 3.1 HackBerry CM5: what the hardware is

Source: the vendor repository [ZitaoTech/HackberryPiCM5](https://github.com/ZitaoTech/HackberryPiCM5) (the owner's
reference), its schematic `Hardware/Schematic_HackberryPi_CM5_Q20.pdf` (rev 1.0, 2025-02-17, read as text and
rendered crops), and its issues.

| Item | Fact | Source |
| --- | --- | --- |
| Gauge | **MAX17048** (U13) at I2C 0x36, SDA/SCL on `TP_SDA`/`TP_SCL`, "the battery voltage measure chip and the screen's touch controller are on the same I2C bus". ALRT# pulled up, no CPU interrupt line | S: `Battery_Voltage_Measure/README.md`, schematic U13 |
| Touch / gauge bus | software `i2c-gpio` on GPIO 10/11, touch `edt,edt-ft5406` at 0x48 (owner's touch: EP0110M09, M) | S: `Operating System/hackberrypi.dts` |
| Bus number | dynamic: the vendor script says 11; users found **13** (issue #22) and **15** (`15-0036`, issue #46). The owner's board has `/dev/i2c-13..15` (M), so it is likely one of those (I) | S: [#22](https://github.com/ZitaoTech/HackberryPiCM5/issues/22), [#46](https://github.com/ZitaoTech/HackberryPiCM5/issues/46) |
| Vendor battery driver | a `max17048.rar` (missing from the repo) later traced to [CNflysky/hackberrypiq20](https://github.com/CNflysky/hackberrypiq20): an out-of-tree **DKMS** module plus a `hackberrypicm5` overlay that replaces the display overlay. It needs kernel headers, dkms, build-essential and dtc, and breaks on kernel updates (issue #46). The module reports charging state from the CRATE register | S: `Operating System/RaspberryPi_OS/README.md`, [#13](https://github.com/ZitaoTech/HackberryPiCM5/issues/13), [#11](https://github.com/ZitaoTech/HackberryPiCM5/issues/11) |
| Stock kernel driver | `drivers/power/supply/max17040_battery.c` supports `maxim,max17048` and the I2C id `max17048`. It registers a power supply named `battery`, type Battery, with capacity, voltage, online and present. Status comes only from a supplier, else **Unknown**. `CONFIG_BATTERY_MAX17040=m` in `bcm2712_defconfig` | S: raspberrypi/linux `rpi-6.12.y` |
| Overlay pitfalls | kernel 6.12.30+ races `i2c-gpio` against the DPI probe (touch lost), and two `i2c-gpio` nodes on the same pins make only one of touch and battery work. Users pin the kernel or rebind the bus after boot. `i2c_arm` and `spi` must stay off (the DPI uses the pins) | S: [#47](https://github.com/ZitaoTech/HackberryPiCM5/issues/47) ([lost-down-south/hackberrypi-cm5-fix](https://github.com/lost-down-south/hackberrypi-cm5-fix)), [#43](https://github.com/ZitaoTech/HackberryPiCM5/issues/43) |
| Charger | IP5310 (5 V) + IP5306 (charging); no data link found in the schematic | S: `Hardware/README.md`, schematic |
| Backlight | U59 **MT3540 boost, EN tied to VIN (3V3)**: always on. Brightness comes from U49 "YX-1024-LED 调光" (a dimming chip; no datasheet found): K1 is push button **SW3**, and output D1 switches the panel's LED cathode through MOSFET Q3. K2 goes to a net label `OUTPUT` that appears nowhere else on the sheet. No CM5 GPIO, PWM or I2C line reaches the circuit | S: schematic, "Display" block |
| Audio | U43 **MH-M18** Bluetooth audio receiver (L/R) feeds U39 **PAM8406** and two 8 Ω speakers. The module's MUTE output mutes the amplifier through Q5; its KEY pin is unconnected. "You need to pair with the bluetooth module on board to make sound"; it shows up as **`XWF-M18-M28-M38`**; "Make a reboot, otherwise you will likely not be able to adjust the volume" | S: `Speakers/README.md`, `Basic setup/README.md`, schematic, `Speakers/MH-M18-datasheet.pdf` (Bluetooth 4.2 receiver, auto-reconnect; profiles not stated) |
| Keyboard | RP2040 (QMK/Vial) over USB; its own key backlight has 10 steps. That is the keys, not the screen | S: `Hardware/README.md`, `Keyboard/README.md` |

**The built-in Bluetooth device (owner's "trick").** It is the MH-M18 speaker module. It is not the keyboard (USB),
and it gives neither the battery (the gauge is the I2C MAX17048; the module is powered from the board's 5 V, S) nor
brightness. It only gives sound. In the launcher:
- **Settings > Bluetooth.** Scan, pair and connect it like any device. After pairing, also set `Trusted=true` (one
  property write next to Pair, `cp0_bluez_dbus_client.cpp:758-779`) so that it reconnects by itself after a boot. Do
  this for every device: it also fixes the BT keyboard that drops when untrusted (memory note of 2026-10-15). It then
  appears in Paired Devices.
- **Showing "trusted".** Whether the paired list should show it is a UI choice (C6).
- **Volume.** It goes through the sound server's Bluetooth sink (`pactl` on that sink, today's backend). Whether the
  module supports AVRCP absolute volume is unknown; PipeWire then uses software volume (I).
- **Risk (I).** The A2DP connection needs the sound server's Bluetooth endpoints. WirePlumber's BlueZ monitor runs
  only while the user's logind session is active on a seat (its seat-monitoring default). The desktop unit's
  `PAMName=login` session is probably not that, so Connect may fail with "profile unavailable" while the launcher
  runs. The fix would be a user drop-in (`monitor.bluez.seat-monitoring = disabled`) written by install.sh. **To
  measure in c-0.**

### 3.2 HackBerry: options

**Battery.**

| Option | What | Cost / risk |
| --- | --- | --- |
| **B1 (rec)** stock driver, device created at boot | a root oneshot unit (`cyberdeckzero-gauge.service`, hackberry-cm5 only) finds the bus with the touch at 0x48 (`/sys/bus/i2c/devices/*-0048`), then writes `max17048 0x36` to that bus's `new_device`; udev loads `max17040_battery`. The launcher reads `battery` as on the tablet | about 30 lines of shell + unit + uninstall + SYSROOT tests. No config.txt, no reboot, survives kernel updates (in-tree module). Percent and voltage only; state Unknown, so no "charging" colour. Bus lookup to confirm in c-0 (I) |
| B2 overlay with `maxim,max17048` under the existing touch node | an overlay that targets the display overlay's `i2c-gpio` node | depends on which display overlay the board uses (`hackberrypi` or `vc4-kms-dpi-hyperpixel4sq`); a second `i2c-gpio` node breaks touch (#47); a reboot is needed |
| B3 vendor/community DKMS driver | `hackberrypi-max17048-dkms` from its apt repository | adds charging state. It needs headers, dkms and a compiler on the board, a third-party apt key, and an overlay swap. It broke after kernel updates (#46) |
| B4 userspace read in the launcher (`/dev/i2c-N`, register 0x04) | like the vendor Python script | needs the `i2c` group and a bus guess. A second, launcher-only battery path that apps' strips would not see. No |

**Brightness.** No backlight can be driven (schematic), so a hardware level is impossible without a board modification.
- **L1 (rec).** An info row "Brightness: use the button on the case" (board.conf hint), if c-0 confirms that SW3
  dims the panel.
- **L2.** Software dimming of the launcher's own frame: an overlay of black at x % opacity. It saves no power and does
  not cover fullscreen apps. It suits a dark room. C7 decides.
- **Dark time.** The screen-off already paints black on backends without a backlight (AGENTS.md lock screen rule);
  nothing changes.
- **Dead backlight device.** If the board uses the vendor `hyperpixel4sq` overlay, a `gpio-backlight` on GPIO19 may
  exist that drives nothing (I). The hackberry profile should then say `APPLAUNCH_BACKLIGHT=none` so detection does
  not offer a dead on/off.

**Sound.**
- **S1 (rec).** The Bluetooth speaker as above: trust on pair, a board.conf hint naming the speaker, and the Speaker
  row when the server has a sink.
- **S2.** An I2S or USB DAC would be a hardware change; out of scope.

### 3.3 FZ-M1 tablet: sound

The hardware is there (M). Three changes:
1. **Runtime row.** The Speaker row comes from the capability, not the profile.
2. **Native Choice.** It becomes a native Choice like Brightness, and the native host then shows it (section 2.1).
3. **Volume backend chain** (C3):
   - If `pactl info` answers within 500 ms, use today's pactl. It covers PulseAudio and PipeWire-pulse, and Bluetooth
     sinks.
   - Else use ALSA: the detected card's first playback control among `Master`, `Speaker`, `PCM`, `Headphone`, through
     `amixer -c N -M sset/sget`. This reuses the soundcard service's runner.
   - System sounds get `{pulseaudio, alsa}` backends.
   - `wpctl` is not needed: pactl works against both servers.

Unknown (I, c-0):
- Is a sound server installed on the Armbian XFCE image?
- Does it keep running in the desktop unit's `PAMName=login` session after lightdm stops?
- Can the user open `/dev/snd/*` without an active seat? install.sh adds the user to `audio` (C3) and puts
  `alsa-utils` in `PC_PKGS` (`install.sh:326`).

### 3.4 Pi 5 + HyperPixel 4: brightness

**What it is.**
- The KMS overlay's shared `vc4-kms-dpi-hyperpixel.dtsi` declares `backlight: backlight { compatible =
  "gpio-backlight"; gpios = <&gpio 19 0>; }`, DPI `dpi_18bit_cpadhi_gpio0`, touch `i2c-gpio` on 10/11, and the
  init SPI on 26/27/18 (S, raspberrypi/linux `rpi-6.12.y`). That is on/off, as measured (max 1, M).
- On the Pi 5, GPIO19 offers RP1 `pwm0` as an alternate function (`drivers/pinctrl/pinctrl-rp1.c:501`, S). The
  channel number is to be read from the RP1 datasheet.
- The overlay README offers `pwm-pio` (Pi 5 only, any GPIO 0-27) (S). Its `pwm` overlay table lists BCM283x/2711
  pins only.

**Owner-supplied dimming methods, assessed.**

| Source | Method | Verified on | Pi 5 | Launcher fit |
| --- | --- | --- | --- | --- |
| [Pimoroni forum 12988](https://forums.pimoroni.com/t/hyperpixel-brightness-solved/12988) (Pi 4, 2020) → [pimoroni/hyperpixel#11](https://github.com/pimoroni/hyperpixel/issues/11) | wiringPi `gpio -g mode 19 pwm; gpio -g pwm 19 <v>`. Reported: 0-24 off, 25-90 dim, 91+ full (2018); "36 to ~160" on a Pi 4 with the latest wiringPi; it "breaks" `bl_power` off. pigpio `set_PWM_dutycycle(19, 0..255)` flicker-free on a Pi Zero W; a coil whine rises when dimmed | HyperPixel (original, 4") on Pi 4 / Pi Zero W, legacy driver era (`rpi_backlight`) | pigpio does not support the Pi 5. wiringPi gained Pi 5 PWM in 3.8 (2024-09, [WiringPi releases](https://github.com/WiringPi/WiringPi/releases)), not tested with a HyperPixel | **no for run time**: an extra package, direct register access (root, I), and it fights the kernel's `gpio-backlight` that owns GPIO19 |
| [gist NatGrayYes/eb753ae](https://gist.github.com/NatGrayYes/eb753ae20b611b3c57aeaf933f128aeb) (2023) | the same wiringPi commands, percent → 0..1023 | "Tested on Hyperpixel Round with Raspberry Pi Zero W" | as above | as above |
| [Pimoroni forum 28915](https://forums.pimoroni.com/t/pimoroni-hyperpixel-4-dimming/28915), [28879](https://forums.pimoroni.com/t/hyperpixel-brightness-control/28879) (2026) | "use PWM on GPIO19", or on/off via `/sys/class/backlight/backlight` | nothing confirmed | - | - |

**What they show.** The HyperPixel backlight hardware does dim with PWM on GPIO19, which is good evidence for the
HyperPixel 4. **Nothing here verifies it on a Pi 5.**

**The launcher-friendly form.** Use the kernel's `pwm-backlight` instead of `gpio-backlight`:
- An overlay retargets the `&backlight` node (as the deck's `waveshare-pwm-backlight.dts` does) with
  `pwms = <&rp1_pwm0 <ch> ...>` on GPIO19, or a `pwm-pio` instance (`CONFIG_PWM_RP1=y`, `CONFIG_BACKLIGHT_PWM=m`, S).
- The device then shows levels in `/sys/class/backlight/`, and the existing `sysfs:` kind plus its udev rule work
  with no sudo at run time.
- It costs an install.sh step: dtc, a config.txt line after the HyperPixel overlay, and a reboot.
- Risks: whether `panel-dpi`/`pimoroni,hyperpixel4` accepts a PWM backlight (I); a low-duty floor (the "0-24 off"
  above); the whine.

**Recommendation (C8).**
- **P1 now.** Detection maps max 1 to the existing on/off kind: On / "Off 10 s" instead of 10 dead levels. install.sh
  extends its gpio rule to whatever on/off device the profile names, if c-0 shows the brightness file is not
  user-writable.
- **P2 as an optional spike (c-4).** The overlay, a 15-minute owner test, then the hyperpixel profile gets `sysfs:`.
- **No wiringPi or pigpio.**

## 4. Capability detection design

### 4.1 Module

`ext_components/cp0_lvgl/include/cp0_capabilities.h` + `src/cp0_capabilities.c`: plain C, no LVGL, next to
`cp0_power_supply`.

```c
typedef enum { CP0_CAP_ABSENT = 0, CP0_CAP_PRESENT = 1, CP0_CAP_NOT_SET_UP = 2 } cp0_cap_state_t;
typedef enum { CP0_BL_NONE, CP0_BL_ONOFF, CP0_BL_LEVELS } cp0_cap_backlight_kind_t;

typedef struct {
    cp0_cap_state_t battery;                    /* PRESENT: cp0_power_supply_battery() finds one */
    cp0_cap_state_t backlight;
    cp0_cap_backlight_kind_t backlight_kind;
    char backlight_dir[160]; int backlight_max; int backlight_writable;
    cp0_cap_state_t sound;                      /* PRESENT: a local output, or a server sink (slow probe) */
    int sound_card; char sound_control[32];     /* local ALSA output: card index, first playback control */
    int sound_server;                           /* -1 not asked, 0 none, 1 a PulseAudio/PipeWire sink */
    char note_battery[96], note_backlight[96], note_sound[96];  /* why NOT_SET_UP (board.conf hint) */
} cp0_caps_t;

void cp0_caps_probe(cp0_caps_t *out);                     /* sysfs/procfs reads only: LVGL thread OK, < 1 ms */
void cp0_caps_probe_sound_server(cp0_caps_t *io, int ms);  /* runs pactl: a worker thread */
```

**Rules.**
- **Battery:** exactly today's `cp0_power_supply` rule (no behaviour change).
- **Backlight:**
  - `APPLAUNCH_BACKLIGHT` wins (`gpio:`, `sysfs:`, and a new `none`).
  - Else scan `/sys/class/backlight`. With several devices, take the kernel's `type` order: firmware, then platform,
    then raw.
  - `max_brightness` of 1 means ONOFF, more means LEVELS. `access(brightness, W_OK)` fills `backlight_writable`.
  - `cp0_backlight_profile` uses this instead of its fixed default path. On the deck it finds the same `backlight`
    device, so nothing changes there.
- **Sound, local:**
  - A card in `/proc/asound/cards` with a playback PCM (`/proc/asound/cardN/pcm*p`).
  - Not HDMI: the card's id or driver does not contain "hdmi", case-insensitive (`vc4hdmi*`, "HDA Intel HDMI"). The
    tablet's mixed PCH card counts.
  - `sound_control` is resolved lazily by the ALSA backend.
- **Sound, server:** `pactl list short sinks`, ignoring `auto_null`. It runs when Settings opens and when the Speaker
  page opens, never per frame.
- **Not set up:** board.conf hints, written by install.sh for known boards only:
  `APPLAUNCH_HINT_BATTERY`, `APPLAUNCH_HINT_BACKLIGHT`, `APPLAUNCH_HINT_SOUND`.
  - Examples for hackberry-cm5: `APPLAUNCH_HINT_SOUND="bluetooth:XWF-M18-M28-M38"`,
    `APPLAUNCH_HINT_BACKLIGHT="button"`.
  - A hint never makes a feature present. It only turns ABSENT into NOT_SET_UP with a reason. An unknown board
    simply shows nothing.
- **Test hooks:** `APPLAUNCH_SYS_ROOT` prefixes `/sys` and `/proc/asound` (`APPLAUNCH_POWER_SUPPLY_ROOT` keeps
  priority). `APPLAUNCH_PACTL` names the pactl binary (a fake script in tests, `/bin/false` in the render harness).
  Unit tests get fake trees: deck, Pi 5, HackBerry before and after the gauge, tablet with and without a server.

### 4.2 Consumers

| Consumer | Uses | Change |
| --- | --- | --- |
| `settings_page.cpp` tree (built at each Settings open) | sound | the Speaker row when PRESENT; an info row when NOT_SET_UP (C4); nothing when ABSENT. Brightness: ONOFF/LEVELS as today; NONE becomes an info row (hint) or nothing. Battery unchanged |
| `settings_hw_profile.hpp` | - | `SPEAKER`, `BATTERY`, `ETHERNET` leave the pizero2w profile (run-time instead). The policy macros (`SHUTDOWN`, `TIME_NO_SUDO`, `TOUCH`, `APPS`, ...) stay: they are product choices, not hardware. The default (SDL) profile keeps its bq27220 path |
| Volume backend (`cp0_audio_mixer_control`) | sound_server, card/control | the pactl → ALSA chain (section 3.3) |
| Hotkeys (`ui_global_hint.cpp:126`) | sound, backlight | no volume/brightness hint on a board without one (C11) |
| Status strip (`cp0_lvgl`) | battery | unchanged: it already reads `power_supply` itself (`cp0_ui_status_strip_widget.cpp:163-165`). No new icons (C9) |
| Apps | - | nothing required. The header is new and additive; existing `cp0_*` functions keep their signatures. Apps built at the current `LAUNCHER_REF` show the HackBerry battery in their strip as soon as the kernel device exists (same reader; I until tested). A pin move is needed only for an app that wants the new API |

### 4.3 install.sh (root, once)

- **All boards.** `usermod -aG audio` when a local card exists. `alsa-utils` (PC; the Pi images have it, I). The
  backlight udev rule covers whichever on/off or levels device the profile names.
- **hackberry-cm5.**
  - The gauge unit (B1), and `uninstall.sh` removes it.
  - The hints, and `APPLAUNCH_BACKLIGHT=none` when the only backlight is the overlay's unwired `gpio-backlight`
    (c-0 decides).
  - Optionally the WirePlumber seat-monitoring drop-in for the user.
- **pi5-hyperpixel4.** Optional, c-4: the PWM backlight overlay, after asking, as for the deck.
- **Tests.** The `test-install-board.sh` SYSROOT fixtures gain these cases.

## 5. Known and unknown

**Known:**
- Every M fact of section 2.2.
- The HackBerry gauge chip, address and bus sharing; the backlight topology; the Bluetooth speaker.
- The stock driver's properties.
- The HyperPixel overlay's GPIO19 `gpio-backlight`; RP1 `pwm0` on GPIO19.
- The launcher code paths of section 2.1.

**Unknown, measured in c-0:**
1. **HackBerry gauge:**
   - which display overlay is in use (`hackberrypi` or `hyperpixel4sq`) and the kernel version;
   - which bus carries 0x48 and 0x36;
   - whether `new_device` binds `max17040_battery` and the percent is plausible.
2. **HackBerry SW3:** does a side button change the brightness?
3. **HackBerry sound:**
   - are `pipewire`/`wireplumber` running in the launcher's session after lightdm stops;
   - does the speaker connect and play with the launcher running;
   - does volume work.
4. **Tablet:** is a sound server installed and running while the launcher runs; can the user use `amixer -c 0`
   without `audio`?
5. **Pi 5:** is `brightness` writable by the user; does `pwm-pio`/RP1 PWM on GPIO19 dim the HyperPixel 4 (c-4 only).
6. **Pi 3A+:** does a "Headphones" card exist, and so would the row appear (C2)?

## 6. Owner decisions

1. **C1 Run-time capabilities.** `cp0_capabilities` replaces the compile-time `SPEAKER`/`BATTERY`/`ETHERNET` switches
   on our boards; the product-policy macros stay. *Rec: yes.*
2. **C2 What counts as sound.** A non-HDMI ALSA playback card, or a sound-server sink (Bluetooth included);
   HDMI-only boards stay without the row. The Pi 3A+ jack would then show the row. *Rec: yes.*
3. **C3 Volume backend.** pactl when a server answers, else the ALSA mixer (amixer); system sounds PulseAudio then
   ALSA; install.sh adds `audio` and `alsa-utils`. *Rec: yes.*
4. **C4 Hardware present but not set up.** An info row with its reason (the grey-info-dot affordance of the row rule)
   only when board.conf names the expectation; otherwise hidden. Rows that do nothing (Pi 5 levels, HackBerry
   brightness) go. *Rec: yes.*
5. **C5 HackBerry battery.** B1: stock `max17040_battery` instantiated at boot by install.sh; percent and voltage,
   no charging state. *Rec: B1; B3 (DKMS) only if the charging colour matters.*
6. **C6 HackBerry speaker and Trusted.** Set `Trusted=true` after every successful pair (all boards). Name the
   built-in speaker in the hackberry hint. Show "Trusted" in Paired Devices? *Rec: trust on pair yes; showing it is
   your UI call (a small text after the name if wanted).*
7. **C7 HackBerry brightness.** L1 info row "use the button" (after c-0 confirms SW3), L2 software dimming of the
   launcher's own frame, or both. *Rec: L1 now; L2 only if you want night dimming.*
8. **C8 Pi 5 brightness.** P1 honest On/Off now; P2 a PWM-backlight overlay spike (install.sh step, reboot, Pi 5
   unverified); never wiringPi/pigpio at run time. *Rec: P1 in c-1, P2 if you want levels.*
9. **C9 Status strip.** Battery only (exists); no sound or brightness icon now. *Rec: yes.*
10. **C10 Ethernet row.** The same run-time rule (a wired NIC) could enable it. *Rec: later, not in this work.*
11. **C11 Hotkeys.** Volume and brightness keys do nothing and show no hint on a board without the capability.
    *Rec: yes.*
12. **C12 System sounds.** With sound working, the lock/unlock/select sounds start playing on the tablet and the
    HackBerry (silent on every board today). *Rec: off by default on our boards unless you want them; your call.*

## 7. Phased plan

ws = worker session. Tiers as in the savvy-flow pipeline.

| Phase | Content | Exit criteria | Owner needs to | Cost (estimate) |
| --- | --- | --- | --- | --- |
| **c-0 probe** | Controller writes a read-only probe script: `i2cdetect -l`, `ls /sys/bus/i2c/devices`, `grep dtoverlay config.txt`, `uname -r`, `modinfo max17040_battery`, `cat /proc/asound/cards`, `ls -l /sys/class/backlight/*/brightness /dev/snd`, `id`, `which pactl amixer wpctl`, `pactl info`, `systemctl --user status pipewire wireplumber` (run once from the desktop, once while the launcher runs) | section 5 items 1-6 answered in this report | run it on HackBerry, Pi 5, tablet (and `cat /proc/asound/cards` on the 3A+); press the HackBerry side button; 15 min | Controller, about 0.15 M tokens |
| **c-1 capabilities core** | `cp0_capabilities` + tests and fake roots; backlight auto-detect (on/off at max 1, `none`, writable); Speaker as native Choice, runtime; pactl→ALSA chain, system sounds backends; hotkey gating; profile macros cleaned; install.sh `audio` group, `alsa-utils`, generalized backlight rule; render scenes for sound on/off and on/off backlight (deck goldens byte-identical) | tablet: Speaker row changes the volume audibly (with and without a server); Pi 5: On/Off works; deck/3A+ unchanged; check-all 8/8 | C1-C4, C8 (P1), C9, C11, C12; 20-min test on tablet + Pi 5 + deck smoke | savvy-heavy, 2 ws, about 1.0 M tokens |
| **c-2 HackBerry** | gauge unit (B1) + uninstall + SYSROOT tests; `Trusted=true` on pair; hackberry hints and `APPLAUNCH_BACKLIGHT=none` if needed; WirePlumber drop-in if c-0 shows it is needed | battery % in strip and Settings > Battery (and in an installed app's strip); speaker paired+trusted, reconnects after reboot, plays and changes volume with the launcher running; brightness row per C7 | C5, C6, C7; 20-min test | savvy-medium, 1 ws, about 0.5 M tokens |
| **c-3 software dim** (optional) | L2: a dim layer for boards without levels (HackBerry, Pi 5 if P2 is not done) | Brightness levels visibly dim the launcher | C7 (L2); 10-min test | savvy-medium, 1 ws, about 0.5 M tokens |
| **c-4 Pi 5 PWM spike** (optional) | overlay `hyperpixel4-pwm-backlight.dts` (RP1 pwm0 or `pwm-pio` on GPIO19), install.sh step after asking, udev rule, profile `sysfs:` | levels dim the panel without flicker; off and on still work; whine acceptable | C8 (P2); reboot + 15-min test | savvy-medium, 0.5-1 ws, about 0.4 M tokens |
| **c-5 release** | README board table (sound/battery/brightness per board), release notes, ROADMAP | release published | - | docs-writer, about 0.3 M tokens |

Smallest useful slice: **c-0 + c-1 + c-2**. That is every requested item except Pi 5 levels: about 3-4 ws and
1.7 M tokens.

**Physical tests by the owner (main path only):**
1. **c-0:** the probe on three boards, and the HackBerry side button.
2. **c-1:**
   - tablet: Settings > Speaker, change the volume, hear it, Quit to desktop and back;
   - Pi 5: Brightness On / Off 10 s;
   - deck: Settings looks as before.
3. **c-2:**
   - HackBerry: battery percent next to the vendor script's voltage, unplugged and charging;
   - pair `XWF-M18-M28-M38`, reboot, play a system sound or a test tone, change the volume;
   - open an app and check its strip shows the battery.
4. **c-4:** Pi 5 levels, then reboot and check they persist.

## Sources

- Launcher code at dc9a9ab (file:line in section 2.1). Board facts: the owner's measurements of 2026-10-22 (brief).
- HackBerry CM5, the vendor repository [ZitaoTech/HackberryPiCM5](https://github.com/ZitaoTech/HackberryPiCM5):
  - `Battery_Voltage_Measure/README.md` and `get_battery_voltage.py`, `Speakers/README.md`, `Basic setup/README.md`,
    `Hardware/README.md`, `Screen/README.md`, `Keyboard/README.md`, `Troubleshoot/README.md`;
  - `Operating System/README.md` and `hackberrypi.dts`, `Operating System/RaspberryPi_OS/README.md`;
  - `Hardware/Schematic_HackberryPi_CM5_Q20.pdf`, `Speakers/MH-M18-datasheet.pdf`;
  - issues [#11](https://github.com/ZitaoTech/HackberryPiCM5/issues/11),
    [#12](https://github.com/ZitaoTech/HackberryPiCM5/issues/12),
    [#13](https://github.com/ZitaoTech/HackberryPiCM5/issues/13),
    [#22](https://github.com/ZitaoTech/HackberryPiCM5/issues/22),
    [#43](https://github.com/ZitaoTech/HackberryPiCM5/issues/43),
    [#46](https://github.com/ZitaoTech/HackberryPiCM5/issues/46),
    [#47](https://github.com/ZitaoTech/HackberryPiCM5/issues/47).
- Community HackBerry drivers and fixes: [CNflysky/hackberrypiq20](https://github.com/CNflysky/hackberrypiq20)
  (`hackberrypicm5.dts`, `hackberrypi-max17048.c`),
  [lost-down-south/hackberrypi-cm5-fix](https://github.com/lost-down-south/hackberrypi-cm5-fix).
- Raspberry Pi kernel `rpi-6.12.y`:
  - `arch/arm64/configs/bcm2712_defconfig` (`BATTERY_MAX17040=m`, `PWM_RP1=y`, `BACKLIGHT_PWM=m`);
  - `drivers/power/supply/max17040_battery.c`, `drivers/pinctrl/pinctrl-rp1.c`;
  - `arch/arm/boot/dts/overlays/vc4-kms-dpi-hyperpixel.dtsi`, `vc4-kms-dpi-hyperpixel4-overlay.dts`, `README`
    (`pwm`, `pwm-pio`).
- HyperPixel dimming:
  - [Pimoroni forum 12988](https://forums.pimoroni.com/t/hyperpixel-brightness-solved/12988),
    [pimoroni/hyperpixel#11](https://github.com/pimoroni/hyperpixel/issues/11),
    [gist NatGrayYes/eb753ae](https://gist.github.com/NatGrayYes/eb753ae20b611b3c57aeaf933f128aeb);
  - [Pimoroni forum 28915](https://forums.pimoroni.com/t/pimoroni-hyperpixel-4-dimming/28915),
    [Pimoroni forum 28879](https://forums.pimoroni.com/t/hyperpixel-brightness-control/28879);
  - [WiringPi releases](https://github.com/WiringPi/WiringPi/releases) (3.0 Pi 5, 3.8 Pi 5 PWM).
- Related reports: 021 (multi-board), 028 (x86, X12 battery), 029 (section 4 items 1-2: Battery and Speaker kept for
  this).

## 8. c-0 probe results (2026-10-22, v0.8.3 installed) and decisions taken

Owner decision: "go with the recommendations" (C1-C12 as recommended), with the changes below from the probe.

**Measured (read-only, M):**
- **HackBerry** (kernel 6.18.50+rpt-rpi-2712, overlay `hackberrypicm5`, user in `i2c`, `audio`, `video`):
  - The overlay already creates the gauge client `15-0036` on `i2c-15` (`i2c-gpio@0`) with compatible
    `hackberrypi,max17048-battery`. It is NOT the stock `maxim,max17048`, so the stock `max17040_battery` module (present,
    not loaded) never binds; `/sys/class/power_supply` stays empty. **B1 (instantiate with `new_device`) cannot work: the
    address is taken by the overlay's client.**
  - A direct i2c-dev read as the normal user works with no root: VCELL 4.188 V, SOC 100.3 %, VERSION 0x0012
    (registers 0x02, 0x04, 0x08 of the MAX17048). The MAX17048 also has CRATE (0x16, signed, 0.208 %/h per LSB) for the
    charging direction.
  - The case speaker `XWF-M18-M28-M38` (9E:0C:81:E6:5D:1A) is already **Paired: yes, Trusted: yes**, not Connected;
    PipeWire + WirePlumber + pipewire-pulse are active with only `auto_null` (the BT sink appears when it connects). No
    backlight device; ALSA has only vc4-hdmi.
- **Pi 5 + HyperPixel 4** (overlay `vc4-kms-dpi-hyperpixel4,rotate=90`): `/sys/class/backlight/backlight/brightness` is
  `rw-rw-r-- root video` (the user is in `video`, no root), max 1. PipeWire active, `wpctl` present, `pactl` NOT installed,
  `amixer` present; ALSA only vc4-hdmi.
- **FZ-M1 tablet:** `pactl` talks to a PulseAudio-compatible server, sink `alsa_output.pci-0000_00_1f.3.analog-stereo`;
  `intel_backlight` writable by `video`; `max17040_battery.ko` present in the x86 kernel too.

**Changes to the plan:**
- **C5 becomes B4: the launcher reads the MAX17048 itself over i2c-dev** (bus and address from board.conf, e.g.
  `APPLAUNCH_GAUGE=max17048:/dev/i2c-15:0x36`): percent from SOC, voltage from VCELL, charging from CRATE, as a
  `cp0_power_supply` source. No root, no reboot, no config.txt line, no install.sh unit, no DKMS. The sysfs path stays
  the first choice when a kernel driver exists.
- **C6:** the trust half is already true on this unit; setting Trusted after every pair is still the right default for
  other boards.
- **Volume backend (C3):** `pactl` on the tablet and the HackBerry; the Pi 5 has `wpctl` but no `pactl`, and no sound
  hardware anyway (HDMI only), so no Speaker row there.
