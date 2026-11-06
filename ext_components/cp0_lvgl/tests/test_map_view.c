/*
 * SPDX-License-Identifier: MIT
 *
 * Map widget, the pure core (src/map/cp0_map_view.h):
 *   - the view: centre and zoom round trips, zoom limits with the one over-zoom level, zoom at a point (the point stays
 *     under the finger), pan (the content follows the finger), the world's wrap and edges, the display scale;
 *   - the tiles of a view in load order (visible first, the centre's tile first, then the ring), screen positions,
 *     over-zoom tiles, the world smaller than the view;
 *   - the controls at the four target canvases (640x480, 480x320, 320x480, 480x800 with the launcher's own tokens, a
 *     header above and an app's tab strip below): inside the widget, none over another, each at most 15 % of the
 *     shorter side, the hit test, a long attribution moved a row up;
 *   - Douglas-Peucker (a line, a zigzag, a 20,000-point walk within eps), label placement with nudging, the
 *     attribution text, fix freshness, the loader's scale step choice.
 */
#ifndef MAP_FIXTURES
#define MAP_FIXTURES ""
#endif
#include "map_test_util.h"

#include "cp0_map_loader.h"
#include "cp0_map_view.h"
#include "cp0_ui_metrics.h"

#include <math.h>

static int near(double a, double b, double eps)
{
    return fabs(a - b) <= eps;
}

static void test_view(void)
{
    cp0_map_view_t v;
    cp0_map_view_init(&v, 640, 400);
    cp0_map_view_set_limits(&v, 2, 14, 1);
    CHECK(v.zmin == 2 && v.zmax == 15 && v.data_max == 14);
    CHECK(cp0_map_view_set_zoom(&v, 20) == 15 && cp0_map_view_overzoom(&v));
    CHECK(cp0_map_view_set_zoom(&v, 0) == 2 && !cp0_map_view_overzoom(&v));
    cp0_map_view_set_limits(&v, 0, 13, 0);
    CHECK(v.zmax == 13);
    cp0_map_view_set_limits(&v, 0, 18, 1);
    cp0_map_view_set_zoom(&v, 12);
    cp0_map_view_set_center(&v, 48.8566, 2.3522);
    double lat, lon;
    cp0_map_view_center(&v, &lat, &lon);
    CHECK(near(lat, 48.8566, 1e-9) && near(lon, 2.3522, 1e-9));
    for (int z = 3; z <= 19; z += 4) {
        cp0_map_view_set_zoom(&v, z);
        cp0_map_view_center(&v, &lat, &lon);
        CHECK(near(lat, 48.8566, 1e-8) && near(lon, 2.3522, 1e-8));
    }
    /* scale: the centre stays, tiles grow */
    for (int s = 2; s <= 4; s++) {
        cp0_map_view_set_scale(&v, s);
        CHECK(cp0_map_view_tile_px(&v) == 128 * s);
        cp0_map_view_center(&v, &lat, &lon);
        CHECK(near(lat, 48.8566, 1e-8));
        CHECK(near(cp0_map_view_metres_per_px(&v), cp0_map_metres_per_px(lat, v.z, s / 2.0f), 1e-9));
    }
    cp0_map_view_set_scale(&v, 9);
    CHECK(v.scale_x2 == 4);
    /* screen <-> lat/lon, both ways */
    for (int s = 2; s <= 4; s++) {
        cp0_map_view_set_scale(&v, s);
        double sx, sy;
        cp0_map_view_latlon_to_screen(&v, 48.86, 2.36, &sx, &sy);
        cp0_map_view_screen_to_latlon(&v, sx, sy, &lat, &lon);
        CHECK(near(lat, 48.86, 1e-9) && near(lon, 2.36, 1e-9));
        cp0_map_view_latlon_to_screen(&v, 48.8566, 2.3522, &sx, &sy);
        CHECK(near(sx, 320, 1.0) && near(sy, 200, 1.0)); /* the centre is in the middle */
    }
    /* zoom at a point: the map point under (100, 80) stays there, in and out */
    cp0_map_view_set_scale(&v, 3);
    cp0_map_view_set_zoom(&v, 13);
    double plat, plon;
    cp0_map_view_screen_to_latlon(&v, 100, 80, &plat, &plon);
    CHECK(cp0_map_view_zoom_at(&v, 1, 100, 80) == 14);
    double sx, sy;
    cp0_map_view_latlon_to_screen(&v, plat, plon, &sx, &sy);
    CHECK(near(sx, 100, 1.01) && near(sy, 80, 1.01));
    CHECK(cp0_map_view_zoom_at(&v, -2, 100, 80) == 12);
    cp0_map_view_latlon_to_screen(&v, plat, plon, &sx, &sy);
    CHECK(near(sx, 100, 1.01) && near(sy, 80, 1.01));
    cp0_map_view_set_limits(&v, 0, 12, 1);
    CHECK(cp0_map_view_zoom_at(&v, 5, 10, 10) == 13);
    CHECK(cp0_map_view_zoom_at(&v, 1, 10, 10) == 13); /* at the limit: nothing moves */
    /* pan: the content follows the finger */
    cp0_map_view_screen_to_latlon(&v, 200, 150, &plat, &plon);
    cp0_map_view_pan(&v, 37, -21);
    cp0_map_view_latlon_to_screen(&v, plat, plon, &sx, &sy);
    CHECK(near(sx, 237, 1.01) && near(sy, 129, 1.01));
    /* the world wraps east-west, stops north-south */
    cp0_map_view_set_zoom(&v, 2);
    cp0_map_view_set_center(&v, 0, 179);
    cp0_map_view_pan(&v, -300, 0);
    cp0_map_view_center(&v, &lat, &lon);
    CHECK(lon < 0 && lon > -180);
    cp0_map_view_pan(&v, 0, 1e6);
    cp0_map_view_center(&v, &lat, &lon);
    CHECK(near(lat, CP0_MAP_MAX_LAT, 1e-6));
    printf("map view: centre / zoom / scale round trips, limits + over-zoom, zoom at a point, pan, wrap\n");
}

