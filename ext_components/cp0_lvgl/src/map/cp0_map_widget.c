/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: the LVGL map widget (see cp0_map_widget.h). The view maths, the control layout, track simplification and
 * label placement are the pure core (cp0_map_view.c); the tile-load thread is cp0_map_loader.c; an online source's
 * missing tiles come from the store's fetch pool (cp0_map_fetch.h, task 3).
 */
#include "cp0_map_widget.h"

#include "cp0_map_loader.h"
#include "cp0_map_view.h"
#include "cp0_ui_list.h"
#include "cp0_ui_metrics_lvgl.h"
#include "input_keys.h"
#include "keyboard_input.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAP_MAGIC 0x4d415057u /* "MAPW" */
#define MAX_PINS (2 * CP0_MAP_VIEW_MAX_TILES)
#define ABSENT_MAX 256
#define FAST_MS 40
#define SLOW_MS 1000
#define MISSING_CHECK_MS 2000
#define DOUBLE_TAP_MS 350
#define LABEL_CAND 128

/* the toolkit palette (cp0_ui_list.h, the shell's dark theme) */
#define C_PLATE CP0_UI_LIST_BAR_RGB      /* #2B2B2B: plates, the empty map */
#define C_TEXT CP0_UI_LIST_TEXT_RGB      /* white */
#define C_MUTED CP0_UI_LIST_SECTION_RGB  /* grey */
#define C_GRID CP0_UI_LIST_SCROLLBAR_RGB /* #555555: grid lines */
#define C_ACCENT CP0_UI_LIST_ACCENT_RGB  /* blue: position, focus, pressed, nodes */
#define C_OK CP0_UI_LIST_OK_RGB          /* green: repeaters */
#define C_WARN CP0_UI_LIST_WARN_RGB      /* amber: rooms */
#define C_BAD CP0_UI_LIST_BAD_RGB        /* red: waypoints, tracks, the missing-archive strip */
#define C_SHADOW 0x000000

enum { LAYER_MARKERS = 1, LAYER_TRACK = 2 };

typedef struct map map_t;

/* a small set of tile keys (absent, stale, not fetchable); the oldest is replaced when full */
typedef struct {
    cp0_map_tile_key_t k[ABSENT_MAX];
    int n, next;
} keyset_t;

struct cp0_map_layer {
    map_t *map;
    int kind;
    int shown;
    char name[32];
    /* markers */
    cp0_map_marker_t *m;
    double *mx, *my;             /* world px at zoom 0 */
    int n, selected, label_zoom;
    /* track */
    double *tx, *ty;             /* world px at zoom 0 */
    int tn, tcap;
    int *keep, *stack;
    unsigned char *mark;
    int kept, simp_z, simp_scale, simp_dirty;
};

struct map {
    uint32_t magic;
    lv_obj_t *obj, *screen;
    cp0_map_store_t *store;
    cp0_map_cache_t *cache;
    cp0_map_loader_t *loader;
    cp0_map_store_fetch_t fetch;
    char source[CP0_MAP_ID_MAX];
    uint32_t source_hash;
    cp0_map_view_t view;
    float scale_req;
    cp0_map_units_t units;
    /* attribution: the source's, or the archives that answered (every-archive source) */
    char attr_ids[2][CP0_MAP_ID_MAX];
    char attribution[256], attr_draw[256];
    int attr_dirty;
    /* missing archive */
    int missing;
    char missing_text[160];
    uint32_t missing_checked;
    /* position */
    int has_fix, fix_fresh;
    cp0_map_fix_t fix;
    /* layers */
    cp0_map_layer_t *layers[CP0_MAP_LAYERS_MAX];
    int nlayers;
    /* events */
    cp0_map_tile_cb on_tile;
    void *on_tile_user;
    /* input */
    int pressing, moved;
    lv_point_t press, last;
    cp0_map_ctl_t press_ctl;
    uint32_t last_tap_ms;
    lv_point_t last_tap;
    /* drawing */
    cp0_map_slot_t slots[CP0_MAP_VIEW_MAX_TILES];
    lv_image_dsc_t imgs[MAX_PINS];
    const cp0_map_image_t *pins[MAX_PINS], *prev_pins[MAX_PINS];
    int npins, nprev;
    char scalebar_label[24];
    cp0_map_layout_t layout;
    int layout_ok;
    /* loading */
    keyset_t absent;
    lv_timer_t *timer;
    int timer_period;
    int view_dirty;
    cp0_map_req_t reqs[CP0_MAP_LOADER_QUEUE];
    cp0_map_done_t done[64];
    /* an online source (task 3): the visible tiles the loader did not find, or found stale, go to the fetch pool */
    int online;
    cp0_map_client_t *client;
    keyset_t stale, nofetch, refresh;
    cp0_map_tile_id_t want[CP0_MAP_CLIENT_TILES_MAX];
    cp0_map_tile_event_t fev[32];
    int fetch_dirty;
    int src_state;               /* CP0_MAP_OK, or why the source does not fetch (the state strip) */
    char state_text[96];
    /* labels (scratch, no per-frame allocation) */
    int lsx[LABEL_CAND], lsy[LABEL_CAND], llw[LABEL_CAND], llh[LABEL_CAND], llx[LABEL_CAND], lly[LABEL_CAND];
    const char *ltext[LABEL_CAND];
};

static lv_color_t rgb(uint32_t c)
{
    return lv_color_hex(c);
}

static map_t *map_of(const lv_obj_t *obj)
{
    if (!obj) return NULL;
    map_t *m = (map_t *)lv_obj_get_user_data((lv_obj_t *)obj);
    return (m && m->magic == MAP_MAGIC && m->obj == obj) ? m : NULL;
}

int cp0_map_is_map(const lv_obj_t *obj)
{
    return map_of(obj) != NULL;
}

static const lv_font_t *caption_font(void)
{
    return cp0_ui_font(CP0_TEXT_CAPTION);
}

