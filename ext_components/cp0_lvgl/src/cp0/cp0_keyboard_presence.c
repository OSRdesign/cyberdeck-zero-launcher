/*
 * Keyboard presence (see cp0_keyboard_presence.h): /proc/bus/input/devices parser, rescan timing and the
 * inotify watcher.
 */

#define _GNU_SOURCE
#include "cp0_keyboard_presence.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#endif

/* evdev codes of the four keys a real keyboard has (linux/input-event-codes.h) */
enum { PRESENCE_KEY_ENTER = 28, PRESENCE_KEY_A = 30, PRESENCE_KEY_Z = 44, PRESENCE_KEY_SPACE = 57 };
#define PRESENCE_BUS_VIRTUAL 0x0006u
#define PRESENCE_HUB_PREFIX "applaunch-"

/* ============================================================
 *  Parser
 * ============================================================ */

unsigned cp0_kbd_presence_native_word_bits(void)
{
    return (unsigned)(sizeof(long) * CHAR_BIT);
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int cp0_kbd_presence_bitmap_bit(const char *bitmap, size_t len, unsigned word_bits, unsigned bit)
{
    if (!bitmap || (word_bits != 32 && word_bits != 64)) return 0;
    const unsigned word = bit / word_bits;
    const unsigned digit = (bit % word_bits) / 4;
    const unsigned shift = bit % 4;

    /* words are counted from the right: the last one holds bits 0..word_bits-1 */
    size_t end = len;
    unsigned index = 0;
    for (;;) {
        while (end > 0 && is_space(bitmap[end - 1])) end--;
        if (end == 0) return 0;
        size_t start = end;
        while (start > 0 && !is_space(bitmap[start - 1])) start--;
        if (index == word) {
            if (digit >= end - start) return 0; /* leading zero digits are not printed */
            const int value = hex_value(bitmap[end - 1 - digit]);
            return value < 0 ? 0 : (value >> shift) & 1;
        }
        index++;
        end = start;
    }
}

typedef struct {
    unsigned word_bits;
    cp0_kbd_presence_list_t *out;
    int total;
    /* the device block being read */
    int is_virtual;
    int has_keys;
    char event[16];
    char name[CP0_KBD_PRESENCE_NAME_SIZE];
} presence_parser_t;

static void parser_reset_block(presence_parser_t *p)
{
    p->is_virtual = 0;
    p->has_keys = 0;
    p->event[0] = '\0';
    p->name[0] = '\0';
}

static void parser_init(presence_parser_t *p, unsigned word_bits, cp0_kbd_presence_list_t *out)
{
    p->word_bits = word_bits ? word_bits : cp0_kbd_presence_native_word_bits();
    p->out = out;
    p->total = 0;
    if (out) out->count = 0;
    parser_reset_block(p);
}

static void parser_flush(presence_parser_t *p)
{
    const int keyboard = p->event[0] != '\0' && p->has_keys && !p->is_virtual &&
                         strncmp(p->name, PRESENCE_HUB_PREFIX, sizeof(PRESENCE_HUB_PREFIX) - 1) != 0;
    if (keyboard) {
        if (p->out && p->out->count < CP0_KBD_PRESENCE_MAX_DEVICES) {
            cp0_kbd_presence_device_t *device = &p->out->devices[p->out->count++];
            snprintf(device->path, sizeof(device->path), CP0_KBD_PRESENCE_DEV_DIR "/%s", p->event);
            snprintf(device->name, sizeof(device->name), "%s", p->name);
        }
        p->total++;
    }
    parser_reset_block(p);
}

static int line_starts(const char *line, size_t len, const char *prefix, size_t *prefix_len)
{
    const size_t n = strlen(prefix);
    if (len < n || memcmp(line, prefix, n) != 0) return 0;
    *prefix_len = n;
    return 1;
}

static void parser_line(presence_parser_t *p, const char *line, size_t len)
{
    while (len > 0 && is_space(line[len - 1])) len--;
    if (len == 0) {
        parser_flush(p); /* blank line: end of a device block */
        return;
    }
    size_t skip = 0;
    if (line_starts(line, len, "I:", &skip)) {
        for (size_t i = skip; i + 4 <= len; i++) {
            if (memcmp(line + i, "Bus=", 4) != 0) continue;
            unsigned bus = 0;
            size_t j = i + 4;
            for (; j < len && hex_value(line[j]) >= 0; j++) bus = bus * 16u + (unsigned)hex_value(line[j]);
            p->is_virtual = j > i + 4 && bus == PRESENCE_BUS_VIRTUAL;
            break;
        }
    } else if (line_starts(line, len, "N: Name=", &skip)) {
        const char *value = line + skip;
        size_t value_len = len - skip;
        if (value_len > 0 && value[0] == '"') { value++; value_len--; }
        if (value_len > 0 && value[value_len - 1] == '"') value_len--;
        if (value_len >= sizeof(p->name)) value_len = sizeof(p->name) - 1;
        memcpy(p->name, value, value_len);
        p->name[value_len] = '\0';
    } else if (line_starts(line, len, "H: Handlers=", &skip)) {
        size_t i = skip;
        while (i < len) {
            while (i < len && is_space(line[i])) i++;
            const size_t start = i;
            while (i < len && !is_space(line[i])) i++;
            const size_t token_len = i - start;
            if (token_len > 5 && token_len < sizeof(p->event) && memcmp(line + start, "event", 5) == 0) {
                int digits = 1;
                for (size_t k = start + 5; k < i; k++) digits = digits && line[k] >= '0' && line[k] <= '9';
                if (digits) {
                    memcpy(p->event, line + start, token_len);
                    p->event[token_len] = '\0';
                }
            }
        }
    } else if (line_starts(line, len, "B: KEY=", &skip)) {
        const char *bitmap = line + skip;
        const size_t bitmap_len = len - skip;
        p->has_keys = cp0_kbd_presence_bitmap_bit(bitmap, bitmap_len, p->word_bits, PRESENCE_KEY_A) &&
                      cp0_kbd_presence_bitmap_bit(bitmap, bitmap_len, p->word_bits, PRESENCE_KEY_Z) &&
                      cp0_kbd_presence_bitmap_bit(bitmap, bitmap_len, p->word_bits, PRESENCE_KEY_ENTER) &&
                      cp0_kbd_presence_bitmap_bit(bitmap, bitmap_len, p->word_bits, PRESENCE_KEY_SPACE);
    }
}

int cp0_kbd_presence_parse(const char *text, size_t len, unsigned word_bits, cp0_kbd_presence_list_t *out)
{
    presence_parser_t p;
    parser_init(&p, word_bits, out);
    size_t start = 0;
    for (size_t i = 0; text && i < len; i++) {
        if (text[i] != '\n') continue;
        parser_line(&p, text + start, i - start);
        start = i + 1;
    }
    if (text && start < len) parser_line(&p, text + start, len - start);
    parser_flush(&p);
    return p.total;
}

int cp0_kbd_presence_enumerate(const char *proc_path, unsigned word_bits, cp0_kbd_presence_list_t *out)
{
    if (out) out->count = 0;
    const int fd = open(proc_path ? proc_path : CP0_KBD_PRESENCE_PROC_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;

    presence_parser_t p;
    parser_init(&p, word_bits, out);
    char chunk[2048];
    char line[512]; /* the longest real line, a 32-bit KEY bitmap, is about 230 bytes */
    size_t line_len = 0;
    for (;;) {
        const ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            if (out) out->count = 0;
            return -1;
        }
        if (n == 0) break;
        for (ssize_t i = 0; i < n; i++) {
            if (chunk[i] == '\n') {
                parser_line(&p, line, line_len);
                line_len = 0;
            } else if (line_len < sizeof(line)) {
                line[line_len++] = chunk[i]; /* an over-long line keeps its beginning */
            }
        }
    }
    close(fd);
    if (line_len > 0) parser_line(&p, line, line_len);
    parser_flush(&p);
    return p.total;
}

int cp0_kbd_presence_count(const char *proc_path)
{
    return cp0_kbd_presence_enumerate(proc_path, 0, NULL);
}

int cp0_kbd_presence_list_equal(const cp0_kbd_presence_list_t *a, const cp0_kbd_presence_list_t *b)
{
    if (!a || !b) return a == b;
    if (a->count != b->count) return 0;
    for (unsigned i = 0; i < a->count; i++) {
        if (strcmp(a->devices[i].path, b->devices[i].path) != 0) return 0;
        if (strcmp(a->devices[i].name, b->devices[i].name) != 0) return 0;
    }
    return 1;
}

/* ============================================================
 *  Rescan timing
 * ============================================================ */

void cp0_kbd_presence_timer_init(cp0_kbd_presence_timer_t *t, uint32_t debounce_ms, uint32_t max_wait_ms,
                                 uint32_t poll_ms, uint64_t now_ms)
{
    t->debounce_ms = debounce_ms ? debounce_ms : CP0_KBD_PRESENCE_DEBOUNCE_MS;
    t->max_wait_ms = max_wait_ms ? max_wait_ms : CP0_KBD_PRESENCE_MAX_WAIT_MS;
    t->poll_ms = poll_ms ? poll_ms : CP0_KBD_PRESENCE_POLL_MS;
    t->pending = 0;
    t->first_event_ms = 0;
    t->deadline_ms = 0;
    t->next_poll_ms = now_ms + t->poll_ms;
}

void cp0_kbd_presence_timer_on_event(cp0_kbd_presence_timer_t *t, uint64_t now_ms)
{
    if (!t->pending) {
        t->pending = 1;
        t->first_event_ms = now_ms;
    }
    const uint64_t latest = t->first_event_ms + t->max_wait_ms;
    const uint64_t deadline = now_ms + t->debounce_ms;
    t->deadline_ms = deadline < latest ? deadline : latest;
}

int cp0_kbd_presence_timer_due(const cp0_kbd_presence_timer_t *t, uint64_t now_ms)
{
    return (t->pending && now_ms >= t->deadline_ms) || now_ms >= t->next_poll_ms;
}

void cp0_kbd_presence_timer_scanned(cp0_kbd_presence_timer_t *t, uint64_t now_ms)
{
    t->pending = 0;
    t->next_poll_ms = now_ms + t->poll_ms;
}

int cp0_kbd_presence_timer_timeout_ms(const cp0_kbd_presence_timer_t *t, uint64_t now_ms)
{
    uint64_t target = t->next_poll_ms;
    if (t->pending && t->deadline_ms < target) target = t->deadline_ms;
    if (target <= now_ms) return 0;
    const uint64_t wait = target - now_ms;
    return wait > (uint64_t)INT_MAX ? INT_MAX : (int)wait;
}

/* ============================================================
 *  Watcher
 * ============================================================ */

#ifdef __linux__

#define PRESENCE_MAX_LISTENERS 4

struct cp0_kbd_presence_listener_slot {
    cp0_kbd_presence_listener_t fn;
    void *user;
};

struct cp0_kbd_presence_watcher {
    char proc_path[256];
    char dev_dir[256];
    unsigned word_bits;
    uint32_t debounce_ms;
    uint32_t max_wait_ms;
    uint32_t poll_ms;

    pthread_t thread;
    int inotify_fd;
    int wake_fd;
    atomic_int stop;

    pthread_mutex_t list_lock;          /* guards list */
    cp0_kbd_presence_list_t list;
    atomic_uint generation;

    pthread_mutex_t listener_lock;      /* guards listeners and serialises their calls */
    struct cp0_kbd_presence_listener_slot listeners[PRESENCE_MAX_LISTENERS];

    cp0_kbd_presence_list_t scratch;    /* watcher thread only */
};

static uint64_t presence_monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void watcher_notify(cp0_kbd_presence_watcher_t *w, const cp0_kbd_presence_list_t *list, unsigned gen)
{
    pthread_mutex_lock(&w->listener_lock);
    for (int i = 0; i < PRESENCE_MAX_LISTENERS; i++)
        if (w->listeners[i].fn) w->listeners[i].fn(list, gen, w->listeners[i].user);
    pthread_mutex_unlock(&w->listener_lock);
}

static void watcher_scan(cp0_kbd_presence_watcher_t *w)
{
    /* an unreadable file keeps the previous list */
    if (cp0_kbd_presence_enumerate(w->proc_path, w->word_bits, &w->scratch) < 0) return;
    unsigned gen = 0;
    pthread_mutex_lock(&w->list_lock);
    const int changed = !cp0_kbd_presence_list_equal(&w->list, &w->scratch);
    if (changed) {
        w->list = w->scratch;
        gen = atomic_fetch_add_explicit(&w->generation, 1u, memory_order_acq_rel) + 1u;
    }
    pthread_mutex_unlock(&w->list_lock);
    if (changed) watcher_notify(w, &w->scratch, gen);
}

static void *watcher_main(void *arg)
{
    cp0_kbd_presence_watcher_t *w = (cp0_kbd_presence_watcher_t *)arg;
    union {
        struct inotify_event event;
        char bytes[4096];
    } events;
    cp0_kbd_presence_timer_t timer;
    cp0_kbd_presence_timer_init(&timer, w->debounce_ms, w->max_wait_ms, w->poll_ms, presence_monotonic_ms());

    while (!atomic_load_explicit(&w->stop, memory_order_acquire)) {
        struct pollfd fds[2];
        nfds_t nfds = 1;
        fds[0].fd = w->wake_fd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        if (w->inotify_fd >= 0) {
            fds[1].fd = w->inotify_fd;
            fds[1].events = POLLIN;
            fds[1].revents = 0;
            nfds = 2;
        }
        const int ready = poll(fds, nfds, cp0_kbd_presence_timer_timeout_ms(&timer, presence_monotonic_ms()));
        if (ready < 0 && errno != EINTR) usleep(100000); /* never spin if poll itself fails */
        if (atomic_load_explicit(&w->stop, memory_order_acquire)) break;

        int changed = 0;
        if (ready > 0) {
            if (fds[0].revents & POLLIN) {
                uint64_t value;
                while (read(w->wake_fd, &value, sizeof(value)) > 0) {}
            }
            if (nfds == 2 && (fds[1].revents & POLLIN)) {
                while (read(w->inotify_fd, events.bytes, sizeof(events.bytes)) > 0) changed = 1;
            }
            if (nfds == 2 && (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                close(w->inotify_fd); /* the safety poll carries on alone */
                w->inotify_fd = -1;
            }
        }
        const uint64_t now = presence_monotonic_ms();
        if (changed) cp0_kbd_presence_timer_on_event(&timer, now);
        if (cp0_kbd_presence_timer_due(&timer, now)) {
            watcher_scan(w);
            cp0_kbd_presence_timer_scanned(&timer, presence_monotonic_ms());
        }
    }
    return NULL;
}

cp0_kbd_presence_watcher_t *cp0_kbd_presence_watcher_start(const cp0_kbd_presence_options_t *options)
{
    cp0_kbd_presence_watcher_t *w = (cp0_kbd_presence_watcher_t *)calloc(1, sizeof(*w));
    if (!w) return NULL;
    snprintf(w->proc_path, sizeof(w->proc_path), "%s",
             options && options->proc_path ? options->proc_path : CP0_KBD_PRESENCE_PROC_PATH);
    snprintf(w->dev_dir, sizeof(w->dev_dir), "%s",
             options && options->dev_dir ? options->dev_dir : CP0_KBD_PRESENCE_DEV_DIR);
    w->word_bits = options && options->word_bits ? options->word_bits : cp0_kbd_presence_native_word_bits();
    w->debounce_ms = options ? options->debounce_ms : 0;
    w->max_wait_ms = options ? options->max_wait_ms : 0;
    w->poll_ms = options ? options->poll_ms : 0;
    atomic_init(&w->stop, 0);
    atomic_init(&w->generation, 0u);
    pthread_mutex_init(&w->list_lock, NULL);
    pthread_mutex_init(&w->listener_lock, NULL);

    w->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    w->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (w->inotify_fd >= 0 &&
        inotify_add_watch(w->inotify_fd, w->dev_dir,
                          IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO) < 0) {
        close(w->inotify_fd);
        w->inotify_fd = -1;
    }
    if (w->wake_fd < 0) goto fail;

    (void)cp0_kbd_presence_enumerate(w->proc_path, w->word_bits, &w->list);
    atomic_store_explicit(&w->generation, 1u, memory_order_release);

    if (pthread_create(&w->thread, NULL, watcher_main, w) != 0) goto fail;
    return w;

fail:
    if (w->inotify_fd >= 0) close(w->inotify_fd);
    if (w->wake_fd >= 0) close(w->wake_fd);
    pthread_mutex_destroy(&w->list_lock);
    pthread_mutex_destroy(&w->listener_lock);
    free(w);
    return NULL;
}

void cp0_kbd_presence_watcher_stop(cp0_kbd_presence_watcher_t *w)
{
    if (!w) return;
    atomic_store_explicit(&w->stop, 1, memory_order_release);
    const uint64_t one = 1;
    if (write(w->wake_fd, &one, sizeof(one)) < 0) {
        /* the poll timeout (at most the safety poll interval) ends the thread anyway */
    }
    pthread_join(w->thread, NULL);
    if (w->inotify_fd >= 0) close(w->inotify_fd);
    close(w->wake_fd);
    pthread_mutex_destroy(&w->list_lock);
    pthread_mutex_destroy(&w->listener_lock);
    free(w);
}

unsigned cp0_kbd_presence_watcher_generation(const cp0_kbd_presence_watcher_t *w)
{
    if (!w) return 0;
    return atomic_load_explicit((atomic_uint *)&w->generation, memory_order_acquire);
}

unsigned cp0_kbd_presence_watcher_snapshot(cp0_kbd_presence_watcher_t *w, cp0_kbd_presence_list_t *out)
{
    if (!w || !out) {
        if (out) out->count = 0;
        return 0;
    }
    pthread_mutex_lock(&w->list_lock);
    *out = w->list;
    const unsigned gen = atomic_load_explicit(&w->generation, memory_order_acquire);
    pthread_mutex_unlock(&w->list_lock);
    return gen;
}

int cp0_kbd_presence_watcher_add_listener(cp0_kbd_presence_watcher_t *w, cp0_kbd_presence_listener_t listener,
                                          void *user)
{
    if (!w || !listener) return -1;
    pthread_mutex_lock(&w->listener_lock);
    int slot = -1;
    for (int i = 0; i < PRESENCE_MAX_LISTENERS && slot < 0; i++)
        if (!w->listeners[i].fn) slot = i;
    if (slot < 0) {
        pthread_mutex_unlock(&w->listener_lock);
        return -1;
    }
    w->listeners[slot].fn = listener;
    w->listeners[slot].user = user;
    cp0_kbd_presence_list_t current;
    const unsigned gen = cp0_kbd_presence_watcher_snapshot(w, &current);
    listener(&current, gen, user);
    pthread_mutex_unlock(&w->listener_lock);
    return 0;
}

void cp0_kbd_presence_watcher_remove_listener(cp0_kbd_presence_watcher_t *w, cp0_kbd_presence_listener_t listener,
                                              void *user)
{
    if (!w) return;
    pthread_mutex_lock(&w->listener_lock);
    for (int i = 0; i < PRESENCE_MAX_LISTENERS; i++) {
        if (w->listeners[i].fn == listener && w->listeners[i].user == user) {
            w->listeners[i].fn = NULL;
            w->listeners[i].user = NULL;
        }
    }
    pthread_mutex_unlock(&w->listener_lock);
}

static pthread_mutex_t s_shared_lock = PTHREAD_MUTEX_INITIALIZER;
static cp0_kbd_presence_watcher_t *s_shared;
static unsigned s_shared_refs;

cp0_kbd_presence_watcher_t *cp0_kbd_presence_shared_acquire(void)
{
    pthread_mutex_lock(&s_shared_lock);
    if (!s_shared) s_shared = cp0_kbd_presence_watcher_start(NULL);
    if (s_shared) s_shared_refs++;
    cp0_kbd_presence_watcher_t *w = s_shared;
    pthread_mutex_unlock(&s_shared_lock);
    return w;
}

void cp0_kbd_presence_shared_release(void)
{
    cp0_kbd_presence_watcher_t *to_stop = NULL;
    pthread_mutex_lock(&s_shared_lock);
    if (s_shared_refs > 0 && --s_shared_refs == 0) {
        to_stop = s_shared;
        s_shared = NULL;
    }
    pthread_mutex_unlock(&s_shared_lock);
    cp0_kbd_presence_watcher_stop(to_stop);
}

#else /* !__linux__ */

cp0_kbd_presence_watcher_t *cp0_kbd_presence_watcher_start(const cp0_kbd_presence_options_t *options)
{
    (void)options;
    return NULL;
}
void cp0_kbd_presence_watcher_stop(cp0_kbd_presence_watcher_t *watcher) { (void)watcher; }
unsigned cp0_kbd_presence_watcher_generation(const cp0_kbd_presence_watcher_t *watcher)
{
    (void)watcher;
    return 0;
}
unsigned cp0_kbd_presence_watcher_snapshot(cp0_kbd_presence_watcher_t *watcher, cp0_kbd_presence_list_t *out)
{
    (void)watcher;
    if (out) out->count = 0;
    return 0;
}
int cp0_kbd_presence_watcher_add_listener(cp0_kbd_presence_watcher_t *watcher,
                                          cp0_kbd_presence_listener_t listener, void *user)
{
    (void)watcher;
    (void)listener;
    (void)user;
    return -1;
}
void cp0_kbd_presence_watcher_remove_listener(cp0_kbd_presence_watcher_t *watcher,
                                              cp0_kbd_presence_listener_t listener, void *user)
{
    (void)watcher;
    (void)listener;
    (void)user;
}
cp0_kbd_presence_watcher_t *cp0_kbd_presence_shared_acquire(void) { return NULL; }
void cp0_kbd_presence_shared_release(void) {}

#endif