static void test_tiles(void)
{
    cp0_map_view_t v;
    cp0_map_view_init(&v, 640, 480);
    cp0_map_view_set_limits(&v, 0, 13, 1);
    cp0_map_view_set_zoom(&v, 13);
    cp0_map_view_set_center(&v, 48.6, 2.0);
    cp0_map_slot_t s[CP0_MAP_VIEW_MAX_TILES];
    uint32_t cx, cy;
    cp0_map_latlon_to_tile(48.6, 2.0, 13, &cx, &cy);
    for (int scale = 2; scale <= 4; scale++) {
        cp0_map_view_set_scale(&v, scale);
        const int T = cp0_map_view_tile_px(&v);
        const int n = cp0_map_view_tiles(&v, 1, s, CP0_MAP_VIEW_MAX_TILES);
        int nvis = 0;
        while (nvis < n && s[nvis].visible) nvis++;
        for (int i = nvis; i < n; i++) CHECK(!s[i].visible);
        for (int i = 1; i < nvis; i++) CHECK(s[i].dist2 >= s[i - 1].dist2);
        for (int i = nvis + 1; i < n; i++) CHECK(s[i].dist2 >= s[i - 1].dist2);
        CHECK(s[0].x == cx && s[0].y == cy && s[0].z == 13 && !s[0].over); /* the centre's tile first */
        CHECK(s[0].sx <= 320 && 320 < s[0].sx + T && s[0].sy <= 240 && 240 < s[0].sy + T);
        /* the visible tiles cover the widget, nothing more */
        int64_t covered = 0;
        for (int i = 0; i < nvis; i++) {
            CHECK(s[i].sx < 640 && s[i].sx + T > 0 && s[i].sy < 480 && s[i].sy + T > 0);
            const int x0 = s[i].sx < 0 ? 0 : s[i].sx, x1 = s[i].sx + T > 640 ? 640 : s[i].sx + T;
            const int y0 = s[i].sy < 0 ? 0 : s[i].sy, y1 = s[i].sy + T > 480 ? 480 : s[i].sy + T;
            covered += (int64_t)(x1 - x0) * (y1 - y0);
        }
        CHECK(covered == 640 * 480);
        /* neighbours share edges */
        for (int i = 0; i < nvis; i++)
            for (int j = 0; j < nvis; j++)
                if (s[j].x == s[i].x + 1 && s[j].y == s[i].y) CHECK(s[j].sx == s[i].sx + T && s[j].sy == s[i].sy);
        const int ring = n - nvis;
        CHECK(ring > 0 && ring <= 2 * (640 / T + 3) + 2 * (480 / T + 3));
    }
    /* over-zoom */
    cp0_map_view_set_zoom(&v, 14);
    int n = cp0_map_view_tiles(&v, 0, s, CP0_MAP_VIEW_MAX_TILES);
    CHECK(n > 0);
    for (int i = 0; i < n; i++) CHECK(s[i].over && s[i].z == 14);
    /* zoom 0 at 1x on a wide widget: the one tile repeats across, rows outside the world are left out */
    cp0_map_view_set_limits(&v, 0, 13, 1);
    cp0_map_view_set_scale(&v, 2);
    cp0_map_view_set_zoom(&v, 0);
    cp0_map_view_set_size(&v, 1280, 400);
    cp0_map_view_set_center(&v, 0, 0);
    n = cp0_map_view_tiles(&v, 1, s, CP0_MAP_VIEW_MAX_TILES);
    for (int i = 0; i < n; i++) CHECK(s[i].x == 0 && s[i].y == 0);
    CHECK(n >= 5);
    /* marker culling: inside, on the margin, outside, and across the antimeridian */
    {
        cp0_map_view_t c;
        cp0_map_view_init(&c, 400, 300);
        cp0_map_view_set_limits(&c, 0, 18, 1);
        cp0_map_view_set_zoom(&c, 10);
        cp0_map_view_set_center(&c, 10.0, 179.9);
        double wx0, wy0, sx, sy;
        cp0_map_latlon_to_world_px(10.0, 179.9, 0, &wx0, &wy0);
        CHECK(cp0_map_view_visible(&c, wx0, wy0, 0, &sx, &sy) && near(sx, 200, 1) && near(sy, 150, 1));
        cp0_map_latlon_to_world_px(10.0, -179.95, 0, &wx0, &wy0); /* east of the antimeridian, still in view */
        CHECK(cp0_map_view_visible(&c, wx0, wy0, 0, &sx, &sy) && sx > 200 && sx < 400);
        cp0_map_latlon_to_world_px(10.0, 179.4, 0, &wx0, &wy0); /* far west: out */
        CHECK(!cp0_map_view_visible(&c, wx0, wy0, 0, &sx, &sy) && sx < 0);
        /* a point just past the left edge: out at margin 0, in with a margin larger than the overshoot */
        double plat, plon;
        cp0_map_view_screen_to_latlon(&c, -5, 100, &plat, &plon);
        cp0_map_latlon_to_world_px(plat, plon, 0, &wx0, &wy0);
        CHECK(!cp0_map_view_visible(&c, wx0, wy0, 0, NULL, NULL) && cp0_map_view_visible(&c, wx0, wy0, 8, NULL, NULL));
    }
    /* a tiny buffer: the visible tiles first */
    cp0_map_view_set_size(&v, 640, 480);
    cp0_map_view_set_zoom(&v, 13);
    CHECK(cp0_map_view_tiles(&v, 1, s, 4) == 4 && s[3].visible);
    printf("map view: tiles in load order at 1x / 1.5x / 2x (centre first, cover exactly, ring after), over-zoom, "
           "zoom 0 repeats\n");
}

