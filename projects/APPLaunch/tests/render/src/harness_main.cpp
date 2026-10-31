/* SPDX-License-Identifier: MIT
 *
 * Render harness: headless screenshots of the launcher's native shell and compat pages.
 *
 *   render-harness render <scene file> --out <dir> [--sizes 640x480,480x320]
 *   render-harness compare <expected.png> <actual.png> [--diff out.png] [--tolerance N]
 *                          [--ignore x,y,w,h]...
 *   render-harness sheet <out.png> <title> <a.png> [<b.png> ...]
 *
 * "render" runs one scene script at every size it names, each size in its own forked process
 * (the display manager and LVGL are process-wide): the real display manager
 * (cp0_lvgl_dpi_scaled.c) on a memory framebuffer, the real launcher code, stubbed services and
 * a fake clock. See README.md for the script format.
 */
#include "harness.h"
#include "harness_launch.hpp"

#include "lvgl/lvgl.h"

#include <png.h>

#include "app_launch_env.hpp"
#include "commount.h"
#include "cp0/cp0_fb_output.h"
#include "cp0/cp0_lvgl.h"
#include "cp0_display.h"
#include "cp0_sudo_prompt_ui.hpp"
#include "cp0_sudo_prompt_key_policy.hpp"
#include "cp0_ui_metrics.h"
#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_overlay.h"
#include "cp0_zone_ui.h"
#include "input_keys.h"
#include "keyboard_input.h"

#include "launch.h"
#include "launcher_osk.hpp"
#include "launcher_ssh.hpp"
#include "launcher_zone.hpp"
#include "multitask/display_handoff.hpp"
#include "multitask/switcher_model.hpp"
#include "multitask/switcher_view.hpp"
#include "launcher_media_osd.h"
#include "launcher_toast.h"
#include "ui_loading.h"
#include "native_screensaver.hpp"
#include "native_ui.hpp"
#include "cp0_ui_widgets.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

/* ===================================================================== small helpers */

struct Size {
    int w = 0, h = 0;
    std::string name() const { return std::to_string(w) + "x" + std::to_string(h); }
    bool operator==(const Size &o) const { return w == o.w && h == o.h; }
};

bool parse_size(const std::string &text, Size &out)
{
    int w = 0, h = 0;
    char x = 0;
    std::istringstream in(text);
    if (!(in >> w >> x >> h) || (x != 'x' && x != 'X') || w <= 0 || h <= 0) return false;
    out = {w, h};
    return true;
}

std::vector<std::string> split_words(const std::string &line)
{
    std::vector<std::string> words;
    std::string word;
    bool quoted = false, any = false;
    for (char c : line) {
        if (c == '"') {
            quoted = !quoted;
            any = true;
            continue;
        }
        if (!quoted && (c == ' ' || c == '\t')) {
            if (any) words.push_back(word);
            word.clear();
            any = false;
            continue;
        }
        word += c;
        any = true;
    }
    if (any) words.push_back(word);
    return words;
}

void mkdirs(const std::string &path)
{
    std::string partial;
    std::istringstream in(path);
    std::string part;
    if (!path.empty() && path[0] == '/') partial = "/";
    while (std::getline(in, part, '/')) {
        if (part.empty()) continue;
        partial += part + "/";
        mkdir(partial.c_str(), 0755);
    }
}

std::string dirname_of(const std::string &path)
{
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

/* ===================================================================== RGB images + PNG */

struct Image {
    int w = 0, h = 0;
    std::vector<unsigned char> rgb; /* w * h * 3 */
    unsigned char *at(int x, int y) { return &rgb[(static_cast<size_t>(y) * w + x) * 3]; }
    const unsigned char *at(int x, int y) const { return &rgb[(static_cast<size_t>(y) * w + x) * 3]; }
    void create(int width, int height, unsigned char r, unsigned char g, unsigned char b)
    {
        w = width;
        h = height;
        rgb.assign(static_cast<size_t>(w) * h * 3, 0);
        for (size_t i = 0; i < rgb.size(); i += 3) rgb[i] = r, rgb[i + 1] = g, rgb[i + 2] = b;
    }
};

/* PNG through the host libpng (simplified API): 8-bit RGB in and out. */
bool load_png(const std::string &path, Image &image)
{
    png_image png;
    std::memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&png, path.c_str())) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), png.message);
        return false;
    }
    png.format = PNG_FORMAT_RGB;
    image.w = static_cast<int>(png.width);
    image.h = static_cast<int>(png.height);
    image.rgb.assign(PNG_IMAGE_SIZE(png), 0);
    if (!png_image_finish_read(&png, nullptr, image.rgb.data(), 0, nullptr)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), png.message);
        png_image_free(&png);
        return false;
    }
    return true;
}

bool save_png(const std::string &path, const Image &image)
{
    mkdirs(dirname_of(path));
    png_image png;
    std::memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    png.width = static_cast<png_uint_32>(image.w);
    png.height = static_cast<png_uint_32>(image.h);
    png.format = PNG_FORMAT_RGB;
    if (!png_image_write_to_file(&png, path.c_str(), 0, image.rgb.data(), 0, nullptr)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), png.message);
        return false;
    }
    return true;
}

/* A 5x7 bitmap font for labels (contact sheet, stock-app placeholder): digits, A-Z, a few signs. */
const char *glyph(char c)
{
    static const std::map<char, const char *> glyphs = {
        {'0', "01110100011001110101110011000101110"}, {'1', "00100011000010000100001000010001110"},
        {'2', "01110100010000100010001000100011111"}, {'3', "11111000100010000010000011000101110"},
        {'4', "00010001100101010010111110001000010"}, {'5', "11111100001111000001000011000101110"},
        {'6', "00110010001000011110100011000101110"}, {'7', "11111000010001000100010000100001000"},
        {'8', "01110100011000101110100011000101110"}, {'9', "01110100011000101111000010001001100"},
        {'A', "01110100011000111111100011000110001"}, {'B', "11110100011000111110100011000111110"},
        {'C', "01110100011000010000100001000101110"}, {'D', "11100100101000110001100011001011100"},
        {'E', "11111100001000011110100001000011111"}, {'F', "11111100001000011110100001000010000"},
        {'G', "01110100011000010111100011000101111"}, {'H', "10001100011000111111100011000110001"},
        {'I', "01110001000010000100001000010001110"}, {'J', "00111000100001000010000101001001100"},
        {'K', "10001100101010011000101001001010001"}, {'L', "10000100001000010000100001000011111"},
        {'M', "10001110111010110101100011000110001"}, {'N', "10001100011100110101100111000110001"},
        {'O', "01110100011000110001100011000101110"}, {'P', "11110100011000111110100001000010000"},
        {'Q', "01110100011000110001101011001001101"}, {'R', "11110100011000111110101001001010001"},
        {'S', "01111100001000001110000010000111110"}, {'T', "11111001000010000100001000010000100"},
        {'U', "10001100011000110001100011000101110"}, {'V', "10001100011000110001100010101000100"},
        {'W', "10001100011000110101101011010101010"}, {'X', "10001100010101000100010101000110001"},
        {'Y', "10001100011000101010001000010000100"}, {'Z', "11111000010001000100010001000011111"},
        {'x', "00000000001000101010001000101010001"}, {'-', "00000000000000011111000000000000000"},
        {'_', "00000000000000000000000000000011111"}, {'.', "00000000000000000000000000110001100"},
        {':', "00000011000110000000011000110000000"}, {'/', "00000000010001000100010001000000000"},
        {'(', "00010001000100001000010000010000010"}, {')', "01000001000001000010000100010001000"},
        {'+', "00000001000010011111001000010000000"}, {' ', "00000000000000000000000000000000000"},
    };
    const char key = (c >= 'a' && c <= 'z' && c != 'x') ? static_cast<char>(c - 'a' + 'A') : c;
    const auto found = glyphs.find(key);
    return found == glyphs.end() ? glyphs.at('-') : found->second;
}

void draw_text(Image &image, int x, int y, const std::string &text, int scale, unsigned char r,
               unsigned char g, unsigned char b)
{
    for (char c : text) {
        const char *bits = glyph(c);
        for (int gy = 0; gy < 7; ++gy)
            for (int gx = 0; gx < 5; ++gx) {
                if (bits[gy * 5 + gx] != '1') continue;
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx) {
                        const int px = x + gx * scale + sx, py = y + gy * scale + sy;
                        if (px < 0 || py < 0 || px >= image.w || py >= image.h) continue;
                        unsigned char *p = image.at(px, py);
                        p[0] = r, p[1] = g, p[2] = b;
                    }
            }
        x += 6 * scale;
    }
}

int text_width(const std::string &text, int scale)
{
    return static_cast<int>(text.size()) * 6 * scale - scale;
}

/* ===================================================================== compare / sheet modes */

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

int cmd_compare(int argc, char **argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: compare <expected.png> <actual.png> [--diff out.png] [--tolerance N] "
                             "[--ignore x,y,w,h]...\n");
        return 2;
    }
    const std::string expected_path = argv[2], actual_path = argv[3];
    std::string diff_path;
    int tolerance = 0;
    std::vector<Rect> ignore;
    for (int i = 4; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--diff" && i + 1 < argc) diff_path = argv[++i];
        else if (option == "--tolerance" && i + 1 < argc) tolerance = std::atoi(argv[++i]);
        else if (option == "--ignore" && i + 1 < argc) {
            Rect r{};
            if (std::sscanf(argv[++i], "%d,%d,%d,%d", &r.x, &r.y, &r.w, &r.h) != 4) return 2;
            ignore.push_back(r);
        } else {
            std::fprintf(stderr, "compare: unknown option %s\n", option.c_str());
            return 2;
        }
    }
    Image expected, actual;
    if (!load_png(expected_path, expected)) {
        std::fprintf(stderr, "compare: cannot read %s\n", expected_path.c_str());
        return 2;
    }
    if (!load_png(actual_path, actual)) {
        std::fprintf(stderr, "compare: cannot read %s\n", actual_path.c_str());
        return 2;
    }
    if (expected.w != actual.w || expected.h != actual.h) {
        std::printf("DIFF size %dx%d (expected) vs %dx%d (actual): %s\n", expected.w, expected.h, actual.w,
                    actual.h, actual_path.c_str());
        return 1;
    }
    long differing = 0, ignored = 0;
    int max_delta = 0, x0 = expected.w, y0 = expected.h, x1 = -1, y1 = -1;
    Image diff;
    diff.create(expected.w, expected.h, 0, 0, 0);
    for (int y = 0; y < expected.h; ++y)
        for (int x = 0; x < expected.w; ++x) {
            const unsigned char *e = expected.at(x, y), *a = actual.at(x, y);
            int delta = 0;
            for (int c = 0; c < 3; ++c) delta = std::max(delta, std::abs(int(e[c]) - int(a[c])));
            unsigned char *d = diff.at(x, y);
            const unsigned char grey = static_cast<unsigned char>((e[0] * 3 + e[1] * 6 + e[2]) / 10 / 3);
            d[0] = d[1] = d[2] = grey;
            bool skip = false;
            for (const Rect &r : ignore) skip = skip || r.contains(x, y);
            if (skip) {
                if (delta > tolerance) ++ignored;
                d[2] = static_cast<unsigned char>(std::min(255, grey + 60)); /* ignored area: blue tint */
                continue;
            }
            if (delta <= tolerance) continue;
            ++differing;
            max_delta = std::max(max_delta, delta);
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
            d[0] = 255, d[1] = 0, d[2] = 0;
        }
    if (!diff_path.empty() && (differing > 0 || !ignore.empty())) save_png(diff_path, diff);
    if (differing == 0) {
        std::printf("SAME %s (tolerance %d, %ld differing pixels inside ignored areas)\n", actual_path.c_str(),
                    tolerance, ignored);
        return 0;
    }
    std::printf("DIFF %ld pixels differ (tolerance %d, max channel delta %d, box x %d..%d y %d..%d): %s%s%s\n",
                differing, tolerance, max_delta, x0, x1, y0, y1, actual_path.c_str(),
                diff_path.empty() ? "" : " -> ", diff_path.c_str());
    return 1;
}

/* Contact sheet: one row per shot, one column per size, at 1:1 so pixels stay comparable.
 *   sheet <out.png> <title> --row <label> <a.png> [<b.png> ...] [--row <label> ...]
 * A missing file leaves its cell empty (a shot that a scene takes at some sizes only). */
struct SheetRow {
    std::string label;
    std::vector<std::string> paths;
};

int make_sheet(const std::string &out_path, const std::string &title, const std::vector<SheetRow> &spec)
{
    struct Row {
        std::string label;
        std::vector<Image> cells; /* w == 0: missing */
    };
    std::vector<Row> rows;
    for (const SheetRow &in : spec) {
        rows.push_back({in.label, {}});
        for (const std::string &path : in.paths) {
            Image image;
            if (access(path.c_str(), R_OK) == 0 && !load_png(path, image)) return 2;
            rows.back().cells.push_back(std::move(image));
        }
    }
    constexpr int kGap = 24, kLabel = 30, kTitle = 48, kRowTitle = 34;
    std::vector<int> col_w;
    std::vector<std::string> col_label;
    for (const Row &row : rows)
        for (std::size_t c = 0; c < row.cells.size(); ++c) {
            if (col_w.size() <= c) col_w.push_back(0), col_label.emplace_back();
            if (row.cells[c].w > col_w[c]) {
                col_w[c] = row.cells[c].w;
                col_label[c] = std::to_string(row.cells[c].w) + "x" + std::to_string(row.cells[c].h);
            }
        }
    int width = kGap, height = kTitle + kLabel;
    for (int w : col_w) width += w + 2 + kGap;
    for (const Row &row : rows) {
        int row_h = 0;
        for (const Image &cell : row.cells) row_h = std::max(row_h, cell.h + 2);
        height += kRowTitle + row_h + kGap;
    }
    width = std::max(width, text_width(title, 4) + 2 * kGap);
    Image sheet;
    sheet.create(width, height, 0x30, 0x30, 0x30);
    draw_text(sheet, kGap, 12, title, 4, 0xF0, 0xB4, 0x00);
    int x = kGap;
    for (std::size_t c = 0; c < col_w.size(); ++c) {
        draw_text(sheet, x, kTitle + 2, col_label[c], 3, 0xE0, 0xE0, 0xE0);
        x += col_w[c] + 2 + kGap;
    }
    int top = kTitle + kLabel;
    for (const Row &row : rows) {
        draw_text(sheet, kGap, top + 6, row.label, 3, 0x9A, 0xC8, 0xFF);
        top += kRowTitle;
        int row_h = 0;
        x = kGap;
        for (std::size_t c = 0; c < row.cells.size(); ++c) {
            const Image &image = row.cells[c];
            row_h = std::max(row_h, image.h + 2);
            for (int yy = 0; image.w > 0 && yy < image.h + 2; ++yy) /* 1 px frame: black screens keep edges */
                for (int xx = 0; xx < image.w + 2; ++xx) {
                    unsigned char *p = sheet.at(x + xx, top + yy);
                    if (xx == 0 || yy == 0 || xx == image.w + 1 || yy == image.h + 1) {
                        p[0] = p[1] = p[2] = 0x80;
                        continue;
                    }
                    const unsigned char *s = image.at(xx - 1, yy - 1);
                    p[0] = s[0], p[1] = s[1], p[2] = s[2];
                }
            x += col_w[c] + 2 + kGap;
        }
        top += row_h + kGap;
    }
    if (!save_png(out_path, sheet)) {
        std::fprintf(stderr, "sheet: cannot write %s\n", out_path.c_str());
        return 2;
    }
    std::printf("sheet %s (%dx%d)\n", out_path.c_str(), sheet.w, sheet.h);
    return 0;
}

int cmd_sheet(int argc, char **argv)
{
    if (argc < 6 || std::string(argv[4]) != "--row") {
        std::fprintf(stderr, "usage: sheet <out.png> <title> --row <label> <a.png> [...] [--row ...]\n");
        return 2;
    }
    std::vector<SheetRow> rows;
    for (int i = 4; i < argc; ++i) {
        if (std::string(argv[i]) == "--row" && i + 1 < argc) {
            rows.push_back({argv[++i], {}});
            continue;
        }
        rows.back().paths.push_back(argv[i]);
    }
    return make_sheet(argv[2], argv[3], rows);
}

/* Cut a rectangle out of a PNG (used once to make fixtures/2048_128.png from the deck capture). */
int cmd_crop(int argc, char **argv)
{
    Image in, out;
    Rect r{};
    if (argc != 8 || !load_png(argv[2], in) || std::sscanf(argv[3], "%d", &r.x) != 1 ||
        std::sscanf(argv[4], "%d", &r.y) != 1 || std::sscanf(argv[5], "%d", &r.w) != 1 ||
        std::sscanf(argv[6], "%d", &r.h) != 1 || r.x < 0 || r.y < 0 || r.w <= 0 || r.h <= 0 ||
        r.x + r.w > in.w || r.y + r.h > in.h) {
        std::fprintf(stderr, "usage: crop <in.png> X Y W H <out.png>\n");
        return 2;
    }
    out.create(r.w, r.h, 0, 0, 0);
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            const unsigned char *s = in.at(r.x + x, r.y + y);
            unsigned char *d = out.at(x, y);
            d[0] = s[0], d[1] = s[1], d[2] = s[2];
        }
    return save_png(argv[7], out) ? 0 : 2;
}

/* ===================================================================== scene runner */

/* Board profiles: what the display manager gets from the environment (board.conf). */
struct Profile {
    std::string name;
    int pw = 0, ph = 0, bpp = 32, rot = 0;
    std::map<std::string, std::string> env;
};

Profile make_profile(const std::string &name, const Size &size)
{
    Profile p;
    p.name = name;
    if (name == "pi3a") { /* Pi 3A+ + Luckfox 3.5": /dev/fb1 320x480 RGB565, turned 90 deg clockwise */
        p.pw = size.h, p.ph = size.w, p.bpp = 16, p.rot = 90;
        p.env = {{"APPLAUNCH_BOARD", "pi3a-luckfox35"},
                 {"APPLAUNCH_ROTATE", "90"},
                 {"APPLAUNCH_COMPAT_SCALE", "1"},
                 {"APPLAUNCH_PANEL_MM", "79x49"}, /* as install.sh writes it */
                 {"APPLAUNCH_BACKLIGHT", "gpio:/sys/class/backlight/backlight_gpio"}};
    } else if (name == "pda") { /* report 038 S2, pi3a-wsdsi43-portrait: a landscape RGB565 panel turned 90 deg clockwise
                                 * to the portrait canvas; 480x800 = the Waveshare 4.3" DSI (94x56 mm, as install.sh
                                 * writes it), 480x640 = a 3.5" 640x480 panel (71x53 mm, 9.0 px/mm) */
        p.pw = size.h, p.ph = size.w, p.bpp = 16, p.rot = 90;
        /* report 041 (S3): the writing zone, as the board profile sets it; a scene's "env APPLAUNCH_PDA 0" shows the
         * portrait launcher without it (the S2 scenes) */
        p.env = {{"APPLAUNCH_BOARD", "pi3a-wsdsi43-portrait"}, {"APPLAUNCH_ROTATE", "90"}, {"APPLAUNCH_PDA", "1"}};
        if (size == Size{480, 800}) p.env["APPLAUNCH_PANEL_MM"] = "94x56";
        else if (size == Size{480, 640}) p.env["APPLAUNCH_PANEL_MM"] = "71x53";
    } else { /* "deck" (no board.conf, the Zero 2 W) and "generic": XRGB8888, no rotation */
        p.pw = size.w, p.ph = size.h, p.bpp = 32, p.rot = 0;
    }
    p.env["APPLAUNCH_LOGICAL"] = size.name();
    return p;
}

