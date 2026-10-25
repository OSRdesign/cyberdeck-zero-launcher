/*
 * SPDX-License-Identifier: MIT
 *
 * On-screen keyboard core (see cp0_ui_osk.h). Pure C: no LVGL, no system call.
 */

#include "cp0_ui_osk.h"

#include "cp0_keyboard_key_contract.h"
#include "input_keys.h"
#include "keyboard_input.h"

#include <stdio.h>
#include <string.h>

/* input_keys.h defines only part of the codes outside Linux (SDL builds on macOS / Windows): the US punctuation keys
 * with their Linux values. */
#ifndef KEY_EQUAL
#define KEY_EQUAL 13
#endif
#ifndef KEY_LEFTBRACE
#define KEY_LEFTBRACE 26
#endif
#ifndef KEY_RIGHTBRACE
#define KEY_RIGHTBRACE 27
#endif
#ifndef KEY_SEMICOLON
#define KEY_SEMICOLON 39
#endif
#ifndef KEY_APOSTROPHE
#define KEY_APOSTROPHE 40
#endif
#ifndef KEY_GRAVE
#define KEY_GRAVE 41
#endif
#ifndef KEY_BACKSLASH
#define KEY_BACKSLASH 43
#endif
#ifndef KEY_COMMA
#define KEY_COMMA 51
#endif
#ifndef KEY_SLASH
#define KEY_SLASH 53
#endif
#ifndef KEY_LEFTALT
#define KEY_LEFTALT 56
#endif

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ layout */

/* Height cap per class, permille of the screen height (report 022 section 3.2; Standard: the F1 rule's 300 px on the
 * deck's 480 rows; Square: half the screen, the user's HackBerry 720x720 test of 2026-10-11, and a target there: the
 * keys grow to fill it). */
static const int k_cap_permille[CP0_UI_CLASS__COUNT] = {
    [CP0_UI_COMPACT] = 600, [CP0_UI_STANDARD] = 625, [CP0_UI_WIDE] = 500,
    [CP0_UI_SQUARE] = 500,  [CP0_UI_LARGE] = 450,    [CP0_UI_PORTRAIT] = 450,
};

int cp0_ui_osk_height_cap(cp0_ui_class_t cls, int screen_h)
{
    if (cls < 0 || cls >= CP0_UI_CLASS__COUNT || screen_h <= 0) return 0;
    return screen_h * k_cap_permille[cls] / 1000;
}

/* even px nearest to tenths_px / 10 */
static int even_px(int tenths_px)
{
    return ((tenths_px + 10) / 20) * 2;
}

static int total_height(const cp0_ui_osk_layout_t *l, int key_h)
{
    const int util_h = (key_h * CP0_UI_OSK_UTIL_PCT + 50) / 100;
    return l->border + 2 * l->pad_y + util_h + 4 * (l->gap + key_h);
}

int cp0_ui_osk_field_reserve(const cp0_ui_metrics_t *m)
{
    if (!m) return 0;
    const int space_m = m->tok[CP0_TOK_SPACE_M];
    const int label_line = (m->text_px[CP0_TEXT_CAPTION] * 13 + 9) / 10; /* a text line: about 1.3 x its px */
    return m->shell.bar_h + space_m + label_line + imax(2, space_m / 3) + m->tok[CP0_TOK_ROW_H] + space_m;
}

