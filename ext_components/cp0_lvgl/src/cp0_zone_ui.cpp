/*
 * SPDX-License-Identifier: MIT
 *
 * The writing zone's LVGL side (see cp0_zone_ui.h): the band's widgets, the ink, the recogniser and its timer.
 */

#include "cp0_zone_ui.h"

#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_widgets.hpp"
#include "input_keys.h"
#include "keyboard_input.h"
#include "strokes.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr int kInkMax = 512;          /* ink points kept for one stroke (beyond: every second one is dropped) */
constexpr uint32_t kTimerMs = 16;     /* the recogniser's poll and the ink's fade, while either has work */
constexpr uint32_t kFadeMs = 260;     /* the ink fades out after the result ... */
constexpr uint32_t kRejectHoldMs = 200; /* ... a refused stroke first stays red this long */
constexpr int kInkStepPx = 1;         /* a point closer than this to the last one is not drawn */

enum class Ink { None, Drawing, Waiting, Fading };

struct Ui {
    bool active = false;
    lv_display_t *display = nullptr;
    cp0_zone_layout_t l{};
    int64_t (*clock)(void) = nullptr;
    lv_obj_t *screen = nullptr;
    lv_obj_t *write_bg = nullptr;
    lv_obj_t *box[2] = {nullptr, nullptr};
    lv_obj_t *box_label[2] = {nullptr, nullptr};
    lv_obj_t *hint[2] = {nullptr, nullptr};
    lv_obj_t *button[CP0_ZONE_BTN__COUNT] = {};
    lv_obj_t *button_label[CP0_ZONE_BTN__COUNT] = {};
    lv_obj_t *key[CP0_ZONE_KEY__COUNT] = {};
    lv_obj_t *key_label[CP0_ZONE_KEY__COUNT] = {};
    lv_obj_t *ink = nullptr;
    lv_obj_t *blank = nullptr;
    lv_point_precise_t pts[kInkMax];
    int n_pts = 0;
    Ink ink_state = Ink::None;
    uint32_t fade_from = 0; /* lv_tick when the fade starts */
    lv_timer_t *timer = nullptr;
    strokes_table *table = nullptr;
    strokes_engine *engine = nullptr;
    strokes_config cfg{};
    cp0_zone_strip_t strip{};
    bool strip_open = false;
    int hint_box = STROKES_BOX_LETTERS;
    int accent = STROKES_ACCENT_NONE;
    cp0_zone_action_cb_t action_cb = nullptr;
    cp0_zone_strip_cb_t strip_cb = nullptr;
    char describe[320] = "";
    /* APPLAUNCH_STROKES_LOG: the stroke being written, in the corpus format */
    FILE *log = nullptr;
    std::string log_points;
    int64_t log_t0 = 0;
    int log_box = STROKES_BOX_LETTERS;
};

/* never destroyed: no LVGL call may run from a static destructor after LVGL is gone */
Ui &ui()
{
    static Ui *u = new Ui;
    return *u;
}

const char *const kButtonText[CP0_ZONE_BTN__COUNT] = {"Home", "Menu", "Apps", "Keys"};
const char *const kKeyText[CP0_ZONE_KEY__COUNT] = {"Tab",         "Ctrl",          "Shift",          "Alt", LV_SYMBOL_LEFT,
                                                   LV_SYMBOL_UP, LV_SYMBOL_DOWN, LV_SYMBOL_RIGHT, "Enter"};

bool is_arrow(int key)
{
    return key == CP0_ZONE_KEY_LEFT || key == CP0_ZONE_KEY_UP || key == CP0_ZONE_KEY_DOWN || key == CP0_ZONE_KEY_RIGHT;
}

lv_obj_t *plain(lv_obj_t *parent, const cp0_zone_rect_t &r, int y_off, uint32_t rgb)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, r.x, r.y - y_off);
    lv_obj_set_size(o, r.w, r.h);
    lv_obj_set_style_bg_color(o, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    if (lv_obj_get_group(o)) lv_group_remove_obj(o);
    return o;
}

