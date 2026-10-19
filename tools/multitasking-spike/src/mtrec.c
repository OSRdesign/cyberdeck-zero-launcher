/*
 * SPDX-License-Identifier: MIT
 *
 * mtrec: read-only raw multitouch recorder for study 026 phase P-a.
 *
 * Records every event of the touch screen (protocol B slots, protocol A frames, or a single-touch device) with its
 * kernel timestamp into a CSV file, optionally guides the user through labelled steps ("3 fingers hold", "palm",
 * "grip"...), and prints per-step and per-run statistics: most fingers at once, contacts, short-lived contacts
 * (< 30 ms), closest two fingers, how far a finger moved, TOUCH_MAJOR range. mta_analyse.py turns recordings into
 * the P-a numbers. It never grabs the device (no EVIOCGRAB): the launcher or an app may run at the same time.
 *
 * Run `mtrec --help` for the options.
 */
#define _GNU_SOURCE
#include "evdev_util.h"
#include "mt_track.h"
#include "mta_util.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/input.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define MTREC_VERSION 1
#define MAX_STEPS 64
#define MAX_SEGS 512

typedef struct {
    char id[32];
    double seconds;
    int reps;
    double lift_at; /* seconds into the step when "LIFT" is shown; 0 = none */
    char text[200];
} step_t;

/* The default guided script: the cases study 026 section 5 and risk 3 need (P-a). */
static const step_t k_default_steps[] = {
    {"idle", 5, 1, 0, "Hands OFF the screen. Do not touch anything (ghost baseline)."},
    {"corners", 8, 1, 0, "ONE finger: touch the TOP-LEFT corner, then TOP-RIGHT, then BOTTOM-RIGHT, then BOTTOM-LEFT, "
                          "about 1 s each."},
    {"tap1", 6, 1, 0, "ONE finger: tap anywhere, about once a second."},
    {"drag1", 6, 1, 0, "ONE finger: drag slowly around the screen, as when scrolling a list."},
    {"pinch2", 8, 1, 0, "TWO fingers: pinch in and out a few times, as when zooming a map."},
    {"pan2", 6, 1, 0, "TWO fingers side by side: drag them across the screen together."},
    {"hold3", 3.5, 5, 2.0, "THREE fingertips (index, middle, ring) down TOGETHER, hold STILL until LIFT."},
    {"hold3close", 3.5, 3, 2.0, "THREE fingertips as CLOSE together as is comfortable, hold STILL until LIFT."},
    {"hold3wide", 3.5, 2, 2.0, "THREE fingertips spread WIDE apart, hold STILL until LIFT."},
    {"tap3", 5, 1, 0, "THREE fingers: quick taps (down and straight up), about once a second."},
    {"swipe3", 6, 1, 0, "THREE fingers: swipe UP from the middle, about once every 2 seconds."},
    {"four", 3.5, 2, 2.0, "FOUR fingers down together, hold still until LIFT."},
    {"palm", 4, 3, 3.0, "Lay your PALM flat on the screen until LIFT."},
    {"palmside", 4, 2, 3.0, "Press the SIDE of your hand (little-finger edge) on the screen until LIFT."},
    {"grip", 5, 3, 0, "GRIP the deck as when carrying it: both hands, fingers around the case edges, whole step."},
    {"gripthumbs", 5, 2, 0, "Hold the deck in both hands with both THUMBS resting on the screen edges, whole step."},
    {"osk", 8, 2, 0, "TWO THUMBS typing fast on the LOWER HALF of the screen (where the on-screen keyboard is), "
                     "alternating left and right."},
    {"idle_end", 5, 1, 0, "Hands OFF the screen."},
};

typedef struct {
    char id[40];    /* step id + "." + rep */
    char label[32]; /* step id */
    long long t0, t1;
    int max_active;
    double min_pair_mm;
    mt_stats st; /* contacts that started in the segment, added when they end */
} seg_t;

static volatile sig_atomic_t g_stop;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static FILE *g_out;
static seg_t g_segs[MAX_SEGS];
static int g_nsegs;
static int g_cur_seg = -1;
static mt_tracker g_trk;
static int g_quiet;

static void on_contact_end(void *user, const mt_contact *c)
{
    (void)user;
    if (c->seg >= 0 && c->seg < g_nsegs) mt_stats_add_contact(&g_segs[c->seg].st, c);
}

static void on_frame(void *user, long long t_us, int active, double min_pair)
{
    (void)user;
    (void)t_us;
    if (g_cur_seg < 0) return;
    seg_t *s = &g_segs[g_cur_seg];
    if (active > s->max_active) s->max_active = active;
    if (min_pair >= 0 && (s->min_pair_mm < 0 || min_pair < s->min_pair_mm)) s->min_pair_mm = min_pair;
}

