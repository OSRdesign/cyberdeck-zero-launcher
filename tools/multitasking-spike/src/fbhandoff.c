/*
 * SPDX-License-Identifier: MIT
 *
 * fbhandoff: framebuffer handoff prototype for study 026 phase P-a (section 7.2, item 2).
 *
 * Runs a fullscreen command as a child in its own process group, then on a trigger (a timer or SIGUSR1):
 *   1. "hide N" on the lifecycle socket proposed in study section 4.2 (APPLAUNCH_LIFECYCLE=1, APPLAUNCH_CONTROL_FD)
 *      and the time to "ack N"; no answer within --ack-ms (an app without the contract) -> SIGSTOP of the group;
 *   2. optional EVIOCGRAB of the input nodes the app has open (experiment for study risk 1), counting what it took;
 *   3. a snapshot of the visible framebuffer page (size and stride from FBIOGET), timed;
 *   4. a test card (colour bars + a counter) for --hold seconds, watching for writes from the "frozen" app;
 *   5. the app's own evdev buffers: pending input peeked, or drained, through pidfd_getfd (the very same open file
 *      as the app's, so reading it empties the app's queue) -- the "stale Enter" question;
 *   6. the snapshot back (timed, verified), SIGCONT, "show N" and its ack;
 *   7. the framebuffer watched: time to the app's first and last change (repaint latency), what is left of the card.
 * One "fbh rec=..." line per step and per cycle, key=value, to stdout and the results file.
 *
 * Safety: the launcher must not own the panel (refused unless --force, or --stop-launcher stops it and starts it
 * again at the end). Every exit path restores the framebuffer if the card is up, releases grabs, sends SIGCONT and
 * ends the app's group; a guardian process does the same if fbhandoff itself is killed (even with SIGKILL).
 *
 * Run `fbhandoff --help` for the options.
 */
#define _GNU_SOURCE
#include "evdev_util.h"
#include "fb_util.h"
#include "mta_util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_getfd
#define SYS_pidfd_getfd 438
#endif

#define MAX_APPFD 64
#define MAX_GRAB 16
#define MAX_LENV 64

typedef struct {
    volatile sig_atomic_t clean, card_up, launcher_stopped;
    volatile pid_t pgid;
    char fb_path[PATH_MAX];
    char fb_geom[32];
    char results[PATH_MAX];
    size_t snap_len;
    uint8_t snap[];
} shared_t;

static struct {
    const char *fb, *fb_geom, *out, *app_log, *env_file, *input_match, *label;
    const char *grab_extra[8];
    int ngrab_extra;
    double after, every, hold, end_after;
    int cycles, ack_ms, background_run, contract, input_drain, input_grab, restore, watch_ms, watch_iv;
    int force, stop_launcher, launcher_env;
    char **cmd;
} O;

static volatile sig_atomic_t g_quit, g_trigger;
static FILE *g_res;
static shared_t *g_sh;
static fbdev g_fb;
static size_t g_len;
static uint8_t *g_card;
static pid_t g_pid = -1, g_pgid = -1, g_guard = -1;
static int g_ctl = -1, g_guard_w = -1;
static int g_app_exited, g_app_status;
static int g_grab_fd[MAX_GRAB], g_ngrab;
static double g_t0;
static char *g_lenv[MAX_LENV];
static int g_nlenv;

static void on_quit(int s) { (void)s; g_quit = 1; }
static void on_usr1(int s) { (void)s; g_trigger = 1; }

static void res(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void res(const char *fmt, ...)
{
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    printf("fbh t_ms=%.0f %s\n", mta_mono_ms() - g_t0, line);
    fflush(stdout);
    if (g_res) {
        fprintf(g_res, "fbh t_ms=%.0f %s\n", mta_mono_ms() - g_t0, line);
        fflush(g_res);
    }
}

/* ------------------------------------------------------------------------------------------------ app state */
static int app_alive(void)
{
    if (g_pid <= 0) return 0;
    if (!g_app_exited) {
        int st = 0;
        if (waitpid(g_pid, &st, WNOHANG) == g_pid) {
            g_app_exited = 1;
            g_app_status = st;
        }
    }
    return !g_app_exited;
}

static const char *exit_desc(char *buf, size_t n)
{
    if (!g_app_exited) snprintf(buf, n, "running");
    else if (WIFEXITED(g_app_status)) snprintf(buf, n, "exit:%d", WEXITSTATUS(g_app_status));
    else if (WIFSIGNALED(g_app_status)) snprintf(buf, n, "signal:%d", WTERMSIG(g_app_status));
    else snprintf(buf, n, "unknown");
    return buf;
}

/* ------------------------------------------------------------------------------------------------ app fds */
typedef struct {
    pid_t pid;
    int fd;
    char target[160];
} appfd_t;

static int scan_app_fds(appfd_t *out, int max)
{
    pid_t pids[128];
    const int np = g_pgid > 0 ? mta_group_members(g_pgid, pids, 128) : 0;
    const size_t mlen = strlen(O.input_match);
    int n = 0;
    for (int i = 0; i < np && n < max; ++i) {
        char dir[64];
        snprintf(dir, sizeof(dir), "/proc/%d/fd", (int)pids[i]);
        DIR *d = opendir(dir);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL && n < max) {
            if (e->d_name[0] == '.') continue;
            char link[128], target[160];
            snprintf(link, sizeof(link), "%s/%s", dir, e->d_name);
            const ssize_t r = readlink(link, target, sizeof(target) - 1);
            if (r <= 0) continue;
            target[r] = '\0';
            if (strncmp(target, O.input_match, mlen) != 0) continue;
            out[n].pid = pids[i];
            out[n].fd = atoi(e->d_name);
            snprintf(out[n].target, sizeof(out[n].target), "%s", target);
            ++n;
        }
        closedir(d);
    }
    return n;
}

