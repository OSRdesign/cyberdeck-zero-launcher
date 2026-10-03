# cyberdeck-zero-launcher — a touch launcher for a Raspberry Pi Zero 2W cyberdeck

I ordered two **CardputerZero** from M5Stack, but they have not been delivered yet and I could not wait any
longer to try the software. Sitting on my desk was the cyberdeck I built from this Reddit post:
[My first cyberdeck build](https://www.reddit.com/r/cyberDeck/comments/1tng497/my_first_cyberdeck_build/)
(a Raspberry Pi Zero 2W, a Waveshare 2.8" DPI touch LCD and a Bluetooth keyboard). So I opened Claude Code
and asked it to adapt the CardputerZero launcher to that machine. This repository is the result: the
CardputerZero launcher running on a Pi Zero 2W, with touch, a 2x upscaled window for the stock 320x170
apps, a full-screen terminal, a Calculator, the Store, Wi-Fi / Bluetooth / time settings and a PWM backlight.

> **This is a hobby project and a derivative work.** Almost all of the code is the work of the CardputerZero
> and M5Stack teams and of the open-source projects listed in the credits below. My changes are the Pi port
> (see [`projects/APPLaunch/pizero2w/`](projects/APPLaunch/pizero2w/)) and were written together with Claude.

## Inspiration and credits

This project exists only because of other people's generous work. Huge thanks to all of them, from a hobbyist
who learns a lot from open source:

- **CardputerZero launcher** (the original this is based on) and its apps — the
  [CardputerZero](https://github.com/CardputerZero) organisation:
  [launcher](https://github.com/CardputerZero/launcher), [Store](https://github.com/CardputerZero/Store),
  [Calculator](https://github.com/CardputerZero/Calculator), [CameraApp](https://github.com/CardputerZero/CameraApp),
  [Keyboard-Guide](https://github.com/CardputerZero/Keyboard-Guide)
- **M5Stack** — [M5Stack_Linux_Libs](https://github.com/m5stack/M5Stack_Linux_Libs) SDK and the CardputerZero hardware
- **LVGL** — [lvgl/lvgl](https://github.com/lvgl/lvgl), the graphics library behind every screen
- **SCons** — [SCons/scons](https://github.com/SCons/scons), the build system
- **The cyberdeck that started it** — the author of the Reddit build post linked above
- **Waveshare** — the 2.8" DPI LCD and its overlays ([Waveshare wiki](https://www.waveshare.com/wiki/2.8inch_DPI_LCD))
- **Raspberry Pi** — [raspberrypi/linux](https://github.com/raspberrypi/linux) and [raspberrypi/firmware](https://github.com/raspberrypi/firmware)
- **EXKnight M4** Bluetooth keyboard, whose layout I mapped
- Libraries bundled by the launcher (full list and licences in
  [`docs/OPEN_SOURCE_COMPONENTS.md`](docs/OPEN_SOURCE_COMPONENTS.md) and [`LICENSES/`](LICENSES/)):
  [nlohmann/json](https://github.com/nlohmann/json), [cJSON](https://github.com/DaveGamble/cJSON),
  [fmt](https://github.com/fmtlib/fmt), [spdlog](https://github.com/gabime/spdlog),
  [cpp-httplib](https://github.com/yhirose/cpp-httplib), [libhv](https://github.com/ithewei/libhv),
  [eventpp](https://github.com/wqking/eventpp), [tinyalsa](https://github.com/tinyalsa/tinyalsa),
  [miniaudio](https://github.com/mackron/miniaudio), [RadioLib](https://github.com/jgromes/RadioLib),
  [SimpleBLE](https://github.com/simpleble/simpleble), [C-Thread-Pool](https://github.com/Pithikos/C-Thread-Pool)
- **System pieces used on the Pi**: [libinput](https://gitlab.freedesktop.org/libinput/libinput),
  [libxkbcommon](https://github.com/xkbcommon/libxkbcommon), [FreeType](https://gitlab.freedesktop.org/freetype/freetype),
  [NetworkManager](https://gitlab.freedesktop.org/NetworkManager/NetworkManager), [BlueZ](https://github.com/bluez/bluez),
  [systemd](https://github.com/systemd/systemd), [polkit](https://github.com/polkit-org/polkit)
- **Claude Code** by [Anthropic](https://github.com/anthropics/claude-code), which did the porting work with me

Thank you all for your contribution to open source, and for making it possible for hobbyists like me to build
and tinker with things like this.

## What is in this repository

- The CardputerZero launcher tree, with the Pi port on top. The original project README is kept in
  [`docs/UPSTREAM_README.md`](docs/UPSTREAM_README.md) (also [中文](README_ZH.md), [日本語](README_JA.md)).
- **[`projects/APPLaunch/pizero2w/`](projects/APPLaunch/pizero2w/README.md)** — everything specific to the Pi:
  what the port adds, how to build the bundle (`build.sh`), how to install it on a Pi (`install.sh`), the
  panel configuration, udev / PolicyKit rules, and the known limits.

## Quick start

1. Set up the Pi and the panel as described in the [Pi port README](projects/APPLaunch/pizero2w/README.md).
2. Build on Linux x86_64 or WSL: `projects/APPLaunch/pizero2w/build.sh --with-store`
3. Copy `pizero2w-bundle.tar.gz` to the Pi, unpack it and run `sudo ./install.sh`.

## Licence

MIT, as the original ([`LICENSE`](LICENSE), copyright M5Stack Technology CO LTD); third-party components keep
their own licences ([`LICENSES/`](LICENSES/)). Not affiliated with or endorsed by M5Stack, CardputerZero,
Waveshare or the Raspberry Pi Foundation.
