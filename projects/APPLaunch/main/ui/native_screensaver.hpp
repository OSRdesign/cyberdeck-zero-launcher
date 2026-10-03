/*
 * Clock screensaver for the Raspberry Pi Zero 2W + Waveshare 2.8" DPI LCD port.
 *
 * After the "DarkTime" timeout the backlight is dimmed to 10 % and the whole panel shows a big
 * white 24 h clock (HH:MM) with the date (dd/mm/yyyy) underneath on black. Any touch or key press
 * wakes it: the brightness is restored, the screen underneath is shown exactly as it was, and the
 * waking touch/key does nothing else.
 *
 * Replaces the stock lock screen when the dpi-scaled display backend is active; a no-op otherwise.
 */
#pragma once

struct key_item;

namespace native_screensaver {

void init();
void shutdown();

/* A keyboard backend hook (LVGL thread). Returns true when the key must be consumed. */
bool filter_key(const struct key_item *item);

bool active();

/* External apps own the framebuffer: the saver is paused while one runs. */
void set_enabled(bool enabled);

} // namespace native_screensaver
