/*
 * SPDX-License-Identifier: MIT
 *
 * mtplay: a scripted multitouch panel for the multitasking P-c board protocol (docs/dev/tests/multitasking-pc-protocol.md,
 * part S). It creates a uinput protocol B touch screen with the real panel's ranges, prints its event node (start the
 * launcher with APPLAUNCH_TOUCH_DEV=<that node>), then plays touch patterns read from stdin or --script: a three-finger
 * hold, the ways it must NOT fire (fingers too close, too slow, moving, lifted early, a fourth finger, a pinch), a
 * long-press on the status strip. Positions are millimetres on the glass as the user sees it (origin top left), turned
 * into raw units with the panel size and the axis swap / mirror of the board (as the launcher's legacy touch path
 * reads them).
 *
 * --dry-run writes the events as an mtrec recording instead (stdout; simulated time, no uinput): the host tests
 * replay it through the launcher's G1 (cp0_lvgl tests/test_g1_replay.c) to check every pattern does what it says.
 *
 * Run `mtplay --help` for the options.
 */
#define _GNU_SOURCE
#include "mta_util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define SLOTS 5

static struct {
    int x0, x1, y0, y1; /* raw ranges */
    double mm_w, mm_h;  /* panel in display orientation */
    int swap, inv_x, inv_y;
    char board[32];
} G = {0, 639, 0, 479, 56.6, 42.5, 1, 0, 0, "deck"};

static int g_fd = -1;           /* uinput, or -1 in --dry-run */
static long long g_t_us;        /* dry run: simulated time */
static int g_dry;
static int g_next_id = 1;
static int g_down[SLOTS];       /* tracking id per slot, 0 = up */
static int g_seg;

