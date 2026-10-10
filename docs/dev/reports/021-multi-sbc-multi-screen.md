# 021 - Launcher on several boards and capacitive screens (feasibility)

Date 2026-10-10. Read-only study: code analysis plus web research. No product code, no deck access, no commit.
Marks: **V** = verified (file:line in the repos, or a page or source file fetched today). **I** = inferred (reasoning or a
secondary source). A board probe must confirm it before anyone builds on it.
It builds on the parked x86 study (`git show docs-later-x86:docs/dev/later/x86-fz-m1/x86-port-options.md`, called "x86
report" below) and does not repeat its findings.

## Executive summary

**Recommendation.** Use **one aarch64 build for all five boards**. All of them, the Walnut Pi included, are 64-bit Cortex-A
boards, so **no armhf build is needed**. The work is phased:
- probe the boards first;
- add a runtime board profile (`board.conf`) with touch found by capability and a calibration matrix;
- support KMS through the kernel's `/dev/fb*` emulation, not a new DRM backend;
- then make the layout fit other screen sizes;
- port the Walnut Pi last.

**Cheapest win:** the Pi 3A+ with the current Waveshare 2.8" DPI panel. It probably needs no code at all: run
`install.sh --force` on Raspberry Pi OS Lite 64-bit.

| Board | Arch / OS image | Supported from | Display path | Confidence |
|---|---|---|---|---|
| Pi Zero 2 W (deck) | arm64, Raspberry Pi OS trixie | today | legacy firmware fbdev | V (running) |
| Pi 3A+ | arm64, Raspberry Pi OS trixie | phase 0 test, phase 1 official | legacy fbdev (same VideoCore IV as Zero 2 W) | high (I) |
| Pi 4 | arm64, Raspberry Pi OS trixie | phase 1 if legacy DPI still boots on trixie, else phase 2 | legacy fbdev, or KMS fbdev emulation (16 bpp) | medium (I) |
| Pi 5 | arm64, 16 KB-page kernel | phase 2 | KMS only, fbdev emulation (RP1 DPI/DSI 32 bpp) | medium (I) |
| Walnut Pi ZeroW (Allwinner H618) | arm64, vendor Debian 12, BSP kernel 6.1.31 | phase 4 | HDMI or SPI LCD only, display stack unknown | low-medium |

**Biggest risks:**
1. **(V) The Walnut Pi image has glibc 2.36.** The launcher and every app need `GLIBC_2.38`, so none of them run on it
   until a Debian 12 baseline build exists.
2. **(V code, I runtime) The Pi 5's default 16 KB-page kernel probably breaks every stock compat app.** The vfb shim maps
   the app's framebuffer at file offset 4096.
3. **(V) The full-screen apps hard-code the deck.** Mesh Hop and viz1090 assume a 640x480 landscape `/dev/fb0` and the
   deck's touch axes.
4. **(I) KMS fbdev emulation details:** 16 bpp on vc4, deferred I/O on RP1, the fb number on the Pi 5, and a panel that
   stays portrait.
5. **UX decisions for non-640x480 screens.** Small, square and large screens need the Controller and the user to decide
   the layout.

## Sources and baseline facts

- **Deck baseline (V):**
  - `/dev/fb0` is 640x480, 32 bpp, landscape, made by the legacy firmware (`display_rotate=1` in
    `pizero2w/config.txt.snippet`).
  - The launcher is selected with `APPLAUNCH_DISPLAY=dpi-scaled`, and touch is configured by environment variables
    (`pizero2w/APPLaunch.service`).
  - The deck has 414 MB of usable RAM.
- **(V) Every binary needs `GLIBC_2.38`.** Measured today by scanning the ELF version strings of
  `projects/APPLaunch/dist/M5CardputerZero-APPLaunch`, the lanscan, wifi-survey, mesh-hop and meshzero binaries, and
  viz1090 and readsb.
- **(V) The cross sysroot is Debian 13 trixie arm64 (glibc 2.41).**
  - It is downloaded from the upstream release `v0.0.7` (`projects/APPLaunch/SConstruct:82-86`;
    `SDK/github_source/static_lib_v0.0.7/usr/lib/os-release`).
  - The toolchain is `aarch64-linux-gnu-` (`linux_x86_cross_cp0_config_defaults.mk:7`), with NEON on (`:96-97`).
- **(V) Reused from the x86 report:**
  - the display backend already reads the panel size at runtime;
  - touch is found by the name "Goodix";
  - the backlight path is hard-coded;
  - battery support is bq27-only;
  - the Settings profile is chosen at compile time;
  - the registry hard-codes `_arm64.deb`;
  - a KMS dumb-buffer variant is "about 150 lines";
  - the native layout constants were tuned for 640x480 at 286 ppi.

---

## 1. Per-board matrix

| | Pi Zero 2 W | Pi 3A+ | Pi 4 B | Pi 5 | Walnut Pi ZeroW |
|---|---|---|---|---|---|
| SoC / CPU | RP3A0, 4x Cortex-A53 1 GHz (V [1]) | BCM2837B0, 4x A53 1.4 GHz (V [2]) | BCM2711, 4x A72 1.8 GHz (V [3]) | BCM2712, 4x A76 2.4 GHz + RP1 I/O chip (V [4]) | Allwinner H618, 4x A53 1.5 GHz, Mali-G31 MP2 (V [5][6]) |
| ISA | ARMv8, aarch64 | ARMv8, aarch64 | ARMv8, aarch64 | ARMv8.2, aarch64 | ARMv8, aarch64 ("64位") (V [6]) |
| OS image to use | Raspberry Pi OS Lite 64-bit trixie (V, deck) | same, the 3A+ is listed (V [7]) | same (V [7]) | same, but the default kernel uses **16 KB pages** (V [8]) | vendor "WalnutPi OS", Debian 12 bookworm, kernel **6.1.31** BSP, server and desktop images (V [9] for the 1B; I that the ZeroW uses the same "gen 1" image [10]) |
| Debian / glibc | 13 / 2.41 | 13 / 2.41 | 13 / 2.41 | 13 / 2.41 | **12 / 2.36** (V [11]): the current binaries do not run |
| Current kernel | Raspberry Pi 6.18 (V [7]) | 6.18 | 6.18 | 6.18 (`kernel_2712.img`) | 6.1.31 BSP |
| RAM | 512 MB (414 MB usable on the deck) | 512 MB (V [2]) | 1/2/3/4/8 GB (V [3]); user's unit unknown | 1/2/4/8/16 GB (V [4]); user's unit unknown | 1/2/4 GB LPDDR4 (V [6]); user's unit unknown |
| Display interfaces | mini-HDMI, DPI on GPIO; **no DSI** (V [1]) | HDMI, DSI (15-pin), DPI (V [2]) | 2x micro-HDMI, 2-lane DSI, DPI (V [3]) | 2x micro-HDMI, 2x 4-lane MIPI DSI/CSI, DPI through RP1 (V [4]) | micro-HDMI, **SPI LCD** only. No DSI, no DPI (V [12]) |
| Display stack | legacy firmware fbdev (deck, V). KMS also possible | legacy firmware fbdev, same VideoCore IV path as the Zero 2 W (I, high). KMS possible | KMS is the OS default. Legacy firmware fbdev probably still works without `vc4-kms-v3d` (I, medium; Waveshare ships `waveshare-28dpi-4b`, V snippet) | **KMS only**, no legacy firmware display (I; secondary [13]). DPI and DSI are Linux RP1 drivers | Allwinner BSP: whether it is fbdev or DRM is **unknown**, the probe will tell. The vendor tool `set-lcd lcd35-st7796` drives its 3.5" SPI panel, and HDMI wins when plugged (V [14]) |
| fbdev depth under KMS | vc4 fbdev emulation is **16 bpp** (V [15] `vc4_drv.c:431`) | 16 bpp | 16 bpp | HDMI (vc4) 16 bpp; RP1 DPI/DSI **32 bpp, shadow buffer + deferred I/O** (V [15] `rp1_dpi.c:447`, `rp1_dsi.c:321`) | unknown |
| Wi-Fi / BT | 2.4 GHz b/g/n, BT 4.2 (V [1]) | 2.4/5 GHz ac, BT 4.2 (V [2]) | 2.4/5 GHz ac, BT 5.0 (V [3]) | dual-band ac, BT 5.0 (V [4]) | dual-band, BT 5.0, chip not named (V [6]); the BlueZ/NetworkManager setup is unknown |
| Wired / audio / RTC | none / none / none | none / 3.5 mm jack / none | GbE / 3.5 mm jack / none | GbE / no jack / **RTC** (V [4]) | 100M Ethernet and audio on an FPC adapter (V [6]) / none |
| Backlight on GPIO18 | PWM0 on the bcm2835 pinmux (`dtoverlay=pwm,pin=18,func=2`) plus our `waveshare-pwm-backlight.dts` (V) | same (I, high). **Analogue audio uses both PWM channels, so the PWM backlight kills the jack** (V [16]) | same as the 3A+ (I) | GPIO18 is **RP1 PWM0 channel 2**, on pwmchip2 (I, secondary [17]). Our overlay's `pwms = <&pwm 0 ...>` (`pizero2w/waveshare-pwm-backlight.dts`) needs a Pi 5 variant | not applicable with HDMI (no panel backlight control). H616/H618 PWM pins differ from the Pi's (I) |
| Boot config | `/boot/firmware/config.txt` | same | same | same | `/boot/config.txt` in a vendor format (`overlays=`, prefix `sun50i-h616`) (V [18]) |

**Walnut Pi ZeroW identification.** It is the WalnutPi (核桃派) "ZeroW", from WalnutPi in China:
- Allwinner H618, quad Cortex-A53 at 1.5 GHz, Mali-G31 MP2;
- 1, 2 or 4 GB LPDDR4;
- dual-band Wi-Fi and BT 5.0, micro-HDMI 2.0a, one USB-C;
- a Pi-compatible 40-pin header, on a 65x30 mm board (Pi Zero size);
- an FPC socket with 2x USB 2.0, audio and 100M Ethernet (V [5][6][12]).

Confidence:
- **High (about 90 %)** for the identity and hardware: the walnutpi.com product list, the wiki parameter table and the
  vendor GitHub README all agree.
- **Medium** for the software: the latest gen-1 release asset is
  `2026-5-20_V2.6.0_WalnutPi-1B_6.1.31_debian12_{server,desktop}` (V [9]). The wiki groups 1B, ZeroW, CM1 and BOX under
  one gen-1 tutorial, so I infer that the ZeroW runs the same image (I).
- Armbian has no Walnut Pi image (I [19]).
- It is **not** an armhf board.

**Conclusion on 32-bit (V/I).** All five boards run the 64-bit image the deck already uses (four of them) or a vendor
arm64 image (Walnut). No armhf build is needed for this hardware set. armhf would only matter for Pi 1, Pi 2 or Pi
Zero (v1) boards, or for a user who insists on a 32-bit image.

## 2. Display layer

### What the launcher needs today (V)

- **The dpi-scaled backend.** It is chosen when `APPLAUNCH_DISPLAY=dpi-scaled` (`cp0_lvgl_freambuffer.c:57-63`).
  - It uses `open(/dev/fb0)` (or `LV_LINUX_FBDEV_DEVICE`), `FBIOGET_VSCREENINFO`, `FBIOGET_FSCREENINFO` and one
    `mmap` of `line_length * yres_virtual` (`cp0_lvgl_dpi_scaled.c:601-630`).
  - It accepts 16 or 32 bpp only (`:617`).
  - It makes no other ioctl: no pan, no blank, no vsync.
- **Software rotation.** It is set with `APPLAUNCH_ROTATE`, and is 90 by default when the fb is portrait (`:633-637`).
  - Only rotation 0 has a `memcpy` fast path (`:160-167`).
  - The external-app blit has a fast path only for rotation 0 at 32 bpp (`:513-537`).
  - Every other case writes pixel by pixel through `put_pixel` (`:93-98`).
- **Two LVGL displays share one fb.**
  - The native display covers the full landscape panel (`:664`).
  - The compat display is 320x170 (`:37-38`), drawn at an integer scale `min(lw/320, lh/170)`, centred horizontally
    and top-aligned (`:639-645`).
- **An LVGL DRM path also exists, but outside dpi-scaled.** `cp0_lvgl_freambuffer.c:64-100` uses `LV_USE_LINUX_DRM`,
  which the cross config does not enable (`linux_x86_cross_cp0_config_defaults.mk:92` turns on fbdev only).

The full-screen apps write the fb themselves:
- **Mesh Hop:** `kScreenW/H = 640x480` (`mesh-hop/src/main/ui/platform.hpp:23-24`), opens `/dev/fb0`
  (`platform.cpp:110`), and has no rotation.
- **viz1090:** runs with `--screensize 640 480` (`run_viz1090.sh:32`). Its bridge centres the frame in the fb
  (`viz_fb_shim.c:161-163`).
- **Stock compat apps:** go through the 320x170 vfb shim and never touch the fb.

### Options for KMS boards (Pi 4, Pi 5, and the Zero 2 W or 3A+ on a KMS image)

**(a) Keep `/dev/fbN` through DRM fbdev emulation. Recommended first.**
- **What it is.** The KMS kernels keep `/dev/fb0` for the console.
  - On vc4 (Pi 0-4, and the Pi 5's HDMI) the emulated fb is DMA-backed and scanned out directly, at **16 bpp RGB565**
    (V [15]).
  - On the Pi 5's RP1 DPI and DSI it is 32 bpp, through `drm_fbdev_ttm`: a shadow buffer flushed by deferred I/O
    (V [15]; I that frames are paced by the deferred-I/O delay).
- **The launcher already copes with 16 bpp:** it picks `LV_COLOR_FORMAT_RGB565` (`:650`). The slow parts are the
  XRGB to RGB565 conversion for vfb apps (`:539-552`) and the rotated paths. Both need row fast paths, about 80 lines.
- **Rotation.** Under KMS, `display_rotate` is gone. The Waveshare wiki rotates with `video=DPI-1:480x640M@60,rotate=90`
  on the cmdline (V [20]). That sets the panel orientation for fbcon and compositors. I expect `/dev/fb0` to stay
  480x640 portrait (I). The launcher then rotates in software, which already works.
  - The full-screen apps do not rotate, so they would draw wrongly: Mesh Hop clips (`platform.cpp` flush clamps to
    `fb_w`).
- **The Pi 5 has several DRM devices** (vc4 for HDMI, RP1 for DPI and DSI). Each has its own fbdev, so "fb0" can be the
  wrong panel. Choose by `/sys/class/graphics/fb*/name` (I).
- **Size estimate: 200-400 lines in total:**
  - fb selection by name, 16 bpp fast paths, rotated fast paths: about 150-250 lines;
  - per-board boot templates;
  - an `APPLAUNCH_FB` and `APPLAUNCH_ROTATE` export to apps.
- **Why it is the cheapest option.** It keeps the vfb, Mesh Hop and viz1090 working without a DRM-master hand-over.

**(b) Native DRM/KMS dumb-buffer backend in cp0_lvgl.**
- **How it maps onto today's code.** dpi-scaled only needs a pointer, stride, bpp and size for a linear buffer. Move
  "get a buffer" behind a small `fb_target` with two providers (fbdev and kms-dumb). The 2x compat logic, the overlay
  rectangle, external blits and touch mapping stay exactly as they are.
- **The KMS provider, with raw kernel ioctls:** `GETRESOURCES`, `GETCONNECTOR`, `GETENCODER`, `CREATE_DUMB`,
  `MAP_DUMB`, `ADDFB2`, `SETCRTC`, plus `DIRTYFB` for SPI/tinydrm panels.
  - Use the `<drm/drm_mode.h>` uapi headers already in the sysroot. **libdrm headers are not in the sysroot**: only
    `libdrm.so.2` is there (V).
  - Size: **250-350 lines.**
- **The hard part is DRM master.** While the launcher holds master, an X-Fullscreen app that writes `/dev/fb0` (Mesh
  Hop, viz1090) is not shown. The launcher must drop master and restore the fbdev mode around such apps, and do the same
  on VT switch: **+100-150 lines** and a real test matrix.
- LVGL's `lv_linux_drm.c` (1,219 lines, V) creates one LVGL display. Ours needs two displays on one buffer, so use it
  only as a reference.
- **When to do it:** only if the probe shows (a) is not good enough, for example deferred-I/O frame pacing on the
  Pi 5's DPI, or tearing.

**(c) SDL2 kmsdrm. Not recommended.**
- The x86 report found that the cp0 build switches every service to SDL simulator stubs when SDL is the display. A
  build split would be needed.
- SDL kmsdrm needs GBM/EGL (Mesa on the Pi; unknown on the Walnut's Mali BSP), adds memory, and buys nothing over (a)
  or (b).

### Resolution-agnostic UI

**Screens to target** (V [21], Pi Hut catalogue; V [16], panel overlays). Resolutions are given in the panel's native
orientation; "portrait" means the launcher rotates it.

| Class | Typical panels | Native res | Touch |
|---|---|---|---|
| Small DPI/DSI | Waveshare 2.8" DPI (current, V); Waveshare 2.8" DSI | 480x640 portrait | Goodix (V [20], [16]) |
| 3.5" | Waveshare 3.5" DSI 640x480; 3.5" capacitive 320x480; Walnut 3.5" SPI ST7796 | 640x480; 480x320 | FT/Goodix; Walnut's is **resistive** XPT2046 (V [14]) |
| 4-5" | HyperPixel 4.0 (480x800 portrait), HyperPixel 4 Square 720x720, Waveshare 4" 720x720, Waveshare 4.3"/5" DSI 800x480, 5" 720x1280, 5" HDMI 800x480 / 1024x600 | 800x480 most common | Goodix GT911 (HyperPixel 4, Waveshare DSI), FT5406 (Square, 4.3" DSI) (V [16]) |
| 7" | Official Touch Display v1 800x480 (DSI); **Touch Display 2** 720x1280 portrait (also 5", and a 10" listed); Waveshare 7" 1024x600 (DSI or HDMI+USB) | 800x480, 720x1280, 1024x600 | FT5406 (v1), **Goodix GT911** (TD2), USB HID (HDMI ones) (V [16]) |
| 8-10" | Waveshare 8"/10.1" 1280x800 (DSI/HDMI), 7.9" 400x1280 bar | 1280x800 | Goodix / USB HID |

**What today's code does with them.** This uses the formula at `cp0_lvgl_dpi_scaled.c:639-645` and the home grid
constants at `native_ui.cpp:45-48` (V calculation):

| Landscape panel | Compat scale | Compat window | Toolbar height | Home tile width, rows visible |
|---|---|---|---|---|
| 640x480 (deck) | 2 | 640x340 | 140 | 192 px, 2 |
| 480x320 | 1 | 320x170 at x=80 | 150 | 138 px (128 px icon barely fits), 1.2 |
| 800x480 | 2 | 640x340 at x=80 | 140 | 245, 2 |
| 1024x600 | 3 | 960x510 at x=32 | 90 | 320, 2.6 |
| 1280x720 | 4 | 1280x680 | **40 (too thin to tap)** | 405, 3.2 |
| 1280x800 | 4 | 1280x680 | 120 | 405, 3.6 |
| 720x720 | 2 | 640x340 | **380** | 218, 3.2 |
| 1280x400 (7.9" bar) | 2 | 640x340 at x=320 | 60 | 405, 1.6 |

**Proposed scaling model.**
- **Compat (320x170): keep the logical canvas with integer scale and letterbox.** It is implemented and cheap. Add two
  rules:
  - choose the scale that leaves a toolbar of at least about 9 mm (needs the panel ppi from the profile, or a pixel
    floor such as 80 px);
  - cap the toolbar's height and centre the window vertically on square panels.
  - Non-integer scaling (1.5x on 480x320) costs a proper resampler and looks uneven: it is a design decision.
- **Native screens: density-independent sizes.** One `ui_metrics` gives `dp(x) = round(x * ppi / 286)`, clamped. It is
  derived from the profile's panel diagonal or from DRM `mm_width`, otherwise from a resolution class. The grid column
  count follows from the width: `cols = clamp(width_dp / 200, 2, 5)`. Fonts snap to the compiled Montserrat sizes 8-48,
  and FreeType is available (V config).
- **Full-screen apps: one "640x480 logical surface" contract, not 2 apps x N screens.** The launcher would give
  X-Fullscreen apps a 640x480 vfb, the way compat apps get a 320x170 one, and would scale, letterbox and rotate it.
  Panels smaller than 640x480 (480x320) cannot show such apps without downscaling. They are either marked unsupported
  (a `.desktop` key such as `X-MinSize=640x480`, hiding or greying the tile) or the app becomes adaptive.

**Effort for adaptive native screens (V counts):**
- **Literal constants:**
  - `native_ui.cpp`: about 35 layout sites (`kBarH 56`, `kCols 3`, `kPad 16`, `kTileH 188`, `kStatusW 320`, icon
    128x128, radius 28 and 18, fonts 24 and 32, offsets 20/10/14/-12, toolbar pad 10 and column 8);
  - `ui_app_calculator.cpp:204-209`: about 20 (pad 10, top bar 44, display 104, gap 6, 6x4 grid, fonts 28-48);
  - `native_screensaver.cpp:25-27`: about 8 (180 px clock);
  - `launcher_toast.cpp:22`: 8 (already scales from a 320 design);
  - lock screen `ui_screensaver.cpp`: about 7 (uses the panel size).
- **Already adaptive:** the terminal (`ui_app_st.cpp:88-92`, font set by `APPLAUNCH_TERM_FONT`) and the SSH form (a
  compat page).
- Settings and stock pages are 320x170 by design and are scaled.
- **Estimate:**
  - 150-300 changed lines once the design for each size class is decided;
  - +300-500 lines for the full-screen 640x480 surface (blit with scale, rotate and letterbox; touch mapping inside the
    letterbox; per-app opt-in).
  - Mesh Hop alone has about 38 literal 640/480 sites (`mesh-hop/src/main/ui/app.cpp`, V grep): making it truly
    adaptive is its own apps task.

## 3. Touch layer

**Today (V):**
- **Discovery:** `find_touch_device` takes the first `event*` whose name contains **"Goodix"**
  (`cp0_lvgl_dpi_scaled.c:195-220`, the match is at `:212`), unless `APPLAUNCH_TOUCH_DEVICE` is set.
- **Coordinates:** it reads `ABS_MT_POSITION_X/Y` or `ABS_X/Y`, with `BTN_TOUCH` or `ABS_MT_TRACKING_ID` as the contact
  (`:225-235`). Ranges come from `EVIOCGABS` (`:448-454`).
- **Mapping:** normalise, then optional `SWAP_XY`, `INVERT_X` and `INVERT_Y` from the environment (`:237-243`, `:455-457`),
  then into physical fb pixels, then through the display rotation (`phys_to_land`, `:248`). Touch axes are therefore
  expressed relative to the physical fb, and rotation is a separate step. That is a good split to keep.
- **The apps are already different:**
  - viz1090 and Mesh Hop **score by capability**: they require MT X/Y, reject anything with `KEY_A`, and prefer a name
    with "goodix" or "touchscreen" (`viz_fb_shim.c:243-258`; `mesh-hop/.../platform.cpp:60-72`), as decided on
    2026-10-04.
  - Both fall back to **the deck's axes when the environment is missing** (swap=1, inv_y=1: `platform.cpp:281-283`,
    `viz_fb_shim.c:305-307`).
  - Both require MT, so a resistive (single-touch) panel gives them no touch.

**Common controllers and how they appear:**

| Controller | Seen on | Kernel driver and evdev | Axis fixes available |
|---|---|---|---|
| Goodix GT911/GT9xx (I2C) | Waveshare 2.8" DPI (current), Waveshare DSI family, Touch Display 2, HyperPixel 4.0 (V [16], [20]) | `goodix`, MT; name contains "Goodix" (I, typical) | DT params `touchscreen-inverted-x/y`, `-swapped-x-y` or `invx/invy/swapxy` on the overlays (V [16]) |
| FocalTech FT5406/FT5x06 (I2C) | Official 7" v1, Waveshare 4.3" DSI, HyperPixel 4 Square (V [16]) | KMS: `edt-ft5x06`, MT. Legacy firmware: the firmware polls the chip and `rpi-ft5406` exposes it (V [16]). **Name is not "Goodix"**, so today's launcher finds no touch (V code) | `invx/invy/swapxy/sizex/sizey` (V [16]) |
| ILITEK (I2C) | some DSI panels | `ilitek-ts-i2c`, `ili251x` overlays exist (V [16]) | DT |
| USB HID multitouch | HDMI screens (Waveshare 5"/7"/10") and the Walnut with HDMI | `hid-multitouch`, MT, vendor names. Some also expose a mouse interface (I) | udev matrix only |
| XPT2046/ADS7846 (SPI, resistive) | 3.5" SPI LCDs, the Walnut 3.5" (V [14]) | `ads7846`, ABS_X/Y + pressure, **no MT** | needs calibration (raw range is not the screen edge) |

**Automatic selection (proposed).** Pick by capability, never by name, through libudev, which is already linked
(`main/SConstruct:157` `udev`):
1. Prefer `ID_INPUT_TOUCHSCREEN=1` from udev's `input_id` builtin (I, it uses the same heuristics).
2. Otherwise score: EV_ABS with MT X/Y or ABS X/Y, plus `BTN_TOUCH` or `TRACKING_ID`, plus `INPUT_PROP_DIRECT`. Reject
   `INPUT_PROP_POINTER` (touchpads), `KEY_A` (keyboards) and BTN_LEFT-only devices (mice).
3. Hot-plug with the same inotify watcher that task 012 F1 phase 0 adds for keyboards.

Size: about 120 lines in cp0_lvgl, shared by the keyboard presence helper.

**Calibration.**
- **The udev matrix does not apply by itself.** `LIBINPUT_CALIBRATION_MATRIX` is only applied by libinput consumers,
  and the launcher reads raw evdev (I).
- **Adopt its format anyway: six floats, row-major 2x3, in normalised units.**
  - Waveshare already documents it for its panels (`"0 -1 1 1 0 0"` for 90°, V [20]).
  - The launcher reads the property from udev or from the profile and applies it in `touch_update`.
  - The existing SWAP/INVERT values map to matrices exactly, so it is backward compatible.
- **The launcher must always export resolved values to children:** `APPLAUNCH_TOUCH_DEVICE`, `APPLAUNCH_TOUCH_MATRIX`,
  and the three legacy SWAP/INVERT variables. The apps' deck defaults then never apply by accident.
- **Where possible, set the axes in the kernel too**, through the DT overlay parameters, so that every consumer agrees.

**Settings wizard (UX decision for the Controller):**
- **Touch calibration:** tap three or four targets. The raw samples are fitted to an affine matrix, so it works even
  while the axes are still wrong.
- **Keyboard path:** an orientation quick-pick (swap x invert-x x invert-y = 8 choices) plus panel rotation.
- **Storage:** per user in `config.json`, overriding `board.conf`, so no root is needed.
- **Size:** 250-400 lines after the design.

## 4. Hardware profile architecture

**Today (V):**
- `APPLAUNCH_HW=pizero2w` adds `-DAPPLAUNCH_HW_PIZERO2W` (`main/SConstruct:108-109`).
- 25 macro lines in `settings_hw_profile.hpp` gate **12 use sites**: 10 in `settings_page.cpp` (`:243`, `:636-784`:
  Speaker, Ethernet, ExtPort, Battery, Touch, Apps, Developer, Software update, Shutdown), 1 in
  `builtin_app_registry.cpp:88`, and 1 in `settings_rtc_page.cpp:9`.
- Interface names are fixed:
  - `eth0` in `settings_ethernet_controller.cpp:49,55` and `cp0_lvgl_osinfo.cpp:258,279`;
  - `wlan0` in `cp0_statusbar.c:95` and `cp0_lvgl_network.cpp:522`.
- The backlight is fixed at `/sys/class/backlight/backlight/*` (`cp0_lvgl_settings.cpp:440-460`).
- `install.sh` refuses anything that is not a "Zero 2" without `--force` (`:54-59`), requires aarch64 (`:60`), and
  writes Pi `config.txt` lines (`:117-143`).

**Runtime detection.**
- **Board id**, in this order:
  1. `/proc/device-tree/compatible` (first string, for example `raspberrypi,3-model-a-plus`, `raspberrypi,5-model-b`,
     `allwinner,sun50i-h616`);
  2. `/proc/device-tree/model`;
  3. on x86, `/sys/class/dmi/id/product_name` (this keeps the parked FZ-M1 plan consistent).
- **Display probe:**
  - `/sys/class/graphics/fb*/{name,virtual_size,bits_per_pixel}`;
  - `/sys/class/drm/card*-*/{status,modes}`;
  - the panel size in mm, from DRM or EDID, for ppi.
- **Touch and keyboard probes** as in section 3.
- **Capabilities:**
  - Wi-Fi: `/sys/class/net/*/wireless`;
  - Bluetooth: `/sys/class/bluetooth/hci*`;
  - Ethernet: a non-wireless netdev with a `device` link;
  - battery: `power_supply` with `TYPE=Battery` (from the x86 report);
  - speaker: an ALSA playback card;
  - RTC: `/dev/rtc0`;
  - backlight: any entry in `/sys/class/backlight`.

**Profile file.**
- **Location and precedence:** `/etc/applaunch/board.conf` (key = value, readable by sh and by a roughly 100-line C
  parser). Values are resolved in this order:
  1. the environment variable, so today's service still works;
  2. the per-user `config.json` (touch matrix, rotation);
  3. `/etc/applaunch/board.conf`;
  4. the template for the detected board, `/usr/share/APPLaunch/boards/<id>.conf`;
  5. autodetection.
- **Example content:**

```
board = rpi3-aplus                 # detected; templates: rpi-zero2w, rpi3-aplus, rpi4, rpi5, walnutpi-zerow, x86-fzm1
display.backend = fbdev            # fbdev | kms (only if option b is ever built)
display.device = auto              # auto = fb chosen by /sys/class/graphics/fb*/name, else /dev/fb0
display.rotate = auto              # 0/90/180/270, done by the launcher
display.ppi = 286                  # optional; native-screen density
display.compat_scale = auto        # auto | 1..6 (see the toolbar-minimum rule)
touch.device = auto                # auto = udev ID_INPUT_TOUCHSCREEN / capability score
touch.matrix = 0 1 0 -1 0 1        # LIBINPUT_CALIBRATION_MATRIX format (deck = swap + invert Y)
backlight = auto                   # auto = first /sys/class/backlight/*; none -> black screen + FBIOBLANK
caps.ethernet = auto               # auto | yes | no, same for wifi, bt, battery, speaker, rtc
```

**Compile-time vs runtime.**
- **Stays compile-time:**
  - CPU arch and NEON flags, toolchain and sysroot (so the glibc baseline);
  - the product family, CardputerZero stock or "deck": Cardputer-only pages (ExtPort, ADB, Software update), the
    Cardputer keymap default, the help-key wording, the upstream updater;
  - the cp0 service set (`CONFIG_CP0_LVGL_INIT_*`).
- **Becomes runtime:**
  - the board, the display device and rotation, the scale and ppi;
  - the touch device and matrix, the backlight method;
  - the Speaker, Ethernet, Battery and RTC items: about 4 `#if` blocks become `if (caps.x)`, about 60-100 lines plus a
    120-line caps module;
  - interface names.
- **Keep `APPLAUNCH_HW` as the family switch.** Rename it to `deck` later if the user wants.

**Bundle, install and build.**
- **build.sh:**
  - add `--arch arm64|armhf|amd64`: it is aarch64-only today (`build.sh:22-25`, `:53` for the vfb shim's CC);
  - parametrise the aarch64 paths in `main/SConstruct:142,177,200-201`;
  - output `deck-bundle-<arch>.tar.gz`. Keep the `pizero2w-bundle.tar.gz` name as an alias for release continuity
    (a user decision).
- **install.sh:**
  - a model-to-template table replaces the "Zero 2" check, and the arch check compares the bundle with
    `dpkg --print-architecture`;
  - boot config per template:
    - Pi legacy: today's lines;
    - Pi KMS: `vc4-kms-v3d` plus the panel overlay, and the Pi 5 PWM variant or `vc4-kms-dpi-generic,backlight-pwm`
      (V [16]);
    - Walnut and x86: no boot edits (`--no-boot-config` already exists, `:10`);
  - writes `/etc/applaunch/board.conf`;
  - the service unit keeps only `APPLAUNCH_DISPLAY`. The touch and rotation environment variables move to the profile.
- **Apps repo (consistent with x86 report decision 7):**
  - `app.json` gets `"arch": ["arm64"]` (the default);
  - `make_registry.py` stops hard-coding `_arm64.deb` (`tools/make_registry.py:90`), writes `registry.json` (arm64,
    unchanged for existing decks) plus `registry-<arch>.json`, and keeps `device_targets` (`:150`);
  - `build_deb.py --arch` already exists (`:92`);
  - Settings > Apps derives the registry URL (`apps_backend.cpp:332`) per `dpkg --print-architecture`: about 20 lines.
- **Add a libc floor to every package.** `build_deb.py` writes no `libc6` dependency (`:120-130`), and lanscan's
  `depends` is empty. Today a Debian 12 board (the Walnut) would **install the package and then fail at run time** with
  "GLIBC_2.38 not found". Adding `libc6 (>= 2.38)` to Depends makes dpkg refuse cleanly: a 5-line tooling change worth
  doing now.

**armhf, if it were ever wanted.**
- **Today's binaries:** every app binary is aarch64 ELF needing GLIBC_2.38 (V). On armhf, dpkg refuses an `arm64`
  package. Nothing breaks silently, and nothing installs.
- **What armhf would cost:**
  1. **An armhf sysroot.** There is none upstream: build one from Raspberry Pi OS or Debian armhf trixie with
     libinput, xkbcommon, freetype, glib, dbus and udev -dev packages, minding the 64-bit `time_t` ABI of trixie armhf
     (I). Then the build.sh and SConstruct parametrisation, and `-mfpu=neon-vfpv4` for the LVGL NEON code. This is M:
     1-2 runs.
  2. **One rebuild per app:**
     - lanscan, wifi-survey, mesh-hop and meshzero: S each through the SDK;
     - viz1090 and readsb: natively on a 32-bit Pi through `build_on_pi.sh`, which readsb supports (I).
  3. **No Store hub apps,** which upstream builds for arm64 only (I).
  4. **A double build forever** for every release.
- **Recommendation: no armhf.**

## 5. Performance and RAM

**Load (V, I):**
- The launcher uses about 20 MB RSS on the deck (x86 report, measured).
- LVGL renders in software. The native draw buffer is 96 lines x width x bpp (`cp0_lvgl_dpi_scaled.c:667-669`): for
  example 737 KB at 1920 px x 32 bpp, which is fine.

**Board by board (I unless noted):**
- **Pi 3A+:** same 512 MB as the deck and a 40 % faster A53, so it should feel faster.
- **Walnut ZeroW:** A53 at 1.5 GHz with at least 1 GB, comparable to the 3A+.
- **Pi 4 and Pi 5:** far faster.
- **What grows the cost is screen pixels:**
  - 1024x600 is 2x the deck's pixels: fine on a 3A+ (I).
  - 1920x1080 is 6.75x: avoid on 512 MB A53 boards.
  - The compat flush writes each scaled pixel one by one (`:125-136`). At scale 3-6 that should get the row-replication
    fast path that `external_blit` already has (`:513-537`): about 40 lines.

**Memory on 512 MB with KMS (I).** Default CMA for `vc4-kms-v3d` is set by the overlay (V [16] lists `cma-64` and up).
CMA stays usable for movable pages, so it is not lost. For a launcher-only system, `cma-64` is a safe start; the probe
reads `CmaTotal`.

**Blocking issues:**
- **32-bit atomics and NEON:** not relevant. Every board is aarch64, and ASIMD is mandatory there. NEON stays on
  (`linux_x86_cross_cp0_config_defaults.mk:96-97`).
- **Pi 5 16 KB pages (V [8]):**
  - `applaunch_vfb.h:20` defines `APPLAUNCH_VFB_HEADER_BYTES 4096u`, and the shim maps the app's framebuffer at
    `offset + 4096` (`applaunch_vfb_shim.c:163-165`).
  - On a 16 KB-page kernel, an mmap offset must be a multiple of 16384, so this mmap fails with EINVAL (I, standard
    mmap rule). **Every stock compat app would get no framebuffer.**
  - **Fix:** a 64 KiB header (a multiple of 4K, 16K and 64K pages). Task 012 F1 phase 2 already reworks this header
    ("VFB2"), so bundle it there.
  - **Workaround with no code:** `kernel=kernel8.img` (4 KB pages) in `config.txt` (V [8]).
  - **Nothing else found:** no jemalloc or tcmalloc anywhere (V grep). LVGL uses the C library allocator. Mesh Hop and
    viz1090 map `/dev/fb0` at offset 0.
  - **Prebuilt Store binaries, Debian's SDL2 and readsb:** low risk, because GNU ld for aarch64 aligns segments to 64 KB
    by default (I).
- **Pi 5 RP1 deferred I/O:** possible frame-rate cap and CPU cost on DPI/DSI fbdev (I). Measure in phase 2; it is the
  trigger for option (b).

## 6. Testing strategy (Claude cannot run hardware)

**`probe-board.sh` (phase 0).** A read-only POSIX sh script with an inline `python3 -I` block for ioctls. It needs no
sudo and prints one markdown block that the user pastes back. The Controller stores it as
`docs/dev/hardware/probes/<board>-<screen>-<date>.md`. It collects:
- **Identity:** `/proc/device-tree/model` and `compatible`; `/proc/cpuinfo` (Hardware, Revision); `uname -a`;
  `getconf PAGESIZE`; `/etc/os-release`; `dpkg --print-architecture`; `ldd --version | head -1`;
  `/proc/meminfo` (MemTotal, CmaTotal); `vcgencmd get_mem arm gpu` if present.
- **Boot config:** non-comment `dtoverlay|dtparam|display_|dpi_|hdmi_|enable_dpi|gpu_mem|kernel=|framebuffer_` lines
  from `/boot/firmware/config.txt` (or `/boot/config.txt` on the Walnut), and `cmdline.txt` with `video=`.
- **Display:** `/proc/fb`; `/sys/class/graphics/fb*/{name,virtual_size,bits_per_pixel,stride}`; `ls -l /dev/dri`;
  `/sys/class/drm/card*-*/{status,enabled,modes}`; `lsmod` filtered for
  `vc4|v3d|drm|rp1|fb|sun4i|sunxi|panel|tinydrm|fbtft`.
- **Input:** `/proc/bus/input/devices`. For each `event*`: `udevadm info` (ID_INPUT_TOUCHSCREEN, ID_INPUT_KEYBOARD,
  LIBINPUT_CALIBRATION_MATRIX), then through Python: EVIOCGNAME, EVIOCGPROP (DIRECT/POINTER), EVIOCGBIT ABS/KEY, and
  EVIOCGABS min/max for X/Y/MT.
- **Backlight and PWM:** `/sys/class/backlight/*/{type,max_brightness,brightness,bl_power}`;
  `/sys/class/pwm/pwmchip*/npwm`; `pinctrl get 18` or `raspi-gpio get 18` if present.
- **Radios and network:** `rfkill list`; `ls /sys/class/bluetooth`; `bluetoothctl show | head -3`;
  `nmcli -t -f DEVICE,TYPE,STATE dev`; `ip -br link` with **MAC addresses masked**, and no SSIDs or keys.
- **Audio and power:** `aplay -l`; `/sys/class/power_supply/*/type`; `ls /dev/rtc*`.
- **Kernel log:** `journalctl -k -b --no-pager` (or `dmesg`), grepped for
  `drm|fb|dpi|dsi|hdmi|goodix|edt|ft5|ili|touch|ads7846|backlight|pwm`, last 60 lines. If not allowed, it says so.
- **Launcher state, if installed:** `systemctl --user is-active APPLaunch`, and the `[dpi]` lines of the journal (fb
  size, rotation, scale, touch range).
- **Two interactive options, off by default:**
  - `--touch-test`: "tap the top-left, then the bottom-right corner of the picture" for 15 s. It prints the raw points
    and the inferred swap/invert or matrix.
  - `--fb-test`: stops nothing itself. It asks the user to stop the launcher, then draws a corner-arrows and RGB-bars
    pattern on the chosen fb, to check orientation and colour order.

**Size:** about 250 lines, savvy-light, 1 run. Expect one fix round after the first board.

**Bring-up checklist, per board and screen** (`docs/dev/checklists/board-bringup.md`):
1. Flash the image named for the board in the matrix.
2. Apply the screen's boot snippet and reboot.
3. Run the probe and paste it.
4. Run `install.sh`.
5. Check:
   - the picture is the right way up and fills the screen;
   - touch hits what is tapped: each corner, then home tiles, toolbar keys and a Settings drag;
   - brightness and screensaver dimming, or a black screen where there is no backlight;
   - Wi-Fi joins and the top bar bars show;
   - the BT keyboard pairs and types;
   - one stock compat app, one full-screen app (Mesh Hop or viz1090) and the terminal;
   - suspend, wake and reboot.
6. Record the result in the matrix.

**Compatibility matrix** (`docs/dev/hardware/compat-matrix.md`). One row per board x screen x OS:

| Board | Screen (interface, res, touch IC) | OS image / kernel / page size | Display path, fb (WxH bpp), rotation | Touch device, matrix | Backlight | Wi-Fi / BT | Launcher version | Status | Probe | Date | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|

Status values: `works`, `works-with-workaround`, `partial` (the failing checklist items), `not-yet` (phase N),
`unsupported` (why).

## 7. Recommendation and phased plan

| Phase | Content | Tier | Size | Boards and screens supported after it | Main risks |
|---|---|---|---|---|---|
| 0. Probe | `probe-board.sh`, compat-matrix and bring-up checklist docs. The user runs it on all five boards with each screen they own. **Also: try the Pi 3A+ with the current panel and `install.sh --force`** | savvy-light | about 250 lines, 1 run | Data. Very likely Pi 3A+ + 2.8" DPI (legacy) works with no code | user time only |
| 0.5. Packaging floor | `libc6 (>= 2.38)` in every app's Depends (`build_deb.py`); install.sh accepts the 3A+ | savvy-light | about 30 lines, 1 run | Zero 2 W, 3A+ | none |
| 1. Profile and touch | `board.conf` plus detection and an env export to apps; touch by capability with hot-plug; calibration matrix (udev format); generic backlight with a black-screen/FBIOBLANK fallback; runtime caps (Speaker, Ethernet, Battery, RTC) and interface names; install.sh board templates. Sequence it after task 012 F1 phase 0 and share its input enumeration | savvy-heavy | 700-1,000 lines, 2-3 runs | Zero 2 W, 3A+, Pi 4 (legacy, if the probe confirms). Screens: 2.8" DPI, official 7" v1 DSI (legacy), HDMI + USB-touch screens on legacy fb | input-enumeration overlap with task 012; the legacy Pi 4 path is unverified |
| 1b. Calibration UI | Settings > Touch wizard and orientation quick-pick, **after the Controller's design** | savvy-medium | 250-400 lines, 1-2 runs | any touch IC, including resistive | needs UX decisions |
| 2. KMS via fbdev emulation | fb chosen by name; 16 bpp and rotated fast paths; KMS boot templates (vc4-kms-v3d, panel overlays, Pi 5 PWM variant); **64 KiB vfb header** (with task 012 F1 phase 2); `APPLAUNCH_FB` and `APPLAUNCH_ROTATE` exported; full-screen apps told about rotation or kept to landscape panels | savvy-medium (savvy-heavy if the Pi 5 needs work) | 300-500 lines, 2 runs | **Pi 5**, Pi 4 on KMS, any Pi on KMS images, Touch Display 2, HyperPixel 4, Waveshare DSI, HDMI | RP1 deferred-I/O pacing; fb numbering on the Pi 5; the Waveshare 2.8" DPI KMS overlay is unverified on the Pi 5 |
| 2b. (conditional) Native KMS backend | `fb_target` kms-dumb provider; DRM master hand-over around X-Fullscreen apps and VT switches | savvy-heavy, or savvy-fable if master hand-over fights back | 350-500 lines, 2-3 runs | same, better frame rate on the Pi 5 | DRM master and fbdev restore races |
| 3. Adaptive layout | `ui_metrics`/dp for native screens; compat toolbar-minimum rule; **640x480 logical surface for X-Fullscreen apps**; `X-MinSize` tile policy. **Controller design per size class first** | savvy-medium (metrics), savvy-careful (full-screen surface) | 450-800 lines, 2-3 runs | screens from 640x480 to 1920x1080 in landscape after rotation; square panels; 480x320 only if the user wants it | UX scope; blit cost on 512 MB boards at large sizes |
| 4. Walnut Pi ZeroW | Debian 12 glibc baseline: either a bookworm arm64 sysroot with `-static-libstdc++` for the launcher and all apps, or a native build on the Walnut. Allwinner profile: HDMI + USB touch, no backlight, vendor `/boot/config.txt` left alone. BlueZ and NetworkManager presence checked | savvy-medium | 1-2 runs plus one rebuild per app | Walnut + HDMI + USB-touch screen (and its SPI LCD if the probe shows an fb) | vendor BSP Wi-Fi/BT drivers; unknown fb stack; glibc baseline affects every package |
| (not recommended) armhf | sysroot, build matrix, per-arch registry, app rebuilds | savvy-medium plus savvy-light per app | M plus ongoing | none of the five boards needs it | permanent double builds; no Store hub apps |

**Cheapest first wins, in order:**
1. Pi 3A+ + the current 2.8" DPI on the legacy path: zero code, test only.
2. Pi 4 + 2.8" DPI on the legacy path, if trixie firmware still supports it: zero to little code.
3. Any Pi + an HDMI USB-touch screen, after phase 1's touch-by-capability.
4. Pi 5, after phase 2.
5. Walnut, last.

**Decisions the user must make:**
1. **OS image per board:** Raspberry Pi OS Lite 64-bit trixie for all four Pis (recommended). For the Walnut, the
   vendor Debian 12 (which forces a glibc baseline build) or skip it until there is a vendor Debian 13.
2. **32-bit support:** recommended **no**.
3. **Minimum supported RAM:** 512 MB, as on the deck (recommended).
4. **Legacy or KMS on the Zero 2 W, 3A+ and Pi 4:** keep legacy where it works (no code), with KMS required only on the
   Pi 5; or move everything to KMS for one display path.
5. **Official target screens.** Which screens the user owns and wants supported. Proposal: Waveshare 2.8" DPI, official
   7" v1, Touch Display 2, one HDMI 1024x600 USB-touch, HyperPixel 4.0.
6. **Scope of resolution adaptability:**
   - letterbox only;
   - density-scaled native screens;
   - full support for small (480x320) and square panels;
   - and whether X-Fullscreen apps get a 640x480 logical surface or must become adaptive.
7. **Pi 5 pages:** fix the vfb (recommended, with task 012) or require `kernel=kernel8.img`.
8. **Glibc baseline for all packages:** Debian 13 only, or Debian 12 too (only needed for the Walnut).
9. **Bundle and registry naming per arch** (aligned with the x86 report decision 7).
10. **Boot config for screens:** may `install.sh` write the screen's boot lines from a per-screen template, or does the
    user keep adding them by hand as today?

**Blockers:**
- **No probe data from any board except the deck.** Phase 0 comes first.
- **The user's screen inventory is unknown.**
- **(V) The Walnut's glibc 2.36 is below the binaries' 2.38.**
- **(V code) The Pi 5 vfb offset.**
- **Task 012 overlaps:** the input enumeration (F1 phase 0) and the vfb header (F1 phase 2). Sequence them, or merge
  them deliberately.

## Open questions

1. Which screens does the user own today, and which should be official targets?
2. Which RAM size are the user's Pi 4, Pi 5 and Walnut ZeroW?
3. Should the same Waveshare 2.8" DPI panel move between the Pi boards? The Zero 2 W has no DSI connector and the
   Walnut has neither DSI nor DPI.
4. On the Pi 3A+ and Pi 4, is the analogue audio jack wanted? It conflicts with the PWM backlight on GPIO18.
5. For the Walnut: is the vendor Debian 12 image acceptable, or is the board only a "nice to have"?
6. Should X-Fullscreen apps get a launcher-provided 640x480 surface? Otherwise Mesh Hop and viz1090 need their own
   adaptive layouts and rotation.
7. The calibration wizard and the small-screen and square-screen layouts need the Controller's visual design. This
   report makes no UI decision.

## Sources

1. Raspberry Pi Zero 2 W: https://www.raspberrypi.com/products/raspberry-pi-zero-2-w/ (V)
2. Raspberry Pi 3A+: https://www.raspberrypi.com/products/raspberry-pi-3-model-a-plus/ (V)
3. Raspberry Pi 4 specifications: https://www.raspberrypi.com/products/raspberry-pi-4-model-b/specifications/ (V)
4. Raspberry Pi 5: https://www.raspberrypi.com/products/raspberry-pi-5/ (V)
5. WalnutPi product list (ZeroW = H618 quad-core): https://www.walnutpi.com/ (V)
6. WalnutPi gen-1 parameters (ZeroW and 1B): https://wiki.walnutpi.com/docs/walnutpi_1/intro/hw-parameter (V)
7. Raspberry Pi OS downloads (64-bit Lite, trixie, kernel 6.18, compatible with 3A+, 4B, 5, Zero 2 W):
   https://www.raspberrypi.com/software/operating-systems/ (V)
8. Pi 5 16 KB pages and the `kernel=kernel8.img` fallback: https://pimylifeup.com/raspberry-pi-page-size/ ;
   jemalloc breakage example: https://forgejo.ellis.link/continuwuation/continuwuity/issues/863 (V, secondary)
9. WalnutPi gen-1 release assets, through the GitHub API on `walnutpi/walnutpi-1` (v2.6.0:
   `..._WalnutPi-1B_6.1.31_debian12_{desktop,server}.rar`): https://github.com/walnutpi/walnutpi-1/releases (V)
10. WalnutPi wiki: gen-1 tutorial covers 1B, ZeroW, CM1 and BOX; image page "核桃派OS（Debian12）":
    https://wiki.walnutpi.com/ , https://wiki.walnutpi.com/docs/walnutpi_1/intro/download (V)
11. Debian bookworm libc6 2.36: https://packages.debian.org/bookworm/libc6 (V)
12. walnutpi-1 README (ZeroW display: "HDMI / SPI LCD"): https://github.com/walnutpi/walnutpi-1 (V)
13. "KMS/DRM is the only method available on the Pi 5" (secondary): https://dietpi.com/docs/releases/v10_5/ ; Raspberry
    Pi DPI white paper (legacy config.txt DPI parameters are not used under KMS):
    https://pip-assets.raspberrypi.com/categories/685-whitepapers-app-notes/documents/RP-003471-WP/Using-a-DPI-display.pdf
    (V for the white paper; Pi 5 statement I)
14. WalnutPi 3.5" LCD (ST7796 480x320, resistive XPT2046, `set-lcd`):
    https://wiki.walnutpi.com/docs/walnutpi_1/os_software/3.5_LCD (V)
15. Raspberry Pi kernel rpi-6.12.y sources, fbdev emulation depth (V, fetched):
    - https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/gpu/drm/vc4/vc4_drv.c (`drm_fbdev_dma_setup(drm, 16)`, line 431)
    - `drivers/gpu/drm/rp1/rp1-dpi/rp1_dpi.c` (`drm_fbdev_ttm_setup(&dpi->drm, 32)`, line 447)
    - `drivers/gpu/drm/rp1/rp1-dsi/rp1_dsi.c` (line 321)
16. Raspberry Pi overlays README, rpi-6.12.y (V, fetched):
    https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm/boot/dts/overlays/README . It covers: pwm (PWM0
    pins, the analogue audio note), pwm-pio (Pi 5), goodix, edt-ft5406, vc4-kms-dpi-generic (backlight-pwm, rotate),
    vc4-kms-dpi-hyperpixel4/4sq/2r, vc4-kms-dsi-7inch, the ILI9881 5"/7" (Touch Display 2; its overlay uses
    `goodix,gt911`), vc4-kms-dsi-waveshare-panel (with resolutions), vc4-kms-v3d (cma sizes). The overlay sources were
    read for their touch compatibles.
17. Pi 5 GPIO18 = PWM0 channel 2, pwmchip2 (secondary): https://pypi.org/project/rpi-hardware-pwm ,
    https://pi4j.com/blog/2024/20240423_pwm_rpi5 (I)
18. WalnutPi `config.txt` (`overlays=`, `overlay_prefix=sun50i-h616`):
    https://wiki.walnutpi.com/docs/walnutpi_1/os_software/config.txt (V)
19. No Armbian image found for the Walnut Pi (search only; absence is I).
20. Waveshare 2.8inch DPI LCD wiki (KMS overlays, `video=DPI-1:480x640M@60,rotate=90`, the udev
    `LIBINPUT_CALIBRATION_MATRIX`, Goodix touch, 480x640 native): https://www.waveshare.com/wiki/2.8inch_DPI_LCD (V)
21. The Pi Hut capacitive touchscreen catalogue (resolutions on sale):
    https://thepihut.com/collections/touchscreen-displays-for-raspberry-pi?page=1 (V)

## Addendum 2026-10-10 - measured on the user's Walnut Pi ZeroW (ssh, read-only probe)

Corrects the report's Walnut assumptions (those came from web research, this is measured):
- Model "Walnut Pi ZeroW", **Armbian 26.11 trixie (Debian 13), kernel 6.18.51-current-sunxi64, aarch64, glibc 2.41**. So the launcher/app binaries (GLIBC_2.38) should run as they are: the "Debian 12, glibc 2.36" blocker does NOT apply to this image.
- RAM 993 MB, swap 496 MB, 4 cores, 4 KB pages, 29 GB SD.
- Display: **DRM only, no /dev/fb\***, card0 + card1 + renderD128, HDMI-A-1 disconnected (no screen attached during the probe). No backlight class, `pwmchip0` exists. Input: only `dw_hdmi` (no touch attached). Wi-Fi `wlan0` present; no `rfkill` binary. sudo needs a password.
- Consequence: the Walnut needs the DRM/KMS display path (no fbdev emulation visible), so it moves from "phase 4" to the same phase as Pi 5 KMS, and it is a good KMS test board (Armbian mainline-style, 4 KB pages).

## Addendum 2026-10-10 (2) - measured on the user's Pi 3A+ with the 3.5" Luckfox screen (ssh, read-only probe)

- Raspberry Pi 3 Model A+ Rev 1.1, **Raspberry Pi OS trixie 64-bit** (kernel 6.18.50+rpt-rpi-v8, aarch64, glibc 2.41, 4 KB pages), 425 MB RAM (+425 MB swap), 4 cores, 58 GB SD. Our binaries (GLIBC_2.38) should run as they are.
- **Not the legacy firmware path:** config.txt has `dtoverlay=vc4-kms-v3d` + `max_framebuffers=2`. The earlier assumption "Pi 3A+ = same path as the Zero 2 W (DPI/fbdev, almost zero code)" is WRONG for this setup: this is a KMS board.
- Screen: ST7796S 320x480 SPI panel through the generic `mipi-dbi-spi` overlay (`speed=48000000,rotation=90`, `width=320,height=480`, `width-mm=49,height-mm=79`, reset GPIO27, DC GPIO22, backlight GPIO18) -> DRM `card0-SPI-1` connected + fbdev emulation **/dev/fb1 (name panel-mipi-dbi, virtual_size 320x480, 16 bpp)**; the HDMI card is card1 (disconnected). Kernel cmdline `fbcon=map:10,rotate:1`. The panel is portrait natively; rotation=90 should make it 480x320 landscape for DRM clients, but the fbdev emulation reports 320x480: **must be tested** (does /dev/fb1 content appear rotated? is the fb portrait with rotation applied by fbcon only?).
- Touch: `1-005d Goodix Capacitive TouchScreen` on I2C (`dtoverlay=goodix,addr=0x5d`), event0 (also registered as mouse0), name contains "Goodix" like the deck's, so name-based detection would even work; axes/rotation for a 480x320 landscape layout still need calibration (swap/invert).
- Backlight: `/sys/class/backlight/backlight_gpio` (on/off only, no PWM brightness): Brightness in Settings must degrade to on/off on this profile. Bluetooth hci0 present (down), Wi-Fi wlan0 present.
- Consequences: (1) the real target here is a 480x320 landscape screen (4:3 is not met: 3:2); the launcher's 640x480 native UI and the 2x-scaled 320x170 compat window (640x340) do not fit as is; needs the resolution-adaptive model (phase 3) or a 1x compat scale (320x170 fits natively in 480x320). (2) fb device is /dev/fb1 and 16 bpp: the display backend must stop assuming /dev/fb0 and 32 bpp. (3) This board is the best real-world first test for the "KMS + emulated fbdev, 16 bpp, small screen" path, and it is already on the user's desk.

## Addendum 2026-10-10 (3) - measured on the user's Pi 5 with the 4" Pimoroni HyperPixel (ssh, read-only probe)

- Raspberry Pi 5 Model B Rev 1.0, **Raspberry Pi OS trixie 64-bit**, kernel 6.18.39+rpt-rpi-2712, glibc 2.41, **4 GB RAM** (+2 GB swap), NVMe 117 GB, 4 cores. **Page size confirmed 16384 (16 KB)**: the vfb shim's 4096-byte header offset risk in the report is real on this board.
- Display: `dtoverlay=vc4-kms-v3d` + `dtoverlay=vc4-kms-dpi-hyperpixel4,rotate=90`. DRM `card2-DPI-1` connected (RP1 DPI), HDMI-A-1/2 disconnected. **/dev/fb0 = drm-rp1-dpidrmf, virtual_size 480x800, 32 bpp**: so the emulated framebuffer exists, is 32 bpp (matches what the launcher expects), but is the panel's NATIVE PORTRAIT 480x800 even though the overlay says rotate=90. Whether `rotate=90` rotates DRM output or only the touch/fbcon must be tested. The launcher's landscape UI needs either a software rotation in the display backend (rotate a 800x480 canvas into a 480x800 fb) or the panel/DRM rotation property.
- Touch: `Goodix Capacitive TouchScreen` (goodix_ts), event5, also mouse0; name contains "Goodix". Axes for the rotated landscape view need calibration (the overlay `rotate=90` may already set a touchscreen transform; verify with evtest).
- Backlight: `/sys/class/backlight/backlight` (a real backlight class device, so PWM-like brightness control through sysfs works, unlike the Pi 3A+ on/off gpio). Bluetooth hci0 UP, Wi-Fi + Ethernet present. `pwmchip0` exists (RP1).
- Target resolution: 800x480 landscape (HyperPixel 4"), the popular "wide" size: the launcher's 640x480 native UI needs the adaptive layout (or letterbox 640x480 inside 800x480 as a cheap first step: 80 px bars; and the compat window 640x340 at 2x fits unchanged).
- Consequence for the plan: the Pi 5 does NOT need a new DRM backend for a first run: it already has a 32 bpp /dev/fb0, only needs (1) rotation handling 480x800 -> 800x480 landscape in the backend, (2) the 16 KB page fix in the vfb header, (3) touch axis config. It is therefore cheaper than the report assumed, and the 640x480 letterbox gives a working first result on this screen.

## Addendum 2026-10-10 (4) - measured on the user's HackBerry Pi CM5 (host "hackberry", ssh, read-only probe)

- **Raspberry Pi Compute Module 5 Rev 1.0** on a ZitaoTech HackBerry Pi CM5 board (`dtoverlay=hackberrypicm5`), Raspberry Pi OS trixie 64-bit, kernel 6.18.50+rpt-rpi-2712, glibc 2.41, **16 KB pages**, 4 GB RAM (+2 GB swap), 4 cores, NVMe 235 GB. Same binaries and same 16 KB-page shim issue as the Pi 5.
- Display: DPI via RP1 (`card0-DPI-1` connected), **/dev/fb0 = drm-rp1-dpidrmf, 720x720, 32 bpp: a SQUARE screen** (the first non-landscape target). The native 640x480 UI does not fit as 4:3; 640x480 fits inside 720x720 letterboxed (40 px side margins would be 40 left/right, 120 px top/bottom) and the 640x340 compat window at 2x fits in width. A proper adaptive layout for a square screen is a different shape problem from 800x480.
- Touch: `15-0048 EP0110M09` (I2C addr 0x48, driver edt_ft5x06, registered as mouse1/event9): NOT named Goodix, so the current name-based touch detection would NOT find it: strong argument for capability-based detection (ABS_MT axes) + a candidate list.
- Input: built-in keyboard `ZitaoTech HACKBerryPiQ20` (USB HID, event0, a BlackBerry-style Q20 keyboard, with mouse/consumer-control/system-control interfaces) plus a paired BT remote `XWF-M18-M28-M38 (AVRCP)` (event10, kbd handler: it will count as a "keyboard" by naive detection but lacks A/Z keys, so the A/Z/Enter/Space filter of F1 phase 0 must be tested on it). `uinput` module loaded already. This board has a permanent physical keyboard: the F1 fallback on-screen keyboard would never show here, and F1 phase 0 (read every real keyboard) is REQUIRED for it to type at all (it is not the "M4 Keyboard" the launcher reads today).
- Backlight: **no /sys/class/backlight device** (empty); brightness would need the board's own method (RP1 PWM `pwmchip0` or an I2C/GPIO control): unknown, to investigate on the board's docs/overlay. Settings > Brightness must be hidden or disabled on this profile until solved.
- Other: Bluetooth hci0 up, Wi-Fi + Ethernet present.

## Addendum 2026-10-10 (5) - measured on the user's ClockworkPi uConsole with CM5 (host "clockworkpi", ssh, read-only probe)

- **Raspberry Pi Compute Module 5** in a ClockworkPi uConsole (`dtoverlay=clockworkpi-uconsole-cm5`, `vc4-kms-v3d-pi5,cma-384`), **8 GB RAM**, 29 GB root, **Debian 12 bookworm, glibc 2.36, kernel 6.12.94-v8-16k+ (16 KB pages)**. **Our binaries (GLIBC_2.38) will NOT run on this image** (this is the blocker the report predicted for the Walnut; the Walnut turned out fine, this board has it). Options: reflash to trixie, or build a second bundle against bookworm's glibc 2.36 (build in a bookworm container; check that LVGL/SDL/other deps exist at that version).
- Display: DSI panel (`card1-DSI-2` connected), **/dev/fb0 = drm-rp1-dsidrmf 720x1280, 32 bpp, native PORTRAIT** (the 5" uConsole panel is 1280x720 mounted rotated; `fbcon=rotate:1`). **HDMI-A-1 on card2 is also connected** and gives /dev/fb1 = vc4drmfb 1024x768, 16 bpp: a board can expose two framebuffers, so the profile must choose by connector/driver, never "fb0". Need rotation 270/90 in the backend to present a 1280x720 landscape canvas.
- Touch: **no touchscreen device** in /proc/bus/input/devices (the uConsole has a trackball/keyboard unit, not touch): the launcher's touch-first navigation is not usable here, keyboard/trackball navigation is the path (there is an existing keyboard-driven stock UI path in the upstream launcher).
- Input: the uConsole keyboard/trackball did NOT enumerate in this probe (no keyboard device; `axp20x-pek` is the power key under phys `m1kbd`). Probably the keyboard unit was off or on a different USB path at probe time: to re-check. **`Realtek RTL2832U reference design` (event6, Handlers kbd, phys usb-.../ir0) is the RTL-SDR dongle's IR receiver**: confirms the false-positive risk for F1 phase 0 (it must be rejected by the A/Z/Enter/Space test; verify).
- Backlight: `/sys/class/backlight/backlight@0` present (works through sysfs). Bluetooth hci0 DOWN, Wi-Fi + Ethernet present, an lxc bridge (lxcbr0) and nftables modules suggest the image has container tooling.
- Screen target: 1280x720 landscape (16:9) after rotation: bigger than any other board; the 640x480 native UI would be a small centred rectangle; the adaptive layout / integer scale 2x of a 640x360 canvas fits 1280x720 exactly.

## Addendum 2026-10-10 (6) - desktop sessions on the Pi 5 and the HackBerry; first fb-test-pattern runs

- **Pi 5 (RPI53) and HackBerry CM5 run the Raspberry Pi OS desktop**: `graphical.target`, lightdm, **labwc (Wayland compositor)**, wf-panel-pi, pcmanfm; the Pi 5 also runs **squeekboard** (on-screen keyboard). The Pi 3A+ and the uConsole probes showed no desktop. The compositor holds the DRM master, so framebuffer writes through the emulated /dev/fb0 do not show reliably and the first pattern runs on those two boards (30 s each, 2026-10-10) are NOT valid evidence. Only their reported geometry is valid (Pi 5 fb 480x800 portrait 32 bpp with touch 0..799 x 0..479 already landscape; HackBerry fb 720x720, touch 0..719 both).
- Pi 3A+ run (no desktop): fb1 320x480 16 bpp rotate 0, touch 0..319 x 0..479 (portrait, consistent with the fb).
- Consequences for the plan: (1) the launcher on such boards must take the screen from the desktop: boot to `multi-user.target` (or stop lightdm) and/or become a DRM master itself; a cheap install-time option is `systemctl set-default multi-user.target` with an opt-in prompt. (2) Under a compositor the correct display path is a DRM/KMS or Wayland client, not fbdev: another argument for the DRM backend (phase 2/3) on desktop images. (3) squeekboard shows desktop images already have an on-screen keyboard: F1's overlay must stay off when a desktop session owns the screen. (4) probe-board.sh and fb-test-pattern.py must detect a running compositor/X server (labwc, wayfire, Xorg, lightdm) and say so; pending small fix.

## Addendum 2026-10-10 (7) - user's visual reading of fb-test-pattern (desktop stopped on Pi 5 and HackBerry)

User report (orientation of the big asymmetric "F" drawn at the top-left of the framebuffer, as seen in the desired landscape viewing position):
- **Pi 3A+ (fb1 320x480 portrait, 16 bpp):** F rotated 90 degrees LEFT (counter-clockwise).
- **Pi 5 + HyperPixel (fb0 480x800 portrait, 32 bpp):** F rotated 90 degrees LEFT (counter-clockwise), same as the Pi 3A+.
- **HackBerry CM5 (fb0 720x720, 32 bpp):** F upright, correct.
- Not yet reported: arrow directions, border cropping, colour order, whether touch squares landed under the finger.

Reading (to confirm with the arrows/calibration): on both portrait-native boards the framebuffer is the native portrait buffer and the user holds the device in landscape, so the launcher must **pre-rotate its landscape canvas by 90 degrees CLOCKWISE** before writing it to the buffer (a CCW-rotated F means the panel shows the buffer turned CCW; the compensation is CW). Square HackBerry needs no rotation. Consequences: the display backend rotation setting is mandatory on 2 of the 3 new boards, and the rotation must be applied in the blit (rotating blit of the dirty rectangle, 90 degrees, with fb dims swapped: logical 480x320 -> fb 320x480; logical 800x480 -> fb 480x800); touch must be mapped through the same rotation (Pi 5 reports touch already in landscape 0..799 x 0..479, the Pi 3A+ in portrait 0..319 x 0..479). The boards' own overlays (`rotation=90` on the 3A+ mipi-dbi overlay, `rotate=90` on the HyperPixel overlay) do NOT rotate the fbdev emulation, they only affect DRM-native clients / fbcon.

## Addendum 2026-10-10 (8) - rest of the user's visual reading

On all three boards (Pi 3A+, Pi 5, HackBerry): borders fully visible (no cropping), colour order correct (RGB565 on the 3A+, XRGB on the others), and the touch squares always matched the finger position. The touch squares were drawn with raw coordinates normalised to the framebuffer size with no swap/invert, so **touch is already in framebuffer (buffer) orientation on all three boards**: even on the Pi 5, whose reported ranges (0..799 x 0..479) do not match its portrait 480x800 buffer, normalising by the reported range lands on the finger. Design consequence: the touch layer must normalise by the device's own ABS range (never assume pixels), then apply the same 90-degree clockwise rotation as the picture when the buffer is portrait (3A+, Pi 5); HackBerry: identity. The DRM-native panel/overlay rotation does not enter. `--calibrate` is therefore optional for these three boards (the axis values can be derived from the rotation).
