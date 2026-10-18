/*
 * SPDX-License-Identifier: MIT
 *
 * Pointer navigation reader (see cp0_pointer_reader.h and cp0_pointer_nav.h).
 */

#define _GNU_SOURCE
#include "cp0_pointer_reader.h"

#include "cp0_app_contract.h"
#include "cp0_keyboard_presence.h"
#include "cp0_pointer_nav.h"
#include "keyboard_input.h"
#include "../../../../SDK/components/utilities/include/sample_log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/input.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#endif

#ifdef __linux__

#define PTR_MAX_SOURCES 4     /* pointer devices read at the same time */
#define PTR_IDLE_WAKE_MS 1000 /* nothing pending: look at the switch (an app's input.state) and the list this often */
#define PTR_RETRY_MS 1000     /* a listed node that cannot be opened yet (udev permissions) is tried again */
#define PTR_MAX_DT_MS 600000u /* a longer gap is fed as this (the core only needs to know it was long) */

typedef struct {
    int fd;
    dev_t rdev;
    char path[CP0_KBD_PRESENCE_PATH_SIZE];
    char name[CP0_KBD_PRESENCE_NAME_SIZE];
    int mono_clock;         /* event times are CLOCK_MONOTONIC (EVIOCSCLOCKID on this handle only) */
    int dropping;           /* SYN_DROPPED: events are skipped up to the next SYN_REPORT */
    uint64_t last_ms;       /* the core's clock, in monotonic ms */
    cp0_ptr_report_t frame; /* the report being collected up to SYN_REPORT */
    int frame_used;
    cp0_ptr_nav_t nav;
} ptr_source_t;

typedef struct {
    char path[CP0_APP_PATH_MAX + 32];
    int have_path;
    int valid;
    dev_t dev;
    ino_t ino;
    struct timespec mtim;
    off_t size;
    int pointer; /* pointer=on */
} state_file_t;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER; /* start / stop */
static pthread_t s_thread;
static int s_started;
static atomic_bool s_stop = false;
static atomic_int s_wake_fd = -1; /* created once, never closed: wake() may run on any thread at any time */
static int s_debug;

static uint64_t mono_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static const char *env_lookup(const char *name)
{
    return getenv(name);
}

/* 1 / 0 for on / off words, -1 when unset or unknown */
static int env_switch(const char *name)
{
    const char *v = getenv(name);
    if (!v || !*v) return -1;
    if (!strcmp(v, "1") || !strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "true")) return 1;
    if (!strcmp(v, "0") || !strcmp(v, "off") || !strcmp(v, "no") || !strcmp(v, "false")) return 0;
    return -1;
}

void cp0_pointer_reader_wake(void)
{
    const int fd = atomic_load(&s_wake_fd);
    if (fd < 0) return;
    const uint64_t one = 1;
    if (write(fd, &one, sizeof(one)) < 0) {
        /* full counter or no fd: the idle wake-up comes within PTR_IDLE_WAKE_MS anyway */
    }
}

/* ------------------------------------------------------------------ the switch */

static void state_file_refresh(state_file_t *sf)
{
    if (!sf->have_path) {
        if (cp0_app_state_path(CP0_APP_INPUT_FILE, sf->path, sizeof(sf->path)) != 0) return;
        sf->have_path = 1;
    }
    struct stat st;
    if (stat(sf->path, &st) != 0) {
        sf->valid = 0;
        sf->pointer = 0; /* no launcher file: off */
        return;
    }
    if (sf->valid && st.st_dev == sf->dev && st.st_ino == sf->ino && st.st_size == sf->size &&
        st.st_mtim.tv_sec == sf->mtim.tv_sec && st.st_mtim.tv_nsec == sf->mtim.tv_nsec)
        return; /* the launcher writes by rename: a new inode for every change */
    cp0_app_input_t in;
    sf->pointer = cp0_app_input_read(sf->path, &in) == 0 ? in.pointer_nav : 0;
    sf->valid = 1;
    sf->dev = st.st_dev;
    sf->ino = st.st_ino;
    sf->size = st.st_size;
    sf->mtim = st.st_mtim;
}

/* pointer navigation on? APPLAUNCH_POINTER_NAV, then the process's own switch (the launcher's setting), then the
 * launcher's input.state (an app) */
