/*
 * Backlight method of the board profile (APPLAUNCH_BACKLIGHT, read once on first use):
 *   gpio:<sysfs dir>       on/off only, e.g. gpio:/sys/class/backlight/backlight_gpio
 *                          (writes <dir>/brightness = 0 or <dir>/max_brightness)
 *   sysfs:<sysfs dir>      brightness levels in that directory, e.g. sysfs:/sys/class/backlight/intel_backlight
 *                          or sysfs:/sys/class/backlight/10-0045 (writes <dir>/brightness = 0 .. max_brightness)
 *   none                   no backlight to drive (a board whose only backlight device drives nothing)
 *   unset                  detected (report 030, cp0_caps_backlight() in cp0_capabilities.h: the kernel's preferred
 *                          device of /sys/class/backlight; the SDL simulator keeps the default):
 *                          - a device named "backlight" with levels: the default, today's brightness levels through
 *                            cp0_backlight_read/max/write (/sys/class/backlight/backlight, the deck's PWM backlight);
 *                          - another device with levels: as sysfs:<its dir>;
 *                          - max_brightness 1 (a gpio-backlight, the Pi 5 HyperPixel): as gpio:<its dir>, on/off;
 *                          - no device: none, looked for again at most every 3 s until one appears (a driver
 *                            that comes up after the launcher started).
 * Other values (pwm) are not supported yet and are detected as if unset.
 * The settings service (BacklightRead/Max/Write) uses the same directory for the gpio and sysfs kinds, so every
 * brightness client (Settings, hotkeys, screensaver dim, sleep, startup restore) follows the profile.
 * Writing needs permission on <dir>/brightness; when it is missing the calls just fail (-1). With no backlight (none)
 * every read and write fails (-1), and Settings has no Brightness row.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CP0_BACKLIGHT_KIND_DEFAULT = 0,    /* brightness levels (today's behaviour) */
    CP0_BACKLIGHT_KIND_GPIO_ONOFF = 1, /* on/off only */
    CP0_BACKLIGHT_KIND_SYSFS = 2,      /* brightness levels in a sysfs backlight directory */
    CP0_BACKLIGHT_KIND_NONE = 3,       /* no backlight (none, or no device found) */
} cp0_backlight_kind_t;

cp0_backlight_kind_t cp0_backlight_profile_kind(void);
/* The sysfs directory of a gpio or sysfs backlight, "" for the default kind. */
const char *cp0_backlight_profile_dir(void);
/* 1 on, 0 off, -1 unknown. */
int cp0_backlight_profile_is_on(void);
/* 0 done, -1 failed. The default and sysfs kinds write 0 or their maximum brightness. */
int cp0_backlight_profile_set_on(int on);
/* Current level (gpio: 0 or max), -1 unknown. */
int cp0_backlight_profile_get_brightness(void);
/* Maximum level (gpio: max_brightness, normally 1; sysfs: max_brightness, 100 when unreadable). */
int cp0_backlight_profile_get_max(void);
/* Default and sysfs kinds: the level (clamped to 0..max). Gpio: > 0 = on, 0 = off. Returns the level set, -1 on
 * failure. */
int cp0_backlight_profile_set_brightness(int value);

/* The level the launcher sets once when it starts (systemd-backlight brings back the level the last session ended
 * with, which may be dim): the brightness saved by Settings > Brightness (`saved`: the raw level kept under the
 * launcher config key "brightness", <= 0 when there is none), clamped to `max`; with nothing saved, `max` when the
 * current level is below half of it or unknown (-1). Returns -1 to leave the backlight alone: no backlight, on/off only
 * (gpio, or max_brightness 1), already at the saved level, or nothing saved and already at least half. Pure, no I/O. */
static inline int cp0_backlight_startup_level(cp0_backlight_kind_t kind, int max, int current, int saved)
{
    if (kind == CP0_BACKLIGHT_KIND_NONE || kind == CP0_BACKLIGHT_KIND_GPIO_ONOFF || max <= 1) return -1;
    if (saved > 0) {
        const int level = saved < max ? saved : max;
        return level == current ? -1 : level;
    }
    if (current >= 0 && (long long)current * 2 >= max) return -1;
    return max;
}

#ifdef __cplusplus
}
#endif