static int need_even(void *user, const cp0_map_slot_t *s)
{
    (void)user;
    return (s->x + s->y) % 2 == 0;
}

/* task 3: what an online source's fetch pool is asked for: the visible tiles only, never the ring */
static void test_fetch_tiles(void)
{
    cp0_map_view_t v;
    cp0_map_slot_t s[CP0_MAP_VIEW_MAX_TILES];
    cp0_map_tile_id_t t[CP0_MAP_VIEW_MAX_TILES];
    static const int sizes[][2] = {{640, 480}, {480, 320}, {320, 480}, {480, 800}};
    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        for (int scale = 2; scale <= 4; scale++) {
            cp0_map_view_init(&v, sizes[k][0], sizes[k][1]);
            cp0_map_view_set_limits(&v, 0, 13, 1);
            cp0_map_view_set_scale(&v, scale);
            cp0_map_view_set_zoom(&v, 13);
            cp0_map_view_set_center(&v, 48.6, 2.0);
            const int n = cp0_map_view_tiles(&v, 1, s, CP0_MAP_VIEW_MAX_TILES);
            int nvis = 0;
            for (int i = 0; i < n; i++) nvis += s[i].visible;
            CHECK(nvis > 0 && n > nvis); /* there is a ring */
            const int m = cp0_map_view_fetch_tiles(s, n, NULL, NULL, t, CP0_MAP_VIEW_MAX_TILES);
            CHECK(m == nvis);
            for (int i = 0; i < m; i++) {
                int visible = 0;
                for (int j = 0; j < n && !visible; j++)
                    visible = s[j].visible && s[j].z == t[i].z && s[j].x == t[i].x && s[j].y == t[i].y;
                CHECK(visible); /* never a ring tile */
            }
            CHECK(t[0].x == s[0].x && t[0].y == s[0].y); /* the centre first */
            /* only the slots the caller needs (absent or stale), still never the ring */
            const int e = cp0_map_view_fetch_tiles(s, n, need_even, NULL, t, CP0_MAP_VIEW_MAX_TILES);
            int want = 0;
            for (int i = 0; i < n; i++) want += s[i].visible && need_even(NULL, &s[i]);
            CHECK(e == want);
            CHECK(cp0_map_view_fetch_tiles(s, n, NULL, NULL, t, 2) == 2);
        }
    }
    /* over-zoom: the parents, each once */
    cp0_map_view_init(&v, 640, 480);
    cp0_map_view_set_limits(&v, 0, 13, 1);
    cp0_map_view_set_zoom(&v, 14);
    cp0_map_view_set_center(&v, 48.6, 2.0);
    const int n = cp0_map_view_tiles(&v, 1, s, CP0_MAP_VIEW_MAX_TILES);
    const int m = cp0_map_view_fetch_tiles(s, n, NULL, NULL, t, CP0_MAP_VIEW_MAX_TILES);
    int nvis = 0;
    for (int i = 0; i < n; i++) nvis += s[i].visible;
    CHECK(m > 0 && m < nvis);
    for (int i = 0; i < m; i++) {
        CHECK(t[i].z == 13);
        for (int j = 0; j < i; j++) CHECK(t[j].x != t[i].x || t[j].y != t[i].y);
    }
    CHECK(cp0_map_view_fetch_tiles(NULL, 3, NULL, NULL, t, 3) == 0);
    printf("map view: the fetch list holds the visible tiles only (never the ring), centre first, parents once\n");
}