/* ---- clock: the events' clock (monotonic after EVIOCSCLOCKID, else realtime) ---- */
static clockid_t g_clk = CLOCK_MONOTONIC;
static long long g_base_us;
static long long clk_us(void)
{
    struct timespec ts;
    clock_gettime(g_clk, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static void usage(void)
{
    printf(
        "mtrec %d: read-only multitouch recorder (study 026 P-a). Never grabs the device.\n"
        "\n"
        "  mtrec [options]\n"
        "\n"
        "  --dev PATH        touch device (default: the best touch screen by capability, as the launcher picks it)\n"
        "  --list            list the input devices with their touch score, then exit\n"
        "  --out FILE        recording (default ~/mta-results/mtrec-<host>-<date>.csv; '-' = stdout)\n"
        "  --duration S      stop after S seconds (default: the end of the guided script, else Ctrl-C)\n"
        "  --guided[=IDS]    guided mode: every step of the default script, or a comma list of step ids\n"
        "  --steps           print the default guided steps and exit\n"
        "  --script FILE     guided script from a file: lines 'id|seconds|reps|lift_at|instruction'\n"
        "  --reps N          repetitions for the steps that repeat (default: the script's)\n"
        "  --prep S          countdown before each step (default 3)\n"
        "  --gap S           pause after each step (default 1.5)\n"
        "  --wait-enter      wait for Enter on the terminal before each step\n"
        "  --board NAME      deck | pi3a | hackberry: panel size and axis swap preset (default: detected)\n"
        "  --panel-mm WxH    panel size in mm, display orientation (default: launcher env, board.conf, screen.state)\n"
        "  --swap 0|1        1 = raw touch X runs along the display height (the deck)\n"
        "  --raw             --dev is a file or FIFO of raw struct input_event (host tests: no ioctls)\n"
        "  --abs X0,X1,Y0,Y1,SLOTS   ranges and slot count for --raw (default 0,639,0,479,5)\n"
        "  --quiet           no live lines (the summary is still printed)\n"
        "\n"
        "Output: '#' header lines, then 'ev,t_us,type,code,value,name' and 'seg,t_us,begin|end,id,text' lines;\n"
        "t_us is microseconds since the start. Statistics go to stdout and, as '# stat' lines, to the end of the file.\n",
        MTREC_VERSION);
}

static int parse_steps_file(const char *path, step_t *steps, int max)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    int n = 0;
    while (n < max && fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        line[strcspn(line, "\r\n")] = '\0';
        char *fields[5] = {0};
        char *p = line;
        for (int i = 0; i < 5 && p; ++i) {
            fields[i] = p;
            char *bar = i < 4 ? strchr(p, '|') : NULL;
            if (bar) *bar = '\0';
            p = bar ? bar + 1 : NULL;
        }
        if (!fields[4]) continue;
        step_t *s = &steps[n];
        snprintf(s->id, sizeof(s->id), "%s", fields[0]);
        for (char *c = s->id; *c; ++c)
            if (*c == '.' || *c == ',' || *c == ' ') *c = '_';
        s->seconds = atof(fields[1]);
        s->reps = atoi(fields[2]);
        if (s->reps < 1) s->reps = 1;
        s->lift_at = atof(fields[3]);
        snprintf(s->text, sizeof(s->text), "%s", fields[4]);
        for (char *c = s->text; *c; ++c)
            if (*c == ',') *c = ';'; /* keeps the CSV simple */
        if (s->seconds > 0) ++n;
    }
    fclose(f);
    return n;
}

static int select_steps(const char *ids, step_t *steps, int max)
{
    const int total = (int)(sizeof(k_default_steps) / sizeof(k_default_steps[0]));
    if (!ids || !ids[0]) {
        for (int i = 0; i < total && i < max; ++i) steps[i] = k_default_steps[i];
        return total < max ? total : max;
    }
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", ids);
    int n = 0;
    for (char *tok = strtok(buf, ","); tok && n < max; tok = strtok(NULL, ",")) {
        int found = 0;
        for (int i = 0; i < total; ++i)
            if (strcmp(k_default_steps[i].id, tok) == 0) {
                steps[n++] = k_default_steps[i];
                found = 1;
            }
        if (!found) {
            fprintf(stderr, "mtrec: unknown step '%s' (see --steps)\n", tok);
            return -1;
        }
    }
    return n;
}

/* ---- geometry: panel size and axis swap, with where each came from ---- */
typedef struct {
    double mm_w, mm_h;
    int swap;
    char board[32], panel_src[48], swap_src[48];
    int ppmm; /* screen.state ppmm (px per mm x100), 0 unknown */
} geom_info;

static int parse_wxh(const char *s, double *w, double *h)
{
    return sscanf(s, "%lfx%lf", w, h) == 2 && *w > 0 && *h > 0 ? 0 : -1;
}

static void state_file(char *out, size_t n)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    char dflt[64];
    if (!rt || !rt[0]) {
        snprintf(dflt, sizeof(dflt), "/run/user/%d", (int)getuid());
        rt = dflt;
    }
    snprintf(out, n, "%s/applaunch/screen.state", rt);
}