static void emit(int type, int code, int value)
{
    if (g_dry) {
        printf("ev,%lld,%d,%d,%d\n", g_t_us, type, code, value);
        return;
    }
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = (unsigned short)type;
    ev.code = (unsigned short)code;
    ev.value = value;
    if (write(g_fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) perror("mtplay: write");
}

static void wait_ms(double ms)
{
    if (g_dry) g_t_us += (long long)(ms * 1000);
    else mta_sleep_ms(ms, NULL);
}

static long long now_us(void)
{
    return g_dry ? g_t_us : mta_mono_us();
}

/* display mm -> raw, the inverse of the launcher's legacy transform (normalise, swap, mirror) */
static void to_raw(double mx, double my, int *rx, int *ry)
{
    double nx = mx / G.mm_w, ny = my / G.mm_h;
    if (nx < 0) nx = 0;
    if (nx > 1) nx = 1;
    if (ny < 0) ny = 0;
    if (ny > 1) ny = 1;
    if (G.inv_x) nx = 1.0 - nx;
    if (G.inv_y) ny = 1.0 - ny;
    if (G.swap) {
        const double t = nx;
        nx = ny;
        ny = t;
    }
    *rx = G.x0 + (int)lround(nx * (G.x1 - G.x0));
    *ry = G.y0 + (int)lround(ny * (G.y1 - G.y0));
}

typedef struct {
    int slot;
    double x, y; /* mm; x < 0 = lift */
} touch_t;

/* one frame: the given contacts land / move (x >= 0) or lift (x < 0), then SYN_REPORT */
static void frame(const touch_t *t, int n)
{
    int before = 0, after = 0;
    for (int s = 0; s < SLOTS; s++) before += g_down[s] != 0;
    for (int i = 0; i < n; i++) {
        const int s = t[i].slot;
        emit(EV_ABS, ABS_MT_SLOT, s);
        if (t[i].x < 0) {
            if (g_down[s]) emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
            g_down[s] = 0;
            continue;
        }
        if (!g_down[s]) {
            g_down[s] = g_next_id++;
            emit(EV_ABS, ABS_MT_TRACKING_ID, g_down[s]);
            emit(EV_ABS, ABS_MT_TOUCH_MAJOR, 48);
        }
        int rx, ry;
        to_raw(t[i].x, t[i].y, &rx, &ry);
        emit(EV_ABS, ABS_MT_POSITION_X, rx);
        emit(EV_ABS, ABS_MT_POSITION_Y, ry);
    }
    for (int s = 0; s < SLOTS; s++) after += g_down[s] != 0;
    if (!before && after) emit(EV_KEY, BTN_TOUCH, 1);
    if (before && !after) emit(EV_KEY, BTN_TOUCH, 0);
    emit(EV_SYN, SYN_REPORT, 0);
}

static void lift_all(void)
{
    touch_t t[SLOTS];
    int n = 0;
    for (int s = 0; s < SLOTS; s++)
        if (g_down[s]) t[n++] = (touch_t){s, -1, -1};
    if (n) frame(t, n);
}

/* n fingers at (x[i], y[i]) landing land_ms apart, held hold_ms with a small tremor (0.3 mm), moving finger
 * `mover` by `move_mm` along x halfway through (-1: none), lifting finger `early` at early_ms (-1: none) */
static void hand(int n, const double *x, const double *y, double land_ms, double hold_ms, int mover, double move_mm,
                 int early, double early_ms)
{
    for (int i = 0; i < n; i++) {
        frame(&(touch_t){i, x[i], y[i]}, 1);
        if (i + 1 < n) wait_ms(land_ms);
    }
    const double step = 50;
    int lifted = 0;
    for (double t = step; t <= hold_ms; t += step) {
        wait_ms(step);
        touch_t f[SLOTS];
        int k = 0;
        for (int i = 0; i < n; i++) {
            if (lifted && i == early) continue;
            const double jitter = ((int)(t / step) % 2 ? 0.3 : -0.3);
            const double dx = (i == mover && t >= hold_ms / 2) ? move_mm : 0;
            f[k++] = (touch_t){i, x[i] + jitter + dx, y[i] - jitter};
        }
        if (!lifted && early >= 0 && t >= early_ms) {
            f[k++] = (touch_t){early, -1, -1};
            lifted = 1;
        }
        frame(f, k);
    }
    wait_ms(30);
    lift_all();
}

static void seg_begin(const char *label, const char *text)
{
    if (g_dry) printf("seg,%lld,begin,%s.%d,%s\n", g_t_us, label, ++g_seg, text);
}

static void seg_end(const char *label)
{
    if (g_dry) printf("seg,%lld,end,%s.%d,\n", g_t_us, label, g_seg);
}

/* one command line; 0 = go on, 1 = quit, -1 = unknown */
static int run(char *line)
{
    char *argv[8];
    int argc = 0;
    for (char *tok = strtok(line, " \t\r\n"); tok && argc < 8; tok = strtok(NULL, " \t\r\n")) argv[argc++] = tok;
    if (argc == 0 || argv[0][0] == '#') return 0;
    const char *c = argv[0];
    const double cx = G.mm_w / 2, cy = G.mm_h / 2;
    const double arg1 = argc > 1 ? atof(argv[1]) : -1;
    const long long t0 = now_us();
    if (strcmp(c, "quit") == 0 || strcmp(c, "q") == 0) return 1;
    if (strcmp(c, "wait") == 0) {
        wait_ms(arg1 > 0 ? arg1 : 1000);
        return 0;
    }
    const char *label = c;
    const char *expect = "no G1";
    if (strcmp(c, "g1") == 0) { /* three fingers 15 mm apart, down within 50 ms, held 1.5 s (or arg ms) */
        label = "hold3";
        expect = "G1 fired";
        seg_begin(label, "g1");
        const double x[3] = {cx - 15, cx, cx + 15}, y[3] = {cy, cy + 1, cy - 1};
        hand(3, x, y, 25, arg1 > 0 ? arg1 : 1500, -1, 0, -1, 0);
    } else if (strcmp(c, "g1short") == 0) { /* lifted after 600 ms */
        expect = "G1 cancelled: a finger lifted";
        seg_begin(label, c);
        const double x[3] = {cx - 15, cx, cx + 15}, y[3] = {cy, cy + 1, cy - 1};
        hand(3, x, y, 25, 600, -1, 0, -1, 0);
    } else if (strcmp(c, "g1close") == 0) { /* two fingers 6 mm apart */
        expect = "G1 cancelled: two fingers";
        seg_begin(label, c);
        const double x[3] = {cx - 15, cx, cx + 6}, y[3] = {cy, cy, cy};
        hand(3, x, y, 25, 1500, -1, 0, -1, 0);
    } else if (strcmp(c, "g1slow") == 0) { /* the third finger 400 ms after the first */
        expect = "G1 cancelled: the third finger came";
        seg_begin(label, c);
        const double x[3] = {cx - 15, cx, cx + 15}, y[3] = {cy, cy + 1, cy - 1};
        hand(3, x, y, 200, 1500, -1, 0, -1, 0);
    } else if (strcmp(c, "g1move") == 0) { /* one finger slides 6 mm during the hold */
        expect = "G1 cancelled: a finger moved";
        seg_begin(label, c);
        const double x[3] = {cx - 15, cx, cx + 15}, y[3] = {cy, cy + 1, cy - 1};
        hand(3, x, y, 25, 1500, 1, 6, -1, 0);
    } else if (strcmp(c, "g1edge") == 0) { /* a finger 1 mm from the left edge */
        expect = "G1 cancelled: a finger";
        seg_begin(label, c);
        const double x[3] = {1, 16, 31}, y[3] = {cy, cy + 1, cy - 1};
        hand(3, x, y, 25, 1500, -1, 0, -1, 0);
    } else if (strcmp(c, "four") == 0) { /* four fingers held 1.5 s */
        expect = "G1 cancelled: 4 contacts";
        seg_begin(label, c);
        const double x[4] = {cx - 20, cx - 7, cx + 7, cx + 20}, y[4] = {cy, cy, cy, cy};
        hand(4, x, y, 25, 1500, -1, 0, -1, 0);
    } else if (strcmp(c, "pinch") == 0) { /* two fingers spreading from 20 to 40 mm over 1 s */
        seg_begin(label, c);
        for (int i = 0; i <= 20; i++) {
            const double half = 10 + i * 0.5;
            const touch_t f[2] = {{0, cx - half, cy}, {1, cx + half, cy}};
            frame(f, 2);
            wait_ms(50);
        }
        lift_all();
    } else if (strcmp(c, "tap") == 0 && argc >= 3) {
        seg_begin(label, c);
        frame(&(touch_t){0, atof(argv[1]), atof(argv[2])}, 1);
        wait_ms(80);
        lift_all();
    } else if ((strcmp(c, "hold") == 0 && argc >= 3) || strcmp(c, "strip") == 0) {
        /* one finger held still; "strip" = on the status strip (top right: the clock), arg = ms (default 800) */
        const int strip = strcmp(c, "strip") == 0;
        const double x = strip ? G.mm_w - 8 : atof(argv[1]), y = strip ? 2.5 : atof(argv[2]);
        const double ms = strip ? (arg1 > 0 ? arg1 : 800) : (argc > 3 ? atof(argv[3]) : 800);
        expect = strip && ms >= 500 ? "G2 fired" : "no G1";
        seg_begin(label, c);
        frame(&(touch_t){0, x, y}, 1);
        for (double t = 50; t <= ms; t += 50) {
            wait_ms(50);
            frame(&(touch_t){0, x + 0.2, y}, 1);
        }
        lift_all();
    } else {
        return -1;
    }
    seg_end(label);
    if (!g_dry)
        printf("mtplay: %s done in %.0f ms (launcher log: %s)\n", c, (now_us() - t0) / 1000.0, expect);
    fflush(stdout);
    wait_ms(400); /* a pause between patterns: the hand is off the glass */
    return 0;
}

/* /dev/input/eventN of the uinput device (sysfs: /sys/devices/virtual/input/<sysname>/eventN) */
static int event_node(char *out, size_t n)
{
    char sys[64] = "", dir[160];
    if (ioctl(g_fd, UI_GET_SYSNAME(sizeof(sys)), sys) < 0) return -1;
    snprintf(dir, sizeof(dir), "/sys/devices/virtual/input/%s", sys);
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    int found = -1;
    while ((e = readdir(d)) != NULL)
        if (strncmp(e->d_name, "event", 5) == 0) {
            snprintf(out, n, "/dev/input/%s", e->d_name);
            found = 0;
        }
    closedir(d);
    return found;
}

static int create_uinput(void)
{
    g_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (g_fd < 0) {
        fprintf(stderr, "mtplay: /dev/uinput: %s (run it with sudo)\n", strerror(errno));
        return -1;
    }
    ioctl(g_fd, UI_SET_EVBIT, EV_SYN);
    ioctl(g_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(g_fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(g_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(g_fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    const struct {
        int code, min, max;
    } axes[] = {{ABS_MT_SLOT, 0, SLOTS - 1},          {ABS_MT_TRACKING_ID, 0, 65535},
                {ABS_MT_POSITION_X, G.x0, G.x1},      {ABS_MT_POSITION_Y, G.y0, G.y1},
                {ABS_MT_TOUCH_MAJOR, 0, 255},         {ABS_X, G.x0, G.x1},
                {ABS_Y, G.y0, G.y1}};
    for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
        ioctl(g_fd, UI_SET_ABSBIT, axes[i].code);
        struct uinput_abs_setup a;
        memset(&a, 0, sizeof(a));
        a.code = (unsigned short)axes[i].code;
        a.absinfo.minimum = axes[i].min;
        a.absinfo.maximum = axes[i].max;
        if (ioctl(g_fd, UI_ABS_SETUP, &a) < 0) {
            fprintf(stderr, "mtplay: UI_ABS_SETUP: %s\n", strerror(errno));
            return -1;
        }
    }
    struct uinput_setup us;
    memset(&us, 0, sizeof(us));
    us.id.bustype = BUS_VIRTUAL;
    us.id.vendor = 0x1209;
    us.id.product = 0x0c0d;
    us.id.version = 1;
    snprintf(us.name, sizeof(us.name), "mtplay touch panel");
    if (ioctl(g_fd, UI_DEV_SETUP, &us) < 0 || ioctl(g_fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "mtplay: creating the device: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* the real panel's raw ranges */
static void like(const char *dev)
{
    const int fd = open(dev, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "mtplay: --like %s: %s (keeping %d..%d x %d..%d)\n", dev, strerror(errno), G.x0, G.x1, G.y0,
                G.y1);
        return;
    }
    struct input_absinfo ai;
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0) G.x0 = ai.minimum, G.x1 = ai.maximum;
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0) G.y0 = ai.minimum, G.y1 = ai.maximum;
    close(fd);
}

static void usage(void)
{
    printf("usage: mtplay [--board deck|pi3a] [--like /dev/input/eventN] [--range XMAXxYMAX] [--panel-mm WxH]\n"
           "              [--swap 0|1] [--invert-x 0|1] [--invert-y 0|1] [--script \"CMD; CMD; ...\"] [--dry-run]\n"
           "\n"
           "Creates a uinput touch panel (5 slots, protocol B), prints its node, then plays commands (stdin, one per\n"
           "line, or --script); positions in mm on the glass from the top-left corner as you see it:\n"
           "  g1 [MS]     three fingers 15 mm apart, down within 50 ms, held 1500 ms (or MS)   -> G1 fires\n"
           "  g1short     the same, lifted after 600 ms                                       -> cancelled: lifted\n"
           "  g1close     two of the fingers 6 mm apart                                       -> cancelled: too close\n"
           "  g1slow      the third finger 400 ms after the first                             -> cancelled: too slow\n"
           "  g1move      one finger slides 6 mm during the hold                              -> cancelled: moved\n"
           "  g1edge      a finger 1 mm from the left edge                                    -> cancelled: edge\n"
           "  four        four fingers held 1.5 s                                             -> cancelled: 4 contacts\n"
           "  pinch       two fingers spreading 20 -> 40 mm over 1 s                          -> nothing\n"
           "  strip [MS]  one finger on the status strip (top right) for 800 ms (or MS)       -> G2 fires (>= 500)\n"
           "  hold X Y [MS], tap X Y, wait MS, quit\n"
           "Geometry: options, else /etc/applaunch/board.conf (APPLAUNCH_PANEL_MM, _TOUCH_SWAP_XY, _INVERT_X/_Y),\n"
           "else the --board preset (deck: 56.6x42.5 mm, swap 1; pi3a: 94x56 mm, swap 0). --dry-run prints an mtrec\n"
           "recording of the commands instead of creating a device (simulated time).\n");
}

int main(int argc, char **argv)
{
    const char *script = NULL, *like_dev = NULL, *opt_panel = NULL, *opt_board = NULL;
    int opt_swap = -1, opt_ix = -1, opt_iy = -1, opt_range = 0, rx = 0, ry = 0;
    enum { O_BOARD = 1, O_LIKE, O_RANGE, O_PANEL, O_SWAP, O_IX, O_IY, O_SCRIPT, O_DRY, O_HELP };
    static const struct option opts[] = {
        {"board", required_argument, 0, O_BOARD},   {"like", required_argument, 0, O_LIKE},
        {"range", required_argument, 0, O_RANGE},   {"panel-mm", required_argument, 0, O_PANEL},
        {"swap", required_argument, 0, O_SWAP},     {"invert-x", required_argument, 0, O_IX},
        {"invert-y", required_argument, 0, O_IY},   {"script", required_argument, 0, O_SCRIPT},
        {"dry-run", no_argument, 0, O_DRY},         {"help", no_argument, 0, O_HELP},
        {0, 0, 0, 0}};
    int o;
    while ((o = getopt_long(argc, argv, "h", opts, NULL)) != -1) {
        switch (o) {
        case O_BOARD: opt_board = optarg; break;
        case O_LIKE: like_dev = optarg; break;
        case O_RANGE: opt_range = sscanf(optarg, "%dx%d", &rx, &ry) == 2; break;
        case O_PANEL: opt_panel = optarg; break;
        case O_SWAP: opt_swap = atoi(optarg) != 0; break;
        case O_IX: opt_ix = atoi(optarg) != 0; break;
        case O_IY: opt_iy = atoi(optarg) != 0; break;
        case O_SCRIPT: script = optarg; break;
        case O_DRY: g_dry = 1; break;
        default: usage(); return o == O_HELP || o == 'h' ? 0 : 2;
        }
    }
    if (opt_board && strncmp(opt_board, "pi3a", 4) == 0) {
        G.x1 = 799, G.y1 = 479, G.mm_w = 94, G.mm_h = 56, G.swap = 0;
        snprintf(G.board, sizeof(G.board), "pi3a");
    }
    char v[128];
    const char *conf = "/etc/applaunch/board.conf";
    if (!opt_board && !g_dry) {
        if (mta_kv_file_get(conf, "APPLAUNCH_PANEL_MM", v, sizeof(v)) == 0) sscanf(v, "%lfx%lf", &G.mm_w, &G.mm_h);
        if (mta_kv_file_get(conf, "APPLAUNCH_TOUCH_SWAP_XY", v, sizeof(v)) == 0) G.swap = atoi(v) != 0;
        if (mta_kv_file_get(conf, "APPLAUNCH_TOUCH_INVERT_X", v, sizeof(v)) == 0) G.inv_x = atoi(v) != 0;
        if (mta_kv_file_get(conf, "APPLAUNCH_TOUCH_INVERT_Y", v, sizeof(v)) == 0) G.inv_y = atoi(v) != 0;
        if (mta_kv_file_get(conf, "APPLAUNCH_BOARD", v, sizeof(v)) == 0) snprintf(G.board, sizeof(G.board), "%s", v);
    }
    if (like_dev) like(like_dev);
    if (opt_range) G.x1 = G.x0 + rx - 1, G.y1 = G.y0 + ry - 1;
    if (opt_panel) sscanf(opt_panel, "%lfx%lf", &G.mm_w, &G.mm_h);
    if (opt_swap >= 0) G.swap = opt_swap;
    if (opt_ix >= 0) G.inv_x = opt_ix;
    if (opt_iy >= 0) G.inv_y = opt_iy;

    if (g_dry) {
        printf("# mtrec v1\n# device path=mtplay name=mtplay dry run\n# mode=mt-b slots=%d props=direct\n", SLOTS);
        printf("# range x0=%d x1=%d y0=%d y1=%d\n", G.x0, G.x1, G.y0, G.y1);
        printf("# geom board=%s panel_mm=%.1fx%.1f panel_src=mtplay swap=%d swap_src=mtplay ppmm=0\n", G.board, G.mm_w,
               G.mm_h, G.swap);
        g_t_us = 1000000;
    } else {
        if (create_uinput() != 0) return 1;
        char node[64] = "?";
        mta_sleep_ms(300, NULL); /* udev makes the node */
        if (event_node(node, sizeof(node)) != 0) fprintf(stderr, "mtplay: event node not found in sysfs\n");
        printf("mtplay: %s \"mtplay touch panel\" raw x %d..%d y %d..%d; panel %.1f x %.1f mm, swap %d, invert x %d "
               "y %d (%s)\n",
               node, G.x0, G.x1, G.y0, G.y1, G.mm_w, G.mm_h, G.swap, G.inv_x, G.inv_y, G.board);
        printf("mtplay: start the launcher with --env APPLAUNCH_TOUCH_DEV=%s, then type commands (help: --help)\n",
               node);
        fflush(stdout);
    }
    int rc = 0;
    if (script) { /* "CMD; CMD; ..." (run() tokenises each command with strtok: split by hand here) */
        char *copy = strdup(script);
        for (char *p = copy; p;) {
            char *semi = strchr(p, ';');
            if (semi) *semi = '\0';
            char line[256];
            snprintf(line, sizeof(line), "%s", p);
            const int r = run(line);
            if (r < 0) {
                fprintf(stderr, "mtplay: unknown command: %s\n", p);
                rc = 2;
            }
            if (r > 0) break;
            p = semi ? semi + 1 : NULL;
        }
        free(copy);
    } else {
        char line[256];
        while (fgets(line, sizeof(line), stdin)) {
            const int r = run(line);
            if (r < 0) fprintf(stderr, "mtplay: unknown command (see --help)\n");
            if (r > 0) break;
        }
    }
    if (g_fd >= 0) {
        ioctl(g_fd, UI_DEV_DESTROY);
        close(g_fd);
    }
    return rc;
}
