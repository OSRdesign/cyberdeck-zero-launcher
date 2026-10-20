/*
 * SPDX-License-Identifier: MIT
 *
 * Slot-aware touch tracker (multitasking P-c, study 026 sections 1.3 and 7.1): turns the evdev events of a touch screen
 * into frames (every contact down at a SYN_REPORT: tracking id, position, contact size) and the single pointer the
 * LVGL UI uses. Pure: no I/O, no clock, no globals; the device side is cp0_touch_reader.h, the display manager maps
 * the raw positions to the screen. Unit-tested on the PC (tests/test_touch_slots.c).
 *
 * Two modes, chosen by the caller from the device's capabilities:
 *   - protocol B (the device has ABS_MT_SLOT: the deck's Goodix, the Pi 3A+'s FT5x06, a uinput test panel): every
 *     slot is tracked. The single pointer follows the FIRST contact of a touch: pressed while that contact is down,
 *     at its position. The other contacts never press it, so two fingers no longer make it jump between them and
 *     lifting the second no longer releases the first; once the first contact is up the pointer stays released until
 *     every contact is up (no stray press under a finger that was only resting). One finger: exactly as before.
 *     ABS_X / ABS_Y / BTN_TOUCH (the kernel's pointer emulation) are ignored: the slots say everything.
 *   - flattened (anything else: a single-touch panel, protocol A): the reading of before, unchanged: one position
 *     from whatever reported last (ABS_MT_POSITION_* or ABS_X / ABS_Y), pressed from the last ABS_MT_TRACKING_ID or
 *     BTN_TOUCH. Frames then hold that one pointer.
 *
 * SYN_DROPPED (the reader fell behind, its queue overflowed): the events up to the next SYN_REPORT are ignored and
 * cp0_touch_slots_event() asks for a resync; the caller reads the slots back (EVIOCGMTSLOTS) into
 * cp0_touch_slots_load(), or clears them when it cannot. The next frame is flagged `resync` (gestures restart).
 */
#ifndef CP0_TOUCH_SLOTS_H
#define CP0_TOUCH_SLOTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Slots tracked: the Pi 3A+ FT5x06 has 10, the deck's Goodix 5. Contacts in higher slots are counted (`extra`). */
#define CP0_TOUCH_SLOTS_MAX 10

typedef struct {
    int32_t id;    /* ABS_MT_TRACKING_ID (>= 0); the flattened mode uses 0 */
    int32_t x, y;  /* raw device units (the display manager maps frames to screen pixels) */
    int32_t major; /* ABS_MT_TOUCH_MAJOR, -1 when the device does not report it */
    int32_t slot;
} cp0_touch_contact_t;

typedef struct {
    int64_t t_us;  /* when the frame ended (event time, or read time: the reader's choice) */
    int n;         /* contacts down, in c[] (slot order) */
    int extra;     /* contacts down in slots >= CP0_TOUCH_SLOTS_MAX (not in c[]) */
    int resync;    /* 1: events were lost before this frame (SYN_DROPPED); the contacts were read back or cleared */
    cp0_touch_contact_t c[CP0_TOUCH_SLOTS_MAX];
} cp0_touch_frame_t;

typedef enum {
    CP0_TOUCH_EV_NONE = 0,   /* nothing complete yet */
    CP0_TOUCH_EV_FRAME = 1,  /* a SYN_REPORT completed a frame: cp0_touch_slots_frame() */
    CP0_TOUCH_EV_RESYNC = 2, /* a SYN_REPORT ended a dropped run: load the slots (or clear them), then a frame */
} cp0_touch_ev_t;

/* Plain data: copy, zero or re-init it freely. */
typedef struct {
    int mt;          /* protocol B */
    int cur;         /* current slot number (any value the device sends) */
    int dropping;    /* inside a SYN_DROPPED run */
    int resync_next; /* the next frame carries resync = 1 */
    struct {
        int32_t id, x, y, major;
        int32_t prev_id; /* id at the last frame: contacts that were already down */
    } s[CP0_TOUCH_SLOTS_MAX];
    uint32_t extra_mask; /* slots CP0_TOUCH_SLOTS_MAX .. +31 with a contact */
    /* the single pointer */
    int primary;        /* slot of the first contact, -1 = none */
    int32_t primary_id;
    int wait_empty;     /* the first contact lifted while others stayed: no pointer until none is down */
    int32_t ptr_x, ptr_y;
    int ptr_pressed;
} cp0_touch_slots_t;

/* mt: 1 for a protocol B device (ABS_MT_SLOT in its EV_ABS bits), 0 for the flattened mode. slot_now: the device's
 * current slot (EVIOCGABS(ABS_MT_SLOT).value; 0 when unknown). Every slot starts empty. */
void cp0_touch_slots_init(cp0_touch_slots_t *t, int mt, int slot_now);

/* One evdev event. */
cp0_touch_ev_t cp0_touch_slots_event(cp0_touch_slots_t *t, uint16_t type, uint16_t code, int32_t value);

/* After CP0_TOUCH_EV_RESYNC: the state read back from the device, slot by slot (ids[i] < 0 = empty; majors may be
 * NULL), n slots from slot 0; slots not given are empty. n = 0 clears every contact (no read back possible). The
 * pointer is recomputed as at a frame. */
void cp0_touch_slots_load(cp0_touch_slots_t *t, const int32_t *ids, const int32_t *xs, const int32_t *ys,
                          const int32_t *majors, int n);

/* The contacts down now (raw positions), stamped t_us. */
void cp0_touch_slots_frame(cp0_touch_slots_t *t, int64_t t_us, cp0_touch_frame_t *out);

/* Contacts down now (extra included): 0 = nothing touches the screen. */
int cp0_touch_slots_down(const cp0_touch_slots_t *t);

/* The single pointer: 1 pressed / 0 released; *x, *y = its raw position (the last one when released). */
int cp0_touch_slots_pointer(const cp0_touch_slots_t *t, int32_t *x, int32_t *y);

#ifdef __cplusplus
}
#endif

#endif /* CP0_TOUCH_SLOTS_H */
