/*
 * SPDX-License-Identifier: MIT
 *
 * The SSH indicator in the status bar renderer (cp0_statusbar.c, 2026-10-30), with the shipped fonts (argv[1]: the
 * launcher's share/font directory):
 *   - a state without a session draws exactly what it drew before (the SSH fields ignored);
 *   - the slot's width at 100 % and 72 % (the strips of 640x480 and up, and of the narrow ones under 600 px): how far
 *     cp0::ui::status_strip_left(), so the header's Esc, moves left on a board without a battery;
 *   - nothing else moves when the glyph shows; idle is the Bluetooth "connected" blue, the blink's other phase white;
 *   - with a battery its percentage is left out while the glyph shows, the level glyph and everything right of it stay,
 *     and the strip's leftmost pixel does not move (the glyph fits where "100%" was).
 */
#define _GNU_SOURCE

#include "cp0_statusbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

#define ROWS (CP0_STATUSBAR_HEIGHT + 16) /* the renderer may draw this deep */
#define MAX_W 320

static cp0_statusbar_t *g_bar;

typedef struct {
    int w, top, pct;
} strip_t;

/* the deck's strip (640x480 and every canvas 600 px wide or more: 320 px at 100 %) and the narrow one (480x320, and the
 * portrait 480x640 / 480x800: 72 % of 320) */
static const strip_t kStrips[] = {{320, 8, 100}, {230, 5, 72}};

static void render(const strip_t *s, const cp0_statusbar_state_t *state, uint32_t *px)
{
    memset(px, 0, sizeof(uint32_t) * MAX_W * ROWS);
    cp0_statusbar_render_scaled(g_bar, px, s->w, s->w, 0, s->top, 0, state, s->pct);
}

static int first_column(const strip_t *s, const uint32_t *px)
{
    for (int x = 0; x < s->w; ++x)
        for (int y = 0; y < ROWS; ++y)
            if (px[y * s->w + x] >> 24) return x;
    return s->w;
}

static int same_from(const strip_t *s, const uint32_t *a, const uint32_t *b, int from_x)
{
    for (int y = 0; y < ROWS; ++y)
        for (int x = from_x; x < s->w; ++x)
            if (a[y * s->w + x] != b[y * s->w + x]) return 0;
    return 1;
}

/* pixels of colour rgb (any alpha) left of x */
static int count_rgb(const strip_t *s, const uint32_t *px, uint32_t rgb, int left_of)
{
    int n = 0;
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < left_of && x < s->w; ++x) {
            const uint32_t p = px[y * s->w + x];
            if ((p >> 24) && (p & 0xFFFFFFu) == rgb) ++n;
        }
    return n;
}

static cp0_statusbar_state_t full_state(void)
{
    cp0_statusbar_state_t st;
    memset(&st, 0, sizeof(st));
    st.wifi_up = 1;
    st.wifi_pct = 100;
    st.bt_on = 1;
    st.bt_connected = 1;
    snprintf(st.clock, sizeof(st.clock), "00:00");
    return st;
}

static uint32_t a[MAX_W * ROWS], b[MAX_W * ROWS], c[MAX_W * ROWS];

/* the slot's width (how far the strip's leftmost pixel moves) at each scale: measured 2026-10-30 with the shipped fonts */
static const int kSlot[] = {49, 35};