static void detect_geom(geom_info *gi, const char *opt_board, const char *opt_panel, int opt_swap, int rx, int ry)
{
    memset(gi, 0, sizeof(*gi));
    char v[128], fbname[64] = "";
    pid_t lp[4];
    const int nl = mta_find_launcher(lp, 4);
    {
        FILE *f = fopen("/sys/class/graphics/fb0/name", "r");
        if (f) {
            if (fgets(fbname, sizeof(fbname), f)) fbname[strcspn(fbname, "\n")] = '\0';
            fclose(f);
        }
    }
    /* board */
    if (opt_board) snprintf(gi->board, sizeof(gi->board), "%s", opt_board);
    else if (mta_kv_file_get("/etc/applaunch/board.conf", "APPLAUNCH_BOARD", v, sizeof(v)) == 0)
        snprintf(gi->board, sizeof(gi->board), "%s", v);
    else if (strcmp(fbname, "BCM2708 FB") == 0) snprintf(gi->board, sizeof(gi->board), "deck");
    else snprintf(gi->board, sizeof(gi->board), "unknown");
    const int is_deck = strcmp(gi->board, "deck") == 0;
    const int is_3a = strncmp(gi->board, "pi3a", 4) == 0;
    const int is_hb = strncmp(gi->board, "hackberry", 9) == 0;

    /* swap */
    gi->swap = 0;
    snprintf(gi->swap_src, sizeof(gi->swap_src), "default");
    if (opt_swap >= 0) {
        gi->swap = opt_swap;
        snprintf(gi->swap_src, sizeof(gi->swap_src), "option");
    } else if (getenv("APPLAUNCH_TOUCH_SWAP_XY")) {
        gi->swap = atoi(getenv("APPLAUNCH_TOUCH_SWAP_XY")) != 0;
        snprintf(gi->swap_src, sizeof(gi->swap_src), "env");
    } else if (nl > 0 && mta_proc_env_get(lp[0], "APPLAUNCH_TOUCH_SWAP_XY", v, sizeof(v)) == 0) {
        gi->swap = atoi(v) != 0;
        snprintf(gi->swap_src, sizeof(gi->swap_src), "launcher-env");
    } else if (mta_kv_file_get("/etc/applaunch/board.conf", "APPLAUNCH_TOUCH_SWAP_XY", v, sizeof(v)) == 0) {
        gi->swap = atoi(v) != 0;
        snprintf(gi->swap_src, sizeof(gi->swap_src), "board.conf");
    } else if (is_deck) {
        gi->swap = 1;
        snprintf(gi->swap_src, sizeof(gi->swap_src), "preset-deck");
    }

    /* panel size */
    double w = 0, h = 0;
    if (opt_panel && parse_wxh(opt_panel, &w, &h) == 0) {
        snprintf(gi->panel_src, sizeof(gi->panel_src), "option");
    } else if (getenv("APPLAUNCH_PANEL_MM") && parse_wxh(getenv("APPLAUNCH_PANEL_MM"), &w, &h) == 0) {
        snprintf(gi->panel_src, sizeof(gi->panel_src), "env");
    } else if (nl > 0 && mta_proc_env_get(lp[0], "APPLAUNCH_PANEL_MM", v, sizeof(v)) == 0 &&
               parse_wxh(v, &w, &h) == 0) {
        snprintf(gi->panel_src, sizeof(gi->panel_src), "launcher-env");
    } else if (mta_kv_file_get("/etc/applaunch/board.conf", "APPLAUNCH_PANEL_MM", v, sizeof(v)) == 0 &&
               parse_wxh(v, &w, &h) == 0) {
        snprintf(gi->panel_src, sizeof(gi->panel_src), "board.conf");
    } else {
        char sf[PATH_MAX], sw[32], sh[32], sp[32];
        state_file(sf, sizeof(sf));
        if (mta_kv_file_get(sf, "w", sw, sizeof(sw)) == 0 && mta_kv_file_get(sf, "h", sh, sizeof(sh)) == 0 &&
            mta_kv_file_get(sf, "ppmm", sp, sizeof(sp)) == 0 && atoi(sp) > 0) {
            gi->ppmm = atoi(sp);
            w = atof(sw) * 100.0 / gi->ppmm;
            h = atof(sh) * 100.0 / gi->ppmm;
            snprintf(gi->panel_src, sizeof(gi->panel_src), "screen.state");
        } else if (is_deck) {
            w = 56.6, h = 42.5;
            snprintf(gi->panel_src, sizeof(gi->panel_src), "preset-deck");
        } else if (is_3a) {
            w = 94, h = 56;
            snprintf(gi->panel_src, sizeof(gi->panel_src), "preset-pi3a");
        } else if (is_hb) {
            w = 72, h = 72;
            snprintf(gi->panel_src, sizeof(gi->panel_src), "preset-hackberry");
        } else {
            /* last resort: 10 px per mm over the raw ranges */
            w = (gi->swap ? ry : rx) / 10.0;
            h = (gi->swap ? rx : ry) / 10.0;
            snprintf(gi->panel_src, sizeof(gi->panel_src), "guess-10px-per-mm");
        }
    }
    gi->mm_w = w;
    gi->mm_h = h;
}

/* ---- statistics printing ---- */
static void fmt_mm(char *out, size_t n, double v)
{
    if (v < 0) snprintf(out, n, "-");
    else snprintf(out, n, "%.1f", v);
}

