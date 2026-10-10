/*
 * Virtual keyboard hub for stock (framebuffer) apps on the Raspberry Pi Zero 2W port.
 *
 * Stock apps read the keyboard device themselves, so keys that exist only inside the launcher (the
 * on-screen toolbar) never reach them. While a stock app runs, the launcher forwards the physical
 * keyboard and the toolbar taps into one uinput device ("applaunch-vkbd", exposed as
 * /dev/input/applaunch-vkbd by a udev rule) and points the app at it.
 */
#pragma once

#include <string>

namespace native_vkbd {

/* Creates the uinput device once. True when it exists and its device node is available. */
bool ensure();

/* The device node an app should read (LV_LINUX_KEYBOARD_DEVICE). */
const char *device_path();

/* Emit a key press (value 1) or release (value 0) into the virtual keyboard. */
void send(unsigned short code, int value);

/* Mirror the physical keyboard's key events into the virtual one until stop_forwarding(): `physical_device`
 * (LV_LINUX_KEYBOARD_DEVICE) and, when the launcher reads all keyboards (cp0_keyboard_get_read_all_keyboards),
 * every real keyboard, including ones plugged in or woken while the app runs. Keys still held by a keyboard
 * that disappears are released in the virtual one. */
void start_forwarding(const std::string &physical_device);
void stop_forwarding();

} // namespace native_vkbd
