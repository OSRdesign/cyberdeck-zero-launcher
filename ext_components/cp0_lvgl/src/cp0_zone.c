/*
 * SPDX-License-Identifier: MIT
 *
 * The writing zone's pure core (see cp0_zone.h). No LVGL, no system call.
 */

#include "cp0_zone.h"

#include "cp0_keyboard_key_contract.h"
#include "input_keys.h"
#include "keyboard_input.h"

#include <stdio.h>
#include <string.h>

#ifndef KEY_LEFTALT
#define KEY_LEFTALT 56
#endif

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

/* ------------------------------------------------------------------ layout */

/* the strip keys' widths, in tenths of an arrow key (the mockup: Tab 1.3, Ctrl 1.5, Shift 1.5, Alt 1.2, the four
 * arrows 1, Enter 1.8) */
static const int k_strip_weight[CP0_ZONE_KEY__COUNT] = {13, 15, 15, 12, 10, 10, 10, 10, 18};

int cp0_zone_layout(const cp0_ui_metrics_t *m, cp0_zone_layout_t *out)
{
    if (!m || !out || m->w <= 0 || m->h <= m->w) return -1;
    cp0_zone_layout_t l;
    memset(&l, 0, sizeof(l));
    l.w = m->w;
    l.h = m->h;
    l.ppmm_x100 = m->ppmm_x100 > 0 ? m->ppmm_x100 : CP0_UI_DEFAULT_PPMM_X100;
    l.pad = imax(4, m->tok[CP0_TOK_SPACE_S]);
    l.gap = imax(2, l.pad / 2);
    l.radius = imax(4, m->tok[CP0_TOK_RADIUS_S]);
    l.key_h = cp0_ui_mm_px(l.ppmm_x100, CP0_ZONE_KEY_MM_X10, CP0_ZONE_KEY_FLOOR_PX);
    l.row_h = l.pad + l.key_h;
    l.write_h = cp0_ui_mm_px(l.ppmm_x100, CP0_ZONE_WRITE_MM_X10, 6 * l.pad);
    l.band_closed = l.row_h + l.write_h;
    l.band_open = l.band_closed + l.row_h;
    l.app_h = l.h - l.band_closed;
    if (l.app_h < CP0_ZONE_MIN_APP_H) return -1;
    l.strip_y = l.app_h - l.row_h;
    l.buttons_y = l.app_h;
    l.write_y = l.h - l.write_h;

    /* the boxes: inset by pad, a gap of pad between them, the letters box 68 % */
    const int inner_w = l.w - 2 * l.pad;
    const int letters_w = (inner_w - l.pad) * CP0_ZONE_LETTERS_PCT / 100;
    l.letters = (cp0_zone_rect_t){l.pad, l.write_y + l.pad, letters_w, l.write_h - 2 * l.pad};
    l.digits = (cp0_zone_rect_t){l.pad + letters_w + l.pad, l.write_y + l.pad, inner_w - letters_w - l.pad,
                                 l.write_h - 2 * l.pad};
    l.divider_x = l.letters.x + l.letters.w + l.pad / 2;

    /* four equal buttons, pad apart; the remainder pixels go to the last */
    const int bw = (inner_w - 3 * l.pad) / 4;
    for (int i = 0; i < CP0_ZONE_BTN__COUNT; i++) {
        const int x = l.pad + i * (bw + l.pad);
        const int w = i == CP0_ZONE_BTN__COUNT - 1 ? l.pad + inner_w - x : bw;
        l.button[i] = (cp0_zone_rect_t){x, l.buttons_y + l.pad, w, l.key_h};
    }

    /* the strip keys by weight, gap apart; the edges follow the cumulated weights so the row ends at the margin */
    int total = 0;
    for (int i = 0; i < CP0_ZONE_KEY__COUNT; i++) total += k_strip_weight[i];
    const int keys_w = inner_w - (CP0_ZONE_KEY__COUNT - 1) * l.gap;
    int cum = 0;
    for (int i = 0; i < CP0_ZONE_KEY__COUNT; i++) {
        const int x0 = l.pad + i * l.gap + (cum * keys_w + total / 2) / total;
        cum += k_strip_weight[i];
        const int x1 = l.pad + i * l.gap + (cum * keys_w + total / 2) / total;
        l.strip_key[i] = (cp0_zone_rect_t){x0, l.strip_y + l.pad, x1 - x0, l.key_h};
    }

    l.button_px = cp0_ui_font_snap(imax(12, l.key_h * 42 / 100));
    l.key_px = cp0_ui_font_snap(imax(12, l.key_h * 38 / 100));
    l.box_label_px = cp0_ui_font_snap(imax(12, l.key_h / 3));
    l.ink_px = imax(2, imin(6, cp0_ui_mm_px(l.ppmm_x100, 4, 2))); /* 0.4 mm */
    *out = l;
    return 0;
}

