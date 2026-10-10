/*
 * Bookkeeping of the input nodes the launcher keyboard thread reads through its libinput path context.
 *
 * libinput keeps its own list of the paths it was given. libinput_suspend() removes every device (a
 * DEVICE_REMOVED event each) and libinput_resume() re-opens every path of that list (DEVICE_ADDED). A device
 * that disappears by itself (Bluetooth sleep, USB unplug) is removed too, but its path stays in libinput's
 * list until libinput_path_remove_device() is called on it. So:
 *   - a node that went away is NOT added again for CP0_KBD_SOURCES_GRACE_MS: libinput may bring it back
 *     itself after a resume, and a second add would deliver every key twice (the duplicated-keys bug);
 *   - after the grace, the thread drops libinput's record of it (libinput_path_remove_device on the reference
 *     it keeps) before reading the node again, so libinput's list never holds stale or doubled paths;
 *   - a device libinput (re)adds for a node another live device already reads is removed at once.
 * Devices are opaque pointers here, so this logic is unit-tested on a PC without libinput.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_KBD_SOURCES_MAX 17 /* the presence list (16) + the explicit LV_LINUX_KEYBOARD_DEVICE */
#define CP0_KBD_SOURCES_NODE_SIZE 32
#define CP0_KBD_SOURCES_GRACE_MS 1500u

typedef struct {
    int used;
    int rejected;     /* the backend refused the node (not a keyboard): skipped until the list changes */
    char node[CP0_KBD_SOURCES_NODE_SIZE]; /* "/dev/input/eventN" */
    void *dev;        /* live device, NULL while it is away */
    void *ref;        /* the caller's reference on the last device of this node, to drop its record later */
    uint64_t gone_ms; /* when dev became NULL */
} cp0_kbd_source_t;

typedef struct {
    cp0_kbd_source_t slot[CP0_KBD_SOURCES_MAX];
} cp0_kbd_sources_t;

typedef struct {
    unsigned purge_count;
    int purge[CP0_KBD_SOURCES_MAX];      /* slots to drop: release the backend record of slot.ref, then free */
    unsigned add_count;
    unsigned add[CP0_KBD_SOURCES_MAX];   /* indexes into desired[] of nodes to open */
} cp0_kbd_sources_plan_t;

enum {
    CP0_KBD_ADDED_IGNORE = 0, /* our own add: already tracked with this device */
    CP0_KBD_ADDED_ADOPT,      /* the backend re-added a tracked node (resume): take a reference on dev and
                                 release *release_ref when it is not NULL */
    CP0_KBD_ADDED_NEW,        /* untracked node: now tracked, take a reference on dev */
    CP0_KBD_ADDED_DUPLICATE,  /* another live device already reads this node: remove dev */
    CP0_KBD_ADDED_FULL,       /* no free slot: remove dev */
};

int cp0_kbd_sources_find_node(const cp0_kbd_sources_t *s, const char *node);
int cp0_kbd_sources_find_dev(const cp0_kbd_sources_t *s, const void *dev);
/* The caller added `node` itself and got `dev` (and took a reference on it). Returns the slot or -1 (full). */
int cp0_kbd_sources_track(cp0_kbd_sources_t *s, const char *node, void *dev);
/* `node` is not a keyboard for the backend: skip it until cp0_kbd_sources_clear_rejected(). */
int cp0_kbd_sources_reject(cp0_kbd_sources_t *s, const char *node);
void cp0_kbd_sources_clear_rejected(cp0_kbd_sources_t *s);
/* DEVICE_ADDED for `node`. Returns a CP0_KBD_ADDED_* value. */
int cp0_kbd_sources_on_added(cp0_kbd_sources_t *s, const char *node, void *dev, void **release_ref);
/* DEVICE_REMOVED: returns the slot whose device went away (its grace starts now), or -1 if not ours. */
int cp0_kbd_sources_on_removed(cp0_kbd_sources_t *s, const void *dev, uint64_t now_ms);
/* One reconcile pass against the wanted nodes (duplicates in `desired` are fine). */
void cp0_kbd_sources_plan(const cp0_kbd_sources_t *s, const char (*desired)[CP0_KBD_SOURCES_NODE_SIZE],
                          unsigned desired_count, uint64_t now_ms, uint64_t grace_ms,
                          cp0_kbd_sources_plan_t *plan);
/* 1 when every wanted node is open (or rejected) and no removed device waits out its grace: nothing to retry
 * until the wanted list changes. */
int cp0_kbd_sources_settled(const cp0_kbd_sources_t *s, const char (*desired)[CP0_KBD_SOURCES_NODE_SIZE],
                            unsigned desired_count);
void cp0_kbd_sources_free(cp0_kbd_sources_t *s, int slot);
/* Number of slots with a live device. */
unsigned cp0_kbd_sources_live(const cp0_kbd_sources_t *s);

#ifdef __cplusplus
}
#endif