static int text_w(const char *s)
{
    if (!s || !s[0]) return 0;
    lv_point_t p;
    lv_text_get_size(&p, s, caption_font(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

static uint8_t variant_of(const map_t *m, int over)
{
    return (uint8_t)(cp0_map_tile_variant(m->view.scale_x2, CP0_MAP_PIX_RGB565) | (over ? 0x08 : 0));
}

static cp0_map_tile_key_t key_of(const map_t *m, int z, uint32_t x, uint32_t y, int over)
{
    cp0_map_tile_key_t k = {m->source_hash, x, y, (uint8_t)z, variant_of(m, over)};
    return k;
}

static int key_eq(const cp0_map_tile_key_t *a, const cp0_map_tile_key_t *b)
{
    return a->source == b->source && a->x == b->x && a->y == b->y && a->z == b->z && a->variant == b->variant;
}

static int ks_has(const keyset_t *s, const cp0_map_tile_key_t *k)
{
    for (int i = 0; i < s->n; i++)
        if (key_eq(&s->k[i], k)) return 1;
    return 0;
}

static void ks_add(keyset_t *s, const cp0_map_tile_key_t *k)
{
    if (ks_has(s, k)) return;
    if (s->n < ABSENT_MAX) s->k[s->n++] = *k;
    else {
        s->k[s->next] = *k;
        s->next = (s->next + 1) % ABSENT_MAX;
    }
}

static void ks_del(keyset_t *s, const cp0_map_tile_key_t *k)
{
    for (int i = 0; i < s->n; i++)
        if (key_eq(&s->k[i], k)) {
            s->k[i] = s->k[--s->n];
            s->next = 0;
            return;
        }
}

static void ks_clear(keyset_t *s)
{
    s->n = s->next = 0;
}

static int is_absent(const map_t *m, const cp0_map_tile_key_t *k)
{
    return ks_has(&m->absent, k);
}

static void add_absent(map_t *m, const cp0_map_tile_key_t *k)
{
    ks_add(&m->absent, k);
}

/* the tile the fetch pool is asked for: the slot's own, or the parent an over-zoom slot is drawn from */
static void fetch_tile_of(const cp0_map_slot_t *s, cp0_map_tile_id_t *t)
{
    t->z = s->over ? s->z - 1 : s->z;
    t->x = s->over ? s->x >> 1 : s->x;
    t->y = s->over ? s->y >> 1 : s->y;
}

/* a visible slot the loader found absent or stale, and the server did not answer 404 for */
static int needs_fetch(void *user, const cp0_map_slot_t *s)
{
    map_t *m = (map_t *)user;
    const cp0_map_tile_key_t k = key_of(m, s->z, s->x, s->y, s->over);
    if (!is_absent(m, &k) && !ks_has(&m->stale, &k)) return 0;
    cp0_map_tile_id_t t;
    fetch_tile_of(s, &t);
    const cp0_map_tile_key_t fk = key_of(m, t.z, t.x, t.y, 0);
    return !ks_has(&m->nofetch, &fk);
}

/* an online source: the visible tiles the loader found absent or stale, centre first, to the fetch pool (never the
 * ring around the view: cp0_map_view_fetch_tiles leaves it out) */
static void push_fetch(map_t *m)
{
    m->fetch_dirty = 0;
    if (!m->online || !m->client) return;
    int nw = 0;
    if (!m->missing) {
        const int n = cp0_map_view_tiles(&m->view, 0, m->slots, CP0_MAP_VIEW_MAX_TILES);
        nw = cp0_map_view_fetch_tiles(m->slots, n, needs_fetch, m, m->want, CP0_MAP_CLIENT_TILES_MAX);
    }
    cp0_map_client_view(m->client, m->source, m->want, nw);
}

static void set_timer(map_t *m, int period)
{
    if (m->timer && m->timer_period != period) {
        lv_timer_set_period(m->timer, (uint32_t)period);
        m->timer_period = period;
    }
}

static void view_changed(map_t *m)
{
    m->view_dirty = 1;
    set_timer(m, FAST_MS);
}

/* the tiles of the view and the ring around it, not cached and not known absent, to the loader */
static void submit(map_t *m)
{
    const int n = cp0_map_view_tiles(&m->view, 1, m->slots, CP0_MAP_VIEW_MAX_TILES);
    int nr = 0;
    if (!m->missing) {
        for (int i = 0; i < n && nr < CP0_MAP_LOADER_QUEUE; i++) {
            const cp0_map_slot_t *s = &m->slots[i];
            const cp0_map_tile_key_t k = key_of(m, s->z, s->x, s->y, s->over);
            if ((cp0_map_cache_contains(m->cache, &k) && !ks_has(&m->refresh, &k)) || is_absent(m, &k)) continue;
            int dup = 0;
            for (int j = 0; j < nr && !dup; j++) dup = key_eq(&m->reqs[j].key, &k);
            if (dup) continue;
            cp0_map_req_t *r = &m->reqs[nr++];
            memset(r, 0, sizeof *r);
            r->key = k;
            r->z = s->z;
            r->x = s->x;
            r->y = s->y;
            r->over = s->over;
            r->visible = s->visible;
            if (ks_has(&m->refresh, &k)) {
                r->force = 1; /* the fetch pool stored a newer tile: decode it again over the cached one */
                ks_del(&m->refresh, &k);
            }
            r->out_px = cp0_map_view_tile_px(&m->view);
            cp0_map__copy_str(r->source, sizeof r->source, m->source, 0);
        }
    }
    cp0_map_loader_submit(m->loader, m->reqs, nr);
    push_fetch(m);
}

static void update_attribution(map_t *m)
{
    char raw[512] = "";
    if (m->source[0]) {
        cp0_map_source_info_t info;
        if (cp0_map_store_find(m->store, m->source, &info) == CP0_MAP_OK) cp0_map__copy_str(raw, sizeof raw, info.attribution, 0);
    } else {
        for (int i = 0; i < 2; i++) {
            if (!m->attr_ids[i][0]) continue;
            cp0_map_source_info_t info;
            if (cp0_map_store_find(m->store, m->attr_ids[i], &info) != CP0_MAP_OK || !info.attribution[0]) continue;
            char clean[256];
            cp0_map_attribution_text(info.attribution, clean, sizeof clean);
            if (raw[0] && strstr(raw, clean)) continue;
            if (raw[0]) strncat(raw, " | ", sizeof raw - strlen(raw) - 1);
            strncat(raw, info.attribution, sizeof raw - strlen(raw) - 1);
        }
    }
    cp0_map_attribution_text(raw, m->attribution, sizeof m->attribution);
    /* the builtin fonts have no copyright sign: write "(C)" then */
    lv_font_glyph_dsc_t g;
    if (!lv_font_get_glyph_dsc(caption_font(), &g, 0xA9, 0)) {
        char tmp[256];
        size_t o = 0;
        for (const char *p = m->attribution; *p && o + 4 < sizeof tmp; p++) {
            if ((unsigned char)p[0] == 0xc2 && (unsigned char)p[1] == 0xa9) {
                memcpy(tmp + o, "(C)", 3);
                o += 3;
                p++;
            } else {
                tmp[o++] = *p;
            }
        }
        tmp[o] = '\0';
        cp0_map__copy_str(m->attribution, sizeof m->attribution, tmp, 0);
    }
    m->attr_dirty = 1;
    m->layout_ok = 0;
}

/* cut to w px with "..." (UTF-8 safe) */
static void fit_text(const char *in, int w, char *out, size_t size)
{
    cp0_map__copy_str(out, size, in, 0);
    if (text_w(out) <= w) return;
    size_t n = strlen(out);
    while (n > 0) {
        do n--;
        while (n > 0 && ((unsigned char)out[n] & 0xc0) == 0x80);
        if (n + 4 > size) continue;
        memcpy(out + n, "...", 4);
        if (text_w(out) <= w) return;
    }
    out[0] = '\0';
}

static void ensure_layout(map_t *m)
{
    const int w = lv_obj_get_width(m->obj), h = lv_obj_get_height(m->obj);
    const int locate = m->has_fix && m->fix_fresh;
    if (m->layout_ok && m->view.w == w && m->view.h == h && (m->layout.ctl[CP0_MAP_CTL_LOCATE].w != 0) == locate) return;
    cp0_map_view_set_size(&m->view, w, h);
    cp0_map_layout_in_t in;
    memset(&in, 0, sizeof in);
    in.w = w;
    in.h = h;
    in.target = cp0_ui_tok(CP0_TOK_TARGET);
    in.space = cp0_ui_tok(CP0_TOK_SPACE_M);
    in.radius = cp0_ui_tok(CP0_TOK_RADIUS_S);
    in.text_h = lv_font_get_line_height(caption_font());
    in.attribution_w = text_w(m->attribution);
    in.badge_w = text_w("overzoom");
    in.strip_w = text_w(m->missing ? m->missing_text : m->state_text);
    in.locate = locate;
    cp0_map_layout(&in, &m->layout);
    fit_text(m->attribution, m->layout.attribution_text_w, m->attr_draw, sizeof m->attr_draw);
    m->layout_ok = 1;
}

static void invalidate_slot(map_t *m, const cp0_map_req_t *r)
{
    if (r->z != m->view.z || r->key.source != m->source_hash || r->key.variant != variant_of(m, r->over)) return;
    const int T = cp0_map_view_tile_px(&m->view);
    lv_area_t a;
    lv_obj_get_coords(m->obj, &a);
    for (int i = 0; i < CP0_MAP_VIEW_MAX_TILES; i++) {
        const cp0_map_slot_t *s = &m->slots[i];
        if (!s->visible || s->z != r->z || s->x != r->x || s->y != r->y) continue;
        lv_area_t t = {a.x1 + s->sx, a.y1 + s->sy, a.x1 + s->sx + T - 1, a.y1 + s->sy + T - 1};
        lv_obj_invalidate_area(m->obj, &t);
    }
}

static void note_archive(map_t *m, const char *id)
{
    if (m->source[0] || !id[0]) return;
    if (!strcmp(m->attr_ids[0], id) || !strcmp(m->attr_ids[1], id)) return;
    if (!m->attr_ids[0][0]) cp0_map__copy_str(m->attr_ids[0], CP0_MAP_ID_MAX, id, 0);
    else if (!m->attr_ids[1][0]) cp0_map__copy_str(m->attr_ids[1], CP0_MAP_ID_MAX, id, 0);
    else return;
    update_attribution(m);
    lv_obj_invalidate(m->obj);
}

static void collect(map_t *m)
{
    int overflow = 0, n;
    while ((n = cp0_map_loader_poll(m->loader, m->done, (int)(sizeof m->done / sizeof m->done[0]), &overflow)) > 0) {
        for (int i = 0; i < n; i++) {
            const cp0_map_done_t *d = &m->done[i];
            if (d->result == CP0_MAP_OK) {
                note_archive(m, d->archive_id);
                invalidate_slot(m, &d->req);
                if (d->stale && m->online) {
                    ks_add(&m->stale, &d->req.key); /* shown, and revalidated while it is on screen */
                    if (d->req.visible) m->fetch_dirty = 1;
                } else {
                    ks_del(&m->stale, &d->req.key);
                }
            } else if (d->result == CP0_MAP_E_NO_TILE) {
                add_absent(m, &d->req.key);
                if (m->online && d->req.visible) m->fetch_dirty = 1;
            } else if (d->result == CP0_MAP_E_ARCHIVE_MISSING && m->source[0] && d->req.key.source == m->source_hash) {
                if (!m->missing) {
                    m->missing = 1;
                    m->missing_checked = lv_tick_get();
                    lv_obj_invalidate(m->obj);
                }
            }
            if (m->on_tile) {
                cp0_map_tile_event_t ev;
                memset(&ev, 0, sizeof ev);
                ev.z = d->req.z;
                ev.x = d->req.x;
                ev.y = d->req.y;
                ev.result = d->result;
                memcpy(ev.archive_id, d->archive_id, sizeof ev.archive_id);
                ev.kind = CP0_MAP_TILE_LOADED;
                ev.stale = d->stale;
                m->on_tile(m->on_tile_user, &ev);
            }
        }
        if (overflow) lv_obj_invalidate(m->obj);
    }
    if (overflow) lv_obj_invalidate(m->obj);
}

static const char *state_message(int code)
{
    switch (code) {
    case CP0_MAP_E_OFFLINE: return "Offline: saved tiles only";
    case CP0_MAP_E_CAP_REACHED: return "Daily tile limit reached: saved tiles only";
    case CP0_MAP_E_BLOCKED: return "Source paused by the server: saved tiles only";
    case CP0_MAP_E_BACKOFF: return "Server busy: retrying later";
    case CP0_MAP_E_NETWORK: return "No connection: saved tiles only";
    case CP0_MAP_E_NO_KEY: return "This source needs an API key";
    case CP0_MAP_E_DISABLED: return "Source switched off: saved tiles only";
    case CP0_MAP_E_CURL_MISSING: return "Network library missing (libcurl4t64)";
    case CP0_MAP_E_READ_ONLY: return "Another app updates this map: saved tiles only";
    case CP0_MAP_E_CACHE_FULL: return "Map cache full: saved tiles only";
    case CP0_MAP_E_NO_USER_AGENT: return "Online tiles off: no User-Agent";
    case CP0_MAP_E_REFUSED: return "Source refused by the tile rules";
    default: return cp0_map_strerror(code);
    }
}

/* the source's state for the strip (an online source only) */
static void set_src_state(map_t *m, int code)
{
    if (!m->online) code = CP0_MAP_OK;
    if (code == m->src_state) return;
    m->src_state = code;
    cp0_map__copy_str(m->state_text, sizeof m->state_text, code == CP0_MAP_OK ? "" : state_message(code), 0);
    m->layout_ok = 0;
    lv_obj_invalidate(m->obj);
}

static int strip_shown(const map_t *m)
{
    return m->missing || (m->online && m->src_state != CP0_MAP_OK);
}

/* a fetched tile: the slots drawn from it load again (their old image stays until the new one is in) */
static void refetched(map_t *m, const cp0_map_tile_event_t *ev)
{
    const int n = cp0_map_view_tiles(&m->view, 1, m->slots, CP0_MAP_VIEW_MAX_TILES);
    for (int i = 0; i < n; i++) {
        cp0_map_tile_id_t t;
        fetch_tile_of(&m->slots[i], &t);
        if (t.z != ev->z || t.x != ev->x || t.y != ev->y) continue;
        const cp0_map_tile_key_t k = key_of(m, m->slots[i].z, m->slots[i].x, m->slots[i].y, m->slots[i].over);
        ks_del(&m->absent, &k);
        ks_del(&m->stale, &k);
        ks_add(&m->refresh, &k);
    }
    view_changed(m);
}

static void collect_fetches(map_t *m)
{
    if (!m->client) return;
    int n;
    while ((n = cp0_map_client_poll(m->client, m->fev, (int)(sizeof m->fev / sizeof m->fev[0]))) > 0) {
        for (int i = 0; i < n; i++) {
            const cp0_map_tile_event_t *ev = &m->fev[i];
            if (ev->kind == CP0_MAP_TILE_FETCHED) {
                refetched(m, ev);
            } else if (ev->kind == CP0_MAP_TILE_FETCH_FAILED && ev->result == CP0_MAP_E_NO_TILE) {
                const cp0_map_tile_key_t fk = key_of(m, ev->z, ev->x, ev->y, 0);
                ks_add(&m->nofetch, &fk); /* not on the server: not asked again by this view */
            } else if (ev->kind == CP0_MAP_TILE_REFUSED) {
                set_src_state(m, ev->result);
            }
            if (m->on_tile) m->on_tile(m->on_tile_user, ev);
        }
    }
}

static void timer_cb(lv_timer_t *t)
{
    map_t *m = (map_t *)lv_timer_get_user_data(t);
    collect(m);
    collect_fetches(m);
    if (m->view_dirty) {
        m->view_dirty = 0;
        ensure_layout(m);
        submit(m);
        lv_obj_invalidate(m->obj);
    } else if (m->fetch_dirty) {
        push_fetch(m);
    }
    if ((m->missing || m->online) && lv_tick_elaps(m->missing_checked) >= MISSING_CHECK_MS) {
        m->missing_checked = lv_tick_get();
        cp0_map_source_info_t info;
        if (cp0_map_store_find(m->store, m->source, &info) == CP0_MAP_OK) {
            if (m->missing && info.state == CP0_MAP_OK) {
                m->missing = 0;
                ks_clear(&m->absent);
                view_changed(m);
            }
            if (m->online) {
                const int was = m->src_state;
                set_src_state(m, info.state);
                if (was != CP0_MAP_OK && info.state == CP0_MAP_OK) {
                    ks_clear(&m->nofetch);
                    m->fetch_dirty = 1;
                }
            }
        }
    }
    if (m->has_fix) {
        const int fresh = cp0_map_fix_fresh(&m->fix, cp0_map_monotonic_ms());
        if (fresh != m->fix_fresh) {
            m->fix_fresh = fresh;
            m->layout_ok = 0;
            lv_obj_invalidate(m->obj);
        }
    }
    const int fetching = m->client && cp0_map_client_pending(m->client) > 0;
    set_timer(m, (m->view_dirty || fetching || !cp0_map_loader_idle(m->loader)) ? FAST_MS : SLOW_MS); /* a missing
                                                                       archive, a source's state: checked every 2 s */
}

/* ------------------------------------------------------------------------------------------------------ drawing */

static void fill(lv_layer_t *layer, const lv_area_t *a, uint32_t c, lv_opa_t opa, int radius)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = rgb(c);
    d.bg_opa = opa;
    d.radius = radius;
    lv_draw_rect(layer, &d, a);
}

static void ring(lv_layer_t *layer, const lv_area_t *a, uint32_t c, lv_opa_t opa, int width, int radius)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = rgb(c);
    d.border_opa = opa;
    d.border_width = width;
    d.radius = radius;
    lv_draw_rect(layer, &d, a);
}

static void line(lv_layer_t *layer, double x0, double y0, double x1, double y1, uint32_t c, lv_opa_t opa, int width)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = (lv_value_precise_t)x0;
    d.p1.y = (lv_value_precise_t)y0;
    d.p2.x = (lv_value_precise_t)x1;
    d.p2.y = (lv_value_precise_t)y1;
    d.color = rgb(c);
    d.opa = opa;
    d.width = width;
    d.round_start = d.round_end = 1;
    lv_draw_line(layer, &d);
}

