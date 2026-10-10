/*
 * Unit test of the keyboard thread's source bookkeeping (src/cp0/cp0_keyboard_sources.c). The device
 * pointers stand in for struct libinput_device; the sequences replay what libinput reports on the deck:
 * suspend/resume around an external app, a Bluetooth keyboard falling asleep, a USB keyboard, the bt-keyboard
 * symlink resolving to a listed node. The invariant under test: one node is never read by two devices
 * (duplicated keys) and a vanished node is never re-added during its grace.
 */
#include "cp0_keyboard_sources.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

typedef char node_t[CP0_KBD_SOURCES_NODE_SIZE];

static void plan(const cp0_kbd_sources_t *s, node_t *wanted, unsigned n, uint64_t now, cp0_kbd_sources_plan_t *p)
{
    cp0_kbd_sources_plan(s, (const char (*)[CP0_KBD_SOURCES_NODE_SIZE])wanted, n, now, CP0_KBD_SOURCES_GRACE_MS, p);
}

static int settled(const cp0_kbd_sources_t *s, node_t *wanted, unsigned n)
{
    return cp0_kbd_sources_settled(s, (const char (*)[CP0_KBD_SOURCES_NODE_SIZE])wanted, n);
}

/* how many live slots read `node` */
static int readers(const cp0_kbd_sources_t *s, const char *node)
{
    int n = 0;
    for (int i = 0; i < CP0_KBD_SOURCES_MAX; i++)
        n += s->slot[i].used && s->slot[i].dev && strcmp(s->slot[i].node, node) == 0;
    return n;
}

static void test_deck_m4_only(void)
{
    /* deck: LV_LINUX_KEYBOARD_DEVICE=/dev/input/bt-keyboard -> event3, and the presence list has event3 */
    cp0_kbd_sources_t s;
    memset(&s, 0, sizeof(s));
    node_t wanted[2] = {"/dev/input/event3", "/dev/input/event3"};
    cp0_kbd_sources_plan_t p;
    plan(&s, wanted, 2, 0, &p);
    CHECK(p.purge_count == 0 && p.add_count == 1 && p.add[0] == 0); /* the symlink and the list are one node */

    int m4a, m4b, m4c;
    CHECK(cp0_kbd_sources_track(&s, "/dev/input/event3", &m4a) >= 0);
    void *release = (void *)1;
    CHECK(cp0_kbd_sources_on_added(&s, "/dev/input/event3", &m4a, &release) == CP0_KBD_ADDED_IGNORE);
    CHECK(release == NULL);
    CHECK(settled(&s, wanted, 2));
    plan(&s, wanted, 2, 10, &p);
    CHECK(p.add_count == 0 && p.purge_count == 0);

    /* external app: libinput_suspend removes it, libinput_resume re-adds the same node as a new device */
    CHECK(cp0_kbd_sources_on_removed(&s, &m4a, 1000) >= 0);
    CHECK(!settled(&s, wanted, 2)); /* a grace is running: keep retrying */
    plan(&s, wanted, 2, 1100, &p);
    CHECK(p.add_count == 0 && p.purge_count == 0); /* the old fix: no add of our own during the grace */
    CHECK(cp0_kbd_sources_on_added(&s, "/dev/input/event3", &m4b, &release) == CP0_KBD_ADDED_ADOPT);
    CHECK(release == &m4a); /* the caller drops its reference on the old device */
    CHECK(readers(&s, "/dev/input/event3") == 1);
    CHECK(settled(&s, wanted, 2));

    /* the M4 falls asleep: its node disappears, the list empties, the symlink dangles */
    CHECK(cp0_kbd_sources_on_removed(&s, &m4b, 5000) >= 0);
    plan(&s, wanted, 0, 6499, &p);
    CHECK(p.purge_count == 0 && p.add_count == 0);
    plan(&s, wanted, 0, 6500, &p);
    CHECK(p.purge_count == 1 && p.add_count == 0); /* grace over: drop libinput's record of it */
    cp0_kbd_sources_free(&s, p.purge[0]);
    CHECK(settled(&s, wanted, 0));

    /* it wakes up as event4: added once */
    node_t woke[2] = {"/dev/input/event4", "/dev/input/event4"};
    plan(&s, woke, 2, 60000, &p);
    CHECK(p.add_count == 1);
    CHECK(cp0_kbd_sources_track(&s, "/dev/input/event4", &m4c) >= 0);
    CHECK(cp0_kbd_sources_on_added(&s, "/dev/input/event4", &m4c, &release) == CP0_KBD_ADDED_IGNORE);
    CHECK(readers(&s, "/dev/input/event4") == 1 && cp0_kbd_sources_live(&s) == 1);
}

