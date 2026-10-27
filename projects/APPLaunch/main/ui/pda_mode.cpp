/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Screen > PDA mode, applied at start (see pda_mode.hpp).
 */

#include "pda_mode.hpp"

#include "cp0_config_json.h"
#include "cp0_ui_metrics.h"
#include "cp0_zone.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pda_mode {
namespace {

bool set(const char *value)
{
    return value && value[0];
}

/* "800" -> 800; digits only, 1..9999 */
bool parse_px(const char *begin, const char *end, int *out)
{
    if (begin == end || end - begin > 4) return false;
    int value = 0;
    for (const char *p = begin; p < end; ++p) {
        if (*p < '0' || *p > '9') return false;
        value = value * 10 + (*p - '0');
    }
    if (value <= 0) return false;
    *out = value;
    return true;
}

/* "1280x800" */
bool parse_size(const char *text, int *w, int *h)
{
    const char *x = std::strchr(text, 'x');
    if (!x) return false;
    return parse_px(text, x, w) && parse_px(x + 1, text + std::strlen(text), h);
}

bool parse_rotation(const char *text, int *rot)
{
    for (const int r : {0, 90, 180, 270}) {
        if (std::strcmp(text, std::to_string(r).c_str()) == 0) {
            *rot = r;
            return true;
        }
    }
    return false;
}

std::string size_text(const Canvas &c)
{
    return std::to_string(c.w) + "x" + std::to_string(c.h);
}

bool s_ready = false; /* s_startup holds apply_at_start()'s or startup()'s result */
Startup s_startup;

Startup compute(int stored)
{
    Startup s;
    s.board = board_from(std::getenv("APPLAUNCH_LOGICAL"), std::getenv("APPLAUNCH_ROTATE"), std::getenv("APPLAUNCH_PDA"));
    s.offered = offered(s.board, std::getenv("APPLAUNCH_PANEL_MM"));
    s.stored = stored;
    s.mode = plan(s.board, stored, s.offered).mode;
    return s;
}

int read_stored_file(const std::string &path)
{
    FILE *fp = std::fopen(path.c_str(), "r");
    if (!fp) return -1;
    std::string text;
    char buffer[512];
    size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), fp)) > 0 && text.size() < (1u << 20))
        text.append(buffer, count);
    const bool ok = std::ferror(fp) == 0;
    std::fclose(fp);
    return ok ? stored_from_json(text) : -1;
}

} // namespace

Board board_from(const char *logical, const char *rotate, const char *pda)
{
    Board b;
    b.pda = pda && std::strcmp(pda, "1") == 0;
    Canvas c;
    if (!set(logical) || !set(rotate) || !parse_size(logical, &c.w, &c.h) || !parse_rotation(rotate, &c.rot)) return b;
    b.known = true;
    b.canvas = c;
    if (c.w == c.h) {
        b.landscape = b.left = b.right = c;
        return b;
    }
    if (c.w > c.h) {
        b.landscape = c; /* a landscape board: its own canvas */
    } else {
        /* a portrait board: the panel's landscape as the landscape install profiles have it, rotate 0 on a landscape
         * framebuffer (the canvas turned a quarter into it: rotate 90 / 270), 90 on a portrait one (rotate 0 / 180) */
        const bool fb_landscape = c.rot == 90 || c.rot == 270;
        b.landscape = Canvas{c.h, c.w, fb_landscape ? 0 : 90};
    }
    const Canvas &l = b.landscape;
    b.left = Canvas{l.h, l.w, (l.rot + 90) % 360};   /* the device turned left: the right edge at the top */
    b.right = Canvas{l.h, l.w, (l.rot + 270) % 360}; /* the device turned right: the left edge at the top */
    return b;
}

bool zone_fits(const Canvas &portrait, const char *panel_mm)
{
    if (portrait.w <= 0 || portrait.h <= portrait.w) return false;
    /* the density as the display manager resolves it before the framebuffer's own mm (unknown before it opens) */
    const cp0_ui_density_t density = cp0_ui_density_resolve(portrait.w, portrait.h, portrait.rot, panel_mm, 0, 0);
    cp0_ui_metrics_t metrics;
    if (cp0_ui_metrics_compute(portrait.w, portrait.h, portrait.rot, density.ppmm_x100, density.src, 0, &metrics) != 0)
        return false;
    cp0_zone_layout_t layout;
    return cp0_zone_layout(&metrics, &layout) == 0;
}

