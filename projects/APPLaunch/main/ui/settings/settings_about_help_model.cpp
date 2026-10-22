/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_about_help_model.hpp"

#include <cstddef>

namespace settings_t12b::about_help {
namespace {

std::string value_or_unknown(std::string_view value)
{
    return value.empty() ? "unknown" : std::string(value);
}


std::vector<std::string> split_lines(std::string_view text)
{
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find('\n', begin);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(begin));
            break;
        }
        lines.emplace_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return lines;
}

} // namespace

Content about(std::string_view version,
              std::string_view build_date,
              std::string_view channel,
              std::string_view commit,
              std::string_view board)
{
    Content content{
        "About",
        {
            "CyberdeckZero Launcher",
            "LVGL: 9.x",
            "Version: " + value_or_unknown(version),
            "Build: " + value_or_unknown(build_date),
            "Channel: " + value_or_unknown(channel),
            "Commit: " + value_or_unknown(commit),
            // the disclaimer, short form (the full text is on the Licenses page)
            "Independent hobby project",
            "Not affiliated with M5Stack",
        },
    };
    if (!board.empty())
        content.lines.insert(content.lines.begin() + 1, "Board: " + std::string(board));
    return content;
}

Content credit()
{
    return {
        "Third-Party Licenses & Credits",
        split_lines(R"CP0_LICENSES(CyberdeckZero Launcher
A touch launcher for Raspberry Pi cyberdecks (Pi Zero 2 W, Pi 3A+ and other boards with a capacitive screen).
Raspberry Pi port, responsive shell and native Settings by OSRdesign, written together with Claude (Anthropic).
Licence: MIT, see LICENSE in the source.
Source: github.com/OSRdesign/cyberdeck-zero-launcher

CyberdeckZero Launcher is an independent hobby project, derived from the MIT-licensed CardputerZero launcher by M5Stack Technology CO LTD, and bundles M5Stack's CardputerZero Store unchanged. It is not affiliated with, sponsored or endorsed by M5Stack.

Derived from
The CardputerZero launcher by M5Stack Technology CO LTD
https://github.com/CardputerZero/launcher
MIT, Copyright (c) 2026 M5Stack Technology CO LTD

Bundled
CardputerZero Store by M5Stack, unchanged
https://github.com/CardputerZero/Store
MIT; it also uses libhv (BSD-3-Clause), nlohmann/json (MIT) and OpenSSL 3 (Apache-2.0)

Upstream
Our sincere thanks to all Kickstarter backers who helped make CardputerZero possible.

We also thank the early community contributors who helped shape CardputerZero: eggfly (Factory Firmware), zhuzhe1983 (Store), whywilson (RFID), JdaieLin (XiaoZhi), vicliu624 (LoFiBox-Zero), doodlewind (PocketJS), 7h30th3r0n3 (RaspyJack), bmorcelli (ESP Flasher), geo-tp (Retro-Zero), andreahaku IU4APC (Radio), n0xa (czconsole), marivaaldo (Flint), YUK_KND (ZeroTop), hishizuka (Bikecomputer), NDenchi (StackChan-Zero), GOROman.

- Software in this launcher:

M5Stack Linux Libs (build framework)
https://github.com/m5stack/M5Stack_Linux_Libs
MIT

LVGL 9.5
https://github.com/lvgl/lvgl/tree/v9.5.0
MIT; includes ThorVG (MIT), LodePNG (Zlib) and AnimatedGIF (Apache-2.0)

FreeType
https://gitlab.freedesktop.org/freetype/freetype
FTL OR GPL-2.0-only

libinput
https://gitlab.freedesktop.org/libinput/libinput
MIT

libxkbcommon
https://github.com/xkbcommon/libxkbcommon
MIT

GLib and GIO
https://gitlab.gnome.org/GNOME/glib
LGPL-2.1-or-later

eventpp
https://github.com/wqking/eventpp
Apache-2.0

miniaudio 0.11.25
https://github.com/mackron/miniaudio/tree/0.11.25
Unlicense OR MIT-0

- Fonts:

Montserrat
https://github.com/JulietaUla/Montserrat
SIL OFL-1.1

Font Awesome 5 Free
https://fontawesome.com
Icons CC BY 4.0, fonts SIL OFL-1.1

DejaVu Sans, Sans Mono and Serif
https://github.com/dejavu-fonts/dejavu-fonts
Bitstream Vera and Arev licenses

Liberation Mono
https://github.com/liberationfonts/liberation-fonts
SIL OFL-1.1

JetBrains Mono
https://github.com/JetBrains/JetBrainsMono
SIL OFL-1.1

Noto Sans CJK and Noto Serif CJK
https://github.com/notofonts
SIL OFL-1.1

Alibaba PuHuiTi 3.0
https://www.alibabafonts.com
Alibaba PuHuiTi Font License Agreement

- Sounds and pictures (as credited by the upstream launcher):

UI SFX 0.4.0
https://github.com/romainsimon/uisfx/tree/v0.4.0
CC0-1.0

Lofoten Aurora lock screen
Johannes Groll
https://commons.wikimedia.org/wiki/File:Lofoten,_Norway_(Unsplash).jpg
CC0-1.0)CP0_LICENSES"),
    };
}

} // namespace settings_t12b::about_help
