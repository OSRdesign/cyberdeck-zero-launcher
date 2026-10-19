/*
 * SPDX-License-Identifier: MIT
 *
 * Pure multitouch contact tracker for mtrec (study 026, P-a): feeds raw evdev events (protocol B slots, protocol A
 * frames or a single-touch device) and keeps per-contact and per-run statistics. No I/O; host unit-tested.
 *
 * Distances are in millimetres on the glass: each raw axis is normalised by its range, as the launcher's legacy touch
 * path does (cp0_lvgl_dpi_scaled.c touch_update), then scaled by the panel size in display orientation; with swap
 * set, the raw X axis runs along the display's height (the deck's Goodix).
 */
#pragma once

#include <stdbool.h>

#define MT_MAX_SLOTS 16
#define MT_GHOST_US 30000LL /* a contact shorter than this is "short-lived" (ghost or bounce) */

enum { MT_MODE_B = 0, MT_MODE_A = 1, MT_MODE_SINGLE = 2 };

typedef struct {
    int min_x, max_x, min_y, max_y; /* raw ranges (EVIOCGABS) */
    double mm_w, mm_h;              /* panel size in display orientation */
    int swap;                       /* raw X runs along the display's Y */
} mt_geom;

typedef struct {
    int tid;                 /* tracking id, -1 = slot free */
    long long id;            /* running contact number (1, 2, ...) */
    long long t_start, t_end; /* microseconds */
    int x, y;                /* latest raw position */
    int x0, y0;              /* landing position (first frame) */
    bool landed;
    int lastx, lasty;
    double path_mm, maxdisp_mm;
    int major_min, major_max; /* -1: never reported */
    int width_max, pressure_max;
    int seg;                 /* segment the contact started in (-1: none) */
} mt_contact;

typedef struct {
    long long contacts, ghosts, frames, dropped;
    int max_active;
    double min_pair_mm;  /* closest two simultaneous contacts; < 0: never two */
    double max_disp_mm;  /* largest distance a contact moved from where it landed */
    double max_path_mm;
    int major_min, major_max; /* over all contacts; -1 none */
    long long longest_us, shortest_us;
} mt_stats;

typedef struct mt_tracker mt_tracker;
typedef void (*mt_end_cb)(void *user, const mt_contact *c);
typedef void (*mt_frame_cb)(void *user, long long t_us, int active, double min_pair_mm);

struct mt_tracker {
    mt_geom g;
    int mode, nslots, cur;
    mt_contact slot[MT_MAX_SLOTS];
    int slot_major[MT_MAX_SLOTS], slot_width[MT_MAX_SLOTS], slot_pressure[MT_MAX_SLOTS];
    bool st_down;
    int a_count;
    bool a_has_data;
    bool in_drop;
    long long next_id;
    int seg; /* current segment id, -1 = none */
    mt_stats total;
    mt_end_cb on_end;
    mt_frame_cb on_frame;
    void *user;
};

void mt_stats_reset(mt_stats *s);
void mt_init(mt_tracker *t, const mt_geom *g, int mode, int nslots);
void mt_feed(mt_tracker *t, long long t_us, int type, int code, int value);
/* ends every open contact at t_us (end of a recording) */
void mt_finish(mt_tracker *t, long long t_us);
int mt_active(const mt_tracker *t);
/* the raw point in mm on the glass (display orientation) */
void mt_to_mm(const mt_geom *g, int x, int y, double *mx, double *my);
double mt_dist_mm(const mt_geom *g, int x1, int y1, int x2, int y2);
/* adds a finished contact to a stats block */
void mt_stats_add_contact(mt_stats *s, const mt_contact *c);