bool offered(const Board &board, const char *panel_mm, ZoneFits fits)
{
    if (!board.known || board.canvas.w == board.canvas.h) return false;
    if (std::min(board.canvas.w, board.canvas.h) < kMinShortSide) return false;
    return fits && fits(board.left, panel_mm); /* both sides: the same size and density */
}

int board_default(const Board &board)
{
    if (!board.known || board.canvas.h <= board.canvas.w || !board.pda) return kOff;
    return board.canvas == board.right ? kRight : kLeft;
}

const Canvas &canvas_of(const Board &board, int mode)
{
    return mode == kLeft ? board.left : mode == kRight ? board.right : board.landscape;
}

std::vector<int> choices(const Board &board, bool is_offered)
{
    if (!board.known || !is_offered) return {};
    return {kOff, kLeft, kRight};
}

const char *label(int mode)
{
    return mode == kLeft ? "On rotate left" : mode == kRight ? "On rotate right" : "Off";
}

int stored_from_json(const std::string &json)
{
    std::vector<std::pair<std::string, std::string>> entries;
    if (!cp0cfg::from_json(json, entries)) return -1;
    int value = -1;
    for (const auto &entry : entries) {
        if (entry.first != kConfigKey) continue;
        /* the last one counts */
        value = entry.second == "0" ? kOff : entry.second == "1" ? kLeft : entry.second == "2" ? kRight : -1;
    }
    return value;
}

Plan plan(const Board &board, int stored, bool is_offered)
{
    Plan p;
    p.mode = board_default(board);
    p.canvas = board.canvas;
    if (!board.known || !is_offered || (stored != kOff && stored != kLeft && stored != kRight)) return p;
    p.mode = stored;
    p.canvas = canvas_of(board, stored);
    p.env.emplace_back("APPLAUNCH_LOGICAL", size_text(p.canvas));
    p.env.emplace_back("APPLAUNCH_ROTATE", std::to_string(p.canvas.rot));
    p.env.emplace_back("APPLAUNCH_PDA", stored == kOff ? "0" : "1");
    return p;
}

std::string config_path()
{
    const char *home = std::getenv("HOME");
    const std::string base = home && home[0] ? home : "/root";
    return base + "/.config/cardputerzero/config.json";
}

const Startup &apply_at_start()
{
    if (s_ready) return s_startup;
    s_ready = true;
    Startup s = compute(-1);
    if (s.offered) s.stored = read_stored_file(config_path());
    const Plan p = plan(s.board, s.stored, s.offered);
    s.mode = p.mode;
    for (const auto &kv : p.env) ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
    s.applied = !p.env.empty();
    if (!s.board.known) {
        std::fprintf(stderr, "[pda-mode] no board profile geometry (APPLAUNCH_LOGICAL and APPLAUNCH_ROTATE): not "
                             "offered\n");
    } else if (!s.offered) {
        std::fprintf(stderr, "[pda-mode] board %s rotate %d: not offered (square, short side under %d px, or no "
                             "room for the writing zone in portrait)\n",
                     size_text(s.board.canvas).c_str(), s.board.canvas.rot, kMinShortSide);
    } else {
        std::fprintf(stderr, "[pda-mode] board %s rotate %d (APPLAUNCH_PDA %d), %s %s: %s rotate %d%s\n",
                     size_text(s.board.canvas).c_str(), s.board.canvas.rot, s.board.pda ? 1 : 0,
                     s.stored < 0 ? "nothing stored, the board's" : "stored", label(s.mode),
                     size_text(p.canvas).c_str(), p.canvas.rot, s.mode != kOff ? " with the writing zone" : "");
    }
    s_startup = s;
    return s_startup;
}

const Startup &startup()
{
    if (!s_ready) {
        s_ready = true;
        s_startup = compute(-1);
    }
    return s_startup;
}

} // namespace pda_mode
