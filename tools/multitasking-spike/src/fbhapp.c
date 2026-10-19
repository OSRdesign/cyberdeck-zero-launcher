/*
 * SPDX-License-Identifier: MIT
 *
 * fbhapp: a fake fullscreen app for the framebuffer handoff spike (study 026 P-a, item 2).
 *
 * It behaves like a native app (draws the whole framebuffer, reads the touch screen and every keyboard itself, no
 * grab, Esc quits) and logs, one machine-readable line each, every key press and touch it acts on, with how long the
 * event waited in the kernel buffer (age_ms): an event older than 150 ms when read is "stale" (it arrived while the
 * app was frozen). Every Enter changes the background colour, so a stale Enter acted on after a thaw is also visible.
 *
 * It speaks the lifecycle protocol proposed in study section 4.2 when started with APPLAUNCH_LIFECYCLE=1 and
 * APPLAUNCH_CONTROL_FD=<fd> (a SOCK_SEQPACKET socket): "hide N" -> stop drawing, ignore input, answer "ack N";
 * "show N" -> drop pending input, repaint, answer "ack N"; "close N" -> exit. Options make it misbehave on purpose
 * (slow ack, no ack, drawing while hidden, no drain) so fbhandoff's measurements can be checked.
 */
#define _GNU_SOURCE
#include "evdev_util.h"
#include "fb_util.h"
#include "mta_util.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_IN 16
#define STALE_MS 150.0

static volatile sig_atomic_t g_quit, g_cont;
static void on_quit(int s) { (void)s; g_quit = 1; }
static void on_cont(int s) { (void)s; g_cont = 1; }

static FILE *g_log;
static double g_t0;

typedef struct {
    int fd;
    char path[128];
    int evdev;
    unsigned char part[sizeof(struct input_event)];
    size_t part_len;
    int mt; /* multitouch events seen: a touch-down is a new tracking id, not BTN_TOUCH (sent too) */
} input_t;

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(g_log, "fbhapp t_ms=%.0f ", mta_mono_ms() - g_t0);
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
    va_end(ap);
}