static int osk_layout(const cp0_ui_metrics_t *m, int alt, cp0_ui_osk_layout_t *out)
{
    if (!m || !out || m->w <= 0 || m->h <= 0) return -1;
    cp0_ui_osk_layout_t l;
    memset(&l, 0, sizeof(l));
    l.w = m->w;
    l.h = m->h;
    l.border = 1;
    l.gap = imax(2, m->tok[CP0_TOK_SPACE_S]);
    l.margin = l.gap;
    l.pad_y = l.gap;

    l.key_w_cap = cp0_ui_mm_px(m->ppmm_x100, CP0_UI_OSK_KEY_W_MAX_MM_X10, 0);
    l.key_w = imax(8, imin((l.w - 2 * l.margin - 9 * l.gap) / 10, l.key_w_cap));

    l.key_h_token = cp0_ui_mm_px(m->ppmm_x100, CP0_UI_OSK_KEY_MM_X10, CP0_UI_OSK_KEY_MIN_PX);
    l.cap = cp0_ui_osk_height_cap(m->cls, l.h);
    if (m->cls == CP0_UI_SQUARE) {
        /* Square: the keys (and the utility row with them) grow until the keyboard reaches its target, half the
         * screen, but the room above always keeps the header and one field with its label */
        l.cap = imin(l.cap, l.h - cp0_ui_osk_field_reserve(m));
        l.key_h = imax(CP0_UI_OSK_KEY_MIN_PX, l.cap / 4);
    } else {
        l.key_h = l.key_h_token;
    }
    while (l.key_h > CP0_UI_OSK_KEY_MIN_PX && total_height(&l, l.key_h) > l.cap) l.key_h--;
    l.util_h = (l.key_h * CP0_UI_OSK_UTIL_PCT + 50) / 100;
    l.height = total_height(&l, l.key_h);
    l.y = l.h - l.height;

    l.row_w = 10 * l.key_w + 9 * l.gap;
    l.row_x = (l.w - l.row_w) / 2;
    /* number pad: 3 letter keys wide, but on a portrait canvas (taller than wide) the full key area wide: 3 keys of
     * (area - 2 gaps) / 3, the digits drawn larger (pad_label_px, the widget) */
    l.pad_key_w = l.h > l.w ? imax(l.key_w, (l.row_w - 2 * l.gap) / 3) : l.key_w;
    l.pad_w = 3 * l.pad_key_w + 2 * l.gap;
    l.pad_x = (l.w - l.pad_w) / 2;

    /* Esc, Tab, Ctrl (, Alt): 9 mm (the OSK keeps its own size, not the 7 mm TARGET token), at least one key, at most
     * two keys and the gap between them */
    const int util = alt ? 4 : 3;
    l.alt = alt ? 1 : 0;
    l.util_key_w = imax(l.key_w, imin(cp0_ui_mm_px(m->ppmm_x100, 90, 48), 2 * l.key_w + l.gap));
    if (util * l.util_key_w + (util - 1) * l.gap > l.row_w) l.util_key_w = l.key_w;
    /* the four arrows (one key each) at the right, when they leave at least a gap after the last utility key */
    l.arrows = util * l.util_key_w + util * l.gap + 4 * l.key_w + 3 * l.gap <= l.row_w;
    if (!l.arrows && alt) {
        /* a terminal keeps its arrows: narrower utility keys, never under one key */
        const int fit = (l.row_w - 4 * l.key_w - 7 * l.gap) / 4;
        if (fit >= l.key_w) {
            l.util_key_w = fit;
            l.arrows = 1;
        }
    }

    l.label_px = imax(12, even_px(imin(l.key_w, l.key_h) * CP0_UI_OSK_LABEL_PCT / 10));
    l.small_px = imax(12, even_px(l.label_px * CP0_UI_OSK_SMALL_PCT / 10));
    *out = l;
    return 0;
}

int cp0_ui_osk_layout(const cp0_ui_metrics_t *m, cp0_ui_osk_layout_t *out)
{
    return osk_layout(m, 0, out);
}

int cp0_ui_osk_layout_alt(const cp0_ui_metrics_t *m, cp0_ui_osk_layout_t *out)
{
    return osk_layout(m, 1, out);
}

/* ------------------------------------------------------------------ pages */

#define CH(t, c) {CP0_UI_OSK_K_CHAR, t, t, c, 0, 10, CP0_UI_OSK_PAGE_LETTERS}
#define CHS(t, c) {CP0_UI_OSK_K_CHAR, t, t, c, 1, 10, CP0_UI_OSK_PAGE_LETTERS}
#define CHW(t, c, s, w) {CP0_UI_OSK_K_CHAR, t, t, c, s, w, CP0_UI_OSK_PAGE_LETTERS}
#define KEYK(kind, label, code, w) {kind, label, NULL, code, 0, w, CP0_UI_OSK_PAGE_LETTERS}
#define PAGE(label, target, w) {CP0_UI_OSK_K_PAGE, label, NULL, 0, 0, w, target}
#define SPACE(w) {CP0_UI_OSK_K_CHAR, NULL, " ", KEY_SPACE, 0, w, CP0_UI_OSK_PAGE_LETTERS}
#define END_ROW {CP0_UI_OSK_K_CHAR, NULL, NULL, 0, 0, 0, CP0_UI_OSK_PAGE_LETTERS}