std::string default_profile(const Size &size)
{
    if (size == Size{480, 320}) return "pi3a";
    if (size == Size{640, 480}) return "deck";
    return "generic";
}

/* x100; report 022 section 3.1. The generic profile's fake framebuffer reports the matching physical size
 * (var.width / var.height in mm), so the layout service reads its density the way it does from a KMS
 * driver. The deck reports none (11.3 px/mm default); the Pi 3A+ has APPLAUNCH_PANEL_MM in its profile. */
int default_ppmm(const Size &size)
{
    if (size == Size{480, 320}) return 610;
    if (size == Size{640, 480}) return 1130;
    if (size == Size{800, 480}) return 920;
    if (size == Size{720, 720}) return 1000;
    if (size == Size{1280, 720}) return 1160;
    return 1130;
}

struct Line {
    int number = 0;
    std::vector<Size> only; /* @WxH[,WxH] prefix: run at these sizes only */
    std::vector<std::string> words;
};

struct Scene {
    std::string path, name, title;
    std::vector<Size> sizes;
    std::vector<Line> lines;
    bool review = false; /* shots for review only: no golden is expected at 640x480 / 480x320 */
};

bool load_scene(const std::string &path, Scene &scene)
{
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "cannot read scene %s\n", path.c_str());
        return false;
    }
    scene.path = path;
    std::string base = path.substr(path.find_last_of('/') + 1);
    scene.name = base.substr(0, base.find('.'));
    scene.title = scene.name;
    std::string text;
    int number = 0;
    while (std::getline(in, text)) {
        ++number;
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        const std::size_t hash = text.find('#');
        if (hash != std::string::npos && text.find('"') == std::string::npos) text.erase(hash);
        Line line;
        line.number = number;
        line.words = split_words(text);
        if (line.words.empty() || line.words[0][0] == '#') continue;
        if (line.words[0][0] == '@') {
            std::istringstream list(line.words[0].substr(1));
            std::string item;
            while (std::getline(list, item, ',')) {
                Size s;
                if (!parse_size(item, s)) {
                    std::fprintf(stderr, "%s:%d: bad size %s\n", path.c_str(), number, item.c_str());
                    return false;
                }
                line.only.push_back(s);
            }
            line.words.erase(line.words.begin());
            if (line.words.empty()) continue;
        }
        if (line.words[0] == "sizes") {
            for (std::size_t i = 1; i < line.words.size(); ++i) {
                Size s;
                if (!parse_size(line.words[i], s)) {
                    std::fprintf(stderr, "%s:%d: bad size %s\n", path.c_str(), number, line.words[i].c_str());
                    return false;
                }
                scene.sizes.push_back(s);
            }
            continue;
        }
        if (line.words[0] == "title") {
            scene.title.clear();
            for (std::size_t i = 1; i < line.words.size(); ++i) scene.title += (i > 1 ? " " : "") + line.words[i];
            continue;
        }
        if (line.words[0] == "review" && line.words.size() == 1) {
            scene.review = true;
            continue;
        }
        scene.lines.push_back(line);
    }
    if (scene.sizes.empty()) scene.sizes = {{480, 320}, {640, 480}, {800, 480}, {720, 720}, {1280, 720}};
    return true;
}

/* ---------------------------------------------------------------- per-size child process state */

uint32_t g_tick = 1000; /* fake LVGL tick (ms) */
uint32_t fake_tick() { return g_tick; }

struct Run {
    Size size;
    Profile profile;
    int ppmm = 0;
    bool ppmm_explicit = false; /* a scene's "ppmm N": the fake framebuffer reports it on every profile */
    bool booted = false;
    std::string out_dir;
    std::string harness_dir;
    cp0_fb_out_t view{}; /* the same logical<->buffer mapping as the display manager */
};

/* ---------------------------------------------------------------- the power_supply tree (decision X12) */

/* The launcher reads its battery from $APPLAUNCH_POWER_SUPPLY_ROOT (cp0_power_supply.h): here a fake sysfs tree next to
 * the shots, empty (no battery, as on the Pi boards; never the host's own) unless the scene's "battery" fills it. */
std::string power_supply_root(const Run &run) { return run.out_dir + "/" + run.size.name() + "/runtime/power_supply"; }

void remove_power_supply(const std::string &root, const char *device)
{
    const std::string dir = root + "/" + device;
    if (DIR *d = opendir(dir.c_str())) {
        while (const dirent *entry = readdir(d))
            if (entry->d_name[0] != '.') unlink((dir + "/" + entry->d_name).c_str());
        closedir(d);
    }
    rmdir(dir.c_str());
}

void put_power_supply(const std::string &root, const char *device, const char *file, const std::string &value)
{
    mkdirs(root + "/" + device);
    std::ofstream(root + "/" + device + "/" + file) << value << '\n';
}

/* battery off: nothing; mains: an AC adapter alone; PCT STATUS: the tablet's two devices, ADP1 (online unless
 * discharging) and BAT1 in energy units (33.3 Wh design, 26.64 Wh full: health 80 %, 7.412 V, 6.54 W discharging,
 * 12 W charging) */
bool set_power_supply(const Run &run, const std::vector<std::string> &w)
{
    const std::string root = power_supply_root(run);
    remove_power_supply(root, "ADP1");
    remove_power_supply(root, "BAT1");
    mkdirs(root);
    if (w.size() == 2 && w[1] == "off") return true;
    if (w.size() == 2 && w[1] == "mains") {
        put_power_supply(root, "ADP1", "type", "Mains");
        put_power_supply(root, "ADP1", "online", "1");
        return true;
    }
    static const std::map<std::string, std::string> kStatus = {
        {"discharging", "Discharging"}, {"charging", "Charging"}, {"full", "Full"}, {"notcharging", "Not charging"}};
    const auto status = w.size() == 3 ? kStatus.find(w[2]) : kStatus.end();
    if (status == kStatus.end()) return false;
    const int pct = std::max(0, std::min(100, std::atoi(w[1].c_str())));
    const long long full_uwh = 26640000;
    put_power_supply(root, "ADP1", "type", "Mains");
    put_power_supply(root, "ADP1", "online", w[2] == "discharging" ? "0" : "1");
    put_power_supply(root, "BAT1", "type", "Battery");
    put_power_supply(root, "BAT1", "present", "1");
    put_power_supply(root, "BAT1", "status", status->second);
    put_power_supply(root, "BAT1", "capacity", std::to_string(pct));
    put_power_supply(root, "BAT1", "energy_full_design", "33300000");
    put_power_supply(root, "BAT1", "energy_full", std::to_string(full_uwh));
    put_power_supply(root, "BAT1", "energy_now", std::to_string(full_uwh * pct / 100));
    put_power_supply(root, "BAT1", "power_now",
                     w[2] == "discharging" ? "6540000" : w[2] == "charging" ? "12000000" : "0");
    put_power_supply(root, "BAT1", "voltage_now", "7412000");
    return true;
}

/* ---------------------------------------------------------------- the backlight and the sound output (report 030) */

/* The launcher finds the backlight in $APPLAUNCH_BACKLIGHT_ROOT and the sound output in $APPLAUNCH_ASOUND_ROOT, asking
 * $APPLAUNCH_AMIXER and $APPLAUNCH_PACTL (cp0_capabilities.h): here fake trees and fake programs next to the shots, never
 * the host's own. Default: the deck's PWM "backlight" (levels, the fixture's maximum) and no sound. */
std::string runtime_path(const Run &run, const std::string &name)
{
    return run.out_dir + "/" + run.size.name() + "/runtime/" + name;
}

void remove_tree(const std::string &path)
{
    const std::string command = "rm -rf '" + path + "'";
    if (std::system(command.c_str()) != 0) std::fprintf(stderr, "[harness] could not remove %s\n", path.c_str());
}

void put_file(const std::string &dir, const std::string &file, const std::string &text, bool executable = false)
{
    mkdirs(dir);
    std::ofstream(dir + "/" + file) << text;
    if (executable) chmod((dir + "/" + file).c_str(), 0755);
}

/* backlight levels: the deck's "backlight" (raw, the fixture's maximum); onoff: the Pi 5 HyperPixel's gpio-backlight
 * "backlight" (max 1); none: no device (the HackBerry) */
bool set_backlight(const Run &run, const std::string &kind)
{
    const std::string root = runtime_path(run, "backlight");
    remove_tree(root);
    mkdirs(root);
    if (kind == "none") return true;
    if (kind != "levels" && kind != "onoff") return false;
    const bool onoff = kind == "onoff";
    put_file(root + "/backlight", "type", "raw\n");
    put_file(root + "/backlight", "max_brightness", std::to_string(onoff ? 1 : harness::fixtures().backlight_max) + "\n");
    put_file(root + "/backlight", "brightness", std::to_string(onoff ? 1 : harness::fixtures().backlight) + "\n");
    return true;
}

/* sound off: no card, no server (the deck); hdmi: vc4-hdmi only (the Pi 5, the HackBerry); card: the tablet's HDA Intel
 * PCH with Master; bluetooth: no local card, a sound server with a Bluetooth speaker's sink (the HackBerry's case
 * speaker connected) */
bool set_sound(const Run &run, const std::string &kind)
{
    const std::string root = runtime_path(run, "asound");
    const std::string bin = runtime_path(run, "fakebin");
    remove_tree(root);
    mkdirs(root);
    std::remove((bin + "/sinks.txt").c_str());
    /* amixer -c 0: the HDA card's controls; pactl list short sinks: sinks.txt when there is one */
    put_file(bin, "amixer",
             "#!/bin/sh\n[ $# -eq 2 ] && [ \"$1 $2\" = \"-c 0\" ] && { echo \"Simple mixer control 'Master',0\";"
             " echo \"  Capabilities: pvolume pswitch\"; exit 0; }\nexit 1\n",
             true);
    put_file(bin, "pactl",
             "#!/bin/sh\nd=$(dirname \"$0\")\n[ \"$1\" = list ] && [ -f \"$d/sinks.txt\" ] && cat \"$d/sinks.txt\" && exit 0\n"
             "exit 1\n",
             true);
    if (kind == "off") return true;
    if (kind == "hdmi") {
        put_file(root, "cards", " 0 [vc4hdmi0       ]: vc4-hdmi - vc4-hdmi-0\n                      vc4-hdmi-0\n"
                                " 1 [vc4hdmi1       ]: vc4-hdmi - vc4-hdmi-1\n                      vc4-hdmi-1\n");
        mkdirs(root + "/card0/pcm0p");
        mkdirs(root + "/card1/pcm0p");
        return true;
    }
    if (kind == "card") {
        put_file(root, "cards", " 0 [PCH            ]: HDA-Intel - HDA Intel PCH\n"
                                "                      HDA Intel PCH at 0x90a10000 irq 126\n");
        mkdirs(root + "/card0/pcm0p");
        return true;
    }
    if (kind == "bluetooth") {
        put_file(bin, "sinks.txt", "35\tauto_null\tPipeWire\tfloat32le 2ch 48000Hz\tSUSPENDED\n"
                                   "52\tbluez_output.02_5A_00_00_00_02.1\tPipeWire\ts16le 2ch 48000Hz\tIDLE\n");
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------- incoming SSH sessions (the SSH indicator) */

/* The launcher's SSH watch (cp0_ssh_watch.h) reads /proc/net/tcp, /proc/net/tcp6 and /etc/ssh under a root: here a fake
 * tree next to the shots (out/<WxH>/runtime/sshroot), never the host's own sessions. The harness has no status feed
 * thread: the scene command takes the feed's samples itself, with the launcher's own Sampler and its setting
 * (launcher_ssh::enabled()), and publishes them as the feed thread does; the strips read them at their next 2 s
 * refresh. No sshd_config in the tree: port 22, the watch's default. */
std::string ssh_root(const Run &run) { return runtime_path(run, "sshroot"); }

struct SshFeed {
    std::unique_ptr<ssh_indicator::Sampler> sampler;
    int64_t clock_ms = 0; /* the feed's own clock: one sample every 2 s */
};
SshFeed g_ssh_feed; /* one per process: per scene and size */

/* ssh_status off | N [idle|active]: sshd listening on 0.0.0.0:22 and N (1..8) incoming connections from 192.168.68.12
 * (ports 50000 + i) to 192.168.68.20:22; active: bytes waiting in their send queue (a fake tree has no socket for the
 * watch's netlink counters: its queue fallback). Then two samples 2 s apart, as the feed thread takes them, so a new
 * connection is confirmed (seen in two consecutive samples) */
bool set_ssh_status(const Run &run, const std::vector<std::string> &w)
{
    int connections = 0;
    bool active = false;
    if (w.size() == 2 && w[1] == "off") {
        connections = 0;
    } else if (w.size() == 2 || w.size() == 3) {
        if (w[1].size() != 1 || w[1][0] < '1' || w[1][0] > '8') return false;
        connections = w[1][0] - '0';
        if (w.size() == 3 && w[2] != "idle" && w[2] != "active") return false;
        active = w.size() == 3 && w[2] == "active";
    } else {
        return false;
    }
    const std::string header = "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  "
                               "timeout inode\n";
    std::string tcp = header + "   0: 00000000:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0  "
                               "      0 1000 1 0000000000000000 100 0 0 10 0\n";
    for (int i = 0; i < connections; ++i) {
        char line[200];
        std::snprintf(line, sizeof(line),
                      "%4d: 1444A8C0:0016 0C44A8C0:%04X 01 %08X:00000000 02:000AFC7B 00000000     0        0 %d 4 "
                      "0000000000000000 20 4 31 10 -1\n",
                      i + 1, 50000 + i, active ? 0x24 : 0, 2000 + i);
        tcp += line;
    }
    const std::string net = ssh_root(run) + "/proc/net";
    put_file(net, "tcp", tcp);
    put_file(net, "tcp6", header);
    SshFeed &feed = g_ssh_feed;
    if (!feed.sampler) feed.sampler = std::make_unique<ssh_indicator::Sampler>(ssh_root(run).c_str());
    for (int i = 0; i < 2; ++i) {
        feed.clock_ms += 2000;
        launcher_ssh::publish(feed.sampler->sample(launcher_ssh::enabled(), feed.clock_ms));
    }
    const ssh_indicator::Snapshot s = launcher_ssh::snapshot();
    std::printf("ssh_status: feed ssh=%d ssh_act=%d (setting %s)\n", s.count, s.active,
                launcher_ssh::enabled() ? "On" : "Off");
    return true;
}

/* sudoprompt: the responsive sudo prompt (cp0_sudo_prompt_ui) driven without the sudo coordinator, which the harness
 * does not link. open / busy / wrong / close stand for what cp0_sudo_async does: SHOW_PROMPT (an input group around the
 * field, as create_prompt makes it), START_WORKER, SHOW_AUTH_ERROR, DESTROY_PROMPT. OK / Enter ends in busy by itself;
 * Cancel / Esc closes it. A launcher request (harness::sudo_prompt_request, Settings > Apps' install) opens the same
 * prompt and is told when it is cancelled. */
struct SudoPromptDemo {
    std::unique_ptr<cp0_sudo::PromptUi> ui;
    lv_group_t *group = nullptr;
    lv_group_t *saved = nullptr;
    std::function<void()> cancelled; /* the launcher's request, while its prompt is open */
    std::function<void()> requested; /* a request waiting for the next cycle */
};
SudoPromptDemo g_sudo_demo;

void sudo_prompt_close()
{
    SudoPromptDemo &d = g_sudo_demo;
    if (!d.ui && !d.group) return;
    lv_group_set_default(d.saved);
    if (lv_indev_t *indev = lv_indev_get_next(nullptr)) lv_indev_set_group(indev, d.saved);
    d.ui.reset();
    if (d.group) lv_group_delete(d.group);
    d.group = nullptr;
}

bool sudo_prompt_open()
{
    sudo_prompt_close();
    SudoPromptDemo &d = g_sudo_demo;
    d.ui = std::make_unique<cp0_sudo::PromptUi>();
    cp0_sudo::PromptUiEvents events;
    events.submit = [](const std::string &) {
        if (g_sudo_demo.ui) g_sudo_demo.ui->show_busy("Authenticating...");
    };
    events.cancel = [] {
        std::function<void()> cancelled = std::move(g_sudo_demo.cancelled);
        g_sudo_demo.cancelled = nullptr;
        sudo_prompt_close();
        if (cancelled) cancelled();
    };
    if (!d.ui->create(lv_display_get_layer_top(cp0_display_native()), std::move(events))) {
        d.ui.reset();
        return false;
    }
    d.saved = lv_group_get_default();
    d.group = lv_group_create();
    lv_group_add_obj(d.group, d.ui->focus_object());
    lv_group_set_default(d.group);
    if (lv_indev_t *indev = lv_indev_get_next(nullptr)) lv_indev_set_group(indev, d.group);
    d.ui->begin_edit();
    return true;
}

/* the launcher's request, one cycle later (cp0_sudo_async shows its prompt from an async hop too) */
void sudo_prompt_request_cb(void *)
{
    std::function<void()> cancelled = std::move(g_sudo_demo.requested);
    g_sudo_demo.requested = nullptr;
    if (!sudo_prompt_open()) {
        if (cancelled) cancelled();
        return;
    }
    g_sudo_demo.cancelled = std::move(cancelled);
}

} // namespace

bool harness::sudo_prompt_request(std::function<void()> cancelled)
{
    if (g_sudo_demo.ui || g_sudo_demo.requested) return false;
    g_sudo_demo.requested = cancelled ? std::move(cancelled) : [] {};
    if (lv_async_call(sudo_prompt_request_cb, nullptr) == LV_RESULT_OK) return true;
    g_sudo_demo.requested = nullptr;
    return false;
}

namespace {

/* listdemo: a cp0::ui::CompactList on a page of its own (the Page scaffold, the shared status strip, an input group on
 * the keypad), as an app builds it. The rows are fixed demo data: Compact = a system monitor (sections, value tones,
 * long labels, 40 rows cycled to the count asked), Comfortable = serial ports (3 rows or fewer) or a terminal app's
 * settings menu. An activation writes "Activated <label>" into the title. "listdemo update" refreshes the values in
 * place (set_rows with the same rows, other values), as a live list does every second. With "kinds": a device page
 * whose rows carry each row affordance (Edit, Open, Info with a Normal and a Bad value, Action idle, one Unspecified, a
 * long label and a long value beside a chevron, an Action running with a value last); Compact puts a section header
 * first. */
struct ListDemo {
    lv_obj_t *screen = nullptr;
    std::unique_ptr<cp0::ui::Page> page;
    std::unique_ptr<cp0::ui::CompactList> list;
    lv_group_t *group = nullptr;
    std::vector<cp0::ui::ListRow> rows;
    bool kinds = false;
};
ListDemo g_list_demo;

std::vector<cp0::ui::ListRow> list_demo_rows(bool comfortable, int count, int generation, bool kinds)
{
    using cp0::ui::ListRow;
    using cp0::ui::RowAffordance;
    using cp0::ui::ValueTone;
    auto item = [](const std::string &label, const std::string &value, ValueTone tone = ValueTone::Normal) {
        return ListRow::item(label, value, tone);
    };
    auto section = [](const std::string &title) { return ListRow::section(title); };
    auto tagged = [](const std::string &label, const std::string &value, RowAffordance affordance,
                     ValueTone tone = ValueTone::Normal) { return ListRow::item(label, value, tone, 0, affordance); };
    std::vector<ListRow> base;
    if (kinds) {
        if (!comfortable) base.push_back(section("Device"));
        const std::vector<ListRow> rows = {
            tagged("Network", "Automatic (wlan0)", RowAffordance::Edit),
            tagged("Diagnose my connection", "", RowAffordance::Open),
            tagged("MAC address", "02:00:00:00:00:01", RowAffordance::Info),
            tagged("Round trip", "2 ms", RowAffordance::Info),
            tagged("Link", "Down", RowAffordance::Info, ValueTone::Bad),
            tagged("Ping this device", "", RowAffordance::Open),
            tagged("Save for Wake-on-LAN", "", RowAffordance::Action),
            item("Port", "/dev/ttyUSB0"),
            tagged("Check a port on a host whose name is far too long for one row", "", RowAffordance::Open),
            tagged("Server", "build-server.home.arpa", RowAffordance::Edit),
            tagged("Listen", "listening", RowAffordance::ActionRunning),
        };
        base.insert(base.end(), rows.begin(), rows.end());
    } else if (comfortable && count <= 3) {
        base = {item("/dev/ttyUSB0", "USB serial"), item("/dev/ttyACM0", "Modem"), item("/dev/ttyAMA0", "GPIO UART")};
    } else if (comfortable) {
        base = {item("Port", "/dev/ttyUSB0"), item("Baud rate", "115200"), item("Data bits", "8"),
                item("Parity", "None"),       item("Stop bits", "1"),       item("Flow control", "Off"),
                item("Local echo", "On", ValueTone::Ok), item("Line ending", "CR LF"), item("Timestamps", "Off"),
                item("Log to file", "Failed", ValueTone::Bad), item("Hex view", "Off"), item("About", "")};
    } else {
        const bool hot = generation % 2 == 1;
        base = {section("System"),
                item("CPU total", hot ? "64.0 %" : "12.5 %", hot ? ValueTone::Warn : ValueTone::Normal),
                item("CPU temperature", hot ? "81.3 \xC2\xB0" "C" : "71.2 \xC2\xB0" "C", hot ? ValueTone::Bad : ValueTone::Warn),
                item("Memory used", hot ? "348 MB of 512 MB" : "312 MB of 512 MB"),
                item("Under-voltage", "Now", ValueTone::Bad),
                item("Throttling", "None", ValueTone::Ok),
                item("Uptime", hot ? "3 d 4 h 1 min" : "3 d 4 h"),
                item("Load average", "0.42 0.38 0.31"),
                item("Root file system", "71 % used"),
                section("Top processes by CPU")};
        static const char *const names[] = {
            "python3 /opt/sensors/bin/environment-logger --interval 1 --output /var/log/env.csv",
            "kworker/u8:2-events_unbound",
            "systemd-journald",
            "a-process-with-a-very-long-name-that-never-fits-in-the-row",
            "sshd: user@pts/0",
            "rtl_433 -F json -M time:utc",
            "dbus-daemon --system",
            "bluetoothd",
            "wpa_supplicant -i wlan0",
            "cron",
        };
        for (int i = 0; static_cast<int>(base.size()) < std::max(count, 10); ++i) {
            const int tenths = ((i * 37 + generation * 11) % 97) * 7 % 700; /* 0.0 .. 69.9 % */
            char value[32];
            std::snprintf(value, sizeof(value), "%d.%d %%", tenths / 10, tenths % 10);
            const ValueTone tone = tenths >= 500 ? ValueTone::Bad : tenths >= 250 ? ValueTone::Warn : ValueTone::Normal;
            base.push_back(item(std::string(names[i % 10]) + (i >= 10 ? " #" + std::to_string(i / 10 + 1) : ""),
                                value, tone));
        }
    }
    std::vector<ListRow> rows;
    for (int i = 0; i < count; ++i) rows.push_back(base[static_cast<size_t>(i) % base.size()]);
    return rows;
}

void list_demo_destroy(ListDemo &d)
{
    d.list.reset();
    d.page.reset();
    if (d.group) lv_group_delete(d.group);
    d.group = nullptr;
    if (d.screen) lv_obj_delete(d.screen);
    d.screen = nullptr;
    d.rows.clear();
}

void list_demo_open(bool comfortable, int count, bool read_only, bool separators, bool wrap, bool kinds,
                    const std::string &empty)
{
    ListDemo old = std::move(g_list_demo);
    g_list_demo = ListDemo{};
    ListDemo &d = g_list_demo;
    d.kinds = kinds;
    native_ui::begin_page(true, false);
    d.screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(d.screen);
    lv_obj_set_style_bg_color(d.screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(d.screen, LV_OPA_COVER, 0);
    cp0_ui_roller_page_t page_layout{};
    cp0_ui_roller_page(cp0_ui_metrics_get(), cp0::ui::status_strip_left(), &page_layout);
    d.page = std::make_unique<cp0::ui::Page>(
        d.screen, page_layout, [](lv_obj_t *header, const char *text) { return cp0::ui::add_title(header, text); },
        [] {});
    d.page->set_title({"List demo"});

    cp0_ui_list_layout_t layout{};
    cp0_ui_list_page_layout(cp0_ui_metrics_get(), &page_layout,
                            comfortable ? CP0_UI_LIST_COMFORTABLE : CP0_UI_LIST_COMPACT, &layout);
    if (separators) layout.separator_px = 1;
    cp0::ui::CompactListEvents events;
    events.activate = [](int index, std::intptr_t) {
        ListDemo &demo = g_list_demo;
        if (demo.page && demo.list) demo.page->set_title({"List demo", "Activated " + demo.list->row(index).label});
    };
    cp0::ui::CompactListOptions options;
    options.read_only = read_only;
    options.wrap = wrap;
    options.empty_text = empty;
    d.list = std::make_unique<cp0::ui::CompactList>(d.page->root(), layout, std::move(events), options);
    d.rows = list_demo_rows(comfortable, count, 0, kinds);
    d.list->reset_rows(d.rows, 0);

    d.group = lv_group_create();
    lv_group_add_obj(d.group, d.list->focus_object());
    lv_group_focus_obj(d.list->focus_object());
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev))
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) lv_indev_set_group(indev, d.group);
    lv_screen_load(d.screen);
    cp0::ui::StatusStrip::add(d.page->header(), native_ui::fill_status);
    list_demo_destroy(old);
}

