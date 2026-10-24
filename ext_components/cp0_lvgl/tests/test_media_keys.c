/*
 * Unit test of the media-key devices' filter and the dedupe across devices (src/cp0/cp0_media_keys.c, task c-7), on a
 * fake clock: what a media-key device delivers, the FZ-M1 tablet's volume button reported by Intel Virtual Buttons and
 * Panasonic Laptop Support (in both orders, overlapping or one after the other), fast presses of one button, a lost
 * release, a source that goes away, the Windows button seen by the i8042 keyboard and Intel Virtual Buttons.
 */
#include "cp0_media_keys.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond)                                                                  \
    do {                                                                             \
        g_checks++;                                                                  \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

static int g_checks;

enum { MUTE = 113, VOLDOWN = 114, VOLUP = 115, BRIGHTDOWN = 224, BRIGHTUP = 225, LEFTMETA = 125, RIGHTMETA = 126 };
enum { KEY_ESC_ = 1, KEY_A_ = 30, KEY_ENTER_ = 28, KEY_UP_ = 103, KEY_POWER_ = 116, KEY_F14_ = 184, ROTATE_LOCK = 0x231 };
/* the tablet's sources as a reader numbers them */
enum { AT_KBD = 0, VBTN = 1, PANA = 2, VIDEO = 3, USB_KBD = 4 };

static cp0_media_keys_t m;

static int ev(int source, int media, unsigned code, int value, uint64_t now)
{
    return cp0_media_keys_filter(&m, source, media, code, value, now);
}

static void test_codes(void)
{
    CHECK(cp0_media_key_is_media(MUTE) && cp0_media_key_is_media(VOLDOWN) && cp0_media_key_is_media(VOLUP));
    CHECK(cp0_media_key_is_media(BRIGHTDOWN) && cp0_media_key_is_media(BRIGHTUP));
    /* c7.4: Super is no longer taken from a media-key device; its duplicates are still dropped between keyboards */
    CHECK(!cp0_media_key_is_media(LEFTMETA) && !cp0_media_key_passes(LEFTMETA) && cp0_media_key_watched(LEFTMETA));
    CHECK(cp0_media_key_watched(VOLUP) && !cp0_media_key_watched(KEY_F14_) && !cp0_media_key_watched(KEY_ESC_));
    CHECK(!cp0_media_key_passes(RIGHTMETA) && !cp0_media_key_passes(KEY_ESC_) && !cp0_media_key_passes(KEY_F14_));
    CHECK(!cp0_media_key_passes(KEY_POWER_) && !cp0_media_key_passes(ROTATE_LOCK) && !cp0_media_key_passes(0));
}

/* a media-key device types nothing: Esc, letters, Enter, arrows (a consumer-control node has them), power, F14, the
 * rotation-lock key and (c7.4) Super are dropped, press, repeat and release alike; its media keys pass */
static void test_media_device_keys(void)
{
    cp0_media_keys_init(&m);
    const unsigned dropped[] = {KEY_ESC_, KEY_A_, KEY_ENTER_, KEY_UP_, KEY_POWER_, KEY_F14_, ROTATE_LOCK, RIGHTMETA, LEFTMETA};
    for (size_t i = 0; i < sizeof(dropped) / sizeof(dropped[0]); i++)
        for (int value = 0; value <= 2; value++) CHECK(ev(VBTN, 1, dropped[i], value, 1000) == 0);
    uint64_t t = 2000;
    const unsigned passed[] = {MUTE, VOLDOWN, VOLUP, BRIGHTDOWN, BRIGHTUP};
    for (size_t i = 0; i < sizeof(passed) / sizeof(passed[0]); i++, t += 500) {
        CHECK(ev(PANA, 1, passed[i], 1, t) == 1);
        CHECK(ev(PANA, 1, passed[i], 2, t + 300) == 1);
        CHECK(ev(PANA, 1, passed[i], 0, t + 350) == 1);
    }
    /* a keyboard's keys are untouched, the same codes included */
    for (int value = 0; value <= 2; value++) {
        CHECK(ev(USB_KBD, 0, KEY_ESC_, value, 9000) == 1);
        CHECK(ev(USB_KBD, 0, KEY_F14_, value, 9000) == 1);
    }
    CHECK(cp0_media_keys_filter(NULL, AT_KBD, 0, KEY_A_, 1, 0) == 1); /* no state: keyboard keys still pass */
    CHECK(cp0_media_keys_filter(NULL, VBTN, 1, KEY_A_, 1, 0) == 0);
}

/* one press of the tablet's volume button reported by two devices: once */
static void test_two_devices_one_press(void)
{
    /* overlapping: Intel Virtual Buttons down, Panasonic down, Intel up, Panasonic up */
    cp0_media_keys_init(&m);
    CHECK(ev(VBTN, 1, VOLUP, 1, 10000) == 1);
    CHECK(ev(PANA, 1, VOLUP, 1, 10003) == 0);
    CHECK(ev(VBTN, 1, VOLUP, 0, 10120) == 1);
    CHECK(ev(PANA, 1, VOLUP, 0, 10122) == 0);
    /* one after the other (each driver sends its press and release at once), Panasonic first this time */
    CHECK(ev(PANA, 1, VOLDOWN, 1, 20000) == 1);
    CHECK(ev(PANA, 1, VOLDOWN, 0, 20001) == 1);
    CHECK(ev(VBTN, 1, VOLDOWN, 1, 20030) == 0);
    CHECK(ev(VBTN, 1, VOLDOWN, 0, 20031) == 0);
    /* the second device late, but the first one still holds the key (a long hold): still one press, its repeats too */
    CHECK(ev(VBTN, 1, VOLUP, 1, 30000) == 1);
    CHECK(ev(PANA, 1, VOLUP, 1, 30400) == 0);
    CHECK(ev(PANA, 1, VOLUP, 2, 30500) == 0);
    CHECK(ev(VBTN, 1, VOLUP, 2, 30500) == 1);
    CHECK(ev(VBTN, 1, VOLUP, 0, 31000) == 1);
    CHECK(ev(PANA, 1, VOLUP, 0, 31001) == 0);
    /* the next real press, from either device, counts again */
    CHECK(ev(PANA, 1, VOLUP, 1, 31500) == 1);
    CHECK(ev(PANA, 1, VOLUP, 0, 31600) == 1);
    CHECK(ev(VBTN, 1, VOLUP, 1, 32000) == 1);
    CHECK(ev(VBTN, 1, VOLUP, 0, 32100) == 1);
    /* other keys of the same moment are not merged: brightness from the video bus while volume is held */
    CHECK(ev(VBTN, 1, VOLUP, 1, 40000) == 1);
    CHECK(ev(VIDEO, 1, BRIGHTUP, 1, 40010) == 1);
    CHECK(ev(PANA, 1, BRIGHTUP, 1, 40012) == 0); /* ... but the same brightness press from Panasonic is */
    CHECK(ev(VIDEO, 1, BRIGHTUP, 0, 40100) == 1);
    CHECK(ev(PANA, 1, BRIGHTUP, 0, 40101) == 0);
    CHECK(ev(VBTN, 1, VOLUP, 0, 40200) == 1);
}

/* one device: every press counts, however fast */
static void test_fast_presses(void)
{
    cp0_media_keys_init(&m);
    for (uint64_t t = 50000; t < 50400; t += 40) {
        CHECK(ev(PANA, 1, VOLDOWN, 1, t) == 1);
        CHECK(ev(PANA, 1, VOLDOWN, 0, t + 10) == 1);
    }
    /* a press whose release this reader never saw: the next press of the same source counts */
    CHECK(ev(PANA, 1, MUTE, 1, 60000) == 1);
    CHECK(ev(PANA, 1, MUTE, 1, 60500) == 1);
    CHECK(ev(PANA, 1, MUTE, 0, 60600) == 1);
}

/* the window: a press of another source 80 ms or more after the last one is a new press */
static void test_window_and_stale(void)
{
    cp0_media_keys_init(&m);
    CHECK(ev(PANA, 1, VOLUP, 1, 70000) == 1);
    CHECK(ev(PANA, 1, VOLUP, 0, 70010) == 1);
    CHECK(ev(VBTN, 1, VOLUP, 1, 70000 + CP0_MEDIA_KEYS_DEDUPE_MS - 1) == 0);
    CHECK(ev(VBTN, 1, VOLUP, 0, 70100) == 0);
    CHECK(ev(USB_KBD, 0, VOLUP, 1, 70010 + CP0_MEDIA_KEYS_DEDUPE_MS) == 1); /* a keyboard's volume key: a new press */
    CHECK(ev(USB_KBD, 0, VOLUP, 0, 70200) == 1);
    /* a lost release does not block the key for ever: after CP0_MEDIA_KEYS_STALE_MS another source counts */
    CHECK(ev(VBTN, 1, VOLDOWN, 1, 80000) == 1);
    CHECK(ev(PANA, 1, VOLDOWN, 1, 80000 + CP0_MEDIA_KEYS_STALE_MS - 1) == 0);
    CHECK(ev(PANA, 1, VOLDOWN, 0, 80000 + CP0_MEDIA_KEYS_STALE_MS) == 0);
    CHECK(ev(PANA, 1, VOLDOWN, 1, 80000 + CP0_MEDIA_KEYS_STALE_MS + 1) == 1);
    CHECK(ev(PANA, 1, VOLDOWN, 0, 80000 + CP0_MEDIA_KEYS_STALE_MS + 50) == 1);
}

/* a source that goes away (unplugged while it held the key): forgotten, the others are not blocked; a source id out of
 * range shares the last id */
static void test_forget(void)
{
    cp0_media_keys_init(&m);
    CHECK(ev(VBTN, 1, VOLUP, 1, 90000) == 1);
    CHECK(ev(PANA, 1, VOLUP, 1, 90200) == 0);
    cp0_media_keys_forget(&m, VBTN);
    cp0_media_keys_forget(&m, PANA); /* its swallowed press is forgotten too: its release is not owed any more */
    CHECK(ev(PANA, 1, VOLUP, 1, 90400) == 1);
    CHECK(ev(PANA, 1, VOLUP, 0, 90500) == 1);
    cp0_media_keys_forget(NULL, 0);
    CHECK(ev(-5, 0, VOLUP, 1, 91000) == 1);
    CHECK(ev(CP0_MEDIA_KEYS_SOURCES - 1, 0, VOLUP, 1, 91010) == 1); /* the same id as -5: one source, a re-press */
    CHECK(ev(500, 0, VOLUP, 0, 91020) == 1);
}

/* the Windows button: Super from the i8042 keyboard with the kernel's auto-repeat; Intel Virtual Buttons' copy (held, or a
 * quick tap at the press) is never taken (c7.4: a tap there would end the hold here); two keyboards' copies count once;
 * Win+F14 (the rotation-lock button in the BIOS's "Rotation lock" mode) keeps its F14 on the keyboard */
static void test_super(void)
{
    cp0_media_keys_init(&m);
    CHECK(ev(VBTN, 1, LEFTMETA, 1, 99999) == 0); /* the copy first, as a tap */
    CHECK(ev(VBTN, 1, LEFTMETA, 0, 100000) == 0);
    CHECK(ev(AT_KBD, 0, LEFTMETA, 1, 100000) == 1);
    CHECK(ev(VBTN, 1, LEFTMETA, 1, 100002) == 0);
    for (uint64_t t = 100250; t < 102900; t += 30) CHECK(ev(AT_KBD, 0, LEFTMETA, 2, t) == 1); /* auto-repeat */
    CHECK(ev(VBTN, 1, LEFTMETA, 0, 100800) == 0);
    CHECK(ev(AT_KBD, 0, LEFTMETA, 0, 103000) == 1);
    CHECK(ev(USB_KBD, 0, LEFTMETA, 1, 104000) == 1); /* two keyboards: the second one's press while the first holds */
    CHECK(ev(AT_KBD, 0, LEFTMETA, 1, 104005) == 0);
    CHECK(ev(AT_KBD, 0, LEFTMETA, 2, 104300) == 0);
    CHECK(ev(AT_KBD, 0, LEFTMETA, 0, 104900) == 0);
    CHECK(ev(USB_KBD, 0, LEFTMETA, 0, 104901) == 1);
    CHECK(ev(AT_KBD, 0, LEFTMETA, 1, 105000) == 1);
    CHECK(ev(AT_KBD, 0, KEY_F14_, 1, 105001) == 1);
    CHECK(ev(AT_KBD, 0, KEY_F14_, 0, 105002) == 1);
    CHECK(ev(AT_KBD, 0, LEFTMETA, 0, 105003) == 1);
}

int main(void)
{
    test_codes();
    test_media_device_keys();
    test_two_devices_one_press();
    test_fast_presses();
    test_window_and_stale();
    test_forget();
    test_super();
    printf("test_media_keys: %d checks passed\n", g_checks);
    return 0;
}