static void text(lv_layer_t *layer, const char *s, int x, int y, int w, uint32_t c)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = s;
    d.text_local = 1;
    d.font = caption_font();
    d.color = rgb(c);
    const lv_area_t a = {x, y, x + w - 1, y + lv_font_get_line_height(d.font) - 1};
    lv_draw_label(layer, &d, &a);
}

static lv_area_t area_of(const lv_area_t *o, const cp0_map_rect_t *r)
{
    lv_area_t a = {o->x1 + r->x, o->y1 + r->y, o->x1 + r->x + r->w - 1, o->y1 + r->y + r->h - 1};
    return a;
}

static void pin(map_t *m, const cp0_map_image_t *img)
{
    if (m->npins < MAX_PINS) m->pins[m->npins++] = img;
    else cp0_map_cache_release(m->cache, img);
}

static void release_prev(map_t *m)
{
    for (int i = 0; i < m->nprev; i++) cp0_map_cache_release(m->cache, m->prev_pins[i]);
    memcpy(m->prev_pins, m->pins, (size_t)m->npins * sizeof m->pins[0]);
    m->nprev = m->npins;
    m->npins = 0;
}

static void draw_image(map_t *m, lv_layer_t *layer, const cp0_map_image_t *img, const uint8_t *px, int w, int h,
                       int x, int y, int scale256)
{
    if (m->npins > MAX_PINS) return;
    lv_image_dsc_t *d = &m->imgs[m->npins ? m->npins - 1 : 0];
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = (uint32_t)w;
    d->header.h = (uint32_t)h;
    d->header.stride = (uint32_t)img->stride;
    d->data = px;
    d->data_size = (uint32_t)((size_t)img->stride * (size_t)h);
    lv_draw_image_dsc_t dd;
    lv_draw_image_dsc_init(&dd);
    dd.src = d;
    if (scale256 != 256) {
        dd.scale_x = dd.scale_y = scale256;
        dd.pivot.x = 0;
        dd.pivot.y = 0;
    }
    const lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_image(layer, &dd, &a);
}