/* Advance the fake clock in 5 ms steps, running LVGL timers (refresh, indev reads, animations).
 * Worker threads some pages start get a little real time between steps to report back: the fake clock runs far faster
 * than the threads, so what a scene shows would depend on their scheduling (the Bluetooth pages' status and device list
 * replies: a switch still off, a name cut with dots at an earlier width). `wait` is where a scene says "async work is
 * in flight": it gives the threads kWaitRealUs per 50 ms of fake time, the other settling a few hundred microseconds. */
constexpr useconds_t kSettleRealUs = 300;
constexpr useconds_t kWaitRealUs = 2000;
void settle(uint32_t ms, useconds_t real_us = kSettleRealUs)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        g_tick += 5;
        lv_timer_handler();
        if (t % 50 == 0) usleep(real_us);
    }
}

void boot(Run &run)
{
    if (run.booted) return;
    run.booted = true;
    setenv("TZ", "UTC", 1);
    tzset();
    setenv("APPLAUNCH_DISPLAY", "dpi-scaled", 1);
    setenv("APPLAUNCH_FB", HARNESS_FB_PATH, 1);
    setenv("APPLAUNCH_TOUCH_DEV", HARNESS_TOUCH_PATH, 1);
    setenv("APPLAUNCH_TOUCH_ORIENT", "buffer", 1);
    /* Settings > Storage reads no mount table here: never the host's disk (its free space would change the shots) */
    setenv("APPLAUNCH_STORAGE_MOUNTS", "/nonexistent/harness-mounts", 1);
    /* nor the host's battery: the scene's fake power_supply tree (empty: no battery) */
    setenv("APPLAUNCH_POWER_SUPPLY_ROOT", power_supply_root(run).c_str(), 1);
    /* nor the host's backlight, sound cards or sound server (report 030): the scene's fake trees and programs */
    setenv("APPLAUNCH_BACKLIGHT_ROOT", runtime_path(run, "backlight").c_str(), 1);
    setenv("APPLAUNCH_ASOUND_ROOT", runtime_path(run, "asound").c_str(), 1);
    setenv("APPLAUNCH_AMIXER", runtime_path(run, "fakebin/amixer").c_str(), 1);
    setenv("APPLAUNCH_PACTL", runtime_path(run, "fakebin/pactl").c_str(), 1);
    /* the writing zone's recogniser (report 041): the repository's shipped tables, never a user's overlay or log */
    setenv("APPLAUNCH_STROKES_DIR", (harness::fixtures().resource_root + "/strokes").c_str(), 1);
    setenv("APPLAUNCH_STROKES_USER", "", 1);
    unsetenv("APPLAUNCH_STROKES_LOG");
    for (const auto &kv : run.profile.env) setenv(kv.first.c_str(), kv.second.c_str(), 1);
    /* screen.state lands next to the shots, never in the real runtime directory */
    const std::string runtime = run.out_dir + "/" + run.size.name() + "/runtime";
    mkdirs(runtime);
    setenv("XDG_RUNTIME_DIR", runtime.c_str(), 1);
    int mm_w = 0, mm_h = 0;
    if (run.ppmm > 0 && (run.ppmm_explicit || run.profile.name == "generic")) {
        mm_w = (run.profile.pw * 100 + run.ppmm / 2) / run.ppmm; /* buffer orientation, whole mm like a driver */
        mm_h = (run.profile.ph * 100 + run.ppmm / 2) / run.ppmm;
    }
    std::fprintf(stderr, "[harness] %s profile=%s fb=%dx%dx%d rot=%d fb_mm=%dx%d (ppmm %d)\n", run.size.name().c_str(),
                 run.profile.name.c_str(), run.profile.pw, run.profile.ph, run.profile.bpp, run.profile.rot, mm_w, mm_h,
                 run.ppmm);
    harness_fb_configure(run.profile.pw, run.profile.ph, run.profile.bpp);
    harness_fb_set_mm(mm_w, mm_h);
    if (chdir(harness::fixtures().resource_root.c_str()) != 0) /* the service's WorkingDirectory */
        std::fprintf(stderr, "[harness] chdir %s: %s\n", harness::fixtures().resource_root.c_str(), std::strerror(errno));

    /* cp0_lvgl_run() + cp0_lvgl_init() order: LVGL, services, events, display, input. */
    lv_init();
    harness::install_services();
    init_lvgl_event();
    if (!cp0_dpi_scaled_create() || !cp0_display_available()) {
        std::fprintf(stderr, "[harness] the display manager did not start\n");
        std::exit(3);
    }
    lv_tick_set_cb(fake_tick); /* after the display manager, which installs the monotonic clock */
    harness_keyboard_init();

    /* LauncherUiRuntime::create_display(): the default theme on both displays. */
    lv_display_t *display = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(display, lv_palette_main(LV_PALETTE_BLUE),
                                              lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_display_set_theme(display, theme);
    lv_display_set_theme(cp0_display_native(), theme);

    size_t size = 0;
    int stride = 0;
    uint8_t *mem = harness_fb_memory(&size, &stride);
    cp0_fbo_setup(&run.view, mem, size, stride, run.profile.bpp / 8, run.profile.pw, run.profile.ph,
                  run.profile.rot, run.size.w, run.size.h, nullptr);
    settle(100);
}

Image grab(const Run &run)
{
    size_t size = 0;
    int stride = 0;
    const uint8_t *mem = harness_fb_memory(&size, &stride);
    Image image;
    image.create(run.size.w, run.size.h, 0, 0, 0);
    for (int y = 0; y < run.size.h; ++y)
        for (int x = 0; x < run.size.w; ++x) {
            int px = 0, py = 0;
            cp0_fbo_logical_to_buffer(&run.view, x, y, &px, &py);
            unsigned char *p = image.at(x, y);
            if (run.profile.bpp == 16) { /* same expansion as the device capture tool (v * 255 / max) */
                const uint16_t v = *reinterpret_cast<const uint16_t *>(mem + static_cast<size_t>(py) * stride + px * 2);
                p[0] = static_cast<unsigned char>(((v >> 11) & 31) * 255 / 31);
                p[1] = static_cast<unsigned char>(((v >> 5) & 63) * 255 / 63);
                p[2] = static_cast<unsigned char>((v & 31) * 255 / 31);
            } else {
                const uint8_t *s = mem + static_cast<size_t>(py) * stride + px * 4; /* XRGB8888, little endian */
                p[0] = s[2], p[1] = s[1], p[2] = s[0];
            }
        }
    return image;
}

uint32_t key_code(const std::string &name)
{
    static const std::map<std::string, uint32_t> keys = {
        {"UP", KEY_UP},       {"DOWN", KEY_DOWN},   {"LEFT", KEY_LEFT},           {"RIGHT", KEY_RIGHT},
        {"ENTER", KEY_ENTER}, {"ESC", KEY_ESC},     {"TAB", KEY_TAB},             {"SPACE", KEY_SPACE},
        {"BACKSPACE", KEY_BACKSPACE}, {"0", KEY_0}, {"1", KEY_1}, {"2", KEY_2},  {"3", KEY_3},
        {"4", KEY_4},         {"5", KEY_5},         {"6", KEY_6},                 {"7", KEY_7},
        {"8", KEY_8},         {"9", KEY_9},         {"MINUS", KEY_MINUS},         {"EQUAL", KEY_EQUAL},
        {"DOT", KEY_DOT},     {"SLASH", KEY_SLASH}, {"DELETE", KEY_DELETE},
        {"F1", KEY_F1},       {"HOME", KEY_HOME},   {"END", KEY_END},
    };
    const auto found = keys.find(name);
    return found == keys.end() ? 0 : found->second;
}

std::string icon_for(const std::string &name)
{
    static const std::map<std::string, std::string> icons = {
        {"Settings", "setting_100.png"}, {"Store", "store_100.png"},   {"CLI", "cli_100.png"},
        {"Python", "python_100.png"},    {"SSH", "ssh_100.png"},       {"IP Panel", "ip_panel_100.png"},
        {"Calculator", "math_100.png"},  {"Snake", "game_100.png"},    {"Tank", "tank_100.png"},
    };
    const auto found = icons.find(name);
    return found == icons.end() ? std::string() : found->second;
}

/* The stock-app stand-in: a flat 320x170 XRGB8888 picture, fed through the real external blit. */
std::vector<uint32_t> stock_placeholder()
{
    Image label;
    label.create(320, 170, 0x1C, 0x28, 0x33);
    for (int y = 0; y < 170; ++y)
        for (int x = 0; x < 320; ++x)
            if (x < 3 || y < 3 || x >= 317 || y >= 167) {
                unsigned char *p = label.at(x, y);
                p[0] = 0x5D, p[1] = 0x6D, p[2] = 0x7E;
            }
    const std::string top = "STOCK APP", bottom = "320x170 PLACEHOLDER";
    draw_text(label, (320 - text_width(top, 3)) / 2, 58, top, 3, 0xD0, 0xD8, 0xE0);
    draw_text(label, (320 - text_width(bottom, 2)) / 2, 96, bottom, 2, 0x90, 0xA0, 0xB0);
    std::vector<uint32_t> xrgb(320 * 170);
    for (int y = 0; y < 170; ++y)
        for (int x = 0; x < 320; ++x) {
            const unsigned char *p = label.at(x, y);
            xrgb[static_cast<size_t>(y) * 320 + x] = (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
        }
    return xrgb;
}

void touch_at(const Run &run, int pressed, int x, int y)
{
    int px = 0, py = 0;
    cp0_fbo_logical_to_buffer(&run.view, std::clamp(x, 0, run.size.w - 1), std::clamp(y, 0, run.size.h - 1), &px, &py);
    harness_touch_report(pressed, px, py);
}

/* The display manager reads the touch device now (its pointer's read callback): the frame just reported reaches the
 * gesture callback and the writing zone's router with the fake clock's time, one frame per read. */
void read_touch_now()
{
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev))
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_read(indev);
            return;
        }
}

/* The writing zone's rectangle NAME (report 041, 042): a strip key (Keyb Tab Ctrl Alt Left Up Down Right Enter) or a
 * box (letters digits), panel coordinates. false when there is no such thing. */
bool zone_rect(const std::string &name, cp0_zone_rect_t *out)
{
    const cp0_zone_layout_t *l = cp0_zone_ui_layout();
    if (!l) return false;
    static const char *const keys[CP0_ZONE_KEY__COUNT] = {"Keyb", "Tab",  "Ctrl",  "Alt",  "Left",
                                                         "Up",   "Down", "Right", "Enter"};
    for (int i = 0; i < CP0_ZONE_KEY__COUNT; ++i)
        if (name == keys[i]) return *out = l->strip_key[i], true;
    if (name == "letters") return *out = l->letters, true;
    if (name == "digits") return *out = l->digits, true;
    return false;
}

/* A landscape app's frame (800x480 or 640x480: the panel seen in landscape, as a turned app draws it at rotate 0,
 * report 038 S2): a dark ground, a frame, two lines of text, written into the framebuffer as the app would. */