static void layout_case(const char *name, int canvas_w, int canvas_h, int ppmm, int attribution_w, int locate)
{
    cp0_ui_metrics_t mt;
    CHECK(cp0_ui_metrics_compute(canvas_w, canvas_h, 0, ppmm, CP0_UI_DENSITY_PANEL_MM, 0, &mt) == 0);
    /* the app's map body: under the page header, above a tab strip of one row */
    const int w = canvas_w, h = canvas_h - mt.tok[CP0_TOK_HEADER_H] - mt.tok[CP0_TOK_ROW_H];
    cp0_map_layout_in_t in;
    memset(&in, 0, sizeof in);
    in.w = w;
    in.h = h;
    in.target = mt.tok[CP0_TOK_TARGET];
    in.space = mt.tok[CP0_TOK_SPACE_M];
    in.radius = mt.tok[CP0_TOK_RADIUS_S];
    in.text_h = mt.text_px[CP0_TEXT_CAPTION] * 5 / 4;
    in.attribution_w = attribution_w;
    in.badge_w = 60;
    in.strip_w = 200;
    in.locate = locate;
    cp0_map_layout_t l;
    cp0_map_layout(&in, &l);
    const int shorter = w < h ? w : h;
    const cp0_map_rect_t *all[] = {&l.plates[1], &l.plates[2], &l.plates[3], &l.scalebar, &l.attribution, &l.badge,
                                   &l.strip};
    const char *names[] = {"zoom in", "zoom out", "locate", "scale bar", "attribution", "badge", "strip"};
    for (int i = 0; i < 7; i++) {
        const cp0_map_rect_t *r = all[i];
        if (!r->w) continue;
        if (r->x < 0 || r->y < 0 || r->x + r->w > w || r->y + r->h > h) {
            fprintf(stderr, "%s: %s outside the %dx%d widget\n", name, names[i], w, h);
            CHECK(0);
        }
        for (int j = i + 1; j < 7; j++) {
            if (i == 5 && j == 6) continue; /* the badge and the strip never show together */
            if (cp0_map_rect_overlap(r, all[j])) {
                fprintf(stderr, "%s: %s overlaps %s\n", name, names[i], names[j]);
                CHECK(0);
            }
        }
        g_checks++;
    }
    CHECK(l.plate <= shorter * 15 / 100 && l.plate <= l.btn && l.btn <= in.target);
    /* the touch squares: inside the widget, never over each other, each holding its plate */
    for (int c = 1; c < CP0_MAP_CTL__COUNT; c++) {
        const cp0_map_rect_t *t = &l.ctl[c], *p = &l.plates[c];
        if (!t->w) continue;
        CHECK(t->x >= 0 && t->y >= 0 && t->x + t->w <= w && t->y + t->h <= h);
        CHECK(p->x >= t->x && p->y >= t->y && p->x + p->w <= t->x + t->w && p->y + p->h <= t->y + t->h);
        for (int d = c + 1; d < CP0_MAP_CTL__COUNT; d++) CHECK(!cp0_map_rect_overlap(t, &l.ctl[d]));
    }
    CHECK(l.scalebar.h <= shorter * 15 / 100 + 4 && l.scalebar_max_px <= w / 4 + 1); /* low, a quarter of the width */
    CHECK((l.ctl[CP0_MAP_CTL_LOCATE].w != 0) == (locate != 0));
    for (int c = 1; c < CP0_MAP_CTL__COUNT; c++) {
        const cp0_map_rect_t *r = &l.ctl[c];
        if (!r->w) continue;
        CHECK(cp0_map_layout_hit(&l, r->x + r->w / 2, r->y + r->h / 2) == (cp0_map_ctl_t)c);
        CHECK(cp0_map_layout_hit(&l, r->x, r->y) == (cp0_map_ctl_t)c);
        CHECK(cp0_map_layout_hit(&l, r->x + r->w, r->y + r->h / 2) != (cp0_map_ctl_t)c);
    }
    CHECK(cp0_map_layout_hit(&l, w / 2, h / 2) == CP0_MAP_CTL_NONE);
    /* the zoom pair is centred vertically when nothing is in its way */
    const int mid = (l.ctl[1].y + l.ctl[2].y + l.ctl[2].h) / 2;
    const int centred = abs(mid - h / 2) <= 2;
    if (attribution_w > 0) CHECK(l.attribution_text_w <= attribution_w && l.attribution_text_w > 0);
    printf("map view: layout %-26s widget %3dx%3d: touch %d px (target %d), plate %d, %s, attribution %s\n", name, w,
           h, l.btn, in.target, l.plate, centred ? "zoom pair centred" : "zoom pair moved",
           !attribution_w ? "none" : l.attribution.y + l.attribution.h <= l.scalebar.y ? "a row up" : "beside the bar");
}

