# License text provenance

This directory stores third-party notices alongside the repository's
M5Stack MIT license. It does not relicense dependencies. The usage and
selection inventory is in [OPEN_SOURCE_COMPONENTS.md](../docs/OPEN_SOURCE_COMPONENTS.md).

| Archived text | Original in this checkout |
| --- | --- |
| M5Stack-SDK-MIT.txt | SDK/LICENSE |
| LVGL-MIT.txt | SDK/github_source/lvgl/lvgl_9_5/lvgl/LICENCE.txt |
| eventpp-Apache-2.0.txt | SDK/github_source/eventpp/license |
| libhv-BSD-3-Clause.txt | SDK/github_source/libhv/LICENSE |
| fmt-MIT.txt | SDK/components/utilities/party/fmt/LICENSE.rst |
| cJSON-MIT.txt | SDK/github_source/cJSON/LICENSE |
| C-Thread-Pool-MIT.txt | SDK/github_source/C-Thread-Pool/LICENSE |
| cpp-httplib-MIT.txt | SDK/github_source/cpp-httplib/LICENSE |
| tinyalsa-BSD-3-Clause.txt | SDK/github_source/tinyalsa/NOTICE; its LICENSE file refers to NOTICE |
| SimpleBLE-BUSL-1.1.md | SDK/github_source/SimpleBLE/LICENSE.md; cached source, not established as linked |
| nlohmann-json-MIT.txt | Standard MIT text with 2013–2023 Niels Lohmann attribution from the emulator's vendor/nlohmann/json.hpp |
| stb_image-MIT-or-Unlicense.txt | The licence section at the end of ext_components/cp0_lvgl/third_party/stb/stb_image.h (v2.30; MIT or public domain) |
| libv4l-host-copyright.txt | /usr/share/doc/libv4l-0t64/copyright on the audit host; reference only, not claimed to match CameraApp's target package version |
| libv4l-1.32.0-copyright.txt | usr/share/doc/libv4l-0t64/copyright from Ubuntu libv4l-0t64_1.32.0-2ubuntu1_arm64.deb, matching CameraApp's libv4l-dev package version |
| ../projects/APPLaunch/APPLaunch/share/font/DejaVu-LICENSE.txt | LICENSE of github.com/dejavu-fonts/dejavu-fonts at tag version_2_37 (`https://raw.githubusercontent.com/dejavu-fonts/dejavu-fonts/version_2_37/LICENSE`), identical to tag version_2_36; the shipped files are Sans 2.35 and Sans Mono / Serif 2.37, and the name table of DejaVuSans.ttf (licence record, name ID 13) begins with the same text |
| ../projects/APPLaunch/APPLaunch/share/font/LiberationMono-LICENSE.txt | LICENSE of github.com/liberationfonts/liberation-fonts at tag 2.1.5 (`https://raw.githubusercontent.com/liberationfonts/liberation-fonts/2.1.5/LICENSE`); the shipped file is Liberation Mono 2.1.5 (name table: SIL OFL 1.1) |
| ../projects/APPLaunch/APPLaunch/share/font/Montserrat-OFL.txt | OFL.txt of github.com/JulietaUla/Montserrat at tag v7.200 (`https://raw.githubusercontent.com/JulietaUla/Montserrat/v7.200/OFL.txt`), identical to the Montserrat OFL.txt in LVGL's `scripts/built_in_font/font_license/`; preceded by a short note of the copyright lines in the two shipped files (Medium 7.200, Bold 2.001). Upstream has no OFL.txt with the Bold file's 2011-2012 line |
| ../projects/APPLaunch/APPLaunch/share/font/NotoSansCJK-OFL.txt | LICENSE.txt of github.com/adobe-fonts/source-han-sans at tag 2.004R (`https://raw.githubusercontent.com/adobe-fonts/source-han-sans/2.004R/LICENSE.txt`), the upstream design of Noto Sans CJK 2.004 and the copy that carries the copyright line 2014-2021 Adobe; the text after that line equals Sans/LICENSE of github.com/googlefonts/noto-cjk (`https://raw.githubusercontent.com/googlefonts/noto-cjk/main/Sans/LICENSE`), which has no copyright line |
| ../projects/APPLaunch/APPLaunch/share/font/FontAwesome-LICENSE.txt | LICENSE.txt of github.com/FortAwesome/Font-Awesome at tag 5.9.0 (`https://raw.githubusercontent.com/FortAwesome/Font-Awesome/5.9.0/LICENSE.txt`), which only summarizes the licences; the full OFL 1.1 text is lines 33-117 of LICENSE.txt on the 6.x branch (`https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/LICENSE.txt`). Only the font file ships |
| GPL-2.0.txt, LGPL-2.0.txt, LGPL-2.1.txt | Full standard texts from /usr/share/common-licenses/GPL-2, LGPL-2 and LGPL-2.1, referenced by the v4l-utils copyright record |

The target libv4l copyright was fetched from
`https://ports.ubuntu.com/ubuntu-ports/pool/main/v/v4l-utils/libv4l-0t64_1.32.0-2ubuntu1_arm64.deb`
through the configured proxy without installing or executing the package.
Package metadata confirms `libv4l-0t64`, version `1.32.0-2ubuntu1`, `arm64`;
SHA-256 is `9a3a5c89c2b367d04734bc07ce285ed559228373cda1eef038c24da1d9c2098a`.
The development package's documentation is a symlink to this runtime package.

Wrapper-local LICENSE files for RadioLib, Sigslot, SmoothUI and Spdlog were
copied from their corresponding SDK/github_source trees. Miniaudio/LICENSE
is the complete dual-license block extracted from its single header.
APPLaunch's local LVGL directory contains copies of the LVGL and FreeType
notices. Keyboard Guide's NotoSans/OFL.txt comes from LVGL's
scripts/built_in_font/font_license/NotoSansSC/OFL.txt; the input font's
embedded metadata confirms the same Adobe copyright and OFL-1.1 license.

Retain original per-file notices and any nested dependency licenses. Recheck
these copies whenever dependency revisions change, especially unpinned downloads.

JetBrainsMono-OFL.txt (next to the fonts) predates this table. The font licence files above are copied by `build.sh` into
/usr/share/doc/cyberdeckzero-launcher/fonts/ because their names match `*OFL*` or `*LICENSE*`. Alibaba PuHuiTi's official legal notice is
AlibabaPuHuiTi-LICENSE.txt (see OPEN_SOURCE_COMPONENTS.md). `lockscreen.png` is an upstream M5Stack
launcher asset under M5Stack-SDK-MIT.txt; the origin of the image itself has not been verified independently.
`svgfont.ttf` was removed (unused, no licence information; history in git).

Note of 2026-10-22 (report 029): the emulator, CameraApp, LaunchWizard (with Keyboard Guide), the stock
Calculator submodule and the RadioLib, SmoothUI and Spdlog wrappers were removed from this repository. The texts
above that came from them stay here unchanged; the paths named in the table refer to the git history.
