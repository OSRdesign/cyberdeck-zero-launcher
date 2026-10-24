/*
 * Media-key devices: the keys they deliver, and one press seen by several devices counted once (see cp0_media_keys.h).
 */

#include "cp0_media_keys.h"

#include <string.h>

/* the launcher-owned keys, in their state slots (linux/input-event-codes.h values: no Linux header needed) */
static const unsigned media_keys_codes[CP0_MEDIA_KEYS_COUNT] = {113 /* MUTE */,           114 /* VOLUMEDOWN */,
                                                                115 /* VOLUMEUP */,       224 /* BRIGHTNESSDOWN */,
                                                                225 /* BRIGHTNESSUP */, 125 /* LEFTMETA */};

static int slot_of(unsigned code)
{
    for (int i = 0; i < CP0_MEDIA_KEYS_COUNT; i++)
        if (media_keys_codes[i] == code) return i;
    return -1;
}

int cp0_media_key_is_media(unsigned code)
{
    const int i = slot_of(code);
    return i >= 0 && code != 125;
}

int cp0_media_key_passes(unsigned code)
{
    return cp0_media_key_is_media(code); /* c7.4: Super no longer (its own device is the keyboard) */
}

int cp0_media_key_watched(unsigned code)
{
    return slot_of(code) >= 0;
}

void cp0_media_keys_init(cp0_media_keys_t *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    for (int i = 0; i < CP0_MEDIA_KEYS_COUNT; i++) {
        m->key[i].holder = -1;
        m->key[i].last_source = -1;
    }
}

static int source_id(int source)
{
    return source >= 0 && source < CP0_MEDIA_KEYS_SOURCES ? source : CP0_MEDIA_KEYS_SOURCES - 1;
}

int cp0_media_keys_filter(cp0_media_keys_t *m, int source, int media, unsigned code, int value, uint64_t now_ms)
{
    if (media && !cp0_media_key_passes(code)) return 0; /* a media-key device types nothing */
    const int i = slot_of(code);
    if (!m || i < 0) return 1;
    cp0_media_key_state_t *k = &m->key[i];
    const int id = source_id(source);
    const uint64_t bit = (uint64_t)1 << id;

    if (value == 1) {
        const int held_elsewhere = k->holder >= 0 && k->holder != id && now_ms - k->holder_ms < CP0_MEDIA_KEYS_STALE_MS;
        const int just_pressed_elsewhere =
            k->last_source >= 0 && k->last_source != id && now_ms - k->last_ms < CP0_MEDIA_KEYS_DEDUPE_MS;
        if (held_elsewhere || just_pressed_elsewhere) {
            k->swallowed |= bit;
            return 0;
        }
        k->holder = id;
        k->holder_ms = now_ms;
        k->last_source = id;
        k->last_ms = now_ms;
        k->swallowed &= ~bit;
        return 1;
    }
    if (k->swallowed & bit) {
        if (value == 0) k->swallowed &= ~bit;
        return 0;
    }
    if (value == 0 && k->holder == id) k->holder = -1;
    return 1;
}

void cp0_media_keys_forget(cp0_media_keys_t *m, int source)
{
    if (!m) return;
    const int id = source_id(source);
    const uint64_t bit = (uint64_t)1 << id;
    for (int i = 0; i < CP0_MEDIA_KEYS_COUNT; i++) {
        if (m->key[i].holder == id) m->key[i].holder = -1;
        m->key[i].swallowed &= ~bit;
    }
}