static int decide(int env_force, const state_file_t *sf, const char **why)
{
    if (env_force >= 0) {
        *why = "APPLAUNCH_POINTER_NAV";
        return env_force;
    }
    const int setting = cp0_pointer_nav_get_override();
    if (setting >= 0) {
        *why = "setting";
        return setting;
    }
    *why = "input.state";
    return sf->pointer;
}

/* ------------------------------------------------------------------ keys */

static void emit_keys(const ptr_source_t *s, const cp0_ptr_key_t *keys, int n)
{
    for (int i = 0; i < n; i++) {
        const cp0_ptr_key_t key = keys[i];
        if (cp0_keyboard_input_paused()) {
            if (s_debug) SLOGI("[PTR] %s dropped (paused)", cp0_ptr_key_name(key));
            continue;
        }
        /* a text field: Left / Right would move the caret, Ctrl+O is a control character in a terminal */
        if (cp0_keyboard_get_input_context() == KBD_INPUT_CONTEXT_TEXT &&
            (key == CP0_PTR_KEY_LEFT || key == CP0_PTR_KEY_RIGHT || key == CP0_PTR_KEY_OPTIONS)) {
            if (s_debug) SLOGI("[PTR] %s held back (text field)", cp0_ptr_key_name(key));
            continue;
        }
        uint32_t code = 0, mods = 0;
        switch (key) {
        case CP0_PTR_KEY_UP: code = KEY_UP; break;
        case CP0_PTR_KEY_DOWN: code = KEY_DOWN; break;
        case CP0_PTR_KEY_LEFT: code = KEY_LEFT; break;
        case CP0_PTR_KEY_RIGHT: code = KEY_RIGHT; break;
        case CP0_PTR_KEY_ENTER: code = KEY_ENTER; break;
        case CP0_PTR_KEY_OPTIONS:
            code = KEY_O;
            mods = KBD_MOD_CTRL;
            break;
        case CP0_PTR_KEY_PAGE_UP: code = KEY_PAGEUP; break;
        case CP0_PTR_KEY_PAGE_DOWN: code = KEY_PAGEDOWN; break;
        default: continue;
        }
        /* a key tap, exactly like a physical key's press and release */
        (void)cp0_keyboard_inject(code, KBD_KEY_PRESSED, mods);
        (void)cp0_keyboard_inject(code, KBD_KEY_RELEASED, mods);
        if (s_debug) SLOGI("[PTR] %s: %s", s->path, cp0_ptr_key_name(key));
    }
}

/* ------------------------------------------------------------------ devices */

static void source_init(ptr_source_t *s)
{
    memset(s, 0, sizeof(*s));
    s->fd = -1;
}

static void source_close(ptr_source_t *s, const char *reason)
{
    if (s->fd < 0) return;
    close(s->fd);
    SLOGI("[PTR] stopped reading %s (%s)", s->path, reason);
    source_init(s); /* any push or press in progress is forgotten: nothing is sent for it */
}

static int source_open(ptr_source_t *sources, int slot, const cp0_kbd_presence_device_t *device,
                       const cp0_ptr_nav_params_t *params)
{
    static char s_failed[CP0_KBD_PRESENCE_PATH_SIZE]; /* the last node whose failure was logged (reader thread) */
    const int fd = open(device->path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { /* not readable yet (udev permissions), or gone: retried; said once */
        if (errno != ENOENT && strcmp(s_failed, device->path) != 0) {
            SLOGW("[PTR] cannot read %s (%s): %s", device->path, device->name, strerror(errno));
            snprintf(s_failed, sizeof(s_failed), "%s", device->path);
        }
        return -1;
    }
    if (strcmp(s_failed, device->path) == 0) s_failed[0] = '\0';
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISCHR(st.st_mode)) {
        close(fd);
        return -1;
    }
    for (int i = 0; i < PTR_MAX_SOURCES; i++) {
        if (sources[i].fd >= 0 && sources[i].rdev == st.st_rdev) { /* the same device under another name */
            close(fd);
            return 0;
        }
    }
    ptr_source_t *s = &sources[slot];
    source_init(s);
    s->fd = fd;
    s->rdev = st.st_rdev;
    snprintf(s->path, sizeof(s->path), "%s", device->path);
    snprintf(s->name, sizeof(s->name), "%s", device->name);
    int clock = CLOCK_MONOTONIC;
    s->mono_clock = ioctl(fd, EVIOCSCLOCKID, &clock) == 0;
    cp0_ptr_nav_init(&s->nav, params);
    s->last_ms = mono_ms();
    SLOGI("[PTR] reading %s (%s), no grab", s->path, s->name);
    return 0;
}