static const cp0_ui_osk_key_t k_utility[] = {
    KEYK(CP0_UI_OSK_K_ESC, "Esc", KEY_ESC, 0),
    KEYK(CP0_UI_OSK_K_TAB, "Tab", KEY_TAB, 0),
    KEYK(CP0_UI_OSK_K_CTRL, "Ctrl", 0, 0),
    KEYK(CP0_UI_OSK_K_ALT, "Alt", KEY_LEFTALT, 0), /* a terminal's keyboard only (l->alt) */
};
static const cp0_ui_osk_key_t k_arrows[] = {
    KEYK(CP0_UI_OSK_K_ARROW, NULL, KEY_LEFT, 10),
    KEYK(CP0_UI_OSK_K_ARROW, NULL, KEY_UP, 10),
    KEYK(CP0_UI_OSK_K_ARROW, NULL, KEY_DOWN, 10),
    KEYK(CP0_UI_OSK_K_ARROW, NULL, KEY_RIGHT, 10),
};

/* rows 1-4 of each page, END_ROW after each row */
static const cp0_ui_osk_key_t k_letters[] = {
    CH("q", KEY_Q), CH("w", KEY_W), CH("e", KEY_E), CH("r", KEY_R), CH("t", KEY_T),
    CH("y", KEY_Y), CH("u", KEY_U), CH("i", KEY_I), CH("o", KEY_O), CH("p", KEY_P), END_ROW,
    CH("a", KEY_A), CH("s", KEY_S), CH("d", KEY_D), CH("f", KEY_F), CH("g", KEY_G),
    CH("h", KEY_H), CH("j", KEY_J), CH("k", KEY_K), CH("l", KEY_L), END_ROW,
    KEYK(CP0_UI_OSK_K_SHIFT, NULL, 0, 15),
    CH("z", KEY_Z), CH("x", KEY_X), CH("c", KEY_C), CH("v", KEY_V), CH("b", KEY_B), CH("n", KEY_N), CH("m", KEY_M),
    KEYK(CP0_UI_OSK_K_BACKSPACE, NULL, KEY_BACKSPACE, 15), END_ROW,
    PAGE("123", CP0_UI_OSK_PAGE_SYMBOLS, 15), CH(",", KEY_COMMA), SPACE(45), CH(".", KEY_DOT),
    KEYK(CP0_UI_OSK_K_ENTER, NULL, KEY_ENTER, 20), END_ROW,
};

static const cp0_ui_osk_key_t k_symbols[] = {
    CH("1", KEY_1), CH("2", KEY_2), CH("3", KEY_3), CH("4", KEY_4), CH("5", KEY_5),
    CH("6", KEY_6), CH("7", KEY_7), CH("8", KEY_8), CH("9", KEY_9), CH("0", KEY_0), END_ROW,
    CH("-", KEY_MINUS), CH("/", KEY_SLASH), CHS(":", KEY_SEMICOLON), CH(";", KEY_SEMICOLON), CHS("(", KEY_9),
    CHS(")", KEY_0), CHS("$", KEY_4), CHS("&", KEY_7), CHS("@", KEY_2), CHS("\"", KEY_APOSTROPHE), END_ROW,
    PAGE("#+=", CP0_UI_OSK_PAGE_SYMBOLS2, 15),
    CHW(".", KEY_DOT, 0, 14), CHW(",", KEY_COMMA, 0, 14), CHW("?", KEY_SLASH, 1, 14), CHW("!", KEY_1, 1, 14),
    CHW("'", KEY_APOSTROPHE, 0, 14),
    KEYK(CP0_UI_OSK_K_BACKSPACE, NULL, KEY_BACKSPACE, 15), END_ROW,
    PAGE("abc", CP0_UI_OSK_PAGE_LETTERS, 15), CH(",", KEY_COMMA), SPACE(45), CH(".", KEY_DOT),
    KEYK(CP0_UI_OSK_K_ENTER, NULL, KEY_ENTER, 20), END_ROW,
};