static void test_layout(void)
{
    static const struct {
        const char *name;
        int w, h, ppmm;
    } canvases[] = {{"640x480 (deck, 11.3)", 640, 480, 1130},  {"480x320 (Pi 3A+, 6.3)", 480, 320, 630},
                    {"320x480 (zero8 portrait)", 320, 480, 630}, {"480x800 (Pi 3A+ DSI, 8.5)", 480, 800, 854},
                    {"800x480 (8.5)", 800, 480, 854},          {"720x720 (HackBerry)", 720, 720, 1000},
                    {"1280x800 (FZ-M1)", 1280, 800, 857}};
    for (size_t i = 0; i < sizeof canvases / sizeof canvases[0]; i++) {
        layout_case(canvases[i].name, canvases[i].w, canvases[i].h, canvases[i].ppmm, 190, 1);
        layout_case(canvases[i].name, canvases[i].w, canvases[i].h, canvases[i].ppmm, 0, 0);
        layout_case(canvases[i].name, canvases[i].w, canvases[i].h, canvases[i].ppmm, 600, 1); /* too long: cut */
    }
    /* tiny widgets still keep everything inside */
    cp0_map_layout_in_t in = {120, 90, 79, 12, 6, 14, 100, 50, 80, 1};
    cp0_map_layout_t l;
    cp0_map_layout(&in, &l);
    const cp0_map_rect_t *all[] = {&l.ctl[1], &l.ctl[2], &l.ctl[3], &l.scalebar, &l.attribution, &l.plates[1]};
    for (int i = 0; i < 6; i++)
        if (all[i]->w) CHECK(all[i]->x >= 0 && all[i]->y >= 0 && all[i]->x + all[i]->w <= 120 && all[i]->y + all[i]->h <= 90);
}