static int listed(const cp0_kbd_presence_list_t *list, const char *path)
{
    for (unsigned i = 0; i < list->count; i++)
        if (strcmp(list->devices[i].path, path) == 0) return 1;
    return 0;
}

/* Close what left the list, open what is new. Returns 1 when a listed device could not be opened (retry). */
static int reconcile(ptr_source_t *sources, const cp0_kbd_presence_list_t *list, const cp0_ptr_nav_params_t *params)
{
    for (int i = 0; i < PTR_MAX_SOURCES; i++)
        if (sources[i].fd >= 0 && !listed(list, sources[i].path)) source_close(&sources[i], "gone");
    int missing = 0;
    for (unsigned d = 0; d < list->count; d++) {
        int open_already = 0, free_slot = -1;
        for (int i = 0; i < PTR_MAX_SOURCES; i++) {
            if (sources[i].fd >= 0 && strcmp(sources[i].path, list->devices[d].path) == 0) open_already = 1;
            if (sources[i].fd < 0 && free_slot < 0) free_slot = i;
        }
        if (open_already) continue;
        if (free_slot < 0) break; /* PTR_MAX_SOURCES pointers already */
        if (source_open(sources, free_slot, &list->devices[d], params) != 0) missing = 1;
    }
    return missing;
}

static void close_all(ptr_source_t *sources, const char *reason)
{
    for (int i = 0; i < PTR_MAX_SOURCES; i++) source_close(&sources[i], reason);
}

static int any_open(const ptr_source_t *sources)
{
    for (int i = 0; i < PTR_MAX_SOURCES; i++)
        if (sources[i].fd >= 0) return 1;
    return 0;
}

static void log_list(const cp0_kbd_presence_list_t *list)
{
    char names[512];
    size_t used = 0;
    names[0] = '\0';
    for (unsigned i = 0; i < list->count && used < sizeof(names); i++) {
        const int n = snprintf(names + used, sizeof(names) - used, "%s%s (%s)", i ? ", " : "", list->devices[i].name,
                               list->devices[i].path);
        if (n < 0) break;
        used += (size_t)n;
    }
    SLOGI("[PTR] pointer devices: %s", list->count ? names : "none");
}

/* ------------------------------------------------------------------ events */

static uint32_t clamp_dt(uint64_t later, uint64_t earlier)
{
    if (later <= earlier) return 0;
    const uint64_t dt = later - earlier;
    return dt > PTR_MAX_DT_MS ? PTR_MAX_DT_MS : (uint32_t)dt;
}

static void frame_flush(ptr_source_t *s, uint64_t event_ms)
{
    if (!s->frame_used) return;
    const uint64_t t = s->mono_clock ? event_ms : mono_ms();
    cp0_ptr_key_t keys[CP0_PTR_NAV_MAX_KEYS];
    const int n = cp0_ptr_nav_step(&s->nav, clamp_dt(t, s->last_ms), &s->frame, keys, CP0_PTR_NAV_MAX_KEYS);
    if (t > s->last_ms) s->last_ms = t;
    memset(&s->frame, 0, sizeof(s->frame));
    s->frame_used = 0;
    emit_keys(s, keys, n);
}

static void handle_event(ptr_source_t *s, const struct input_event *ev)
{
    const uint64_t t = (uint64_t)ev->input_event_sec * 1000u + (uint64_t)ev->input_event_usec / 1000u;
    if (s->dropping) {
        if (ev->type == EV_SYN && ev->code == SYN_REPORT) s->dropping = 0;
        return;
    }
    switch (ev->type) {
    case EV_REL:
        if (ev->code == REL_X) s->frame.dx += ev->value;
        else if (ev->code == REL_Y) s->frame.dy += ev->value;
        else if (ev->code == REL_WHEEL) s->frame.wheel += ev->value; /* notches; the hi-res copy is ignored */
        else return;
        s->frame_used = 1;
        break;
    case EV_KEY:
        if (ev->code != BTN_LEFT || (ev->value != 0 && ev->value != 1)) return;
        {
            const cp0_ptr_button_t button = ev->value ? CP0_PTR_BUTTON_DOWN : CP0_PTR_BUTTON_UP;
            if (s->frame.button != CP0_PTR_BUTTON_SAME && s->frame.button != button) frame_flush(s, t);
            s->frame.button = button;
            s->frame_used = 1;
            if (s_debug) SLOGI("[PTR] %s: button %s", s->path, ev->value ? "down" : "up");
        }
        break;
    case EV_SYN:
        if (ev->code == SYN_REPORT) {
            frame_flush(s, t);
        } else if (ev->code == SYN_DROPPED) { /* the kernel buffer overflowed: start again from a clean state */
            s->dropping = 1;
            memset(&s->frame, 0, sizeof(s->frame));
            s->frame_used = 0;
            cp0_ptr_nav_reset(&s->nav);
        }
        break;
    default:
        break;
    }
}

