/*
 * SPDX-License-Identifier: MIT
 *
 * evdev helpers. See evdev_util.h.
 */
#define _GNU_SOURCE
#include "evdev_util.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define NBITS(x) ((((x) - 1) / (8 * sizeof(unsigned long))) + 1)
static int has_bit(const unsigned long *bits, int bit)
{
    return (bits[bit / (8 * sizeof(unsigned long))] >> (bit % (8 * sizeof(unsigned long)))) & 1UL;
}

void ev_name(int type, int code, char *out, size_t n)
{
    static const char *types[] = {"EV_SYN", "EV_KEY", "EV_REL", "EV_ABS", "EV_MSC", "EV_SW"};
    const char *t = (type >= 0 && type < 6) ? types[type] : NULL;
    const char *c = NULL;
    if (type == EV_SYN) {
        switch (code) {
        case SYN_REPORT: c = "SYN_REPORT"; break;
        case SYN_CONFIG: c = "SYN_CONFIG"; break;
        case SYN_MT_REPORT: c = "SYN_MT_REPORT"; break;
        case SYN_DROPPED: c = "SYN_DROPPED"; break;
        default: break;
        }
    } else if (type == EV_ABS) {
        switch (code) {
        case ABS_X: c = "ABS_X"; break;
        case ABS_Y: c = "ABS_Y"; break;
        case ABS_PRESSURE: c = "ABS_PRESSURE"; break;
        case ABS_MT_SLOT: c = "ABS_MT_SLOT"; break;
        case ABS_MT_TOUCH_MAJOR: c = "ABS_MT_TOUCH_MAJOR"; break;
        case ABS_MT_TOUCH_MINOR: c = "ABS_MT_TOUCH_MINOR"; break;
        case ABS_MT_WIDTH_MAJOR: c = "ABS_MT_WIDTH_MAJOR"; break;
        case ABS_MT_WIDTH_MINOR: c = "ABS_MT_WIDTH_MINOR"; break;
        case ABS_MT_ORIENTATION: c = "ABS_MT_ORIENTATION"; break;
        case ABS_MT_POSITION_X: c = "ABS_MT_POSITION_X"; break;
        case ABS_MT_POSITION_Y: c = "ABS_MT_POSITION_Y"; break;
        case ABS_MT_TOOL_TYPE: c = "ABS_MT_TOOL_TYPE"; break;
        case ABS_MT_TRACKING_ID: c = "ABS_MT_TRACKING_ID"; break;
        case ABS_MT_PRESSURE: c = "ABS_MT_PRESSURE"; break;
        default: break;
        }
    } else if (type == EV_KEY) {
        switch (code) {
        case BTN_TOUCH: c = "BTN_TOUCH"; break;
        case BTN_TOOL_FINGER: c = "BTN_TOOL_FINGER"; break;
        case BTN_TOOL_DOUBLETAP: c = "BTN_TOOL_DOUBLETAP"; break;
        case BTN_TOOL_TRIPLETAP: c = "BTN_TOOL_TRIPLETAP"; break;
        case BTN_TOOL_QUADTAP: c = "BTN_TOOL_QUADTAP"; break;
        case BTN_TOOL_QUINTTAP: c = "BTN_TOOL_QUINTTAP"; break;
        case KEY_ENTER: c = "KEY_ENTER"; break;
        case KEY_KPENTER: c = "KEY_KPENTER"; break;
        case KEY_ESC: c = "KEY_ESC"; break;
        case KEY_TAB: c = "KEY_TAB"; break;
        case KEY_LEFTALT: c = "KEY_LEFTALT"; break;
        case KEY_SPACE: c = "KEY_SPACE"; break;
        default: break;
        }
    } else if (type == EV_MSC && code == MSC_TIMESTAMP) {
        c = "MSC_TIMESTAMP";
    }
    char tb[16], cb[16];
    if (!t) { snprintf(tb, sizeof(tb), "EV_%d", type); t = tb; }
    if (!c) { snprintf(cb, sizeof(cb), "0x%x", code); c = cb; }
    snprintf(out, n, "%s:%s", t, c);
}

int ev_is_evdev(int fd)
{
    int version = 0;
    return ioctl(fd, EVIOCGVERSION, &version) == 0;
}

static int contains_nocase(const char *hay, const char *needle)
{
    const size_t n = strlen(needle);
    for (; *hay; ++hay)
        if (strncasecmp(hay, needle, n) == 0) return 1;
    return 0;
}