static int open_input(input_t *in, const char *path)
{
    memset(in, 0, sizeof(*in));
    snprintf(in->path, sizeof(in->path), "%s", path);
    struct stat st;
    int flags = O_RDONLY | O_NONBLOCK | O_CLOEXEC;
    if (stat(path, &st) == 0 && S_ISFIFO(st.st_mode)) flags = O_RDWR | O_NONBLOCK | O_CLOEXEC; /* no EOF, no HUP */
    in->fd = open(path, flags);
    if (in->fd < 0) return -1;
    in->evdev = ev_is_evdev(in->fd);
    if (in->evdev) {
        int clk = CLOCK_MONOTONIC;
        (void)ioctl(in->fd, EVIOCSCLOCKID, &clk);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *fb_path = NULL, *fb_geom = NULL, *inputs = "auto", *log_path = NULL, *tag = "app";
    double fps = 20, duration = 0;
    int is_static = 0, ack_delay = 0, ignore_hide = 0, draw_hidden = 0, no_drain = 0, no_contract = 0, no_esc = 0;
    static const struct option opts[] = {
        {"fb", required_argument, 0, 'f'},       {"fb-geom", required_argument, 0, 'g'},
        {"input", required_argument, 0, 'i'},    {"log", required_argument, 0, 'l'},
        {"fps", required_argument, 0, 'r'},      {"static", no_argument, 0, 's'},
        {"ack-delay-ms", required_argument, 0, 'a'}, {"ignore-hide", no_argument, 0, 'I'},
        {"draw-hidden", no_argument, 0, 'D'},    {"no-drain", no_argument, 0, 'N'},
        {"no-contract", no_argument, 0, 'C'},    {"duration", required_argument, 0, 'd'},
        {"tag", required_argument, 0, 't'},      {"no-esc", no_argument, 0, 'E'},
        {"help", no_argument, 0, 'h'},           {0, 0, 0, 0}};
    int c;
    while ((c = getopt_long(argc, argv, "h", opts, NULL)) != -1) {
        switch (c) {
        case 'f': fb_path = optarg; break;
        case 'g': fb_geom = optarg; break;
        case 'i': inputs = optarg; break;
        case 'l': log_path = optarg; break;
        case 'r': fps = atof(optarg); break;
        case 's': is_static = 1; break;
        case 'a': ack_delay = atoi(optarg); break;
        case 'I': ignore_hide = 1; break;
        case 'D': draw_hidden = 1; break;
        case 'N': no_drain = 1; break;
        case 'C': no_contract = 1; break;
        case 'd': duration = atof(optarg); break;
        case 't': tag = optarg; break;
        case 'E': no_esc = 1; break;
        default:
            printf("fbhapp [--fb PATH] [--fb-geom WxHxBPP] [--input auto|none|PATH,...] [--log FILE] [--fps N]\n"
                   "       [--static] [--ack-delay-ms N] [--ignore-hide] [--draw-hidden] [--no-drain] [--no-contract]\n"
                   "       [--no-esc] [--duration S] [--tag NAME]\n"
                   "A fake fullscreen app for fbhandoff (see the README). Esc quits; Enter changes the colour.\n");
            return c == 'h' ? 0 : 2;
        }
    }
    g_t0 = mta_mono_ms();
    g_log = log_path ? fopen(log_path, "a") : stderr;
    if (!g_log) g_log = stderr;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_quit;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sa.sa_handler = on_cont;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCONT, &sa, NULL);

    char fbbuf[256];
    if (!fb_path) fb_path = getenv("APPLAUNCH_SCREEN_FB");
    if (!fb_path || !fb_path[0]) {
        snprintf(fbbuf, sizeof(fbbuf), "/dev/fb0");
        fb_path = fbbuf;
    }
    fbdev fb;
    char err[256];
    if (fb_open(&fb, fb_path, fb_geom, 1, err, sizeof(err)) != 0) {
        logf_("rec=error what=fb msg=\"%s\"", err);
        return 1;
    }

    /* inputs */
    input_t in[MAX_IN];
    int nin = 0;
    if (strcmp(inputs, "auto") == 0) {
        char paths[MAX_IN][64];
        char tpath[64], tname[128];
        if (ev_find_touch(tpath, sizeof(tpath), tname, sizeof(tname)) == 0 && open_input(&in[nin], tpath) == 0) ++nin;
        const int nk = ev_list_inputs(paths, MAX_IN - 1, 0);
        for (int i = 0; i < nk && nin < MAX_IN; ++i)
            if (open_input(&in[nin], paths[i]) == 0) ++nin;
    } else if (strcmp(inputs, "none") != 0) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", inputs);
        for (char *tok = strtok(buf, ","); tok && nin < MAX_IN; tok = strtok(NULL, ","))
            if (open_input(&in[nin], tok) == 0) ++nin;
            else logf_("rec=error what=input path=%s msg=\"%s\"", tok, strerror(errno));
    }

    /* the lifecycle socket */
    int ctl = -1;
    const char *lc = getenv("APPLAUNCH_LIFECYCLE"), *cfd = getenv("APPLAUNCH_CONTROL_FD");
    if (!no_contract && lc && strcmp(lc, "1") == 0 && cfd && cfd[0]) ctl = atoi(cfd);
    {
        char list[600] = "";
        for (int i = 0; i < nin; ++i) {
            size_t u = strlen(list);
            snprintf(list + u, sizeof(list) - u, "%s%s", i ? "," : "", in[i].path);
        }
        logf_("rec=start pid=%d pgid=%d tag=%s fb=%s geom=%dx%dx%d inputs=%s contract=%d static=%d", (int)getpid(),
              (int)getpgrp(), tag, fb_path, fb.w, fb.h, fb.bpp, nin ? list : "none", ctl >= 0, is_static);
    }

    static const int palette[][3] = {{20, 40, 90}, {90, 30, 30}, {20, 80, 40}, {80, 70, 10}, {60, 20, 80}, {10, 70, 80}};
    const size_t len = fb_vis_len(&fb);
    uint8_t *back = malloc(len);
    if (!back) return 1;
    long frame = 0, keys = 0, enters = 0, touches = 0, stale = 0, ignored = 0;
    int hidden = 0, dirty = 1, pending_ack = -1;
    double ack_due = 0, last_draw = 0, last_cont = -1e9;
    const double period = fps > 0 ? 1000.0 / fps : 50.0;
    char last_ev[64] = "-";

    while (!g_quit) {
        const double now = mta_mono_ms();
        if (duration > 0 && now - g_t0 > duration * 1000) break;
        if (g_cont) {
            g_cont = 0;
            last_cont = now;
            logf_("rec=cont");
        }
        if (pending_ack >= 0 && now >= ack_due) {
            char msg[32];
            snprintf(msg, sizeof(msg), "ack %d", pending_ack);
            if (send(ctl, msg, strlen(msg), MSG_NOSIGNAL) < 0) logf_("rec=error what=ack msg=\"%s\"", strerror(errno));
            pending_ack = -1;
        }
        /* draw */
        if ((!hidden || draw_hidden) && (now - last_draw >= period) && (dirty || !is_static)) {
            const int *pc = palette[enters % 6];
            fb_fill(&fb, back, 0, 0, fb.w, fb.h, fb_pack(&fb, pc[0], pc[1], pc[2]));
            const uint32_t white = fb_pack(&fb, 240, 240, 240), yellow = fb_pack(&fb, 250, 220, 40);
            const int sc = fb.h >= 400 ? 4 : 2;
            char line[96];
            snprintf(line, sizeof(line), "FBHAPP %s%s", tag, hidden ? " (HIDDEN!)" : "");
            fb_text(&fb, back, 10, 10, sc, line, white);
            snprintf(line, sizeof(line), "frame %ld", frame);
            fb_text(&fb, back, 10, 10 + 10 * sc, sc, line, white);
            snprintf(line, sizeof(line), "keys %ld enter %ld", keys, enters);
            fb_text(&fb, back, 10, 10 + 20 * sc, sc, line, white);
            snprintf(line, sizeof(line), "touch %ld stale %ld", touches, stale);
            fb_text(&fb, back, 10, 10 + 30 * sc, sc, line, stale ? yellow : white);
            snprintf(line, sizeof(line), "last %s", last_ev);
            fb_text(&fb, back, 10, 10 + 40 * sc, sc > 2 ? sc - 1 : sc, line, white);
            if (!is_static) {
                const int bx = (int)(frame * 8 % (fb.w > 40 ? fb.w - 40 : 1));
                fb_fill(&fb, back, bx, fb.h - 50, 40, 40, yellow);
            }
            memcpy(fb_vis(&fb), back, len);
            ++frame;
            last_draw = now;
            dirty = 0;
        }
        /* wait for input or the next frame */
        struct pollfd pfd[MAX_IN + 1];
        for (int i = 0; i < nin; ++i) pfd[i] = (struct pollfd){in[i].fd, POLLIN, 0};
        int np = nin;
        if (ctl >= 0) pfd[np++] = (struct pollfd){ctl, POLLIN, 0};
        int timeout = (int)(period - (mta_mono_ms() - last_draw));
        if (timeout < 1) timeout = 1;
        if (pending_ack >= 0) {
            const int t2 = (int)(ack_due - mta_mono_ms());
            if (t2 < timeout) timeout = t2 < 0 ? 0 : t2;
        }
        const int pr = poll(pfd, (nfds_t)np, timeout);
        if (pr < 0) continue;
        if (ctl >= 0 && (pfd[nin].revents & (POLLIN | POLLHUP))) {
            char msg[80];
            const ssize_t r = recv(ctl, msg, sizeof(msg) - 1, 0);
            if (r <= 0) {
                logf_("rec=ctl-closed");
                ctl = -1;
            } else {
                msg[r] = '\0';
                int n = 0;
                if (sscanf(msg, "hide %d", &n) == 1) {
                    hidden = 1;
                    logf_("rec=hide n=%d", n);
                    if (!ignore_hide) {
                        pending_ack = n;
                        ack_due = mta_mono_ms() + ack_delay;
                    }
                } else if (sscanf(msg, "show %d", &n) == 1) {
                    long ev = 0, presses = 0;
                    if (!no_drain) {
                        for (int i = 0; i < nin; ++i) {
                            struct input_event e[64];
                            ssize_t rr;
                            while ((rr = read(in[i].fd, e, sizeof(e))) > 0) {
                                const int k = (int)(rr / (ssize_t)sizeof(e[0]));
                                ev += k;
                                for (int j = 0; j < k; ++j)
                                    if (e[j].type == EV_KEY && e[j].value == 1 && e[j].code < BTN_MISC) ++presses;
                            }
                        }
                    }
                    hidden = 0;
                    dirty = 1;
                    last_draw = 0;
                    logf_("rec=show n=%d drained_events=%ld drained_presses=%ld drain=%d", n, ev, presses, !no_drain);
                    pending_ack = n;
                    ack_due = mta_mono_ms();
                } else if (sscanf(msg, "close %d", &n) == 1) {
                    logf_("rec=exit reason=close n=%d", n);
                    break;
                } else {
                    logf_("rec=ctl-unknown msg=\"%s\"", msg);
                }
            }
        }
        for (int i = 0; i < nin; ++i) {
            if (!(pfd[i].revents & POLLIN)) continue;
            unsigned char buf[sizeof(struct input_event) * 64];
            size_t have = in[i].part_len;
            memcpy(buf, in[i].part, have);
            const ssize_t r = read(in[i].fd, buf + have, sizeof(buf) - have);
            if (r <= 0) continue;
            have += (size_t)r;
            const size_t whole = have / sizeof(struct input_event);
            in[i].part_len = have - whole * sizeof(struct input_event);
            memcpy(in[i].part, buf + whole * sizeof(struct input_event), in[i].part_len);
            const double read_ms = mta_mono_ms();
            for (size_t j = 0; j < whole; ++j) {
                struct input_event e;
                memcpy(&e, buf + j * sizeof(e), sizeof(e));
                if (e.type == EV_ABS && e.code == ABS_MT_TRACKING_ID) in[i].mt = 1;
                const int press = e.type == EV_KEY && e.value == 1 && e.code != BTN_TOUCH && e.code < BTN_MISC;
                const int tdown = (e.type == EV_ABS && e.code == ABS_MT_TRACKING_ID && e.value >= 0) ||
                                  (!in[i].mt && e.type == EV_KEY && e.code == BTN_TOUCH && e.value == 1);
                if (!press && !tdown) continue;
                const double ev_ms = (double)e.input_event_sec * 1000.0 + (double)e.input_event_usec / 1000.0;
                double age = read_ms - ev_ms;
                if (age < 0 || age > 86400000.0) age = -1; /* not a monotonic timestamp */
                const int is_stale = age > STALE_MS;
                const int acted = !hidden;
                if (is_stale && acted) ++stale;
                if (!acted) ++ignored;
                logf_("rec=input dev=%s kind=%s code=%d age_ms=%.0f stale=%d acted=%d state=%s since_cont_ms=%.0f",
                      in[i].path, press ? "key" : "touch", e.code, age, is_stale, acted, hidden ? "hidden" : "visible",
                      read_ms - last_cont);
                if (!acted) continue;
                dirty = 1;
                if (press) {
                    ++keys;
                    snprintf(last_ev, sizeof(last_ev), "key %d%s", e.code, is_stale ? " STALE" : "");
                    if (e.code == KEY_ENTER || e.code == KEY_KPENTER) ++enters;
                    if (e.code == KEY_ESC && !no_esc) {
                        logf_("rec=exit reason=esc stale=%d", is_stale);
                        g_quit = 1;
                    }
                } else {
                    ++touches;
                    snprintf(last_ev, sizeof(last_ev), "touch%s", is_stale ? " STALE" : "");
                }
            }
        }
    }
    logf_("rec=end frames=%ld keys=%ld enters=%ld touches=%ld stale=%ld ignored_hidden=%ld", frame, keys, enters,
          touches, stale, ignored);
    free(back);
    fb_close(&fb);
    return 0;
}