static void test_strip(int i)
{
    const strip_t *s = &kStrips[i];

    /* no session: the SSH fields are ignored, the pixels are the old ones */
    cp0_statusbar_state_t plain = full_state();
    render(s, &plain, a);
    cp0_statusbar_state_t noise = plain;
    noise.ssh_active = 1;
    noise.ssh_bright = 1;
    render(s, &noise, b);
    CHECK(memcmp(a, b, sizeof(a)) == 0);
    const int left_plain = first_column(s, a);

    /* a session, no battery: the glyph left of the Bluetooth glyph, nothing else moves */
    cp0_statusbar_state_t ssh = plain;
    ssh.ssh = 1;
    render(s, &ssh, b);
    const int left_ssh = first_column(s, b);
    printf("statusbar %d %%: strip's leftmost pixel %d -> %d with the SSH slot (%d px)\n", s->pct, left_plain, left_ssh,
           left_plain - left_ssh);
    CHECK(left_plain - left_ssh == kSlot[i]);
    CHECK(same_from(s, a, b, left_plain));
    CHECK(count_rgb(s, b, 0x3B9DFFu, left_plain) > 20); /* BT_CONNECTED */
    CHECK(count_rgb(s, b, 0xFFFFFFu, left_plain) == 0);
    /* more connections draw the same (no count badge) */
    cp0_statusbar_state_t two = ssh;
    two.ssh = 2;
    render(s, &two, c);
    CHECK(memcmp(b, c, sizeof(b)) == 0);
    /* the blink's other phase: white, in the same place */
    cp0_statusbar_state_t bright = ssh;
    bright.ssh_active = 1;
    bright.ssh_bright = 1;
    render(s, &bright, c);
    CHECK(first_column(s, c) == left_ssh);
    CHECK(same_from(s, b, c, left_plain));
    CHECK(count_rgb(s, c, 0xFFFFFFu, left_plain) == count_rgb(s, b, 0x3B9DFFu, left_plain));
    CHECK(count_rgb(s, c, 0x3B9DFFu, left_plain) == 0);
    /* ssh_active alone is not drawn */
    cp0_statusbar_state_t active = ssh;
    active.ssh_active = 1;
    render(s, &active, c);
    CHECK(memcmp(b, c, sizeof(b)) == 0);
    /* the slot does not depend on the Bluetooth glyph being drawn */
    cp0_statusbar_state_t bt_off = ssh;
    bt_off.bt_on = 0;
    bt_off.bt_connected = 0;
    render(s, &bt_off, c);
    CHECK(first_column(s, c) == left_ssh);

    /* a battery at 100 %, discharging (white), Bluetooth on but not connected (grey): the glyph takes the percentage's
     * place; the level glyph and everything right of it stay; the leftmost pixel does not move left */
    cp0_statusbar_state_t batt = plain;
    batt.bt_connected = 0;
    batt.battery = 1;
    batt.battery_pct = 100;
    render(s, &batt, a);
    const int left_batt = first_column(s, a);
    cp0_statusbar_state_t batt_ssh = batt;
    batt_ssh.ssh = 1;
    render(s, &batt_ssh, b);
    const int left_batt_ssh = first_column(s, b);
    int level_x = s->w; /* the level glyph's first column: the first white one with the SSH glyph shown */
    for (int x = 0; x < s->w && level_x == s->w; ++x)
        for (int y = 0; y < ROWS; ++y) {
            const uint32_t p = b[y * s->w + x];
            if ((p >> 24) && (p & 0xFFFFFFu) == 0xFFFFFFu) {
                level_x = x;
                break;
            }
        }
    printf("statusbar %d %% with a battery at 100 %%: leftmost pixel %d (\"100%%\") -> %d (SSH glyph), level glyph at "
           "%d\n",
           s->pct, left_batt, left_batt_ssh, level_x);
    CHECK(left_batt_ssh >= left_batt);
    CHECK(level_x < s->w && same_from(s, a, b, level_x));
    CHECK(count_rgb(s, a, 0xFFFFFFu, level_x) > 20);  /* the percentage, left of the level glyph */
    CHECK(count_rgb(s, b, 0xFFFFFFu, level_x) == 0);  /* gone */
    CHECK(count_rgb(s, b, 0x3B9DFFu, level_x) > 20);  /* the SSH glyph there instead */
    /* charging: the battery green, the SSH glyph still blue */
    batt_ssh.battery_charging = 1;
    render(s, &batt_ssh, c);
    CHECK(count_rgb(s, c, 0x3B9DFFu, level_x) == count_rgb(s, b, 0x3B9DFFu, level_x));
    /* a lower charge (a narrower "85%") keeps the glyph's place: it hangs off the level glyph, not the text */
    batt_ssh.battery_charging = 0;
    batt_ssh.battery_pct = 85;
    render(s, &batt_ssh, c);
    CHECK(first_column(s, c) == left_batt_ssh);
}

/* the translucent backing (viz1090's bar) covers the SSH glyph */
static void test_backing(void)
{
    const strip_t *s = &kStrips[0];
    cp0_statusbar_state_t ssh = full_state();
    ssh.ssh = 1;
    render(s, &ssh, a);
    const int glyph_left = first_column(s, a);
    memset(b, 0, sizeof(b));
    cp0_statusbar_render_scaled(g_bar, b, s->w, s->w, 0, s->top, 128, &ssh, s->pct);
    CHECK(first_column(s, b) < glyph_left);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s FONT_DIR\n", argv[0]);
        return 2;
    }
    char text[512], icon[512];
    snprintf(text, sizeof(text), "%s/Montserrat-Medium.ttf", argv[1]);
    snprintf(icon, sizeof(icon), "%s/FontAwesome5-Solid+Brands+Regular.woff", argv[1]);
    g_bar = cp0_statusbar_create(text, icon);
    CHECK(g_bar != NULL);
    for (int i = 0; i < (int)(sizeof(kStrips) / sizeof(kStrips[0])); ++i) test_strip(i);
    test_backing();
    cp0_statusbar_destroy(g_bar);
    printf("test_statusbar_ssh: PASS\n");
    return 0;
}