int cp0_zone_band_top(const cp0_zone_layout_t *l, int strip_open)
{
    if (!l) return 0;
    return strip_open ? l->strip_y : l->app_h;
}

cp0_zone_hit_t cp0_zone_hit(const cp0_zone_layout_t *l, int strip_open, int x, int y)
{
    if (!l || y < cp0_zone_band_top(l, strip_open)) return CP0_ZONE_HIT_APP;
    if (y < l->write_y) return CP0_ZONE_HIT_BAND;
    return x < l->divider_x ? CP0_ZONE_HIT_LETTERS : CP0_ZONE_HIT_DIGITS;
}

/* ------------------------------------------------------------------ touch routing */

void cp0_zone_router_init(cp0_zone_router_t *r, int heal_ms, int heal_px)
{
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->heal_us = (int64_t)imax(0, heal_ms) * 1000;
    r->heal_px = imax(0, heal_px);
}

static void pen_set(cp0_zone_pen_t *pen, cp0_zone_pen_kind_t kind, int box, int x, int y, int64_t t)
{
    if (!pen) return;
    pen->kind = kind;
    pen->box = box;
    pen->x = x;
    pen->y = y;
    pen->t_us = t;
}

static const cp0_touch_contact_t *find_contact(const cp0_touch_frame_t *f, int32_t id)
{
    for (int i = 0; i < f->n && i < CP0_TOUCH_SLOTS_MAX; i++)
        if (f->c[i].id == id) return &f->c[i];
    return NULL;
}

static int within_heal(const cp0_zone_router_t *r, const cp0_touch_contact_t *c, int64_t t)
{
    if (!r->has_up || r->heal_us <= 0 || t < r->up_t_us || t - r->up_t_us > r->heal_us) return 0;
    const long long dx = (long long)c->x - r->up_x, dy = (long long)c->y - r->up_y;
    return dx * dx + dy * dy <= (long long)r->heal_px * r->heal_px;
}