typedef struct {
    long events, presses, enters, escs, touches, dropped;
    long btn_touches; /* BTN_TOUCH downs: the touch count of a single-touch device (multitouch sends both) */
} evcount_t;

static long touches_of(const evcount_t *c) { return c->touches ? c->touches : c->btn_touches; }

static void count_events(evcount_t *c, const unsigned char *buf, size_t bytes)
{
    const size_t n = bytes / sizeof(struct input_event);
    for (size_t i = 0; i < n; ++i) {
        struct input_event e;
        memcpy(&e, buf + i * sizeof(e), sizeof(e));
        c->events++;
        if (e.type == EV_KEY && e.value == 1 && e.code < BTN_MISC) {
            c->presses++;
            if (e.code == KEY_ENTER || e.code == KEY_KPENTER) c->enters++;
            if (e.code == KEY_ESC) c->escs++;
        }
        if (e.type == EV_ABS && e.code == ABS_MT_TRACKING_ID && e.value >= 0) c->touches++;
        if (e.type == EV_KEY && e.code == BTN_TOUCH && e.value == 1) c->btn_touches++;
        if (e.type == EV_SYN && e.code == SYN_DROPPED) c->dropped++;
    }
}

/* peek (non-destructive poll) or drain the app's own input queues through pidfd_getfd */
static void inspect_app_input(int drain, int *nfds, int *pending, evcount_t *drained, char *err, size_t errn)
{
    appfd_t fds[MAX_APPFD];
    const int n = scan_app_fds(fds, MAX_APPFD);
    *nfds = n;
    *pending = 0;
    err[0] = '\0';
    for (int i = 0; i < n; ++i) {
        const int pidfd = (int)syscall(SYS_pidfd_open, fds[i].pid, 0);
        if (pidfd < 0) {
            snprintf(err, errn, "pidfd_open:%s", strerrorname_np(errno) ? strerrorname_np(errno) : "?");
            continue;
        }
        const int dup = (int)syscall(SYS_pidfd_getfd, pidfd, fds[i].fd, 0);
        if (dup < 0) {
            snprintf(err, errn, "pidfd_getfd:%s", strerrorname_np(errno) ? strerrorname_np(errno) : "?");
            close(pidfd);
            continue;
        }
        struct pollfd p = {dup, POLLIN, 0};
        if (poll(&p, 1, 0) == 1 && (p.revents & POLLIN)) ++*pending;
        while (drain) {
            p.revents = 0;
            if (poll(&p, 1, 0) != 1 || !(p.revents & POLLIN)) break;
            unsigned char buf[sizeof(struct input_event) * 64];
            const ssize_t r = read(dup, buf, sizeof(buf));
            if (r <= 0) break;
            count_events(drained, buf, (size_t)r);
        }
        close(dup);
        close(pidfd);
    }
}

