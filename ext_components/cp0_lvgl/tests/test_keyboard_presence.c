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

int main(int argc, char **argv)
{
    CHECK(argc == 2); /* the fixture path */
    test_bitmap_decoder();
    test_fixture(argv[1]);
    test_rule_edges();
    test_pointer_classifier();
    test_timer();
    test_watcher();
    test_pointer_watcher();
    printf("test_keyboard_presence: PASS\n");
    return 0;
}