static const cp0_ui_osk_key_t k_symbols2[] = {
    CH("[", KEY_LEFTBRACE), CH("]", KEY_RIGHTBRACE), CHS("{", KEY_LEFTBRACE), CHS("}", KEY_RIGHTBRACE),
    CHS("#", KEY_3), CHS("%", KEY_5), CHS("^", KEY_6), CHS("*", KEY_8), CHS("+", KEY_EQUAL), CH("=", KEY_EQUAL),
    END_ROW,
    CHS("_", KEY_MINUS), CH("\\", KEY_BACKSLASH), CHS("|", KEY_BACKSLASH), CHS("~", KEY_GRAVE), CHS("<", KEY_COMMA),
    CHS(">", KEY_DOT), CH("`", KEY_GRAVE), CH("\xe2\x82\xac", 0), CH("\xc2\xa3", 0), CH("\xc2\xa5", 0), END_ROW,
    PAGE("123", CP0_UI_OSK_PAGE_SYMBOLS, 15),
    CHW(".", KEY_DOT, 0, 14), CHW(",", KEY_COMMA, 0, 14), CHW("?", KEY_SLASH, 1, 14), CHW("!", KEY_1, 1, 14),
    CHW("'", KEY_APOSTROPHE, 0, 14),
    KEYK(CP0_UI_OSK_K_BACKSPACE, NULL, KEY_BACKSPACE, 15), END_ROW,
    PAGE("abc", CP0_UI_OSK_PAGE_LETTERS, 15), CH(",", KEY_COMMA), SPACE(45), CH(".", KEY_DOT),
    KEYK(CP0_UI_OSK_K_ENTER, NULL, KEY_ENTER, 20), END_ROW,
};

static const cp0_ui_osk_key_t k_numpad[] = {
    CH("1", KEY_1), CH("2", KEY_2), CH("3", KEY_3), END_ROW,
    CH("4", KEY_4), CH("5", KEY_5), CH("6", KEY_6), END_ROW,
    CH("7", KEY_7), CH("8", KEY_8), CH("9", KEY_9), END_ROW,
    KEYK(CP0_UI_OSK_K_BACKSPACE, NULL, KEY_BACKSPACE, 10), CH("0", KEY_0),
    KEYK(CP0_UI_OSK_K_ENTER, NULL, KEY_ENTER, 10), END_ROW,
};

static const cp0_ui_osk_key_t *page_keys(cp0_ui_osk_page_t page, int *count)
{
    switch (page) {
    case CP0_UI_OSK_PAGE_LETTERS: *count = (int)(sizeof(k_letters) / sizeof(k_letters[0])); return k_letters;
    case CP0_UI_OSK_PAGE_SYMBOLS: *count = (int)(sizeof(k_symbols) / sizeof(k_symbols[0])); return k_symbols;
    case CP0_UI_OSK_PAGE_SYMBOLS2: *count = (int)(sizeof(k_symbols2) / sizeof(k_symbols2[0])); return k_symbols2;
    case CP0_UI_OSK_PAGE_NUMPAD: *count = (int)(sizeof(k_numpad) / sizeof(k_numpad[0])); return k_numpad;
    default: *count = 0; return NULL;
    }
}

static int is_end_row(const cp0_ui_osk_key_t *k)
{
    return k->width_x10 == 0;
}

