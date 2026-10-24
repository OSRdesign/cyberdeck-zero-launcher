/*
 * Unit test of the keyboard presence helper (src/cp0/cp0_keyboard_presence.c):
 *   - the bitmap decoder (64- and 32-bit words, leading zero words and digits left out),
 *   - the "real keyboard" rule against a /proc/bus/input/devices capture holding every device seen on the
 *     boards (tests/fixtures/proc_bus_input_devices_all_boards.txt, path given as argv[1]),
 *   - the rescan timing (debounce, max wait, safety poll) with an injected clock,
 *   - the inotify watcher thread on a temporary directory and devices file.
 */
#define _GNU_SOURCE

#include "cp0_keyboard_presence.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

static int bit(const char *bitmap, unsigned word_bits, unsigned n)
{
    return cp0_kbd_presence_bitmap_bit(bitmap, strlen(bitmap), word_bits, n);
}

static void test_bitmap_decoder(void)
{
    /* the probe-board.sh --selftest unit checks */
    CHECK(bit("10000ffc", 64, 28) == 1);
    CHECK(bit("10000ffc", 64, 30) == 0);
    CHECK(bit("10000ffc", 64, 2) == 1);
    CHECK(bit("400 0 0 0 0 0", 64, 330) == 1); /* BTN_TOUCH: word 5, bit 10 */
    CHECK(bit("400 0 0 0 0 0", 64, 10) == 0);
    CHECK(bit("1 0", 64, 64) == 1);
    CHECK(bit("1 0", 64, 0) == 0);
    CHECK(bit("265800000000003", 64, 53) == 1);
    CHECK(bit("2658000 3", 32, 53) == 1);
    CHECK(bit("2658000 3", 32, 0) == 1);
    CHECK(bit("2658000 3", 32, 2) == 0);
    /* the same "1 0" is bit 64 on a 64-bit reader and bit 32 on a 32-bit one */
    CHECK(bit("1 0", 32, 32) == 1);
    CHECK(bit("1 0", 32, 64) == 0);
    /* a short (unpadded) low word: digits are counted from the right, never from the word width */
    CHECK(bit("3 4000000", 64, 26) == 1);
    CHECK(bit("3 4000000", 64, 64) == 1);
    CHECK(bit("3 4000000", 64, 65) == 1);
    CHECK(bit("3 4000000", 64, 66) == 0);
    CHECK(bit("3 4000000", 64, 30) == 0); /* beyond the printed digits of word 0 */
    /* upper case, extra spaces, trailing newline */
    CHECK(bit("  FFFF  0 \n", 64, 64 + 15) == 1);
    /* malformed and empty */
    CHECK(bit("", 64, 0) == 0);
    CHECK(bit("0", 64, 0) == 0);
    CHECK(bit("zz", 64, 0) == 0);
    CHECK(bit("ff", 16, 0) == 0); /* unsupported word size */
    CHECK(cp0_kbd_presence_bitmap_bit(NULL, 0, 64, 0) == 0);
    CHECK(cp0_kbd_presence_native_word_bits() == sizeof(long) * 8);

    /* the Logitech bitmap as a 64-bit and as a 32-bit reader sees it: the same bits */
    const char *logi64 = "1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe";
    const char *logi32 = "10000 7 ff9f207a c14057ff febeffdf ffefffff ffffffff fffffffe";
    CHECK(bit(logi64, 64, 28) && bit(logi64, 64, 30) && bit(logi64, 64, 44) && bit(logi64, 64, 57));
    for (unsigned n = 0; n < 256; n++) CHECK(bit(logi64, 64, n) == bit(logi32, 32, n));
}

static char *read_text(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    static char text[65536];
    *len = fread(text, 1, sizeof(text), f);
    fclose(f);
    CHECK(*len > 0 && *len < sizeof(text));
    return text;
}

static void check_fixture_list(const cp0_kbd_presence_list_t *list)
{
    CHECK(list->count == 5);
    CHECK(strcmp(list->devices[0].path, "/dev/input/event0") == 0);
    CHECK(strcmp(list->devices[0].name, "ZitaoTech HACKBerryPiQ20") == 0);
    CHECK(strcmp(list->devices[1].path, "/dev/input/event4") == 0);
    CHECK(strcmp(list->devices[1].name, "2.4G Composite Devic") == 0);
    CHECK(strcmp(list->devices[2].path, "/dev/input/event8") == 0);
    CHECK(strcmp(list->devices[2].name, "Logitech USB Keyboard") == 0);
    CHECK(strcmp(list->devices[3].path, "/dev/input/event9") == 0);
    CHECK(strcmp(list->devices[3].name, "ZMK Project bb9900 Keyboard") == 0);
    CHECK(strcmp(list->devices[4].path, "/dev/input/event10") == 0);
    CHECK(strcmp(list->devices[4].name, "M4 Keyboard") == 0);
    /* the bus tells a Bluetooth keyboard (it sleeps: on-screen keyboard policy, task 014 P2b) from a USB one */
    CHECK(list->devices[0].bus == 0x0003 && list->devices[1].bus == 0x0003 && list->devices[2].bus == 0x0003);
    CHECK(list->devices[3].bus == CP0_KBD_PRESENCE_BUS_BLUETOOTH);
    CHECK(list->devices[4].bus == CP0_KBD_PRESENCE_BUS_BLUETOOTH);
}

static void test_fixture(const char *fixture)
{
    size_t len = 0;
    const char *text = read_text(fixture, &len);
    cp0_kbd_presence_list_t list;
    /* keyboards: HACKBerry, 2.4G receiver, Logitech, ZMK, M4. Not keyboards: the receivers' Mouse, Consumer
     * Control and System Control siblings, the AVRCP remote, the RTL2832U IR receiver, HDMI CEC, pwr_button,
     * axp20x-pek, the Goodix touch panel and applaunch-vkbd. */
    CHECK(cp0_kbd_presence_parse(text, len, 64, &list) == 5);
    check_fixture_list(&list);

    /* the same file through the reader, the cheap count, and an unreadable path */
    cp0_kbd_presence_list_t read_list;
    CHECK(cp0_kbd_presence_enumerate(fixture, 64, &read_list) == 5);
    CHECK(cp0_kbd_presence_list_equal(&list, &read_list));
    if (cp0_kbd_presence_native_word_bits() == 64) {
        CHECK(cp0_kbd_presence_count(fixture) == 5);
        CHECK(cp0_kbd_presence_enumerate(fixture, 0, &read_list) == 5);
    }
    read_list.count = 3;
    CHECK(cp0_kbd_presence_enumerate("/nonexistent/devices", 64, &read_list) == -1);
    CHECK(read_list.count == 0);
    CHECK(cp0_kbd_presence_count("/nonexistent/devices") == -1);

    /* pointers (pointer navigation): the HackBerry's pad (event1) and the 2.4G receiver's mouse (event5). Not
     * pointers: every keyboard (event0 has no REL), the Consumer Control siblings (REL_HWHEEL and ABS_VOLUME only),
     * the ZMK / M4 keyboards (REL_HWHEEL only), HDMI CEC (REL_X/Y but no BTN_LEFT), the Goodix panel (ABS, BTN_TOUCH),
     * the AVRCP remote (REL=0) and applaunch-vkbd. */
    cp0_kbd_presence_list_t pointers;
    CHECK(cp0_kbd_presence_parse_pointers(text, len, 64, &pointers) == 2);
    CHECK(pointers.count == 2);
    CHECK(strcmp(pointers.devices[0].path, "/dev/input/event1") == 0);
    CHECK(strcmp(pointers.devices[0].name, "ZitaoTech HACKBerryPiQ20 Mouse") == 0);
    CHECK(pointers.devices[0].bus == 0x0003);
    CHECK(strcmp(pointers.devices[1].path, "/dev/input/event5") == 0);
    CHECK(strcmp(pointers.devices[1].name, "2.4G Composite Devic Mouse") == 0);
    /* both lists from one read */
    cp0_kbd_presence_list_t both_k, both_p;
    CHECK(cp0_kbd_presence_enumerate_all(fixture, 64, &both_k, &both_p) == 5);
    CHECK(cp0_kbd_presence_list_equal(&both_k, &list) && cp0_kbd_presence_list_equal(&both_p, &pointers));
    CHECK(cp0_kbd_presence_enumerate_all(fixture, 64, NULL, &both_p) == 5 && both_p.count == 2);
    both_p.count = 9;
    CHECK(cp0_kbd_presence_enumerate_all("/nonexistent/devices", 64, &both_k, &both_p) == -1);
    CHECK(both_k.count == 0 && both_p.count == 0);
    CHECK(cp0_kbd_presence_parse_pointers(text, len, 64, NULL) == 2);
}