cp0_zone_owner_t cp0_zone_route(cp0_zone_router_t *r, const cp0_zone_layout_t *l, int strip_open, int swallow,
                                const cp0_touch_frame_t *f, cp0_zone_pen_t *pen)
{
    pen_set(pen, CP0_ZONE_PEN_NONE, 0, 0, 0, f ? f->t_us : 0);
    if (!r || !l || !f) return r ? r->owner : CP0_ZONE_OWNER_NONE;
    const int n = f->n + f->extra;

    if (n <= 0) { /* every contact is up */
        if (r->owner == CP0_ZONE_OWNER_PEN && r->pen_live) {
            /* the last position of the pen (a lift reports none): the router kept it at the last move */
            pen_set(pen, CP0_ZONE_PEN_UP, r->pen_box, r->up_x, r->up_y, f->t_us);
            r->has_up = 1;
            r->up_t_us = f->t_us;
            r->up_box = r->pen_box;
        }
        r->owner = CP0_ZONE_OWNER_NONE;
        r->pen_live = 0;
        return r->owner;
    }

    if (r->owner == CP0_ZONE_OWNER_NONE) { /* a new touch: who owns it is decided by its first contact */
        const cp0_touch_contact_t *c = &f->c[0];
        if (swallow || f->n <= 0) {
            r->owner = CP0_ZONE_OWNER_IGNORED;
            return r->owner;
        }
        if (n == 1 && within_heal(r, c, f->t_us)) {
            /* a drop-out of the pen (it lifted a moment ago, close by): the stroke goes on, wherever it lands */
            r->owner = CP0_ZONE_OWNER_PEN;
            r->pen_id = c->id;
            r->pen_live = 1;
            r->pen_box = r->up_box;
            r->up_x = c->x;
            r->up_y = c->y;
            pen_set(pen, CP0_ZONE_PEN_DOWN, r->pen_box, c->x, c->y, f->t_us);
            return r->owner;
        }
        r->has_up = 0;
        const cp0_zone_hit_t hit = cp0_zone_hit(l, strip_open, c->x, c->y);
        if (hit == CP0_ZONE_HIT_APP) {
            r->owner = CP0_ZONE_OWNER_APP;
        } else if (hit == CP0_ZONE_HIT_BAND) {
            r->owner = CP0_ZONE_OWNER_BAND;
        } else if (n > 1) {
            r->owner = CP0_ZONE_OWNER_IGNORED; /* several fingers at once in the writing zone: not a stroke */
        } else {
            r->owner = CP0_ZONE_OWNER_PEN;
            r->pen_id = c->id;
            r->pen_live = 1;
            r->pen_box = hit == CP0_ZONE_HIT_LETTERS ? STROKES_BOX_LETTERS : STROKES_BOX_DIGITS;
            r->up_x = c->x;
            r->up_y = c->y;
            pen_set(pen, CP0_ZONE_PEN_DOWN, r->pen_box, c->x, c->y, f->t_us);
        }
        return r->owner;
    }

    if (r->owner == CP0_ZONE_OWNER_PEN && r->pen_live) {
        const cp0_touch_contact_t *c = find_contact(f, r->pen_id);
        if (n > 1) { /* a second finger: not a stroke (a three-finger gesture never writes) */
            pen_set(pen, CP0_ZONE_PEN_CANCEL, r->pen_box, r->up_x, r->up_y, f->t_us);
            r->pen_live = 0;
            r->has_up = 0;
            r->owner = CP0_ZONE_OWNER_IGNORED;
        } else if (c) {
            r->up_x = c->x;
            r->up_y = c->y;
            pen_set(pen, CP0_ZONE_PEN_MOVE, r->pen_box, c->x, c->y, f->t_us);
        } else { /* the pen lifted and another contact landed in the same frame: the stroke ends, that one is nobody's */
            pen_set(pen, CP0_ZONE_PEN_UP, r->pen_box, r->up_x, r->up_y, f->t_us);
            r->pen_live = 0;
            r->has_up = 0;
            r->owner = CP0_ZONE_OWNER_IGNORED;
        }
    }
    return r->owner;
}

void cp0_zone_router_cancel(cp0_zone_router_t *r, cp0_zone_pen_t *pen)
{
    pen_set(pen, CP0_ZONE_PEN_NONE, 0, 0, 0, 0);
    if (!r) return;
    if (r->owner == CP0_ZONE_OWNER_PEN && r->pen_live)
        pen_set(pen, CP0_ZONE_PEN_CANCEL, r->pen_box, r->up_x, r->up_y, r->up_t_us);
    r->pen_live = 0;
    r->has_up = 0;
    if (r->owner != CP0_ZONE_OWNER_NONE) r->owner = CP0_ZONE_OWNER_IGNORED;
}

void cp0_zone_router_reset(cp0_zone_router_t *r, int contacts_down)
{
    if (!r) return;
    r->owner = contacts_down > 0 ? CP0_ZONE_OWNER_IGNORED : CP0_ZONE_OWNER_NONE;
    r->pen_live = 0;
    r->has_up = 0;
}

/* ------------------------------------------------------------------ the strip and the results */

static int emit(cp0_ui_osk_event_t *out, int max, int at, uint32_t code, int state, uint32_t mods, const char *utf8)
{
    if (!out || at >= max) return at;
    memset(&out[at], 0, sizeof(out[at]));
    out[at].code = code;
    out[at].state = state;
    out[at].mods = mods;
    if (utf8) snprintf(out[at].utf8, sizeof(out[at].utf8), "%s", utf8);
    return at + 1;
}

