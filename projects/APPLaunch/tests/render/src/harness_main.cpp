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

#include "commount.h"
#include "cp0/cp0_fb_output.h"
#include "cp0/cp0_lvgl.h"
#include "cp0_display.h"
#include "cp0_ui_metrics.h"
#include "input_keys.h"
#include "keyboard_input.h"

#include "launch.h"
#include "launcher_toast.h"
#include "native_screensaver.hpp"
#include "native_ui.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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

/* Advance the fake clock in 5 ms steps, running LVGL timers (refresh, indev reads, animations).
 * Worker threads some pages start get a little real time between steps to report back. */
void settle(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        g_tick += 5;
        lv_timer_handler();
        if (t % 50 == 0) usleep(300);
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
        {"DOT", KEY_DOT},     {"SLASH", KEY_SLASH},
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
        } else if (cmd == "env" && w.size() == 3) run.profile.env[w[1]] = w[2];
        else return fail(scene, line, "usage: profile deck|pi3a|generic, ppmm N, env KEY VALUE");
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
        return true;
    }
    if (cmd == "wifi") {
        if (w.size() != 2) return fail(scene, line, "usage: wifi off|PERCENT");
        fx.wifi_connected = w[1] != "off";
        fx.wifi_signal = fx.wifi_connected ? to_int(w[1], 70) : 0;
        return true;
    }
    if (cmd == "bt") {
        if (w.size() != 2) return fail(scene, line, "usage: bt off|on|connected");
        fx.bt_powered = w[1] != "off";
        fx.bt_connected = w[1] == "connected";
        return true;
    }
    if (cmd == "config") {
        if (w.size() != 3) return fail(scene, line, "usage: config KEY VALUE");
        harness::config_set(w[1], w[2]);
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
            if (icon.compare(0, 8, "fixture:") == 0)
                spec.icon = "A:" + fx.fixture_root + "/" + icon.substr(8);
            else
                spec.icon = harness::resolve_resource(icon); /* launcher_platform::path(desc.icon) */
            spec.kind = spec.name == "Settings" ? "settings" : spec.name == "Calculator" ? "calculator" : "tile";
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
    } else if (cmd == "key") {
        if (w.size() < 2 || w.size() > 3) return fail(scene, line, "usage: key NAME [press|release|tap]");
        const uint32_t code = key_code(w[1]);
        if (!code) return fail(scene, line, "unknown key " + w[1]);
        const std::string mode = w.size() == 3 ? w[2] : "tap";
        if (mode == "press" || mode == "tap") {
            cp0_keyboard_inject(code, KBD_KEY_PRESSED, 0);
            settle(60);
        }
        if (mode == "release" || mode == "tap") {
            cp0_keyboard_inject(code, KBD_KEY_RELEASED, 0);
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
    } else if (cmd == "ribbon") {
        /* external_poll() in native_ui.cpp when the runner raises the hold-Esc hint over a stock app */
        cp0_display_set_overlay_rect(20, 4, 280, 22);
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
    } else if (cmd == "toast") {
        std::string text;
        for (std::size_t i = 1; i < w.size(); ++i) text += (i > 1 ? " " : "") + w[i];
        for (std::size_t at; (at = text.find("\\n")) != std::string::npos;) text.replace(at, 2, "\n");
        launcher_toast().show_persistent(text.c_str());
        settle(200);
    } else if (cmd == "screensaver") {
        native_screensaver::init(); /* ui_screensaver_init() does this on the Pi */
        settle(50);
    } else if (cmd == "metrics") {
        /* what the layout service computed for this size, plus what the launcher exported to its children */
        if (w.size() != 2) return fail(scene, line, "usage: metrics NAME");
        const cp0_ui_metrics_t *m = cp0_ui_metrics_current();
        char text[2048];
        if (!m || cp0_ui_metrics_describe(m, text, sizeof(text)) < 0)
            return fail(scene, line, "the display manager published no metrics");
        std::string report = text;
        report += "environment";
        for (const char *name : {"APPLAUNCH_SCREEN_W", "APPLAUNCH_SCREEN_H", "APPLAUNCH_SCREEN_ROTATE",
                                 "APPLAUNCH_SCREEN_PPMM", "APPLAUNCH_SCREEN_CLASS"}) {
            const char *value = std::getenv(name);
            report += std::string(" ") + name + "=" + (value ? value : "(unset)");
        }
        report += "\n";
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
         * (header home ZERO). */
        if (w.size() < 3) return fail(scene, line, "usage: header NAME TITLE [TITLE ...]");
        const cp0_ui_metrics_t *m = cp0_ui_metrics_current();
        if (!m) return fail(scene, line, "the display manager published no metrics");
        settle(500);
        lv_refr_now(nullptr);
        const int strip_x = std::clamp(native_ui::status_strip_left(), 0, run.size.w - 1);
        const int strip_h = m->shell.bar_h;
        const std::string dir = run.out_dir + "/" + run.size.name() + "/header/";
        if (!save_png(dir + w[1] + ".png", crop_image(grab(run), strip_x, 0, run.size.w - strip_x, strip_h)))
            return fail(scene, line, "cannot write " + dir + w[1] + ".png");
        const std::vector<std::string> titles(w.begin() + 2, w.end());
        lv_obj_t *title = find_label(lv_display_get_screen_active(cp0_display_native()), titles);
        if (!title) return fail(scene, line, "no title label with the text " + w[2] + " on the native screen");
        lv_area_t area;
        lv_obj_get_coords(title, &area);
        std::ofstream out(dir + w[1] + ".txt");
        out << area.x1 << " " << area.y1 << " " << lv_label_get_text(title) << "\n";
        if (!out) return fail(scene, line, "cannot write " + dir + w[1] + ".txt");
        std::printf("%s%s.png\n", dir.c_str(), w[1].c_str());
    } else if (cmd == "wait") {
        if (w.size() != 2) return fail(scene, line, "usage: wait MS");
        settle(static_cast<uint32_t>(std::max(0, to_int(w[1]))));
    } else if (cmd == "shot") {
        if (w.size() != 2) return fail(scene, line, "usage: shot NAME");
        settle(500);
        lv_refr_now(nullptr);
        const std::string path = run.out_dir + "/" + run.size.name() + "/" + w[1] + ".png";
        if (!save_png(path, grab(run))) return fail(scene, line, "cannot write " + path);
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