int cp0_ui_osk_char_key(const char *utf8, uint32_t *code, int *shift)
{
    if (code) *code = 0;
    if (shift) *shift = 0;
    if (!utf8 || !utf8[0]) return 0;
    if (utf8[0] >= 'A' && utf8[0] <= 'Z' && utf8[1] == '\0') { /* an upper-case letter: its key with Shift */
        const char lower[2] = {(char)(utf8[0] - 'A' + 'a'), '\0'};
        if (!cp0_ui_osk_char_key(lower, code, NULL)) return 0;
        if (shift) *shift = 1;
        return 1;
    }
    const cp0_ui_osk_page_t pages[] = {CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_PAGE_SYMBOLS, CP0_UI_OSK_PAGE_SYMBOLS2};
    for (size_t p = 0; p < sizeof(pages) / sizeof(pages[0]); ++p) {
        int count = 0;
        const cp0_ui_osk_key_t *keys = page_keys(pages[p], &count);
        for (int i = 0; keys && i < count; ++i) {
            const cp0_ui_osk_key_t *k = &keys[i];
            if (k->kind != CP0_UI_OSK_K_CHAR || is_end_row(k) || !k->text || k->code == 0) continue;
            if (strcmp(k->text, utf8) != 0) continue;
            if (code) *code = k->code;
            if (shift) *shift = k->shift;
            return 1;
        }
    }
    return 0;
}

int cp0_ui_osk_is_hold_key(const cp0_ui_osk_key_t *key)
{
    return key && (key->kind == CP0_UI_OSK_K_ESC || key->kind == CP0_UI_OSK_K_BACKSPACE ||
                   key->kind == CP0_UI_OSK_K_ARROW);
}

static int put(cp0_ui_osk_place_t *out, int max, int at, const cp0_ui_osk_key_t *k, int row, int x, int y, int w,
               int h)
{
    if (at >= max) return -1;
    out[at].key = k;
    out[at].row = row;
    out[at].x = x;
    out[at].y = y;
    out[at].w = w;
    out[at].h = h;
    return at + 1;
}

int cp0_ui_osk_place_keys(const cp0_ui_osk_layout_t *l, cp0_ui_osk_page_t page, cp0_ui_osk_place_t *out, int max)
{
    if (!l || !out || max <= 0) return -1;
    int count = 0;
    const cp0_ui_osk_key_t *keys = page_keys(page, &count);
    if (!keys) return -1;
    int n = 0;

    /* row 0: Esc, Tab, Ctrl (, Alt) from the left of the key area, the arrows at its right */
    const int y0 = l->border + l->pad_y;
    int x = l->row_x;
    for (int i = 0; i < (l->alt ? 4 : 3) && n >= 0; ++i) {
        n = put(out, max, n, &k_utility[i], 0, x, y0, l->util_key_w, l->util_h);
        x += l->util_key_w + l->gap;
    }
    if (l->arrows) {
        for (int i = 0; i < 4 && n >= 0; ++i) {
            const int ax = l->row_x + l->row_w - (4 - i) * l->key_w - (3 - i) * l->gap;
            n = put(out, max, n, &k_arrows[i], 0, ax, y0, l->key_w, l->util_h);
        }
    }

    /* rows 1-4: widths in letter keys x10, each row centred in the key area (the number pad's for NUMPAD); a key's
     * edges follow the cumulated units so a full row ends exactly at the area's right edge */
    const int area_x = page == CP0_UI_OSK_PAGE_NUMPAD ? l->pad_x : l->row_x;
    const int area_w = page == CP0_UI_OSK_PAGE_NUMPAD ? l->pad_w : l->row_w;
    const int pitch = (page == CP0_UI_OSK_PAGE_NUMPAD ? l->pad_key_w : l->key_w) + l->gap;
    int row = 1;
    int first = 0;
    for (int i = 0; i < count && n >= 0; ++i) {
        if (!is_end_row(&keys[i])) continue;
        int units = 0;
        for (int j = first; j < i; ++j) units += keys[j].width_x10;
        const int span = (units * pitch + 5) / 10 - l->gap;
        const int left = area_x + (area_w - span) / 2;
        const int y = y0 + l->util_h + l->gap + (row - 1) * (l->key_h + l->gap);
        int cum = 0;
        for (int j = first; j < i && n >= 0; ++j) {
            const int kx = left + (cum * pitch + 5) / 10;
            cum += keys[j].width_x10;
            const int kr = left + (cum * pitch + 5) / 10 - l->gap;
            n = put(out, max, n, &keys[j], row, kx, y, kr - kx, l->key_h);
        }
        first = i + 1;
        ++row;
    }
    return n;
}

