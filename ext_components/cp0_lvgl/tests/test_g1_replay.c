/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-c: G1 (cp0_gesture_g1.h) replayed over the P-a touch recordings (mtrec CSV, the .csv.gz files in fixtures/mta:
 * the deck's Goodix and the Pi 3A+'s FT5x06, recorded by the owner on 2026-10-20 with the guided script of
 * docs/dev/tests/multitasking-pa-protocol.md; the `ev` lines keep time, type, code and value, the event names are cut).
 * The events go through the slot tracker and the display manager's legacy transform (normalise each raw axis by its
 * range, swap when the recording says so, scale to the screen) and G1 runs on screen pixels with the density the
 * launcher computes from the panel size. Counted as tools/multitasking-spike/mta_analyse.py counts: a fire whose due
 * time (t3 + hold) lies inside a labelled step is a hit for the hold3* steps and a false positive for every other.
 *
 *   test_g1_replay FILE|- WxH HITS/REPS FALSE_POSITIVES     (the expected numbers; exit 1 when they differ)
 */
#include "cp0_gesture_g1.h"
#include "cp0_touch_slots.h"
#include "cp0_ui_metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEGS 256

typedef struct {
    char label[64];
    int64_t t0, t1;
    int fired;
    char miss[96];
} seg_t;

static seg_t segs[MAX_SEGS];
static int nsegs;

static int seg_open(const char *id, int64_t t)
{
    if (nsegs >= MAX_SEGS) return -1;
    seg_t *s = &segs[nsegs++];
    memset(s, 0, sizeof(*s));
    snprintf(s->label, sizeof(s->label), "%s", id);
    char *dot = strchr(s->label, '.');
    if (dot) *dot = '\0';
    s->t0 = t;
    s->t1 = -1;
    return 0;
}

static void seg_close(const char *id, int64_t t)
{
    char label[64];
    snprintf(label, sizeof(label), "%s", id);
    char *dot = strchr(label, '.');
    if (dot) *dot = '\0';
    for (int i = nsegs - 1; i >= 0; i--)
        if (segs[i].t1 < 0 && strcmp(segs[i].label, label) == 0) {
            segs[i].t1 = t;
            return;
        }
}

static int is_positive(const seg_t *s)
{
    return strncmp(s->label, "hold3", 5) == 0;
}