void paint_turned_app(const Run &run)
{
    const int w = run.size.h, h = run.size.w; /* the panel in landscape */
    Image picture;
    picture.create(w, h, 0x16, 0x26, 0x1C);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (x < 6 || y < 6 || x >= w - 6 || y >= h - 6) {
                unsigned char *p = picture.at(x, y);
                p[0] = 0x6C, p[1] = 0xC0, p[2] = 0x80;
            }
    const std::string top = "LANDSCAPE APP", middle = std::to_string(w) + "x" + std::to_string(h) + " TURNED",
                      bottom = "NO ZONE NO STRIP";
    draw_text(picture, (w - text_width(top, 5)) / 2, h / 2 - 80, top, 5, 0xE0, 0xF0, 0xE4);
    draw_text(picture, (w - text_width(middle, 3)) / 2, h / 2 + 10, middle, 3, 0xA0, 0xC8, 0xAC);
    draw_text(picture, (w - text_width(bottom, 3)) / 2, h / 2 + 50, bottom, 3, 0xA0, 0xC8, 0xAC);
    cp0_fb_out_t landscape{};
    size_t size = 0;
    int stride = 0;
    uint8_t *mem = harness_fb_memory(&size, &stride);
    /* the app's own output stage: the buffer seen through the turned app's APPLAUNCH_ROTATE (a quarter turn back from
     * the launcher's: 0 at rotate 90, 180 at rotate 270, app_launch_env::turned_rotation()) */
    const int app_rot = app_launch_env::turned_rotation(run.profile.rot);
    if (cp0_fbo_setup(&landscape, mem, size, stride, run.profile.bpp / 8, run.profile.pw, run.profile.ph, app_rot, w,
                      h, nullptr) < 0)
        return;
    std::vector<uint8_t> row(static_cast<size_t>(w) * 4), scratch(static_cast<size_t>(w) * 4 * 16);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = picture.at(x, y);
            row[static_cast<size_t>(x) * 4 + 0] = p[2];
            row[static_cast<size_t>(x) * 4 + 1] = p[1];
            row[static_cast<size_t>(x) * 4 + 2] = p[0];
            row[static_cast<size_t>(x) * 4 + 3] = 0xFF;
        }
        cp0_fbo_blit_xrgb_scaled(&landscape, 0, y, w, 1, row.data(), w * 4, 1, 0, 0, 0, 0, scratch.data(),
                                 scratch.size());
    }
}

/* The first visible label under obj (depth first) whose text is one of texts. */
lv_obj_t *find_label(lv_obj_t *obj, const std::vector<std::string> &texts)
{
    if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *text = lv_label_get_text(obj);
        if (text && std::find(texts.begin(), texts.end(), std::string(text)) != texts.end()) return obj;
    }
    const uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i)
        if (lv_obj_t *found = find_label(lv_obj_get_child(obj, static_cast<int32_t>(i)), texts)) return found;
    return nullptr;
}

/* The first visible label under obj (depth first) whose text is `text` (suffix: ends with it), for expect_text. */
lv_obj_t *find_label_text(lv_obj_t *obj, const std::string &text, bool suffix)
{
    if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *t = lv_label_get_text(obj);
        if (t) {
            const std::string s(t);
            const bool hit = suffix ? (s.size() >= text.size() && s.compare(s.size() - text.size(), text.size(), text) == 0)
                                    : s == text;
            if (hit) return obj;
        }
    }
    const uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i)
        if (lv_obj_t *found = find_label_text(lv_obj_get_child(obj, static_cast<int32_t>(i)), text, suffix)) return found;
    return nullptr;
}

/* The first visible label under obj (depth first) with a text whose screen origin is (x, y). */
lv_obj_t *find_label_at(lv_obj_t *obj, int x, int y)
{
    if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *text = lv_label_get_text(obj);
        lv_area_t area;
        lv_obj_get_coords(obj, &area);
        if (text && text[0] && area.x1 == x && area.y1 == y) return obj;
    }
    const uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i)
        if (lv_obj_t *found = find_label_at(lv_obj_get_child(obj, static_cast<int32_t>(i)), x, y)) return found;
    return nullptr;
}

Image crop_image(const Image &in, int x, int y, int w, int h)
{
    Image out;
    out.create(w, h, 0, 0, 0);
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) {
            const unsigned char *s = in.at(x + xx, y + yy);
            unsigned char *d = out.at(xx, yy);
            d[0] = s[0], d[1] = s[1], d[2] = s[2];
        }
    return out;
}

bool fail(const Scene &scene, const Line &line, const std::string &why)
{
    std::fprintf(stderr, "%s:%d: %s\n", scene.path.c_str(), line.number, why.c_str());
    return false;
}

int to_int(const std::string &text, int fallback = 0)
{
    char *end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    return end && *end == '\0' && !text.empty() ? static_cast<int>(value) : fallback;
}

/* ---------------------------------------------------------------- multitasking P-d: the switcher screen, the sheet
 * The real screens (multitask/switcher_view.cpp) with cards the scene names, as multitask_ui builds them from the
 * supervisor's report; what they report (pick, close, home, close-all, cancel, keep, sheet-close) is recorded for
 * expect_event. */
struct SwitcherDemo {
    std::unique_ptr<multitask_ui::SwitcherScreen> screen;
    std::unique_ptr<multitask_ui::LegacySheet> sheet;
    std::vector<std::string> names;  /* card names by id */
    std::vector<std::string> events; /* reported, oldest first */
    std::vector<uint8_t> frame;      /* the "app's" last frame under the sheet */
};
SwitcherDemo g_switcher;

std::string switcher_name(std::intptr_t id)
{
    return id >= 0 && id < static_cast<std::intptr_t>(g_switcher.names.size()) ? g_switcher.names[static_cast<std::size_t>(id)]
                                                                                : std::string("?");
}

int switcher_item(const std::string &name)
{
    if (name == "home") return multitask::kItemHome;
    if (name == "closeall") return multitask::kItemCloseAll;
    if (name.size() == 5 && name.compare(0, 4, "card") == 0 && name[4] >= '0' && name[4] <= '3') return name[4] - '0';
    return -1;
}

/* The XRGB8888 framebuffer outside the sheet's box must be the frame dimmed (cp0_display_fb_backdrop), and inside it
 * the sheet must be drawn. Returns "" or what is wrong. */
std::string check_sheet_backdrop(const Run &run, const lv_area_t &box)
{
    size_t fb_size = 0;
    int stride = 0;
    const uint8_t *fb = harness_fb_memory(&fb_size, &stride);
    if (!fb || run.profile.bpp != 32 || g_switcher.frame.size() < static_cast<size_t>(stride) * 1) return std::string();
    const uint32_t k = static_cast<uint32_t>(multitask_ui::kSheetBackdropPct) * 256u / 100u;
    long outside_wrong = 0, inside_drawn = 0;
    for (int y = 0; y < run.size.h; ++y)
        for (int x = 0; x < run.size.w; ++x) {
            int px = 0, py = 0;
            cp0_fbo_logical_to_buffer(&run.view, x, y, &px, &py);
            const size_t at = static_cast<size_t>(py) * stride + static_cast<size_t>(px) * 4;
            if (at + 3 >= g_switcher.frame.size()) continue;
            const bool inside = x >= box.x1 && x <= box.x2 && y >= box.y1 && y <= box.y2;
            bool same = true;
            for (int c = 0; c < 3; ++c)
                if (fb[at + c] != static_cast<uint8_t>((g_switcher.frame[at + c] * k) >> 8)) same = false;
            if (!inside && !same) ++outside_wrong;
            if (inside && !same) ++inside_drawn;
        }
    if (outside_wrong) return std::to_string(outside_wrong) + " pixels outside the sheet are not the dimmed frame";
    if (!inside_drawn) return "the sheet is not drawn";
    return std::string();
}

bool multitask_command(Run &run, const Scene &scene, const Line &line)
{
    const std::vector<std::string> &w = line.words;
    const std::string &cmd = w[0];
    SwitcherDemo &d = g_switcher;
    if (cmd == "switcher") {
        /* switcher CARD... (Name=front|running|paused|nr), switcher select ITEM, switcher off */
        if (w.size() == 2 && w[1] == "off") {
            native_ui::show_home();
            d.screen.reset();
            settle(300);
            return true;
        }
        if (w.size() == 3 && w[1] == "select") {
            if (!d.screen || switcher_item(w[2]) < 0) return fail(scene, line, "usage: switcher select card0..3|home|closeall");
            d.screen->select(switcher_item(w[2]));
            settle(200);
            return true;
        }
        if (!d.screen) {
            multitask_ui::SwitcherEvents events;
            events.pick = [](std::intptr_t id) { g_switcher.events.push_back("pick " + switcher_name(id)); };
            events.close = [](std::intptr_t id) { g_switcher.events.push_back("close " + switcher_name(id)); };
            events.home = [] { g_switcher.events.push_back("home"); };
            events.close_all = [] { g_switcher.events.push_back("close-all"); };
            events.cancel = [] { g_switcher.events.push_back("cancel"); };
            d.screen = std::make_unique<multitask_ui::SwitcherScreen>(std::move(events));
            lv_screen_load(d.screen->screen());
        }
        d.names.clear();
        std::vector<multitask_ui::CardView> cards;
        for (std::size_t i = 1; i < w.size(); ++i) {
            const std::size_t eq = w[i].rfind('=');
            if (eq == std::string::npos) return fail(scene, line, "switcher: a card is Name=front|running|paused|nr");
            const std::string name = w[i].substr(0, eq), state = w[i].substr(eq + 1);
            using multitask::State;
            const State s = state == "running" ? State::HiddenRunning
                            : state == "nr"    ? State::NotResponding
                                               : State::HiddenFrozen;
            if (state != "front" && state != "running" && state != "paused" && state != "nr")
                return fail(scene, line, "switcher: unknown state " + state);
            multitask_ui::CardView v;
            v.id = static_cast<std::intptr_t>(d.names.size());
            v.name = name;
            v.icon = native_ui::app_icon("tile:" + name); /* as multitask_ui does: the tile's icon */
            v.label = multitask::card_label(s, state == "front");
            d.names.push_back(name);
            cards.push_back(std::move(v));
        }
        d.screen->set_cards(cards);
        settle(300);
        return true;
    }
    if (cmd == "sheet") {
        /* sheet NAME: the screen shown now stands for the legacy app's last frame; the launcher freezes it (snapshot),
         * writes it back dimmed and draws only the sheet's box over it, as multitask_ui does; sheet off */
        if (w.size() == 2 && w[1] == "off") {
            cp0_display_set_native_clip(0, 0, 0, 0);
            native_ui::show_home();
            d.sheet.reset();
            settle(300);
            return true;
        }
        if (w.size() < 2) return fail(scene, line, "usage: sheet NAME | sheet off");
        std::string name;
        for (std::size_t i = 1; i < w.size(); ++i) name += (i > 1 ? " " : "") + w[i];
        settle(300);
        lv_refr_now(nullptr);
        const size_t page = cp0_display_fb_page_bytes();
        d.frame.assign(page, 0);
        int page_y = 0;
        if (page == 0 || cp0_display_fb_snapshot(d.frame.data(), d.frame.size(), &page_y) != 0)
            return fail(scene, line, "sheet: no snapshot");
        display_handoff::enter_app_front(nullptr); /* the app is in front: the launcher is suspended */
        if (cp0_display_fb_backdrop(d.frame.data(), d.frame.size(), multitask_ui::kSheetBackdropPct) != 0)
            return fail(scene, line, "sheet: backdrop failed");
        d.sheet = std::make_unique<multitask_ui::LegacySheet>(
            name, [] { g_switcher.events.push_back("keep"); }, [] { g_switcher.events.push_back("sheet-close"); });
        const lv_area_t box = d.sheet->box();
        cp0_display_set_native_clip(box.x1, box.y1, box.x2 - box.x1 + 1, box.y2 - box.y1 + 1);
        lv_screen_load(d.sheet->screen());
        display_handoff::leave_app_front(); /* the launcher takes the panel back: only the box is written */
        settle(500);
        lv_refr_now(nullptr);
        const std::string wrong = check_sheet_backdrop(run, box);
        if (!wrong.empty()) return fail(scene, line, "sheet: " + wrong);
        std::printf("sheet ok: box %dx%d at (%d,%d), the rest is the frame dimmed to %d %%\n", box.x2 - box.x1 + 1,
                    box.y2 - box.y1 + 1, box.x1, box.y1, multitask_ui::kSheetBackdropPct);
        return true;
    }
    if (cmd == "dots") {
        /* dots NAME...: these tiles' apps are left open (native_ui::set_resident_apps, keys "tile:NAME") */
        std::vector<std::string> keys;
        for (std::size_t i = 1; i < w.size(); ++i) keys.push_back("tile:" + w[i]);
        native_ui::set_resident_apps(keys);
        settle(200);
        return true;
    }
    if (cmd == "tapitem") {
        /* tapitem card0..3|close0..3|home|closeall|keep|sheetclose [edge]: a tap through the touch path at the item's
         * centre; edge: 2 px inside the corner of its touch target farthest from what is drawn (an X: the bottom-left
         * corner of its 7 mm square, outside the key) */
        if (w.size() < 2) return fail(scene, line, "usage: tapitem ITEM [edge]");
        const bool edge = w.size() == 3 && w[2] == "edge";
        lv_area_t a{};
        bool found = false;
        if (w[1].compare(0, 5, "close") == 0 && w[1].size() == 6 && d.screen)
            found = d.screen->close_area(w[1][5] - '0', &a);
        else if ((w[1] == "home" || w[1] == "closeall") && d.screen)
            found = d.screen->bar_area(switcher_item(w[1]), &a);
        else if (switcher_item(w[1]) >= 0 && switcher_item(w[1]) < 4 && d.screen)
            found = d.screen->card_area(switcher_item(w[1]), &a);
        else if ((w[1] == "keep" || w[1] == "sheetclose") && d.sheet)
            found = d.sheet->button_area(w[1] == "keep" ? 0 : 1, &a);
        if (!found) return fail(scene, line, "tapitem: no " + w[1] + " on the screen");
        int x = (a.x1 + a.x2) / 2, y = (a.y1 + a.y2) / 2;
        if (w[1].compare(0, 4, "card") == 0) x = a.x1 + (a.x2 - a.x1) / 5; /* on the icon, away from the X */
        if (edge) {
            x = a.x1 + 2;
            y = a.y2 - 2;
        }
        touch_at(run, 1, x, y);
        settle(80);
        touch_at(run, 0, x, y);
        settle(400);
        return true;
    }
    if (cmd == "expect_event") {
        std::string want;
        for (std::size_t i = 1; i < w.size(); ++i) want += (i > 1 ? " " : "") + w[i];
        const std::string got = d.events.empty() ? std::string("(none)") : d.events.back();
        d.events.clear();
        if (got != want) return fail(scene, line, "expect_event: want \"" + want + "\", got \"" + got + "\"");
        std::printf("expect_event ok: %s\n", want.c_str());
        return true;
    }
    if (cmd == "expect_targets") {
        /* every touch target of the screen up is at least 7 mm both ways; cards and bar inside the panel, apart */
        const int ppmm = cp0_ui_metrics_get()->ppmm_x100;
        const int seven = cp0_ui_mm_px(ppmm, 70, 0);
        std::vector<std::pair<std::string, lv_area_t>> targets;
        lv_area_t a{};
        if (d.screen) {
            for (int i = 0; i < d.screen->card_count(); ++i)
                if (d.screen->close_area(i, &a)) targets.push_back({"close" + std::to_string(i), a});
            if (d.screen->bar_area(multitask::kItemHome, &a)) targets.push_back({"home", a});
            if (d.screen->bar_area(multitask::kItemCloseAll, &a)) targets.push_back({"closeall", a});
            lv_area_t last{}, bar{};
            d.screen->card_area(3, &last);
            d.screen->bar_area(multitask::kItemHome, &bar);
            if (last.y2 >= bar.y1 || bar.y2 >= run.size.h || last.x2 >= run.size.w)
                return fail(scene, line, "expect_targets: the cards and the bar overlap or leave the panel");
        }
        if (d.sheet) {
            if (d.sheet->button_area(0, &a)) targets.push_back({"keep", a});
            if (d.sheet->button_area(1, &a)) targets.push_back({"close", a});
        }
        if (targets.empty()) return fail(scene, line, "expect_targets: no switcher or sheet");
        for (const auto &t : targets) {
            const int tw = t.second.x2 - t.second.x1 + 1, th = t.second.y2 - t.second.y1 + 1;
            std::printf("target %s %dx%d px = %.1fx%.1f mm (7 mm = %d px)\n", t.first.c_str(), tw, th, tw * 100.0 / ppmm,
                        th * 100.0 / ppmm, seven);
            if (tw < seven || th < seven) return fail(scene, line, "expect_targets: " + t.first + " is under 7 mm");
        }
        return true;
    }
    return fail(scene, line, "unknown " + cmd);
}