/* ------------------------------------------------------------------------------------------------ grab */
static void grab_begin(char *desc, size_t n)
{
    desc[0] = '\0';
    appfd_t fds[MAX_APPFD];
    const int nf = scan_app_fds(fds, MAX_APPFD);
    const char *paths[MAX_GRAB];
    int np = 0;
    for (int i = 0; i < nf && np < MAX_GRAB; ++i) {
        if (strncmp(fds[i].target, "/dev/input/", 11) != 0 && strcmp(O.input_match, "/dev/input/event") == 0) continue;
        int dupl = 0;
        for (int j = 0; j < np; ++j)
            if (strcmp(paths[j], fds[i].target) == 0) dupl = 1;
        if (!dupl) paths[np++] = strdup(fds[i].target);
    }
    for (int i = 0; i < O.ngrab_extra && np < MAX_GRAB; ++i) paths[np++] = strdup(O.grab_extra[i]);
    g_ngrab = 0;
    for (int i = 0; i < np; ++i) {
        const char *base = strrchr(paths[i], '/') ? strrchr(paths[i], '/') + 1 : paths[i];
        const int fd = open(paths[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        const char *st = "ok";
        if (fd < 0) st = strerrorname_np(errno);
        else if (ioctl(fd, EVIOCGRAB, 1) != 0) {
            st = strerrorname_np(errno);
            close(fd);
        } else {
            g_grab_fd[g_ngrab++] = fd;
        }
        const size_t u = strlen(desc);
        snprintf(desc + u, n - u, "%s%s:%s", i ? "," : "", base, st ? st : "?");
        free((void *)paths[i]);
    }
    if (!desc[0]) snprintf(desc, n, "nothing-open");
}

static void grab_poll(evcount_t *c)
{
    for (int i = 0; i < g_ngrab; ++i) {
        unsigned char buf[sizeof(struct input_event) * 64];
        ssize_t r;
        while ((r = read(g_grab_fd[i], buf, sizeof(buf))) > 0) count_events(c, buf, (size_t)r);
    }
}

static void grab_end(void)
{
    for (int i = 0; i < g_ngrab; ++i) {
        (void)ioctl(g_grab_fd[i], EVIOCGRAB, 0);
        close(g_grab_fd[i]);
    }
    g_ngrab = 0;
}

/* ------------------------------------------------------------------------------------------------ the card */
static void draw_card(uint8_t *buf, int cycle, double left_s, const char *mode)
{
    static const int bars[8][3] = {{235, 235, 235}, {235, 235, 16}, {16, 235, 235}, {16, 235, 16},
                                   {235, 16, 235},  {235, 16, 16},  {16, 16, 235},  {16, 16, 16}};
    const int top = g_fb.h * 7 / 10;
    for (int i = 0; i < 8; ++i) {
        const int x0 = g_fb.w * i / 8, x1 = g_fb.w * (i + 1) / 8;
        fb_fill(&g_fb, buf, x0, 0, x1 - x0, top, fb_pack(&g_fb, bars[i][0], bars[i][1], bars[i][2]));
    }
    fb_fill(&g_fb, buf, 0, top, g_fb.w, g_fb.h - top, fb_pack(&g_fb, 40, 40, 40));
    const int sc = g_fb.h >= 400 ? 3 : 2;
    const uint32_t white = fb_pack(&g_fb, 240, 240, 240);
    char line[96];
    snprintf(line, sizeof(line), "FBHANDOFF cycle %d  %s", cycle, mode);
    fb_text(&g_fb, buf, 8, top + 6, sc, line, white);
    snprintf(line, sizeof(line), "%.1f s", left_s < 0 ? 0 : left_s);
    const int big = (g_fb.h - top - 10 * sc - 12) / 8 > 1 ? (g_fb.h - top - 10 * sc - 12) / 8 : 1;
    fb_text(&g_fb, buf, 8, top + 10 * sc + 6, big, line, fb_pack(&g_fb, 250, 220, 40));
}

/* ------------------------------------------------------------------------------------------------ control socket */
static int ctl_send(const char *what, int n)
{
    if (g_ctl < 0) return -1;
    char msg[32];
    snprintf(msg, sizeof(msg), "%s %d", what, n);
    return send(g_ctl, msg, strlen(msg), MSG_NOSIGNAL) < 0 ? -1 : 0;
}

/* waits for "ack n" up to ms; returns the time it took, or -1 */
static double ctl_wait_ack(int n, int ms)
{
    if (g_ctl < 0) return -1;
    const double t0 = mta_mono_ms();
    for (;;) {
        const double left = ms - (mta_mono_ms() - t0);
        if (left <= 0) return -1;
        struct pollfd p = {g_ctl, POLLIN, 0};
        const int r = poll(&p, 1, (int)left + 1);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        char msg[80];
        const ssize_t got = recv(g_ctl, msg, sizeof(msg) - 1, MSG_DONTWAIT);
        if (got <= 0) {
            res("rec=ctl-closed");
            close(g_ctl);
            g_ctl = -1;
            return -1;
        }
        msg[got] = '\0';
        int k = -1;
        if (sscanf(msg, "ack %d", &k) == 1 && k == n) return mta_mono_ms() - t0;
        res("rec=ctl-message msg=\"%s\"", msg);
    }
}

/* ------------------------------------------------------------------------------------------------ the cycle */
static void watch_after_thaw(double t_thaw, const uint8_t *ref, double *first_ms, double *last_ms, int *samples,
                             double *changed_pct, double *residue_pct)
{
    uint8_t *prev = malloc(g_len), *cur = malloc(g_len);
    *first_ms = *last_ms = -1;
    *samples = 0;
    if (!prev || !cur) {
        free(prev);
        free(cur);
        return;
    }
    memcpy(prev, ref, g_len);
    while (!g_quit && mta_mono_ms() - t_thaw < O.watch_ms) {
        memcpy(cur, fb_vis(&g_fb), g_len);
        const double t = mta_mono_ms() - t_thaw;
        ++*samples;
        if (*first_ms < 0 && memcmp(cur, ref, g_len) != 0) *first_ms = t;
        if (memcmp(cur, prev, g_len) != 0) *last_ms = t;
        uint8_t *sw = prev;
        prev = cur;
        cur = sw;
        mta_sleep_ms(O.watch_iv, &g_quit);
    }
    /* prev = the last frame seen */
    const double total = (double)g_fb.w * g_fb.h;
    *changed_pct = 100.0 * (double)fb_diff_pixels(&g_fb, prev, g_sh->snap) / total;
    long card_px = 0, left = 0;
    const int bp = g_fb.bpp / 8;
    for (int y = 0; y < g_fb.h; ++y)
        for (int x = 0; x < g_fb.w; ++x) {
            const size_t o = (size_t)y * (size_t)g_fb.stride + (size_t)x * (size_t)bp;
            if (memcmp(g_card + o, g_sh->snap + o, (size_t)bp) == 0) continue;
            ++card_px;
            if (memcmp(prev + o, g_card + o, (size_t)bp) == 0) ++left;
        }
    *residue_pct = card_px ? 100.0 * (double)left / (double)card_px : 0;
    free(prev);
    free(cur);
}

static void run_cycle(int n, const char *trigger)
{
    const char *input_mode = O.input_grab ? (O.input_drain ? "grab-drain" : "grab") : (O.input_drain ? "drain" : "none");
    res("rec=trigger n=%d trigger=%s", n, trigger);
    /* 1. hide */
    double ack_ms = -1;
    const char *hide = "none";
    if (g_ctl >= 0) {
        if (ctl_send("hide", n) == 0) {
            ack_ms = ctl_wait_ack(n, O.ack_ms);
            hide = ack_ms >= 0 ? "ack" : "timeout";
        } else {
            hide = "send-failed";
        }
    }
    const int acked = ack_ms >= 0;
    int stopped = 0;
    double stop_ms = -1;
    if (!acked || !O.background_run) {
        const double t = mta_mono_ms();
        if (killpg(g_pgid, SIGSTOP) == 0) {
            stopped = 1;
            while (mta_group_stopped(g_pgid) == 0 && mta_mono_ms() - t < 500) mta_sleep_ms(1, NULL);
            stop_ms = mta_mono_ms() - t;
        }
    }
    /* 2. grab */
    char grabdesc[512] = "none";
    evcount_t grabbed = {0};
    if (O.input_grab) grab_begin(grabdesc, sizeof(grabdesc));
    /* 3. snapshot */
    double t = mta_mono_ms();
    memcpy(g_sh->snap, fb_vis(&g_fb), g_len);
    const double snapshot_ms = mta_mono_ms() - t;
    g_sh->card_up = 1;
    /* 4. card */
    const char *mode = stopped ? (acked ? "FROZEN (ack+SIGSTOP)" : "FROZEN (SIGSTOP)") : "HIDDEN (running)";
    t = mta_mono_ms();
    draw_card(g_card, n, O.hold, mode);
    memcpy(fb_vis(&g_fb), g_card, g_len);
    const double card_ms = mta_mono_ms() - t;
    long foreign = 0, ticks = 0;
    const double hold_start = mta_mono_ms();
    int aborted = 0;
    while (mta_mono_ms() - hold_start < O.hold * 1000.0) {
        if (g_quit) {
            aborted = 1;
            break;
        }
        mta_sleep_ms(100, &g_quit);
        if (memcmp(fb_vis(&g_fb), g_card, g_len) != 0) ++foreign; /* someone else wrote the panel */
        draw_card(g_card, n, O.hold - (mta_mono_ms() - hold_start) / 1000.0, mode);
        memcpy(fb_vis(&g_fb), g_card, g_len);
        ++ticks;
        if (O.input_grab) grab_poll(&grabbed);
    }
    const double hold_ms = mta_mono_ms() - hold_start;
    if (O.input_grab) {
        grab_poll(&grabbed);
        grab_end();
    }
    /* 5. the app's own input queues */
    int app_fds = 0, pending = 0;
    evcount_t drained = {0};
    char inerr[96];
    inspect_app_input(O.input_drain, &app_fds, &pending, &drained, inerr, sizeof(inerr));
    /* 6. restore and thaw */
    double restore_ms = -1;
    int verified = -1;
    if (O.restore) {
        t = mta_mono_ms();
        memcpy(fb_vis(&g_fb), g_sh->snap, g_len);
        restore_ms = mta_mono_ms() - t;
        verified = memcmp(fb_vis(&g_fb), g_sh->snap, g_len) == 0;
    }
    g_sh->card_up = 0;
    const double t_thaw = mta_mono_ms();
    if (stopped) killpg(g_pgid, SIGCONT);
    double show_ack_ms = -1;
    if (acked && ctl_send("show", n) == 0) show_ack_ms = ctl_wait_ack(n, O.ack_ms > 1000 ? O.ack_ms : 1000);
    /* 7. watch */
    double first_ms = -1, last_ms = -1, changed_pct = 0, residue_pct = 0;
    int samples = 0;
    watch_after_thaw(t_thaw, O.restore ? g_sh->snap : g_card, &first_ms, &last_ms, &samples, &changed_pct,
                     &residue_pct);
    char st[32];
    const int alive = app_alive();
    res("rec=cycle n=%d trigger=%s label=%s contract=%d hide=%s ack_ms=%.1f background=%s frozen=%d stop_ms=%.1f "
        "snapshot_ms=%.2f card_ms=%.2f hold_ms=%.0f ticks=%ld foreign_writes=%ld input=%s grab=%s grabbed_events=%ld "
        "grabbed_presses=%ld grabbed_enters=%ld grabbed_touches=%ld app_input_fds=%d app_pending=%d "
        "drained_events=%ld drained_presses=%ld drained_enters=%ld drained_escs=%ld drained_touches=%ld "
        "syn_dropped=%ld input_err=%s restore=%s restore_ms=%.2f restore_verified=%d show_ack_ms=%.1f "
        "first_change_ms=%.0f last_change_ms=%.0f watch_ms=%d watch_samples=%d changed_px_pct=%.2f "
        "card_residue_pct=%.2f app=%s aborted=%d",
        n, trigger, O.label, g_ctl >= 0 || acked, hide, ack_ms, O.background_run ? "run" : "freeze", stopped, stop_ms,
        snapshot_ms, card_ms, hold_ms, ticks, foreign, input_mode, grabdesc, grabbed.events, grabbed.presses,
        grabbed.enters, touches_of(&grabbed), app_fds, pending, drained.events, drained.presses, drained.enters,
        drained.escs, touches_of(&drained), drained.dropped + grabbed.dropped, inerr[0] ? inerr : "none",
        O.restore ? "snapshot" : "none", restore_ms, verified, show_ack_ms, first_ms, last_ms, O.watch_ms, samples,
        changed_pct, residue_pct, alive ? "running" : exit_desc(st, sizeof(st)), aborted);
    printf("   cycle %d: %s, snapshot %.1f ms, restore %.1f ms, first repaint %s, app %s, pending input %s%s\n", n,
           mode, snapshot_ms, restore_ms, first_ms < 0 ? "none seen" : "seen", alive ? "running" : "gone",
           pending ? "YES" : "no", O.input_drain ? " (drained)" : "");
    fflush(stdout);
}

/* ------------------------------------------------------------------------------------------------ guardian */
static void guardian_main(int rfd)
{
    setsid();
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    char c;
    for (;;) {
        const ssize_t r = read(rfd, &c, 1);
        if (r == 0) break;
        if (r < 0 && errno != EINTR) break;
    }
    if (g_sh->clean) _exit(0);
    /* fbhandoff died without cleaning up */
    int restored = 0, killed = 0, started = 0;
    if (g_sh->card_up) {
        fbdev fb;
        char err[128];
        if (fb_open(&fb, g_sh->fb_path, g_sh->fb_geom[0] ? g_sh->fb_geom : NULL, 1, err, sizeof(err)) == 0) {
            if (fb_vis_len(&fb) == g_sh->snap_len) {
                memcpy(fb_vis(&fb), g_sh->snap, g_sh->snap_len);
                restored = 1;
            }
            fb_close(&fb);
        }
    }
    const pid_t pg = g_sh->pgid;
    if (pg > 0 && kill(-pg, 0) == 0) {
        killpg(pg, SIGCONT);
        killpg(pg, SIGTERM);
        for (int i = 0; i < 30 && kill(-pg, 0) == 0; ++i) usleep(100000);
        if (kill(-pg, 0) == 0) killpg(pg, SIGKILL);
        killed = 1;
    }
    if (g_sh->launcher_stopped) started = mta_launcher_service("start") == 0;
    FILE *f = fopen(g_sh->results, "a");
    if (f) {
        fprintf(f, "fbh rec=guardian restored_fb=%d ended_group=%d launcher_started=%d\n", restored, killed, started);
        fclose(f);
    }
    _exit(0);
}

/* ------------------------------------------------------------------------------------------------ cleanup */
static void cleanup(const char *reason)
{
    if (g_sh && g_sh->card_up) {
        memcpy(fb_vis(&g_fb), g_sh->snap, g_len);
        g_sh->card_up = 0;
        res("rec=restore-on-exit");
    }
    grab_end();
    int ended = 0;
    if (g_pgid > 0 && kill(-g_pgid, 0) == 0) {
        killpg(g_pgid, SIGCONT);
        if (g_ctl >= 0 && app_alive()) {
            ctl_send("close", 0);
            for (int i = 0; i < 10 && app_alive(); ++i) mta_sleep_ms(100, NULL);
        }
        if (kill(-g_pgid, 0) == 0) {
            killpg(g_pgid, SIGTERM);
            for (int i = 0; i < 30 && kill(-g_pgid, 0) == 0; ++i) {
                app_alive();
                while (waitpid(-g_pgid, NULL, WNOHANG) > 0) {}
                mta_sleep_ms(100, NULL);
            }
            if (kill(-g_pgid, 0) == 0) killpg(g_pgid, SIGKILL);
        }
        ended = 1;
    }
    app_alive();
    while (g_pid > 0 && waitpid(-g_pgid, NULL, WNOHANG) > 0) {}
    int started = 0;
    if (g_sh && g_sh->launcher_stopped) {
        res("rec=launcher action=start");
        started = mta_launcher_service("start") == 0;
        g_sh->launcher_stopped = 0;
    }
    char st[32];
    res("rec=end reason=%s app=%s ended_group=%d launcher_started=%d", reason, exit_desc(st, sizeof(st)), ended,
        started);
    if (g_sh) g_sh->clean = 1;
    if (g_guard_w >= 0) close(g_guard_w);
    if (g_guard > 0) waitpid(g_guard, NULL, 0);
}

/* ------------------------------------------------------------------------------------------------ main */
static void usage(void)
{
    printf(
        "fbhandoff: framebuffer handoff prototype (study 026 P-a). Runs a fullscreen app, freezes or hides it,\n"
        "shows a test card, gives the panel back and measures.\n"
        "\n"
        "  fbhandoff [options] -- COMMAND [ARGS...]\n"
        "\n"
        "  --after S          first freeze S seconds after the start (default 8; 0 = only on SIGUSR1)\n"
        "  --every S          next freeze S seconds after the previous one ended (default: none)\n"
        "  --cycles N         freezes before the end (default 1; 0 = until the app exits or Ctrl-C)\n"
        "  --hold S           test card time (default 5)\n"
        "  --end-after S      after the last cycle, let the app run S seconds, then end it (default 3)\n"
        "  --ack-ms N         hide deadline (default 300); no ack -> SIGSTOP of the group\n"
        "  --background freeze|run   after an ack: SIGSTOP (default) or keep running hidden\n"
        "  --no-contract      do not offer the lifecycle socket (SIGSTOP at once, like a legacy app)\n"
        "  --input none|drain|grab|grab-drain   what happens to input typed during the freeze (default none):\n"
        "                     drain = empty the app's own evdev queues before the thaw (pidfd_getfd);\n"
        "                     grab = EVIOCGRAB the app's input nodes during the freeze, released before the thaw\n"
        "  --input-match P    which app fds count as input (default /dev/input/event)\n"
        "  --grab PATH        also grab this node (repeatable)\n"
        "  --restore snapshot|none   put the app's last frame back before the thaw (default snapshot)\n"
        "  --watch-ms N       after the thaw, watch the panel this long (default 3000); --watch-interval-ms (10)\n"
        "  --fb PATH          framebuffer (default: screen.state fb=, else /dev/fb0); --fb-geom WxHxBPP for a file\n"
        "  --out FILE         results (default ~/mta-results/fbhandoff-<host>-<date>.txt); --app-log FILE (default\n"
        "                     <out>-app.log): the app's stdout and stderr\n"
        "  --env-file FILE    KEY=VALUE lines added to the app's environment\n"
        "  --no-launcher-env  do not copy APPLAUNCH_* / LV_* from the running launcher into the app's environment\n"
        "  --stop-launcher    stop cyberdeckzero.service (systemctl --user) first and start it again at the end\n"
        "  --force            run although the launcher or another process has the framebuffer open\n"
        "  --label TEXT       written into every cycle line\n"
        "\n"
        "SIGUSR1 starts a cycle now. Ctrl-C, SIGTERM or SIGHUP end cleanly: the panel comes back, the app's group gets\n"
        "SIGCONT then SIGTERM (SIGKILL 3 s later), the launcher is started again if fbhandoff stopped it.\n"
        "Exit status: 0 done, 2 usage, 3 refused (the launcher owns the panel), 1 other error.\n");
}

static void default_fb(char *out, size_t n)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    char sf[PATH_MAX], v[256];
    if (rt && rt[0]) snprintf(sf, sizeof(sf), "%s/applaunch/screen.state", rt);
    else snprintf(sf, sizeof(sf), "/run/user/%d/applaunch/screen.state", (int)getuid());
    if (mta_kv_file_get(sf, "fb", v, sizeof(v)) == 0 && v[0]) snprintf(out, n, "%s", v);
    else snprintf(out, n, "/dev/fb0");
}

static const char *fbcon_state(void)
{
    DIR *d = opendir("/sys/class/vtconsole");
    if (!d) return "absent";
    struct dirent *e;
    static char out[32];
    snprintf(out, sizeof(out), "absent");
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "vtcon", 5) != 0) continue;
        char p[300], name[128] = "", bind[8] = "";
        snprintf(p, sizeof(p), "/sys/class/vtconsole/%s/name", e->d_name);
        FILE *f = fopen(p, "r");
        if (!f) continue;
        if (!fgets(name, sizeof(name), f)) name[0] = '\0';
        fclose(f);
        if (!strstr(name, "frame buffer")) continue;
        snprintf(p, sizeof(p), "/sys/class/vtconsole/%s/bind", e->d_name);
        f = fopen(p, "r");
        if (f) {
            if (!fgets(bind, sizeof(bind), f)) bind[0] = '\0';
            fclose(f);
        }
        snprintf(out, sizeof(out), "%s", bind[0] == '1' ? "bound" : "unbound");
    }
    closedir(d);
    return out;
}