static int is_pointer(const char *key, const char *rel, const char *abs, unsigned word_bits)
{
    cp0_ptr_caps_t caps;
    memset(&caps, 0, sizeof(caps));
    caps.has_node = 1;
    cp0_ptr_caps_from_bitmaps(key, rel, abs, word_bits, &caps);
    return cp0_ptr_caps_is_pointer(&caps);
}

static void test_pointer_classifier(void)
{
    const char *logi64 = "1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe";
    /* the HackBerry keyboard (event0): keys, no REL */
    CHECK(!is_pointer(logi64, NULL, NULL, 64));
    /* the HackBerry pad (event1): BTN_LEFT..BTN_EXTRA, REL_X REL_Y HWHEEL WHEEL WHEEL_HI_RES HWHEEL_HI_RES */
    CHECK(is_pointer("1f0000 0 0 0 0", "1943", NULL, 64));
    /* the same as a 32-bit reader prints it */
    CHECK(is_pointer("1f0000 0 0 0 0 0 0 0 0", "1943", NULL, 32));
    CHECK(!is_pointer("1f0000 0 0 0 0", "1943", NULL, 32)); /* 64-bit words read as 32-bit: BTN_LEFT not there */
    /* a USB mouse with a wheel: left, right, middle; REL_X REL_Y REL_WHEEL REL_WHEEL_HI_RES */
    CHECK(is_pointer("70000 0 0 0 0", "903", NULL, 64));
    /* a trackball with only the left button */
    CHECK(is_pointer("10000 0 0 0 0", "3", NULL, 64));
    /* a touch screen: ABS and BTN_TOUCH, no REL */
    CHECK(!is_pointer("400 0 0 0 0 0", NULL, "265800000000003", 64));
    /* a touchpad: BTN_LEFT, BTN_TOOL_FINGER, BTN_TOUCH and absolute axes */
    CHECK(!is_pointer("420 10000 0 0 0 0", NULL, "660800001000003", 64));
    /* a touchpad that also declares REL_X / REL_Y: still not (ABS, BTN_TOUCH) */
    CHECK(!is_pointer("420 10000 0 0 0 0", "3", "660800001000003", 64));
    /* BTN_TOUCH alone is enough to refuse, even without ABS */
    CHECK(!is_pointer("400 10000 0 0 0 0", "3", NULL, 64));
    /* a mouse node that declares any absolute axis (ABS_VOLUME here) is refused too (the "no ABS" rule) */
    CHECK(!is_pointer("70000 0 0 0 0", "903", "100000000", 64));
    /* HDMI CEC: REL_X / REL_Y without BTN_LEFT */
    CHECK(!is_pointer("800 80040000000000 3ff 0 400000002fc000 c30db00000000 0 18300 41c2c0006401 "
                      "9e378000000000 f810000010000000",
                      "3", NULL, 64));
    /* a consumer-control sibling: horizontal wheel only */
    CHECK(!is_pointer("3fb2800000000 0 0 302019300 3978f8401000 10e000000000000 10000002", "1040", "100000000", 64));
    /* only one axis */
    CHECK(!is_pointer("10000 0 0 0 0", "1", NULL, 64));
    CHECK(!is_pointer("10000 0 0 0 0", "2", NULL, 64));
    /* a zero ABS line ("B: ABS=0") is no absolute axis */
    CHECK(is_pointer("10000 0 0 0 0", "3", "0", 64));

    /* the device-level parts: no node, the virtual bus, the launcher's own devices */
    cp0_ptr_caps_t caps;
    memset(&caps, 0, sizeof(caps));
    cp0_ptr_caps_from_bitmaps("1f0000 0 0 0 0", "1943", NULL, 64, &caps);
    CHECK(!cp0_ptr_caps_is_pointer(&caps)); /* has_node 0 */
    caps.has_node = 1;
    CHECK(cp0_ptr_caps_is_pointer(&caps));
    caps.is_virtual = 1;
    CHECK(!cp0_ptr_caps_is_pointer(&caps));
    caps.is_virtual = 0;
    caps.hub_name = 1;
    CHECK(!cp0_ptr_caps_is_pointer(&caps));
    CHECK(!cp0_ptr_caps_is_pointer(NULL));
    cp0_ptr_caps_from_bitmaps(NULL, NULL, NULL, 64, NULL); /* no crash */

    /* the same rules through /proc text: a combined node (keyboard + pointer on one node) is in both lists, a
     * virtual mouse and an "applaunch-" one in neither, a mouse without a node neither */
    char text[4096];
    snprintf(text, sizeof(text),
             "I: Bus=0003\nN: Name=\"combo\"\nH: Handlers=sysrq kbd leds mouse0 event3\nB: EV=120017\n"
             "B: KEY=%s\nB: REL=103\n\n"
             "I: Bus=0006\nN: Name=\"virtual mouse\"\nH: Handlers=mouse1 event4\nB: KEY=10000 0 0 0 0\nB: REL=3\n\n"
             "I: Bus=0003\nN: Name=\"applaunch-pointer\"\nH: Handlers=mouse2 event6\nB: KEY=10000 0 0 0 0\n"
             "B: REL=3\n\n"
             "I: Bus=0003\nN: Name=\"no node\"\nH: Handlers=mouse3\nB: KEY=10000 0 0 0 0\nB: REL=3\n\n"
             "I: Bus=0005\nN: Name=\"BT mouse\"\nH: Handlers=mouse4 event12\nB: KEY=70000 0 0 0 0\nB: REL=903\n",
             "70000 1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe"); /* BTN_LEFT.. + keys */
    cp0_kbd_presence_list_t list;
    CHECK(cp0_kbd_presence_parse_pointers(text, strlen(text), 64, &list) == 2);
    CHECK(strcmp(list.devices[0].name, "combo") == 0 && strcmp(list.devices[0].path, "/dev/input/event3") == 0);
    CHECK(strcmp(list.devices[1].name, "BT mouse") == 0 && list.devices[1].bus == CP0_KBD_PRESENCE_BUS_BLUETOOTH);
    CHECK(cp0_kbd_presence_parse(text, strlen(text), 64, &list) == 1);
    CHECK(strcmp(list.devices[0].name, "combo") == 0);
}

