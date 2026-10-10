/*
 * Keyboard thread source bookkeeping (see cp0_keyboard_sources.h).
 */

#include "cp0_keyboard_sources.h"

#include <stdio.h>
#include <string.h>

int cp0_kbd_sources_find_node(const cp0_kbd_sources_t *s, const char *node)
{
    if (!s || !node || !node[0]) return -1;
    for (int i = 0; i < CP0_KBD_SOURCES_MAX; i++)
        if (s->slot[i].used && strcmp(s->slot[i].node, node) == 0) return i;
    return -1;
}

int cp0_kbd_sources_find_dev(const cp0_kbd_sources_t *s, const void *dev)
{
    if (!s || !dev) return -1;
    for (int i = 0; i < CP0_KBD_SOURCES_MAX; i++)
        if (s->slot[i].used && s->slot[i].dev == dev) return i;
    return -1;
}

static int free_slot(const cp0_kbd_sources_t *s)
{
    for (int i = 0; i < CP0_KBD_SOURCES_MAX; i++)
        if (!s->slot[i].used) return i;
    return -1;
}

static int claim(cp0_kbd_sources_t *s, const char *node)
{
    if (!node || !node[0] || strlen(node) >= CP0_KBD_SOURCES_NODE_SIZE) return -1;
    const int i = free_slot(s);
    if (i < 0) return -1;
    memset(&s->slot[i], 0, sizeof(s->slot[i]));
    s->slot[i].used = 1;
    snprintf(s->slot[i].node, sizeof(s->slot[i].node), "%s", node);
    return i;
}

int cp0_kbd_sources_track(cp0_kbd_sources_t *s, const char *node, void *dev)
{
    if (!s || !dev) return -1;
    const int i = claim(s, node);
    if (i < 0) return -1;
    s->slot[i].dev = dev;
    s->slot[i].ref = dev;
    return i;
}

int cp0_kbd_sources_reject(cp0_kbd_sources_t *s, const char *node)
{
    if (!s) return -1;
    int i = cp0_kbd_sources_find_node(s, node);
    if (i < 0) i = claim(s, node);
    if (i < 0) return -1;
    s->slot[i].rejected = 1;
    return i;
}

void cp0_kbd_sources_clear_rejected(cp0_kbd_sources_t *s)
{
    if (!s) return;
    for (int i = 0; i < CP0_KBD_SOURCES_MAX; i++)
        if (s->slot[i].used && s->slot[i].rejected && !s->slot[i].dev && !s->slot[i].ref)
            cp0_kbd_sources_free(s, i);
}

int cp0_kbd_sources_on_added(cp0_kbd_sources_t *s, const char *node, void *dev, void **release_ref)
{
    if (release_ref) *release_ref = NULL;
    if (!s || !dev || cp0_kbd_sources_find_dev(s, dev) >= 0) return CP0_KBD_ADDED_IGNORE;
    const int i = cp0_kbd_sources_find_node(s, node);
    if (i >= 0) {
        cp0_kbd_source_t *slot = &s->slot[i];
        if (slot->dev == dev) return CP0_KBD_ADDED_IGNORE;
        if (slot->dev) return CP0_KBD_ADDED_DUPLICATE;
        if (release_ref && slot->ref != dev) *release_ref = slot->ref;
        slot->dev = dev;
        slot->ref = dev;
        slot->rejected = 0;
        return CP0_KBD_ADDED_ADOPT;
    }
    return cp0_kbd_sources_track(s, node, dev) >= 0 ? CP0_KBD_ADDED_NEW : CP0_KBD_ADDED_FULL;
}

int cp0_kbd_sources_on_removed(cp0_kbd_sources_t *s, const void *dev, uint64_t now_ms)
{
    const int i = cp0_kbd_sources_find_dev(s, dev);
    if (i < 0) return -1;
    s->slot[i].dev = NULL;
    s->slot[i].gone_ms = now_ms;
    return i;
}

void cp0_kbd_sources_plan(const cp0_kbd_sources_t *s, const char (*desired)[CP0_KBD_SOURCES_NODE_SIZE],
                          unsigned desired_count, uint64_t now_ms, uint64_t grace_ms,
                          cp0_kbd_sources_plan_t *plan)
{
    memset(plan, 0, sizeof(*plan));
    int purged[CP0_KBD_SOURCES_MAX] = {0};
    for (int i = 0; i < CP0_KBD_SOURCES_MAX; i++) {
        const cp0_kbd_source_t *slot = &s->slot[i];
        if (!slot->used || slot->rejected || slot->dev) continue;
        if (now_ms - slot->gone_ms < grace_ms) continue; /* libinput may still bring it back itself */
        purged[i] = 1;
        plan->purge[plan->purge_count++] = i;
    }
    unsigned free_slots = 0;
    for (int i = 0; i < CP0_KBD_SOURCES_MAX; i++)
        if (!s->slot[i].used || purged[i]) free_slots++;

    for (unsigned d = 0; d < desired_count && plan->add_count < free_slots; d++) {
        const char *node = desired[d];
        if (!node[0]) continue;
        int seen = 0;
        for (unsigned e = 0; e < d && !seen; e++) seen = strcmp(desired[e], node) == 0;
        if (seen) continue;
        int covered = 0;
        for (int i = 0; i < CP0_KBD_SOURCES_MAX && !covered; i++)
            covered = s->slot[i].used && !purged[i] && strcmp(s->slot[i].node, node) == 0;
        if (!covered) plan->add[plan->add_count++] = d;
    }
}

int cp0_kbd_sources_settled(const cp0_kbd_sources_t *s, const char (*desired)[CP0_KBD_SOURCES_NODE_SIZE],
                            unsigned desired_count)
{
    for (int i = 0; i < CP0_KBD_SOURCES_MAX; i++)
        if (s->slot[i].used && !s->slot[i].rejected && !s->slot[i].dev) return 0; /* a grace is running */
    for (unsigned d = 0; d < desired_count; d++)
        if (desired[d][0] && cp0_kbd_sources_find_node(s, desired[d]) < 0) return 0; /* not open yet */
    return 1;
}

void cp0_kbd_sources_free(cp0_kbd_sources_t *s, int slot)
{
    if (!s || slot < 0 || slot >= CP0_KBD_SOURCES_MAX) return;
    memset(&s->slot[slot], 0, sizeof(s->slot[slot]));
}

unsigned cp0_kbd_sources_live(const cp0_kbd_sources_t *s)
{
    unsigned n = 0;
    for (int i = 0; s && i < CP0_KBD_SOURCES_MAX; i++)
        if (s->slot[i].used && s->slot[i].dev) n++;
    return n;
}