/* Returns -1 when the device went away. */
static int read_events(ptr_source_t *s)
{
    struct input_event events[64];
    for (;;) {
        const ssize_t n = read(s->fd, events, sizeof(events));
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) return 0;
            return -1; /* ENODEV: unplugged */
        }
        if (n == 0) return 0;
        const size_t count = (size_t)n / sizeof(events[0]);
        for (size_t i = 0; i < count && s->fd >= 0; i++) handle_event(s, &events[i]);
        if ((size_t)n < sizeof(events)) return 0;
    }
}

/* timers of the core (repeat steps, release, hold) up to now */
static void serve(ptr_source_t *s, uint64_t now)
{
    cp0_ptr_key_t keys[CP0_PTR_NAV_MAX_KEYS];
    const int n = cp0_ptr_nav_step(&s->nav, clamp_dt(now, s->last_ms), NULL, keys, CP0_PTR_NAV_MAX_KEYS);
    if (now > s->last_ms) s->last_ms = now;
    emit_keys(s, keys, n);
}

/* ------------------------------------------------------------------ thread */

static void *reader_main(void *arg)
{
    (void)arg;
    cp0_ptr_nav_params_t params;
    cp0_ptr_nav_params_default(&params);
    const int overrides = cp0_ptr_nav_params_apply_env(&params, env_lookup);
    const int env_force = env_switch("APPLAUNCH_POINTER_NAV");
    s_debug = env_switch("APPLAUNCH_POINTER_DEBUG") == 1;

    cp0_kbd_presence_watcher_t *watcher = cp0_kbd_presence_shared_acquire();
    if (!watcher) {
        SLOGW("[PTR] keyboard presence watcher unavailable: no pointer navigation");
        return NULL;
    }
    SLOGI("[PTR] reader ready: dead zone %d, full level %d, %d.%d to %d.%d steps/s, repeat delay %d ms, release %d ms, "
          "hold %d ms, tap slop %d, click guard %d ms (%d APPLAUNCH_POINTER_* override%s)%s",
          params.dead_zone, params.full_level, params.slow_rate_x10 / 10, params.slow_rate_x10 % 10,
          params.max_rate_x10 / 10, params.max_rate_x10 % 10, params.repeat_delay_ms, params.release_ms,
          params.hold_ms, params.tap_slop, params.click_guard_ms, overrides, overrides == 1 ? "" : "s",
          s_debug ? ", debug log" : "");

    ptr_source_t sources[PTR_MAX_SOURCES];
    for (int i = 0; i < PTR_MAX_SOURCES; i++) source_init(&sources[i]);
    cp0_kbd_presence_list_t list;
    list.count = 0;
    unsigned seen_gen = 0;
    int logged_on = -1, was_wanted = 0;
    state_file_t sf;
    memset(&sf, 0, sizeof(sf));
    uint64_t next_file_check = 0, next_retry = UINT64_MAX;

    while (!atomic_load(&s_stop)) {
        uint64_t now = mono_ms();
        /* 1. the switch */
        if (env_force < 0 && cp0_pointer_nav_get_override() < 0 && now >= next_file_check) {
            state_file_refresh(&sf);
            next_file_check = now + PTR_IDLE_WAKE_MS;
        }
        const char *why = "";
        const int on = decide(env_force, &sf, &why);
        if (on != logged_on) {
            SLOGI("[PTR] pointer navigation %s (%s)", on ? "on" : "off", why);
            logged_on = on;
        }
        const int paused = cp0_keyboard_input_paused();
        const int wanted = on && !paused;

        /* 2. the pointer list (hot-plug through the presence watcher) */
        int list_changed = 0;
        const unsigned gen = cp0_kbd_presence_watcher_pointer_generation(watcher);
        if (gen != seen_gen) {
            seen_gen = cp0_kbd_presence_watcher_pointer_snapshot(watcher, &list);
            log_list(&list);
            list_changed = 1;
        }

        /* 3. open or close */
        if (!wanted) {
            if (any_open(sources)) close_all(sources, paused ? "keyboard paused: an app runs" : "pointer navigation off");
            next_retry = UINT64_MAX;
        } else if (list_changed || !was_wanted || now >= next_retry) {
            next_retry = reconcile(sources, &list, &params) ? now + PTR_RETRY_MS : UINT64_MAX;
        }
        was_wanted = wanted;

        /* 4. wait: the wake-up, the devices, the first timer of a core, the retry, or the idle tick */
        struct pollfd fds[1 + PTR_MAX_SOURCES];
        int slot_of[1 + PTR_MAX_SOURCES];
        nfds_t nfds = 1;
        fds[0].fd = atomic_load(&s_wake_fd);
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        int timeout = PTR_IDLE_WAKE_MS;
        for (int i = 0; i < PTR_MAX_SOURCES; i++) {
            if (sources[i].fd < 0) continue;
            fds[nfds].fd = sources[i].fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            slot_of[nfds++] = i;
            const int next = cp0_ptr_nav_next_ms(&sources[i].nav);
            if (next >= 0 && next < timeout) timeout = next;
        }
        if (next_retry != UINT64_MAX) {
            const uint64_t until = next_retry > now ? next_retry - now : 0;
            if (until < (uint64_t)timeout) timeout = (int)until;
        }
        const int ready = poll(fds, nfds, timeout);
        if (ready < 0 && errno != EINTR) usleep(100000); /* never spin */
        if (atomic_load(&s_stop)) break;
        if (ready > 0 && (fds[0].revents & POLLIN)) {
            uint64_t value;
            while (read(fds[0].fd, &value, sizeof(value)) > 0) {}
        }

        /* 5. reports */
        for (nfds_t k = 1; ready > 0 && k < nfds; k++) {
            ptr_source_t *s = &sources[slot_of[k]];
            if (s->fd < 0) continue;
            if ((fds[k].revents & POLLIN) && read_events(s) != 0) {
                source_close(s, "gone");
                next_retry = mono_ms() + PTR_RETRY_MS;
                continue;
            }
            if (fds[k].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                source_close(s, "gone");
                next_retry = mono_ms() + PTR_RETRY_MS;
            }
        }

        /* 6. timers */
        now = mono_ms();
        for (int i = 0; i < PTR_MAX_SOURCES; i++)
            if (sources[i].fd >= 0) serve(&sources[i], now);
    }

    close_all(sources, "stopped");
    cp0_kbd_presence_shared_release();
    return NULL;
}