static void test_simplify(void)
{
    enum { N = 20000 };
    double *x = (double *)malloc(N * sizeof *x), *y = (double *)malloc(N * sizeof *y);
    int *keep = (int *)malloc(N * sizeof *keep), *stack = (int *)malloc(2 * N * sizeof *stack);
    unsigned char *mark = (unsigned char *)malloc(N);
    for (int i = 0; i < 100; i++) {
        x[i] = i * 3.0;
        y[i] = i * 1.5;
    }
    CHECK(cp0_map_simplify(x, y, 100, 0.5, keep, mark, stack) == 2 && keep[0] == 0 && keep[1] == 99);
    for (int i = 0; i < 41; i++) {
        x[i] = i * 10.0;
        y[i] = (i & 1) ? 5.0 : 0.0;
    }
    CHECK(cp0_map_simplify(x, y, 41, 1.0, keep, mark, stack) == 41);
    CHECK(cp0_map_simplify(x, y, 41, 6.0, keep, mark, stack) == 2);
    CHECK(cp0_map_simplify(x, y, 1, 1.0, keep, mark, stack) == 1);
    CHECK(cp0_map_simplify(x, y, 0, 1.0, keep, mark, stack) == 0);
    /* a 20,000-point walk: every dropped point within eps of the kept polyline */
    rng_seed(99);
    x[0] = y[0] = 0;
    for (int i = 1; i < N; i++) {
        x[i] = x[i - 1] + ((int)(rng() % 200) - 100) / 50.0;
        y[i] = y[i - 1] + ((int)(rng() % 200) - 100) / 50.0;
    }
    const double eps = 0.75;
    const double t0 = now_ms();
    const int k = cp0_map_simplify(x, y, N, eps, keep, mark, stack);
    const double ms = now_ms() - t0;
    CHECK(k >= 2 && k < N && keep[0] == 0 && keep[k - 1] == N - 1);
    for (int j = 1; j < k; j++) {
        CHECK(keep[j] > keep[j - 1]);
        const double ax = x[keep[j - 1]], ay = y[keep[j - 1]], bx = x[keep[j]], by = y[keep[j]];
        for (int i = keep[j - 1] + 1; i < keep[j]; i++) {
            const double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
            double t = len2 > 0 ? ((x[i] - ax) * dx + (y[i] - ay) * dy) / len2 : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            const double ex = ax + t * dx - x[i], ey = ay + t * dy - y[i];
            if (ex * ex + ey * ey > eps * eps + 1e-9) CHECK(0);
        }
    }
    g_checks += N;
    printf("map view: Douglas-Peucker (line 2 points, zigzag, a 20,000-point walk kept %d within %.2f px in %.1f ms)\n",
           k, eps, ms);
    free(x);
    free(y);
    free(keep);
    free(stack);
    free(mark);
}