lv_obj_t *text(lv_obj_t *parent, const char *t, int px, uint32_t rgb)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, t);
    lv_obj_set_style_text_font(label, cp0_ui_font_px(px), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

lv_obj_t *key_obj(lv_obj_t *parent, const cp0_zone_rect_t &r, int y_off, const char *t, int px, lv_obj_t **label_out)
{
    lv_obj_t *o = plain(parent, r, y_off, CP0_ZONE_KEY_RGB);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(o, ui().l.radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(CP0_ZONE_KEY_PRESSED_RGB), LV_STATE_PRESSED);
    lv_obj_t *label = text(o, t, px, CP0_ZONE_LABEL_RGB);
    lv_obj_center(label);
    if (label_out) *label_out = label;
    return o;
}

void inject(const cp0_ui_osk_event_t *e, int n)
{
    for (int i = 0; i < n; i++) (void)cp0_keyboard_inject_key(e[i].code, e[i].state, e[i].mods, e[i].utf8);
}

uint64_t now_ms()
{
    return static_cast<uint64_t>(lv_tick_get());
}

/* ------------------------------------------------------------------ drawing state */

void refresh_strip()
{
    Ui &u = ui();
    for (int k = 0; k < CP0_ZONE_KEY__COUNT; k++) {
        if (!u.key[k]) continue;
        const int state = cp0_zone_strip_mod_state(&u.strip, static_cast<cp0_zone_key_t>(k));
        const uint32_t bg = state == CP0_UI_OSK_MOD_ARMED    ? CP0_ZONE_LIT_RGB
                            : state == CP0_UI_OSK_MOD_LOCKED ? CP0_ZONE_LOCKED_RGB
                                                             : CP0_ZONE_KEY_RGB;
        const uint32_t fg = state == CP0_UI_OSK_MOD_LOCKED ? CP0_UI_OSK_ARMED_LABEL_RGB : CP0_ZONE_LABEL_RGB;
        lv_obj_set_style_bg_color(u.key[k], lv_color_hex(bg), 0);
        lv_obj_set_style_text_color(u.key_label[k], lv_color_hex(fg), 0);
    }
    if (u.button[CP0_ZONE_BTN_KEYS])
        lv_obj_set_style_bg_color(u.button[CP0_ZONE_BTN_KEYS],
                                  lv_color_hex(u.strip_open ? CP0_ZONE_LIT_RGB : CP0_ZONE_KEY_RGB), 0);
}

void refresh_mode()
{
    Ui &u = ui();
    if (!u.engine) return;
    const unsigned mode = strokes_get_mode(u.engine);
    if (!(mode & STROKES_MODE_ACCENT)) u.accent = STROKES_ACCENT_NONE;
    char letters[8], hint[64];
    cp0_zone_mode_text(mode, u.accent, letters, sizeof(letters), hint, sizeof(hint));
    lv_label_set_text(u.box_label[0], letters);
    for (int b = 0; b < 2; b++) {
        const bool here = hint[0] && b == u.hint_box;
        if (here) lv_label_set_text(u.hint[b], hint);
        if (here) lv_obj_remove_flag(u.hint[b], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(u.hint[b], LV_OBJ_FLAG_HIDDEN);
    }
}

void wake_timer()
{
    Ui &u = ui();
    if (u.timer) lv_timer_resume(u.timer);
}

/* ------------------------------------------------------------------ the ink */

void ink_show(uint32_t rgb)
{
    Ui &u = ui();
    lv_obj_set_style_line_color(u.ink, lv_color_hex(rgb), 0);
    lv_obj_set_style_line_opa(u.ink, LV_OPA_COVER, 0);
    lv_obj_remove_flag(u.ink, LV_OBJ_FLAG_HIDDEN);
}

void ink_hide()
{
    Ui &u = ui();
    u.n_pts = 0;
    u.ink_state = Ink::None;
    lv_line_set_points(u.ink, u.pts, 0);
    lv_obj_add_flag(u.ink, LV_OBJ_FLAG_HIDDEN);
}

void ink_add(int x, int y)
{
    Ui &u = ui();
    const float fx = static_cast<float>(x), fy = static_cast<float>(y - u.l.strip_y);
    if (u.n_pts > 0) {
        const float dx = fx - static_cast<float>(u.pts[u.n_pts - 1].x), dy = fy - static_cast<float>(u.pts[u.n_pts - 1].y);
        if (std::fabs(dx) < kInkStepPx && std::fabs(dy) < kInkStepPx) return;
    }
    if (u.n_pts >= kInkMax) { /* keep every second point: the shape stays */
        int n = 0;
        for (int i = 0; i < u.n_pts; i += 2) u.pts[n++] = u.pts[i];
        u.n_pts = n;
    }
    u.pts[u.n_pts].x = static_cast<lv_value_precise_t>(fx);
    u.pts[u.n_pts].y = static_cast<lv_value_precise_t>(fy);
    u.n_pts++;
    lv_line_set_points(u.ink, u.pts, static_cast<uint32_t>(u.n_pts));
}

void ink_fade(bool reject)
{
    Ui &u = ui();
    if (u.ink_state == Ink::None) return;
    if (reject) ink_show(CP0_ZONE_REJECT_RGB);
    u.fade_from = lv_tick_get() + (reject ? kRejectHoldMs : 0);
    u.ink_state = Ink::Fading;
    wake_timer();
}

/* the fade's step; true while it goes on */
bool ink_step()
{
    Ui &u = ui();
    if (u.ink_state != Ink::Fading) return false;
    const uint32_t now = lv_tick_get();
    if (static_cast<int32_t>(now - u.fade_from) < 0) return true; /* a refused stroke's red hold */
    const uint32_t t = now - u.fade_from;
    if (t >= kFadeMs) {
        ink_hide();
        return false;
    }
    const uint32_t opa = 255u - 255u * t / kFadeMs;
    lv_obj_set_style_line_opa(u.ink, static_cast<lv_opa_t>(opa), 0);
    return true;
}

/* ------------------------------------------------------------------ the corpus log (development) */

void log_point(char kind, int64_t t, int x, int y)
{
    Ui &u = ui();
    if (!u.log) return;
    char buf[64];
    const double ms = static_cast<double>(t - u.log_t0) / 1000.0;
    if (kind == '^') std::snprintf(buf, sizeof(buf), " ^%.3f", ms);
    else std::snprintf(buf, sizeof(buf), "%s%.3f,%d,%d", u.log_points.empty() ? "" : " ", ms, x, y);
    u.log_points += buf;
}

void log_result(const strokes_result &r)
{
    Ui &u = ui();
    if (!u.log || u.log_points.empty()) return;
    std::fprintf(u.log, "# zone %s glyph=%s utf8=%s score=%.3f margin=%.3f reject=%s mode=%u\n",
                 strokes_kind_name(r.kind), r.glyph, r.kind == STROKES_CHAR ? r.utf8 : "-", r.confidence, r.margin,
                 strokes_reject_name(r.reject), r.mode);
    std::fprintf(u.log, "?\t%s\t%.3f\t%s\n", u.log_box == STROKES_BOX_DIGITS ? "digits" : "letters", u.cfg.ppmm,
                 u.log_points.c_str());
    std::fflush(u.log);
    u.log_points.clear();
}

/* ------------------------------------------------------------------ the recogniser */

void handle(const strokes_result &r)
{
    Ui &u = ui();
    log_result(r);
    if (r.kind == STROKES_CHAR || r.kind == STROKES_KEY) {
        cp0_ui_osk_event_t e[CP0_ZONE_MAX_EVENTS];
        const int n = cp0_zone_result_events(&u.strip, &r, cp0::ui::osk::multiline() ? 1 : 0, e, CP0_ZONE_MAX_EVENTS);
        inject(e, n);
    }
    u.hint_box = r.box == STROKES_BOX_DIGITS ? STROKES_BOX_DIGITS : STROKES_BOX_LETTERS;
    if (r.mode & STROKES_MODE_ACCENT) u.accent = r.accent;
    ink_fade(r.kind == STROKES_REJECT);
    refresh_mode();
    refresh_strip();
}

void drain(int64_t now_us)
{
    Ui &u = ui();
    if (!u.engine) return;
    strokes_result r;
    while (strokes_poll(u.engine, static_cast<uint64_t>(now_us < 0 ? 0 : now_us), &r)) handle(r);
}

void timer_cb(lv_timer_t *timer)
{
    Ui &u = ui();
    drain(u.clock ? u.clock() : static_cast<int64_t>(lv_tick_get()) * 1000);
    const bool fading = ink_step();
    if (!fading && !(u.engine && strokes_busy(u.engine))) lv_timer_pause(timer);
}

/* ------------------------------------------------------------------ the buttons and the strip */

void set_strip(bool open)
{
    Ui &u = ui();
    if (open == u.strip_open || !u.active) return;
    u.strip_open = open;
    if (!open) {
        cp0_ui_osk_event_t e[CP0_ZONE_MAX_EVENTS];
        inject(e, cp0_zone_strip_release_all(&u.strip, e, CP0_ZONE_MAX_EVENTS));
    }
    for (int k = 0; k < CP0_ZONE_KEY__COUNT; k++) {
        if (open) lv_obj_remove_flag(u.key[k], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(u.key[k], LV_OBJ_FLAG_HIDDEN);
    }
    refresh_strip();
    lv_obj_invalidate(u.screen);
    /* pages, the terminal and dialogs leave the strip's row free, as for the on-screen keyboard */
    cp0::ui::osk::set_reserved_inset(open ? u.l.row_h : 0);
    if (u.strip_cb) u.strip_cb(open ? 1 : 0);
    std::fprintf(stderr, "[zone] key strip %s\n", open ? "open" : "closed");
}

void button_cb(lv_event_t *event)
{
    Ui &u = ui();
    const auto which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    switch (which) {
    case CP0_ZONE_BTN_HOME:
        if (u.action_cb) u.action_cb(CP0_ZONE_ACTION_HOME);
        break;
    case CP0_ZONE_BTN_APPS:
        if (u.action_cb) u.action_cb(CP0_ZONE_ACTION_APPS);
        break;
    case CP0_ZONE_BTN_MENU: {
        /* Ctrl+O: the options key of the apps (APP-CONTRACT.md section 10b), as the pointer's held click sends it */
        (void)cp0_keyboard_inject_key(KEY_O, KBD_KEY_PRESSED, KBD_MOD_CTRL, nullptr);
        (void)cp0_keyboard_inject_key(KEY_O, KBD_KEY_RELEASED, KBD_MOD_CTRL, nullptr);
        break;
    }
    case CP0_ZONE_BTN_KEYS:
        set_strip(!u.strip_open);
        break;
    default:
        break;
    }
}

void key_cb(lv_event_t *event)
{
    Ui &u = ui();
    const auto key = static_cast<cp0_zone_key_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    const lv_event_code_t code = lv_event_get_code(event);
    cp0_ui_osk_event_t e[CP0_ZONE_MAX_EVENTS];
    int n = 0;
    if (is_arrow(key)) {
        if (code == LV_EVENT_PRESSED) n = cp0_zone_strip_down(&u.strip, key, e, CP0_ZONE_MAX_EVENTS);
        else if (code == LV_EVENT_LONG_PRESSED_REPEAT) n = cp0_zone_strip_repeat(&u.strip, key, e, CP0_ZONE_MAX_EVENTS);
        else if (code == LV_EVENT_RELEASED)
            n = cp0_zone_strip_up(&u.strip, key, 0, now_ms(), e, CP0_ZONE_MAX_EVENTS);
        else if (code == LV_EVENT_PRESS_LOST) n = cp0_zone_strip_cancel(&u.strip, e, CP0_ZONE_MAX_EVENTS);
        else return;
    } else {
        if (code != LV_EVENT_CLICKED) return;
        n = cp0_zone_strip_up(&u.strip, key, cp0::ui::osk::multiline() ? 1 : 0, now_ms(), e, CP0_ZONE_MAX_EVENTS);
    }
    inject(e, n);
    refresh_strip();
}

/* ------------------------------------------------------------------ the tables */

std::string env_or(const char *name, const char *fallback)
{
    const char *v = std::getenv(name);
    return v ? std::string(v) : std::string(fallback);
}

bool readable(const std::string &path)
{
    FILE *f = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

bool add_table(strokes_table *t, const std::string &path, const char *what)
{
    strokes_error err;
    std::memset(&err, 0, sizeof(err));
    if (strokes_table_add_file(t, path.c_str(), &err) == 0) return true;
    std::fprintf(stderr, "[zone] %s ignored: %s:%d: %s\n", what, err.source, err.line, err.msg);
    return false;
}

std::string user_overlay()
{
    if (const char *v = std::getenv("APPLAUNCH_STROKES_USER")) return v; /* "" = none */
    const char *config = std::getenv("XDG_CONFIG_HOME");
    if (config && config[0]) return std::string(config) + "/applaunch/strokes-user.txt";
    const char *home = std::getenv("HOME");
    return home && home[0] ? std::string(home) + "/.config/applaunch/strokes-user.txt" : std::string();
}

void load_tables(Ui &u)
{
    u.table = strokes_table_new();
    if (!u.table) return;
    const std::string dir = env_or("APPLAUNCH_STROKES_DIR", "/usr/share/APPLaunch/strokes");
    const bool base = add_table(u.table, dir + "/glyphs.txt", "the glyph table");
    const char *extra_env = std::getenv("APPLAUNCH_STROKES_EXTRA");
    const bool want_extra = !(extra_env && std::strcmp(extra_env, "0") == 0);
    const bool extra = want_extra && add_table(u.table, dir + "/glyphs-extra.txt", "the optional glyph section");
    const std::string overlay = user_overlay();
    const bool user = readable(overlay) && add_table(u.table, overlay, "the user overlay");
    std::snprintf(u.describe, sizeof(u.describe),
                  "band %dx%d at y %d (strip row %d, buttons %d, writing %d px), app area %dx%d, boxes %d+%d px, "
                  "recogniser %s: %d glyphs (%s%s%s)",
                  u.l.w, u.l.band_closed, u.l.app_h, u.l.row_h, u.l.row_h, u.l.write_h, u.l.w, u.l.app_h, u.l.letters.w,
                  u.l.digits.w, STROKES_VERSION, strokes_table_glyph_count(u.table),
                  base ? "glyphs.txt" : "NO glyphs.txt", extra ? ", extra" : "", user ? ", user overlay" : "");
}

void build(Ui &u)
{
    const cp0_zone_layout_t &l = u.l;
    const int y0 = l.strip_y; /* the display's row 0 */
    u.screen = lv_display_get_screen_active(u.display);
    lv_obj_remove_style_all(u.screen);
    lv_obj_set_style_bg_color(u.screen, lv_color_hex(CP0_ZONE_BAND_RGB), 0);
    lv_obj_set_style_bg_opa(u.screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(u.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(u.screen, LV_OBJ_FLAG_CLICKABLE);

    u.write_bg = plain(u.screen, cp0_zone_rect_t{0, l.write_y, l.w, l.write_h}, y0, CP0_ZONE_WRITE_RGB);
    const cp0_zone_rect_t boxes[2] = {l.letters, l.digits};
    const char *const labels[2] = {"abc", "123"};
    for (int b = 0; b < 2; b++) {
        u.box[b] = plain(u.screen, boxes[b], y0, CP0_ZONE_BOX_RGB);
        lv_obj_set_style_radius(u.box[b], l.radius, 0);
        lv_obj_set_style_border_width(u.box[b], 1, 0);
        lv_obj_set_style_border_color(u.box[b], lv_color_hex(CP0_ZONE_BOX_BORDER_RGB), 0);
        lv_obj_set_style_border_opa(u.box[b], LV_OPA_COVER, 0);
        u.box_label[b] = text(u.box[b], labels[b], l.box_label_px, CP0_ZONE_BOX_LABEL_RGB);
        lv_obj_align(u.box_label[b], LV_ALIGN_TOP_LEFT, l.pad, l.pad / 2);
        u.hint[b] = text(u.box[b], "", l.box_label_px, CP0_ZONE_INK_RGB);
        lv_obj_align(u.hint[b], LV_ALIGN_TOP_RIGHT, -l.pad, l.pad / 2);
        lv_obj_add_flag(u.hint[b], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < CP0_ZONE_BTN__COUNT; i++) {
        u.button[i] = key_obj(u.screen, l.button[i], y0, kButtonText[i], l.button_px, &u.button_label[i]);
        lv_obj_add_event_cb(u.button[i], button_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }
    for (int k = 0; k < CP0_ZONE_KEY__COUNT; k++) {
        u.key[k] = key_obj(u.screen, l.strip_key[k], y0, kKeyText[k], l.key_px, &u.key_label[k]);
        lv_obj_add_event_cb(u.key[k], key_cb, LV_EVENT_ALL, reinterpret_cast<void *>(static_cast<intptr_t>(k)));
        lv_obj_add_flag(u.key[k], LV_OBJ_FLAG_HIDDEN);
    }

    u.ink = lv_line_create(u.screen);
    lv_obj_remove_style_all(u.ink);
    lv_obj_set_pos(u.ink, 0, 0);
    lv_obj_set_size(u.ink, l.w, l.band_open);
    lv_obj_set_style_line_width(u.ink, l.ink_px, 0);
    lv_obj_set_style_line_rounded(u.ink, true, 0);
    lv_obj_set_style_line_color(u.ink, lv_color_hex(CP0_ZONE_INK_RGB), 0);
    lv_obj_remove_flag(u.ink, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(u.ink, LV_OBJ_FLAG_HIDDEN);

    u.blank = plain(u.screen, cp0_zone_rect_t{0, y0, l.w, l.band_open}, y0, 0x000000);
    lv_obj_add_flag(u.blank, LV_OBJ_FLAG_HIDDEN);
    refresh_strip();
}

} // namespace

extern "C" {

int cp0_zone_ui_create(lv_display_t *display, const cp0_zone_layout_t *layout, int64_t (*clock_us)(void))
{
    Ui &u = ui();
    if (u.active || !display || !layout) return -1;
    u.display = display;
    u.l = *layout;
    u.clock = clock_us;
    cp0_zone_strip_init(&u.strip);
    build(u);
    load_tables(u);
    strokes_config_default(&u.cfg);
    u.cfg.ppmm = static_cast<float>(u.l.ppmm_x100) / 100.0f;
    u.engine = u.table ? strokes_engine_new(u.table, &u.cfg) : nullptr;
    if (u.engine) strokes_set_mode(u.engine, 0);
    if (const char *path = std::getenv("APPLAUNCH_STROKES_LOG")) {
        if (path[0]) {
#if defined(__linux__)
            u.log = std::fopen(path, "ae"); /* close-on-exec: the apps the launcher starts do not inherit it */
#else
            u.log = std::fopen(path, "a");
#endif
            if (u.log) {
                std::fprintf(u.log, "# strokes-corpus 1\n# written by the launcher's writing zone (APPLAUNCH_STROKES_LOG); "
                                    "format: tools/strokes/README.md \"Corpus format\"\n");
                std::fflush(u.log);
            } else {
                std::fprintf(stderr, "[zone] APPLAUNCH_STROKES_LOG=%s: cannot append\n", path);
            }
        }
    }
    u.timer = lv_timer_create(timer_cb, kTimerMs, nullptr);
    if (u.timer) lv_timer_pause(u.timer);
    u.active = true;
    refresh_mode();
    return 0;
}

int cp0_zone_ui_active(void)
{
    return ui().active ? 1 : 0;
}

const cp0_zone_layout_t *cp0_zone_ui_layout(void)
{
    return ui().active ? &ui().l : nullptr;
}

void cp0_zone_ui_heal(int *ms, int *px)
{
    const Ui &u = ui();
    strokes_config cfg = u.cfg;
    if (!u.active) strokes_config_default(&cfg);
    const float ppmm = u.active ? cfg.ppmm : 8.54f;
    if (ms) *ms = static_cast<int>(std::lround(cfg.heal_ms));
    /* the recogniser heals within heal_mm, or the distance the pen covers at its speed (at most 15 mm more) */
    if (px) *px = static_cast<int>(std::lround((cfg.heal_mm + 15.0f) * ppmm));
}

void cp0_zone_ui_pen(const cp0_zone_pen_t *pen)
{
    Ui &u = ui();
    if (!u.active || !pen || !u.engine) return;
    switch (pen->kind) {
    case CP0_ZONE_PEN_DOWN: {
        drain(pen->t_us); /* a lifted stroke past its heal window is finished first */
        const bool continues = strokes_busy(u.engine) != 0; /* a drop-out: the same stroke goes on */
        strokes_down(u.engine, pen->box, static_cast<float>(pen->x), static_cast<float>(pen->y),
                     static_cast<uint64_t>(pen->t_us < 0 ? 0 : pen->t_us));
        if (!continues) {
            u.n_pts = 0;
            ink_show(CP0_ZONE_INK_RGB);
            u.log_points.clear();
            u.log_t0 = pen->t_us;
            u.log_box = pen->box;
        }
        u.ink_state = Ink::Drawing;
        ink_add(pen->x, pen->y);
        log_point('p', pen->t_us, pen->x, pen->y);
        refresh_mode(); /* a press in the other box leaves the punctuation / extended mode at once */
        break;
    }
    case CP0_ZONE_PEN_MOVE:
        strokes_move(u.engine, static_cast<float>(pen->x), static_cast<float>(pen->y),
                     static_cast<uint64_t>(pen->t_us < 0 ? 0 : pen->t_us));
        ink_add(pen->x, pen->y);
        log_point('p', pen->t_us, pen->x, pen->y);
        break;
    case CP0_ZONE_PEN_UP:
        strokes_up(u.engine, static_cast<uint64_t>(pen->t_us < 0 ? 0 : pen->t_us));
        log_point('^', pen->t_us, 0, 0);
        if (u.ink_state == Ink::Drawing) u.ink_state = Ink::Waiting;
        break;
    case CP0_ZONE_PEN_CANCEL:
        strokes_cancel(u.engine);
        u.log_points.clear();
        ink_fade(false);
        break;
    default:
        return;
    }
    wake_timer();
}

int cp0_zone_ui_strip_open(void)
{
    return ui().strip_open ? 1 : 0;
}

void cp0_zone_ui_set_strip_open(int open)
{
    set_strip(open != 0);
}

void cp0_zone_ui_set_strip_cb(cp0_zone_strip_cb_t cb)
{
    ui().strip_cb = cb;
}

void cp0_zone_ui_set_action_cb(cp0_zone_action_cb_t cb)
{
    ui().action_cb = cb;
}

void cp0_zone_ui_set_blank(int on)
{
    Ui &u = ui();
    if (!u.active) return;
    if (on) {
        lv_obj_remove_flag(u.blank, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(u.blank);
    } else {
        lv_obj_add_flag(u.blank, LV_OBJ_FLAG_HIDDEN);
    }
}

void cp0_zone_ui_reset_input(void)
{
    Ui &u = ui();
    if (!u.active) return;
    if (u.engine) {
        strokes_cancel(u.engine);
        strokes_result r;
        while (strokes_flush(u.engine, &r)) {
        } /* nothing queued is typed once the panel changed hands */
        strokes_set_mode(u.engine, 0);
    }
    u.accent = STROKES_ACCENT_NONE;
    u.log_points.clear();
    cp0_ui_osk_event_t e[CP0_ZONE_MAX_EVENTS];
    inject(e, cp0_zone_strip_release_all(&u.strip, e, CP0_ZONE_MAX_EVENTS));
    ink_hide();
    refresh_mode();
    refresh_strip();
}

const char *cp0_zone_ui_describe(void)
{
    return ui().describe;
}

} // extern "C"