bool run_line(Run &run, const Scene &scene, const Line &line)
{
    const std::vector<std::string> &w = line.words;
    const std::string &cmd = w[0];
    harness::Fixtures &fx = harness::fixtures();

    /* directives that set up the board: before the first drawing command */
    if (cmd == "profile" || cmd == "ppmm" || cmd == "env") {
        if (run.booted) return fail(scene, line, cmd + " must come before the first drawing command");
        if (cmd == "profile" && w.size() == 2) run.profile = make_profile(w[1], run.size);
        else if (cmd == "ppmm" && w.size() == 2) {
            run.ppmm = to_int(w[1], run.ppmm);
            run.ppmm_explicit = true;
        } else if (cmd == "env" && w.size() == 3) {
            run.profile.env[w[1]] = w[2];
            /* the panel held the other way (report 043, PDA mode On rotate right: 270 for the pda profile's 90): the
             * display manager draws through that rotation, and the harness looks at the framebuffer and touches it
             * through the same one; only a rotation that keeps the fake framebuffer's size */
            int rot = -1;
            if (w[1] == "APPLAUNCH_ROTATE" && cp0_fbo_parse_rotation(w[2].c_str(), &rot) == 0) {
                if ((rot % 180) != (run.profile.rot % 180))
                    return fail(scene, line, "env APPLAUNCH_ROTATE: only a rotation of the same quarter as the "
                                             "profile's (the framebuffer keeps its size)");
                run.profile.rot = rot;
            }
        }
        else return fail(scene, line, "usage: profile deck|pi3a|pda|generic, ppmm N, env KEY VALUE");
        return true;
    }
    if (cmd == "strokesuser") {
        /* strokesuser: the writing zone's user overlay is out/<WxH>/runtime/config/strokes-user.txt, empty at the start
         * (the Strokes training page saves it there, and its capture log strokes-training.txt next to it; report 042) */
        if (run.booted) return fail(scene, line, "strokesuser must come before the first drawing command");
        const std::string dir = runtime_path(run, "config");
        remove_tree(dir);
        mkdirs(dir);
        run.profile.env["APPLAUNCH_STROKES_USER"] = dir + "/strokes-user.txt";
        return true;
    }
    if (cmd == "expect_file") {
        /* expect_file NAME [lines N] [has TEXT]: a file of out/<WxH>/runtime/config exists (with N lines starting with
         * "# training" when lines is given; containing TEXT when has is given) */
        if (w.size() != 2 && w.size() != 4) return fail(scene, line, "usage: expect_file NAME [lines N|has TEXT]");
        std::ifstream in(runtime_path(run, "config/" + w[1]));
        if (!in) return fail(scene, line, "expect_file: no " + w[1]);
        const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (w.size() == 4 && w[2] == "lines") {
            int n = 0;
            for (std::size_t at = 0; (at = content.find("# training ", at)) != std::string::npos; ++at) n++;
            if (n != to_int(w[3])) return fail(scene, line, "expect_file: " + std::to_string(n) + " training lines");
        } else if (w.size() == 4 && w[2] == "has") {
            if (content.find(w[3]) == std::string::npos) return fail(scene, line, "expect_file: no \"" + w[3] + "\"");
        } else if (w.size() == 4) {
            return fail(scene, line, "usage: expect_file NAME [lines N|has TEXT]");
        }
        std::printf("expect_file ok: %s\n", w[1].c_str());
        return true;
    }
    /* fixture state: may come before or after boot */
    if (cmd == "clock") {
        int hh = 0, mm = 0, year = 2026, month = 10, day = 10;
        if (w.size() < 2 || std::sscanf(w[1].c_str(), "%d:%d", &hh, &mm) != 2)
            return fail(scene, line, "usage: clock HH:MM [YYYY-MM-DD]");
        if (w.size() >= 3 && std::sscanf(w[2].c_str(), "%d-%d-%d", &year, &month, &day) != 3)
            return fail(scene, line, "usage: clock HH:MM [YYYY-MM-DD]");
        std::tm t{};
        t.tm_year = year - 1900, t.tm_mon = month - 1, t.tm_mday = day, t.tm_hour = hh, t.tm_min = mm;
        harness_set_wall_clock(static_cast<int64_t>(timegm(&t)));
        fx.time_set_epoch.store(-1); /* a time saved from Settings no longer overrides it */
        return true;
    }
    if (cmd == "wifi") {
        if (w.size() != 2) return fail(scene, line, "usage: wifi off|PERCENT");
        fx.wifi_connected = w[1] != "off";
        fx.wifi_signal = fx.wifi_connected ? to_int(w[1], 70) : 0;
        return true;
    }
    if (cmd == "wifinet") {
        if (w.size() != 3) return fail(scene, line, "usage: wifinet SSID IP");
        fx.wifi_ssid = w[1];
        fx.wifi_ip = w[2];
        if (!fx.wifi_networks.empty()) fx.wifi_networks[0].ssid = w[1]; /* the connected network of the scan */
        return true;
    }
    if (cmd == "expect_text") {
        /* expect_text [end] TEXT: a visible label of the native screen (or of its top layer: the sudo prompt) shows TEXT
         * whole (with `end`: a label whose text ENDS with TEXT), and its box is wide enough for its text: not cut with
         * dots, not past the screen. A scene fails otherwise. */
        const bool suffix = w.size() == 3 && w[1] == "end";
        if (w.size() != 2 && !suffix) return fail(scene, line, "usage: expect_text [end] TEXT");
        const std::string &text = w.back();
        settle(500);
        lv_refr_now(nullptr);
        lv_obj_t *label = find_label_text(lv_display_get_screen_active(cp0_display_native()), text, suffix);
        if (!label) label = find_label_text(lv_display_get_layer_top(cp0_display_native()), text, suffix);
        if (!label)
            return fail(scene, line, "expect_text: no visible label " + std::string(suffix ? "ending with " : "") + text);
        lv_area_t area;
        lv_obj_get_coords(label, &area);
        const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
        lv_point_t size{};
        lv_text_get_size(&size, lv_label_get_text(label), font,
                         lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const int pad = lv_obj_get_style_pad_left(label, LV_PART_MAIN) + lv_obj_get_style_pad_right(label, LV_PART_MAIN);
        const int box = area.x2 - area.x1 + 1;
        if (box < size.x + pad || area.x2 >= run.size.w)
            return fail(scene, line, "expect_text: the label is cut (box " + std::to_string(box) + " px, text " +
                                         std::to_string(size.x + pad) + " px): " + lv_label_get_text(label));
        std::printf("expect_text ok: %s\n", lv_label_get_text(label));
        return true;
    }
    if (cmd == "expect_rows") {
        /* expect_rows LABEL...: the list holding the visible row label LABEL (the first one) shows exactly these rows,
         * in this order, nothing else: every row of the list, also those scrolled out of view (the native Settings
         * roller builds one object per row, its label first; cp0_ui_roller_widget.cpp). Pins a section's row list:
         * a row added, dropped or moved fails the scene. */
        if (w.size() < 2) return fail(scene, line, "usage: expect_rows LABEL...");
        const std::vector<std::string> want(w.begin() + 1, w.end());
        settle(500);
        lv_refr_now(nullptr);
        lv_obj_t *label = find_label_text(lv_display_get_screen_active(cp0_display_native()), want.front(), false);
        lv_obj_t *row = label ? lv_obj_get_parent(label) : nullptr;
        lv_obj_t *list = row ? lv_obj_get_parent(row) : nullptr;
        if (!list) return fail(scene, line, "expect_rows: no visible row " + want.front());
        std::vector<std::string> have;
        const uint32_t count = lv_obj_get_child_count(list);
        for (uint32_t i = 0; i < count; ++i) {
            lv_obj_t *child = lv_obj_get_child(list, static_cast<int32_t>(i));
            lv_obj_t *first = child ? lv_obj_get_child(child, 0) : nullptr;
            const char *text = first && lv_obj_check_type(first, &lv_label_class) ? lv_label_get_text(first) : nullptr;
            have.push_back(text ? text : "?");
        }
        std::string joined;
        for (const std::string &s : have) joined += (joined.empty() ? "" : " | ") + s;
        if (have != want) return fail(scene, line, "expect_rows: the list shows " + joined);
        std::printf("expect_rows ok: %s\n", joined.c_str());
        return true;
    }
    if (cmd == "expect_tiles") {
        /* expect_tiles NAME...: the home grid's tiles, in the grid's child order (what is drawn, a carried tile's place
         * included: study 046 P2 move mode), are exactly these. The grid is the parent of the tile holding the label of
         * the first NAME; each tile's name is its own label (a direct child). */
        if (w.size() < 2) return fail(scene, line, "usage: expect_tiles NAME...");
        const std::vector<std::string> want(w.begin() + 1, w.end());
        settle(300);
        lv_obj_t *label = find_label_text(lv_display_get_screen_active(cp0_display_native()), want.front(), false);
        lv_obj_t *tile = label ? lv_obj_get_parent(label) : nullptr;
        lv_obj_t *grid = tile ? lv_obj_get_parent(tile) : nullptr;
        if (!grid) return fail(scene, line, "expect_tiles: no tile " + want.front());
        std::vector<std::string> have;
        const uint32_t count = lv_obj_get_child_count(grid);
        for (uint32_t i = 0; i < count; ++i) {
            lv_obj_t *child = lv_obj_get_child(grid, static_cast<int32_t>(i));
            const char *text = nullptr;
            const uint32_t parts = child ? lv_obj_get_child_count(child) : 0;
            for (uint32_t j = 0; j < parts && !text; ++j) {
                lv_obj_t *part = lv_obj_get_child(child, static_cast<int32_t>(j));
                if (lv_obj_check_type(part, &lv_label_class)) text = lv_label_get_text(part);
            }
            have.push_back(text ? text : "?");
        }
        std::string joined;
        for (const std::string &s : have) joined += (joined.empty() ? "" : " | ") + s;
        if (have != want) return fail(scene, line, "expect_tiles: the grid shows " + joined);
        std::printf("expect_tiles ok: %s\n", joined.c_str());
        return true;
    }
    if (cmd == "expect_grid_view") {
        /* expect_grid_view NAME: the home grid's view is inside its content (no blank space under the last row nor above
         * the first; a grid shorter than the view starts at its first row) and the tile NAME is wholly inside the grid's
         * area (study 046 P2: the view follows a carried tile by the least scroll) */
        if (w.size() != 2) return fail(scene, line, "usage: expect_grid_view NAME");
        settle(300);
        lv_obj_t *label = find_label_text(lv_display_get_screen_active(cp0_display_native()), w[1], false);
        lv_obj_t *tile = label ? lv_obj_get_parent(label) : nullptr;
        lv_obj_t *grid = tile ? lv_obj_get_parent(tile) : nullptr;
        if (!grid) return fail(scene, line, "expect_grid_view: no tile " + w[1]);
        lv_obj_update_layout(grid);
        const int32_t above = lv_obj_get_scroll_top(grid), below = lv_obj_get_scroll_bottom(grid);
        const bool short_content = above + below < 0;
        if (above < 0 || (short_content ? above != 0 : below < 0))
            return fail(scene, line, "expect_grid_view: the view is outside the content (scroll top " +
                                         std::to_string(above) + ", bottom " + std::to_string(below) + ")");
        lv_area_t t, g;
        lv_obj_get_coords(tile, &t);
        lv_obj_get_coords(grid, &g);
        if (t.y1 < g.y1 || t.y2 > g.y2)
            return fail(scene, line, "expect_grid_view: " + w[1] + " is not wholly in the grid (tile y " +
                                         std::to_string(t.y1) + ".." + std::to_string(t.y2) + ", grid y " +
                                         std::to_string(g.y1) + ".." + std::to_string(g.y2) + ")");
        std::printf("expect_grid_view ok: %s, scroll top %d bottom %d\n", w[1].c_str(), static_cast<int>(above),
                    static_cast<int>(below));
        return true;
    }
    if (cmd == "grid_scroll_mark" || cmd == "expect_grid_scroll_same") {
        /* grid_scroll_mark NAME: remember the home grid's scroll (the grid holding the tile NAME) now;
         * expect_grid_scroll_same NAME: fail unless it is still the same (study 046 P2: a press and the 3 s pick-up hold
         * on a tile do not scroll the grid) */
        if (w.size() != 2) return fail(scene, line, "usage: " + cmd + " NAME");
        settle(300);
        lv_obj_t *label = find_label_text(lv_display_get_screen_active(cp0_display_native()), w[1], false);
        lv_obj_t *tile = label ? lv_obj_get_parent(label) : nullptr;
        lv_obj_t *grid = tile ? lv_obj_get_parent(tile) : nullptr;
        if (!grid) return fail(scene, line, cmd + ": no tile " + w[1]);
        static int32_t marked = -1;
        const int32_t now = lv_obj_get_scroll_y(grid);
        if (cmd == "grid_scroll_mark") {
            marked = now;
            std::printf("grid_scroll_mark: %d\n", static_cast<int>(now));
            return true;
        }
        if (now != marked)
            return fail(scene, line, "expect_grid_scroll_same: the grid scrolled from " + std::to_string(marked) +
                                         " to " + std::to_string(now));
        std::printf("expect_grid_scroll_same ok: %d\n", static_cast<int>(now));
        return true;
    }
    if (cmd == "expect_color") {
        /* expect_color TEXT RRGGBB: a visible label of the native screen shows TEXT, its text colour is RRGGBB and the
         * screen as drawn has pixels of that colour (within the RGB565 rounding) inside the label's box: a colour change
         * reached the framebuffer, not only the style. A scene fails otherwise. */
        if (w.size() != 3 || w[2].size() != 6) return fail(scene, line, "usage: expect_color TEXT RRGGBB");
        const uint32_t want = static_cast<uint32_t>(std::strtoul(w[2].c_str(), nullptr, 16));
        settle(500);
        lv_refr_now(nullptr);
        lv_obj_t *label = find_label_text(lv_display_get_screen_active(cp0_display_native()), w[1], false);
        if (!label) return fail(scene, line, "expect_color: no visible label " + w[1]);
        const uint32_t style = lv_color_to_u32(lv_obj_get_style_text_color(label, LV_PART_MAIN)) & 0xFFFFFF;
        char have[8];
        std::snprintf(have, sizeof(have), "%06X", static_cast<unsigned>(style));
        if (style != want) return fail(scene, line, "expect_color: " + w[1] + " has text colour " + have);
        lv_area_t area;
        lv_obj_get_coords(label, &area);
        const Image image = grab(run);
        int hits = 0;
        for (int y = std::max<int>(0, area.y1); y <= std::min<int>(run.size.h - 1, area.y2); ++y)
            for (int x = std::max<int>(0, area.x1); x <= std::min<int>(run.size.w - 1, area.x2); ++x) {
                const unsigned char *p = image.at(x, y);
                const int dr = std::abs(p[0] - int((want >> 16) & 0xFF)), dg = std::abs(p[1] - int((want >> 8) & 0xFF)),
                          db = std::abs(p[2] - int(want & 0xFF));
                if (dr <= 8 && dg <= 4 && db <= 8) ++hits;
            }
        if (hits == 0) return fail(scene, line, "expect_color: no pixel of " + w[2] + " drawn in the label " + w[1]);
        std::printf("expect_color ok: %s %s (%d px)\n", w[1].c_str(), w[2].c_str(), hits);
        return true;
    }
    if (cmd == "expect_chevron") {
        /* expect_chevron LABEL yes|no: the list row of the visible label LABEL shows a row-affordance chevron (yes) or
         * none (no). yes: the chevron label is visible and grey (CP0_UI_LIST_CHEVRON_RGB) with pixels of that colour
         * drawn; its box lies right of the row's other texts (no overlap) and inside the row; its drawn pixels are
         * centred on the row's middle (within 1.5 px); its font is larger than the label's (not a tiny mark); the row
         * keeps the list's item height. A scene fails otherwise. */
        if (w.size() != 3 || (w[2] != "yes" && w[2] != "no"))
            return fail(scene, line, "usage: expect_chevron LABEL yes|no");
        settle(500);
        lv_refr_now(nullptr);
        lv_obj_t *label = find_label_text(lv_display_get_screen_active(cp0_display_native()), w[1], false);
        if (!label) return fail(scene, line, "expect_chevron: no visible label " + w[1]);
        lv_obj_t *row = lv_obj_get_parent(label);
        lv_obj_t *chevron = nullptr;
        std::vector<lv_obj_t *> texts;
        for (uint32_t i = 0; i < lv_obj_get_child_count(row); ++i) {
            lv_obj_t *child = lv_obj_get_child(row, static_cast<int32_t>(i));
            if (!lv_obj_check_type(child, &lv_label_class) || lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
            const std::string text = lv_label_get_text(child) ? lv_label_get_text(child) : "";
            if (text == CP0_UI_LIST_CHEVRON_TEXT || text == CP0_UI_LIST_CHEVRON_FALLBACK) chevron = child;
            else if (!text.empty()) texts.push_back(child);
        }
        if (w[2] == "no") {
            if (chevron) return fail(scene, line, "expect_chevron: the row of " + w[1] + " shows a chevron");
            std::printf("expect_chevron ok: %s no\n", w[1].c_str());
            return true;
        }
        if (!chevron) return fail(scene, line, "expect_chevron: the row of " + w[1] + " shows no chevron");
        const int row_h = g_list_demo.list ? g_list_demo.list->layout().row_h : 0;
        if (row_h && lv_obj_get_height(row) != row_h)
            return fail(scene, line, "expect_chevron: the row of " + w[1] + " is " +
                                         std::to_string(lv_obj_get_height(row)) + " px high, not " +
                                         std::to_string(row_h));
        const uint32_t want = CP0_UI_LIST_CHEVRON_RGB;
        const uint32_t style = lv_color_to_u32(lv_obj_get_style_text_color(chevron, LV_PART_MAIN)) & 0xFFFFFF;
        if (style != want) return fail(scene, line, "expect_chevron: the chevron's text colour is not the grey");
        const lv_font_t *chevron_font = lv_obj_get_style_text_font(chevron, LV_PART_MAIN);
        const lv_font_t *label_font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
        if (!chevron_font || !label_font ||
            lv_font_get_line_height(chevron_font) <= lv_font_get_line_height(label_font))
            return fail(scene, line, "expect_chevron: the chevron's font is not larger than the label's");
        lv_area_t c, r;
        lv_obj_get_coords(chevron, &c);
        lv_obj_get_coords(row, &r);
        if (c.x2 > r.x2 || c.x1 < r.x1) return fail(scene, line, "expect_chevron: the chevron is outside its row");
        for (lv_obj_t *text : texts) {
            lv_area_t t;
            lv_obj_get_coords(text, &t);
            if (t.x2 >= c.x1)
                return fail(scene, line, std::string("expect_chevron: \"") + lv_label_get_text(text) + "\" reaches x " +
                                             std::to_string(t.x2) + ", the chevron starts at " + std::to_string(c.x1));
        }
        const Image image = grab(run);
        int hits = 0, top = INT32_MAX, bottom = -1;
        for (int y = std::max<int>({0, c.y1, r.y1}); y <= std::min<int>({run.size.h - 1, c.y2, r.y2}); ++y)
            for (int x = std::max<int>(0, c.x1); x <= std::min<int>(run.size.w - 1, c.x2); ++x) {
                const unsigned char *p = image.at(x, y);
                const int dr = std::abs(p[0] - int((want >> 16) & 0xFF)), dg = std::abs(p[1] - int((want >> 8) & 0xFF)),
                          db = std::abs(p[2] - int(want & 0xFF));
                if (dr <= 8 && dg <= 4 && db <= 8) {
                    ++hits;
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        if (hits == 0) return fail(scene, line, "expect_chevron: no grey chevron pixel drawn in the row of " + w[1]);
        const int off2 = (top + bottom) - (r.y1 + r.y2); /* twice the drawn centre's offset from the row's middle */
        if (off2 < -3 || off2 > 3)
            return fail(scene, line, "expect_chevron: the chevron's pixels (y " + std::to_string(top) + ".." +
                                         std::to_string(bottom) + ") are not centred in the row (y " +
                                         std::to_string(r.y1) + ".." + std::to_string(r.y2) + ")");
        std::printf("expect_chevron ok: %s yes (%d px, y %d..%d in row %d..%d, x %d..%d)\n", w[1].c_str(), hits, top,
                    bottom, static_cast<int>(r.y1), static_cast<int>(r.y2), static_cast<int>(c.x1),
                    static_cast<int>(c.x2));
        return true;
    }
    if (cmd == "expect_dot") {
        /* expect_dot LABEL idle|running|no: the list row of the visible label LABEL shows the Action dot, white (idle)
         * or the accent blue (running), or no dot (no). idle / running: the dot is visible, a circle of the list's dot
         * size, its colour drawn; its drawn pixels are centred on the row's middle and on the chevron's slot (within
         * 1.5 px: dots and chevrons share one column); it lies right of the row's texts and inside the row; the row's
         * value (if any) ends where a chevron row's value ends; the row keeps the list's item height. A scene fails
         * otherwise. */
        if (w.size() != 3 || (w[2] != "idle" && w[2] != "running" && w[2] != "no"))
            return fail(scene, line, "usage: expect_dot LABEL idle|running|no");
        if (!g_list_demo.list) return fail(scene, line, "expect_dot needs a list (listdemo compact N first)");
        settle(500);
        lv_refr_now(nullptr);
        lv_obj_t *label = find_label_text(lv_display_get_screen_active(cp0_display_native()), w[1], false);
        if (!label) return fail(scene, line, "expect_dot: no visible label " + w[1]);
        lv_obj_t *row = lv_obj_get_parent(label);
        lv_obj_t *dot = nullptr, *value = nullptr;
        std::vector<lv_obj_t *> texts;
        for (uint32_t i = 0; i < lv_obj_get_child_count(row); ++i) {
            lv_obj_t *child = lv_obj_get_child(row, static_cast<int32_t>(i));
            if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
            if (!lv_obj_check_type(child, &lv_label_class)) {
                dot = child;
                continue;
            }
            const std::string text = lv_label_get_text(child) ? lv_label_get_text(child) : "";
            if (text.empty()) continue;
            texts.push_back(child);
            if (i == 1) value = child; /* the row's children: label, value, then the chevron / dot */
        }
        if (w[2] == "no") {
            if (dot) return fail(scene, line, "expect_dot: the row of " + w[1] + " shows a dot");
            std::printf("expect_dot ok: %s no\n", w[1].c_str());
            return true;
        }
        if (!dot) return fail(scene, line, "expect_dot: the row of " + w[1] + " shows no dot");
        const cp0_ui_list_layout_t &l = g_list_demo.list->layout();
        if (lv_obj_get_height(row) != l.row_h)
            return fail(scene, line, "expect_dot: the row of " + w[1] + " is " +
                                         std::to_string(lv_obj_get_height(row)) + " px high, not " +
                                         std::to_string(l.row_h));
        const int d = cp0_ui_list_dot_px(&l);
        if (lv_obj_get_width(dot) != d || lv_obj_get_height(dot) != d ||
            lv_obj_get_style_radius(dot, LV_PART_MAIN) != LV_RADIUS_CIRCLE)
            return fail(scene, line, "expect_dot: the dot is not a circle of " + std::to_string(d) + " px");
        const uint32_t want = w[2] == "idle" ? CP0_UI_LIST_DOT_IDLE_RGB : CP0_UI_LIST_DOT_RUNNING_RGB;
        const uint32_t style = lv_color_to_u32(lv_obj_get_style_bg_color(dot, LV_PART_MAIN)) & 0xFFFFFF;
        char have[8];
        std::snprintf(have, sizeof(have), "%06X", static_cast<unsigned>(style));
        if (style != want) return fail(scene, line, "expect_dot: the dot's colour is " + std::string(have));
        lv_area_t c, r;
        lv_obj_get_coords(dot, &c);
        lv_obj_get_coords(row, &r);
        if (c.x2 > r.x2 || c.x1 < r.x1 || c.y1 < r.y1 || c.y2 > r.y2)
            return fail(scene, line, "expect_dot: the dot is outside its row");
        for (lv_obj_t *text : texts) {
            lv_area_t t;
            lv_obj_get_coords(text, &t);
            if (t.x2 >= c.x1)
                return fail(scene, line, std::string("expect_dot: \"") + lv_label_get_text(text) + "\" reaches x " +
                                             std::to_string(t.x2) + ", the dot starts at " + std::to_string(c.x1));
        }
        if (value) {
            lv_area_t v;
            lv_obj_get_coords(value, &v);
            const int chevron_row_end = r.x2 - l.inset - cp0_ui_list_trail_w(&l, CP0_UI_LIST_AFFORDANCE_EDIT);
            if (v.x2 != chevron_row_end)
                return fail(scene, line, "expect_dot: the value ends at x " + std::to_string(v.x2) +
                                             ", a chevron row's at " + std::to_string(chevron_row_end));
        }
        const Image image = grab(run);
        int hits = 0, top = INT32_MAX, bottom = -1, left = INT32_MAX, right = -1;
        for (int y = std::max<int>(0, c.y1); y <= std::min<int>(run.size.h - 1, c.y2); ++y)
            for (int x = std::max<int>(0, c.x1); x <= std::min<int>(run.size.w - 1, c.x2); ++x) {
                const unsigned char *p = image.at(x, y);
                const int dr = std::abs(p[0] - int((want >> 16) & 0xFF)), dg = std::abs(p[1] - int((want >> 8) & 0xFF)),
                          db = std::abs(p[2] - int(want & 0xFF));
                if (dr <= 8 && dg <= 4 && db <= 8) {
                    ++hits;
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
            }
        if (hits == 0) return fail(scene, line, "expect_dot: no " + w[2] + " dot pixel drawn in the row of " + w[1]);
        const int slot_x1 = r.x1 + l.w - l.inset - cp0_ui_list_chevron_w(&l);
        const int slot_x2 = slot_x1 + cp0_ui_list_chevron_w(&l) - 1;
        const int off_y = (top + bottom) - (r.y1 + r.y2), off_x = (left + right) - (slot_x1 + slot_x2);
        if (off_y < -3 || off_y > 3 || off_x < -3 || off_x > 3)
            return fail(scene, line, "expect_dot: the dot's pixels (x " + std::to_string(left) + ".." +
                                         std::to_string(right) + ", y " + std::to_string(top) + ".." +
                                         std::to_string(bottom) + ") are not centred on the row (y " +
                                         std::to_string(r.y1) + ".." + std::to_string(r.y2) + ") and the slot (x " +
                                         std::to_string(slot_x1) + ".." + std::to_string(slot_x2) + ")");
        std::printf("expect_dot ok: %s %s (%d px, x %d..%d in slot %d..%d, y %d..%d in row %d..%d)\n", w[1].c_str(),
                    w[2].c_str(), hits, left, right, slot_x1, slot_x2, top, bottom, static_cast<int>(r.y1),
                    static_cast<int>(r.y2));
        return true;
    }
    if (cmd == "expect_same") {
        /* expect_same SHOT: the screen as drawn now is pixel-identical to the shot SHOT taken earlier in this scene at
         * this size (out/<WxH>/SHOT.png). A scene fails otherwise. */
        if (w.size() != 2) return fail(scene, line, "usage: expect_same SHOT");
        settle(500);
        lv_refr_now(nullptr);
        const std::string path = run.out_dir + "/" + run.size.name() + "/" + w[1] + ".png";
        Image before;
        if (!load_png(path, before)) return fail(scene, line, "expect_same: cannot read " + path);
        const Image now = grab(run);
        if (now.w != before.w || now.h != before.h) return fail(scene, line, "expect_same: the sizes differ");
        int changed = 0;
        for (int y = 0; y < now.h; ++y)
            for (int x = 0; x < now.w; ++x)
                if (std::memcmp(now.at(x, y), before.at(x, y), 3) != 0) ++changed;
        if (changed)
            return fail(scene, line, "expect_same: " + std::to_string(changed) + " pixels differ from " + w[1]);
        std::printf("expect_same ok: %s\n", w[1].c_str());
        return true;
    }
    if (cmd == "switcher" || cmd == "sheet" || cmd == "dots" || cmd == "tapitem" || cmd == "expect_event" ||
        cmd == "expect_targets")
        return multitask_command(run, scene, line); /* multitasking P-d */
    if (cmd == "handoff" || cmd == "expect_fb") {
        /* Multitasking P-b (task 015 section 5), the display suspended mode with the real display backend:
         *   handoff enter      display_handoff::enter_app_front() (display suspended, timers paused)
         *   handoff paint      an "app" draws its frame straight into the framebuffer (a fixed pattern)
         *   handoff snapshot   cp0_display_fb_snapshot() of the page shown (must equal the painted frame)
         *   handoff restore    cp0_display_fb_restore() of that snapshot (must read back identical)
         *   handoff leave      display_handoff::leave_app_front(), then a refresh as the launcher does
         *   expect_fb painted  after 500 ms of timers and a forced refresh, the framebuffer is still the app's frame
         *   expect_fb paused   enter_app_front() paused at least one timer, and the display reports suspended */
        static std::vector<uint8_t> painted, snapshot;
        static int snapshot_page = 0;
        size_t fb_size = 0;
        int fb_stride = 0;
        uint8_t *fb = harness_fb_memory(&fb_size, &fb_stride);
        const size_t page = cp0_display_fb_page_bytes();
        if (w.size() != 2 || !fb || page == 0 || page > fb_size) return fail(scene, line, "usage: handoff enter|paint|snapshot|restore|leave, expect_fb painted|paused");
        const std::string &what = w[1];
        if (cmd == "handoff" && what == "enter") {
            display_handoff::enter_app_front(nullptr);
        } else if (cmd == "handoff" && what == "paint") {
            painted.resize(page);
            for (size_t i = 0; i < page; ++i) painted[i] = static_cast<uint8_t>((i * 7 + i / 4093) & 0xFF);
            std::memcpy(fb, painted.data(), page);
        } else if (cmd == "handoff" && what == "snapshot") {
            snapshot.assign(page, 0);
            if (cp0_display_fb_snapshot(snapshot.data(), snapshot.size(), &snapshot_page) != 0)
                return fail(scene, line, "handoff snapshot: failed");
            if (snapshot != painted) return fail(scene, line, "handoff snapshot: differs from the painted frame");
        } else if (cmd == "handoff" && what == "restore") {
            const int rc = cp0_display_fb_restore(snapshot.data(), snapshot.size(), snapshot_page);
            if (rc != 0) return fail(scene, line, "handoff restore: returned " + std::to_string(rc));
        } else if (cmd == "handoff" && what == "leave") {
            display_handoff::leave_app_front();
            settle(500);
            lv_refr_now(nullptr);
        } else if (cmd == "expect_fb" && what == "painted") {
            settle(500);
            lv_refr_now(nullptr);
            size_t changed = 0;
            for (size_t i = 0; i < page && i < painted.size(); ++i)
                if (fb[i] != painted[i]) ++changed;
            if (painted.size() != page || changed)
                return fail(scene, line, "expect_fb painted: " + std::to_string(changed) + " bytes written over the app");
        } else if (cmd == "expect_fb" && what == "paused") {
            if (!display_handoff::app_front() || display_handoff::paused_timers() == 0 || !cp0_display_suspended())
                return fail(scene, line, "expect_fb paused: the launcher is not suspended");
        } else {
            return fail(scene, line, "unknown " + cmd + " " + what);
        }
        std::printf("%s %s ok\n", cmd.c_str(), what.c_str());
        return true;
    }
    if (cmd == "bt") {
        if (w.size() != 2) return fail(scene, line, "usage: bt off|on|connected");
        fx.bt_powered = w[1] != "off";
        fx.bt_connected = w[1] == "connected";
        return true;
    }
    if (cmd == "battery") {
        if (!set_power_supply(run, w))
            return fail(scene, line, "usage: battery off|mains|PCT discharging|charging|full|notcharging");
        return true;
    }
    if (cmd == "ssh_status") { /* after "home": the launcher reads Settings > SSH indicator there */
        if (!set_ssh_status(run, w)) return fail(scene, line, "usage: ssh_status off|N [idle|active] (N 1..8)");
        return true;
    }
    if (cmd == "backlight") { /* detected once per launcher run: before the first drawing command */
        if (run.booted) return fail(scene, line, "backlight must come before the first drawing command");
        if (w.size() != 2 || !set_backlight(run, w[1])) return fail(scene, line, "usage: backlight levels|onoff|none");
        return true;
    }
    if (cmd == "sound") { /* looked up as Settings opens */
        if (w.size() != 2 || !set_sound(run, w[1])) return fail(scene, line, "usage: sound off|hdmi|card|bluetooth");
        return true;
    }
    if (cmd == "config") {
        if (w.size() != 3) return fail(scene, line, "usage: config KEY VALUE");
        harness::config_set(w[1], w[2]);
        return true;
    }
    if (cmd == "ntp") {
        if (w.size() != 2 || (w[1] != "on" && w[1] != "off")) return fail(scene, line, "usage: ntp on|off");
        fx.ntp_on = w[1] == "on";
        return true;
    }
    if (cmd == "timeset") {
        if (w.size() != 2 || (w[1] != "ok" && w[1] != "fail")) return fail(scene, line, "usage: timeset ok|fail");
        fx.time_settable = w[1] == "ok";
        return true;
    }
    if (cmd == "tz") {
        if (w.size() != 2) return fail(scene, line, "usage: tz ZONE|none");
        std::lock_guard<std::mutex> lock(fx.tz_mutex);
        fx.timezone = w[1] == "none" ? std::string() : w[1];
        return true;
    }
    if (cmd == "tzlist") {
        if (w.size() != 2 || (w[1] != "on" && w[1] != "off")) return fail(scene, line, "usage: tzlist on|off");
        std::lock_guard<std::mutex> lock(fx.tz_mutex);
        fx.timezone_list = w[1] == "on";
        return true;
    }
    if (cmd == "tzset") {
        if (w.size() != 2 || (w[1] != "ok" && w[1] != "fail" && w[1] != "denied"))
            return fail(scene, line, "usage: tzset ok|fail|denied");
        std::lock_guard<std::mutex> lock(fx.tz_mutex);
        fx.timezone_set_result = w[1] == "ok" ? 0 : w[1] == "fail" ? -1 : -13;
        return true;
    }
    if (cmd == "keyboards") {
        if (w.size() != 2) return fail(scene, line, "usage: keyboards N");
        fx.keyboards = static_cast<unsigned>(std::max(0, to_int(w[1])));
        return true;
    }
    if (cmd == "configfull") {
        if (w.size() != 2 || (w[1] != "on" && w[1] != "off")) return fail(scene, line, "usage: configfull on|off");
        fx.config_full = w[1] == "on";
        return true;
    }
    if (cmd == "update") { /* Settings > Updates' fixtures (harness_update.cpp, report 047 P1) */
        static const char *usage = "usage: update net on|off | clock on|off | answer FILE|pending|ratelimit|error|dns|"
                                   "notjson | build VERSION CHANNEL [ARCH] | state KEY VALUE | cache FILE";
        if (w.size() < 3) return fail(scene, line, usage);
        std::lock_guard<std::mutex> lock(fx.update_mutex);
        if ((w[1] == "net" || w[1] == "clock") && w.size() == 3 && (w[2] == "on" || w[2] == "off")) {
            (w[1] == "net" ? fx.update_network : fx.update_clock) = w[2] == "on";
        } else if (w[1] == "answer" && w.size() == 3) {
            fx.update_answer = w[2];
        } else if (w[1] == "build" && (w.size() == 4 || w.size() == 5)) {
            fx.update_version = w[2];
            fx.update_channel = w[3];
            if (w.size() == 5) fx.update_arch = w[4];
        } else if (w[1] == "state" && w.size() == 4) {
            fx.update_seed_state.emplace_back(w[2], w[3]);
        } else if (w[1] == "cache" && w.size() == 3) {
            fx.update_seed_cache = w[2];
        } else {
            return fail(scene, line, usage);
        }
        return true;
    }

    boot(run);

    if (cmd == "apps") {
        std::vector<harness::AppSpec> apps;
        for (std::size_t i = 1; i < w.size(); ++i) {
            harness::AppSpec spec;
            spec.name = w[i];
            std::string icon;
            const std::size_t eq = spec.name.find('=');
            if (eq != std::string::npos) {
                icon = spec.name.substr(eq + 1);
                spec.name.erase(eq);
            } else {
                icon = icon_for(spec.name);
            }
            if (icon == "none" || icon == "stale") /* no icon (harness_launch.hpp AppSpec::empty) */
                spec.empty = icon == "none" ? harness::AppSpec::Empty::Zeroed : harness::AppSpec::Empty::Stale;
            else if (icon.compare(0, 8, "fixture:") == 0)
                spec.icon = "A:" + fx.fixture_root + "/" + icon.substr(8);
            else
                spec.icon = harness::resolve_resource(icon); /* launcher_platform::path(desc.icon) */
            spec.kind = spec.name == "Settings"     ? "settings"
                        : spec.name == "Calculator" ? "calculator"
                        : spec.name == "SSH"        ? "ssh"
                        : spec.name == "CLI"        ? "cli"
                                                    : "tile";
            apps.push_back(spec);
        }
        harness::set_apps(apps);
    } else if (cmd == "home") {
        native_ui::refresh_apps();
        native_ui::show_home();
        settle(300);
    } else if (cmd == "launch") {
        if (w.size() != 2) return fail(scene, line, "usage: launch NAME");
        const int index = harness::app_index(w[1]);
        if (index < 0 || !harness::launcher()) return fail(scene, line, "no tile named " + w[1]);
        harness::launcher()->launch_index(static_cast<std::size_t>(index));
        settle(600);
    } else if (cmd == "stockapp") {
        /* native_ui::run_external() without the process: toolbar chrome, compat window owned by
         * the app's picture, which the real external blit scales into the window. */
        native_ui::begin_page(false, false);
        cp0_display_set_external(1);
        static const std::vector<uint32_t> picture = stock_placeholder();
        cp0_display_external_blit(picture.data(), 320, 170, 320 * 4);
        settle(300);
    } else if (cmd == "compatpage") {
        /* a compat page (a legacy page, SSH, a terminal): the compat window is default and shows the page's own
         * screen (a flat placeholder with a label), no external app owns the picture */
        native_ui::begin_page(false, false);
        lv_obj_t *screen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1c2a3a), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_t *label = lv_label_create(screen);
        lv_label_set_text(label, "COMPAT PAGE 320x170");
        lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 8, 40);
        lv_screen_load(screen);
        settle(300);
    } else if (cmd == "key") {
        if (w.size() < 2 || w.size() > 3) return fail(scene, line, "usage: key [CTRL+]NAME [press|release|tap]");
        /* CTRL+NAME: the key with KBD_MOD_CTRL, as a physical keyboard or the pointer's held click (Ctrl+O) sends it */
        const bool ctrl = w[1].compare(0, 5, "CTRL+") == 0;
        const std::string name = ctrl ? w[1].substr(5) : w[1];
        const uint32_t mods = ctrl ? KBD_MOD_CTRL : 0;
        static const uint32_t letters[26] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
                                             KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
                                             KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
        /* a letter only with CTRL+ (no text: a control key); plain letters are `type` / `shortcut` */
        const uint32_t code = ctrl && name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z' ? letters[name[0] - 'A']
                                                                                            : key_code(name);
        if (!code) return fail(scene, line, "unknown key " + w[1]);
        const std::string mode = w.size() == 3 ? w[2] : "tap";
        if (mode == "press" || mode == "tap") {
            cp0_keyboard_inject(code, KBD_KEY_PRESSED, mods);
            settle(60);
        }
        if (mode == "release" || mode == "tap") {
            cp0_keyboard_inject(code, KBD_KEY_RELEASED, mods);
            settle(300);
        }
    } else if (cmd == "type") {
        /* printable characters, as the keyboard thread queues them (key code + UTF-8) */
        if (w.size() != 2) return fail(scene, line, "usage: type TEXT");
        for (char c : w[1]) {
            const char text[2] = {c, 0};
            uint32_t code = 0;
            if (c >= '1' && c <= '9') code = KEY_1 + static_cast<uint32_t>(c - '1');
            else if (c == '0') code = KEY_0;
            else if (c >= 'a' && c <= 'z') code = key_code(std::string(1, static_cast<char>(c - 'a' + 'A')));
            else if (c == '.') code = KEY_DOT;
            else if (c == '-') code = KEY_MINUS;
            else if (c == '/') code = KEY_SLASH;
            else if (c == '=' || c == '+') code = KEY_EQUAL;
            else if (c == '*') code = KEY_8;
            harness_keyboard_inject_text(code, KBD_KEY_PRESSED, text);
            settle(40);
            harness_keyboard_inject_text(code, KBD_KEY_RELEASED, text);
            settle(40);
        }
        settle(200);
    } else if (cmd == "shortcut") {
        /* shortcut LETTER: a letter key with its key code and its text, as a physical keyboard sends it outside a text
         * field (page shortcuts read by key code: the Apps page's U, S, A, D; "type" sends letters without a code) */
        if (w.size() != 2 || w[1].size() != 1 || w[1][0] < 'a' || w[1][0] > 'z')
            return fail(scene, line, "usage: shortcut a-z");
        static const uint32_t letters[26] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
                                             KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
                                             KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
        const uint32_t code = letters[w[1][0] - 'a'];
        harness_keyboard_inject_text(code, KBD_KEY_PRESSED, w[1].c_str());
        settle(40);
        harness_keyboard_inject_text(code, KBD_KEY_RELEASED, w[1].c_str());
        settle(300);
    } else if (cmd == "ribbon") {
        /* external_poll() in native_ui.cpp when the runner raises the hold-Esc hint over a stock app */
        cp0_display_set_overlay_rect(CP0_UI_RIBBON_X, CP0_UI_RIBBON_Y, CP0_UI_RIBBON_W, CP0_UI_RIBBON_H);
        launcher_toast().show_persistent("Hold ESC 3s to return home");
        settle(200);
    } else if (cmd == "touch") {
        if (w.size() != 3) return fail(scene, line, "usage: touch X Y   (finger stays down until release)");
        touch_at(run, 1, to_int(w[1]), to_int(w[2]));
        settle(150);
    } else if (cmd == "release") {
        touch_at(run, 0, 0, 0);
        settle(400);
    } else if (cmd == "tap") {
        if (w.size() != 3) return fail(scene, line, "usage: tap X Y");
        touch_at(run, 1, to_int(w[1]), to_int(w[2]));
        settle(80);
        touch_at(run, 0, to_int(w[1]), to_int(w[2]));
        settle(400);
    } else if (cmd == "tapkey") {
        /* tapkey LABEL: a tap through the touch path on the centre of the on-screen keyboard key whose label is LABEL
         * (the native display's top layer). SHIFT, ENTER, BACKSPACE, LEFT, UP, DOWN, RIGHT, CLOSE (the cross that
         * brings the writing zone back, report 042) name the symbol keys. */
        if (w.size() != 2) return fail(scene, line, "usage: tapkey LABEL");
        static const std::map<std::string, std::string> symbols = {
            {"SHIFT", "\xe2\x87\xa7"}, {"ENTER", LV_SYMBOL_NEW_LINE}, {"BACKSPACE", LV_SYMBOL_BACKSPACE},
            {"LEFT", LV_SYMBOL_LEFT},  {"UP", LV_SYMBOL_UP},          {"DOWN", LV_SYMBOL_DOWN},
            {"RIGHT", LV_SYMBOL_RIGHT}, {"CLOSE", LV_SYMBOL_CLOSE},
            {"SPACE", ""}, /* the space bar: the keyboard's only key without a label */
        };
        const auto symbol = symbols.find(w[1]);
        const std::string text = symbol != symbols.end() ? symbol->second : w[1];
        lv_obj_t *label = find_label(lv_display_get_layer_top(cp0_display_native()), {text});
        lv_obj_t *key = label ? lv_obj_get_parent(label) : nullptr;
        if (!key) return fail(scene, line, "no on-screen keyboard key labelled " + w[1]);
        lv_area_t area;
        lv_obj_get_coords(key, &area);
        const int x = (area.x1 + area.x2) / 2, y = (area.y1 + area.y2) / 2;
        touch_at(run, 1, x, y);
        settle(80);
        touch_at(run, 0, x, y);
        settle(400);
    } else if (cmd == "tapbutton") {
        /* tapbutton TEXT: a tap through the touch path on the centre of the object holding the first visible label TEXT
         * on the native display's screen (a button of a native page, a cell of the Strokes training picker) */
        if (w.size() != 2) return fail(scene, line, "usage: tapbutton TEXT");
        lv_obj_t *label = find_label(lv_display_get_screen_active(cp0_display_native()), {w[1]});
        lv_obj_t *button = label ? lv_obj_get_parent(label) : nullptr;
        if (!button) return fail(scene, line, "no visible label " + w[1]);
        lv_area_t area;
        lv_obj_get_coords(button, &area);
        const int x = (area.x1 + area.x2) / 2, y = (area.y1 + area.y2) / 2;
        touch_at(run, 1, x, y);
        settle(80);
        touch_at(run, 0, x, y);
        settle(400);
    } else if (cmd == "touchbutton") {
        /* touchbutton TEXT: as tapbutton, but the finger stays down (until release): a hold on a home tile (study 046
         * P2: `touchbutton Calculator`, `wait 3200`, `release` picks it up) */
        if (w.size() != 2) return fail(scene, line, "usage: touchbutton TEXT   (finger stays down until release)");
        lv_obj_t *label = find_label(lv_display_get_screen_active(cp0_display_native()), {w[1]});
        lv_obj_t *button = label ? lv_obj_get_parent(label) : nullptr;
        if (!button) return fail(scene, line, "no visible label " + w[1]);
        lv_area_t area;
        lv_obj_get_coords(button, &area);
        touch_at(run, 1, (area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
        settle(150);
    } else if (cmd == "tapfield") {
        /* tapfield LABEL: a tap on the field under the TextField label LABEL (the label's next sibling) */
        if (w.size() != 2) return fail(scene, line, "usage: tapfield LABEL");
        lv_obj_t *label = find_label(lv_display_get_screen_active(cp0_display_native()), {w[1]});
        lv_obj_t *row = label ? lv_obj_get_parent(label) : nullptr;
        const int32_t at = label ? lv_obj_get_index(label) : -1;
        lv_obj_t *field = row && at >= 0 && static_cast<uint32_t>(at + 1) < lv_obj_get_child_count(row)
                              ? lv_obj_get_child(row, at + 1)
                              : nullptr;
        if (!field) return fail(scene, line, "no field under a label " + w[1]);
        lv_area_t area;
        lv_obj_get_coords(field, &area);
        const int x = (area.x1 + area.x2) / 2, y = (area.y1 + area.y2) / 2;
        touch_at(run, 1, x, y);
        settle(80);
        touch_at(run, 0, x, y);
        settle(400);
    } else if (cmd == "drag") {
        if (w.size() < 5) return fail(scene, line, "usage: drag X1 Y1 X2 Y2 [STEPS]");
        const int x1 = to_int(w[1]), y1 = to_int(w[2]), x2 = to_int(w[3]), y2 = to_int(w[4]);
        const int steps = w.size() >= 6 ? std::max(1, to_int(w[5])) : 10;
        for (int i = 0; i <= steps; ++i) {
            touch_at(run, 1, x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps);
            settle(35);
        }
        touch_at(run, 0, x2, y2);
        settle(600);
    } else if (cmd == "sudoprompt") {
        if (w.size() != 2) return fail(scene, line, "usage: sudoprompt open|busy|wrong|close");
        if (w[1] == "open") {
            if (!sudo_prompt_open()) return fail(scene, line, "the sudo prompt could not be built");
        } else if (w[1] == "busy" && g_sudo_demo.ui) {
            g_sudo_demo.ui->show_busy("Authenticating...");
        } else if (w[1] == "wrong" && g_sudo_demo.ui) {
            g_sudo_demo.ui->show_error(cp0_sudo::auth_error_text(1));
        } else if (w[1] == "close") {
            sudo_prompt_close();
        } else {
            return fail(scene, line, "usage: sudoprompt open|busy|wrong|close (open first)");
        }
        settle(400);
    } else if (cmd == "listtitle") {
        /* listtitle PART...: the list demo page's title path (Page::set_title), one word per part; a quoted word may
         * itself be a breadcrumb ("Net Toolbox > Diagnosis", as an app writing its path as one string) */
        if (w.size() < 2) return fail(scene, line, "usage: listtitle PART...");
        if (!g_list_demo.page) return fail(scene, line, "listtitle needs a list page (listdemo compact N first)");
        g_list_demo.page->set_title(std::vector<std::string>(w.begin() + 1, w.end()));
        settle(200);
    } else if (cmd == "listdemo") {
        /* listdemo compact|comfortable ROWS [readonly] [separators] [wrap] [empty TEXT...]: a CompactList page;
         * listdemo update: refresh the values in place (a live list's set_rows) */
        if (w.size() == 2 && w[1] == "update") {
            if (!g_list_demo.list) return fail(scene, line, "listdemo update needs a list (listdemo compact N first)");
            static int generation = 0;
            const bool comfortable = g_list_demo.list->layout().density == CP0_UI_LIST_COMFORTABLE;
            g_list_demo.rows = list_demo_rows(comfortable, g_list_demo.list->count(), ++generation, g_list_demo.kinds);
            g_list_demo.list->set_rows(g_list_demo.rows);
            settle(200);
            return true;
        }
        /* listdemo value|row INDEX normal|ok|warn|bad|muted TEXT: one row's value and tone in place, through
         * set_value (value) or update_row (row, same label) */
        if (w.size() == 5 && (w[1] == "value" || w[1] == "row")) {
            if (!g_list_demo.list) return fail(scene, line, "listdemo " + w[1] + " needs a list (listdemo compact N first)");
            using cp0::ui::ValueTone;
            static const std::map<std::string, ValueTone> tones = {{"normal", ValueTone::Normal}, {"ok", ValueTone::Ok},
                                                                   {"warn", ValueTone::Warn},     {"bad", ValueTone::Bad},
                                                                   {"muted", ValueTone::Muted}};
            const auto tone = tones.find(w[3]);
            const int index = to_int(w[2], -1);
            if (tone == tones.end() || index < 0 || index >= g_list_demo.list->count())
                return fail(scene, line, "usage: listdemo value|row INDEX normal|ok|warn|bad|muted TEXT");
            if (w[1] == "value") {
                g_list_demo.list->set_value(index, w[4], tone->second);
            } else {
                cp0::ui::ListRow row = g_list_demo.list->row(index);
                row.value = w[4];
                row.tone = tone->second;
                g_list_demo.list->update_row(index, row);
            }
            settle(200);
            return true;
        }
        /* listdemo kind|setkind INDEX unspecified|info|open|edit|action|action-running: one row's affordance in place
         * (same label, value and tone), through update_row (kind) or the live refresh set_rows with every other row
         * unchanged (setkind) */
        if (w.size() == 4 && (w[1] == "kind" || w[1] == "setkind")) {
            if (!g_list_demo.list)
                return fail(scene, line, "listdemo " + w[1] + " needs a list (listdemo compact N first)");
            using cp0::ui::RowAffordance;
            static const std::map<std::string, RowAffordance> kinds = {
                {"unspecified", RowAffordance::Unspecified}, {"info", RowAffordance::Info},
                {"open", RowAffordance::Open},               {"edit", RowAffordance::Edit},
                {"action", RowAffordance::Action},           {"action-running", RowAffordance::ActionRunning}};
            const auto kind = kinds.find(w[3]);
            const int index = to_int(w[2], -1);
            if (kind == kinds.end() || index < 0 || index >= g_list_demo.list->count())
                return fail(scene, line, "usage: listdemo kind|setkind INDEX "
                                         "unspecified|info|open|edit|action|action-running");
            if (w[1] == "kind") {
                cp0::ui::ListRow row = g_list_demo.list->row(index);
                row.affordance = kind->second;
                g_list_demo.list->update_row(index, row);
            } else {
                std::vector<cp0::ui::ListRow> rows;
                for (int i = 0; i < g_list_demo.list->count(); ++i) rows.push_back(g_list_demo.list->row(i));
                rows[static_cast<size_t>(index)].affordance = kind->second;
                g_list_demo.list->set_rows(rows);
            }
            settle(200);
            return true;
        }
        if (w.size() < 3 || (w[1] != "compact" && w[1] != "comfortable"))
            return fail(scene, line, "usage: listdemo compact|comfortable ROWS [readonly] [separators] [wrap] [kinds] "
                                     "[empty TEXT...] | listdemo update | listdemo value|row INDEX TONE TEXT | "
                                     "listdemo kind|setkind INDEX KIND");
        bool read_only = false, separators = false, wrap = false, kinds = false;
        std::string empty;
        for (std::size_t i = 3; i < w.size(); ++i) {
            if (w[i] == "readonly") read_only = true;
            else if (w[i] == "separators") separators = true;
            else if (w[i] == "wrap") wrap = true;
            else if (w[i] == "kinds") kinds = true;
            else if (w[i] == "empty") {
                for (std::size_t j = i + 1; j < w.size(); ++j) empty += (j > i + 1 ? " " : "") + w[j];
                break;
            } else return fail(scene, line, "listdemo: unknown option " + w[i]);
        }
        list_demo_open(w[1] == "comfortable", std::max(0, to_int(w[2])), read_only, separators, wrap, kinds, empty);
        settle(200);
    } else if (cmd == "toast") {
        std::string text;
        for (std::size_t i = 1; i < w.size(); ++i) text += (i > 1 ? " " : "") + w[i];
        for (std::size_t at; (at = text.find("\\n")) != std::string::npos;) text.replace(at, 2, "\n");
        if (text == "off") launcher_toast().hide(); /* toast off: hide the persistent toast */
        else launcher_toast().show_persistent(text.c_str());
        settle(200);
    } else if (cmd == "osd") {
        /* osd level TITLE PCT | osd mute on|off: the media OSD of the brightness / volume keys
         * (ui_global_hint.cpp::on_key). It hides itself after 1000 ms; a shot settles 700 ms after this. */
        if (w.size() == 4 && w[1] == "level") {
            const int pct = to_int(w[3]);
            const char *icon = w[2] == "Brightness" ? LV_SYMBOL_TINT : pct == 0 ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX;
            launcher_media_osd().show_level(w[2].c_str(), icon, pct);
        } else if (w.size() == 3 && w[1] == "mute" && (w[2] == "on" || w[2] == "off")) {
            launcher_media_osd().show_mute(w[2] == "on");
        } else {
            return fail(scene, line, "usage: osd level TITLE PCT | osd mute on|off");
        }
        settle(200);
    } else if (cmd == "loading") {
        /* loading TEXT | loading off: the "Loading..." overlay of an app launch (ui_loading.cpp) */
        if (w.size() < 2) return fail(scene, line, "usage: loading TEXT | loading off");
        if (w.size() == 2 && w[1] == "off") {
            ui_loading::hide();
        } else {
            std::string text;
            for (std::size_t i = 1; i < w.size(); ++i) text += (i > 1 ? " " : "") + w[i];
            ui_loading::show(text.c_str());
        }
        settle(200);
    } else if (cmd == "screensaver") {
        native_screensaver::init(); /* ui_screensaver_init() does this on the Pi */
        settle(50);
    } else if (cmd == "metrics") {
        /* what the layout service computed for this size, plus what the launcher exported to its children */
        if (w.size() != 2) return fail(scene, line, "usage: metrics NAME");
        /* the panel (report 041 gave the pages an app area above the writing zone; since 042 it is the whole panel) */
        const cp0_ui_metrics_t *m = cp0_ui_metrics_panel();
        char text[2048];
        if (!m || cp0_ui_metrics_describe(m, text, sizeof(text)) < 0)
            return fail(scene, line, "the display manager published no metrics");
        std::string report = text;
        if (const cp0_ui_metrics_t *area = cp0_ui_metrics_current()) {
            if (area->h != m->h && cp0_ui_metrics_describe(area, text, sizeof(text)) >= 0)
                report += "app area (the pages' metrics, above the writing zone)\n" + std::string(text);
        }
        if (cp0_zone_ui_active()) report += std::string("zone ") + cp0_zone_ui_describe() + "\n";
        report += "environment";
        /* the panel variables every child inherits, as "export ...; " for run.sh contract (app contract 1) */
        std::string panel_exports = "export";
        for (const char *name : {"APPLAUNCH_SCREEN_W", "APPLAUNCH_SCREEN_H", "APPLAUNCH_SCREEN_ROTATE",
                                 "APPLAUNCH_SCREEN_PPMM", "APPLAUNCH_SCREEN_CLASS", "APPLAUNCH_SCREEN_FB",
                                 "APPLAUNCH_SCREEN_TOUCH"}) {
            const char *value = std::getenv(name);
            report += std::string(" ") + name + "=" + (value ? value : "(unset)");
            if (value) panel_exports += std::string(" ") + name + "=" + app_launch_env::shell_quote(value);
        }
        report += "\n";
        /* what run_external (compat) and launch_Exec (fullscreen) put in front of an app's command line */
        const std::string compat_env = app_launch_env::export_for_current(app_launch_env::Mode::Compat);
        const std::string full_env = app_launch_env::export_for_current(app_launch_env::Mode::Fullscreen);
        report += "launch compat     " + compat_env + "\n";
        report += "launch fullscreen " + full_env + "\n";
        const std::string base = run.out_dir + "/" + run.size.name() + "/" + w[1];
        std::ofstream(base + ".compat.env") << panel_exports << "; " << compat_env << "\n";
        std::ofstream(base + ".fullscreen.env") << panel_exports << "; " << full_env << "\n";
        /* report 038 S2: on a portrait canvas, what a landscape fullscreen app run turned gets (desktop_fit Turned) */
        if (m->h > m->w) {
            const std::string turned_env = app_launch_env::export_for_current(app_launch_env::Mode::Fullscreen, true);
            report += "launch turned     " + turned_env + "\n";
            std::ofstream(base + ".turned.env") << panel_exports << "; " << turned_env << "\n";
        }
        /* the layout service's own numbers, for run.sh contract to check what the probe read against */
        std::ofstream(base + ".facts") << "size=" << m->w << "x" << m->h << "\nclass=" << cp0_ui_class_name(m->cls)
                                       << "\nppmm=" << m->ppmm_x100 << "\nrot=" << m->rot << "\ncompat=" << m->compat.x
                                       << "," << m->compat.y << "," << m->compat.w << "," << m->compat.h
                                       << "\nscale=" << m->compat.scale << "\n";
        char state_path[1024];
        if (cp0_ui_state_path(std::getenv("XDG_RUNTIME_DIR"), state_path, sizeof(state_path)) == 0) {
            std::ifstream state(state_path);
            std::stringstream body;
            body << state.rdbuf();
            report += "screen.state\n" + body.str();
        }
        const std::string path = run.out_dir + "/" + run.size.name() + "/" + w[1] + ".txt";
        std::ofstream out(path);
        out << report;
        if (!out) return fail(scene, line, "cannot write " + path);
        std::printf("%s\n", path.c_str());
    } else if (cmd == "header") {
        /* header NAME TITLE [TITLE ...]: the home grid's status strip rectangle (from the strip's leftmost drawn
         * pixel, its Bluetooth icon, to the right edge, bar_h high) cut out as out/<WxH>/header/NAME.png, and the
         * screen origin of the title label (the first visible label on the native screen whose text is one of
         * TITLE ...) as out/<WxH>/header/NAME.txt ("x y text"). run.sh header requires both to equal the home grid's
         * (header home). The home grid has no title (rebranding plan 025): without TITLE the origin written is the
         * shell's title origin (the layout service's shell.title_x / title_y, where every page puts its title), and
         * no visible label may stand there. */
        if (w.size() < 2) return fail(scene, line, "usage: header NAME [TITLE ...]");
        const cp0_ui_metrics_t *m = cp0_ui_metrics_current();
        if (!m) return fail(scene, line, "the display manager published no metrics");
        settle(500);
        lv_refr_now(nullptr);
        const int strip_x = std::clamp(cp0::ui::status_strip_left(), 0, run.size.w - 1);
        const int strip_h = m->shell.bar_h;
        const std::string dir = run.out_dir + "/" + run.size.name() + "/header/";
        if (!save_png(dir + w[1] + ".png", crop_image(grab(run), strip_x, 0, run.size.w - strip_x, strip_h)))
            return fail(scene, line, "cannot write " + dir + w[1] + ".png");
        if (w.size() == 2) {
            const int tx = m->shell.title_x, ty = m->shell.title_y;
            if (lv_obj_t *stray = find_label_at(lv_display_get_screen_active(cp0_display_native()), tx, ty))
                return fail(scene, line, std::string("a title label stands at the title origin: ") +
                                             lv_label_get_text(stray));
            std::ofstream out(dir + w[1] + ".txt");
            out << tx << " " << ty << " (no title)\n";
            if (!out) return fail(scene, line, "cannot write " + dir + w[1] + ".txt");
            std::printf("%s%s.png\n", dir.c_str(), w[1].c_str());
            return true;
        }
        const std::vector<std::string> titles(w.begin() + 2, w.end());
        lv_obj_t *title = find_label(lv_display_get_screen_active(cp0_display_native()), titles);
        if (!title) return fail(scene, line, "no title label with the text " + w[2] + " on the native screen");
        lv_area_t area;
        lv_obj_get_coords(title, &area);
        std::ofstream out(dir + w[1] + ".txt");
        out << area.x1 << " " << area.y1 << " " << lv_label_get_text(title) << "\n";
        if (!out) return fail(scene, line, "cannot write " + dir + w[1] + ".txt");
        std::printf("%s%s.png\n", dir.c_str(), w[1].c_str());
    } else if (cmd == "btagent") {
        /* btagent METHOD ADDRESS [VALUE]: a BlueZ agent request as the backend sends it (task 014 P3c) */
        if (w.size() < 3 || w.size() > 4) return fail(scene, line, "usage: btagent METHOD ADDRESS [VALUE]");
        harness::bt_agent_request(w[1], w[2], w.size() == 4 ? w[3] : std::string());
        settle(400);
    } else if (cmd == "btkeytyped") {
        /* the remote keyboard typed the passkey shown for it: the pending pairing completes */
        harness::bt_key_typed();
        settle(400);
    } else if (cmd == "ptyout" || cmd == "ptyprompt") {
        /* ptyout TEXT...: what the program in the fake PTY prints next ("\n" = CR LF, "\e" = Esc); ptyprompt TEXT: the
         * prompt the fake shell prints after Enter and ^C (task 014 P4b) */
        std::string text;
        for (std::size_t i = 1; i < w.size(); ++i) text += (i > 1 ? " " : "") + w[i];
        if (cmd == "ptyout") harness::pty_output(text);
        else harness::pty_set_prompt(text);
        settle(300);
    } else if (cmd == "ptyexit") {
        /* the program in the fake PTY exits: the terminal says "Press any key to exit" */
        harness::pty_exit();
        settle(300);
    } else if (cmd == "ptylog") {
        /* ptylog NAME: what the terminal did with the fake PTY (open, every write as bytes, resize, close) as
         * out/<WxH>/NAME.txt */
        if (w.size() != 2) return fail(scene, line, "usage: ptylog NAME");
        const std::string path = run.out_dir + "/" + run.size.name() + "/" + w[1] + ".txt";
        std::ofstream out(path);
        for (const std::string &entry : harness::pty_log()) out << entry << "\n";
        if (!out) return fail(scene, line, "cannot write " + path);
        std::printf("%s\n", path.c_str());
    } else if (cmd == "stroke") {
        /* stroke letters|digits X,Y X,Y ... [hold]: a pen stroke through the touch path into the writing zone (report
         * 041): the router, the recogniser, the ink, what it types. The points are px from the box's top-left corner;
         * between two of them a frame every 1 mm, each read at once, 10 ms of the fake clock apart (about 100 mm/s); one
         * point is a tap. Then the lift and 400 ms (the heal window, the result, the ink's fade), unless "hold": the
         * pen stays down (the ink of a stroke in progress). */
        cp0_zone_rect_t box{};
        if (w.size() < 3 || (w[1] != "letters" && w[1] != "digits") || !zone_rect(w[1], &box))
            return fail(scene, line, "usage: stroke letters|digits X,Y ... [hold] (profile pda: the writing zone)");
        const bool hold = w.back() == "hold";
        std::vector<std::pair<int, int>> pts;
        for (std::size_t i = 2; i < w.size() - (hold ? 1 : 0); ++i) {
            int x = 0, y = 0;
            if (std::sscanf(w[i].c_str(), "%d,%d", &x, &y) != 2) return fail(scene, line, "stroke: bad point " + w[i]);
            pts.emplace_back(box.x + x, box.y + y);
        }
        if (pts.empty()) return fail(scene, line, "stroke: no point");
        const double step = std::max(1.0, cp0_ui_metrics_panel()->ppmm_x100 / 100.0); /* 1 mm */
        auto frame = [&](int x, int y) {
            touch_at(run, 1, x, y);
            g_tick += 10;
            read_touch_now();
        };
        frame(pts[0].first, pts[0].second);
        for (std::size_t i = 1; i < pts.size(); ++i) {
            const double dx = pts[i].first - pts[i - 1].first, dy = pts[i].second - pts[i - 1].second;
            const int n = std::max(1, static_cast<int>(std::ceil(std::hypot(dx, dy) / step)));
            for (int k = 1; k <= n; ++k)
                frame(pts[i - 1].first + static_cast<int>(std::lround(dx * k / n)),
                      pts[i - 1].second + static_cast<int>(std::lround(dy * k / n)));
        }
        if (hold) {
            settle(50);
        } else {
            touch_at(run, 0, pts.back().first, pts.back().second);
            g_tick += 10;
            read_touch_now();
            settle(400);
        }
    } else if (cmd == "zonekey") {
        /* zonekey NAME [hold MS]: a tap (or a press held MS ms) through the touch path on the centre of a key of the
         * strip (Keyb Tab Ctrl Alt Left Up Down Right Enter; the band shown) */
        cp0_zone_rect_t r{};
        if ((w.size() != 2 && w.size() != 4) || !zone_rect(w[1], &r) || w[1] == "letters" || w[1] == "digits")
            return fail(scene, line, "usage: zonekey Keyb|Tab|Ctrl|Alt|Left|Up|Down|Right|Enter [hold MS]");
        const int held = w.size() == 4 && w[2] == "hold" ? std::max(0, to_int(w[3])) : 80;
        touch_at(run, 1, r.x + r.w / 2, r.y + r.h / 2);
        settle(static_cast<uint32_t>(held));
        touch_at(run, 0, r.x + r.w / 2, r.y + r.h / 2);
        settle(400);
    } else if (cmd == "expect_zone") {
        /* expect_zone shown|hidden: the writing zone's band (the strip and the boxes, report 042) */
        if (w.size() != 2 || (w[1] != "shown" && w[1] != "hidden")) return fail(scene, line, "usage: expect_zone shown|hidden");
        if (!cp0_zone_ui_active()) return fail(scene, line, "expect_zone: no writing zone");
        const bool shown = cp0_zone_ui_shown() != 0;
        if (shown != (w[1] == "shown"))
            return fail(scene, line, std::string("expect_zone: the band is ") + (shown ? "shown" : "hidden"));
        std::printf("expect_zone ok: %s\n", w[1].c_str());
    } else if (cmd == "expect_osk") {
        /* expect_osk shown|hidden: the standard on-screen keyboard (cp0::ui::osk) */
        if (w.size() != 2 || (w[1] != "shown" && w[1] != "hidden")) return fail(scene, line, "usage: expect_osk shown|hidden");
        const bool shown = cp0::ui::osk::visible();
        if (shown != (w[1] == "shown"))
            return fail(scene, line, std::string("expect_osk: the keyboard is ") + (shown ? "shown" : "hidden"));
        std::printf("expect_osk ok: %s\n", w[1].c_str());
    } else if (cmd == "expect_mode") {
        /* expect_mode N: Settings > On-screen keyboard as the launcher reads it (launcher_osk::mode(): 0 Auto, 2 On,
         * 3 Handwriting + keyboard; a stored 1, the old Off, reads as 0) */
        if (w.size() != 2) return fail(scene, line, "usage: expect_mode 0|2|3");
        if (launcher_osk::mode() != to_int(w[1]))
            return fail(scene, line, "expect_mode: " + std::to_string(launcher_osk::mode()));
        std::printf("expect_mode ok: %s\n", w[1].c_str());
    } else if (cmd == "expect_vkb") {
        /* expect_vkb auto|off|force|zone: the on-screen keyboard rule launcher_osk publishes (input.state vkb=) */
        if (w.size() != 2) return fail(scene, line, "usage: expect_vkb auto|off|force|zone");
        if (fx.vkb != w[1]) return fail(scene, line, "expect_vkb: published " + fx.vkb);
        std::printf("expect_vkb ok: %s\n", w[1].c_str());
    } else if (cmd == "expect_hand") {
        /* expect_hand 0|1: what launcher_osk publishes as input.state hand= (report 044: Handwriting + keyboard with the
         * writing zone, so an app run upright shows its own band) */
        if (w.size() != 2 || (w[1] != "0" && w[1] != "1")) return fail(scene, line, "usage: expect_hand 0|1");
        if (fx.hand != to_int(w[1])) return fail(scene, line, "expect_hand: published " + std::to_string(fx.hand));
        std::printf("expect_hand ok: %s\n", w[1].c_str());
    } else if (cmd == "turnedapp") {
        /* turnedapp on|off: a landscape fullscreen app run turned takes the panel (report 038 S2): the launcher is
         * suspended as by the supervisor, the app draws its landscape frame over the whole panel (no writing zone, no
         * strip: it has its own display backend and canvas), its keyboard rule is the usual one (launcher_zone); off:
         * the launcher takes the panel back and draws everything again, the zone included */
        if (w.size() != 2 || (w[1] != "on" && w[1] != "off")) return fail(scene, line, "usage: turnedapp on|off");
        if (w[1] == "on") {
            launcher_zone::set_app_front(true);
            display_handoff::enter_app_front(nullptr);
            paint_turned_app(run);
            settle(300);
        } else {
            display_handoff::leave_app_front();
            launcher_zone::set_app_front(false);
            cp0_display_set_touch_swallow(2);
            settle(500);
            lv_refr_now(nullptr);
        }
    } else if (cmd == "wait") {
        if (w.size() != 2) return fail(scene, line, "usage: wait MS");
        settle(static_cast<uint32_t>(std::max(0, to_int(w[1]))), kWaitRealUs);
    } else if (cmd == "shot") {
        if (w.size() != 2) return fail(scene, line, "usage: shot NAME");
        settle(500);
        lv_refr_now(nullptr);
        const std::string path = run.out_dir + "/" + run.size.name() + "/" + w[1] + ".png";
        if (!save_png(path, grab(run))) return fail(scene, line, "cannot write " + path);
        std::printf("%s\n", path.c_str());
    } else if (cmd == "panelshot") {
        /* panelshot NAME: the framebuffer as the panel shows it (its own orientation, pw x ph, no rotation undone):
         * which edge of the panel the picture's top is at (report 043, PDA mode On rotate left / right) */
        if (w.size() != 2) return fail(scene, line, "usage: panelshot NAME");
        settle(500);
        lv_refr_now(nullptr);
        size_t size = 0;
        int stride = 0;
        uint8_t *mem = harness_fb_memory(&size, &stride);
        Run panel = run;
        panel.size = Size{run.profile.pw, run.profile.ph};
        if (cp0_fbo_setup(&panel.view, mem, size, stride, run.profile.bpp / 8, run.profile.pw, run.profile.ph, 0,
                          run.profile.pw, run.profile.ph, nullptr) < 0)
            return fail(scene, line, "panelshot: no framebuffer view");
        const std::string path = run.out_dir + "/" + run.size.name() + "/" + w[1] + ".png";
        if (!save_png(path, grab(panel))) return fail(scene, line, "cannot write " + path);
        std::printf("%s\n", path.c_str());
    } else {
        return fail(scene, line, "unknown command " + cmd);
    }
    return true;
}

int run_scene_at(const Scene &scene, const Size &size, const std::string &out_dir)
{
    Run run;
    run.size = size;
    run.profile = make_profile(default_profile(size), size);
    run.ppmm = default_ppmm(size);
    run.out_dir = out_dir;
    set_power_supply(run, {"battery", "off"}); /* an earlier scene's battery never leaks into this one */
    set_backlight(run, "levels");               /* the deck's, as before detection (report 030) */
    set_sound(run, "off");
    for (const Line &line : scene.lines) {
        if (!line.only.empty() && std::find(line.only.begin(), line.only.end(), size) == line.only.end()) continue;
        if (!run_line(run, scene, line)) return 1;
    }
    return 0;
}

int cmd_render(int argc, char **argv, const std::string &harness_dir)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: render <scene> --out DIR [--sizes WxH,WxH]\n");
        return 2;
    }
    Scene scene;
    if (!load_scene(argv[2], scene)) return 2;
    std::string out_dir = harness_dir + "/out";
    for (int i = 3; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (option == "--sizes" && i + 1 < argc) {
            std::vector<Size> only;
            std::istringstream list(argv[++i]);
            std::string item;
            while (std::getline(list, item, ',')) {
                Size s;
                if (parse_size(item, s)) only.push_back(s);
            }
            std::vector<Size> kept;
            for (const Size &s : scene.sizes)
                if (std::find(only.begin(), only.end(), s) != only.end()) kept.push_back(s);
            scene.sizes = kept;
        } else {
            std::fprintf(stderr, "render: unknown option %s\n", option.c_str());
            return 2;
        }
    }
    if (out_dir.empty() || out_dir[0] != '/') { /* children chdir to the resource tree */
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) out_dir = std::string(cwd) + "/" + out_dir;
    }
    int failures = 0;
    std::vector<std::pair<pid_t, Size>> children;
    for (const Size &size : scene.sizes) {
        const std::string log_path = out_dir + "/" + size.name() + "/" + scene.name + ".log";
        mkdirs(out_dir + "/" + size.name());
        std::fflush(stdout);
        const pid_t pid = fork();
        if (pid == 0) {
            FILE *log = std::freopen(log_path.c_str(), "w", stderr);
            (void)log;
            const int rc = run_scene_at(scene, size, out_dir);
            std::fflush(stdout);
            std::fflush(stderr);
            std::_Exit(rc); /* skip LVGL and thread teardown: the child ends here */
        }
        if (pid < 0) {
            std::perror("fork");
            return 2;
        }
        children.emplace_back(pid, size);
    }
    for (const auto &child : children) {
        int status = 0;
        waitpid(child.first, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            ++failures;
            std::fprintf(stderr, "scene %s at %s failed (%s %d), see %s/%s/%s.log\n", scene.name.c_str(),
                         child.second.name().c_str(), WIFEXITED(status) ? "exit" : "signal",
                         WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status), out_dir.c_str(),
                         child.second.name().c_str(), scene.name.c_str());
        }
    }
    /* A review-only scene lists its shots in out/<WxH>/review.list: run.sh compare expects no golden for them. */
    if (scene.review)
        for (const Size &size : scene.sizes) {
            std::ofstream list(out_dir + "/" + size.name() + "/review.list", std::ios::app);
            for (const Line &line : scene.lines)
                if (line.words.size() == 2 && line.words[0] == "shot") list << line.words[1] << ".png\n";
        }
    /* One contact sheet per scene: a row per shot, a column per size. */
    std::vector<SheetRow> rows;
    for (const Line &line : scene.lines) {
        if (line.words.size() != 2 || line.words[0] != "shot") continue;
        if (std::any_of(rows.begin(), rows.end(), [&](const SheetRow &r) { return r.label == line.words[1]; }))
            continue;
        SheetRow row{line.words[1], {}};
        for (const Size &size : scene.sizes) row.paths.push_back(out_dir + "/" + size.name() + "/" + line.words[1] + ".png");
        rows.push_back(row);
    }
    if (!rows.empty() && make_sheet(out_dir + "/sheets/" + scene.name + ".png", scene.title, rows) != 0) ++failures;
    return failures ? 1 : 0;
}

std::string self_dir(const char *argv0)
{
    char buffer[4096];
    const ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    std::string exe = n > 0 ? std::string(buffer, static_cast<size_t>(n)) : std::string(argv0);
    return dirname_of(exe);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s render|compare|sheet ...\n", argv[0]);
        return 2;
    }
    const std::string mode = argv[1];
    if (mode == "compare") return cmd_compare(argc, argv);
    if (mode == "sheet") return cmd_sheet(argc, argv);
    if (mode == "crop") return cmd_crop(argc, argv);
    if (mode == "render") {
        /* The binary lives in tests/render/build/; resources are found from the repository. */
        const char *root = std::getenv("HARNESS_DIR");
        const std::string harness_dir = root && root[0] ? root : dirname_of(self_dir(argv[0]));
        harness::fixtures().fixture_root = harness_dir + "/fixtures";
        harness::fixtures().resource_root = harness_dir + "/../../APPLaunch";
        char resolved[4096];
        if (realpath(harness::fixtures().resource_root.c_str(), resolved)) harness::fixtures().resource_root = resolved;
        if (realpath(harness::fixtures().fixture_root.c_str(), resolved)) harness::fixtures().fixture_root = resolved;
        return cmd_render(argc, argv, harness_dir);
    }
    std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
}