/* ------------------------------------------------------------------ modifiers */

void cp0_ui_osk_mod_tap(cp0_ui_osk_mod_t *m, uint64_t now_ms)
{
    if (!m) return;
    switch (m->state) {
    case CP0_UI_OSK_MOD_OFF:
        m->state = CP0_UI_OSK_MOD_ARMED;
        m->armed_at_ms = now_ms;
        break;
    case CP0_UI_OSK_MOD_ARMED:
        m->state = now_ms - m->armed_at_ms <= CP0_UI_OSK_DOUBLE_TAP_MS ? CP0_UI_OSK_MOD_LOCKED : CP0_UI_OSK_MOD_OFF;
        break;
    default:
        m->state = CP0_UI_OSK_MOD_OFF;
        break;
    }
}

void cp0_ui_osk_mod_used(cp0_ui_osk_mod_t *m)
{
    if (m && m->state == CP0_UI_OSK_MOD_ARMED) m->state = CP0_UI_OSK_MOD_OFF;
}

void cp0_ui_osk_mod_reset(cp0_ui_osk_mod_t *m)
{
    if (!m) return;
    m->state = CP0_UI_OSK_MOD_OFF;
    m->armed_at_ms = 0;
}

int cp0_ui_osk_mod_on(const cp0_ui_osk_mod_t *m)
{
    return m && m->state != CP0_UI_OSK_MOD_OFF;
}

/* ------------------------------------------------------------------ key events */

void cp0_ui_osk_state_init(cp0_ui_osk_state_t *s, int numeric, int multiline)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->home = numeric ? CP0_UI_OSK_PAGE_NUMPAD : CP0_UI_OSK_PAGE_LETTERS;
    s->page = s->home;
    s->multiline = multiline != 0;
}

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

/* press + release of one key */
static int emit_pair(cp0_ui_osk_event_t *out, int max, int at, uint32_t code, uint32_t mods, const char *utf8)
{
    at = emit(out, max, at, code, KBD_KEY_PRESSED, mods, utf8);
    return emit(out, max, at, code, KBD_KEY_RELEASED, mods, utf8);
}

/* Alt was used by a key: an armed Alt goes off, and its key's release goes out */
static int alt_used(cp0_ui_osk_state_t *s, cp0_ui_osk_event_t *out, int max, int at)
{
    if (s->alt.state != CP0_UI_OSK_MOD_ARMED) return at;
    cp0_ui_osk_mod_used(&s->alt);
    return emit(out, max, at, KEY_LEFTALT, KBD_KEY_RELEASED, 0, NULL);
}

/* the modifiers a key carries: Alt on (a terminal's keyboard) */
static uint32_t alt_mods(const cp0_ui_osk_state_t *s)
{
    return cp0_ui_osk_mod_on(&s->alt) ? KBD_MOD_ALT : 0;
}

static int release_held(cp0_ui_osk_state_t *s, cp0_ui_osk_event_t *out, int max, int at)
{
    if (!s->held_code) return at;
    at = emit(out, max, at, s->held_code, KBD_KEY_RELEASED, s->held_mods, s->held_utf8);
    if (s->held_mods & KBD_MOD_CTRL) cp0_ui_osk_mod_used(&s->ctrl);
    if (s->held_mods & KBD_MOD_ALT) at = alt_used(s, out, max, at);
    s->held_code = 0;
    s->held_mods = 0;
    s->held_utf8[0] = '\0';
    return at;
}

static int is_ascii_letter(const char *text)
{
    return text && text[0] >= 'a' && text[0] <= 'z' && text[1] == '\0';
}

