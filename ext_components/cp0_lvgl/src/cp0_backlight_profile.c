/*
 * Backlight method of the board profile (see include/cp0_backlight_profile.h).
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* clock_gettime under -std=c11 */
#endif

#include "cp0_backlight_profile.h"
#include "cp0_capabilities.h"
#include "cp0_lvgl_app.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct {
    cp0_backlight_kind_t kind;
    char dir[256];
} bl;
static pthread_once_t bl_once = PTHREAD_ONCE_INIT;

#ifndef HAL_PLATFORM_SDL
/* No device found by the scan (not "none" in the profile, which is final): looked for again at most every
 * BL_RESCAN_MS, until one is there (a driver that came up after the launcher started). */
#define BL_RESCAN_MS 3000
static pthread_mutex_t bl_rescan_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_int bl_scan_found_none;
static long long bl_next_scan_ms;

static long long bl_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
#endif

/* "<prefix><dir>" with a non-empty dir that fits: the dir (trailing slashes trimmed) and the kind */
static int take_dir(const char *spec, const char *prefix, cp0_backlight_kind_t kind)
{
    const size_t len = strlen(prefix);
    if (strncmp(spec, prefix, len) != 0 || !spec[len] || strlen(spec + len) >= sizeof(bl.dir)) return 0;
    snprintf(bl.dir, sizeof(bl.dir), "%s", spec + len);
    size_t n = strlen(bl.dir);
    while (n > 1 && bl.dir[n - 1] == '/') bl.dir[--n] = '\0';
    bl.kind = kind;
    return 1;
}

#ifndef HAL_PLATFORM_SDL
/* No profile value: the device the kernel prefers (cp0_capabilities.h). A levels device named "backlight" (the deck's
 * PWM backlight) keeps the default path through the settings service, exactly as before detection existed. */
static void bl_detect(void)
{
    cp0_caps_backlight_t found;
    cp0_caps_backlight_at(NULL, NULL, &found);
    const char *slash = strrchr(found.dir, '/');
    const char *name = slash ? slash + 1 : found.dir;
    if (found.kind == CP0_BL_NONE) {
        bl.kind = CP0_BACKLIGHT_KIND_NONE;
    } else if (found.kind == CP0_BL_LEVELS && strcmp(name, "backlight") == 0) {
        bl.kind = CP0_BACKLIGHT_KIND_DEFAULT;
    } else {
        snprintf(bl.dir, sizeof(bl.dir), "%s", found.dir);
        bl.kind = found.kind == CP0_BL_ONOFF ? CP0_BACKLIGHT_KIND_GPIO_ONOFF : CP0_BACKLIGHT_KIND_SYSFS;
    }
    bl_next_scan_ms = bl_now_ms() + BL_RESCAN_MS;
    atomic_store(&bl_scan_found_none, bl.kind == CP0_BACKLIGHT_KIND_NONE);
}
#endif

static void bl_init(void)
{
    const char *spec = getenv("APPLAUNCH_BACKLIGHT");
    bl.kind = CP0_BACKLIGHT_KIND_DEFAULT;
    bl.dir[0] = '\0';
    if (spec && spec[0]) {
        if (take_dir(spec, "gpio:", CP0_BACKLIGHT_KIND_GPIO_ONOFF)) return;
        if (take_dir(spec, "sysfs:", CP0_BACKLIGHT_KIND_SYSFS)) return;
        if (strcmp(spec, "none") == 0) {
            bl.kind = CP0_BACKLIGHT_KIND_NONE;
            return;
        }
        fprintf(stderr, "[display-profile] APPLAUNCH_BACKLIGHT=%s is not supported yet: detected backlight\n", spec);
    }
#ifndef HAL_PLATFORM_SDL
    bl_detect();
#endif /* the simulator keeps the default levels (its settings service fakes them), never the host's own backlight */
}

static void bl_ready(void)
{
    pthread_once(&bl_once, bl_init);
#ifndef HAL_PLATFORM_SDL
    if (atomic_load(&bl_scan_found_none)) {
        pthread_mutex_lock(&bl_rescan_mutex);
        if (atomic_load(&bl_scan_found_none) && bl_now_ms() >= bl_next_scan_ms) bl_detect();
        pthread_mutex_unlock(&bl_rescan_mutex);
    }
#endif
}

static int read_int(const char *name, int *out)
{
    char path[300];
    snprintf(path, sizeof(path), "%s/%s", bl.dir, name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = 0;
    const int ok = fscanf(f, "%d", &v) == 1;
    fclose(f);
    if (!ok) return -1;
    *out = v;
    return 0;
}

static int write_int(const char *name, int value)
{
    char path[300];
    snprintf(path, sizeof(path), "%s/%s", bl.dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    const int written = fprintf(f, "%d", value);
    const int closed = fclose(f);
    return written > 0 && closed == 0 ? 0 : -1;
}

cp0_backlight_kind_t cp0_backlight_profile_kind(void)
{
    bl_ready();
    return bl.kind;
}

const char *cp0_backlight_profile_dir(void)
{
    bl_ready();
    return bl.dir;
}

int cp0_backlight_profile_get_max(void)
{
    bl_ready();
    if (bl.kind == CP0_BACKLIGHT_KIND_NONE) return -1;
    if (bl.kind == CP0_BACKLIGHT_KIND_DEFAULT) return cp0_backlight_max();
    int v = 0;
    if (read_int("max_brightness", &v) == 0 && v > 0) return v;
    return bl.kind == CP0_BACKLIGHT_KIND_GPIO_ONOFF ? 1 : 100; /* the settings service's fallbacks */
}

int cp0_backlight_profile_get_brightness(void)
{
    bl_ready();
    if (bl.kind == CP0_BACKLIGHT_KIND_NONE) return -1;
    if (bl.kind == CP0_BACKLIGHT_KIND_DEFAULT) return cp0_backlight_read();
    int v = 0;
    return read_int("brightness", &v) == 0 && v >= 0 ? v : -1;
}

int cp0_backlight_profile_is_on(void)
{
    const int v = cp0_backlight_profile_get_brightness();
    return v < 0 ? -1 : v > 0;
}

int cp0_backlight_profile_set_brightness(int value)
{
    bl_ready();
    if (bl.kind == CP0_BACKLIGHT_KIND_NONE) return -1;
    if (value < 0) value = 0;
    if (bl.kind == CP0_BACKLIGHT_KIND_DEFAULT) return cp0_backlight_write(value);
    const int max = cp0_backlight_profile_get_max();
    int level = value > max ? max : value;          /* sysfs: a level, clamped */
    if (bl.kind == CP0_BACKLIGHT_KIND_GPIO_ONOFF) level = value > 0 ? max : 0;
    return write_int("brightness", level) == 0 ? level : -1;
}

int cp0_backlight_profile_set_on(int on)
{
    return cp0_backlight_profile_set_brightness(on ? cp0_backlight_profile_get_max() : 0) < 0 ? -1 : 0;
}