void cp0_pointer_reader_start(void)
{
    pthread_mutex_lock(&s_lock);
    if (s_started) {
        pthread_mutex_unlock(&s_lock);
        return;
    }
    if (atomic_load(&s_wake_fd) < 0) {
        const int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (fd < 0) {
            SLOGW("[PTR] eventfd: %s: no pointer navigation", strerror(errno));
            pthread_mutex_unlock(&s_lock);
            return;
        }
        atomic_store(&s_wake_fd, fd);
    }
    atomic_store(&s_stop, false);
    if (pthread_create(&s_thread, NULL, reader_main, NULL) != 0) {
        SLOGW("[PTR] cannot start the pointer reader: %s", strerror(errno));
        pthread_mutex_unlock(&s_lock);
        return;
    }
    (void)pthread_setname_np(s_thread, "cp0-pointer");
    s_started = 1;
    cp0_pointer_nav_set_wake_hook(cp0_pointer_reader_wake);
    pthread_mutex_unlock(&s_lock);
}

void cp0_pointer_reader_stop(void)
{
    pthread_mutex_lock(&s_lock);
    if (!s_started) {
        pthread_mutex_unlock(&s_lock);
        return;
    }
    cp0_pointer_nav_set_wake_hook(NULL);
    atomic_store(&s_stop, true);
    cp0_pointer_reader_wake();
    if (!pthread_equal(s_thread, pthread_self())) pthread_join(s_thread, NULL);
    s_started = 0;
    pthread_mutex_unlock(&s_lock);
}

#else /* !__linux__ */

void cp0_pointer_reader_start(void) {}
void cp0_pointer_reader_stop(void) {}
void cp0_pointer_reader_wake(void) {}

#endif