static int char_events(cp0_ui_osk_state_t *s, const cp0_ui_osk_key_t *k, cp0_ui_osk_event_t *out, int max)
{
    const int letter = s->page == CP0_UI_OSK_PAGE_LETTERS && is_ascii_letter(k->text);
    const int shift_on = cp0_ui_osk_mod_on(&s->shift);
    const int ctrl_on = cp0_ui_osk_mod_on(&s->ctrl) && k->code != 0; /* a key without a code types its text */
    int n = 0;
    const uint32_t alt = k->code != 0 ? alt_mods(s) : 0; /* Alt applies to keys with a code, as Ctrl */
    if (ctrl_on) {
        /* Ctrl+C: the key code with CTRL, no text */
        const uint32_t mods = KBD_MOD_CTRL | alt | ((letter && shift_on) || k->shift ? KBD_MOD_SHIFT : 0);
        n = emit_pair(out, max, 0, k->code, mods, NULL);
        cp0_ui_osk_mod_used(&s->ctrl);
    } else {
        char text[16];
        snprintf(text, sizeof(text), "%s", k->text ? k->text : "");
        uint32_t mods = k->shift ? KBD_MOD_SHIFT : 0;
        if (letter && shift_on) {
            text[0] = (char)(text[0] - 'a' + 'A');
            mods = s->shift.state == CP0_UI_OSK_MOD_LOCKED ? KBD_MOD_CAPS : KBD_MOD_SHIFT;
        }
        n = emit_pair(out, max, 0, k->code, mods | alt, text); /* with Alt: the text too, as a physical Alt+key */
    }
    cp0_ui_osk_mod_used(&s->shift); /* one-shot: the next character key uses it */
    if (alt) n = alt_used(s, out, max, n);
    return n;
}

int cp0_ui_osk_key_down(cp0_ui_osk_state_t *s, const cp0_ui_osk_key_t *k, cp0_ui_osk_event_t *out, int max)
{
    if (!s || !k || !cp0_ui_osk_is_hold_key(k)) return 0;
    int n = release_held(s, out, max, 0);
    /* Esc stays exactly the toolbar's Esc: never combined with Ctrl or Alt (while Alt is down it cancels an Alt+Tab
     * session, Alt stays) */
    const int ctrl_on = k->kind != CP0_UI_OSK_K_ESC && cp0_ui_osk_mod_on(&s->ctrl);
    const char *control = cp0_keyboard_control_utf8(k->code);
    s->held_code = k->code;
    s->held_mods = (ctrl_on ? KBD_MOD_CTRL : 0) | (k->kind != CP0_UI_OSK_K_ESC ? alt_mods(s) : 0);
    snprintf(s->held_utf8, sizeof(s->held_utf8), "%s", !ctrl_on && control ? control : "");
    return emit(out, max, n, s->held_code, KBD_KEY_PRESSED, s->held_mods, s->held_utf8);
}

int cp0_ui_osk_key_repeat(cp0_ui_osk_state_t *s, const cp0_ui_osk_key_t *k, cp0_ui_osk_event_t *out, int max)
{
    if (!s || !k || !cp0_ui_osk_is_hold_key(k) || s->held_code != k->code) return 0;
    return emit(out, max, 0, s->held_code, KBD_KEY_REPEATED, s->held_mods, s->held_utf8);
}