static void draw_grid_tile(lv_layer_t *layer, int x, int y, int T)
{
    const lv_area_t a = {x, y, x + T - 1, y + T - 1};
    fill(layer, &a, C_PLATE, LV_OPA_COVER, 0);
    ring(layer, &a, C_GRID, LV_OPA_COVER, 1, 0);
    const int q = T / 4;
    for (int k = 1; k < 4; k++) {
        line(layer, x + k * q, y + 1, x + k * q, y + T - 2, C_GRID, LV_OPA_50, 1);
        line(layer, x + 1, y + k * q, x + T - 2, y + k * q, C_GRID, LV_OPA_50, 1);
    }
}

static void draw_tiles(map_t *m, lv_layer_t *layer, const lv_area_t *o)
{
    const int n = cp0_map_view_tiles(&m->view, 0, m->slots, CP0_MAP_VIEW_MAX_TILES);
    const int T = cp0_map_view_tile_px(&m->view);
    for (int i = 0; i < n; i++) {
        const cp0_map_slot_t *s = &m->slots[i];
        const int x = o->x1 + s->sx, y = o->y1 + s->sy;
        if (m->missing) {
            draw_grid_tile(layer, x, y, T);
            continue;
        }
        const cp0_map_tile_key_t k = key_of(m, s->z, s->x, s->y, s->over);
        const cp0_map_image_t *img = m->npins < MAX_PINS ? cp0_map_cache_get(m->cache, &k) : NULL;
        if (img) {
            pin(m, img);
            draw_image(m, layer, img, img->px, img->w, img->h, x, y, 256);
            continue;
        }
        /* the parent, magnified: the quadrant of (z - 1, x / 2, y / 2) at twice its size */
        if (s->z > m->view.zmin && m->npins < MAX_PINS) {
            const cp0_map_tile_key_t pk = key_of(m, s->z - 1, s->x >> 1, s->y >> 1, s->z - 1 > m->view.data_max);
            const cp0_map_image_t *p = cp0_map_cache_get(m->cache, &pk);
            if (p && p->w == T && p->h == T) {
                pin(m, p);
                const int hw = T / 2;
                const uint8_t *q = p->px + (size_t)((s->y & 1) ? hw : 0) * (size_t)p->stride + (size_t)((s->x & 1) ? hw : 0) * 2;
                draw_image(m, layer, p, q, hw, hw, x, y, 512);
                continue;
            }
            if (p) cp0_map_cache_release(m->cache, p);
        }
        draw_grid_tile(layer, x, y, T);
    }
}

static void draw_track(map_t *m, cp0_map_layer_t *l, lv_layer_t *layer, const lv_area_t *o, int width)
{
    if (l->tn < 2) return;
    if (l->simp_dirty || l->simp_z != m->view.z || l->simp_scale != m->view.scale_x2) {
        const double eps = 0.75 / (ldexp(1.0, m->view.z) * m->view.scale_x2 / 2.0);
        l->kept = cp0_map_simplify(l->tx, l->ty, l->tn, eps, l->keep, l->mark, l->stack);
        l->simp_dirty = 0;
        l->simp_z = m->view.z;
        l->simp_scale = m->view.scale_x2;
    }
    const double world_screen = ldexp(256.0, m->view.z) * m->view.scale_x2 / 2.0;
    double px = 0, py = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < l->kept; i++) {
            double sx, sy;
            cp0_map_view_world0_to_screen(&m->view, l->tx[l->keep[i]], l->ty[l->keep[i]], &sx, &sy);
            if (i > 0) {
                const int out = (px < 0 && sx < 0) || (py < 0 && sy < 0) || (px > m->view.w && sx > m->view.w) ||
                                (py > m->view.h && sy > m->view.h);
                if (!out && fabs(sx - px) < world_screen / 2) {
                    if (pass == 0) line(layer, o->x1 + px, o->y1 + py, o->x1 + sx, o->y1 + sy, C_SHADOW, LV_OPA_50, width + 2);
                    else line(layer, o->x1 + px, o->y1 + py, o->x1 + sx, o->y1 + sy, C_BAD, LV_OPA_COVER, width);
                }
            }
            px = sx;
            py = sy;
        }
}

