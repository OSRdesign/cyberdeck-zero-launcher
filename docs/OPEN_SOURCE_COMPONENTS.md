# Open-source and third-party components

Inventory checked against this checkout on 2026-09-09, updated 2026-10-22 when the upstream code we do not use was removed (report 029, section "Removed on 2026-10-22" below), and again in 2026-11 (the stb, SQLite, libcurl and map rows, and the programs the launcher runs). Build references distinguish linked dependencies from optional SDK downloads; this is not a binary SBOM. M5Stack SPDX headers and the root MIT license apply to M5Stack-authored code, without replacing upstream, font or submodule notices.

## Project code

The [root MIT license](../LICENSE) uses `2026 M5Stack Technology CO LTD`. A copy is in `ext_components/cp0_lvgl/LICENSE`. The Store (`projects/AppStore`) is a separate Git repository (submodule): its source and license changes are delivered with that repository.

## Embedded, fetched and linked source dependencies

Paths in the evidence column are relative to the repository root.

| Component | Build/source evidence | License and local text |
| --- | --- | --- |
| M5Stack Linux Libs SDK | `SDK`, pinned gitlink; SCons/Kconfig framework | [MIT, 2024 M5Stack](../LICENSES/M5Stack-SDK-MIT.txt) |
| LVGL 9.5 | SDK LVGL component, APPLaunch `lv_freetype*` and `lv_sdl_keyboard.c` overrides | [MIT](../LICENSES/LVGL-MIT.txt); bundled sublibraries retain their own notices |
| FreeType | APPLaunch `main/lvgl/ftmodule.h` and `main/SConstruct` | FTL or GPL-2.0; [FTL text](../projects/APPLaunch/main/lvgl/freetype-LICENSE.txt) |
| miniaudio 0.11.25 | `ext_components/Miniaudio/include/miniaudio.h`; audio service | [Unlicense OR MIT-0](../ext_components/Miniaudio/LICENSE), David Reid |
| sigslot | `ext_components/Sigslot/SConstruct`, pinned commit `b588b791b9cf7eb17ff0a74d8aebd4a61166c2e1`; used by apps that build against this tree (LAN Scan, an app of the `small-screen` branch only) | [MIT](../ext_components/Sigslot/LICENSE) |
| eventpp | cp0_lvgl, APPLaunch and AppStore requirements; SDK eventpp component | [Apache-2.0](../LICENSES/eventpp-Apache-2.0.txt) |
| libhv | AppStore `main/SConstruct` requirement `hv` | [BSD-3-Clause](../LICENSES/libhv-BSD-3-Clause.txt) |
| fmt | `SDK/components/utilities/SConstruct`, `party/fmt` | [MIT with optional compiled-object exception](../LICENSES/fmt-MIT.txt) |
| nlohmann/json 3.11.3 | the Store (`projects/AppStore`, credited on the Licenses page) | [MIT](../LICENSES/nlohmann-json-MIT.txt), Niels Lohmann; original SPDX preserved |
| stb_image 2.30 | `ext_components/cp0_lvgl/third_party/stb/stb_image.h` (unmodified, provenance in its `README.md`), compiled only into the map engine (`src/map/cp0_map_stb.c`, `CONFIG_CP0_LVGL_MAP`, off by default; the launcher does not use it): PNG / JPEG tiles, the zlib inflater for PMTiles directories; built `STB_IMAGE_STATIC` | [public domain (Unlicense) or MIT](../LICENSES/stb_image-MIT-or-Unlicense.txt), Sean Barrett, at the user's choice |
| JetBrains Mono | `projects/APPLaunch/APPLaunch/share/font` | [OFL-1.1](../projects/APPLaunch/APPLaunch/share/font/JetBrainsMono-OFL.txt) |

## Fonts, images and sounds of the launcher (`projects/APPLaunch/APPLaunch/share`)

Installed under `/usr/share/APPLaunch/share/`. Every font family below except Alibaba PuHuiTi and `svgfont.ttf` has its
licence text in `share/font/` (named `*-OFL.txt` or `*-LICENSE.txt`, which `build.sh` copies into
`/usr/share/doc/cyberdeckzero-launcher/fonts/`). The licence of each file was read from the file's own name table
(copyright, licence description, version) and the text taken from the upstream repository at the matching tag; the
source URLs are in [`LICENSES/README.md`](../LICENSES/README.md). "Not established" means the licence of the file could
not be shown from the file or from an upstream source.

