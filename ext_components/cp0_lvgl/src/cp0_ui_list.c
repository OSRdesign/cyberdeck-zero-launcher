/*
 * SPDX-License-Identifier: MIT
 *
 * List maths and layout of CompactList (see cp0_ui_list.h). Pure C: no LVGL.
 */

#include "cp0_ui_list.h"

#include <stddef.h>

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* round(value * pct / 100) for non-negative values */
static int pct_of(int value, int pct)
{
    return (value * pct + 50) / 100;
}

/* ------------------------------------------------------------------ layout */

int cp0_ui_list_layout(const cp0_ui_metrics_t *m, cp0_ui_list_density_t density, int x, int y, int w, int h,
                       cp0_ui_list_layout_t *out)
{
    if (!m || !out || w <= 0 || h <= 0) return -1;
    cp0_ui_list_layout_t l = {0};
    l.x = x;
    l.y = y;
    l.w = w;
    l.h = h;
    l.density = density == CP0_UI_LIST_COMFORTABLE ? CP0_UI_LIST_COMFORTABLE : CP0_UI_LIST_COMPACT;
    const int comfortable = l.density == CP0_UI_LIST_COMFORTABLE;
    l.row_h = imax(1, imin(m->tok[comfortable ? CP0_TOK_ROW_H : CP0_TOK_LIST_ROW_H], h));
    l.section_h = imax(1, pct_of(l.row_h, CP0_UI_LIST_SECTION_PCT));
    l.text_px = cp0_ui_roller_font_px(
        pct_of(l.row_h, comfortable ? CP0_UI_LIST_TEXT_PCT_COMFORTABLE : CP0_UI_LIST_TEXT_PCT_COMPACT));
    l.value_px = cp0_ui_roller_font_px(pct_of(l.text_px, CP0_UI_LIST_VALUE_PCT));
    l.section_px = cp0_ui_roller_font_px(pct_of(l.text_px, CP0_UI_LIST_SECTION_PCT));
    l.inset = m->tok[CP0_TOK_SPACE_L];
    l.gap = m->tok[CP0_TOK_SPACE_M];
    l.separator_px = 0;
    l.scrollbar_w = imax(3, m->tok[CP0_TOK_SPACE_M] / 3);
    l.scrollbar_pad = imax(2, m->tok[CP0_TOK_SPACE_S] / 2);
    *out = l;
    return 0;
}

int cp0_ui_list_page_layout(const cp0_ui_metrics_t *m, const cp0_ui_roller_page_t *page, cp0_ui_list_density_t density,
                            cp0_ui_list_layout_t *out)
{
    if (!page) return -1;
    return cp0_ui_list_layout(m, density, page->roller_x, page->body_y, page->roller_w, page->body_h, out);
}

int cp0_ui_list_row_height(const cp0_ui_list_layout_t *l, cp0_ui_list_kind_t kind)
{
    if (!l) return 0;
    return kind == CP0_UI_LIST_SECTION ? l->section_h : l->row_h;
}

int cp0_ui_list_tops(const cp0_ui_list_layout_t *l, const cp0_ui_list_kind_t *kinds, int count, int *tops)
{
    if (!l || !tops || count < 0) return -1;
    int y = 0;
    for (int i = 0; i < count; ++i) {
        tops[i] = y;
        y += cp0_ui_list_row_height(l, kinds ? kinds[i] : CP0_UI_LIST_ITEM);
    }
    tops[count] = y;
    return y;
}

int cp0_ui_list_max_scroll(int content_h, int view_h)
{
    return imax(0, content_h - imax(0, view_h));
}