static void draw_glyph(lv_layer_t *layer, cp0_map_marker_kind_t kind, int cx, int cy, int r, int selected)
{
    const uint32_t colours[CP0_MAP_MARKER__COUNT] = {C_MUTED, C_ACCENT, C_OK, C_WARN, C_BAD};
    const uint32_t c = colours[kind < CP0_MAP_MARKER__COUNT ? kind : 0];
    const int rr = selected ? r + r / 2 : r;
    lv_draw_triangle_dsc_t t;
    switch (kind) {
    case CP0_MAP_MARKER_REPEATER: { /* a diamond: two triangles over a dark one */
        for (int pass = 0; pass < 2; pass++) {
            const int e = rr + (pass ? 0 : 2);
            for (int half = 0; half < 2; half++) {
                lv_draw_triangle_dsc_init(&t);
                t.color = rgb(pass ? c : C_SHADOW);
                t.opa = pass ? LV_OPA_COVER : LV_OPA_70;
                t.p[0].x = cx - e;
                t.p[0].y = cy;
                t.p[1].x = cx + e;
                t.p[1].y = cy;
                t.p[2].x = cx;
                t.p[2].y = half ? cy + e : cy - e;
                lv_draw_triangle(layer, &t);
            }
        }
        break;
    }
    case CP0_MAP_MARKER_ROOM: {
        const lv_area_t a = {cx - rr, cy - rr, cx + rr, cy + rr};
        fill(layer, &a, c, LV_OPA_COVER, 2);
        ring(layer, &a, C_SHADOW, LV_OPA_70, 2, 2);
        break;
    }
    case CP0_MAP_MARKER_WAYPOINT: { /* a pin: a triangle standing on its point, a dot on top */
        for (int pass = 0; pass < 2; pass++) {
            const int e = pass ? 0 : 2;
            lv_draw_triangle_dsc_init(&t);
            t.color = rgb(pass ? c : C_SHADOW);
            t.opa = pass ? LV_OPA_COVER : LV_OPA_70;
            t.p[0].x = cx - rr - e;
            t.p[0].y = cy - 2 * rr - e;
            t.p[1].x = cx + rr + e;
            t.p[1].y = cy - 2 * rr - e;
            t.p[2].x = cx;
            t.p[2].y = cy + e;
            lv_draw_triangle(layer, &t);
        }
        const lv_area_t d = {cx - rr / 2, cy - 2 * rr + 1, cx + rr / 2, cy - 2 * rr + 1 + rr};
        fill(layer, &d, C_TEXT, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        break;
    }
    default: {
        const lv_area_t a = {cx - rr, cy - rr, cx + rr, cy + rr};
        fill(layer, &a, c, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        ring(layer, &a, kind == CP0_MAP_MARKER_GENERIC ? C_TEXT : C_SHADOW, LV_OPA_80, 2, LV_RADIUS_CIRCLE);
        break;
    }
    }
}

static void draw_markers(map_t *m, cp0_map_layer_t *l, lv_layer_t *layer, const lv_area_t *o, int r, int *nlab)
{
    const int show_labels = m->view.z >= l->label_zoom;
    const int lh = lv_font_get_line_height(caption_font()) + 2;
    /* the selected marker first, so its label gets the best spot */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < l->n; i++) {
            const int sel = i == l->selected;
            if ((pass == 0) != sel) continue;
            double sx, sy;
            if (!cp0_map_view_visible(&m->view, l->mx[i], l->my[i], 3 * r, &sx, &sy)) continue;
            draw_glyph(layer, l->m[i].kind, o->x1 + (int)lround(sx), o->y1 + (int)lround(sy), r, sel);
            if ((show_labels || sel) && l->m[i].label[0] && *nlab < LABEL_CAND) {
                const int k = (*nlab)++;
                m->lsx[k] = (int)lround(sx);
                m->lsy[k] = (int)lround(sy);
                m->llw[k] = text_w(l->m[i].label) + 4;
                m->llh[k] = lh;
                m->ltext[k] = l->m[i].label;
            }
        }
}

static void draw_position(map_t *m, lv_layer_t *layer, const lv_area_t *o, int r)
{
    if (!m->has_fix || !m->fix_fresh) return;
    double sx, sy;
    cp0_map_view_latlon_to_screen(&m->view, m->fix.lat, m->fix.lon, &sx, &sy);
    const int x = o->x1 + (int)lround(sx), y = o->y1 + (int)lround(sy);
    if (m->fix.acc_m > 0) {
        double ar = m->fix.acc_m / cp0_map_view_metres_per_px(&m->view);
        const double cap = 2.0 * (m->view.w > m->view.h ? m->view.w : m->view.h);
        if (ar > cap) ar = cap;
        if (ar > r) {
            const int ir = (int)lround(ar);
            const lv_area_t a = {x - ir, y - ir, x + ir, y + ir};
            fill(layer, &a, C_ACCENT, LV_OPA_20, LV_RADIUS_CIRCLE);
            ring(layer, &a, C_ACCENT, LV_OPA_60, 1, LV_RADIUS_CIRCLE);
        }
    }
    if (m->fix.speed_mps > CP0_MAP_HEADING_MIN_MPS && m->fix.course_deg >= 0) {
        const double a = m->fix.course_deg * M_PI / 180.0, ux = sin(a), uy = -cos(a);
        lv_draw_triangle_dsc_t t;
        for (int pass = 0; pass < 2; pass++) {
            const double e = pass ? 0 : 1.5;
            lv_draw_triangle_dsc_init(&t);
            t.color = rgb(pass ? C_ACCENT : C_TEXT);
            t.opa = LV_OPA_COVER;
            const double tip = 2.6 * r + e, base = 1.2 * r, half = 0.9 * r + e;
            t.p[0].x = (lv_value_precise_t)(x + ux * tip);
            t.p[0].y = (lv_value_precise_t)(y + uy * tip);
            t.p[1].x = (lv_value_precise_t)(x + ux * base - uy * half);
            t.p[1].y = (lv_value_precise_t)(y + uy * base + ux * half);
            t.p[2].x = (lv_value_precise_t)(x + ux * base + uy * half);
            t.p[2].y = (lv_value_precise_t)(y + uy * base - ux * half);
            lv_draw_triangle(layer, &t);
        }
    }
    const lv_area_t d = {x - r, y - r, x + r, y + r};
    fill(layer, &d, C_ACCENT, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    ring(layer, &d, C_TEXT, LV_OPA_COVER, r / 3 > 2 ? r / 3 : 2, LV_RADIUS_CIRCLE);
}

static int can_zoom(const map_t *m, int dz)
{
    return dz > 0 ? m->view.z < m->view.zmax : m->view.z > m->view.zmin;
}

static void draw_button(map_t *m, lv_layer_t *layer, const lv_area_t *o, cp0_map_ctl_t c)
{
    const cp0_map_rect_t *r = &m->layout.plates[c];
    if (!r->w) return;
    const int p = m->layout.plate, cx = o->x1 + r->x + p / 2, cy = o->y1 + r->y + p / 2;
    const lv_area_t a = area_of(o, r);
    const int pressed = m->pressing && m->press_ctl == c;
    fill(layer, &a, pressed ? C_ACCENT : C_PLATE, LV_OPA_80, cp0_ui_tok(CP0_TOK_RADIUS_S));
    ring(layer, &a, C_TEXT, LV_OPA_30, 1, cp0_ui_tok(CP0_TOK_RADIUS_S));
    const int stroke = p / 12 > 2 ? p / 12 : 2, len = p * 2 / 5;
    if (c == CP0_MAP_CTL_LOCATE) {
        const int rr = p / 4;
        const lv_area_t ca = {cx - rr, cy - rr, cx + rr, cy + rr};
        ring(layer, &ca, C_TEXT, LV_OPA_COVER, stroke, LV_RADIUS_CIRCLE);
        line(layer, cx - len, cy, cx - rr, cy, C_TEXT, LV_OPA_COVER, stroke);
        line(layer, cx + rr, cy, cx + len, cy, C_TEXT, LV_OPA_COVER, stroke);
        line(layer, cx, cy - len, cx, cy - rr, C_TEXT, LV_OPA_COVER, stroke);
        line(layer, cx, cy + rr, cx, cy + len, C_TEXT, LV_OPA_COVER, stroke);
        return;
    }
    const uint32_t gc = can_zoom(m, c == CP0_MAP_CTL_ZOOM_IN ? 1 : -1) ? C_TEXT : C_MUTED;
    line(layer, cx - len / 2, cy, cx + len / 2, cy, gc, LV_OPA_COVER, stroke);
    if (c == CP0_MAP_CTL_ZOOM_IN) line(layer, cx, cy - len / 2, cx, cy + len / 2, gc, LV_OPA_COVER, stroke);
}

static void draw_controls(map_t *m, lv_layer_t *layer, const lv_area_t *o)
{
    const cp0_map_layout_t *L = &m->layout;
    const int radius = cp0_ui_tok(CP0_TOK_RADIUS_S), th = lv_font_get_line_height(caption_font());
    /* scale bar */
    cp0_map_scalebar_t sb;
    if (cp0_map_scalebar(cp0_map_view_metres_per_px(&m->view), L->scalebar_max_px, m->units, &sb) == CP0_MAP_OK) {
        cp0_map__copy_str(m->scalebar_label, sizeof m->scalebar_label, sb.label, 0);
        cp0_map_rect_t pr = L->scalebar;
        const int need = text_w(sb.label);
        const int pw = (sb.px > need ? sb.px : need) + 2 * L->pad;
        if (pw < pr.w) pr.w = pw;
        const lv_area_t a = area_of(o, &pr);
        fill(layer, &a, C_PLATE, LV_OPA_80, radius);
        text(layer, m->scalebar_label, a.x1 + L->pad, a.y1 + L->pad, pr.w - L->pad, C_TEXT);
        const int bar_h = th / 4 > 3 ? th / 4 : 3, by = a.y2 - L->pad - bar_h + 1, bx = a.x1 + L->pad;
        const lv_area_t bar = {bx, by, bx + sb.px - 1, by + bar_h - 1};
        fill(layer, &bar, C_TEXT, LV_OPA_COVER, 0);
        const lv_area_t t0 = {bx, by - bar_h, bx + 1, by + bar_h - 1}, t1 = {bx + sb.px - 2, by - bar_h, bx + sb.px - 1, by + bar_h - 1};
        fill(layer, &t0, C_TEXT, LV_OPA_COVER, 0);
        fill(layer, &t1, C_TEXT, LV_OPA_COVER, 0);
    }
    /* attribution */
    if (L->attribution.w && m->attr_draw[0]) {
        const lv_area_t a = area_of(o, &L->attribution);
        fill(layer, &a, C_PLATE, LV_OPA_80, radius);
        const int tw = text_w(m->attr_draw);
        text(layer, m->attr_draw, a.x2 - L->pad - tw + 1, a.y1 + L->pad, tw + 1, C_TEXT);
    }
    if (strip_shown(m)) {
        /* the missing archive (red), or why an online source shows only saved tiles (amber), in the same strip */
        const lv_area_t a = area_of(o, &L->strip);
        fill(layer, &a, C_PLATE, LV_OPA_90, radius);
        char cut[160];
        fit_text(m->missing ? m->missing_text : m->state_text, L->strip.w - 2 * L->pad, cut, sizeof cut);
        text(layer, cut, a.x1 + L->pad, a.y1 + L->pad, L->strip.w - 2 * L->pad, m->missing ? C_BAD : C_WARN);
    } else if (cp0_map_view_overzoom(&m->view)) {
        const lv_area_t a = area_of(o, &L->badge);
        fill(layer, &a, C_PLATE, LV_OPA_80, radius);
        text(layer, "overzoom", a.x1 + L->pad, a.y1 + L->pad, L->badge.w - L->pad, C_WARN);
    }
    draw_button(m, layer, o, CP0_MAP_CTL_LOCATE);
    draw_button(m, layer, o, CP0_MAP_CTL_ZOOM_IN);
    draw_button(m, layer, o, CP0_MAP_CTL_ZOOM_OUT);
}

static void draw(map_t *m, lv_layer_t *layer)
{
    ensure_layout(m);
    lv_area_t o;
    lv_obj_get_coords(m->obj, &o);
    fill(layer, &o, C_PLATE, LV_OPA_COVER, 0);
    draw_tiles(m, layer, &o);
    const int target = cp0_ui_tok(CP0_TOK_TARGET);
    const int width = target / 14 > 2 ? target / 14 : 2, r = target / 9 > 4 ? target / 9 : 4;
    int nlab = 0;
    for (int i = 0; i < m->nlayers; i++) {
        cp0_map_layer_t *l = m->layers[i];
        if (!l->shown) continue;
        if (l->kind == LAYER_TRACK) draw_track(m, l, layer, &o, width);
    }
    for (int i = 0; i < m->nlayers; i++) {
        cp0_map_layer_t *l = m->layers[i];
        if (l->shown && l->kind == LAYER_MARKERS) draw_markers(m, l, layer, &o, r, &nlab);
    }
    draw_position(m, layer, &o, r);
    if (nlab) {
        const cp0_map_layout_t *L = &m->layout;
        const cp0_map_rect_t avoid[7] = {L->plates[1], L->plates[2], L->plates[3], L->scalebar, L->attribution,
                                         cp0_map_view_overzoom(&m->view) ? L->badge : (cp0_map_rect_t){0, 0, 0, 0},
                                         strip_shown(m) ? L->strip : (cp0_map_rect_t){0, 0, 0, 0}};
        cp0_map_labels_place(m->lsx, m->lsy, m->llw, m->llh, nlab, r + r / 2, m->view.w, m->view.h, avoid, 7, m->llx,
                             m->lly);
        for (int k = 0; k < nlab; k++) {
            if (m->llx[k] < 0) continue;
            const lv_area_t a = {o.x1 + m->llx[k], o.y1 + m->lly[k], o.x1 + m->llx[k] + m->llw[k] - 1,
                                 o.y1 + m->lly[k] + m->llh[k] - 1};
            fill(layer, &a, C_PLATE, LV_OPA_80, 2);
            text(layer, m->ltext[k], a.x1 + 2, a.y1 + 1, m->llw[k], C_TEXT);
        }
    }
    draw_controls(m, layer, &o);
    if (lv_obj_has_state(m->obj, LV_STATE_FOCUS_KEY)) ring(layer, &o, C_ACCENT, LV_OPA_COVER, 2, 0);
    release_prev(m);
}

/* -------------------------------------------------------------------------------------------------------- input */

static void zoom_by(map_t *m, int dz, double sx, double sy)
{
    const int before = m->view.z;
    cp0_map_view_zoom_at(&m->view, dz, sx, sy);
    if (m->view.z != before) view_changed(m);
}

static void act(map_t *m, cp0_map_ctl_t c)
{
    if (c == CP0_MAP_CTL_ZOOM_IN) zoom_by(m, 1, m->view.w / 2.0, m->view.h / 2.0);
    else if (c == CP0_MAP_CTL_ZOOM_OUT) zoom_by(m, -1, m->view.w / 2.0, m->view.h / 2.0);
    else if (c == CP0_MAP_CTL_LOCATE) cp0_map_center_on_position(m->obj);
}

static void local_point(map_t *m, lv_point_t *p)
{
    lv_indev_t *indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, p);
    lv_area_t a;
    lv_obj_get_coords(m->obj, &a);
    p->x -= a.x1;
    p->y -= a.y1;
}

static int slop(void)
{
    const int t = cp0_ui_tok(CP0_TOK_TARGET) / 6;
    return t > 6 ? t : 6;
}

static void on_input(map_t *m, lv_event_code_t code)
{
    lv_point_t p = {0, 0};
    local_point(m, &p);
    if (code == LV_EVENT_PRESSED) {
        ensure_layout(m);
        m->pressing = 1;
        m->moved = 0;
        m->press = m->last = p;
        m->press_ctl = cp0_map_layout_hit(&m->layout, p.x, p.y);
        if (m->press_ctl) lv_obj_invalidate(m->obj);
    } else if (code == LV_EVENT_PRESSING && m->pressing) {
        if (m->press_ctl) return;
        if (!m->moved && (abs(p.x - m->press.x) > slop() || abs(p.y - m->press.y) > slop())) {
            m->moved = 1;
            m->last = m->press;
        }
        if (m->moved && (p.x != m->last.x || p.y != m->last.y)) {
            cp0_map_view_pan(&m->view, p.x - m->last.x, p.y - m->last.y);
            m->last = p;
            view_changed(m);
        }
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && m->pressing) {
        m->pressing = 0;
        const cp0_map_ctl_t c = m->press_ctl;
        m->press_ctl = CP0_MAP_CTL_NONE;
        if (c) {
            if (code == LV_EVENT_RELEASED && cp0_map_layout_hit(&m->layout, p.x, p.y) == c) act(m, c);
            lv_obj_invalidate(m->obj);
            return;
        }
        if (m->moved || code != LV_EVENT_RELEASED) return;
        const uint32_t now = lv_tick_get();
        if (m->last_tap_ms && now - m->last_tap_ms <= DOUBLE_TAP_MS && abs(p.x - m->last_tap.x) <= 2 * slop() &&
            abs(p.y - m->last_tap.y) <= 2 * slop()) {
            m->last_tap_ms = 0;
            zoom_by(m, 1, p.x, p.y);
        } else {
            m->last_tap_ms = now ? now : 1;
            m->last_tap = p;
        }
    }
}

int cp0_map_handle_key(lv_obj_t *obj, uint32_t key, uint32_t mods)
{
    map_t *m = map_of(obj);
    if (!m) return 0;
    const int big = (mods & KBD_MOD_SHIFT) != 0;
    const double sx = m->view.w / (big ? 2.0 : 8.0), sy = m->view.h / (big ? 2.0 : 8.0);
    switch (key) {
    case KEY_UP: cp0_map_view_pan(&m->view, 0, sy); break;
    case KEY_DOWN: cp0_map_view_pan(&m->view, 0, -sy); break;
    case KEY_LEFT: cp0_map_view_pan(&m->view, sx, 0); break;
    case KEY_RIGHT: cp0_map_view_pan(&m->view, -sx, 0); break;
    case KEY_EQUAL:
    case KEY_KPPLUS:
    case KEY_PAGEUP: zoom_by(m, 1, m->view.w / 2.0, m->view.h / 2.0); return 1;
    case KEY_MINUS:
    case KEY_KPMINUS:
    case KEY_PAGEDOWN: zoom_by(m, -1, m->view.w / 2.0, m->view.h / 2.0); return 1;
    case KEY_HOME: return cp0_map_center_on_position(obj) == CP0_MAP_OK;
    default: return 0;
    }
    view_changed(m);
    return 1;
}

static void keyboard_cb(lv_event_t *e)
{
    map_t *m = (map_t *)lv_event_get_user_data(e);
    const struct key_item *item = (const struct key_item *)lv_event_get_param(e);
    if (!m || !item || item->key_state == KBD_KEY_RELEASED) return;
    if (!lv_obj_has_state(m->obj, LV_STATE_FOCUSED)) return;
    const uint32_t key = item->semantic_key ? item->semantic_key : item->key_code;
    cp0_map_handle_key(m->obj, key, item->mods);
}

/* ------------------------------------------------------------------------------------------------------- object */

static void free_layer(cp0_map_layer_t *l)
{
    free(l->m);
    free(l->mx);
    free(l->my);
    free(l->tx);
    free(l->ty);
    free(l->keep);
    free(l->stack);
    free(l->mark);
    free(l);
}

static void event_cb(lv_event_t *e)
{
    map_t *m = (map_t *)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_DRAW_MAIN: draw(m, lv_event_get_layer(e)); break;
    case LV_EVENT_PRESSED:
    case LV_EVENT_PRESSING:
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST: on_input(m, code); break;
    case LV_EVENT_SIZE_CHANGED:
        m->layout_ok = 0;
        ensure_layout(m);
        view_changed(m);
        break;
    case LV_EVENT_FOCUSED:
    case LV_EVENT_DEFOCUSED: lv_obj_invalidate(m->obj); break;
    case LV_EVENT_DELETE: {
        if (m->timer) lv_timer_delete(m->timer);
        if (m->screen && LV_EVENT_KEYBOARD) lv_obj_remove_event_cb_with_user_data(m->screen, keyboard_cb, m);
        cp0_map_client_free(m->client);
        cp0_map_loader_free(m->loader);
        cp0_map_store_fetch_done(&m->fetch);
        for (int i = 0; i < m->npins; i++) cp0_map_cache_release(m->cache, m->pins[i]);
        for (int i = 0; i < m->nprev; i++) cp0_map_cache_release(m->cache, m->prev_pins[i]);
        for (int i = 0; i < m->nlayers; i++) free_layer(m->layers[i]);
        m->magic = 0;
        lv_obj_set_user_data(m->obj, NULL);
        free(m);
        break;
    }
    default: break;
    }
}

