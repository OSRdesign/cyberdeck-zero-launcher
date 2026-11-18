/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Screen > Idle dim level, Terminal text size, Screen size (2026-11-18, main/ui/model/screen_prefs_model.hpp):
 * the stored values, the precedence rules, the panel size from a diagonal, and install.sh's own screen_mm() against it.
 *
 * Usage: test_screen_prefs_model PATH/TO/install.sh   (the parity test extracts install.sh's functions and runs them
 * with sh and awk; without the argument that one test is skipped and says so)
 */

#include "../main/ui/model/screen_prefs_model.hpp"
#include "../main/ui/model/st_viewport_model.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace screen_prefs;

static std::string shell(const std::string &command)
{
    std::string out;
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) return "popen failed";
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) out += buffer;
    pclose(pipe);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

int main(int argc, char **argv)
{
    // ---- idle dim level: steps, labels, precedence (setting > profile > 10)
    assert(kDimStepCount == 5);
    assert(kDimSteps[0] == 5 && kDimSteps[1] == 10 && kDimSteps[2] == 20 && kDimSteps[3] == 30 && kDimSteps[4] == 50);
    assert(dim_label(5) == "5 %" && dim_label(50) == "50 %");
    assert(dim_default_label(10) == "Default (10 %)");
    assert(dim_default_label(20) == "Default (20 %)");
    assert(dim_effective(10, 0) == 10);   /* Default: the profile's level */
    assert(dim_effective(20, 0) == 20);
    assert(dim_effective(20, 5) == 5);    /* the setting overrides the profile */
    assert(dim_effective(10, 50) == 50);
    assert(dim_effective(20, -1) == 20 && dim_effective(20, 101) == 20); /* unusable: no override */
    assert((dim_choices() == std::vector<int>{0, 5, 10, 20, 30, 50}));
    assert(dim_row_of(0) == 0 && dim_row_of(5) == 1 && dim_row_of(50) == 5 && dim_row_of(20) == 3);
    assert(dim_row_of(15) == -1);         /* a hand-edited level that is not a step */
    assert((dim_choices_with(15) == std::vector<int>{0, 5, 10, 20, 30, 50, 15}));
    assert((dim_choices_with(20) == dim_choices()));

    // ---- terminal text size: choices, labels, rows, Auto = board.conf else the display default
    assert(kTermSizeCount == 4);
    assert((term_choices() == std::vector<int>{0, 14, 17, 20, 24}));
    assert(term_label(0, 20) == "Auto (20 px)" && term_label(0, 14) == "Auto (14 px)");
    assert(term_label(14, 20) == "Small 14 px" && term_label(17, 20) == "Medium 17 px");
    assert(term_label(20, 14) == "Large 20 px" && term_label(24, 14) == "XL 24 px");
    assert(term_label(30, 14) == "Custom 30 px");
    assert(grid_text(57, 24) == "57 x 24");
    assert(term_row_of(0) == 0 && term_row_of(14) == 1 && term_row_of(24) == 4 && term_row_of(30) == -1);
    {
        // the full precedence: profile (the display default) < board.conf (APPLAUNCH_TERM_FONT) < the setting
        using st_viewport::font_px_resolved;
        assert(font_px_resolved(480, 800, 0, nullptr) == 20);
        assert(font_px_resolved(480, 800, 0, "17") == 17);
        assert(font_px_resolved(480, 800, 14, "17") == 14);
        // the grid each choice shows on a 480x800 screen with the real JetBrains Mono Bold metrics the terminal
        // measures (advance 0.6 em, line 1.32 em, rounded: 14 px = 8x18, 17 px = 10x22, 20 px = 12x26, 24 px = 14x32)
        assert(st_viewport::grid_cols(480, 8, st_viewport::kMaxCols) == 60);
        assert(st_viewport::grid_cols(480, 10, st_viewport::kMaxCols) == 48);
        assert(st_viewport::grid_cols(480, 12, st_viewport::kMaxCols) == 40);
        assert(st_viewport::grid_cols(480, 14, st_viewport::kMaxCols) == 34);
        assert(st_viewport::grid_rows(800, 32, st_viewport::kMaxRows) == 24); /* the terminal never has more than 24 rows */
    }

    // ---- screen size: the list, validation, mm
    assert((diagonals() == std::vector<std::string>{"2.8", "3.5", "4.3", "5", "5.5", "7", "8", "10.1"}));
    for (const char *ok : {"1.5", "2.8", "15", "10.1", "5", "5.0", "3.50"}) assert(diag_valid(ok));
    for (const char *bad : {"", "1.4", "15.1", "abc", "5.", ".5", "5.5.5", "-5", "5 ", " 5", "5in", "1e1", "0"})
        assert(!diag_valid(bad));
    assert(mm_valid("154x86") && mm_valid("20x20") && mm_valid("500x500") && mm_valid("94x56"));
    for (const char *bad : {"", "154", "x86", "154x", "19x50", "501x50", "154x86x1", "154 x86", "1a4x86", "-1x50"})
        assert(!mm_valid(bad));
    assert(panel_mm("7", 800, 480) == "154x86");   /* the sizes the profiles know */
    assert(panel_mm("5", 800, 480) == "108x65");
    assert(panel_mm("4.3", 800, 480) == "94x56");
    assert(panel_mm("3.5", 480, 320) == "79x49");
    assert(panel_mm("7", 480, 800) == "154x86");   /* the canvas may be portrait: the landscape pair, larger side first */
    assert(panel_mm("3.5", 320, 480) == "79x49");
    assert(panel_mm("2.8", 640, 480) == "57x43");  /* the formula: 640 * 2.8 * 25.4 / 800 = 56.9, 480 -> 42.7 */
    assert(panel_mm("7", 720, 720).substr(0, 3) == "126"); /* a square canvas: d * 25.4 / sqrt(2) = 125.7 */
    assert(panel_mm("", 800, 480).empty() && panel_mm("abc", 800, 480).empty() && panel_mm("7", 0, 480).empty());
    assert(diag_label("7") == "7 in" && diag_label("10.1") == "10.1 in");

    // size source and what is applied at start
    assert(installer_size_from_conf("keyboard_name=x\nscreen_size=7\npanel_mm=\nboot_config=1\n") == "7 in");
    assert(installer_size_from_conf("screen_size=\npanel_mm=108x65\n") == "108x65 mm");
    assert(installer_size_from_conf("screen_size=\npanel_mm=\n").empty());
    assert(installer_size_from_conf("keyboard_name=x\r\nscreen_size=4.3\r\n") == "4.3 in");
    assert(installer_size_from_conf("screen_size=junk\npanel_mm=junk\n").empty());
    assert(installer_size_from_conf("").empty());
    assert(size_source("154x86", "7 in") == SizeSource::Setting); /* a setting wins over the installer's size */
    assert(size_source("", "7 in") == SizeSource::Installer);
    assert(size_source("", "") == SizeSource::Auto);
    assert(size_source("junk", "") == SizeSource::Auto);
    assert(std::string(source_name(SizeSource::Setting)) == "setting");
    assert(panel_mm_to_apply("154x86") == "154x86");                   /* the setting is applied over board.conf */
    assert(panel_mm_to_apply("").empty() && panel_mm_to_apply("junk").empty()); /* none: board.conf stays */
    assert(size_row_of("", "") == 0 && size_row_of("7", "154x86") == 6 && size_row_of("2.8", "57x43") == 1);
    assert(size_row_of("7.0", "154x86") == 6);   /* the same diagonal written another way */
    assert(size_row_of("6", "130x73") == -1);    /* valid but not in the list */
    assert(size_row_of("7", "") == 0 && size_row_of("", "154x86") == 0);

    // ---- the stored values (config.json)
    {
        Stored s = stored_from_json("{\"idle_dim_percent\":20,\"terminal_font_px\":17,\"screen_diag\":\"7\",\"screen_mm\":\"154x86\"}");
        assert(s.dim == 20 && s.terminal == 17 && s.diag == "7" && s.mm == "154x86");
        s = stored_from_json("{}");
        assert(s.dim == 0 && s.terminal == 0 && s.diag.empty() && s.mm.empty());
        s = stored_from_json("");
        assert(s.dim == 0 && s.terminal == 0);
        s = stored_from_json("not json");
        assert(s.dim == 0);
        s = stored_from_json("{\"idle_dim_percent\":0,\"terminal_font_px\":0,\"screen_diag\":\"\",\"screen_mm\":\"\"}");
        assert(s.dim == 0 && s.terminal == 0 && s.diag.empty() && s.mm.empty()); /* Default / Auto written back */
        s = stored_from_json("{\"idle_dim_percent\":101,\"terminal_font_px\":9}");
        assert(s.dim == 0 && s.terminal == 0);                                   /* out of range: no override */
        s = stored_from_json("{\"idle_dim_percent\":\"abc\",\"terminal_font_px\":\"x\"}");
        assert(s.dim == 0 && s.terminal == 0);
        s = stored_from_json("{\"screen_diag\":\"7\"}");
        assert(s.diag.empty() && s.mm.empty());                                  /* half a size is no size */
        s = stored_from_json("{\"screen_mm\":\"154x86\"}");
        assert(s.diag.empty() && s.mm.empty());
        s = stored_from_json("{\"screen_diag\":\"99\",\"screen_mm\":\"154x86\"}");
        assert(s.diag.empty() && s.mm.empty());
        s = stored_from_json("{\"launcher\":{\"x\":1},\"idle_dim_percent\":5}"); /* other keys do not matter */
        assert(s.dim == 5);
    }

    // ---- install.sh and the launcher compute the same millimetres
    if (argc < 2) {
        std::printf("SKIP install.sh parity (pass the path of install.sh)\n");
        std::printf("test_screen_prefs_model: ok\n");
        return 0;
    }
    const std::string install = argv[1];
    const std::string tmp = "/tmp/screen_prefs_parity." + std::to_string(static_cast<long>(std::rand())) + ".sh";
    const std::string extract = "sed -n '/^screen_mm() {/,/^}/p;/^valid_size() {/,/^}/p;/^valid_mm() {/,/^}/p' '" + install + "' > " + tmp;
    assert(std::system(extract.c_str()) == 0);
    assert(!shell("grep -c '^screen_mm()' " + tmp).empty() && shell("grep -c '^screen_mm()' " + tmp) == "1");
    const std::vector<std::pair<int, int>> canvases{{800, 480}, {480, 800}, {480, 320}, {320, 480}, {640, 480}, {480, 640},
                                                    {720, 720}, {1280, 720}, {1280, 800}, {1024, 600}, {320, 170}, {854, 480}};
    std::vector<std::string> sizes = diagonals();
    for (const char *extra : {"1.5", "15", "3.50", "5.0", "4.30", "12.3", "9.7", "6.5", "2.4"}) sizes.push_back(extra);
    int compared = 0;
    for (const std::string &diag : sizes) {
        for (const auto &canvas : canvases) {
            const std::string want = shell(". " + tmp + "; screen_mm " + diag + " " + std::to_string(canvas.first) + " " +
                                           std::to_string(canvas.second));
            const std::string got = panel_mm(diag, canvas.first, canvas.second);
            if (want != got) {
                std::fprintf(stderr, "screen_mm %s %dx%d: install.sh %s, launcher %s\n", diag.c_str(), canvas.first,
                             canvas.second, want.c_str(), got.c_str());
                return 1;
            }
            ++compared;
        }
        // the validation too
        const bool want_valid = std::system((". " + tmp + "; valid_size " + diag).c_str()) == 0;
        assert(want_valid == diag_valid(diag));
    }
    for (const char *text : {"", "1.4", "15.1", "5.", ".5", "5.5.5", "abc", "7", "1.5", "15", "0"}) {
        const bool want = std::system((". " + tmp + "; valid_size '" + text + "'").c_str()) == 0;
        if (want != diag_valid(text)) {
            std::fprintf(stderr, "valid_size '%s': install.sh %d, launcher %d\n", text, want, diag_valid(text));
            return 1;
        }
    }
    for (const char *text : {"", "154", "154x86", "20x20", "19x50", "500x500", "501x50", "1a4x86", "154x86x1", "x86", "154x"}) {
        const bool want = std::system((". " + tmp + "; valid_mm '" + text + "'").c_str()) == 0;
        if (want != mm_valid(text)) {
            std::fprintf(stderr, "valid_mm '%s': install.sh %d, launcher %d\n", text, want, mm_valid(text));
            return 1;
        }
    }
    std::remove(tmp.c_str());
    std::printf("install.sh parity: %d screen_mm results identical\n", compared);
    std::printf("test_screen_prefs_model: ok\n");
    return 0;
}
