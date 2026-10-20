/*
 * SPDX-License-Identifier: MIT
 *
 * Device side of the slot-aware touch tracker (see cp0_touch_reader.h).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* O_CLOEXEC, clock_gettime under -std=c11 */
#endif
#include "cp0_touch_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define TR_LONG_BITS (sizeof(unsigned long) * 8)
#define TR_NLONGS(n) (((n) + TR_LONG_BITS - 1) / TR_LONG_BITS)

int64_t cp0_touch_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int cp0_touch_reader_caps(int fd, int *slots, int *slot_now)
{
    unsigned long abs_bits[TR_NLONGS(ABS_CNT)];
    memset(abs_bits, 0, sizeof(abs_bits));
    if (slots) *slots = 1;
    if (slot_now) *slot_now = 0;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) return -1;
    if (!((abs_bits[ABS_MT_SLOT / TR_LONG_BITS] >> (ABS_MT_SLOT % TR_LONG_BITS)) & 1u)) return 0;
    struct input_absinfo ai;
    memset(&ai, 0, sizeof(ai));
    if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &ai) < 0) return 0;
    if (slots) *slots = ai.maximum >= 0 ? ai.maximum + 1 : 1;
    if (slot_now) *slot_now = ai.value;
    return 1;
}

void cp0_touch_reader_attach(cp0_touch_reader_t *r, int fd, int mt, int slots, int slot_now)
{
    memset(r, 0, sizeof(*r));
    r->fd = fd;
    r->slots = slots;
    cp0_touch_slots_init(&r->t, mt, slot_now);
}

int cp0_touch_reader_open(cp0_touch_reader_t *r, const char *path, int mt_fallback)
{
    memset(r, 0, sizeof(*r));
    r->fd = -1;
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    int slots = 1, slot_now = 0;
    int mt = cp0_touch_reader_caps(fd, &slots, &slot_now);
    if (mt < 0) {
        mt = mt_fallback != 0;
        slots = mt ? CP0_TOUCH_SLOTS_MAX : 1;
    }
    cp0_touch_reader_attach(r, fd, mt, slots, slot_now);
    r->owned = 1;
    (void)cp0_touch_reader_use_monotonic(r);
    if (mt) {
        cp0_touch_reader_resync(r);
        r->t.resync_next = 0; /* the starting state, not events lost: the first frame is an ordinary one */
    }
    return 0;
}

int cp0_touch_reader_use_monotonic(cp0_touch_reader_t *r)
{
    int clk = CLOCK_MONOTONIC;
    r->monotonic = r->fd >= 0 && ioctl(r->fd, EVIOCSCLOCKID, &clk) == 0;
    return r->monotonic;
}

void cp0_touch_reader_close(cp0_touch_reader_t *r)
{
    if (r->owned && r->fd >= 0) close(r->fd);
    r->fd = -1;
    r->owned = 0;
}

void cp0_touch_reader_resync(cp0_touch_reader_t *r)
{
    struct {
        uint32_t code;
        int32_t values[CP0_TOUCH_SLOTS_MAX];
    } req[4];
    static const uint32_t codes[4] = {ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TOUCH_MAJOR};
    int ok[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++) {
        memset(&req[i], 0, sizeof(req[i]));
        req[i].code = codes[i];
        for (int k = 0; k < CP0_TOUCH_SLOTS_MAX; k++) req[i].values[k] = i == 0 || i == 3 ? -1 : 0;
        ok[i] = r->fd >= 0 && ioctl(r->fd, EVIOCGMTSLOTS(sizeof(req[i])), &req[i]) >= 0;
    }
    if (!ok[0] || !ok[1] || !ok[2]) {
        cp0_touch_slots_load(&r->t, NULL, NULL, NULL, NULL, 0); /* nothing to read back: nothing is down */
        return;
    }
    cp0_touch_slots_load(&r->t, req[0].values, req[1].values, req[2].values, ok[3] ? req[3].values : NULL,
                         CP0_TOUCH_SLOTS_MAX);
}

int cp0_touch_reader_read(cp0_touch_reader_t *r, cp0_touch_frame_fn frame_fn, void *ctx)
{
    if (r->fd < 0) return -1;
    struct input_event ev[64];
    int total = 0;
    for (;;) {
        const ssize_t got = read(r->fd, ev, sizeof(ev));
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? total : -1;
        if (got == 0) return total > 0 ? total : -1; /* end of file: a FIFO whose writer left, a device gone */
        const int n = (int)(got / (ssize_t)sizeof(ev[0]));
        const int64_t read_at = r->monotonic ? 0 : cp0_touch_now_us();
        for (int i = 0; i < n; i++) {
            const cp0_touch_ev_t k = cp0_touch_slots_event(&r->t, ev[i].type, ev[i].code, ev[i].value);
            if (k == CP0_TOUCH_EV_RESYNC) cp0_touch_reader_resync(r);
            if (k == CP0_TOUCH_EV_NONE || !frame_fn) continue;
            cp0_touch_frame_t f;
            const int64_t t = r->monotonic ? (int64_t)ev[i].input_event_sec * 1000000 + ev[i].input_event_usec : read_at;
            cp0_touch_slots_frame(&r->t, t, &f);
            frame_fn(ctx, &f);
        }
        total += n;
        if (got < (ssize_t)sizeof(ev)) return total;
    }
}