static void test_rule_edges(void)
{
    cp0_kbd_presence_list_t list;
    const char *full = "1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe";
    char text[8192];

    /* only the "applaunch-" PREFIX is the hub; another USB name containing it is a keyboard; no event node
     * means nothing to read; the last block needs no blank line after it; CRLF line ends are tolerated */
    snprintf(text, sizeof(text),
             "I: Bus=0003 Vendor=1 Product=2 Version=3\nN: Name=\"applaunch-other\"\nH: Handlers=kbd event1 \n"
             "B: KEY=%s\n\n"
             "I: Bus=0003 Vendor=1 Product=2 Version=3\nN: Name=\"my applaunch-board\"\nH: Handlers=kbd event2\n"
             "B: KEY=%s\n\n"
             "I: Bus=0003 Vendor=1 Product=2 Version=3\nN: Name=\"no node\"\nH: Handlers=sysrq kbd leds\n"
             "B: KEY=%s\n\n"
             "I: Bus=0006 Vendor=1 Product=2 Version=3\nN: Name=\"virtual board\"\nH: Handlers=kbd event3\n"
             "B: KEY=%s\n\n"
             "I: Bus=0005 Vendor=1 Product=2 Version=3\r\nN: Name=\"crlf board\"\r\nH: Handlers=kbd event21 mouse9\r\n"
             "B: KEY=%s\r\n",
             full, full, full, full, full);
    CHECK(cp0_kbd_presence_parse(text, strlen(text), 64, &list) == 2);
    CHECK(list.count == 2);
    CHECK(strcmp(list.devices[0].path, "/dev/input/event2") == 0);
    CHECK(strcmp(list.devices[0].name, "my applaunch-board") == 0);
    CHECK(strcmp(list.devices[1].path, "/dev/input/event21") == 0);
    CHECK(strcmp(list.devices[1].name, "crlf board") == 0);

    /* a keyboard missing one of the four keys does not count: Space (57) cleared */
    snprintf(text, sizeof(text),
             "I: Bus=0003\nN: Name=\"no space\"\nH: Handlers=kbd event5\n"
             "B: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fdfffffffffffffe\n");
    CHECK(cp0_kbd_presence_parse(text, strlen(text), 64, &list) == 0);
    CHECK(list.count == 0);

    /* a 32-bit reader: the same keyboard as 32-bit words counts, the hub does not */
    snprintf(text, sizeof(text),
             "I: Bus=0003\nN: Name=\"Logitech USB Keyboard\"\nH: Handlers=sysrq kbd leds event0 \n"
             "B: KEY=10000 7 ff9f207a c14057ff febeffdf ffefffff ffffffff fffffffe\n\n"
             "I: Bus=0018\nN: Name=\"Goodix Capacitive TouchScreen\"\nH: Handlers=mouse0 event5\n"
             "B: KEY=400 0 0 0 0 0 0 0 0 0 0\n\n"
             "I: Bus=0006\nN: Name=\"applaunch-vkbd\"\nH: Handlers=kbd event9\n"
             "B: KEY=7fffffff ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff "
             "ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff "
             "ffffffff ffffffff fffffffe\n");
    CHECK(cp0_kbd_presence_parse(text, strlen(text), 32, &list) == 1);
    CHECK(strcmp(list.devices[0].path, "/dev/input/event0") == 0);

    /* more keyboards than the list holds: all are counted, the first 16 are listed */
    size_t used = 0;
    for (int i = 0; i < 20; i++)
        used += (size_t)snprintf(text + used, sizeof(text) - used,
                                 "I: Bus=0003\nN: Name=\"kbd %d\"\nH: Handlers=kbd event%d\nB: KEY=%s\n\n", i, 30 + i,
                                 full);
    CHECK(cp0_kbd_presence_parse(text, used, 64, &list) == 20);
    CHECK(list.count == CP0_KBD_PRESENCE_MAX_DEVICES);
    CHECK(strcmp(list.devices[15].path, "/dev/input/event45") == 0);

    /* empty text, a long name is cut, not overflowed */
    CHECK(cp0_kbd_presence_parse("", 0, 64, &list) == 0 && list.count == 0);
    char long_name[400];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    snprintf(text, sizeof(text), "I: Bus=0003\nN: Name=\"%s\"\nH: Handlers=kbd event7\nB: KEY=%s\n", long_name, full);
    CHECK(cp0_kbd_presence_parse(text, strlen(text), 64, &list) == 1);
    CHECK(strlen(list.devices[0].name) == CP0_KBD_PRESENCE_NAME_SIZE - 1);

    /* list equality */
    cp0_kbd_presence_list_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    CHECK(cp0_kbd_presence_list_equal(&a, &b));
    a.count = 1;
    snprintf(a.devices[0].path, sizeof(a.devices[0].path), "/dev/input/event1");
    CHECK(!cp0_kbd_presence_list_equal(&a, &b));
    b = a;
    CHECK(cp0_kbd_presence_list_equal(&a, &b));
    snprintf(b.devices[0].name, sizeof(b.devices[0].name), "renamed");
    CHECK(!cp0_kbd_presence_list_equal(&a, &b));
}

static void test_timer(void)
{
    cp0_kbd_presence_timer_t t;
    cp0_kbd_presence_timer_init(&t, 0, 0, 0, 1000);
    CHECK(t.debounce_ms == 400 && t.max_wait_ms == 2000 && t.poll_ms == 2000);
    /* idle: only the safety poll, every 2 s */
    CHECK(!cp0_kbd_presence_timer_due(&t, 2999));
    CHECK(cp0_kbd_presence_timer_timeout_ms(&t, 1000) == 2000);
    CHECK(cp0_kbd_presence_timer_due(&t, 3000));
    cp0_kbd_presence_timer_scanned(&t, 3000);
    CHECK(!cp0_kbd_presence_timer_due(&t, 4999) && cp0_kbd_presence_timer_due(&t, 5000));

    /* one change: rescan 400 ms later */
    cp0_kbd_presence_timer_on_event(&t, 3100);
    CHECK(cp0_kbd_presence_timer_timeout_ms(&t, 3100) == 400);
    CHECK(!cp0_kbd_presence_timer_due(&t, 3499));
    CHECK(cp0_kbd_presence_timer_due(&t, 3500));
    cp0_kbd_presence_timer_scanned(&t, 3500);
    CHECK(!t.pending);
    CHECK(cp0_kbd_presence_timer_timeout_ms(&t, 3500) == 2000); /* safety poll restarts after a scan */

    /* a burst (IN_CREATE then IN_ATTRIB from udev): debounced after the last change */
    cp0_kbd_presence_timer_on_event(&t, 4000);
    cp0_kbd_presence_timer_on_event(&t, 4150);
    cp0_kbd_presence_timer_on_event(&t, 4300);
    CHECK(!cp0_kbd_presence_timer_due(&t, 4699));
    CHECK(cp0_kbd_presence_timer_due(&t, 4700));
    cp0_kbd_presence_timer_scanned(&t, 4700);

    /* an endless stream of changes still rescans within max_wait of its first change */
    for (uint64_t now = 5000; now < 7500; now += 100) {
        if (cp0_kbd_presence_timer_due(&t, now)) {
            CHECK(now <= 7000);
            cp0_kbd_presence_timer_scanned(&t, now);
            break;
        }
        cp0_kbd_presence_timer_on_event(&t, now);
        CHECK(now < 7000);
    }
    CHECK(!t.pending);

    /* the timeout never goes negative */
    CHECK(cp0_kbd_presence_timer_timeout_ms(&t, 1000000) == 0);
}

