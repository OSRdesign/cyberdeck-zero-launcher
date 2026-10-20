/*
 * SPDX-License-Identifier: MIT
 *
 * Device side of the slot-aware touch tracker (multitasking P-c, cp0_touch_slots.h): reads a touch screen's evdev node
 * into a tracker and hands out frames. The display manager (cp0_lvgl_dpi_scaled.c) uses one on its own file descriptor
 * for the LVGL pointer; the launcher's supervisor opens a second, passive one on the same node while an app is in
 * front, for the three-finger hold. Neither ever takes EVIOCGRAB here: the app in front reads the same node, and each
 * open file gets every event (the launcher's own grab while it is in front is cp0_display_set_touch_grab).
 * Linux device builds only (src/cp0).
 */
#ifndef CP0_TOUCH_READER_H
#define CP0_TOUCH_READER_H

#include "cp0_touch_slots.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cp0_touch_frame_fn)(void *ctx, const cp0_touch_frame_t *frame);

typedef struct {
    int fd;
    int owned;     /* opened by cp0_touch_reader_open: closed by cp0_touch_reader_close */
    int monotonic; /* EVIOCSCLOCKID(CLOCK_MONOTONIC) accepted: frames carry the events' own times */
    int slots;     /* screen.state touch_slots: 0 no touch, 1 single touch (or not protocol B), N protocol B slots */
    cp0_touch_slots_t t;
} cp0_touch_reader_t;

/* What an evdev node reports: 1 = protocol B (*slots = ABS_MT_SLOT max + 1, *slot_now = its current slot), 0 = a
 * touch screen without slots (*slots = 1), -1 = not readable (not an evdev node). */
int cp0_touch_reader_caps(int fd, int *slots, int *slot_now);

/* Uses fd (not owned) with what its capabilities say (cp0_touch_reader_caps, read by the caller). */
void cp0_touch_reader_attach(cp0_touch_reader_t *r, int fd, int mt, int slots, int slot_now);

/* Opens path read-only, non-blocking, close-on-exec; reads its capabilities (mt_fallback when they cannot be read: a
 * FIFO standing in for the device in the host tests), asks for monotonic event times and reads back the contacts down
 * now. 0, or -1 (errno set). */
int cp0_touch_reader_open(cp0_touch_reader_t *r, const char *path, int mt_fallback);

/* Event times on CLOCK_MONOTONIC (EVIOCSCLOCKID: this open file only). 1 when the device accepted it; otherwise frames
 * carry the time they were read. */
int cp0_touch_reader_use_monotonic(cp0_touch_reader_t *r);

void cp0_touch_reader_close(cp0_touch_reader_t *r);

/* Reads every pending event; frame_fn (NULL: none) gets each completed frame with raw positions. A SYN_DROPPED run is
 * resynchronised (cp0_touch_reader_resync). Returns the number of events read, or -1 when the device is gone (a read
 * error other than EAGAIN / EINTR, or end of file). */
int cp0_touch_reader_read(cp0_touch_reader_t *r, cp0_touch_frame_fn frame_fn, void *ctx);

/* Reads back the contacts down now (EVIOCGMTSLOTS: tracking ids, positions, sizes); clears them when that fails. */
void cp0_touch_reader_resync(cp0_touch_reader_t *r);

/* CLOCK_MONOTONIC now, microseconds. */
int64_t cp0_touch_now_us(void);

#ifdef __cplusplus
}
#endif

#endif /* CP0_TOUCH_READER_H */
