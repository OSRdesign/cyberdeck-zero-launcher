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

/* The render harness: what the idle timer does when it finds the machine slept for that long (no clock involved). */
void resume_for_test(int slept_seconds);

/* External apps own the framebuffer: the saver is paused while one runs. */
void set_enabled(bool enabled);

/* A page that must stay in view keeps the saver away while held (Settings > Updates while the root update helper
 * runs: its progress, "do not switch off"); released, the idle interval starts again. */
void hold(bool held);

} // namespace native_screensaver