static void fmt_major(char *out, size_t n, const mt_stats *s)
{
    if (s->major_max < 0) snprintf(out, n, "-");
    else snprintf(out, n, "%d..%d", s->major_min, s->major_max);
}

static void stat_line(FILE *f, const char *prefix, const char *rec, const char *id, const char *label, long long t0,
                      long long t1, int max_active, double min_pair, const mt_stats *s)
{
    fprintf(f,
            "%srec=%s id=%s label=%s t0_ms=%.0f dur_ms=%.0f max_fingers=%d contacts=%lld short_lt30ms=%lld "
            "min_pair_mm=%.2f max_disp_mm=%.2f max_path_mm=%.2f major_min=%d major_max=%d dropped=%lld\n",
            prefix, rec, id, label, t0 / 1000.0, (t1 - t0) / 1000.0, max_active, s->contacts, s->ghosts, min_pair,
            s->max_disp_mm, s->max_path_mm, s->major_min, s->major_max, s->dropped);
}

static void print_summary(long long t_end)
{
    printf("\n%-16s %7s %6s %7s %8s %10s %10s %10s %9s\n", "segment", "t0_s", "dur_s", "fingers", "contacts",
           "short<30ms", "min_gap_mm", "max_move", "major");
    for (int i = 0; i < g_nsegs; ++i) {
        const seg_t *s = &g_segs[i];
        char gap[16], mv[16], mj[24];
        fmt_mm(gap, sizeof(gap), s->min_pair_mm);
        fmt_mm(mv, sizeof(mv), s->st.contacts ? s->st.max_disp_mm : -1);
        fmt_major(mj, sizeof(mj), &s->st);
        printf("%-16s %7.1f %6.1f %7d %8lld %10lld %10s %10s %9s\n", s->id, s->t0 / 1e6, (s->t1 - s->t0) / 1e6,
               s->max_active, s->st.contacts, s->st.ghosts, gap, mv, mj);
    }
    const mt_stats *t = &g_trk.total;
    char gap[16], mv[16], mj[24];
    fmt_mm(gap, sizeof(gap), t->min_pair_mm);
    fmt_mm(mv, sizeof(mv), t->contacts ? t->max_disp_mm : -1);
    fmt_major(mj, sizeof(mj), t);
    printf("%-16s %7.1f %6.1f %7d %8lld %10lld %10s %10s %9s\n", "TOTAL", 0.0, t_end / 1e6, t->max_active,
           t->contacts, t->ghosts, gap, mv, mj);
    if (t->dropped) printf("SYN_DROPPED seen %lld times: the kernel buffer overflowed (events lost)\n", t->dropped);
    /* machine-readable */
    for (int i = 0; i < g_nsegs; ++i) {
        const seg_t *s = &g_segs[i];
        stat_line(stdout, "mtrec ", "seg", s->id, s->label, s->t0, s->t1, s->max_active, s->min_pair_mm, &s->st);
        if (g_out && g_out != stdout)
            stat_line(g_out, "# stat ", "seg", s->id, s->label, s->t0, s->t1, s->max_active, s->min_pair_mm, &s->st);
    }
    stat_line(stdout, "mtrec ", "total", "total", "total", 0, t_end, t->max_active, t->min_pair_mm, t);
    if (g_out && g_out != stdout)
        stat_line(g_out, "# stat ", "total", "total", "total", 0, t_end, t->max_active, t->min_pair_mm, t);
}

/* the step just ended: what was seen, including contacts still down */
static void print_step_feedback(const seg_t *s)
{
    mt_stats st = s->st;
    for (int i = 0; i < g_trk.nslots; ++i) {
        const mt_contact *c = &g_trk.slot[i];
        if (c->tid >= 0 && c->seg == g_cur_seg) {
            mt_contact tmp = *c;
            tmp.t_end = clk_us() - g_base_us;
            mt_stats_add_contact(&st, &tmp);
        }
    }
    char gap[16], mv[16], mj[24];
    fmt_mm(gap, sizeof(gap), s->min_pair_mm);
    fmt_mm(mv, sizeof(mv), st.contacts ? st.max_disp_mm : -1);
    fmt_major(mj, sizeof(mj), &st);
    printf("   seen: max %d finger(s), %lld contact(s), %lld short (<30 ms), closest %s mm, moved up to %s mm, "
           "major %s\n",
           s->max_active, st.contacts, st.ghosts, gap, mv, mj);
    fflush(stdout);
}

/* ---- guided mode ---- */
enum { G_OFF, G_PROMPT, G_WAIT_ENTER, G_COUNT, G_RUN, G_GAP, G_DONE };
typedef struct {
    step_t steps[MAX_STEPS];
    int nsteps, si, rep, phase, count_left, lifted;
    long long next_us, run_start_us;
    double prep, gap;
    int wait_enter;
} guide_t;