static void capture_launcher_env(pid_t lp)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/environ", (int)lp);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    static char buf[65536];
    const ssize_t r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (r <= 0) return;
    buf[r] = '\0';
    for (char *p = buf; p < buf + r && g_nlenv < MAX_LENV; p += strlen(p) + 1)
        if (strncmp(p, "APPLAUNCH_", 10) == 0 || strncmp(p, "LV_", 3) == 0) g_lenv[g_nlenv++] = strdup(p);
}

int main(int argc, char **argv)
{
    O.after = 8;
    O.cycles = 1;
    O.hold = 5;
    O.end_after = 3;
    O.ack_ms = 300;
    O.contract = 1;
    O.restore = 1;
    O.watch_ms = 3000;
    O.watch_iv = 10;
    O.launcher_env = 1;
    O.input_match = "/dev/input/event";
    O.label = "-";
    enum { A_AFTER = 1000, A_EVERY, A_CYCLES, A_HOLD, A_END, A_ACK, A_BG, A_NOC, A_INPUT, A_MATCH, A_GRAB, A_RESTORE,
           A_WATCH, A_WIV, A_FB, A_GEOM, A_OUT, A_APPLOG, A_ENV, A_NOLENV, A_STOP, A_FORCE, A_LABEL };
    static const struct option opts[] = {
        {"after", required_argument, 0, A_AFTER},      {"every", required_argument, 0, A_EVERY},
        {"cycles", required_argument, 0, A_CYCLES},    {"hold", required_argument, 0, A_HOLD},
        {"end-after", required_argument, 0, A_END},    {"ack-ms", required_argument, 0, A_ACK},
        {"background", required_argument, 0, A_BG},    {"no-contract", no_argument, 0, A_NOC},
        {"input", required_argument, 0, A_INPUT},      {"input-match", required_argument, 0, A_MATCH},
        {"grab", required_argument, 0, A_GRAB},        {"restore", required_argument, 0, A_RESTORE},
        {"watch-ms", required_argument, 0, A_WATCH},   {"watch-interval-ms", required_argument, 0, A_WIV},
        {"fb", required_argument, 0, A_FB},            {"fb-geom", required_argument, 0, A_GEOM},
        {"out", required_argument, 0, A_OUT},          {"app-log", required_argument, 0, A_APPLOG},
        {"env-file", required_argument, 0, A_ENV},     {"no-launcher-env", no_argument, 0, A_NOLENV},
        {"stop-launcher", no_argument, 0, A_STOP},     {"force", no_argument, 0, A_FORCE},
        {"label", required_argument, 0, A_LABEL},      {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};
    int c;
    while ((c = getopt_long(argc, argv, "+h", opts, NULL)) != -1) {
        switch (c) {
        case A_AFTER: O.after = atof(optarg); break;
        case A_EVERY: O.every = atof(optarg); break;
        case A_CYCLES: O.cycles = atoi(optarg); break;
        case A_HOLD: O.hold = atof(optarg); break;
        case A_END: O.end_after = atof(optarg); break;
        case A_ACK: O.ack_ms = atoi(optarg); break;
        case A_BG:
            if (strcmp(optarg, "run") == 0) O.background_run = 1;
            else if (strcmp(optarg, "freeze") == 0) O.background_run = 0;
            else { usage(); return 2; }
            break;
        case A_NOC: O.contract = 0; break;
        case A_INPUT:
            O.input_drain = strcmp(optarg, "drain") == 0 || strcmp(optarg, "grab-drain") == 0;
            O.input_grab = strncmp(optarg, "grab", 4) == 0;
            if (!O.input_drain && !O.input_grab && strcmp(optarg, "none") != 0) { usage(); return 2; }
            break;
        case A_MATCH: O.input_match = optarg; break;
        case A_GRAB: if (O.ngrab_extra < 8) O.grab_extra[O.ngrab_extra++] = optarg; break;
        case A_RESTORE:
            if (strcmp(optarg, "none") == 0) O.restore = 0;
            else if (strcmp(optarg, "snapshot") == 0) O.restore = 1;
            else { usage(); return 2; }
            break;
        case A_WATCH: O.watch_ms = atoi(optarg); break;
        case A_WIV: O.watch_iv = atoi(optarg) > 0 ? atoi(optarg) : 1; break;
        case A_FB: O.fb = optarg; break;
        case A_GEOM: O.fb_geom = optarg; break;
        case A_OUT: O.out = optarg; break;
        case A_APPLOG: O.app_log = optarg; break;
        case A_ENV: O.env_file = optarg; break;
        case A_NOLENV: O.launcher_env = 0; break;
        case A_STOP: O.stop_launcher = 1; break;
        case A_FORCE: O.force = 1; break;
        case A_LABEL: O.label = optarg; break;
        case 'h': usage(); return 0;
        default: usage(); return 2;
        }
    }
    if (optind >= argc) {
        usage();
        return 2;
    }
    O.cmd = argv + optind;
    g_t0 = mta_mono_ms();
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    /* results */
    char out_buf[PATH_MAX], log_buf[PATH_MAX + 16];
    if (!O.out) {
        mta_default_out(out_buf, sizeof(out_buf), "fbhandoff", "txt");
        O.out = out_buf;
    }
    g_res = fopen(O.out, "a");
    if (!g_res) {
        fprintf(stderr, "fbhandoff: cannot write %s: %s\n", O.out, strerror(errno));
        return 1;
    }
    if (!O.app_log) {
        snprintf(log_buf, sizeof(log_buf), "%s-app.log", O.out);
        O.app_log = log_buf;
    }

    /* who owns the panel */
    char fb_buf[PATH_MAX];
    if (!O.fb) {
        default_fb(fb_buf, sizeof(fb_buf));
        O.fb = fb_buf;
    }
    pid_t lp[4];
    int nl = mta_find_launcher(lp, 4);
    if (nl > 0 && O.launcher_env) capture_launcher_env(lp[0]);
    char holders[512];
    int nh = mta_path_holders(O.fb, -1, holders, sizeof(holders));
    char host[64];
    mta_hostname(host, sizeof(host));
    res("rec=start v=1 host=%s fb=%s launcher=%s fb_holders=%s fbcon=%s cmd=\"%s\" out=%s", host, O.fb,
        nl ? "running" : "stopped", nh ? holders : "none", fbcon_state(), O.cmd[0], O.out);
    if ((nl > 0 || nh > 0) && !O.force && !O.stop_launcher) {
        fprintf(stderr,
                "fbhandoff: refusing to run: %s has the panel (%s).\n"
                "  Stop it yourself:   systemctl --user stop cyberdeckzero   (and start it again afterwards)\n"
                "  or let fbhandoff do both: add --stop-launcher.   --force runs anyway (both will draw).\n",
                nl ? "the launcher" : "another process", nh ? holders : "launcher process");
        res("rec=refused reason=panel-owned");
        fclose(g_res);
        return 3;
    }

    /* the framebuffer */
    char err[256];
    if (fb_open(&g_fb, O.fb, O.fb_geom, 1, err, sizeof(err)) != 0) {
        fprintf(stderr, "fbhandoff: %s\n", err);
        res("rec=error what=fb msg=\"%s\"", err);
        return 1;
    }
    g_len = fb_vis_len(&g_fb);
    g_card = malloc(g_len);
    g_sh = mmap(NULL, sizeof(shared_t) + g_len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!g_card || g_sh == MAP_FAILED) {
        fprintf(stderr, "fbhandoff: out of memory\n");
        return 1;
    }
    memset(g_sh, 0, sizeof(shared_t));
    snprintf(g_sh->fb_path, sizeof(g_sh->fb_path), "%s", O.fb);
    snprintf(g_sh->fb_geom, sizeof(g_sh->fb_geom), "%s", O.fb_geom ? O.fb_geom : "");
    if (!realpath(O.out, g_sh->results)) snprintf(g_sh->results, sizeof(g_sh->results), "%s", O.out);
    g_sh->snap_len = g_len;
    memcpy(g_card, fb_vis(&g_fb), g_len);
    res("rec=fb id=%s geom=%dx%dx%d stride=%d visible_bytes=%zu offset=%zu rgb=%d/%d,%d/%d,%d/%d", g_fb.id, g_fb.w,
        g_fb.h, g_fb.bpp, g_fb.stride, g_len, g_fb.vis_off, g_fb.r_off, g_fb.r_len, g_fb.g_off, g_fb.g_len,
        g_fb.b_off, g_fb.b_len);

    /* the guardian, before anything changes */
    int gp[2];
    if (pipe2(gp, O_CLOEXEC) != 0) {
        perror("pipe2");
        return 1;
    }
    fflush(stdout);
    fflush(g_res);
    g_guard = fork();
    if (g_guard == 0) {
        close(gp[1]);
        guardian_main(gp[0]);
    }
    close(gp[0]);
    g_guard_w = gp[1];

    /* stop the launcher when asked */
    if (O.stop_launcher && nl > 0) {
        res("rec=launcher action=stop");
        g_sh->launcher_stopped = 1; /* set first: a stop that half-worked must still be undone */
        const int rc = mta_launcher_service("stop");
        for (int i = 0; i < 50 && (nl = mta_find_launcher(lp, 4)) > 0; ++i) mta_sleep_ms(100, NULL);
        if (rc != 0 || nl > 0) {
            fprintf(stderr, "fbhandoff: could not stop the launcher (systemctl rc=%d)\n", rc);
            cleanup("launcher-stop-failed");
            return 1;
        }
        nh = mta_path_holders(O.fb, -1, holders, sizeof(holders));
        if (nh > 0 && !O.force) {
            fprintf(stderr, "fbhandoff: the panel is still open by %s (--force to run anyway)\n", holders);
            cleanup("panel-still-owned");
            return 3;
        }
    }

    /* the app */
    int sv[2] = {-1, -1};
    if (O.contract && socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) != 0) {
        perror("socketpair");
        cleanup("socketpair-failed");
        return 1;
    }
    const int logfd = open(O.app_log, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    fflush(stdout);
    fflush(g_res);
    g_pid = fork();
    if (g_pid == 0) {
        setpgid(0, 0);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        const int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) dup2(nul, 0);
        if (logfd >= 0) {
            dup2(logfd, 1);
            dup2(logfd, 2);
        }
        for (int i = 0; i < g_nlenv; ++i) {
            char *eq = strchr(g_lenv[i], '=');
            if (!eq) continue;
            *eq = '\0';
            setenv(g_lenv[i], eq + 1, 0);
        }
        if (O.env_file && mta_load_env_file(O.env_file) < 0) fprintf(stderr, "fbhandoff: cannot read %s\n", O.env_file);
        if (sv[1] >= 0) {
            fcntl(sv[1], F_SETFD, 0);
            char num[16];
            snprintf(num, sizeof(num), "%d", sv[1]);
            setenv("APPLAUNCH_LIFECYCLE", "1", 1);
            setenv("APPLAUNCH_CONTROL_FD", num, 1);
        }
        execvp(O.cmd[0], O.cmd);
        fprintf(stderr, "fbhandoff: exec %s: %s\n", O.cmd[0], strerror(errno));
        _exit(127);
    }
    if (g_pid < 0) {
        perror("fork");
        cleanup("fork-failed");
        return 1;
    }
    setpgid(g_pid, g_pid);
    g_pgid = g_pid;
    g_sh->pgid = g_pgid;
    if (logfd >= 0) close(logfd);
    if (sv[1] >= 0) close(sv[1]);
    g_ctl = sv[0];
    {
        char envlist[1024] = "";
        for (int i = 0; i < g_nlenv; ++i) {
            const size_t u = strlen(envlist);
            snprintf(envlist + u, sizeof(envlist) - u, "%s%s", i ? " " : "", g_lenv[i]);
        }
        res("rec=spawn pid=%d pgid=%d contract=%d app_log=%s launcher_env=\"%s\" env_file=%s", (int)g_pid,
            (int)g_pgid, g_ctl >= 0, O.app_log, envlist, O.env_file ? O.env_file : "none");
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_quit;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sa.sa_handler = on_usr1;
    sigaction(SIGUSR1, &sa, NULL);

    /* the loop */
    double next = O.after > 0 ? mta_mono_ms() + O.after * 1000.0 : -1, end_at = -1;
    int done = 0;
    const char *reason = "signal";
    while (!g_quit) {
        if (!app_alive()) {
            reason = "app-exited";
            break;
        }
        const double now = mta_mono_ms();
        if (end_at > 0 && now >= end_at) {
            reason = "cycles-done";
            break;
        }
        const int want = g_trigger || (next > 0 && now >= next);
        if (want && end_at < 0) {
            const char *trig = g_trigger ? "usr1" : "timer";
            g_trigger = 0;
            run_cycle(++done, trig);
            next = O.every > 0 ? mta_mono_ms() + O.every * 1000.0 : -1;
            if (O.cycles > 0 && done >= O.cycles) end_at = mta_mono_ms() + O.end_after * 1000.0;
            continue;
        }
        g_trigger = 0;
        struct pollfd p = {g_ctl, POLLIN, 0};
        const int r = poll(&p, g_ctl >= 0 ? 1 : 0, 50);
        if (r == 1 && g_ctl >= 0) {
            char msg[80];
            const ssize_t got = recv(g_ctl, msg, sizeof(msg) - 1, MSG_DONTWAIT);
            if (got <= 0) {
                close(g_ctl);
                g_ctl = -1;
            } else {
                msg[got] = '\0';
                res("rec=ctl-message msg=\"%s\"", msg);
            }
        }
    }
    cleanup(g_quit ? "signal" : reason);
    fb_close(&g_fb);
    if (g_res) fclose(g_res);
    return 0;
}
