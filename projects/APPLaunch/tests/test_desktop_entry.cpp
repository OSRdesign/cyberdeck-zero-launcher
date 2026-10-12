/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "desktop_entry.h"

#include <cassert>
#include <string>

int main()
{
    const auto entry = parse_desktop_entry(
        "[Other]\nName=Ignored\n[Desktop Entry]\r\n"
        " Name = Demo App \r\nIcon = demo.png\n"
        "Exec=/usr/bin/demo --flag=value\nTerminal=True\nSysplause=0\n");
    assert(entry);
    assert(entry->name == "Demo App");
    assert(entry->icon == "demo.png");
    assert(entry->exec == "/usr/bin/demo --flag=value");
    assert(entry->terminal);
    assert(!entry->sysplause);
    assert(!parse_desktop_entry("[Desktop Entry]\nName=Missing Exec\n"));
    assert(!parse_desktop_entry("[Desktop Entry]\nExec=missing-name\n"));

    const auto defaults = parse_desktop_entry("[Desktop Entry]\nName=A\nExec=a\n");
    assert(defaults && !defaults->terminal && defaults->sysplause);

    assert(desktop_entry_filename_valid("demo.desktop"));
    assert(!desktop_entry_filename_valid(".desktop"));
    assert(!desktop_entry_filename_valid("../demo.desktop"));
    assert(!desktop_entry_filename_valid("sub\\demo.desktop"));
    assert(!desktop_entry_filename_valid("bad\n.desktop"));

    assert(!parse_desktop_entry(
        "[Desktop Entry]\nType=Link\nName=A\nExec=python3\n"));
    assert(!parse_desktop_entry(
        "[Desktop Entry]\nHidden=true\nName=A\nExec=python3\n"));
    assert(!parse_desktop_entry(
        "[Desktop Entry]\nNoDisplay=1\nName=A\nExec=python3\n"));
    std::string nul_exec = "[Desktop Entry]\nName=A\nExec=python3";
    nul_exec.push_back('\0');
    nul_exec += " --unsafe\n";
    assert(!parse_desktop_entry(nul_exec));
    assert(!parse_desktop_entry("[Desktop Entry]\nName=Bad\tName\nExec=python3\n"));
    assert(!parse_desktop_entry(
        "[Desktop Entry]\nName=" +
        std::string(DESKTOP_ENTRY_MAX_NAME_BYTES + 1, 'a') + "\nExec=python3\n"));

    // X-MinSize (app contract 1)
    assert(defaults && defaults->min_w == 0 && defaults->min_h == 0);
    const auto sized = parse_desktop_entry("[Desktop Entry]\nName=A\nExec=a\nX-MinSize = 640x480 \n");
    assert(sized && sized->min_w == 640 && sized->min_h == 480);
    const auto upper = parse_desktop_entry("[Desktop Entry]\nName=A\nExec=a\nX-MinSize=800X600\n");
    assert(upper && upper->min_w == 800 && upper->min_h == 600);
    const auto garbage = parse_desktop_entry("[Desktop Entry]\nName=A\nExec=a\nX-MinSize=big\n");
    assert(garbage && garbage->min_w == 0 && garbage->min_h == 0);
    const auto later_garbage = parse_desktop_entry(
        "[Desktop Entry]\nName=A\nExec=a\nX-MinSize=640x480\nX-MinSize=640*480\n");
    assert(later_garbage && later_garbage->min_w == 0 && later_garbage->min_h == 0);

    int w = 7;
    int h = 9;
    assert(parse_desktop_min_size("480x320", w, h) && w == 480 && h == 320);
    assert(parse_desktop_min_size(" 720 x 720 ", w, h) && w == 720 && h == 720);
    assert(parse_desktop_min_size("1x65535", w, h) && w == 1 && h == 65535);
    w = 7;
    h = 9;
    for (const char *bad : {"", "x", "640", "640x", "x480", "0x480", "640x0", "-640x480", "640x480x2",
                            "640,480", "640x480px", "65536x480", "6.4x4.8", "0640x480 trailing", "640 480"})
        assert(!parse_desktop_min_size(bad, w, h) && w == 7 && h == 9);

    // Shown / hidden: smaller in either dimension hides; no metrics or no restriction shows.
    assert(desktop_min_size_fits(640, 480, true, 640, 480));
    assert(desktop_min_size_fits(640, 480, true, 1280, 720));
    assert(!desktop_min_size_fits(640, 480, true, 480, 320));
    assert(!desktop_min_size_fits(640, 480, true, 720, 400));
    assert(!desktop_min_size_fits(640, 480, true, 600, 720));
    assert(desktop_min_size_fits(640, 480, false, 0, 0));
    assert(desktop_min_size_fits(640, 480, false, 320, 170));
    assert(desktop_min_size_fits(0, 0, true, 320, 170));
}