static void test_labels(void)
{
    /* three markers close together: three labels, none over another */
    int sx[6] = {100, 104, 108, 630, 320, 320}, sy[6] = {100, 102, 104, 240, 5, 470};
    int lw[6] = {60, 60, 60, 60, 50, 50}, lh[6] = {16, 16, 16, 16, 16, 16}, lx[6], ly[6];
    const cp0_map_rect_t avoid[1] = {{150, 60, 40, 40}}; /* a control: no label may cover it */
    const int placed = cp0_map_labels_place(sx, sy, lw, lh, 6, 6, 640, 480, avoid, 1, lx, ly);
    CHECK(placed >= 5);
    for (int i = 0; i < 6; i++) {
        if (lx[i] < 0) continue;
        CHECK(lx[i] >= 0 && ly[i] >= 0 && lx[i] + lw[i] <= 640 && ly[i] + lh[i] <= 480);
        for (int j = i + 1; j < 6; j++) {
            if (lx[j] < 0) continue;
            const cp0_map_rect_t a = {lx[i], ly[i], lw[i], lh[i]}, b = {lx[j], ly[j], lw[j], lh[j]};
            CHECK(!cp0_map_rect_overlap(&a, &b));
        }
        const cp0_map_rect_t a = {lx[i], ly[i], lw[i], lh[i]};
        CHECK(!cp0_map_rect_overlap(&a, &avoid[0]));
    }
    CHECK(lx[3] >= 0 && lx[3] + lw[3] <= 630); /* at the right edge: nudged to the left side */
    /* a crowd: some are left out, never overlapping */
    int cx[40], cy[40], cw[40], ch[40], ox[40], oy[40];
    for (int i = 0; i < 40; i++) {
        cx[i] = 200 + (i % 4);
        cy[i] = 200 + (i % 3);
        cw[i] = 80;
        ch[i] = 16;
    }
    const int p2 = cp0_map_labels_place(cx, cy, cw, ch, 40, 6, 640, 480, NULL, 0, ox, oy);
    CHECK(p2 >= 1 && p2 <= 4);
    printf("map view: labels placed without overlap, nudged off the edge, a crowd thinned to %d\n", p2);
}

