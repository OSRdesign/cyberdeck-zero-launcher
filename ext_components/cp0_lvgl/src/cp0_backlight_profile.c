/*
 * Backlight method of the board profile (see include/cp0_backlight_profile.h).
 */

#include "cp0_backlight_profile.h"
#include "cp0_lvgl_app.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    cp0_backlight_kind_t kind;
    char dir[256];
} bl;
static pthread_once_t bl_once = PTHREAD_ONCE_INIT;

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

static void bl_init(void)
{
    const char *spec = getenv("APPLAUNCH_BACKLIGHT");
    bl.kind = CP0_BACKLIGHT_KIND_DEFAULT;
    bl.dir[0] = '\0';
    if (!spec || !spec[0]) return;
    if (take_dir(spec, "gpio:", CP0_BACKLIGHT_KIND_GPIO_ONOFF)) return;
    if (take_dir(spec, "sysfs:", CP0_BACKLIGHT_KIND_SYSFS)) return;
    fprintf(stderr, "[display-profile] APPLAUNCH_BACKLIGHT=%s is not supported yet: default backlight\n", spec);
}

static void bl_ready(void)
{
    pthread_once(&bl_once, bl_init);
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
    if (bl.kind == CP0_BACKLIGHT_KIND_DEFAULT) return cp0_backlight_max();
    int v = 0;
    if (read_int("max_brightness", &v) == 0 && v > 0) return v;
    return bl.kind == CP0_BACKLIGHT_KIND_GPIO_ONOFF ? 1 : 100; /* the settings service's fallbacks */
}

int cp0_backlight_profile_get_brightness(void)
{
    bl_ready();
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
