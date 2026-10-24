/*
 * The keys the launcher takes from a media-key device, and one press seen by several devices counted once (task c-7).
 *
 * A media-key device (cp0_keyboard_presence.h: not a keyboard, but it has volume, mute or brightness keys; the FZ-M1
 * tablet's "Panasonic Laptop Support", "Intel Virtual Buttons", the ACPI "Video Bus", a keyboard's "Consumer Control"
 * sibling, a headset's AVRCP node) is read through the same paths as the keyboards, but only these keys come out of it:
 *   KEY_MUTE (113), KEY_VOLUMEDOWN (114), KEY_VOLUMEUP (115), KEY_BRIGHTNESSDOWN (224), KEY_BRIGHTNESSUP (225).
 * Every other key of such a device is dropped: it cannot type, navigate, press Esc or cancel anything. Super
 * (KEY_LEFTMETA) included since 0.8.7-c7.4: the tablet's Windows key comes from its i8042 keyboard, and a second copy
 * from "Intel Virtual Buttons" (which declares it) could only disturb its hold (a tap there ends the hold here).
 *
 * The same physical press can come from two devices (the tablet's volume buttons are declared by both "Intel Virtual
 * Buttons" and "Panasonic Laptop Support"; Super by two keyboards). For those five keys and KEY_LEFTMETA, across every
 * source a reader has (keyboards and media-key devices alike), a press is dropped when
 *   - another source holds the same key down (pressed less than CP0_MEDIA_KEYS_STALE_MS ago, a lost release does not
 *     block the key for ever), or
 *   - another source pressed the same key less than CP0_MEDIA_KEYS_DEDUPE_MS ago (the second device reports the
 *     press after the first one's release);
 * the dropped press's repeats and release are dropped with it. Presses from one source are never merged (a fast double
 * press of one button is two presses). Other keys pass untouched.
 *
 * Pure (no I/O, no clock of its own: the caller passes CLOCK_MONOTONIC milliseconds), unit-tested on a PC
 * (tests/test_media_keys.c). One state per reader: the launcher's keyboard thread and each raw reader of the keys while
 * an app is in front (cp0_esc_key_watch.hpp).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_MEDIA_KEYS_DEDUPE_MS 80u   /* a second device's report of the same press comes within this */
#define CP0_MEDIA_KEYS_STALE_MS  5000u /* a key "held" longer by another source no longer blocks a press */
#define CP0_MEDIA_KEYS_SOURCES   64    /* source ids 0..63 (a reader's slot index); others share id 63 */
#define CP0_MEDIA_KEYS_COUNT     6

/* 1 for the five media keys (volume up / down, mute, brightness up / down). */
int cp0_media_key_is_media(unsigned code);
/* 1 for the keys a media-key device delivers: the five media keys. */
int cp0_media_key_passes(unsigned code);
/* 1 for the keys whose duplicates are dropped (the five media keys and KEY_LEFTMETA): the readers log their presses and
 * releases with the filter's verdict. */
int cp0_media_key_watched(unsigned code);

typedef struct {
    int holder;          /* the source holding the key down, -1 none */
    uint64_t holder_ms;  /* when it pressed */
    int last_source;     /* the source of the last press let through, -1 none */
    uint64_t last_ms;
    uint64_t swallowed;  /* bit per source: its press was dropped, its repeats and release go too */
} cp0_media_key_state_t;

typedef struct {
    cp0_media_key_state_t key[CP0_MEDIA_KEYS_COUNT];
} cp0_media_keys_t;

void cp0_media_keys_init(cp0_media_keys_t *m);
/* One key event from `source` (value 1 press, 0 release, 2 repeat; media: the source is a media-key device) at now_ms.
 * Returns 1 to deliver it, 0 to drop it. */
int cp0_media_keys_filter(cp0_media_keys_t *m, int source, int media, unsigned code, int value, uint64_t now_ms);
/* `source` went away (unplugged, closed): what it held is forgotten (no release will come). */
void cp0_media_keys_forget(cp0_media_keys_t *m, int source);

#ifdef __cplusplus
}
#endif