static int emit_pair(cp0_ui_osk_event_t *out, int max, int at, uint32_t code, uint32_t mods, const char *utf8)
{
    at = emit(out, max, at, code, KBD_KEY_PRESSED, mods, utf8);
    return emit(out, max, at, code, KBD_KEY_RELEASED, mods, utf8);
}

void cp0_zone_strip_init(cp0_zone_strip_t *s)
{
    if (s) memset(s, 0, sizeof(*s));
}

int cp0_zone_strip_mod_state(const cp0_zone_strip_t *s, cp0_zone_key_t key)
{
    if (!s) return -1;
    switch (key) {
    case CP0_ZONE_KEY_CTRL: return (int)s->ctrl.state;
    case CP0_ZONE_KEY_SHIFT: return (int)s->shift.state;
    case CP0_ZONE_KEY_ALT: return (int)s->alt.state;
    default: return -1;
    }
}

uint32_t cp0_zone_strip_mods(const cp0_zone_strip_t *s)
{
    if (!s) return 0;
    return (cp0_ui_osk_mod_on(&s->ctrl) ? KBD_MOD_CTRL : 0) | (cp0_ui_osk_mod_on(&s->shift) ? KBD_MOD_SHIFT : 0) |
           (cp0_ui_osk_mod_on(&s->alt) ? KBD_MOD_ALT : 0);
}

/* The modifiers were used by a key: the armed ones go off (Alt: its release goes out); the locked ones stay. */
static int mods_used(cp0_zone_strip_t *s, uint32_t used, cp0_ui_osk_event_t *out, int max, int at)
{
    if (used & KBD_MOD_CTRL) cp0_ui_osk_mod_used(&s->ctrl);
    if (used & KBD_MOD_SHIFT) cp0_ui_osk_mod_used(&s->shift);
    if ((used & KBD_MOD_ALT) && s->alt.state == CP0_UI_OSK_MOD_ARMED) {
        cp0_ui_osk_mod_used(&s->alt);
        at = emit(out, max, at, KEY_LEFTALT, KBD_KEY_RELEASED, 0, NULL);
    }
    return at;
}

static int release_held(cp0_zone_strip_t *s, cp0_ui_osk_event_t *out, int max, int at)
{
    if (!s->held_code) return at;
    at = emit(out, max, at, s->held_code, KBD_KEY_RELEASED, s->held_mods, s->held_utf8);
    const uint32_t used = s->held_mods;
    s->held_code = 0;
    s->held_mods = 0;
    s->held_utf8[0] = '\0';
    return mods_used(s, used, out, max, at);
}

static uint32_t arrow_code(cp0_zone_key_t key)
{
    switch (key) {
    case CP0_ZONE_KEY_LEFT: return KEY_LEFT;
    case CP0_ZONE_KEY_UP: return KEY_UP;
    case CP0_ZONE_KEY_DOWN: return KEY_DOWN;
    case CP0_ZONE_KEY_RIGHT: return KEY_RIGHT;
    default: return 0;
    }
}

int cp0_zone_strip_down(cp0_zone_strip_t *s, cp0_zone_key_t key, cp0_ui_osk_event_t *out, int max)
{
    const uint32_t code = arrow_code(key);
    if (!s || !code) return 0;
    int n = release_held(s, out, max, 0);
    const uint32_t mods = cp0_zone_strip_mods(s);
    const char *control = cp0_keyboard_control_utf8(code);
    s->held_code = code;
    s->held_mods = mods;
    /* the arrow's control text as a physical key's, unless Ctrl makes it a shortcut */
    snprintf(s->held_utf8, sizeof(s->held_utf8), "%s", !(mods & KBD_MOD_CTRL) && control ? control : "");
    return emit(out, max, n, code, KBD_KEY_PRESSED, mods, s->held_utf8);
}

int cp0_zone_strip_repeat(cp0_zone_strip_t *s, cp0_zone_key_t key, cp0_ui_osk_event_t *out, int max)
{
    const uint32_t code = arrow_code(key);
    if (!s || !code || s->held_code != code) return 0;
    return emit(out, max, 0, code, KBD_KEY_REPEATED, s->held_mods, s->held_utf8);
}