lv_obj_t *cp0_map_create(lv_obj_t *parent, cp0_map_store_t *store)
{
    if (!store) return NULL;
    map_t *m = (map_t *)calloc(1, sizeof *m);
    if (!m) return NULL;
    m->magic = MAP_MAGIC;
    m->store = store;
    m->cache = cp0_map_store_ram_cache(store);
    m->fetch.store = store;
    m->loader = cp0_map_loader_new(m->cache, cp0_map_store_fetch, &m->fetch);
    if (!m->loader) {
        free(m);
        return NULL;
    }
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER |
                                LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    m->obj = obj;
    lv_obj_set_user_data(obj, m);
    lv_obj_add_event_cb(obj, event_cb, LV_EVENT_ALL, m);
    m->screen = lv_obj_get_screen(obj);
    if (m->screen && LV_EVENT_KEYBOARD)
        lv_obj_add_event_cb(m->screen, keyboard_cb, (lv_event_code_t)LV_EVENT_KEYBOARD, m);
    cp0_map_view_init(&m->view, 1, 1);
    m->scale_req = 0.0f;
    cp0_map_view_set_scale(&m->view, cp0_map_scale_x2(cp0_map_choose_scale(cp0_ui_metrics_get()->ppmm_x100)));
    cp0_map_set_source(obj, NULL);
    m->timer = lv_timer_create(timer_cb, FAST_MS, m);
    m->timer_period = FAST_MS;
    view_changed(m);
    return obj;
}