static void seg_begin(guide_t *g, long long t)
{
    if (g_nsegs >= MAX_SEGS) return;
    seg_t *s = &g_segs[g_nsegs];
    memset(s, 0, sizeof(*s));
    const step_t *st = &g->steps[g->si];
    snprintf(s->id, sizeof(s->id), "%s.%d", st->id, g->rep + 1);
    snprintf(s->label, sizeof(s->label), "%s", st->id);
    s->t0 = t;
    s->min_pair_mm = -1;
    mt_stats_reset(&s->st);
    g_cur_seg = g_nsegs++;
    g_trk.seg = g_cur_seg;
    /* fingers put down a little before GO (outside any step) count for this step */
    for (int i = 0; i < g_trk.nslots; ++i)
        if (g_trk.slot[i].tid >= 0 && g_trk.slot[i].seg < 0) g_trk.slot[i].seg = g_cur_seg;
    fprintf(g_out, "seg,%lld,begin,%s,%s\n", t, s->id, st->text);
    fflush(g_out);
}

static void seg_end(long long t)
{
    if (g_cur_seg < 0) return;
    seg_t *s = &g_segs[g_cur_seg];
    s->t1 = t;
    fprintf(g_out, "seg,%lld,end,%s,\n", t, s->id);
    fflush(g_out);
    print_step_feedback(s);
    g_cur_seg = -1;
    g_trk.seg = -1;
}

/* advances the guide; returns the next deadline (absolute, clk us) or -1 */
static long long guide_step(guide_t *g, long long now_abs)
{
    const long long rel = now_abs - g_base_us;
    while (g->phase != G_DONE && g->phase != G_OFF && g->phase != G_WAIT_ENTER && now_abs >= g->next_us) {
        const step_t *st = &g->steps[g->si];
        switch (g->phase) {
        case G_PROMPT:
            printf("\n[%d/%d] %s  (step '%s', %d of %d, %.1f s)\n", g->si + 1, g->nsteps, st->text, st->id, g->rep + 1,
                   st->reps, st->seconds);
            fflush(stdout);
            if (g->wait_enter) {
                printf("   press Enter when ready...\n");
                fflush(stdout);
                g->phase = G_WAIT_ENTER;
                return -1;
            }
            g->phase = G_COUNT;
            g->count_left = (int)ceil(g->prep);
            g->next_us = now_abs;
            break;
        case G_COUNT:
            if (g->count_left > 0) {
                printf("   %d...\n", g->count_left);
                fflush(stdout);
                g->count_left--;
                g->next_us = now_abs + (long long)(g->prep * 1e6 / ceil(g->prep > 0 ? g->prep : 1));
            } else {
                printf("   GO\n");
                fflush(stdout);
                seg_begin(g, rel);
                g->run_start_us = now_abs;
                g->lifted = 0;
                g->phase = G_RUN;
                g->next_us = st->lift_at > 0 ? now_abs + (long long)(st->lift_at * 1e6)
                                             : now_abs + (long long)(st->seconds * 1e6);
            }
            break;
        case G_RUN:
            if (st->lift_at > 0 && !g->lifted) {
                printf("   LIFT\n");
                fflush(stdout);
                fprintf(g_out, "note,%lld,lift %s\n", rel, g_cur_seg >= 0 ? g_segs[g_cur_seg].id : "");
                g->lifted = 1;
                g->next_us = g->run_start_us + (long long)(st->seconds * 1e6);
                if (g->next_us > now_abs) break;
            }
            printf("   STOP: hands off\n");
            seg_end(rel);
            g->phase = G_GAP;
            g->next_us = now_abs + (long long)(g->gap * 1e6);
            break;
        case G_GAP:
            if (++g->rep >= st->reps) {
                g->rep = 0;
                if (++g->si >= g->nsteps) {
                    g->phase = G_DONE;
                    printf("\nguided script finished\n");
                    fflush(stdout);
                    return -1;
                }
            }
            g->phase = G_PROMPT;
            g->next_us = now_abs;
            break;
        default: break;
        }
    }
    return (g->phase == G_DONE || g->phase == G_OFF || g->phase == G_WAIT_ENTER) ? -1 : g->next_us;
}