typedef struct {
    int type, code, value;
    int64_t t;
} rec_ev_t;

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: test_g1_replay FILE|- WxH HITS/REPS FALSE_POSITIVES\n");
        return 2;
    }
    FILE *in = strcmp(argv[1], "-") == 0 ? stdin : fopen(argv[1], "r");
    if (!in) {
        perror(argv[1]);
        return 2;
    }
    int W = 0, H = 0, want_hits = 0, want_reps = 0, want_fp = 0;
    if (sscanf(argv[2], "%dx%d", &W, &H) != 2 || sscanf(argv[3], "%d/%d", &want_hits, &want_reps) != 2) return 2;
    want_fp = atoi(argv[4]);

    int x0 = 0, x1 = 639, y0 = 0, y1 = 479, swap = 0, mt = 1;
    char panel[32] = "", board[32] = "?";
    size_t cap = 1 << 16, n = 0;
    rec_ev_t *ev = malloc(cap * sizeof(*ev));
    char line[512];
    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#') {
            const char *p;
            if ((p = strstr(line, "# range ")) == line) sscanf(p, "# range x0=%d x1=%d y0=%d y1=%d", &x0, &x1, &y0, &y1);
            if (strncmp(line, "# geom ", 7) == 0) {
                if ((p = strstr(line, " swap="))) swap = atoi(p + 6);
                if ((p = strstr(line, " panel_mm="))) sscanf(p + 10, "%31[0-9.x]", panel);
                if ((p = strstr(line, " board="))) sscanf(p + 7, "%31s", board);
            }
            if (strncmp(line, "# mode=", 7) == 0) mt = strncmp(line + 7, "mt-b", 4) == 0;
            continue;
        }
        long long t = 0;
        if (strncmp(line, "ev,", 3) == 0) {
            int type, code, value;
            if (sscanf(line + 3, "%lld,%d,%d,%d", &t, &type, &code, &value) != 4) continue;
            if (n == cap) ev = realloc(ev, (cap *= 2) * sizeof(*ev));
            ev[n++] = (rec_ev_t){type, code, value, t};
        } else if (strncmp(line, "seg,", 4) == 0) {
            char edge[8], id[64];
            if (sscanf(line + 4, "%lld,%7[^,],%63[^,]", &t, edge, id) != 3) continue;
            if (strcmp(edge, "begin") == 0) seg_open(id, t);
            else seg_close(id, t);
        }
    }
    if (in != stdin) fclose(in);
    for (int i = 0; i < nsegs; i++)
        if (segs[i].t1 < 0) segs[i].t1 = n ? ev[n - 1].t : segs[i].t0;

    const cp0_ui_density_t density = cp0_ui_density_resolve(W, H, 0, panel, 0, 0);
    cp0_g1_t g;
    cp0_g1_init(&g, NULL, W, H, density.ppmm_x100);
    cp0_touch_slots_t tr;
    cp0_touch_slots_init(&tr, mt, 0);
    int fires = 0, fires_outside = 0;
    const double spanx = x1 - x0, spany = y1 - y0;
    for (size_t i = 0; i < n; i++) {
        const cp0_touch_ev_t r = cp0_touch_slots_event(&tr, (uint16_t)ev[i].type, (uint16_t)ev[i].code, ev[i].value);
        if (r == CP0_TOUCH_EV_RESYNC) cp0_touch_slots_load(&tr, NULL, NULL, NULL, NULL, 0); /* nothing to read back */
        if (r == CP0_TOUCH_EV_NONE) continue;
        cp0_touch_frame_t f;
        cp0_touch_slots_frame(&tr, ev[i].t, &f);
        for (int k = 0; k < f.n; k++) { /* the display manager's legacy transform */
            double nx = spanx > 0 ? (f.c[k].x - x0) / spanx : 0, ny = spany > 0 ? (f.c[k].y - y0) / spany : 0;
            if (swap) {
                const double tmp = nx;
                nx = ny;
                ny = tmp;
            }
            int px = (int)(nx * (W - 1)), py = (int)(ny * (H - 1));
            f.c[k].x = px < 0 ? 0 : px >= W ? W - 1 : px;
            f.c[k].y = py < 0 ? 0 : py >= H ? H - 1 : py;
        }
        /* the time first (a hold that completed before this frame fires at its due time, as in the analyser), then
         * the frame itself */
        const cp0_g1_event_t both[2] = {cp0_g1_tick(&g, f.t_us), cp0_g1_frame(&g, &f)};
        for (int b = 0; b < 2; b++) {
        const cp0_g1_event_t e = both[b];
        if (e.outcome == CP0_G1_FIRED) {
            fires++;
            int inside = 0;
            for (int s = 0; s < nsegs; s++)
                if (e.t_due_us >= segs[s].t0 && e.t_due_us <= segs[s].t1) {
                    segs[s].fired = 1;
                    inside = 1;
                }
            if (!inside) fires_outside++;
        } else if (e.outcome == CP0_G1_CANCELLED) {
            for (int s = 0; s < nsegs; s++)
                if (is_positive(&segs[s]) && g.t0 >= segs[s].t0 - 300000 && g.t0 <= segs[s].t1 && !segs[s].miss[0])
                    cp0_g1_describe(&g, &e, segs[s].miss, sizeof(segs[s].miss));
        }
        if (e.outcome != CP0_G1_NONE && getenv("G1_REPLAY_VERBOSE")) { /* every report, with the step it fell in */
            char text[192];
            const char *in = "-";
            for (int s = 0; s < nsegs; s++)
                if (f.t_us >= segs[s].t0 && f.t_us <= segs[s].t1 + 500000) in = segs[s].label;
            cp0_g1_describe(&g, &e, text, sizeof(text));
            printf("  %8.3f s  %-11s %s\n", f.t_us / 1e6, in, text);
        }
        }
    }
    free(ev);
    const size_t nevents = n;

    int hits = 0, reps = 0, fp = 0, negs = 0;
    for (int s = 0; s < nsegs; s++) {
        if (is_positive(&segs[s])) {
            reps++;
            hits += segs[s].fired;
        } else {
            negs++;
            fp += segs[s].fired;
        }
    }
    printf("%s: board %s, %dx%d at %d.%02d px/mm, swap %d: G1 hold %d ms: hold3* hit %d/%d, false positives %d/%d "
           "labelled steps, fires between steps %d\n",
           strcmp(argv[1], "-") == 0 ? "(stdin)" : argv[1], board, W, H, density.ppmm_x100 / 100,
           density.ppmm_x100 % 100, swap, g.p.hold_ms, hits, reps, fp, negs, fires_outside);
    for (int s = 0; s < nsegs; s++) {
        if (is_positive(&segs[s]) && !segs[s].fired)
            printf("  missed %-11s at %6.1f s: %s\n", segs[s].label, segs[s].t0 / 1e6,
                   segs[s].miss[0] ? segs[s].miss : "no attempt with three contacts");
        if (!is_positive(&segs[s]) && segs[s].fired)
            printf("  FALSE POSITIVE in %s at %.1f s\n", segs[s].label, segs[s].t0 / 1e6);
    }
    const int ok = hits == want_hits && reps == want_reps && fp == want_fp && fires_outside == 0 && nevents > 0 &&
                   negs > 0;
    printf("%s expected hit %d/%d, false positives %d\n", ok ? "ok  " : "FAIL", want_hits, want_reps, want_fp);
    return ok ? 0 : 1;
}
