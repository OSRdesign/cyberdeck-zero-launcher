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

    // X-Background (task 015): kept as written, trimmed; absent = empty
    assert(defaults && defaults->background.empty());
    const auto frozen = parse_desktop_entry("[Desktop Entry]\nName=A\nExec=a\nX-Fullscreen=true\nX-Background = freeze \n");
    assert(frozen && frozen->background == "freeze" && frozen->fullscreen);
    const auto running = parse_desktop_entry("[Desktop Entry]\nName=A\nExec=a\nX-Background=run\n");
    assert(running && running->background == "run");
    const auto other_group = parse_desktop_entry("[Desktop Entry]\nName=A\nExec=a\n[Extra]\nX-Background=run\n");
    assert(other_group && other_group->background.empty());

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

    // X-Portrait (report 038 S2, the PDA profile): the same WxH syntax; absent or malformed = landscape only
    const auto portrait = parse_desktop_entry(
        "[Desktop Entry]\nName=A\nExec=a\nX-Fullscreen=true\nX-MinSize=640x480\nX-Portrait = 480x420 \n");
    assert(portrait && portrait->portrait_w == 480 && portrait->portrait_h == 420);
    assert(portrait->min_w == 640 && portrait->min_h == 480); // X-MinSize keeps its landscape meaning
    const auto portrait_upper = parse_desktop_entry("[Desktop Entry]\nName=A\nExec=a\nX-Portrait=480X800\n");
    assert(portrait_upper && portrait_upper->portrait_w == 480 && portrait_upper->portrait_h == 800);
    assert(defaults->portrait_w == 0 && defaults->portrait_h == 0);
    for (const char *bad : {"480", "480x", "x420", "0x420", "480x0", "480x420x1", "4 8 0x420", "-480x420", "480*420"}) {
        const auto e = parse_desktop_entry(std::string("[Desktop Entry]\nName=A\nExec=a\nX-Portrait=") + bad + "\n");
        assert(e && e->portrait_w == 0 && e->portrait_h == 0);
    }
    const auto portrait_last = parse_desktop_entry(
        "[Desktop Entry]\nName=A\nExec=a\nX-Portrait=480x420\nX-Portrait=wide\n");
    assert(portrait_last && portrait_last->portrait_w == 0); // the last line wins, as for X-MinSize

    // the launch rule: landscape apps turned on a portrait launcher, X-MinSize against the landscape canvas
    assert(!desktop_portrait_screen(640, 480) && !desktop_portrait_screen(720, 720) && !desktop_portrait_screen(0, 0));
    assert(desktop_portrait_screen(480, 640) && desktop_portrait_screen(480, 800) && desktop_portrait_screen(320, 480));
    DesktopEntry full;
    full.fullscreen = true;
    full.min_w = 640;
    full.min_h = 480;
    DesktopEntry compat = full;
    compat.fullscreen = false;
    DesktopEntry portrait_app = full;
    portrait_app.portrait_w = 480;
    portrait_app.portrait_h = 420;
    DesktopEntry no_min = full;
    no_min.min_w = no_min.min_h = 0;
    // a landscape or square launcher: as before (X-MinSize against the canvas), X-Portrait ignored
    assert(desktop_fit(full, true, 640, 480) == DesktopFit::Native);
    assert(desktop_fit(full, true, 800, 480) == DesktopFit::Native);
    assert(desktop_fit(full, true, 480, 320) == DesktopFit::Hidden);
    assert(desktop_fit(portrait_app, true, 480, 320) == DesktopFit::Hidden);
    assert(desktop_fit(full, true, 720, 720) == DesktopFit::Native);
    assert(desktop_fit(compat, true, 480, 320) == DesktopFit::Hidden);
    assert(desktop_fit(full, false, 0, 0) == DesktopFit::Native); // no metrics (stock / SDL): shown, as before
    // a portrait launcher, landscape app: turned when X-MinSize fits the panel seen in landscape
    assert(desktop_fit(full, true, 480, 800) == DesktopFit::Turned);  // 800x480 >= 640x480
    assert(desktop_fit(full, true, 480, 640) == DesktopFit::Turned);  // 640x480 >= 640x480
    assert(desktop_fit(full, true, 480, 600) == DesktopFit::Hidden);  // 600x480 < 640x480
    assert(desktop_fit(full, true, 320, 480) == DesktopFit::Hidden);  // 480x320 < 640x480
    assert(desktop_fit(no_min, true, 480, 800) == DesktopFit::Turned); // no minimum: still landscape only, turned
    assert(desktop_fit(no_min, true, 320, 480) == DesktopFit::Turned);
    // ... an app with X-Portrait: native (the portrait canvas) when it fits, else turned, else hidden
    assert(desktop_fit(portrait_app, true, 480, 800) == DesktopFit::Native);
    assert(desktop_fit(portrait_app, true, 480, 640) == DesktopFit::Native);
    DesktopEntry tall = portrait_app;
    tall.portrait_h = 700;
    assert(desktop_fit(tall, true, 480, 800) == DesktopFit::Native);
    assert(desktop_fit(tall, true, 480, 640) == DesktopFit::Turned); // 480x640 < 480x700: turned 640x480
    DesktopEntry tall_big = tall;
    tall_big.min_w = 800;
    assert(desktop_fit(tall_big, true, 480, 640) == DesktopFit::Hidden); // neither canvas fits
    // ... a compat app is never turned; a portrait launcher hides one that is not a terminal (fixed-pixel: LAN Scan,
    // Wi-Fi Survey, MeshZero), whatever its X-MinSize (report 040)
    assert(desktop_fit(compat, true, 480, 800) == DesktopFit::Hidden);
    DesktopEntry compat_small = compat;
    compat_small.min_w = 320;
    compat_small.min_h = 170;
    assert(desktop_fit(compat_small, true, 480, 800) == DesktopFit::Hidden);
    assert(desktop_fit(compat_small, true, 480, 640) == DesktopFit::Hidden);
    DesktopEntry compat_none = compat;
    compat_none.min_w = compat_none.min_h = 0;
    assert(desktop_fit(compat_none, true, 320, 480) == DesktopFit::Hidden);
    // a terminal app stays (it runs in the launcher's terminal page): X-MinSize against the canvas, as before
    DesktopEntry term = compat_small;
    term.terminal = true;
    assert(desktop_fit(term, true, 480, 800) == DesktopFit::Native);
    DesktopEntry term_big = term;
    term_big.min_w = 640;
    term_big.min_h = 480;
    assert(desktop_fit(term_big, true, 480, 800) == DesktopFit::Hidden);
    // a fullscreen app is not affected (turned / native as above); landscape, square and no metrics neither
    assert(desktop_fit(full, true, 480, 800) == DesktopFit::Turned);
    assert(desktop_fit(compat_small, true, 800, 480) == DesktopFit::Native);
    assert(desktop_fit(compat_small, true, 720, 720) == DesktopFit::Native);
    assert(desktop_fit(compat_none, true, 640, 480) == DesktopFit::Native);
    assert(desktop_fit(compat_small, false, 0, 0) == DesktopFit::Native);
}