int main(int argc, char **argv)
{
    const char *dev = NULL, *out_path = NULL, *guided_ids = NULL, *script = NULL, *opt_board = NULL,
               *opt_panel = NULL, *abs_spec = NULL;
    double duration = 0, prep = 3, gap = 1.5;
    int guided = 0, reps = 0, raw = 0, opt_swap = -1, wait_enter = 0;
    enum { O_LIST = 1000, O_GUIDED, O_STEPS, O_SCRIPT, O_REPS, O_PREP, O_GAP, O_WAIT, O_BOARD, O_PANEL, O_SWAP, O_RAW,
           O_ABS, O_QUIET, O_DUR };
    static const struct option opts[] = {
        {"dev", required_argument, 0, 'd'},       {"out", required_argument, 0, 'o'},
        {"list", no_argument, 0, O_LIST},         {"duration", required_argument, 0, O_DUR},
        {"guided", optional_argument, 0, O_GUIDED}, {"steps", no_argument, 0, O_STEPS},
        {"script", required_argument, 0, O_SCRIPT}, {"reps", required_argument, 0, O_REPS},
        {"prep", required_argument, 0, O_PREP},   {"gap", required_argument, 0, O_GAP},
        {"wait-enter", no_argument, 0, O_WAIT},   {"board", required_argument, 0, O_BOARD},
        {"panel-mm", required_argument, 0, O_PANEL}, {"swap", required_argument, 0, O_SWAP},
        {"raw", no_argument, 0, O_RAW},           {"abs", required_argument, 0, O_ABS},
        {"quiet", no_argument, 0, O_QUIET},       {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};
    int c;
    while ((c = getopt_long(argc, argv, "d:o:h", opts, NULL)) != -1) {
        switch (c) {
        case 'd': dev = optarg; break;
        case 'o': out_path = optarg; break;
        case O_LIST: ev_print_inputs(); return 0;
        case O_DUR: duration = atof(optarg); break;
        case O_GUIDED: guided = 1; guided_ids = optarg; break;
        case O_STEPS:
            for (size_t i = 0; i < sizeof(k_default_steps) / sizeof(k_default_steps[0]); ++i)
                printf("%-11s %4.1f s x%d%s  %s\n", k_default_steps[i].id, k_default_steps[i].seconds,
                       k_default_steps[i].reps, k_default_steps[i].lift_at > 0 ? " (LIFT prompt)" : "",
                       k_default_steps[i].text);
            return 0;
        case O_SCRIPT: script = optarg; guided = 1; break;
        case O_REPS: reps = atoi(optarg); break;
        case O_PREP: prep = atof(optarg); break;
        case O_GAP: gap = atof(optarg); break;
        case O_WAIT: wait_enter = 1; break;
        case O_BOARD: opt_board = optarg; break;
        case O_PANEL: opt_panel = optarg; break;
        case O_SWAP: opt_swap = atoi(optarg) != 0; break;
        case O_RAW: raw = 1; break;
        case O_ABS: abs_spec = optarg; break;
        case O_QUIET: g_quiet = 1; break;
        case 'h': usage(); return 0;
        default: usage(); return 2;
        }
    }

    /* ---- the device ---- */
    char dev_path[PATH_MAX] = "", dev_name[128] = "";
    if (dev) snprintf(dev_path, sizeof(dev_path), "%s", dev);
    else if (raw) {
        fprintf(stderr, "mtrec: --raw needs --dev FILE\n");
        return 2;
    } else if (ev_find_touch(dev_path, sizeof(dev_path), dev_name, sizeof(dev_name)) != 0) {
        fprintf(stderr, "mtrec: no touch screen found under /dev/input (see --list; are you in group 'input'?)\n");
        return 1;
    }
    /* read-only; never EVIOCGRAB */
    int fd = open(dev_path, O_RDONLY | O_CLOEXEC | (raw ? 0 : O_NONBLOCK));
    if (fd < 0) {
        fprintf(stderr, "mtrec: open %s: %s\n", dev_path, strerror(errno));
        return 1;
    }
    int mode = MT_MODE_B, nslots = 5;
    int rx0 = 0, rx1 = 639, ry0 = 0, ry1 = 479;
    char props[64] = "";
    struct input_id iid = {0};
    struct {
        int code;
        struct input_absinfo ai;
        int ok;
    } axes[] = {{ABS_X, {0}, 0},
                {ABS_Y, {0}, 0},
                {ABS_PRESSURE, {0}, 0},
                {ABS_MT_SLOT, {0}, 0},
                {ABS_MT_TOUCH_MAJOR, {0}, 0},
                {ABS_MT_WIDTH_MAJOR, {0}, 0},
                {ABS_MT_POSITION_X, {0}, 0},
                {ABS_MT_POSITION_Y, {0}, 0},
                {ABS_MT_TRACKING_ID, {0}, 0},
                {ABS_MT_PRESSURE, {0}, 0}};
    const int naxes = (int)(sizeof(axes) / sizeof(axes[0]));
    if (raw) {
        if (abs_spec && sscanf(abs_spec, "%d,%d,%d,%d,%d", &rx0, &rx1, &ry0, &ry1, &nslots) < 4) {
            fprintf(stderr, "mtrec: --abs X0,X1,Y0,Y1[,SLOTS]\n");
            return 2;
        }
        mode = nslots > 1 ? MT_MODE_B : MT_MODE_SINGLE;
        snprintf(dev_name, sizeof(dev_name), "raw stream");
    } else {
        if (!dev_name[0] && ioctl(fd, EVIOCGNAME(sizeof(dev_name) - 1), dev_name) < 0) dev_name[0] = '\0';
        (void)ioctl(fd, EVIOCGID, &iid);
        unsigned long absbits[(ABS_CNT + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))] = {0};
        unsigned long propbits[1] = {0};
        (void)ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
        (void)ioctl(fd, EVIOCGPROP(sizeof(propbits)), propbits);
#define HASABS(b) ((absbits[(b) / (8 * sizeof(unsigned long))] >> ((b) % (8 * sizeof(unsigned long)))) & 1UL)
        for (int i = 0; i < naxes; ++i)
            if (HASABS(axes[i].code) && ioctl(fd, EVIOCGABS(axes[i].code), &axes[i].ai) == 0) axes[i].ok = 1;
        const int has_slot = HASABS(ABS_MT_SLOT), has_mt = HASABS(ABS_MT_POSITION_X) && HASABS(ABS_MT_POSITION_Y);
#undef HASABS
        mode = has_slot ? MT_MODE_B : (has_mt ? MT_MODE_A : MT_MODE_SINGLE);
        const int xi = mode == MT_MODE_SINGLE ? 0 : 6, yi = mode == MT_MODE_SINGLE ? 1 : 7;
        if (axes[xi].ok) { rx0 = axes[xi].ai.minimum; rx1 = axes[xi].ai.maximum; }
        if (axes[yi].ok) { ry0 = axes[yi].ai.minimum; ry1 = axes[yi].ai.maximum; }
        nslots = (mode == MT_MODE_B && axes[3].ok) ? axes[3].ai.maximum + 1 : 1;
        snprintf(props, sizeof(props), "%s%s", (propbits[0] & (1UL << INPUT_PROP_DIRECT)) ? "direct" : "",
                 (propbits[0] & (1UL << INPUT_PROP_POINTER)) ? ",pointer" : "");
        /* event timestamps on CLOCK_MONOTONIC for this reader only (the guide's marks use the same clock) */
        int clk = CLOCK_MONOTONIC;
        if (ioctl(fd, EVIOCSCLOCKID, &clk) != 0) g_clk = CLOCK_REALTIME;
    }
    if (nslots > MT_MAX_SLOTS) nslots = MT_MAX_SLOTS;

    geom_info gi;
    detect_geom(&gi, opt_board, opt_panel, opt_swap, rx1 - rx0 + 1, ry1 - ry0 + 1);

    /* ---- guided script ---- */
    static guide_t gd;
    memset(&gd, 0, sizeof(gd));
    gd.phase = G_OFF;
    if (guided) {
        gd.nsteps = script ? parse_steps_file(script, gd.steps, MAX_STEPS) : select_steps(guided_ids, gd.steps, MAX_STEPS);
        if (gd.nsteps <= 0) {
            fprintf(stderr, "mtrec: no guided steps (%s)\n", script ? script : "ids");
            return 2;
        }
        if (reps > 0)
            for (int i = 0; i < gd.nsteps; ++i)
                if (gd.steps[i].reps > 1) gd.steps[i].reps = reps;
        gd.prep = prep < 0 ? 0 : prep;
        gd.gap = gap < 0 ? 0 : gap;
        gd.wait_enter = wait_enter;
        gd.phase = G_PROMPT;
    }

    /* ---- output ---- */
    char out_buf[PATH_MAX];
    if (!out_path) {
        mta_default_out(out_buf, sizeof(out_buf), "mtrec", "csv");
        out_path = out_buf;
    }
    g_out = strcmp(out_path, "-") == 0 ? stdout : fopen(out_path, "w");
    if (!g_out) {
        fprintf(stderr, "mtrec: cannot write %s: %s\n", out_path, strerror(errno));
        return 1;
    }
    if (g_out == stdout) g_quiet = 1;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    char host[64], stamp[64];
    mta_hostname(host, sizeof(host));
    struct utsname un;
    uname(&un);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S%z", &tm);
    pid_t lp[4];
    const int nl = mta_find_launcher(lp, 4);
    const char *mode_s = mode == MT_MODE_B ? "mt-b" : (mode == MT_MODE_A ? "mt-a" : "single");

    fprintf(g_out, "# mtrec v%d\n", MTREC_VERSION);
    fprintf(g_out, "# host=%s kernel=%s started=%s clock=%s\n", host, un.release, stamp,
            raw ? "raw" : (g_clk == CLOCK_MONOTONIC ? "monotonic" : "realtime"));
    fprintf(g_out, "# device path=%s bus=0x%x vendor=0x%x product=0x%x name=%s\n", dev_path, iid.bustype, iid.vendor,
            iid.product, dev_name);
    fprintf(g_out, "# mode=%s slots=%d props=%s\n", mode_s, nslots, props[0] ? props : "none");
    for (int i = 0; i < naxes; ++i) {
        if (!axes[i].ok) continue;
        char nm[64];
        ev_name(EV_ABS, axes[i].code, nm, sizeof(nm));
        fprintf(g_out, "# abs code=0x%x name=%s min=%d max=%d fuzz=%d flat=%d res=%d\n", axes[i].code, nm + 7,
                axes[i].ai.minimum, axes[i].ai.maximum, axes[i].ai.fuzz, axes[i].ai.flat, axes[i].ai.resolution);
    }
    fprintf(g_out, "# range x0=%d x1=%d y0=%d y1=%d\n", rx0, rx1, ry0, ry1);
    fprintf(g_out, "# geom board=%s panel_mm=%.1fx%.1f panel_src=%s swap=%d swap_src=%s ppmm=%d\n", gi.board, gi.mm_w,
            gi.mm_h, gi.panel_src, gi.swap, gi.swap_src, gi.ppmm);
    fprintf(g_out, "# launcher=%s", nl ? "running" : "stopped");
    for (int i = 0; i < nl; ++i) fprintf(g_out, " pid=%d", (int)lp[i]);
    fprintf(g_out, "\n# columns ev,t_us,type,code,value,name | seg,t_us,begin|end,id,text | note,t_us,text\n");
    fflush(g_out);

    printf("mtrec: %s \"%s\" %s, %d slot(s), x %d..%d y %d..%d\n", dev_path, dev_name, mode_s, nslots, rx0, rx1, ry0,
           ry1);
    printf("mtrec: panel %.1f x %.1f mm (%s), swap %d (%s), board %s, launcher %s\n", gi.mm_w, gi.mm_h, gi.panel_src,
           gi.swap, gi.swap_src, gi.board, nl ? "RUNNING (it also sees these touches)" : "stopped");
    printf("mtrec: writing %s%s\n", out_path, guided ? "" : "  (Ctrl-C to stop)");
    if (mode == MT_MODE_A) printf("mtrec: protocol A device: contact tracking is limited to counts per frame\n");
    fflush(stdout);

    mt_geom geo = {rx0, rx1, ry0, ry1, gi.mm_w, gi.mm_h, gi.swap};
    mt_init(&g_trk, &geo, mode, nslots);
    g_trk.on_end = on_contact_end;
    g_trk.on_frame = on_frame;

    g_base_us = clk_us();
    long long raw_base = LLONG_MIN;
    const long long end_abs = duration > 0 ? g_base_us + (long long)(duration * 1e6) : -1;
    if (gd.phase != G_OFF) gd.next_us = g_base_us;

    long long last_live = 0;
    int last_live_active = -1;
    long long t_last_rel = 0;
    struct input_event evs[64];
    while (!g_stop) {
        const long long now_abs = clk_us();
        if (end_abs > 0 && now_abs >= end_abs) break;
        long long deadline = guide_step(&gd, now_abs);
        if (gd.phase == G_DONE) break;
        if (end_abs > 0 && (deadline < 0 || end_abs < deadline)) deadline = end_abs;
        int timeout = 200;
        if (deadline > 0) {
            const long long left = (deadline - now_abs) / 1000;
            timeout = left < 0 ? 0 : (left > 200 ? 200 : (int)left);
        }
        struct pollfd pfd[2] = {{fd, POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
        const int nfds = gd.phase == G_WAIT_ENTER ? 2 : 1;
        const int pr = poll(pfd, (nfds_t)nfds, timeout);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (nfds == 2 && (pfd[1].revents & (POLLIN | POLLHUP))) {
            char line[64];
            if (!fgets(line, sizeof(line), stdin)) gd.wait_enter = 0;
            gd.phase = G_COUNT;
            gd.count_left = (int)ceil(gd.prep);
            gd.next_us = clk_us();
        }
        if (!(pfd[0].revents & (POLLIN | POLLHUP | POLLERR))) continue;
        const ssize_t r = read(fd, evs, sizeof(evs));
        if (r == 0) break; /* end of a raw file or FIFO */
        if (r < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            fprintf(stderr, "mtrec: read: %s\n", strerror(errno));
            break;
        }
        const int n = (int)(r / (ssize_t)sizeof(evs[0]));
        for (int i = 0; i < n; ++i) {
            const struct input_event *e = &evs[i];
            long long ev_us = (long long)e->input_event_sec * 1000000LL + (long long)e->input_event_usec;
            long long rel;
            if (raw) {
                /* synthetic streams: monotonic timestamps of the writer, or anything else (relative to the first) */
                if (raw_base == LLONG_MIN) raw_base = llabs(ev_us - g_base_us) < 86400LL * 1000000LL ? g_base_us : ev_us;
                rel = ev_us - raw_base;
            } else {
                rel = ev_us - g_base_us;
            }
            t_last_rel = rel;
            char nm[64];
            ev_name(e->type, e->code, nm, sizeof(nm));
            fprintf(g_out, "ev,%lld,%d,%d,%d,%s\n", rel, e->type, e->code, e->value, nm);
            mt_feed(&g_trk, rel, e->type, e->code, e->value);
        }
        if (!g_quiet && gd.phase == G_OFF) {
            const int active = mt_active(&g_trk);
            const long long t = clk_us();
            if (active != last_live_active || t - last_live > 500000) {
                printf("\r  fingers now %d | max %d | contacts %lld | short %lld   ", active, g_trk.total.max_active,
                       g_trk.total.contacts, g_trk.total.ghosts);
                fflush(stdout);
                last_live = t;
                last_live_active = active;
            }
        }
    }
    long long t_end = raw ? t_last_rel : clk_us() - g_base_us;
    if (t_end < t_last_rel) t_end = t_last_rel;
    if (g_cur_seg >= 0) seg_end(t_end);
    mt_finish(&g_trk, t_end);
    if (g_stop) fprintf(g_out, "note,%lld,stopped by a signal\n", t_end);
    print_summary(t_end);
    if (g_out != stdout) fclose(g_out);
    close(fd);
    printf("mtrec: saved %s\n", out_path);
    return 0;
}