int cp0_map_set_source(lv_obj_t *obj, const char *id)
{
    map_t *m = map_of(obj);
    if (!m) return CP0_MAP_E_INVALID;
    int dmin = 0, dmax = 18;
    char missing_title[128] = "";
    int missing = 0, online = 0, state = CP0_MAP_OK;
    if (id) {
        cp0_map_source_info_t info;
        if (cp0_map_store_find(m->store, id, &info) != CP0_MAP_OK) return CP0_MAP_E_NOT_FOUND;
        if (info.minzoom >= 0) dmin = info.minzoom;
        if (info.maxzoom >= 0) dmax = info.maxzoom;
        missing = info.state == CP0_MAP_E_ARCHIVE_MISSING;
        online = info.kind == CP0_MAP_ENTRY_SOURCE;
        state = online ? info.state : CP0_MAP_OK;
        cp0_map__copy_str(missing_title, sizeof missing_title, info.title, 0);
    } else {
        int n = cp0_map_store_sources(m->store, NULL, 0);
        cp0_map_source_info_t *all = n > 0 ? (cp0_map_source_info_t *)calloc((size_t)n, sizeof *all) : NULL;
        if (all) {
            n = cp0_map_store_sources(m->store, all, n);
            int lo = 99, hi = -1;
            for (int i = 0; i < n; i++) {
                if (all[i].kind != CP0_MAP_ENTRY_ARCHIVE) continue;
                if (all[i].minzoom >= 0 && all[i].minzoom < lo) lo = all[i].minzoom;
                if (all[i].maxzoom > hi) hi = all[i].maxzoom;
            }
            if (hi >= 0) {
                dmin = lo <= hi ? lo : 0;
                dmax = hi;
            }
            free(all);
        }
    }
    cp0_map__copy_str(m->source, sizeof m->source, id ? id : "", 0);
    m->source_hash = cp0_map_source_hash(id ? id : "*");
    m->missing = missing;
    m->missing_checked = lv_tick_get();
    snprintf(m->missing_text, sizeof m->missing_text, "Map archive missing: %s", missing_title[0] ? missing_title : m->source);
    ks_clear(&m->absent);
    ks_clear(&m->stale);
    ks_clear(&m->nofetch);
    ks_clear(&m->refresh);
    if (m->client) cp0_map_client_view(m->client, NULL, NULL, 0); /* the old source's wishes go */
    m->online = online;
    if (online && !m->client && cp0_map_client_new(m->store, &m->client) != CP0_MAP_OK) m->client = NULL;
    m->src_state = CP0_MAP_OK;
    m->state_text[0] = '\0';
    set_src_state(m, state);
    m->attr_ids[0][0] = m->attr_ids[1][0] = '\0';
    cp0_map_view_set_limits(&m->view, dmin, dmax, 1);
    update_attribution(m);
    view_changed(m);
    lv_obj_invalidate(obj);
    return CP0_MAP_OK;
}

void cp0_map_set_center(lv_obj_t *obj, double lat, double lon)
{
    map_t *m = map_of(obj);
    if (!m) return;
    cp0_map_view_set_center(&m->view, lat, lon);
    view_changed(m);
}

void cp0_map_get_center(lv_obj_t *obj, double *lat, double *lon)
{
    map_t *m = map_of(obj);
    if (!m) return;
    cp0_map_view_center(&m->view, lat, lon);
}

void cp0_map_set_zoom(lv_obj_t *obj, int zoom)
{
    map_t *m = map_of(obj);
    if (!m) return;
    cp0_map_view_set_zoom(&m->view, zoom);
    view_changed(m);
}

int cp0_map_get_zoom(lv_obj_t *obj)
{
    map_t *m = map_of(obj);
    return m ? m->view.z : -1;
}

void cp0_map_get_zoom_range(lv_obj_t *obj, int *zmin, int *zmax)
{
    map_t *m = map_of(obj);
    if (zmin) *zmin = m ? m->view.zmin : 0;
    if (zmax) *zmax = m ? m->view.zmax : 0;
}

void cp0_map_set_scale(lv_obj_t *obj, float scale)
{
    map_t *m = map_of(obj);
    if (!m) return;
    m->scale_req = scale;
    cp0_map_view_set_scale(&m->view, cp0_map_scale_x2(cp0_map_resolve_scale(scale, cp0_ui_metrics_get()->ppmm_x100)));
    view_changed(m);
}