int cp0_ui_osk_key_up(cp0_ui_osk_state_t *s, const cp0_ui_osk_key_t *k, uint64_t now_ms, cp0_ui_osk_event_t *out,
                      int max)
{
    if (!s || !k) return 0;
    if (cp0_ui_osk_is_hold_key(k)) return release_held(s, out, max, 0);
    const int ctrl_on = cp0_ui_osk_mod_on(&s->ctrl);
    int n = 0;
    switch (k->kind) {
    case CP0_UI_OSK_K_CHAR:
        n = char_events(s, k, out, max);
        break;
    case CP0_UI_OSK_K_TAB: {
        /* a tab character only in a multi-line field; elsewhere the key code alone (next field). With Alt: Alt+Tab,
         * the code alone (the switcher reads the key codes: Alt's press, Tab, Alt's release) */
        const uint32_t alt = alt_mods(s);
        n = emit_pair(out, max, 0, KEY_TAB, (ctrl_on ? KBD_MOD_CTRL : 0) | alt,
                      !ctrl_on && !alt && s->multiline ? "\t" : NULL);
        cp0_ui_osk_mod_used(&s->ctrl);
        n = alt_used(s, out, max, n);
        break;
    }
    case CP0_UI_OSK_K_ENTER:
        n = emit_pair(out, max, 0, KEY_ENTER, (ctrl_on ? KBD_MOD_CTRL : 0) | alt_mods(s),
                      ctrl_on ? NULL : cp0_keyboard_control_utf8(KEY_ENTER));
        cp0_ui_osk_mod_used(&s->ctrl);
        n = alt_used(s, out, max, n);
        break;
    case CP0_UI_OSK_K_SHIFT:
        cp0_ui_osk_mod_tap(&s->shift, now_ms);
        break;
    case CP0_UI_OSK_K_CTRL:
        cp0_ui_osk_mod_tap(&s->ctrl, now_ms);
        break;
    case CP0_UI_OSK_K_ALT: {
        /* a real key: Alt on = its press, Alt off = its release (armed -> locked sends nothing) */
        const int was_on = cp0_ui_osk_mod_on(&s->alt);
        cp0_ui_osk_mod_tap(&s->alt, now_ms);
        const int on = cp0_ui_osk_mod_on(&s->alt);
        if (on && !was_on) n = emit(out, max, 0, KEY_LEFTALT, KBD_KEY_PRESSED, KBD_MOD_ALT, NULL);
        if (!on && was_on) n = emit(out, max, 0, KEY_LEFTALT, KBD_KEY_RELEASED, 0, NULL);
        break;
    }
    case CP0_UI_OSK_K_PAGE:
        s->page = k->target;
        break;
    default:
        break;
    }
    return n;
}

int cp0_ui_osk_key_cancel(cp0_ui_osk_state_t *s, cp0_ui_osk_event_t *out, int max)
{
    return s ? release_held(s, out, max, 0) : 0;
}

int cp0_ui_osk_release_all(cp0_ui_osk_state_t *s, cp0_ui_osk_event_t *out, int max)
{
    if (!s) return 0;
    int n = release_held(s, out, max, 0);
    if (cp0_ui_osk_mod_on(&s->alt)) n = emit(out, max, n, KEY_LEFTALT, KBD_KEY_RELEASED, 0, NULL);
    cp0_ui_osk_mod_reset(&s->shift);
    cp0_ui_osk_mod_reset(&s->ctrl);
    cp0_ui_osk_mod_reset(&s->alt);
    return n;
}

/* ------------------------------------------------------------------ policy */

cp0_ui_osk_env_t cp0_ui_osk_env_parse(const char *value)
{
    if (!value) return CP0_UI_OSK_ENV_UNSET;
    if (strcmp(value, "force") == 0) return CP0_UI_OSK_ENV_FORCE;
    if (strcmp(value, "auto") == 0) return CP0_UI_OSK_ENV_AUTO;
    if (strcmp(value, "off") == 0) return CP0_UI_OSK_ENV_OFF;
    return CP0_UI_OSK_ENV_UNSET;
}

cp0_app_vkb_t cp0_ui_osk_effective_vkb(cp0_ui_osk_env_t env, int setting_off, int desktop_session)
{
    if (env == CP0_UI_OSK_ENV_OFF) return CP0_APP_VKB_OFF;
    if (env == CP0_UI_OSK_ENV_FORCE) return CP0_APP_VKB_FORCE;
    if (setting_off || desktop_session) return CP0_APP_VKB_OFF;
    return CP0_APP_VKB_AUTO;
}

int cp0_ui_osk_policy_allows(const cp0_ui_osk_policy_t *p)
{
    if (!p || p->locked) return 0;
    return cp0_app_osk_rule(cp0_ui_osk_effective_vkb(p->env, p->setting_off, p->desktop_session), p->keyboards,
                            p->now_s, p->any_gone_s, p->bt_gone_s);
}

/* ------------------------------------------------------------------ dialogs */

int cp0_ui_osk_dialog_y(int screen_h, int top, int inset, int dialog_h)
{
    const int bottom = imax(top, screen_h - imax(0, inset));
    const int room = bottom - top;
    if (dialog_h >= room) return top;
    return top + (room - dialog_h) / 2;
}
