/*
 * Backlight method of the board profile (APPLAUNCH_BACKLIGHT, read once on first use):
 *   unset                  default: today's brightness levels through cp0_backlight_read/max/write
 *                          (/sys/class/backlight/backlight on the deck)
 *   gpio:<sysfs dir>       on/off only, e.g. gpio:/sys/class/backlight/backlight_gpio
 *                          (writes <dir>/brightness = 0 or <dir>/max_brightness)
 * Other values (sysfs:<dir>, pwm) are not supported yet and fall back to the default.
 * Writing needs permission on <dir>/brightness; when it is missing the calls just fail (-1).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CP0_BACKLIGHT_KIND_DEFAULT = 0,    /* brightness levels (today's behaviour) */
    CP0_BACKLIGHT_KIND_GPIO_ONOFF = 1, /* on/off only */
} cp0_backlight_kind_t;

cp0_backlight_kind_t cp0_backlight_profile_kind(void);
/* The sysfs directory of a gpio backlight, "" for the default kind. */
const char *cp0_backlight_profile_dir(void);
/* 1 on, 0 off, -1 unknown. */
int cp0_backlight_profile_is_on(void);
/* 0 done, -1 failed. The default kind writes 0 or its maximum brightness. */
int cp0_backlight_profile_set_on(int on);
/* Current level (gpio: 0 or max), -1 unknown. */
int cp0_backlight_profile_get_brightness(void);
/* Maximum level (gpio: max_brightness, normally 1). */
int cp0_backlight_profile_get_max(void);
/* Default kind: the level (clamped). Gpio: > 0 = on, 0 = off. Returns the level set, -1 on failure. */
int cp0_backlight_profile_set_brightness(int value);

#ifdef __cplusplus
}
#endif