float cp0_map_get_scale(lv_obj_t *obj)
{
    map_t *m = map_of(obj);
    return m ? (float)m->view.scale_x2 / 2.0f : 0.0f;
}

void cp0_map_pan_px(lv_obj_t *obj, int dx, int dy)
{
    map_t *m = map_of(obj);
    if (!m) return;
    cp0_map_view_pan(&m->view, dx, dy);
    view_changed(m);
}

void cp0_map_set_units(lv_obj_t *obj, cp0_map_units_t units)
{
    map_t *m = map_of(obj);
    if (!m) return;
    m->units = units;
    lv_obj_invalidate(obj);
}

void cp0_map_set_position(lv_obj_t *obj, const cp0_map_fix_t *fix)
{
    map_t *m = map_of(obj);
    if (!m) return;
    m->has_fix = fix != NULL;
    if (fix) m->fix = *fix;
    m->fix_fresh = m->has_fix && cp0_map_fix_fresh(&m->fix, cp0_map_monotonic_ms());
    m->layout_ok = 0;
    lv_obj_invalidate(obj);
}

int cp0_map_center_on_position(lv_obj_t *obj)
{
    map_t *m = map_of(obj);
    if (!m || !m->has_fix || !cp0_map_fix_fresh(&m->fix, cp0_map_monotonic_ms())) return CP0_MAP_E_NOT_FOUND;
    cp0_map_view_set_center(&m->view, m->fix.lat, m->fix.lon);
    view_changed(m);
    return CP0_MAP_OK;
}

static cp0_map_layer_t *add_layer(lv_obj_t *obj, const char *name, int kind)
{
    map_t *m = map_of(obj);
    if (!m || m->nlayers >= CP0_MAP_LAYERS_MAX) return NULL;
    cp0_map_layer_t *l = (cp0_map_layer_t *)calloc(1, sizeof *l);
    if (!l) return NULL;
    l->map = m;
    l->kind = kind;
    l->shown = 1;
    l->selected = -1;
    l->label_zoom = CP0_MAP_LABEL_ZOOM;
    l->simp_dirty = 1;
    cp0_map__copy_str(l->name, sizeof l->name, name, 1);
    m->layers[m->nlayers++] = l;
    return l;
}

cp0_map_layer_t *cp0_map_add_markers(lv_obj_t *obj, const char *name)
{
    return add_layer(obj, name, LAYER_MARKERS);
}

cp0_map_layer_t *cp0_map_add_track(lv_obj_t *obj, const char *name)
{
    return add_layer(obj, name, LAYER_TRACK);
}

int cp0_map_markers_set(cp0_map_layer_t *l, const cp0_map_marker_t *mk, int n)
{
    if (!l || l->kind != LAYER_MARKERS || n < 0 || (n > 0 && !mk)) return CP0_MAP_E_INVALID;
    if (n > CP0_MAP_MARKERS_MAX) n = CP0_MAP_MARKERS_MAX;
    if (n > l->n || !l->m) {
        cp0_map_marker_t *nm = (cp0_map_marker_t *)realloc(l->m, (size_t)(n ? n : 1) * sizeof *nm);
        if (!nm) return CP0_MAP_E_NOMEM;
        l->m = nm;
        double *nx = (double *)realloc(l->mx, (size_t)(n ? n : 1) * sizeof *nx);
        if (!nx) return CP0_MAP_E_NOMEM;
        l->mx = nx;
        double *ny = (double *)realloc(l->my, (size_t)(n ? n : 1) * sizeof *ny);
        if (!ny) return CP0_MAP_E_NOMEM;
        l->my = ny;
    }
    for (int i = 0; i < n; i++) {
        l->m[i] = mk[i];
        l->m[i].label[CP0_MAP_LABEL_MAX - 1] = '\0';
        cp0_map_latlon_to_world_px(mk[i].lat, mk[i].lon, 0, &l->mx[i], &l->my[i]);
    }
    l->n = n;
    if (l->selected >= n) l->selected = -1;
    lv_obj_invalidate(l->map->obj);
    return n;
}

void cp0_map_markers_select(cp0_map_layer_t *l, int index)
{
    if (!l || l->kind != LAYER_MARKERS) return;
    l->selected = index >= 0 && index < l->n ? index : -1;
    lv_obj_invalidate(l->map->obj);
}

void cp0_map_markers_label_zoom(cp0_map_layer_t *l, int zoom)
{
    if (!l || l->kind != LAYER_MARKERS) return;
    l->label_zoom = zoom;
    lv_obj_invalidate(l->map->obj);
}

int cp0_map_track_append(cp0_map_layer_t *l, double lat, double lon)
{
    if (!l || l->kind != LAYER_TRACK) return CP0_MAP_E_INVALID;
    if (l->tn >= CP0_MAP_TRACK_POINTS_MAX) return CP0_MAP_E_FULL;
    if (l->tn == l->tcap) {
        int cap = l->tcap ? l->tcap * 2 : 256;
        if (cap > CP0_MAP_TRACK_POINTS_MAX) cap = CP0_MAP_TRACK_POINTS_MAX;
        double *x = (double *)realloc(l->tx, (size_t)cap * sizeof *x);
        if (!x) return CP0_MAP_E_NOMEM;
        l->tx = x;
        double *y = (double *)realloc(l->ty, (size_t)cap * sizeof *y);
        if (!y) return CP0_MAP_E_NOMEM;
        l->ty = y;
        int *k = (int *)realloc(l->keep, (size_t)cap * sizeof *k);
        if (!k) return CP0_MAP_E_NOMEM;
        l->keep = k;
        int *s = (int *)realloc(l->stack, (size_t)cap * 2 * sizeof *s);
        if (!s) return CP0_MAP_E_NOMEM;
        l->stack = s;
        unsigned char *mk = (unsigned char *)realloc(l->mark, (size_t)cap);
        if (!mk) return CP0_MAP_E_NOMEM;
        l->mark = mk;
        l->tcap = cap;
    }
    cp0_map_latlon_to_world_px(lat, lon, 0, &l->tx[l->tn], &l->ty[l->tn]);
    l->tn++;
    l->simp_dirty = 1;
    lv_obj_invalidate(l->map->obj);
    return CP0_MAP_OK;
}

void cp0_map_track_clear(cp0_map_layer_t *l)
{
    if (!l || l->kind != LAYER_TRACK) return;
    l->tn = l->kept = 0;
    l->simp_dirty = 1;
    lv_obj_invalidate(l->map->obj);
}

void cp0_map_layer_show(cp0_map_layer_t *l, int on)
{
    if (!l) return;
    l->shown = on != 0;
    lv_obj_invalidate(l->map->obj);
}

void cp0_map_on_tile(lv_obj_t *obj, cp0_map_tile_cb cb, void *user)
{
    map_t *m = map_of(obj);
    if (!m) return;
    m->on_tile = cb;
    m->on_tile_user = user;
}

int cp0_map_control_area(lv_obj_t *obj, cp0_map_control_t control, lv_area_t *out)
{
    map_t *m = map_of(obj);
    if (!m || !out || control < CP0_MAP_CONTROL_ZOOM_IN || control > CP0_MAP_CONTROL_LOCATE) return CP0_MAP_E_INVALID;
    ensure_layout(m);
    const cp0_map_rect_t *r = &m->layout.ctl[control];
    if (!r->w) return CP0_MAP_E_NOT_FOUND;
    lv_area_t o;
    lv_obj_get_coords(obj, &o);
    *out = area_of(&o, r);
    return CP0_MAP_OK;
}

int cp0_map_wait_idle(lv_obj_t *obj, int timeout_ms)
{
    map_t *m = map_of(obj);
    if (!m) return CP0_MAP_E_INVALID;
    if (m->view_dirty) {
        m->view_dirty = 0;
        ensure_layout(m);
        submit(m);
        lv_obj_invalidate(obj);
    }
    const int64_t until = cp0_map_monotonic_ms() + timeout_ms;
    int rc = CP0_MAP_OK;
    while (!cp0_map_loader_idle(m->loader)) {
        collect(m);
        if (cp0_map_monotonic_ms() > until) {
            rc = CP0_MAP_E_CANCELLED;
            break;
        }
        struct timespec ts = {0, 1000000};
        nanosleep(&ts, NULL);
    }
    collect(m);
    return rc;
}