| File(s) | Asset | Licence | Status |
| --- | --- | --- | --- |
| `font/JetBrainsMono-Regular.ttf`, `JetBrainsMono-Bold.ttf` | JetBrains Mono | SIL OFL 1.1 (version 2.304), [text shipped](../projects/APPLaunch/APPLaunch/share/font/JetBrainsMono-OFL.txt) | recorded |
| `font/DejaVuSans.ttf`, `DejaVuSansMono.ttf`, `DejaVuSerif.ttf` | DejaVu fonts (<https://dejavu-fonts.github.io/>) | Bitstream Vera Fonts licence (DejaVu changes in the public domain), plus the Arev licence for some glyphs (Sans 2.35, Sans Mono and Serif 2.37), [text shipped](../projects/APPLaunch/APPLaunch/share/font/DejaVu-LICENSE.txt) | recorded |
| `font/LiberationMono-Regular.ttf` | Liberation Mono | SIL OFL 1.1 (the file is version 2.1.5; the 1.x releases were GPL-2.0 with a font exception, not shipped), [text shipped](../projects/APPLaunch/APPLaunch/share/font/LiberationMono-LICENSE.txt) | recorded |
| `font/Montserrat-Medium.ttf`, `Montserrat-Bold.ttf` | Montserrat (<https://github.com/JulietaUla/Montserrat>) | SIL OFL 1.1 (Medium is version 7.200, Bold is version 2.001 with an older copyright notice), [text shipped](../projects/APPLaunch/APPLaunch/share/font/Montserrat-OFL.txt); also covers the Montserrat glyphs of LVGL's built-in `lv_font_montserrat_*` fonts compiled into the launcher | recorded |
| `font/NotoSansCJK-Regular.ttc` | Noto Sans CJK (<https://github.com/notofonts/noto-cjk>) | SIL OFL 1.1 (version 2.004, Copyright 2014-2021 Adobe, Reserved Font Name 'Source'), [text shipped](../projects/APPLaunch/APPLaunch/share/font/NotoSansCJK-OFL.txt) | recorded |
| `font/AlibabaPuHuiTi-3-55-Regular.ttf` | Alibaba PuHuiTi 3.0 | Copyright 2020-2023 Alibaba (China) Co., Ltd. (file version 3.01); the file carries no licence text or URL, and no official licence text could be fetched | **not established**: the terms and the right to redistribute the file in a bundle are not shown; owner decision (see the pull request that added the texts) |
| `font/FontAwesome5-Solid+Brands+Regular.woff` | Font Awesome 5 Free (<https://fontawesome.com/>) | Font Awesome 5.9.0: the font file is SIL OFL 1.1; the CC BY 4.0 icons (SVG/JS) and the MIT code are not part of the bundle (brand icons are trademarks of their owners), [text shipped](../projects/APPLaunch/APPLaunch/share/font/FontAwesome-LICENSE.txt) | recorded |
| `font/svgfont.ttf` | icon font "svgfont", 10 glyphs (exit, fast_forward, fast_rewind, list, pause, play, record, sample_rate, speed, stop); generated by svg2ttf from a Fontello project; no copyright or licence in the file; no code names it (report 029) | **not established**: Fontello fonts take the licences of the glyph sets they were built from, and those are not recorded; first added in this history by the upstream launcher author on 2026-06-11 (commit 45535b0f, Compass app assets) |
| `images/lockscreen.png` | lock-screen wallpaper (PNG, 320x150, no metadata); added upstream by the M5Stack launcher author in 2026-09 (commits 72fcc076, 21dcb17e) | no source or licence recorded | **not established**: the image carries no author or licence, so only the root MIT licence of the upstream launcher can be presumed; owner decision (replace it or find the source) |
| `images/logo_lcd.png` | upstream start-up logo (M5Stack logo, a trademark of its owner) | not ours to use as a brand | never in our bundles (report 025); removed from the repository (report 029) |
| other `images/*.png` (app icons, carousel parts) | upstream launcher assets | presumably the root MIT licence of the upstream launcher | source to verify |
| `audio/Ding2.wav`, `enter.wav`, `key_back.wav`, `key_enter.wav`, `shutdown.wav`, `switch.wav`, `blocked.mp3`, `lock.mp3`, `select.mp3`, `startup.mp3`, `unlock.mp3` | system sounds | unknown | source and licence to verify |

LVGL embeds separately licensed codecs, renderers and fonts. Preserve its `src/libs/*/LICENSE*`, `src/stdlib/builtin/LICENSE*` and `scripts/built_in_font/font_license/` records when distributing those features. Generated font arrays retain the input font's license. Audio notices remain beside the assets that carry them.

## Additional SDK downloads

These source trees exist locally; cache presence does not establish that APPLaunch links them.

| Component | Evidence / selection | License text |
| --- | --- | --- |
| cJSON | `SDK/components/cjson/SConstruct`, linked into the launcher (Settings > Updates reads GitHub's release list) | [MIT](../LICENSES/cJSON-MIT.txt) |
| C-Thread-Pool | `SDK/github_source/C-Thread-Pool`; no direct launcher wrapper found | [MIT](../LICENSES/C-Thread-Pool-MIT.txt) |
| cpp-httplib | `SDK/github_source/cpp-httplib`; no direct launcher wrapper found | [MIT](../LICENSES/cpp-httplib-MIT.txt) |
| SimpleBLE | `SDK/github_source/SimpleBLE`; cp0 Bluetooth uses BlueZ/DBus | [BUSL-1.1](../LICENSES/SimpleBLE-BUSL-1.1.md): source-available with usage restrictions and version-dependent GPL v3 change date; not a permissive open-source dependency |

## Platform dependencies

Component SConstruct files and application SConstruct/CMake files resolve these from the host or target sysroot. Preserve version-specific notices from the packages actually shipped.

| Dependency | Use | License family |
| --- | --- | --- |
| SDL2, optional SDL2_mixer | Simulator display, input and audio (and the unit tests of the SDL backend) | zlib; mixer codecs have separate terms |
| GLib, GObject, GIO | Native service/event integration | LGPL-2.1-or-later |
| libdbus; BlueZ daemon | Bluetooth IPC | D-Bus AFL/GPL dual terms; BlueZ GPL/LGPL per component |
| libnm; NetworkManager daemon | Wi-Fi | libnm LGPL-2.1-or-later; NetworkManager GPL-2.0-or-later |
| libinput, xkbcommon, udev | Native input/device discovery | MIT for libinput/xkbcommon; udev LGPL terms |
| libcamera, libcamera-base | Camera | LGPL-2.1-or-later library; separate tool terms |
| libjpeg / libjpeg-turbo | Camera/image encoding | IJG, BSD and zlib notices by source file |
| ZeroMQ | Optional log publisher and automation RPC | MPL-2.0 for current libzmq; verify selected package |
| OpenSSL | AppStore TLS | OpenSSL 3 Apache-2.0; older releases differ |
| IEEE OUI registry (data) | Vendor names in the LAN Scan and Wi-Fi Survey apps (apps repo, `share/<app>/oui.tsv`), from <https://standards-oui.ieee.org/> | Public registry published by the IEEE; credited in each package's `copyright` file |
| MeshCore protocol and firmware (data format only) | Mesh Hop app (apps repo): talks the MeshCore companion protocol over USB serial; written from the protocol docs and meshcore_py as a reference, radio presets from `api.meshcore.nz`; no launcher code involved | MeshCore: MIT (Scott Powell / rippleradios.com); meshcore_py: MIT (Florent de Lamotte); GPL clients (wadamesh, Meshy) not used as code; credited in the package's `copyright` file |
| FreeType; Montserrat Medium, Font Awesome 5 Free (fonts) | Status bar of the Pi port (cp0_statusbar), shared with full-screen apps | FreeType FTL/GPL-2.0; fonts SIL OFL 1.1; Font Awesome icons CC BY 4.0 |
| Programs the launcher runs, never linked or shipped: `pactl` (pulseaudio-utils or pipewire-pulse), `amixer` (alsa-utils), `bluetoothctl` and `btmgmt` (bluez) | Sound output and volume, and the Bluetooth audio facts of `board.state` (`board_facts.cpp`, `cp0_capabilities`); each is optional and its absence only leaves the fact out | Per package: pulseaudio-utils LGPL-2.1-or-later, alsa-utils GPL-2.0-or-later, bluez GPL-2.0-or-later; the packages of the system, not ours |
| libcrypt, libc, libstdc++ | Account setup and runtime | Package-specific LGPL/GPL with runtime exceptions |
| SQLite (`libsqlite3.so.0`, Debian package `libsqlite3-0`) | MBTiles tile archives in the map engine of cp0_lvgl (`CONFIG_CP0_LVGL_MAP`, off by default): loaded at run time with `dlopen`, never linked, no header needed to build (`src/map/cp0_map_sqlite.c`); apps that enable the engine list `libsqlite3-0` in their `depends` | Public domain (<https://sqlite.org/copyright.html>) |
| PMTiles v3 specification (format only) | The map engine's own PMTiles reader, written from the specification (no reference code used); the tests' fixtures `tests/fixtures/map/ref_*.pmtiles` were written with the reference `pmtiles` Python package (BSD-3-Clause, Protomaps), which is not shipped | Specification CC0; reference implementations BSD-3-Clause |
| MBTiles 1.3 specification (format only) | The map engine's MBTiles reader and folder converter, and its viewing caches (`tiles/<id>.mbtiles`) | Specification text CC BY 3.0 US, "use in products and code is entirely free" (Mapbox) |
| libcurl (`libcurl.so.4`, Debian package `libcurl4t64`, with the system's TLS library and `ca-certificates`) | HTTPS tile fetches of the map engine's online sources (`CONFIG_CP0_LVGL_MAP`, off by default): loaded at run time with `dlopen` only when an app switches the network on, never linked, no curl header needed to build (`src/map/cp0_map_http.c`, the pattern of the Home Assistant app); without it the sources say "network unavailable: library missing (install libcurl4t64)"; apps with online sources list `libcurl4t64, ca-certificates` in their `depends` | curl licence, MIT/X derivative (<https://curl.se/docs/copyright.html>) |
| Map tiles of the online sources (OpenTopoMap, Thunderforest, Geoapify) | Fetched only for the tiles a user views, after the user switches a source on (and gives it their own key where needed); the attribution each provider asks for is drawn on the map (`docs/cp0_lvgl.en.md`, "Online sources") | Map data ODbL (OpenStreetMap contributors); OpenTopoMap map style CC-BY-SA; Thunderforest and Geoapify per their terms; OpenMapTiles design CC-BY |

Packaging must preserve notices for the selected configuration, including static libraries, fonts and audio. SimpleBLE restrictions must be checked against the shipped artifact; this source inventory does not resolve that release-specific question (libv4l, which this sentence also named, went with CameraApp on 2026-10-22: no build of ours links it). The Settings menu tree used the GPL-3.0-only `tree.hh` (Kasper Peeters) until 2026-10-13; it is now the project's own MIT `ext_components/cp0_lvgl/include/cp0_tree.hpp`.

## Removed on 2026-10-22

Report 029 removed the upstream code CyberdeckZero does not use. These components went with it; their licence
texts stay in [`LICENSES/`](../LICENSES/) and the git history keeps the code:

| Component | Was used by | Licence text |
| --- | --- | --- |
| RadioLib | the LoRa service of cp0_lvgl (CardputerZero LoRa HAT) | MIT, Jan Gromeš (wrapper `ext_components/RadioLib`, in the history) |
| smooth_ui_toolkit, spdlog | optional wrappers no build enabled | MIT (wrappers `ext_components/SmoothUI`, `Spdlog`, in the history) |
| nmtui, bluectl | M5Stack components no build used | MIT, M5Stack (in the history) |
| nlohmann/json (emulator copy) | the CardputerZero emulator | [MIT](../LICENSES/nlohmann-json-MIT.txt) |
| tinyalsa | the stock Calculator submodule | [BSD-3-Clause](../LICENSES/tinyalsa-BSD-3-Clause.txt) |
| libv4l / libv4lconvert | the CameraApp project | [package copyright](../LICENSES/libv4l-1.32.0-copyright.txt) |
| Noto Sans SC generated fonts | the Keyboard Guide inside LaunchWizard | OFL-1.1 (in the history) |