static void test_sleep_during_app(void)
{
    /* the keyboard leaves while an app runs: libinput_resume fails and re-adds nothing */
    cp0_kbd_sources_t s;
    memset(&s, 0, sizeof(s));
    int usb, bt, usb2;
    node_t wanted[2] = {"/dev/input/event3", "/dev/input/event8"};
    CHECK(cp0_kbd_sources_track(&s, "/dev/input/event3", &bt) >= 0);
    CHECK(cp0_kbd_sources_track(&s, "/dev/input/event8", &usb) >= 0);
    CHECK(cp0_kbd_sources_on_removed(&s, &bt, 100) >= 0);
    CHECK(cp0_kbd_sources_on_removed(&s, &usb, 100) >= 0);
    CHECK(cp0_kbd_sources_live(&s) == 0);
    /* back from the app: only the USB keyboard is still listed */
    node_t after[1] = {"/dev/input/event8"};
    cp0_kbd_sources_plan_t p;
    plan(&s, after, 1, 1000, &p);
    CHECK(p.purge_count == 0 && p.add_count == 0);
    plan(&s, after, 1, 1600, &p);
    CHECK(p.purge_count == 2); /* both records dropped (libinput would retry the dead one at every resume) */
    CHECK(p.add_count == 1 && strcmp(after[p.add[0]], "/dev/input/event8") == 0);
    for (unsigned i = 0; i < p.purge_count; i++) cp0_kbd_sources_free(&s, p.purge[i]);
    CHECK(cp0_kbd_sources_track(&s, "/dev/input/event8", &usb2) >= 0);
    CHECK(readers(&s, "/dev/input/event8") == 1);
    CHECK(settled(&s, after, 1));
    (void)wanted;
}

static void test_duplicates_and_unknown(void)
{
    cp0_kbd_sources_t s;
    memset(&s, 0, sizeof(s));
    int a, b, c;
    void *release = NULL;
    CHECK(cp0_kbd_sources_track(&s, "/dev/input/event3", &a) >= 0);
    /* libinput (re)adds a second device for a node that is already read: drop it */
    CHECK(cp0_kbd_sources_on_added(&s, "/dev/input/event3", &b, &release) == CP0_KBD_ADDED_DUPLICATE);
    CHECK(readers(&s, "/dev/input/event3") == 1 && cp0_kbd_sources_find_dev(&s, &b) < 0);
    /* its removal event is not ours */
    CHECK(cp0_kbd_sources_on_removed(&s, &b, 10) < 0);
    CHECK(readers(&s, "/dev/input/event3") == 1);
    /* an added node nobody asked for is tracked so that its record can be dropped later */
    CHECK(cp0_kbd_sources_on_added(&s, "/dev/input/event7", &c, &release) == CP0_KBD_ADDED_NEW);
    CHECK(cp0_kbd_sources_find_node(&s, "/dev/input/event7") >= 0);
    /* the same device reported twice is ignored */
    CHECK(cp0_kbd_sources_on_added(&s, "/dev/input/event7", &c, &release) == CP0_KBD_ADDED_IGNORE);
}

static void test_rejected(void)
{
    cp0_kbd_sources_t s;
    memset(&s, 0, sizeof(s));
    node_t wanted[1] = {"/dev/input/event6"};
    cp0_kbd_sources_plan_t p;
    CHECK(cp0_kbd_sources_reject(&s, "/dev/input/event6") >= 0);
    plan(&s, wanted, 1, 0, &p);
    CHECK(p.add_count == 0 && p.purge_count == 0); /* not retried while the list is the same */
    CHECK(settled(&s, wanted, 1));
    plan(&s, wanted, 1, 100000, &p);
    CHECK(p.purge_count == 0);
    cp0_kbd_sources_clear_rejected(&s);
    plan(&s, wanted, 1, 0, &p);
    CHECK(p.add_count == 1);
}

static void test_capacity(void)
{
    cp0_kbd_sources_t s;
    memset(&s, 0, sizeof(s));
    static int devs[CP0_KBD_SOURCES_MAX + 1];
    node_t wanted[CP0_KBD_SOURCES_MAX + 3];
    for (int i = 0; i < CP0_KBD_SOURCES_MAX + 3; i++) snprintf(wanted[i], sizeof(wanted[i]), "/dev/input/event%d", i);
    cp0_kbd_sources_plan_t p;
    plan(&s, wanted, CP0_KBD_SOURCES_MAX + 3, 0, &p);
    CHECK(p.add_count == CP0_KBD_SOURCES_MAX); /* never more adds than free slots */
    for (int i = 0; i < CP0_KBD_SOURCES_MAX; i++) CHECK(cp0_kbd_sources_track(&s, wanted[i], &devs[i]) == i);
    CHECK(cp0_kbd_sources_track(&s, wanted[CP0_KBD_SOURCES_MAX], &devs[CP0_KBD_SOURCES_MAX]) < 0);
    void *release = NULL;
    CHECK(cp0_kbd_sources_on_added(&s, "/dev/input/event99", &devs[CP0_KBD_SOURCES_MAX], &release) ==
          CP0_KBD_ADDED_FULL);
    plan(&s, wanted, CP0_KBD_SOURCES_MAX + 3, 0, &p);
    CHECK(p.add_count == 0);
    /* a node name that does not fit is refused */
    char long_node[64];
    memset(long_node, 'n', sizeof(long_node) - 1);
    long_node[sizeof(long_node) - 1] = '\0';
    cp0_kbd_sources_t empty;
    memset(&empty, 0, sizeof(empty));
    CHECK(cp0_kbd_sources_track(&empty, long_node, &devs[0]) < 0);
    CHECK(cp0_kbd_sources_track(&empty, "", &devs[0]) < 0);
}

int main(void)
{
    test_deck_m4_only();
    test_sleep_during_app();
    test_duplicates_and_unknown();
    test_rejected();
    test_capacity();
    printf("test_keyboard_sources: PASS\n");
    return 0;
}