/* press + release of a key with the strip's modifiers; text: what it types without Ctrl (NULL: the code alone) */
static int key_with_mods(cp0_zone_strip_t *s, uint32_t code, const char *text, cp0_ui_osk_event_t *out, int max,
                         int at)
{
    const uint32_t mods = cp0_zone_strip_mods(s);
    at = emit_pair(out, max, at, code, mods, (mods & KBD_MOD_CTRL) ? NULL : text);
    return mods_used(s, mods, out, max, at);
}

int cp0_zone_strip_up(cp0_zone_strip_t *s, cp0_zone_key_t key, int multiline, uint64_t now_ms, cp0_ui_osk_event_t *out,
                      int max)
{
    if (!s) return 0;
    switch (key) {
    case CP0_ZONE_KEY_LEFT:
    case CP0_ZONE_KEY_UP:
    case CP0_ZONE_KEY_DOWN:
    case CP0_ZONE_KEY_RIGHT:
        return s->held_code == arrow_code(key) ? release_held(s, out, max, 0) : 0;
    case CP0_ZONE_KEY_TAB: {
        /* a tab character only in a multi-line field without other modifiers (the on-screen keyboard's rule); else the
         * code alone (the next field, Shift+Tab the previous one, Alt+Tab the switcher) */
        const uint32_t mods = cp0_zone_strip_mods(s);
        const int n = emit_pair(out, max, 0, KEY_TAB, mods, multiline && !mods ? "\t" : NULL);
        return mods_used(s, mods, out, max, n);
    }
    case CP0_ZONE_KEY_ENTER:
        return key_with_mods(s, KEY_ENTER, cp0_keyboard_control_utf8(KEY_ENTER), out, max, 0);
    case CP0_ZONE_KEY_CTRL:
        cp0_ui_osk_mod_tap(&s->ctrl, now_ms);
        return 0;
    case CP0_ZONE_KEY_SHIFT:
        cp0_ui_osk_mod_tap(&s->shift, now_ms);
        return 0;
    case CP0_ZONE_KEY_ALT: {
        /* a real key: Alt on = its press, Alt off = its release (armed -> locked sends nothing) */
        const int was_on = cp0_ui_osk_mod_on(&s->alt);
        cp0_ui_osk_mod_tap(&s->alt, now_ms);
        const int on = cp0_ui_osk_mod_on(&s->alt);
        if (on && !was_on) return emit(out, max, 0, KEY_LEFTALT, KBD_KEY_PRESSED, KBD_MOD_ALT, NULL);
        if (!on && was_on) return emit(out, max, 0, KEY_LEFTALT, KBD_KEY_RELEASED, 0, NULL);
        return 0;
    }
    default:
        return 0;
    }
}

int cp0_zone_strip_cancel(cp0_zone_strip_t *s, cp0_ui_osk_event_t *out, int max)
{
    return s ? release_held(s, out, max, 0) : 0;
}

int cp0_zone_strip_release_all(cp0_zone_strip_t *s, cp0_ui_osk_event_t *out, int max)
{
    if (!s) return 0;
    int n = release_held(s, out, max, 0);
    if (cp0_ui_osk_mod_on(&s->alt)) n = emit(out, max, n, KEY_LEFTALT, KBD_KEY_RELEASED, 0, NULL);
    cp0_ui_osk_mod_reset(&s->ctrl);
    cp0_ui_osk_mod_reset(&s->shift);
    cp0_ui_osk_mod_reset(&s->alt);
    return n;
}

int cp0_zone_upper(const char *utf8, char *out, size_t size)
{
    if (!out || size == 0) return 0;
    out[0] = '\0';
    if (!utf8) return 0;
    snprintf(out, size, "%s", utf8);
    const unsigned char *u = (const unsigned char *)utf8;
    if (u[0] >= 'a' && u[0] <= 'z' && u[1] == '\0') {
        out[0] = (char)(u[0] - 'a' + 'A');
        return 1;
    }
    /* U+00E0..U+00FE (C3 A0..C3 BE) except U+00F7 (C3 B7): minus 0x20; U+00FF (y diaeresis) has no Latin-1 upper case */
    if (u[0] == 0xC3 && u[1] >= 0xA0 && u[1] <= 0xBE && u[1] != 0xB7 && u[2] == '\0' && size >= 3) {
        out[1] = (char)(u[1] - 0x20);
        return 1;
    }
    return 0;
}