int cp0_ui_list_row_at(const int *tops, int count, int y)
{
    if (!tops || count <= 0 || y < tops[0] || y >= tops[count]) return -1;
    int lo = 0, hi = count - 1; /* the last row whose top is <= y */
    while (lo < hi) {
        const int mid = lo + (hi - lo + 1) / 2;
        if (tops[mid] <= y) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

int cp0_ui_list_scroll_to_show(const int *tops, int count, int index, int scroll_y, int view_h, int context)
{
    if (!tops || count <= 0 || view_h <= 0) return 0;
    const int content = tops[count];
    const int max = cp0_ui_list_max_scroll(content, view_h);
    scroll_y = clampi(scroll_y, 0, max);
    if (index < 0 || index >= count) return scroll_y;
    int top = tops[index], bottom = tops[index + 1];
    if (bottom - top >= view_h) return clampi(top, 0, max); /* taller than the view: its top */
    if (context) {
        int above = top - (index > 0 ? tops[index - 1] : top);
        int below = (index + 1 < count ? tops[index + 2] : bottom) - bottom;
        const int room = view_h - (bottom - top); /* what the neighbours may take */
        if (above + below > room) {
            const int half = room / 2;
            if (above <= half) below = room - above;
            else if (below <= room - half) above = room - below;
            else {
                above = half;
                below = room - half;
            }
        }
        top -= above;
        bottom += below;
    }
    if (top < scroll_y) scroll_y = top;
    else if (bottom > scroll_y + view_h) scroll_y = bottom - view_h;
    return clampi(scroll_y, 0, max);
}

/* ------------------------------------------------------------------ items */

int cp0_ui_list_selectable(const cp0_ui_list_kind_t *kinds, int count, int index)
{
    if (index < 0 || index >= count) return 0;
    return !kinds || kinds[index] == CP0_UI_LIST_ITEM;
}

int cp0_ui_list_first(const cp0_ui_list_kind_t *kinds, int count)
{
    for (int i = 0; i < count; ++i)
        if (cp0_ui_list_selectable(kinds, count, i)) return i;
    return -1;
}

int cp0_ui_list_last(const cp0_ui_list_kind_t *kinds, int count)
{
    for (int i = count - 1; i >= 0; --i)
        if (cp0_ui_list_selectable(kinds, count, i)) return i;
    return -1;
}

/* the next item from `from` (exclusive) in direction dir, -1 when none */
static int next_item(const cp0_ui_list_kind_t *kinds, int count, int from, int dir)
{
    for (int i = from + dir; i >= 0 && i < count; i += dir)
        if (cp0_ui_list_selectable(kinds, count, i)) return i;
    return -1;
}

int cp0_ui_list_step(const cp0_ui_list_kind_t *kinds, int count, int index, int delta, int wrap)
{
    const int first = cp0_ui_list_first(kinds, count);
    if (first < 0) return -1;
    const int last = cp0_ui_list_last(kinds, count);
    const int dir = delta < 0 ? -1 : 1;
    long long steps = delta < 0 ? -(long long)delta : delta;
    int at = index;
    if (!cp0_ui_list_selectable(kinds, count, at)) {
        /* not an item: the nearest item in the step's direction is the first step */
        at = clampi(at, -1, count);
        const int near = next_item(kinds, count, at, dir);
        if (steps == 0) {
            const int other = next_item(kinds, count, at, -dir);
            return near >= 0 ? near : other;
        }
        if (near < 0) return wrap ? (dir > 0 ? first : last) : (dir > 0 ? last : first);
        at = near;
        steps--;
    }
    for (long long s = 0; s < steps; ++s) {
        const int next = next_item(kinds, count, at, dir);
        if (next >= 0) {
            at = next;
            continue;
        }
        if (wrap && s == 0 && at == index) at = dir > 0 ? first : last; /* one step past an end */
        break;
    }
    return at;
}

int cp0_ui_list_label_w(int row_w, int inset, int gap, int value_w)
{
    const int room = imax(0, row_w - 2 * imax(0, inset));
    if (value_w <= 0) return room;
    return imax(0, room - imin(value_w, room) - imax(0, gap));
}

unsigned cp0_ui_list_tone_rgb(cp0_ui_list_tone_t tone)
{
    switch (tone) {
    case CP0_UI_LIST_TONE_OK: return CP0_UI_LIST_OK_RGB;
    case CP0_UI_LIST_TONE_WARN: return CP0_UI_LIST_WARN_RGB;
    case CP0_UI_LIST_TONE_BAD: return CP0_UI_LIST_BAD_RGB;
    default: return CP0_UI_LIST_VALUE_RGB;
    }
}

/* ------------------------------------------------------------------ state machine */

static int content_h(const cp0_ui_list_state_t *s)
{
    return s->count > 0 ? s->tops[s->count] : 0;
}

/* The scroll for a new selection: one row of context (keyboard); the first item also shows whatever is above it (a
 * leading section) and the last one what is below it, when that fits. */
static int show_item(const cp0_ui_list_state_t *s, int index, int scroll)
{
    const int max = cp0_ui_list_max_scroll(content_h(s), s->view_h);
    int y = cp0_ui_list_scroll_to_show(s->tops, s->count, index, scroll, s->view_h, 1);
    if (index == cp0_ui_list_first(s->kinds, s->count) && s->tops[index + 1] <= s->view_h) y = 0;
    if (index == cp0_ui_list_last(s->kinds, s->count) && content_h(s) - s->tops[index] <= s->view_h) y = max;
    return y;
}

static cp0_ui_list_result_t base_result(const cp0_ui_list_state_t *s)
{
    cp0_ui_list_result_t r = {CP0_UI_LIST_NONE, -1, 0};
    if (!s) return r;
    r.index = s->selected;
    if (s->tops && s->count > 0) r.scroll_y = clampi(s->scroll_y, 0, cp0_ui_list_max_scroll(content_h(s), s->view_h));
    return r;
}

static int valid(const cp0_ui_list_state_t *s)
{
    return s && s->tops && s->count >= 0;
}

cp0_ui_list_result_t cp0_ui_list_key(const cp0_ui_list_state_t *s, cp0_ui_list_key_t key)
{
    cp0_ui_list_result_t r = base_result(s);
    if (!valid(s)) return r;
    if (key == CP0_UI_LIST_KEY_ESC || key == CP0_UI_LIST_KEY_LEFT) {
        r.action = CP0_UI_LIST_BACK;
        return r;
    }
    const int max = cp0_ui_list_max_scroll(content_h(s), s->view_h);
    if (s->read_only) {
        const int step = imax(1, s->row_h);
        const int page = imax(step, s->view_h - step);
        int to = r.scroll_y;
        switch (key) {
        case CP0_UI_LIST_KEY_UP: to -= step; break;
        case CP0_UI_LIST_KEY_DOWN: to += step; break;
        case CP0_UI_LIST_KEY_PAGE_UP: to -= page; break;
        case CP0_UI_LIST_KEY_PAGE_DOWN: to += page; break;
        case CP0_UI_LIST_KEY_HOME: to = 0; break;
        case CP0_UI_LIST_KEY_END: to = max; break;
        default: return r;
        }
        r.action = CP0_UI_LIST_SCROLL;
        r.scroll_y = clampi(to, 0, max);
        return r;
    }
    const int selected = cp0_ui_list_selectable(s->kinds, s->count, s->selected) ? s->selected : -1;
    if (key == CP0_UI_LIST_KEY_ENTER || key == CP0_UI_LIST_KEY_RIGHT) {
        if (selected >= 0) {
            r.action = CP0_UI_LIST_ACTIVATE;
            r.index = selected;
        }
        return r;
    }
    const int first = cp0_ui_list_first(s->kinds, s->count);
    int target = -1;
    switch (key) {
    case CP0_UI_LIST_KEY_UP:
    case CP0_UI_LIST_KEY_DOWN:
        target = selected < 0 ? first
                              : cp0_ui_list_step(s->kinds, s->count, selected, key == CP0_UI_LIST_KEY_UP ? -1 : 1, s->wrap);
        break;
    case CP0_UI_LIST_KEY_PAGE_UP:
    case CP0_UI_LIST_KEY_PAGE_DOWN: {
        const int n = imax(1, s->view_h / imax(1, s->row_h) - 1);
        target = selected < 0 ? first
                              : cp0_ui_list_step(s->kinds, s->count, selected, key == CP0_UI_LIST_KEY_PAGE_UP ? -n : n, 0);
        break;
    }
    case CP0_UI_LIST_KEY_HOME: target = first; break;
    case CP0_UI_LIST_KEY_END: target = cp0_ui_list_last(s->kinds, s->count); break;
    default: return r;
    }
    if (target < 0) return r; /* no item */
    r.action = CP0_UI_LIST_MOVE;
    r.index = target;
    r.scroll_y = show_item(s, target, r.scroll_y);
    return r;
}

cp0_ui_list_result_t cp0_ui_list_tap(const cp0_ui_list_state_t *s, int index, int busy)
{
    cp0_ui_list_result_t r = base_result(s);
    if (!valid(s) || s->read_only || !cp0_ui_list_selectable(s->kinds, s->count, index)) return r;
    if (index == s->selected) {
        if (!busy) {
            r.action = CP0_UI_LIST_ACTIVATE;
            r.index = index;
        }
        return r;
    }
    r.action = CP0_UI_LIST_MOVE;
    r.index = index;
    r.scroll_y = cp0_ui_list_scroll_to_show(s->tops, s->count, index, r.scroll_y, s->view_h, 0);
    return r;
}

cp0_ui_list_result_t cp0_ui_list_keep(const cp0_ui_list_state_t *s)
{
    cp0_ui_list_result_t r = base_result(s);
    if (!valid(s)) return r;
    r.action = CP0_UI_LIST_MOVE;
    r.index = -1;
    const int first = cp0_ui_list_first(s->kinds, s->count);
    if (s->read_only || first < 0) return r;
    if (s->selected < 0) {
        r.index = first; /* the highlight starts on the first item */
        return r;
    }
    const int at = imin(s->selected, s->count - 1);
    if (cp0_ui_list_selectable(s->kinds, s->count, at)) r.index = at;
    else {
        const int next = next_item(s->kinds, s->count, at, 1);
        r.index = next >= 0 ? next : next_item(s->kinds, s->count, at, -1);
    }
    return r;
}