static void test_text_and_fix(void)
{
    char out[128];
    cp0_map_attribution_text("<a href=\"https://www.openstreetmap.org/copyright\">&copy; OpenStreetMap contributors</a>",
                             out, sizeof out);
    CHECK(!strcmp(out, "\xc2\xa9 OpenStreetMap contributors"));
    cp0_map_attribution_text("  &#169;  Map&amp;Data\n<b>x</b>&nbsp;y &lt;3 &unknown; ", out, sizeof out);
    CHECK(!strcmp(out, "\xc2\xa9 Map&Data x y <3 &unknown;"));
    cp0_map_attribution_text("<unterminated", out, sizeof out);
    CHECK(!strcmp(out, ""));
    cp0_map_attribution_text("abcdefghij", out, 5);
    CHECK(!strcmp(out, "abcd"));
    cp0_map_attribution_text(NULL, out, sizeof out);
    CHECK(out[0] == '\0');
    const int64_t now = cp0_map_monotonic_ms();
    cp0_map_fix_t f = {48.0, 2.0, 5, 1, 90, now};
    CHECK(cp0_map_fix_fresh(&f, now) && cp0_map_fix_fresh(&f, now + CP0_MAP_FIX_STALE_MS));
    CHECK(!cp0_map_fix_fresh(&f, now + CP0_MAP_FIX_STALE_MS + 1));
    f.mono_ms = 0;
    CHECK(cp0_map_fix_fresh(&f, now + 999999));
    f.lat = NAN;
    CHECK(!cp0_map_fix_fresh(&f, now) && !cp0_map_fix_fresh(NULL, now));
    printf("map view: attribution text (HTML, entities, spaces), fix freshness\n");
}

static void test_scale_to(void)
{
    rng_seed(7);
    const int sizes[][2] = {{256, 384}, {256, 512}, {256, 256}, {512, 384}, {512, 256}, {512, 512}, {128, 384},
                            {128, 512}, {256, 768}};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        const int w = sizes[i][0], out = sizes[i][1];
        uint16_t *src = (uint16_t *)malloc((size_t)w * w * 2);
        for (int k = 0; k < w * w; k++) src[k] = (uint16_t)rng();
        cp0_map_image_t *img = cp0_map_scale_to(src, w, w, w * 2, out);
        CHECK(img && img->w == out && img->h == out);
        const int x4 = out * 4 / w;
        if (x4 == 2 || x4 == 3 || x4 == 4 || x4 == 6 || x4 == 8) {
            uint16_t *ref = (uint16_t *)malloc((size_t)out * out * 2);
            CHECK_RC(cp0_map_resample_rgb565(src, w, w, w * 2, x4, 0, 0, out, out, ref, out * 2), CP0_MAP_OK);
            CHECK(!memcmp(ref, img->px, (size_t)out * out * 2));
            free(ref);
        } else if (x4 == 12 || x4 == 16) {
            const int mid = w * (x4 == 12 ? 6 : 8) / 4;
            uint16_t *m1 = (uint16_t *)malloc((size_t)mid * mid * 2), *ref = (uint16_t *)malloc((size_t)out * out * 2);
            CHECK_RC(cp0_map_resample_rgb565(src, w, w, w * 2, x4 == 12 ? 6 : 8, 0, 0, mid, mid, m1, mid * 2), CP0_MAP_OK);
            CHECK_RC(cp0_map_resample_rgb565(m1, mid, mid, mid * 2, 8, 0, 0, out, out, ref, out * 2), CP0_MAP_OK);
            CHECK(!memcmp(ref, img->px, (size_t)out * out * 2));
            free(m1);
            free(ref);
        }
        cp0_map_image_free(img);
        free(src);
    }
    /* an odd size: nearest */
    uint16_t odd[100 * 100];
    for (int k = 0; k < 10000; k++) odd[k] = (uint16_t)k;
    cp0_map_image_t *img = cp0_map_scale_to(odd, 100, 100, 200, 256);
    CHECK(img && ((uint16_t *)img->px)[0] == 0 && ((uint16_t *)img->px)[255] == 99);
    cp0_map_image_free(img);
    CHECK(cp0_map_scale_to(NULL, 10, 10, 20, 256) == NULL);
    printf("map view: the loader's scale steps (256 px x1 / 1.5 / 2, 512 px x0.5 / 0.75 / 1, over-zoom x3 / x4, "
           "nearest for odd sizes)\n");
}

int main(void)
{
    test_view();
    test_tiles();
    test_fetch_tiles();
    test_layout();
    test_simplify();
    test_labels();
    test_text_and_fix();
    test_scale_to();
    printf("map view OK (%ld checks)\n", g_checks);
    return 0;
}