/* ---------------------------------------------------------------- watcher */

typedef struct {
    pthread_mutex_t lock;
    int calls;
    unsigned last_count;
    unsigned last_generation;
} listener_log_t;

static void listener(const cp0_kbd_presence_list_t *list, unsigned generation, void *user)
{
    listener_log_t *log = (listener_log_t *)user;
    pthread_mutex_lock(&log->lock);
    log->calls++;
    log->last_count = list->count;
    log->last_generation = generation;
    pthread_mutex_unlock(&log->lock);
}

static int log_calls(listener_log_t *log)
{
    pthread_mutex_lock(&log->lock);
    const int calls = log->calls;
    pthread_mutex_unlock(&log->lock);
    return calls;
}

static void sleep_ms(long ms)
{
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static long wait_calls(listener_log_t *log, int calls, long timeout_ms)
{
    for (long waited = 0; waited <= timeout_ms; waited += 10) {
        if (log_calls(log) >= calls) return waited;
        sleep_ms(10);
    }
    return -1;
}

static void write_devices(const char *path, int keyboards)
{
    static const char *blocks[] = {
        "I: Bus=0005 Vendor=04e8 Product=7021 Version=0001\nN: Name=\"M4 Keyboard\"\nH: Handlers=sysrq kbd leds "
        "event10 \nB: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe\n\n",
        "I: Bus=0003 Vendor=046d Product=c31c Version=0110\nN: Name=\"Logitech USB Keyboard\"\nH: Handlers=sysrq "
        "kbd leds event8 \nB: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe\n\n",
    };
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    FILE *f = fopen(tmp, "w");
    CHECK(f != NULL);
    fputs("I: Bus=0018 Vendor=0416 Product=038f Version=0100\nN: Name=\"Goodix Capacitive TouchScreen\"\n"
          "H: Handlers=mouse0 event5 \nB: KEY=400 0 0 0 0 0\n\n",
          f);
    for (int i = 0; i < keyboards && i < 2; i++) fputs(blocks[i], f);
    fclose(f);
    CHECK(rename(tmp, path) == 0);
}

static void touch(const char *dir, const char *name)
{
    char path[600];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fclose(f);
}

static void test_watcher(void)
{
    char root[] = "/tmp/cp0-kbd-presence-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char dev_dir[600], devices[600];
    snprintf(dev_dir, sizeof(dev_dir), "%s/input", root);
    snprintf(devices, sizeof(devices), "%s/devices", root);
    CHECK(mkdir(dev_dir, 0755) == 0);
    write_devices(devices, 1);

    /* inotify path: the safety poll is far away, so only the /dev/input change can trigger the rescan */
    cp0_kbd_presence_options_t options = {devices, dev_dir, 60, 1000, 60000, 64};
    cp0_kbd_presence_watcher_t *w = cp0_kbd_presence_watcher_start(&options);
    CHECK(w != NULL);
    cp0_kbd_presence_list_t list;
    CHECK(cp0_kbd_presence_watcher_generation(w) == 1); /* scanned before start() returned */
    CHECK(cp0_kbd_presence_watcher_snapshot(w, &list) == 1);
    CHECK(list.count == 1 && strcmp(list.devices[0].name, "M4 Keyboard") == 0);

    listener_log_t log = {PTHREAD_MUTEX_INITIALIZER, 0, 0, 0};
    CHECK(cp0_kbd_presence_watcher_add_listener(w, listener, &log) == 0);
    CHECK(log_calls(&log) == 1 && log.last_count == 1 && log.last_generation == 1); /* the current list at once */

    /* a USB keyboard is plugged in: devices changes, then udev creates event8 */
    write_devices(devices, 2);
    touch(dev_dir, "event8");
    const long waited = wait_calls(&log, 2, 3000);
    CHECK(waited >= 0);
    CHECK(log.last_count == 2 && log.last_generation == 2);
    CHECK(cp0_kbd_presence_watcher_generation(w) == 2);

    /* a change of /dev/input that leaves the keyboard list as it was: rescanned, no new generation */
    CHECK(chmod(dev_dir, 0755) == 0);
    touch(dev_dir, "mouse3");
    sleep_ms(300);
    CHECK(log_calls(&log) == 2 && cp0_kbd_presence_watcher_generation(w) == 2);

    /* the keyboard goes away (attribute change only: IN_ATTRIB) */
    write_devices(devices, 1);
    char event8[700];
    snprintf(event8, sizeof(event8), "%s/event8", dev_dir);
    CHECK(chmod(event8, 0600) == 0);
    CHECK(wait_calls(&log, 3, 3000) >= 0);
    CHECK(log.last_count == 1 && log.last_generation == 3);

    /* removed listeners are not called again */
    cp0_kbd_presence_watcher_remove_listener(w, listener, &log);
    write_devices(devices, 0);
    touch(dev_dir, "event9");
    sleep_ms(300);
    CHECK(log_calls(&log) == 3);
    CHECK(cp0_kbd_presence_watcher_generation(w) == 4);
    CHECK(cp0_kbd_presence_watcher_snapshot(w, &list) == 4 && list.count == 0);
    cp0_kbd_presence_watcher_stop(w);

    /* safety poll path: no inotify event at all (no such directory), the 150 ms poll finds the change */
    write_devices(devices, 0);
    cp0_kbd_presence_options_t poll_only = {devices, "/nonexistent-dev-input", 60, 1000, 150, 64};
    w = cp0_kbd_presence_watcher_start(&poll_only);
    CHECK(w != NULL);
    listener_log_t log2 = {PTHREAD_MUTEX_INITIALIZER, 0, 0, 0};
    CHECK(cp0_kbd_presence_watcher_add_listener(w, listener, &log2) == 0);
    CHECK(log2.last_count == 0);
    write_devices(devices, 2);
    CHECK(wait_calls(&log2, 2, 2000) >= 0);
    CHECK(log2.last_count == 2);
    /* an unreadable devices file keeps the last list */
    CHECK(unlink(devices) == 0);
    sleep_ms(400);
    CHECK(cp0_kbd_presence_watcher_snapshot(w, &list) == 2 && list.count == 2);
    cp0_kbd_presence_watcher_stop(w);

    /* the shared watcher is one instance, reference counted */
    cp0_kbd_presence_watcher_t *s1 = cp0_kbd_presence_shared_acquire();
    cp0_kbd_presence_watcher_t *s2 = cp0_kbd_presence_shared_acquire();
    CHECK(s1 != NULL && s1 == s2);
    cp0_kbd_presence_shared_release();
    CHECK(cp0_kbd_presence_watcher_generation(s1) >= 1); /* still running after one release */
    cp0_kbd_presence_shared_release();

    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

/* hot-plug of a pointer: its own generation; the keyboard generation and the keyboard listeners do not move */
static void test_pointer_watcher(void)
{
    char root[] = "/tmp/cp0-ptr-presence-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char devices[600];
    snprintf(devices, sizeof(devices), "%s/devices", root);
    write_devices(devices, 1);
    cp0_kbd_presence_options_t poll_only = {devices, "/nonexistent-dev-input", 60, 1000, 100, 64};
    cp0_kbd_presence_watcher_t *w = cp0_kbd_presence_watcher_start(&poll_only);
    CHECK(w != NULL);
    cp0_kbd_presence_list_t list;
    CHECK(cp0_kbd_presence_watcher_pointer_generation(w) == 1);
    CHECK(cp0_kbd_presence_watcher_pointer_snapshot(w, &list) == 1 && list.count == 0);
    listener_log_t log = {PTHREAD_MUTEX_INITIALIZER, 0, 0, 0};
    CHECK(cp0_kbd_presence_watcher_add_listener(w, listener, &log) == 0 && log_calls(&log) == 1);

    /* the HackBerry pad appears next to the keyboard */
    char tmp[700];
    snprintf(tmp, sizeof(tmp), "%s.new", devices);
    FILE *f = fopen(tmp, "w");
    CHECK(f != NULL);
    fputs("I: Bus=0005 Vendor=04e8 Product=7021 Version=0001\nN: Name=\"M4 Keyboard\"\nH: Handlers=sysrq kbd leds "
          "event10 \nB: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe\n\n"
          "I: Bus=0003 Vendor=1209 Product=0b20 Version=0111\nN: Name=\"ZitaoTech HACKBerryPiQ20 Mouse\"\n"
          "H: Handlers=mouse0 event1 \nB: PROP=0\nB: EV=17\nB: KEY=1f0000 0 0 0 0\nB: REL=1943\nB: MSC=10\n\n",
          f);
    fclose(f);
    CHECK(rename(tmp, devices) == 0);
    unsigned gen = 0;
    for (int i = 0; i < 200 && gen < 2; i++) {
        sleep_ms(10);
        gen = cp0_kbd_presence_watcher_pointer_generation(w);
    }
    CHECK(gen == 2);
    CHECK(cp0_kbd_presence_watcher_pointer_snapshot(w, &list) == 2 && list.count == 1);
    CHECK(strcmp(list.devices[0].path, "/dev/input/event1") == 0);
    CHECK(cp0_kbd_presence_watcher_generation(w) == 1 && log_calls(&log) == 1);

    /* it goes away again */
    write_devices(devices, 1);
    for (int i = 0; i < 200 && gen < 3; i++) {
        sleep_ms(10);
        gen = cp0_kbd_presence_watcher_pointer_generation(w);
    }
    CHECK(gen == 3);
    CHECK(cp0_kbd_presence_watcher_pointer_snapshot(w, &list) == 3 && list.count == 0);
    CHECK(cp0_kbd_presence_watcher_generation(w) == 1 && log_calls(&log) == 1);
    cp0_kbd_presence_watcher_remove_listener(w, listener, &log);
    cp0_kbd_presence_watcher_stop(w);
    CHECK(cp0_kbd_presence_watcher_pointer_generation(NULL) == 0);
    CHECK(cp0_kbd_presence_watcher_pointer_snapshot(NULL, &list) == 0 && list.count == 0);

    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

/* The ignore list (APPLAUNCH_KBD_IGNORE, report 028 X5) against the FZ-M1 tablet (tests/fixtures/
 * proc_bus_input_devices_fzm1.txt, modelled on the x-a probe): its built-in "AT Translated Set 2 keyboard" (i8042, bus
 * 0011) is always there, plus a USB keyboard. Not keyboards: the power button, the ACPI video bus, the Sharp touch. */
static void test_ignore_list(const char *fixture)
{
    /* entries: names exactly, bus:XXXX in hex, spaces around, drop: wins */
    CHECK(cp0_kbd_presence_ignore_match(NULL, "AT Translated Set 2 keyboard", 0x11) == CP0_KBD_IGNORE_NO);
    CHECK(cp0_kbd_presence_ignore_match("", "AT Translated Set 2 keyboard", 0x11) == CP0_KBD_IGNORE_NO);
    CHECK(cp0_kbd_presence_ignore_match("bus:0011", "AT Translated Set 2 keyboard", 0x11) == CP0_KBD_IGNORE_PRESENCE);
    CHECK(cp0_kbd_presence_ignore_match("bus:11", "x", 0x11) == CP0_KBD_IGNORE_PRESENCE);
    CHECK(cp0_kbd_presence_ignore_match("bus:001E", "x", 0x1e) == CP0_KBD_IGNORE_PRESENCE);
    CHECK(cp0_kbd_presence_ignore_match("bus:0011", "Logitech USB Keyboard", 0x03) == CP0_KBD_IGNORE_NO);
    CHECK(cp0_kbd_presence_ignore_match("bus:", "x", 0) == CP0_KBD_IGNORE_NO);
    CHECK(cp0_kbd_presence_ignore_match("bus:00zz", "x", 0) == CP0_KBD_IGNORE_NO);
    CHECK(cp0_kbd_presence_ignore_match(" M4 Keyboard , bus:0011 ", "M4 Keyboard", 0x05) == CP0_KBD_IGNORE_PRESENCE);
    CHECK(cp0_kbd_presence_ignore_match("M4", "M4 Keyboard", 0x05) == CP0_KBD_IGNORE_NO); /* whole names only */
    CHECK(cp0_kbd_presence_ignore_match("m4 keyboard", "M4 Keyboard", 0x05) == CP0_KBD_IGNORE_NO);
    CHECK(cp0_kbd_presence_ignore_match("drop:Barcode Scanner", "Barcode Scanner", 0x03) == CP0_KBD_IGNORE_DROP);
    CHECK(cp0_kbd_presence_ignore_match("drop:bus:0003", "Barcode Scanner", 0x03) == CP0_KBD_IGNORE_DROP);
    CHECK(cp0_kbd_presence_ignore_match("bus:0003,drop:Barcode Scanner", "Barcode Scanner", 0x03) ==
          CP0_KBD_IGNORE_DROP);
    CHECK(cp0_kbd_presence_ignore_match("drop:", "x", 0) == CP0_KBD_IGNORE_NO);
    CHECK(cp0_kbd_presence_ignore_match(",,", "x", 0) == CP0_KBD_IGNORE_NO);

    size_t len = 0;
    const char *text = read_text(fixture, &len);
    cp0_kbd_presence_list_t list, counted;
    CHECK(cp0_kbd_presence_parse(text, len, 64, &list) == 2);
    CHECK(strcmp(list.devices[0].name, "AT Translated Set 2 keyboard") == 0 && list.devices[0].bus == 0x0011);
    CHECK(strcmp(list.devices[0].path, "/dev/input/event3") == 0);
    CHECK(strcmp(list.devices[1].name, "Logitech USB Keyboard") == 0);
    /* the pc profile's bus:0011: the USB keyboard counts, the built-in one does not; both stay in the read list */
    CHECK(cp0_kbd_presence_counted(&list, "bus:0011", &counted) == 1);
    CHECK(strcmp(counted.devices[0].name, "Logitech USB Keyboard") == 0);
    CHECK(cp0_kbd_presence_counted(&list, NULL, &counted) == 2);
    CHECK(cp0_kbd_presence_counted(&list, "AT Translated Set 2 keyboard", &counted) == 1);
    /* the USB keyboard unplugged: nothing counts, so the on-screen keyboard shows; the i8042 keys are still read */
    const char *usb = strstr(text, "I: Bus=0003");
    CHECK(usb != NULL);
    CHECK(cp0_kbd_presence_parse(text, (size_t)(usb - text), 64, &list) == 1);
    CHECK(cp0_kbd_presence_counted(&list, "bus:0011", &counted) == 0);
    CHECK(list.count == 1 && strcmp(list.devices[0].path, "/dev/input/event3") == 0);
    CHECK(cp0_kbd_presence_counted(NULL, "bus:0011", &counted) == 0);

    /* drop: in place, order kept */
    CHECK(cp0_kbd_presence_parse(text, len, 64, &list) == 2);
    CHECK(cp0_kbd_presence_drop(&list, "bus:0011") == 0 && list.count == 2);
    CHECK(cp0_kbd_presence_drop(&list, "drop:bus:0011") == 1 && list.count == 1);
    CHECK(strcmp(list.devices[0].name, "Logitech USB Keyboard") == 0);

    /* the reader (and so the watcher) applies the environment's drop entries, never the plain ones */
    cp0_kbd_presence_list_t read_list;
    CHECK(setenv(CP0_KBD_IGNORE_ENV, "bus:0011", 1) == 0);
    CHECK(cp0_kbd_presence_enumerate(fixture, 64, &read_list) == 2 && read_list.count == 2);
    CHECK(setenv(CP0_KBD_IGNORE_ENV, "drop:AT Translated Set 2 keyboard", 1) == 0);
    CHECK(cp0_kbd_presence_enumerate(fixture, 64, &read_list) == 1 && read_list.count == 1);
    CHECK(strcmp(read_list.devices[0].name, "Logitech USB Keyboard") == 0);
    CHECK(cp0_kbd_presence_enumerate_all(fixture, 64, NULL, NULL) == 1); /* counted right without a list */
    CHECK(cp0_kbd_presence_enumerate("/nonexistent/devices", 64, &read_list) == -1 && read_list.count == 0);
    CHECK(unsetenv(CP0_KBD_IGNORE_ENV) == 0);
    CHECK(cp0_kbd_presence_enumerate(fixture, 64, &read_list) == 2);
}

/* ---------------------------------------------------------------- media-key devices (task c-7) */

static int list_has(const cp0_kbd_presence_list_t *list, const char *path, const char *name)
{
    for (unsigned i = 0; i < list->count; i++)
        if (strcmp(list->devices[i].path, path) == 0 && strcmp(list->devices[i].name, name) == 0) return 1;
    return 0;
}

/* Every device seen on the Pi boards: the media-key devices are the two keyboards' Consumer Control siblings (the
 * HackBerry's and the 2.4G receiver's) and the speaker's AVRCP node. Not: every keyboard (their media keys come with
 * them), the System Control siblings and the power keys (KEY_POWER only), the RTL2832U IR receiver and the three HDMI CEC
 * devices (rc-core receivers, although their keymaps have MUTE / VOLUMEUP), applaunch-vkbd (virtual), the touch panel. */
static void test_media_all_boards(const char *fixture)
{
    size_t len = 0;
    const char *text = read_text(fixture, &len);
    cp0_kbd_presence_list_t media;
    CHECK(cp0_kbd_presence_parse_media(text, len, 64, &media) == 3);
    CHECK(media.count == 3);
    CHECK(strcmp(media.devices[0].path, "/dev/input/event3") == 0);
    CHECK(strcmp(media.devices[0].name, "ZitaoTech HACKBerryPiQ20 Consumer Control") == 0);
    CHECK(strcmp(media.devices[1].path, "/dev/input/event6") == 0);
    CHECK(strcmp(media.devices[1].name, "2.4G Composite Devic Consumer Control") == 0);
    CHECK(strcmp(media.devices[2].path, "/dev/input/event11") == 0);
    CHECK(strcmp(media.devices[2].name, "XWF-M18-M28-M38 (AVRCP)") == 0);
    CHECK(media.devices[2].bus == CP0_KBD_PRESENCE_BUS_BLUETOOTH);
    CHECK(cp0_kbd_presence_parse_media(text, len, 64, NULL) == 3);
    /* the keyboard and pointer lists are what they were before media-key devices existed */
    cp0_kbd_presence_list_t keyboards, pointers, again;
    CHECK(cp0_kbd_presence_enumerate_lists(fixture, 64, &keyboards, &pointers, &again) == 5);
    check_fixture_list(&keyboards);
    CHECK(pointers.count == 2 && cp0_kbd_presence_list_equal(&again, &media));
    CHECK(cp0_kbd_presence_enumerate_lists(fixture, 64, NULL, NULL, &again) == 5 && again.count == 3);
    again.count = 7;
    CHECK(cp0_kbd_presence_enumerate_lists("/nonexistent/devices", 64, &keyboards, &pointers, &again) == -1);
    CHECK(keyboards.count == 0 && pointers.count == 0 && again.count == 0);
}

/* The FZ-M1 tablet as it is (tests/fixtures/proc_bus_input_devices_fzm1_full.txt: the whole /proc/bus/input/devices of
 * the owner's tablet, 2026-10-24, the BIOS sending the volume buttons as KEY_VOLUMEUP / DOWN, a Telink 2.4 GHz keyboard
 * and mouse plugged in). */
static void test_media_fzm1(const char *fixture, const char *older)
{
    size_t len = 0;
    const char *text = read_text(fixture, &len);
    cp0_kbd_presence_list_t keyboards, media, pointers;
    /* keyboards: the built-in i8042 one (where the Windows button and Win+F14 arrive) and the Telink receiver's */
    CHECK(cp0_kbd_presence_parse(text, len, 64, &keyboards) == 2);
    CHECK(list_has(&keyboards, "/dev/input/event2", "AT Translated Set 2 keyboard"));
    CHECK(list_has(&keyboards, "/dev/input/event6", "Telink Wireless Receiver"));
    CHECK(keyboards.devices[0].bus == 0x0011);
    /* media-key devices: the receiver's consumer control, the ACPI video bus (brightness), Intel Virtual Buttons (volume,
     * Windows, rotation lock, power) and Panasonic Laptop Support (volume, mute, brightness), in file order */
    CHECK(cp0_kbd_presence_parse_media(text, len, 64, &media) == 4);
    CHECK(strcmp(media.devices[0].path, "/dev/input/event4") == 0);
    CHECK(strcmp(media.devices[0].name, "Telink Wireless Receiver Consumer Control") == 0);
    CHECK(strcmp(media.devices[1].path, "/dev/input/event8") == 0);
    CHECK(strcmp(media.devices[1].name, "Video Bus") == 0);
    CHECK(strcmp(media.devices[2].path, "/dev/input/event9") == 0);
    CHECK(strcmp(media.devices[2].name, "Intel Virtual Buttons") == 0);
    CHECK(strcmp(media.devices[3].path, "/dev/input/event10") == 0);
    CHECK(strcmp(media.devices[3].name, "Panasonic Laptop Support") == 0);
    CHECK(media.devices[1].bus == 0x0019 && media.devices[3].bus == 0x0019);
    /* not media: the two ACPI power buttons, the mouse, the System Control sibling (power / sleep / wake only), the Sharp
     * touch panel, the headphone jack switch (no keys) */
    CHECK(cp0_kbd_presence_parse_pointers(text, len, 64, &pointers) == 1);
    CHECK(list_has(&pointers, "/dev/input/event3", "Telink Wireless Receiver Mouse"));
    /* the bits behind it, as the tablet prints them */
    const char *vbtn = "2000000000000 0 0 0 0 1000000000000 0 201c000000000000 0";
    CHECK(bit(vbtn, 64, 114) && bit(vbtn, 64, 115) && bit(vbtn, 64, 125) && bit(vbtn, 64, 116) && bit(vbtn, 64, 0x231));
    CHECK(!bit(vbtn, 64, 113) && !bit(vbtn, 64, 224));
    const char *pana = "7ff0000 0 0 0 800000000000 0 0 1100300002000 104000 e000000000000 0";
    CHECK(bit(pana, 64, 113) && bit(pana, 64, 114) && bit(pana, 64, 115) && bit(pana, 64, 224) && bit(pana, 64, 225));
    CHECK(!bit(pana, 64, 125));
    CHECK(bit("3e000b00000000 0 0 0", 64, 224) && bit("3e000b00000000 0 0 0", 64, 225));

    /* the tablet's profile (APPLAUNCH_KBD_IGNORE=bus:0011): the i8042 keyboard is read but not counted, the media-key
     * devices are read and never counted; "drop:" leaves one out */
    CHECK(setenv(CP0_KBD_IGNORE_ENV, "bus:0011", 1) == 0);
    cp0_kbd_presence_list_t counted, read_media;
    CHECK(cp0_kbd_presence_enumerate_lists(fixture, 64, &keyboards, NULL, &read_media) == 2);
    CHECK(cp0_kbd_presence_counted(&keyboards, "bus:0011", &counted) == 1);
    CHECK(strcmp(counted.devices[0].name, "Telink Wireless Receiver") == 0);
    CHECK(cp0_kbd_presence_list_equal(&read_media, &media));
    CHECK(setenv(CP0_KBD_IGNORE_ENV, "bus:0011, drop:Video Bus", 1) == 0);
    CHECK(cp0_kbd_presence_enumerate_lists(fixture, 64, &keyboards, NULL, &read_media) == 2);
    CHECK(read_media.count == 3 && !list_has(&read_media, "/dev/input/event8", "Video Bus"));
    CHECK(setenv(CP0_KBD_IGNORE_ENV, "drop:bus:0019", 1) == 0); /* every ACPI / platform device */
    CHECK(cp0_kbd_presence_enumerate_lists(fixture, 64, &keyboards, NULL, &read_media) == 2);
    CHECK(read_media.count == 1 && strcmp(read_media.devices[0].name, "Telink Wireless Receiver Consumer Control") == 0);
    CHECK(unsetenv(CP0_KBD_IGNORE_ENV) == 0);

    /* the tablet as the owner runs it (0.8.7-c7.1 test, 2026-10-24): no USB keyboard, only the built-in i8042 one, which
     * the profile's bus:0011 keeps from counting. The same file without the Telink receiver's blocks: one keyboard read
     * and none counted, the three platform media-key devices, no pointer */
    static char solo[65536];
    size_t solo_len = 0;
    size_t block = 0; /* where the current device block starts */
    int telink = 0;
    for (size_t at = 0; at < len;) {
        size_t eol = at;
        while (eol < len && text[eol] != '\n') eol++;
        const size_t next = eol < len ? eol + 1 : len;
        const int blank = eol == at || (eol == at + 1 && text[at] == '\r');
        for (size_t i = at; i + 6 <= eol && !telink; i++) telink = memcmp(text + i, "Telink", 6) == 0;
        if (blank || next == len) { /* a block ends with its blank line (or the file) */
            if (!telink) {
                memcpy(solo + solo_len, text + block, next - block);
                solo_len += next - block;
            }
            block = next;
            telink = 0;
        }
        at = next;
    }
    CHECK(cp0_kbd_presence_parse(solo, solo_len, 64, &keyboards) == 1);
    CHECK(strcmp(keyboards.devices[0].name, "AT Translated Set 2 keyboard") == 0);
    CHECK(cp0_kbd_presence_counted(&keyboards, "bus:0011", &counted) == 0); /* the on-screen keyboard shows */
    CHECK(cp0_kbd_presence_parse_media(solo, solo_len, 64, &media) == 3);
    CHECK(strcmp(media.devices[0].name, "Video Bus") == 0 && strcmp(media.devices[1].name, "Intel Virtual Buttons") == 0 &&
          strcmp(media.devices[2].name, "Panasonic Laptop Support") == 0);
    CHECK(cp0_kbd_presence_parse_pointers(solo, solo_len, 64, &pointers) == 0);

    /* the earlier FZ-M1 capture (tests/fixtures/proc_bus_input_devices_fzm1.txt: the x-a probe, without the platform
     * button devices): the video bus only */
    text = read_text(older, &len);
    CHECK(cp0_kbd_presence_parse_media(text, len, 64, &media) == 1);
    CHECK(strcmp(media.devices[0].path, "/dev/input/event4") == 0 && strcmp(media.devices[0].name, "Video Bus") == 0);
}

/* the rule's edges on made-up blocks */
static void test_media_rule_edges(void)
{
    const char *full = "1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe";
    char text[8192];
    cp0_kbd_presence_list_t list;
    snprintf(text, sizeof(text),
             /* a keyboard with media keys: a keyboard, never a media-key device as well */
             "I: Bus=0003\nN: Name=\"kbd\"\nH: Handlers=kbd event1\nB: KEY=%s\n\n"
             /* volume up alone */
             "I: Bus=0003\nN: Name=\"knob\"\nH: Handlers=kbd event2\nB: KEY=8000000000000 0\n\n"
             /* a lone bit in word 6: KEY 384, not a media key */
             "I: Bus=0019\nN: Name=\"far\"\nH: Handlers=kbd event3\nB: KEY=1 0 0 0 0 0 0\n\n"
             /* virtual, the launcher's own, no node, rc-core: none */
             "I: Bus=0006\nN: Name=\"virtual\"\nH: Handlers=kbd event4\nB: KEY=e000000000000 0\n\n"
             "I: Bus=0003\nN: Name=\"applaunch-media\"\nH: Handlers=kbd event5\nB: KEY=e000000000000 0\n\n"
             "I: Bus=0003\nN: Name=\"no node\"\nH: Handlers=kbd\nB: KEY=e000000000000 0\n\n"
             "I: Bus=0003\nN: Name=\"ir\"\nS: Sysfs=/devices/platform/x/usb3/3-2/rc/rc1/input12\nH: Handlers=kbd event6\n"
             "B: KEY=e000000000000 0\n\n"
             /* KEY_POWER and KEY_PLAYPAUSE only (a headset without volume keys): no */
             "I: Bus=0005\nN: Name=\"play\"\nH: Handlers=kbd event7\nB: KEY=1000000000 10000000000000 0\n\n"
             /* mute alone, CRLF, and a plain Sysfs path */
             "I: Bus=0005\r\nN: Name=\"mute\"\r\nS: Sysfs=/devices/virtual/input/input30\r\nH: Handlers=kbd event8\r\n"
             "B: KEY=2000000000000 0\r\n",
             full);
    /* knob: bit 115 (word 1 bit 51), mute: bit 113 (word 1 bit 49) */
    CHECK(cp0_kbd_presence_parse_media(text, strlen(text), 64, &list) == 2);
    CHECK(strcmp(list.devices[0].name, "knob") == 0 && strcmp(list.devices[1].name, "mute") == 0);
    CHECK(cp0_kbd_presence_parse(text, strlen(text), 64, &list) == 1 && strcmp(list.devices[0].name, "kbd") == 0);
    /* brightness down alone as a 32-bit reader prints it (word 7 bit 0 = 224); the same words read as 64-bit: bit 448 */
    snprintf(text, sizeof(text), "I: Bus=0019\nN: Name=\"dim32\"\nH: Handlers=kbd event3\nB: KEY=1 0 0 0 0 0 0 0\n");
    CHECK(cp0_kbd_presence_parse_media(text, strlen(text), 32, &list) == 1);
    CHECK(strcmp(list.devices[0].path, "/dev/input/event3") == 0);
    CHECK(cp0_kbd_presence_parse_media(text, strlen(text), 64, &list) == 0);

    /* more than the list holds: all counted, 16 listed; the readers open the first CP0_KBD_PRESENCE_MAX_MEDIA */
    size_t used = 0;
    for (int i = 0; i < 20; i++)
        used += (size_t)snprintf(text + used, sizeof(text) - used,
                                 "I: Bus=0003\nN: Name=\"cc %d\"\nH: Handlers=kbd event%d\nB: KEY=e000000000000 0\n\n", i,
                                 40 + i);
    CHECK(cp0_kbd_presence_parse_media(text, used, 64, &list) == 20);
    CHECK(list.count == CP0_KBD_PRESENCE_MAX_DEVICES && CP0_KBD_PRESENCE_MAX_MEDIA <= CP0_KBD_PRESENCE_MAX_DEVICES);
}

/* hot-plug of a media-key device (a Bluetooth headset connects): its own generation; the keyboard generation and the
 * keyboard listeners do not move, so the on-screen keyboard policy and input.state are untouched */
static void test_media_watcher(void)
{
    char root[] = "/tmp/cp0-media-presence-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char devices[600];
    snprintf(devices, sizeof(devices), "%s/devices", root);
    write_devices(devices, 1);
    cp0_kbd_presence_options_t poll_only = {devices, "/nonexistent-dev-input", 60, 1000, 100, 64};
    cp0_kbd_presence_watcher_t *w = cp0_kbd_presence_watcher_start(&poll_only);
    CHECK(w != NULL);
    cp0_kbd_presence_list_t list;
    CHECK(cp0_kbd_presence_watcher_media_generation(w) == 1);
    CHECK(cp0_kbd_presence_watcher_media_snapshot(w, &list) == 1 && list.count == 0);
    listener_log_t log = {PTHREAD_MUTEX_INITIALIZER, 0, 0, 0};
    CHECK(cp0_kbd_presence_watcher_add_listener(w, listener, &log) == 0 && log_calls(&log) == 1);

    char tmp[700];
    snprintf(tmp, sizeof(tmp), "%s.new", devices);
    FILE *f = fopen(tmp, "w");
    CHECK(f != NULL);
    fputs("I: Bus=0005 Vendor=04e8 Product=7021 Version=0001\nN: Name=\"M4 Keyboard\"\nH: Handlers=sysrq kbd leds "
          "event10 \nB: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe\n\n"
          "I: Bus=0005 Vendor=0000 Product=0000 Version=0000\nN: Name=\"Headset (AVRCP)\"\n"
          "S: Sysfs=/devices/virtual/input/input31\nH: Handlers=kbd event12 \nB: PROP=0\nB: EV=100007\n"
          "B: KEY=400000000c0000 104200000000 0 10300 41ea00002400 1e378000000000 f810000010000ffc\nB: REL=0\n\n",
          f);
    fclose(f);
    CHECK(rename(tmp, devices) == 0);
    unsigned gen = 0;
    for (int i = 0; i < 200 && gen < 2; i++) {
        sleep_ms(10);
        gen = cp0_kbd_presence_watcher_media_generation(w);
    }
    CHECK(gen == 2);
    CHECK(cp0_kbd_presence_watcher_media_snapshot(w, &list) == 2 && list.count == 1);
    CHECK(strcmp(list.devices[0].path, "/dev/input/event12") == 0);
    CHECK(cp0_kbd_presence_watcher_generation(w) == 1 && log_calls(&log) == 1);
    CHECK(cp0_kbd_presence_watcher_pointer_generation(w) == 1);

    write_devices(devices, 1); /* it goes away again */
    for (int i = 0; i < 200 && gen < 3; i++) {
        sleep_ms(10);
        gen = cp0_kbd_presence_watcher_media_generation(w);
    }
    CHECK(gen == 3);
    CHECK(cp0_kbd_presence_watcher_media_snapshot(w, &list) == 3 && list.count == 0);
    CHECK(cp0_kbd_presence_watcher_generation(w) == 1 && log_calls(&log) == 1);
    cp0_kbd_presence_watcher_remove_listener(w, listener, &log);
    cp0_kbd_presence_watcher_stop(w);
    CHECK(cp0_kbd_presence_watcher_media_generation(NULL) == 0);
    CHECK(cp0_kbd_presence_watcher_media_snapshot(NULL, &list) == 0 && list.count == 0);

    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

int main(int argc, char **argv)
{
    CHECK(argc == 4); /* the fixture paths: every board, the FZ-M1 tablet (keyboards), the FZ-M1 tablet in full */
    test_bitmap_decoder();
    test_fixture(argv[1]);
    test_ignore_list(argv[2]);
    test_rule_edges();
    test_pointer_classifier();
    test_timer();
    test_watcher();
    test_pointer_watcher();
    test_media_all_boards(argv[1]);
    test_media_fzm1(argv[3], argv[2]);
    test_media_rule_edges();
    test_media_watcher();
    printf("test_keyboard_presence: PASS\n");
    return 0;
}