int cp0_zone_result_events(cp0_zone_strip_t *s, const strokes_result *r, int multiline, cp0_ui_osk_event_t *out,
                           int max)
{
    if (!s || !r) return 0;
    if (r->kind == STROKES_KEY) {
        const uint32_t code = (uint32_t)strokes_key_linux_code(r->key);
        if (!code) return 0;
        if (code == KEY_TAB) {
            const uint32_t mods = cp0_zone_strip_mods(s);
            const int n = emit_pair(out, max, 0, KEY_TAB, mods, multiline && !mods ? "\t" : NULL);
            return mods_used(s, mods, out, max, n);
        }
        const char *text = code == KEY_SPACE ? " " : cp0_keyboard_control_utf8(code);
        return key_with_mods(s, code, text, out, max, 0);
    }
    if (r->kind != STROKES_CHAR || !r->utf8[0]) return 0;

    /* the strip's Shift makes a letter upper case (the recogniser's own shift already did, when it was on) */
    char text[16];
    const int strip_shift = cp0_ui_osk_mod_on(&s->shift);
    if (strip_shift) cp0_zone_upper(r->utf8, text, sizeof(text));
    else snprintf(text, sizeof(text), "%s", r->utf8);

    uint32_t code = 0;
    int us_shift = 0;
    (void)cp0_ui_osk_char_key(text, &code, &us_shift);
    const uint32_t strip = cp0_zone_strip_mods(s);
    uint32_t mods = 0;
    if (us_shift) mods |= (text[0] >= 'A' && text[0] <= 'Z' && (r->mode & STROKES_MODE_CAPS) && !strip_shift)
                              ? KBD_MOD_CAPS
                              : KBD_MOD_SHIFT;
    else if (strip_shift && strcmp(text, r->utf8) != 0) mods |= KBD_MOD_SHIFT; /* an accented letter made upper case */
    int n = 0;
    if ((strip & KBD_MOD_CTRL) && code) {
        /* Ctrl+C: the key code with CTRL (and Shift, Alt), no text */
        const uint32_t used = KBD_MOD_CTRL | (strip & (KBD_MOD_SHIFT | KBD_MOD_ALT));
        n = emit_pair(out, max, 0, code, mods | used, NULL);
        return mods_used(s, used, out, max, n);
    }
    /* Alt applies to a character with a code, as on the on-screen keyboard (with its text, as a physical Alt+key);
     * Ctrl waits for a key with a code */
    const uint32_t alt = code ? (strip & KBD_MOD_ALT) : 0;
    n = emit_pair(out, max, 0, code, mods | alt, text);
    return mods_used(s, (strip & KBD_MOD_SHIFT) | alt, out, max, n);
}

/* ------------------------------------------------------------------ the mode line */

static void append(char *buf, size_t size, const char *text)
{
    const size_t used = strlen(buf);
    if (used >= size) return;
    snprintf(buf + used, size - used, "%s%s", used ? "  " : "", text);
}

void cp0_zone_mode_text(unsigned mode, int accent, char *letters, size_t letters_size, char *hint, size_t hint_size)
{
    if (letters && letters_size) {
        const char *label = (mode & STROKES_MODE_CAPS) ? "ABC" : (mode & STROKES_MODE_SHIFT) ? "Abc" : "abc";
        snprintf(letters, letters_size, "%s", label);
    }
    if (!hint || !hint_size) return;
    hint[0] = '\0';
    if (mode & STROKES_MODE_CAPS) append(hint, hint_size, "Caps");
    else if (mode & STROKES_MODE_SHIFT) append(hint, hint_size, "Shift");
    if (mode & STROKES_MODE_PUNCT) append(hint, hint_size, "Punct");
    else if (mode & STROKES_MODE_EXT) append(hint, hint_size, "Ext");
    if ((mode & STROKES_MODE_ACCENT) && accent != STROKES_ACCENT_NONE) {
        char text[40];
        snprintf(text, sizeof(text), "Accent: %s", strokes_accent_name(accent));
        append(hint, hint_size, text);
    }
}