int ev_touch_score(int fd, char *name, size_t name_n)
{
    unsigned long ev[NBITS(EV_CNT)] = {0}, abs_bits[NBITS(ABS_CNT)] = {0};
    unsigned long key[NBITS(KEY_CNT)] = {0}, prop[NBITS(INPUT_PROP_CNT)] = {0};
    struct input_id id;
    memset(name, 0, name_n);
    if (ioctl(fd, EVIOCGNAME(name_n - 1), name) < 0) name[0] = '\0';
    if (ioctl(fd, EVIOCGID, &id) == 0 && id.bustype == BUS_VIRTUAL) return -1;
    if (contains_nocase(name, "applaunch")) return -1;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0 || !has_bit(ev, EV_ABS)) return -1;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) return -1;
    if (has_bit(ev, EV_KEY)) (void)ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key);
    (void)ioctl(fd, EVIOCGPROP(sizeof(prop)), prop);
    const int mt = has_bit(abs_bits, ABS_MT_POSITION_X) && has_bit(abs_bits, ABS_MT_POSITION_Y);
    const int st = has_bit(abs_bits, ABS_X) && has_bit(abs_bits, ABS_Y);
    const int direct = has_bit(prop, INPUT_PROP_DIRECT);
    const int btn_touch = has_bit(key, BTN_TOUCH);
    if (!mt && !st) return -1;
    if (!direct && !btn_touch) return -1;
    if (!direct && has_bit(prop, INPUT_PROP_POINTER)) return -1;
    return (direct ? 4 : 0) + (mt ? 2 : 0) + (btn_touch ? 1 : 0);
}

static int event_num(const char *d_name)
{
    if (strncmp(d_name, "event", 5) != 0) return -1;
    char *end = NULL;
    const long num = strtol(d_name + 5, &end, 10);
    if (end == d_name + 5 || *end != '\0' || num < 0 || num > 1023) return -1;
    return (int)num;
}

int ev_find_touch(char *path, size_t n, char *name, size_t name_n)
{
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e;
    int best_score = -1, best_num = -1;
    while ((e = readdir(d)) != NULL) {
        const int num = event_num(e->d_name);
        if (num < 0) continue;
        char p[64], dev_name[128];
        snprintf(p, sizeof(p), "/dev/input/event%d", num);
        int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        const int score = ev_touch_score(fd, dev_name, sizeof(dev_name));
        close(fd);
        if (score > best_score || (score == best_score && score >= 0 && num < best_num)) {
            best_score = score;
            best_num = num;
            snprintf(path, n, "%s", p);
            snprintf(name, name_n, "%s", dev_name);
        }
    }
    closedir(d);
    return best_score >= 0 ? 0 : -1;
}

int ev_is_keyboard(int fd)
{
    unsigned long ev[NBITS(EV_CNT)] = {0}, key[NBITS(KEY_CNT)] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0 || !has_bit(ev, EV_KEY)) return 0;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) < 0) return 0;
    return has_bit(key, KEY_ENTER) || has_bit(key, KEY_A);
}

int ev_list_inputs(char paths[][64], int max, int also_touch)
{
    DIR *d = opendir("/dev/input");
    if (!d) return 0;
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < max) {
        const int num = event_num(e->d_name);
        if (num < 0) continue;
        char p[64], name[128];
        snprintf(p, sizeof(p), "/dev/input/event%d", num);
        int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        const int take = ev_is_keyboard(fd) || (also_touch && ev_touch_score(fd, name, sizeof(name)) >= 0);
        close(fd);
        if (take) snprintf(paths[count++], 64, "%s", p);
    }
    closedir(d);
    return count;
}

void ev_print_inputs(void)
{
    DIR *d = opendir("/dev/input");
    if (!d) {
        printf("no /dev/input\n");
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const int num = event_num(e->d_name);
        if (num < 0) continue;
        char p[64], name[128] = "";
        snprintf(p, sizeof(p), "/dev/input/event%d", num);
        int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            printf("%-20s (cannot open: not in group input?)\n", p);
            continue;
        }
        const int score = ev_touch_score(fd, name, sizeof(name));
        const int kbd = ev_is_keyboard(fd);
        int slots = 0;
        struct input_absinfo ai;
        if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &ai) == 0) slots = ai.maximum + 1;
        close(fd);
        printf("%-20s touch_score=%-2d keyboard=%d slots=%-2d \"%s\"\n", p, score, kbd, slots, name);
    }
    closedir(d);
}
